# C++ Parallel Text Analyzer

A C++17 command-line program comparing sequential and parallel processing of multiple text
files. It uses streaming file I/O, fixed worker threads, and static file partitioning, with
benchmarks showing how throughput and memory usage change as worker count increases.

## Key features

- Count lines, whitespace-separated words, and exact search-word matches across files.
- Run sequentially or in parallel using `std::thread`, without retaining complete files.
- Keep each worker's statistics separate and aggregate only after joining.
- Report file errors and worker exceptions; join started threads if another cannot be created.
- Test correctness and failure paths; measure wall time, throughput, speedup, and peak RSS.

## Tech stack

C++17, STL, Linux, CMake, CTest, clang-format, and GitHub Actions.
Python 3 (standard library only) generates and runs benchmarks; Linux `/usr/bin/time` measures
resource usage. The application has no third-party C++ dependencies.

## Architecture and implementation

In [src/analyzer.cpp](src/analyzer.cpp):

- `processFile()` reads one line at a time with `std::getline`, extracts words with
  `std::istringstream`, and returns a `FileStats` struct.
- `processSequential()` visits each input path and adds its counts.
- `processParallel()` assigns contiguous ranges of file indices to
  `min(requested threads, file count)` workers. A worker processes each assigned file in full.

Workers write to separate statistics and exception slots; shared input is read-only.
No mutex is needed for counters because workers never update the same counter. Result storage
stays valid until every thread is joined. The main thread then checks captured exceptions and
combines results with `addStats()`. A creation failure joins earlier workers before rethrowing.

Start with [src/main.cpp](src/main.cpp) for the CLI, [include/analyzer.h](include/analyzer.h)
for the data structure/API, and [tests/test_analyzer.cpp](tests/test_analyzer.cpp) for correctness
and failure cases.

## Performance results

The original run is preserved in [benchmarks/results.json](benchmarks/results.json).
Measured 2026-10-12 on Ubuntu 26.04.1 LTS, Intel i7-11370H (4 physical cores / 8 logical CPUs,
allowed CPUs 0–7), GCC 15.2.0, Release `-O3 -DNDEBUG`. CPUs and clock frequency were not pinned
or isolated from other work.

Eight equal files total 44,040,192 bytes (42 MiB). After one warm-up per configuration, five
fresh-process trials run in rotating order, with the same files and search word (`error`).
Every execution must match the expected counts. Median end-to-end wall time includes process
startup and captured output; compilation and input generation are excluded. Peak RSS is from
one separate `/usr/bin/time -v` run per configuration.

| Mode | Requested threads | Actual workers | Median seconds | MB/s | Speedup | Peak RSS KiB |
|---|---:|---:|---:|---:|---:|---:|
| Sequential | — | 0 | 0.7366 | 59.79 | 1.00× | 4064 |
| Parallel | 1 | 1 | 0.7893 | 55.80 | 0.93× | 4152 |
| Parallel | 2 | 2 | 0.3911 | 112.61 | 1.88× | 4076 |
| Parallel | 4 | 4 | 0.2098 | 209.95 | 3.51× | 4128 |
| Parallel | 8 | 8 | 0.2147 | 205.14 | 3.43× | 4212 |

Four workers had the best median, but four/eight-worker timing ranges overlap; the small
difference is inconclusive. One worker was slower than sequential. Resource runs reported
zero filesystem inputs, consistent with warm-cache reads; this is not a cold-disk throughput
test. Results are specific to this workload and machine.

MB/s = bytes / 1,000,000 / median seconds; speedup = sequential median / configuration median.
Sequential uses the main thread and zero workers. Peak RSS stayed near 4 MiB for this short-line
workload; memory usage is not constant.

To reproduce with Python 3, `/usr/bin/time`, and CMake's Unix Makefiles generator:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles"
cmake --build build-release
ctest --test-dir build-release --output-on-failure
LC_ALL=C python3 benchmarks/run.py
```

[benchmarks/run.py](benchmarks/run.py) generates deterministic inputs and writes new samples,
environment details, and resource logs to ignored `benchmarks/generated/`. The original snapshot
is not overwritten. That generated directory and `build-release/` are disposable after use.

## Build and run

Requires Linux, a C++17 compiler, and CMake 3.16 or newer. From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/cpp-parallel-text-analyzer sequential error examples/file1.txt examples/file2.txt
./build/cpp-parallel-text-analyzer parallel 4 error examples/file1.txt examples/file2.txt
ctest --test-dir build --output-on-failure
```

The CLI requires at least one file, a search token without whitespace, and a positive parallel
thread count. Errors return a nonzero exit status without printing partial totals.

## Example input and output

[examples/file1.txt](examples/file1.txt):

```text
error warning error
INFO error.
```

[examples/file2.txt](examples/file2.txt):

```text
error info
```

Both commands above produce:

```text
Lines: 3
Words: 7
Matches: 3
```

`error.` does not match `error`. Empty files have zero counts. Every line returned by `getline`
counts once, including a final line without a newline. Repeated paths are processed each time.

## Testing and reliability

One CTest executable checks counting rules, repeated files, empty inputs, 1/2/4/8/20 requested
workers, repeated sequential/parallel equality, invalid counts, file errors, and worker exceptions.
A small hook compiled only into tests simulates creation failures before worker 0, 1, or 3,
checking that existing workers are joined. It does not exhaust OS resources. Permission tests
skip if the current user bypasses permission bits; CTest has a 30-second timeout.

Clean Debug/Release builds and separate AddressSanitizer, UndefinedBehaviorSanitizer, and
ThreadSanitizer runs passed locally. ASan needed leak detection disabled because LeakSanitizer
cannot run under this environment's ptrace tracing; leak detection is unverified. TSan reported
no races on exercised paths, alongside a review of ownership and object lifetimes. Passing
sanitizers does not prove the absence of every possible defect.

[GitHub Actions](.github/workflows/ci.yml) configures, builds, and runs CTest. It is configured;
hosted execution has not yet been verified. Check formatting with clang-format:

```bash
clang-format --dry-run --Werror include/analyzer.h src/analyzer.cpp src/main.cpp tests/test_analyzer.cpp
```

## Technical decisions and trade-offs

- Streaming bounds buffers by line length rather than file size.
- Static ranges keep thread ownership simple and avoid a queue or scheduler, but cannot rebalance
  unequal files while running.
- Separate counters and aggregation after joining avoid locks in the processing loop.
- Eight workers share four physical cores on this CPU. Resource sharing, scheduling, and thread
  startup are possible explanations for limited scaling, not isolated measured causes.

## Complexity and limitations

For B bytes, F input paths, W workers, longest line L, and total stored path bytes P:

- Sequential time is O(B + F). Parallel total work is O(B + F + W), including O(W) setup/merging;
  elapsed time depends on the busiest range, available CPU resources, and I/O.
- Memory is O(P + F + L) sequentially and O(P + F + W × L) in parallel, plus worker stream
  buffers, state, and thread stacks. Long lines and additional workers increase memory needs.
- Files are not split into chunks. Unequal sizes can leave workers idle; more workers need not
  improve throughput. Cache state and disk speed affect results.
- Matching is case-sensitive and whitespace-based; punctuation stays attached. Input uses text
  bytes without full Unicode-aware tokenization. Counter overflow is not detected.
- No cancellation or application timeout is provided; blocked filesystems or FIFOs can block
  completion. Library callers must not modify inputs concurrently with processing.
