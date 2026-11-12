#!/bin/bash
# Legacy vs hybrid, BLOCKS x BLOCK runs per mode, alternating order every block
# ("back and forth"), every single measurement recorded.
#   Scenario "hackathon": hackathon server over IPv4, pqc_client, TLS handshake time
#                         (legacy = X25519, hybrid = X25519MLKEM768; server auth RSA-3072 in both)
#   Scenario "own":       own server on the Mac, hybrid_client, end-to-end time
#                         (legacy = X25519 + RSA-3072, hybrid = X25519MLKEM768 + ECDSA P-256 + ML-DSA-44)
# Output: raw_1000.csv  scenario,mode,block,run,metric_ms
BLOCK=${1:-50}
BLOCKS=${2:-20}
OUT=raw_1000.csv
LOG=bench_1000.log
SERVER=193.30.40.242
gov() { cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; }
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }

echo "scenario,mode,block,run,ms" > $OUT
echo "start $(date -u +%FT%TZ) governor=$(gov) temp=$(temp)C block=$BLOCK blocks=$BLOCKS" | tee $LOG

hackathon() {   # $1 = mode, $2 = block
    local g=X25519; [ "$1" = hybrid ] && g=X25519MLKEM768
    PQC_PAUSE_MS=200 ./pqc_client $g $BLOCK $SERVER > last.out 2>&1
    grep -m1 -E 'Key exchange' last.out | sed "s/^/  hackathon $1 block $2: /" >> $LOG
    grep 'Handshake took' last.out | awk -v m=$1 -v b=$2 '{print "hackathon," m "," b "," NR "," $3}' >> $OUT
}
own() {         # $1 = mode, $2 = block
    if [ "$1" = hybrid ]; then
        RAW=1 ./hybrid_client_opt 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys binary > last.out 2>&1
    else
        RAW=1 ./hybrid_client_std 192.168.2.1 8444 X25519 rsa $BLOCK keys > last.out 2>&1
    fi
    grep -m1 -E 'PQ signature|FAILED' last.out | sed "s/^/  own $1 block $2: /" >> $LOG
    grep '^RAW,' last.out | grep -v FAILED | awk -F, -v m=$1 -v b=$2 '{print "own," m "," b "," $2 "," $5}' >> $OUT
}

for s in hackathon own; do
    for ((b = 1; b <= BLOCKS; b++)); do
        if ((b % 2)); then $s legacy $b; $s hybrid $b; else $s hybrid $b; $s legacy $b; fi
        printf "%s block %d/%d done, temp %sC\n" $s $b $BLOCKS "$(temp)"
    done
done
echo "end $(date -u +%FT%TZ) governor=$(gov) temp=$(temp)C" | tee -a $LOG
for s in hackathon own; do for m in legacy hybrid; do
    printf "%-9s %-6s %5d measurements\n" $s $m $(grep -c "^$s,$m," $OUT)
done; done | tee -a $LOG
echo BENCH1000_DONE
