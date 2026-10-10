"""Read the actual local container environment; no guessed hardware details."""
import json
import os
from pathlib import Path
import platform

cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
            if line.startswith("model name")), "unknown")
print(json.dumps({"cpu_model": cpu, "kernel": platform.release(), "machine": platform.machine(),
                  "visible_logical_cpus": os.cpu_count(),
                  "cpu_quota": Path("/sys/fs/cgroup/cpu.max").read_text().strip(),
                  "memory_cap": Path("/sys/fs/cgroup/memory.max").read_text().strip(),
                  "gpu_attached_in_this_test": Path("/dev/nvidia0").exists(),
                  "python": platform.python_version()}))
