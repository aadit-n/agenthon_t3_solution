This solution speeds up the organizer's stock-exchange simulator while preserving its orders, trades, agent reactions, and message timing. Existing fast simulators informed the design, but their speed claims do not establish correctness or speed on Agenthon's workloads. This review covered the main publicly discoverable implementation families on October 10, 2026; it cannot establish that every existing or private solution has been found.

## Existing implementations and research

| Approach | Primary source | What it contributes | Decision for this submission |
|---|---|---|---|
| ABIDES | [Pinned JPMorgan source](https://github.com/jpmorganchase/abides-jpmc-public/tree/f9cbe51342b7dedd9587e4e069040d68a5c6477f), [original paper](https://arxiv.org/abs/1904.12066) | Message-driven agent simulation and exchange semantics | Preserve the pinned implementation and all four organizer patches; accelerate execution and output collection. |
| MAXE | [C++ implementation](https://github.com/maxe-team/maxe), [paper](https://arxiv.org/abs/2008.07871) | Efficient compiled multi-agent event simulation with a Python interface | Supports the choice of compiled event processing. Porting this track's agents, random streams, and protocol onto MAXE would still require an exact compatibility implementation. |
| JAX-LOB | [paper](https://arxiv.org/abs/2308.13289), [implementation](https://github.com/KangOxford/jax-lob) | GPU order-book processing and parallel simulation for reinforcement learning | Useful for many independent books. Its interface alone does not reproduce ABIDES agent callbacks, seeded latency, wakeups, or causal ledger. |
| AlphaTrade | [repository](https://github.com/KangOxford/AlphaTrade) | Related JAX-LOB trading environments and GPU implementation | Same compatibility work as JAX-LOB; GPU batching is not automatically safe within one causal market. |
| Bourse | [repository](https://github.com/zombie-einstein/bourse), [discrete-event API](https://docs.rs/bourse-de/latest/bourse_de/) | Rust order book and event simulation with Python API | Strong architectural alternative; substituting it would change the engine unless price-time, RNG, agents, and reports were ported and verified. |
| ABIDES Rust | [repository](https://github.com/mariotrerotola/abides-rs) | Rust agent-based market simulator | A useful reference, but its stated CLI and configuration system differ from this track. No published evidence found of passing Agenthon's exact public suite. |
| OrderBook-rs | [repository](https://github.com/joaquinbejar/OrderBook-rs) | Rust matching-engine data structures | An order book is only part of the task. Concurrent processing is particularly sensitive to deterministic tie ordering here. |
| HftBacktest | [repository](https://github.com/nkaz001/hftbacktest) | Efficient tick replay, latency and queue-position models | Good backtesting infrastructure; historical replay is a different workload from agents generating new order flow endogenously. |
| Cython | [pure Python compilation documentation](https://docs.cython.org/en/latest/src/tutorial/pure.html), [build documentation](https://docs.cython.org/en/latest/src/userguide/source_files_and_compilation.html) | Ahead-of-time compilation that integrates with Python objects and NumPy | Selected. It preserves the existing Python/NumPy semantics and avoids a full cross-language RNG/protocol rewrite. |

Searches included Agenthon-specific solutions and forks, ABIDES acceleration, C++/Rust event simulators, GPU limit-order-book implementations, and replay engines. The organizer's internal vectorized simulator is not published, so it is not available to reuse. No independently verified, ready-to-submit compatible solution was found in these searches. These conclusions are inferences from the reviewed interfaces and published material, not benchmark results for those projects.

## Original language and architecture

The authored engine and helpers use Python syntax compiled to C by Cython. The runtime uses Python 3.11 with NumPy 1.26.4 and pandas 1.5.3, matching the organizer's reference stack. The scoring toolkit runs separately on its required Python 3.13. This keeps random draws and integer nanosecond timestamps compatible while removing Python dispatch overhead from hot modules.

The first baseline profile identified substantial time in diagnostic timestamp formatting, generic deepcopy, scalar NumPy clipping, PriorityQueue synchronization, and pandas log flattening. Consequently the implementation:

1. Removes disabled debug/info formatting at build time, without removing exchange event logging.
2. Uses the same heap tuple ordering without queue locks and caches agent callbacks in the dispatcher.
3. Copies scalar order records directly, retaining deep copies of mutable tags.
4. Buffers draws from the independent latency RandomState; scalar/vector stream equality is tested across refills for every latency model.
5. Records the canonical event columns directly instead of creating and flattening unrelated debugging logs.
6. Writes typed, deterministic Snappy Parquet, preserving nullable integer metadata so pandas cannot round nanosecond timestamps.
7. Runs independent batch markets in up to four separate processes, resetting IDs and RNGs per market. It never parallelizes dependent events within a market.
8. Compiles extensions and Python bytecode at image build time, avoiding JIT or source compilation in timed runs.

An additional cached-depth implementation is available for experiments through `T3SIM_DEPTH_CACHE=1`. Randomized differential tests cover partial fills, quantity changes, removals, hidden orders, FIFO ordering, and price-to-comply fallback. A local two-repeat ablation showed no consistent gain on ordinary and deep-book workloads, so the default leaves it disabled. SciPy's spatial utilities load only if the fallback geographic latency model is actually requested; configured latency distributions avoid that startup cost.

GPU use is optional under the published contract. This implementation reports zero GPU seconds. Using a GPU for these relatively small, causally dependent markets would introduce initialization and synchronization costs plus compatibility work. A GPU implementation may outperform this design on a much larger independent-market batch; that has not been measured here. Likewise this work does not prove a global optimum over languages or algorithms.

## Correctness and performance evidence

The public validator uses the installed, pinned `qfbench2-common` v2.5.1 and imports the track's own verifier directly. It also requires exact column values for both Parquet files, exceeding the statistical tolerance on Tier B scenarios. It checks the public manifests and firewall through the official Linux functions. Reference data remain outside the submission image and outside simulation input mounts.

Validation outputs and repeat benchmarks are written under `results/`; `README.md` records the final results. Repeated traces and ledgers must have identical bytes. Local timings use Docker's `State.StartedAt` to `State.FinishedAt`, with one warmup excluded and alternating sequential image order. They are local measurements, not official Final scores or measurements on the competition's pinned instance.

The initial v1 default passed 71 public units, including exact exchange and ledger values for 95 markets, with 2.51x, 3.28x and 4.32x improvements over the original baseline. V2 replaces the expensive Python-object paths with native market state; its current evidence is described below and in the README.

The official timing release has a documented repeat-binding issue: [organizer issue #5](https://github.com/Agenthon-2026/track3-simulation-public/issues/5). The solution keeps timing sidecars truthful and stable outputs deterministic; it does not replace real elapsed times with constants.

## Native v2, guided by profiling

The prepared-source CPU profile in `results/profiling-v1/cpu.txt` attributes most work to dispatch, exchange/order-book calls, message creation and ledger dictionaries. Cython compilation of dynamic Python objects retained those costs. V2 therefore uses C++17 scalar records, a heap with the original tuple comparator, sorted price maps, FIFO lists, an order-ID index, cached depth totals and compact trace/ledger vectors. Typed Cython integrates them with the exact reference oracle.

Native NoiseTrader draws use NumPy's actual MT19937 bit-generator callbacks and the legacy Gaussian/masked integer behavior. NumPy deliberately retains [legacy RandomState distributions](https://github.com/numpy/numpy/blob/v1.26.4/numpy/random/src/legacy/legacy-distributions.c) to preserve streams. The differential test covers 180,000 normal/buy/offset triplets, Gaussian cache states and 32/64-bit bounds. Floating-point contraction is disabled for the native module and fast-math is not used.

Configuration preserves the original global/private draw order. The sparse oracle's numerically unchanged methods are emitted from the vendored source without unused dense-oracle, pandas or kernel imports. Direct Arrow buffers avoid importing pandas while retaining its nullable integer metadata. The [Arrow 15 writer](https://arrow.apache.org/docs/15.0/python/generated/pyarrow.parquet.write_table.html) supports the string dictionaries and delta-packed integer columns used here. Parallel independent file writes and this layout improved large-case throughput by approximately 37% and 41%, respectively, in alternating three-repeat ablations.

V2 again passes 71/71 units and all 95 markets exactly, plus six unseen-seed/parameter scenarios. Its local public single-pass mean is 428,466 self-reported events/s versus 42,521 for v1. Fresh same-machine startup-inclusive repeats show 2.60x, 4.08x, 26.68x and 2.28x improvements over v1 on mixed, medium, large and eight-market batch workloads. [Full results and limitations](results/v2-optimization/REPORT.md). These measurements do not establish an official Codabench rank or a global optimum.

## Standalone C++ v3

The user reported approximately 97k on Codabench for v2, following approximately 33k for v1. V3 removes Python from the submission execution path altogether. C++17 reuses the verified native structures and links the pinned [Arrow/Parquet C++ libraries bundled in PyArrow](https://arrow.apache.org/docs/15.0/_modules/pyarrow.html). The [native Parquet writer](https://arrow.apache.org/docs/cpp/parquet.html) exposes dictionary, numeric encoding and output-stream controls without interpreter startup; the implementation was compiled against the actual 15.0.2 wheel headers.

The new RNG reproduces scalar-seeded MT19937, 53-bit uniform doubles, legacy cached polar normals, masked bounded integers, exponential, lognormal and Pareto draws. Linux/Windows math libraries produced tiny differences in early tests, so exact differential validation uses pinned Linux NumPy. The sparse oracle retains the reference's integer/floating timestamp distinction around shocks, global/private seed order, clipping and ties-to-even rounding. No fast-math or FMA contraction is used.

Phase measurements motivated compact heap entries, recycled message slots, sorting equal-time runs, dense execution markers, activity-based buffer reservation, one-pass column construction, earlier row-buffer release, adaptive string/dictionary arrays, buffered writes and streaming hashing. The native job writes both required Parquet files, with all trace/ledger values checked.

Controlled rotated/reversed trials found dictionary arrays reduce large-table cost and memory, while small batches benefit from strings and one writer per concurrent market. Snappy remains the default: uncompressed output and omitted optional statistics did not consistently improve the workload mix. Representations depend only on row count, preserving identical standalone/batch bytes.

Replacing C++ with Rust would require another compatibility port with no measured advantage. GPU event processing, parallel dependent callbacks, optimistic rollback, calendar queues and altered random generators introduce ordering/numerical risks without demonstrated gains on these workloads. The current heap processes small live queues; output and container lifecycle remain significant costs after native dispatch. Independent-market parallelism uses the four-CPU budget.

[The v3 report](results/v3-optimization/REPORT.md) binds correctness and repeated performance to an immutable image. These results support the changes; they do not prove a global optimum or predict the organizer's score.
