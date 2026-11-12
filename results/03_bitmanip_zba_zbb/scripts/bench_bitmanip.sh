#!/bin/bash
# Bit-manipulation (Zba/Zbb) build vs the plain rv64gc builds, alternated block by block
# so both see the same network conditions. CPU setting is left as it is (record it).
#   plain    = binaries used for the baseline: pqc_client (OpenSSL /opt/openssl-3.5),
#              hybrid_client_std (legacy), hybrid_client_opt (hybrid, fast-P256 build)
#   bitmanip = same sources linked against ~/openssl-3.5-bitmanip
#              (fast-P256 build + -march=rv64gc_zba_zbb)
# Usage: ./bench_bitmanip.sh BLOCK BLOCKS PREFIX
# Output: PREFIX_raw.csv  config,scenario,mode,block,run,ms   and PREFIX.log
BLOCK=$1; BLOCKS=$2; PREFIX=$3
SERVER=193.30.40.242
POL=/sys/devices/system/cpu/cpufreq/policy0
RAW=${PREFIX}_raw.csv; LOG=${PREFIX}.log
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }

echo "config,scenario,mode,block,run,ms" > $RAW
echo "start $(date -u +%FT%TZ) governor=$(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) temp=$(temp)C" | tee $LOG
for b in pqc_client pqc_client_bm hybrid_client_std hybrid_client_opt hybrid_client_bm; do
    echo "  $b -> $(ldd ./$b | grep -oE '/[^ ]*libcrypto[^ ]*')" | tee -a $LOG
done

hackathon() {   # config mode block
    local g=X25519 bin=./pqc_client; [ "$2" = hybrid ] && g=X25519MLKEM768
    [ "$1" = bitmanip ] && bin=./pqc_client_bm
    PQC_PAUSE_MS=200 $bin $g $BLOCK $SERVER > last.out 2>&1
    grep -m1 'Key exchange' last.out | sed "s/^/  $1 hackathon $2 block $3: /" >> $LOG
    grep 'Handshake took' last.out | awk -v c=$1 -v m=$2 -v b=$3 '{print c ",hackathon," m "," b "," NR "," $3}' >> $RAW
}
own() {         # config mode block
    local bin
    if [ "$1" = bitmanip ]; then bin=./hybrid_client_bm
    elif [ "$2" = hybrid ]; then bin=./hybrid_client_opt
    else bin=./hybrid_client_std; fi
    if [ "$2" = hybrid ]; then
        RAW=1 $bin 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys binary > last.out 2>&1
    else
        RAW=1 $bin 192.168.2.1 8444 X25519 rsa $BLOCK keys > last.out 2>&1
    fi
    grep -m1 -E 'PQ signature|FAILED' last.out | sed "s/^/  $1 own $2 block $3: /" >> $LOG
    grep '^RAW,' last.out | grep -v FAILED | awk -F, -v c=$1 -v m=$2 -v b=$3 '{print c ",own," m "," b "," $2 "," $5}' >> $RAW
}

for ((b = 1; b <= BLOCKS; b++)); do
    if ((b % 2)); then order="plain bitmanip"; else order="bitmanip plain"; fi
    for cfg in $order; do
        for s in hackathon own; do
            if ((b % 2)); then $s $cfg legacy $b; $s $cfg hybrid $b; else $s $cfg hybrid $b; $s $cfg legacy $b; fi
        done
    done
    echo "block $b/$BLOCKS done, temp $(temp) C" | tee -a $LOG
done
echo "end $(date -u +%FT%TZ) governor=$(cat $POL/scaling_governor) temp=$(temp)C" | tee -a $LOG
tail -n +2 $RAW | cut -d, -f1-3 | sort | uniq -c | tee -a $LOG
echo BITMANIP_BENCH_DONE
