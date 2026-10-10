#!/bin/sh
# Local diagnostic wrapper. The unchanged submission entrypoint performs the run.
# Cgroup statistics cover every worker and include the small wrapper overhead.
cat /sys/fs/cgroup/cpu.stat > /tmp/t3_cpu_before 2>/dev/null || true
entrypoint "$@"
simulation_status=$?
cat /sys/fs/cgroup/cpu.stat > /tmp/t3_cpu_after 2>/dev/null || true
if [ -r /sys/fs/cgroup/memory.peak ]; then
    cat /sys/fs/cgroup/memory.peak > /tmp/t3_memory_peak
elif [ -r /sys/fs/cgroup/memory/memory.max_usage_in_bytes ]; then
    cat /sys/fs/cgroup/memory/memory.max_usage_in_bytes > /tmp/t3_memory_peak
fi
exit "$simulation_status"
