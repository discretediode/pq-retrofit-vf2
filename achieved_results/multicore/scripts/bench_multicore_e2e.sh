#!/bin/bash
# Multicore end-to-end: legacy vs hybrid v1 vs hybrid v2 (one core) vs hybrid v2 (two cores),
# own server on the LAN, BLOCKS x BLOCK runs per mode, mode order rotating every block.
# Usage: ./bench_multicore_e2e.sh BLOCK BLOCKS PREFIX [LIB]   (LIB = opt (default) or bm)
BLOCK=$1; BLOCKS=$2; PREFIX=$3; LIB=${4:-opt}
POL=/sys/devices/system/cpu/cpufreq/policy0
RAW=${PREFIX}_raw.csv; LOG=${PREFIX}.log
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }
MODES=(legacy v1 v2_seq v2_par)
run() {     # mode block
    local out=last_mc.out
    case $1 in
        legacy) RAW=1 ./hybrid_client_std 192.168.2.1 8444 X25519 rsa $BLOCK keys > $out 2>&1 ;;
        v1)     RAW=1 ./hybrid_client_$LIB 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys binary > $out 2>&1 ;;
        v2_seq) RAW=1 ./hybrid_client_$LIB 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys certext > $out 2>&1 ;;
        v2_par) RAW=1 ./hybrid_client_$LIB 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys certext par > $out 2>&1 ;;
    esac
    grep -m1 -E 'PQ signature|FAILED|missing' $out | cut -c1-120 | sed "s/^/  $1 block $2: /" >> $LOG
    grep -c 'FAILED' $out | sed "s/^/  $1 block $2 failures: /" >> $LOG
    # RAW,run,tcp,handshake,e2e,resumed,verify,wait
    grep '^RAW,' $out | grep -v FAILED | awk -F, -v m=$1 -v b=$2 '{print m "," b "," $2 "," $3 "," $4 "," $5 "," $7 "," $8}' >> $RAW
}
echo "mode,block,run,tcp_ms,handshake_ms,e2e_ms,pq_verify_ms,wait_ms" > $RAW
echo "start $(date -u +%FT%TZ) governor=$(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) temp=$(temp)C lib=$LIB" | tee $LOG
for ((b = 1; b <= BLOCKS; b++)); do
    for ((k = 0; k < 4; k++)); do run ${MODES[$(((k + b) % 4))]} $b; done
    echo "block $b/$BLOCKS done, temp $(temp) C" | tee -a $LOG
done
echo "end $(date -u +%FT%TZ) governor=$(cat $POL/scaling_governor) temp=$(temp)C" | tee -a $LOG
tail -n +2 $RAW | cut -d, -f1 | sort | uniq -c | tee -a $LOG
echo MC_E2E_DONE
