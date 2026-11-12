#!/bin/bash
# Dynamic CPU frequency experiment: for each CPU setting, measure idle temperature
# and legacy vs hybrid latency (same method as the baseline bench_1000.sh), then
# restore the default (ondemand, min 312.5 MHz).
#
# Usage: ./bench_dyn.sh BLOCK BLOCKS PREFIX CONFIG...
#   CONFIG = name:governor:min_khz      e.g. D1:ondemand:750000
# Output: PREFIX_raw.csv    config,scenario,mode,block,run,ms
#         PREFIX_temps.csv  config,phase,elapsed_s,temp_c
#         PREFIX.log        per-block checks
BLOCK=$1; BLOCKS=$2; PREFIX=$3; shift 3
POL=/sys/devices/system/cpu/cpufreq/policy0
SERVER=193.30.40.242
RAW=${PREFIX}_raw.csv; TEMPS=${PREFIX}_temps.csv; LOG=${PREFIX}.log
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }

set_cpu() {   # governor min_khz
    echo "$1" | sudo tee $POL/scaling_governor >/dev/null &&
    echo "$2" | sudo tee $POL/scaling_min_freq >/dev/null
}
restore() {
    kill $KEEPER $SAMPLER 2>/dev/null
    set_cpu ondemand 312500
    echo "restored: $(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq)" | tee -a $LOG
}
trap restore EXIT

# Keep the sudo timestamp fresh during the long run.
( while sleep 240; do sudo -n -v; done ) >/dev/null 2>&1 & KEEPER=$!

echo "config,scenario,mode,block,run,ms" > $RAW
echo "config,phase,elapsed_s,temp_c" > $TEMPS
: > $LOG

hackathon() {   # config mode block
    local g=X25519; [ "$2" = hybrid ] && g=X25519MLKEM768
    PQC_PAUSE_MS=200 ./pqc_client $g $BLOCK $SERVER > last.out 2>&1
    grep -m1 'Key exchange' last.out | sed "s/^/  $1 hackathon $2 block $3: /" >> $LOG
    grep 'Handshake took' last.out | awk -v c=$1 -v m=$2 -v b=$3 '{print c ",hackathon," m "," b "," NR "," $3}' >> $RAW
}
own() {         # config mode block
    if [ "$2" = hybrid ]; then
        RAW=1 ./hybrid_client_opt 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys binary > last.out 2>&1
    else
        RAW=1 ./hybrid_client_std 192.168.2.1 8444 X25519 rsa $BLOCK keys > last.out 2>&1
    fi
    grep -m1 -E 'PQ signature|FAILED' last.out | sed "s/^/  $1 own $2 block $3: /" >> $LOG
    grep '^RAW,' last.out | grep -v FAILED | awk -F, -v c=$1 -v m=$2 -v b=$3 '{print c ",own," m "," b "," $2 "," $5}' >> $RAW
}

for cfg in "$@"; do
    IFS=: read -r name gov min <<< "$cfg"
    set_cpu $gov $min || { echo "cannot set $cfg" | tee -a $LOG; exit 1; }
    echo "== $name: governor=$(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) $(date -u +%T)" | tee -a $LOG
    # Idle phase: IDLE_S seconds (default 120), temperature every 5 s.
    for ((t = 0; t <= ${IDLE_S:-120}; t += 5)); do echo "$name,idle,$t,$(temp)" >> $TEMPS; sleep 5; done
    echo "   idle temp after ${IDLE_S:-120} s: $(temp) C, idle freq $(cat $POL/scaling_cur_freq) kHz" | tee -a $LOG
    # Load phase: sample temperature in the background.
    ( t=0; while true; do echo "$name,load,$t,$(temp)" >> $TEMPS; sleep 5; t=$((t + 5)); done ) >/dev/null 2>&1 & SAMPLER=$!
    for s in hackathon own; do
        for ((b = 1; b <= BLOCKS; b++)); do
            if ((b % 2)); then $s $name legacy $b; $s $name hybrid $b; else $s $name hybrid $b; $s $name legacy $b; fi
        done
        echo "   $s done, temp $(temp) C" | tee -a $LOG
    done
    kill $SAMPLER 2>/dev/null; SAMPLER=
done
echo "measurements per config/scenario/mode:" | tee -a $LOG
tail -n +2 $RAW | cut -d, -f1-3 | sort | uniq -c | tee -a $LOG
echo DYN_DONE
