#!/bin/bash
# Crypto micro-benchmarks: plain builds vs Zba/Zbb build (same machine, same CPU setting).
# Usage: ./micro_bitmanip.sh > PREFIX_micro.txt
echo "governor=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor) min=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq)"
for b in std:/opt/openssl-3.5 opt:$HOME/openssl-3.5-opt bitmanip:$HOME/openssl-3.5-bitmanip; do
    name=${b%%:*}; P=${b#*:}
    echo; echo "########## $name ($P)"
    echo "## sigbench (sign/verify per operation)"
    ./sigbench_$name 200 | tail -4
    echo "## KEM (keygen / encaps / decaps per operation)"
    $P/bin/openssl speed -seconds 3 -kem-algorithms ML-KEM-768 X25519MLKEM768 2>/dev/null | grep -E "ML-KEM-768|X25519MLKEM768"
    echo "## X25519 key exchange (op/s)"
    $P/bin/openssl speed -seconds 3 ecdhx25519 2>/dev/null | grep -E "X25519"
    echo "## SHA3-256 / SHAKE-128 throughput (1024-byte blocks, kB/s)"
    $P/bin/openssl speed -seconds 3 -bytes 1024 -evp sha3-256 2>/dev/null | grep -iE "^sha3-256"
    $P/bin/openssl speed -seconds 3 -bytes 1024 -evp shake128 2>/dev/null | grep -iE "^shake128"
    echo "## SHA-256 throughput (1024-byte blocks, kB/s)"
    $P/bin/openssl speed -seconds 3 -bytes 1024 -evp sha256 2>/dev/null | grep -iE "^sha256"
done
echo MICRO_DONE
