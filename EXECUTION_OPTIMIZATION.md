# Execution specialization and compact scheduler experiments

These C++17 changes target repeated work in the simulation loop. Performance selection and immutable artifact identities are recorded in `results/execution-specialization/REPORT.md`. Native timings and complete Docker lifecycle timings use separate clocks; neither establishes a Codabench score.

## Configuration specialization

`ExecutionMode` gives the compiler constant latency and protocol properties. `Simulation::run()` chooses one implementation before dispatch begins. It never chooses by scenario name, seed or reference data.

Latency implementations cover fixed transport, geographic transport, uniform/log-normal/Pareto scalar sampling, and prepared latency blocks. Same-sender messages still bypass the latency RNG. Scalar sampling uses the same distributions, clamp, rounding and draw order. Action-stream latency preparation uses the generic path because it has a different producer. Agent callbacks, wakeups, oracle observations, close handling and message scheduling remain in their original order.

The plain protocol implementation is eligible only when both `stp == 0` and `ack_delay == 0`. It removes acknowledgement-kind checks and the resting-order read used solely for self-trade prevention. Exchange computation delays still apply. Configurations with active self-trading policies or acknowledgement delays retain their general handling. The independent `T3_LAZY_STP_FRONT` experiment moves the resting-order read inside the dynamic STP check; it is measured separately.

For differential testing, `T3SIM_EXECUTION_MODE=generic` forces the generic event-loop implementation in specialization builds. This still uses whichever scheduler was compiled into that executable.

## Compact scheduler storage

`NarrowRadixQueue` stores pending events in 24 bytes instead of 32. Timestamps and packed signed routing stay 64-bit. Message IDs use uint32 storage only after a checked range test; payload slots were already uint32 in v7.

A negative ID, ID above `UINT32_MAX`, or backwards timestamp migrates every pending event into a full-width comparison heap before inserting the triggering event. No message ID is truncated. Output and causal IDs remain int64. The ordering is still timestamp, signed sender, signed recipient, then message ID. The original stop condition is unchanged.

This queue has no sampled contention controller. Its normal path uses the same radix algorithm as v7, with smaller entries; the fallback remains active once needed.

## Row-buffer ownership

The separate `T3_REUSE_ROW_BUFFERS` experiment recycles completed streaming blocks instead of repeatedly allocating large vectors. Each writer keeps up to four row buffers, including the producer's buffer, with the original limit of two pending blocks. Ownership returns to the producer only after the writer has encoded every column and copied the deferred fill-label information. A cleared vector retains its storage and is repopulated with complete rows. The 65,536-row ledger groups, trace timestamp-group boundary, codecs, compression and full-file hashes remain unchanged.

## Build options

The experiments are controlled by `T3_SPECIALIZE_LATENCY`, `T3_SPECIALIZE_PROTOCOL`, `T3_LAZY_STP_FRONT`, and `T3_NARROW_SCHEDULED`. For example, to explicitly build compact storage with latency specialization:

```powershell
docker build --platform linux/amd64 `
  --build-arg "T3_EXTRA_CXX_FLAGS=-DT3_NARROW_SCHEDULED -DT3_SPECIALIZE_LATENCY" `
  --build-arg T3_SPECIALIZED_PGO=1 `
  -t agenthon-t3:execution-narrow-latency .
```

`T3_SPECIALIZED_PGO=1` trains on the original synthetic compiler scenarios plus independently varied protocol configurations. This covers the otherwise-untrained plain protocol path. It uses no public or sealed reference outputs. Profiles and generated training files do not ship in the runtime.

`scripts/build_execution.sh` compiles controls and specialization variants in an offline, resource-capped compiler container. `scripts/build_narrow_schedule.sh` compiles compact-storage combinations; `scripts/build_row_reuse.sh` compiles allocation-reuse combinations. `Dockerfile.execution` packages an already measured executable and its matching source snapshot using v7's compatibility libraries, notices and direct entrypoint in one runtime layer.

## Verification

`tests/execution_modes.cpp` compares the complete generic and specialized market traces, ledger records and final counters. ASAN/UBSAN covers 60 cases with all latency implementations, both protocol paths and action preparation, including 981,690 exchange rows and 1,146,465 ledger rows.

`tests/narrow_scheduler.cpp` compares against an independent standard priority queue: four million operations, 2,002,051 exact pops, dense timestamp ties, random signed routing, backwards arrivals, negative/wide message IDs, pending-event migration and timestamp extremes.

`tests/row_reuse.cpp` checks 972,416 ledger rows and 409,600 trace rows against a sequential writer under ASAN/UBSAN, including large and small blocks, backpressure, canonical sorting, deferred fill labels, empty output and asynchronous writer failure.

`scripts/tune_v5.py` compares fresh processes on Linux storage and independently hashes every Parquet file after timing. `scripts/benchmark_lifecycle.py` measures fresh complete containers on a Linux volume, separately retaining daemon start/exit timestamps. Public verification, extra seeded cases and batch worker isolation must pass before release selection. Output codecs, page/group sizes, hashing, RNG and the required ledger remain unchanged.
