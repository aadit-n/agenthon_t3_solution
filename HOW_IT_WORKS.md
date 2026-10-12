The simulator reads a market scenario, recreates the reference simulator's random decisions and message ordering, and writes the resulting exchange events and message ledger. V7 uses the same v5 market rules and output bytes; it reduces executable startup work and the space occupied by an internal trace row. Its local improvements do not establish a Codabench score.

## What happens during a run

1. `native/main_lite.cpp` reads the JSON and passes its seed and configuration to `Simulation`.
2. Each agent gets its own reference-compatible random state. An independent stream supplies message latency. Agents create orders and react to messages; those actions determine later events.
3. A timestamp queue selects the next message. Equal timestamps follow the reference sender, recipient and message-ID order. Busy agents postpone processing until they become free.
4. The exchange matches incoming orders against the best opposite price, respecting FIFO within each price level and the configured self-trade rule. It sends acceptance, fill and cancellation messages back to agents.
5. The engine records exchange events and message deliveries. It sorts exchange rows into the required canonical order and labels final versus partial fills.
6. The writer converts finalized rows into Parquet columns, encodes the integers/strings, compresses pages with Snappy, closes the files and computes their SHA256 hashes. `events.json` records counts, real elapsed time and resource telemetry.

The single-market event loop remains sequential because a later action can depend on an earlier trade. Spare cores prepare independent latency draws and write completed output blocks. Batch markets are independent, so up to four can run concurrently. Every worker finishes and every output file closes before the process exits.

## Why the new version can be faster

V5 already executes C++, despite retaining Python in its base image. V7 links the engine and its libraries into one native executable, starts it directly, and removes the Python installation from the runtime image. Some glibc compatibility files remain for injected driver utilities.

A trace row previously occupied 56 bytes. V7 uses 48 bytes by storing the event category and side in one byte each; those categories have fixed small domains. Timestamps, prices, quantities, order IDs and stable ranks keep their original integer widths. The Parquet writer still produces exactly the v5 bytes.

Other algorithms were tested behind disabled build macros. Their small or inconsistent improvements did not justify making them defaults. The selected compiler flags are in `scripts/build_release.sh`.

## What the speed numbers mean

An exchange event is a submitted/accepted/filled/cancelled order or a quote update; it is not an HTTP request. Delivered messages and order commands are different counts.

Native process timing includes program startup, simulation, output closure, hashing and exit. Complete Docker CLI timing also includes container creation/removal and local client/transport overhead. The provisional Development board uses the organizer's verified event count divided by its own creation-to-removal window on a shared host. Self-reported throughput is checked for consistency but does not set the score. The planned Final timing uses a separate repeated start-to-exit protocol.

The current evidence and precise limits are in [the v7 report](results/v7-optimization/REPORT.md). A new Codabench run is required to know whether this version improves the submitted score.
