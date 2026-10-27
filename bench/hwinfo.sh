#!/bin/sh
# Print what the numbers were measured on.
cpu=${1:-?}
echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "cpu model: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
echo "logical cpus visible: $(nproc)"
echo "pinned to cpu: $cpu"
echo "cpu flags: $(grep -m1 '^flags' /proc/cpuinfo | grep -o -w -E 'constant_tsc|nonstop_tsc|rdtscp|hypervisor' | tr '\n' ' ')"
echo "memory: $(awk '/MemTotal/ {printf "%.1f GiB", $2/1048576}' /proc/meminfo)"
echo "kernel: $(uname -sr)"
echo "compiler: $(c++ --version | head -1)"
echo "flags: $(grep CMAKE_CXX_FLAGS_RELEASE CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')"
echo "cpufreq governor: $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor 2>/dev/null || echo 'not exposed')"
if command -v lscpu >/dev/null; then
    echo "--- lscpu"
    lscpu
fi
