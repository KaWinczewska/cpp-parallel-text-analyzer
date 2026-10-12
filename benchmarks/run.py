"""Run from the repository root after building build-release (Python standard library only)."""

import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=120)
    if result.stderr:
        raise RuntimeError(result.stderr)
    if result.stdout != expected:
        raise RuntimeError("Incorrect analyzer output: " + result.stdout)


output = Path("benchmarks/generated")
output.mkdir(parents=True, exist_ok=True)
line = b"error warning error INFO error. info data\n"
lines_per_file = 131072
files = []
for index in range(8):
    path = output / f"file{index}.txt"
    with path.open("wb") as file:
        # Small blocks avoid retaining the entire dataset in memory.
        for block in range(lines_per_file // 1024):
            file.write(line * 1024)
    files.append(str(path))

total_lines = lines_per_file * len(files)
expected = f"Lines: {total_lines}\nWords: {total_lines * 7}\nMatches: {total_lines * 2}\n"
total_bytes = sum(Path(file).stat().st_size for file in files)
executable = "./build-release/cpp-parallel-text-analyzer"
commands = [[executable, "sequential", "error", *files]]
for count in [1, 2, 4, 8]:
    commands.append([executable, "parallel", str(count), "error", *files])

# Validate and warm each configuration before recording any timings.
for command in commands:
    run(command)
samples = [[] for command in commands]
for trial in range(5):
    # Rotate order to reduce a consistent first/last configuration bias.
    for offset in range(len(commands)):
        index = (trial + offset) % len(commands)
        start = time.perf_counter()
        run(commands[index])
        samples[index].append(time.perf_counter() - start)

rows = []
baseline = statistics.median(samples[0])
for index, command in enumerate(commands):
    # Memory measurements are separate from the timed trials.
    log = output / f"time-{index}.txt"
    run(["/usr/bin/time", "-v", "-o", str(log), *command])
    memory_line = next(line for line in log.read_text().splitlines()
                       if "Maximum resident set size (kbytes):" in line)
    median = statistics.median(samples[index])
    requested = None if index == 0 else int(command[2])
    rows.append({
        "mode": command[1], "requested_threads": requested,
        "actual_workers": 0 if index == 0 else min(requested, len(files)),
        "seconds": samples[index], "median_seconds": median,
        "MB_per_second": total_bytes / 1_000_000 / median,
        "speedup": baseline / median,
        "peak_RSS_KiB": int(memory_line.rsplit(":", 1)[1]),
    })

environment = os.environ.copy()
environment["LC_ALL"] = "C"
metadata = {
    "os": platform.platform(),
    "cpu": subprocess.check_output(["lscpu"], text=True, env=environment),
    "compiler": subprocess.check_output(["c++", "--version"], text=True),
    "build_flags": Path("build-release/CMakeFiles/analyzer.dir/flags.make").read_text(),
    "input_files": len(files), "bytes_per_file": total_bytes // len(files),
    "total_bytes": total_bytes, "search_word": "error", "expected_output": expected,
    "trials_per_configuration": 5, "results": rows,
}
(output / "results.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(f"Input: {total_bytes:,} bytes; {len(files)} equal files; five trials per configuration")
print("| Mode | Requested | Workers | Median s | MB/s | Speedup | Peak RSS KiB |")
print("|---|---:|---:|---:|---:|---:|---:|")
for row in rows:
    print(f"| {row['mode']} | {row['requested_threads'] or '—'} | {row['actual_workers']} | "
          f"{row['median_seconds']:.4f} | {row['MB_per_second']:.2f} | "
          f"{row['speedup']:.2f}x | {row['peak_RSS_KiB']} |")
