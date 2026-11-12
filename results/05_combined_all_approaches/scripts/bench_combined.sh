#!/bin/bash
# ALL APPROACHES COMBINED vs BASELINE, alternated block by block (same network conditions).
#   BASE      default clock (ondemand, min 312.5 MHz), plain builds, hybrid auth v1 (one core)
#   COMBINED  ondemand + min 750 MHz, bitmanip (Zba/Zbb) build, hybrid auth v2 on a 2nd core
# Scenarios: hackathon server (handshake; key exchange only) and own server (end-to-end).
# Usage: ./bench_combined.sh BLOCK BLOCKS PREFIX     (needs sudo; restores the default at the end)
BLOCK=$1; BLOCKS=$2; PREFIX=$3
POL=/sys/devices/system/cpu/cpufreq/policy0
SERVER=193.30.40.242
RAW=${PREFIX}_raw.csv; TEMPS=${PREFIX}_temps.csv; LOG=${PREFIX}.log
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }
set_cpu() { echo "$1" | sudo tee $POL/scaling_governor >/dev/null && echo "$2" | sudo tee $POL/scaling_min_freq >/dev/null; }
apply() {
    case $1 in
        BASE)     set_cpu ondemand 312500 ;;
        COMBINED) set_cpu ondemand 750000 ;;
    esac || { echo "cannot set $1" | tee -a $LOG; exit 1; }
}
restore() { kill $KEEPER 2>/dev/null; set_cpu ondemand 312500; echo "restored: $(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) temp=$(temp)C" | tee -a $LOG; }
trap restore EXIT
( while sleep 240; do sudo -n -v; done ) >/dev/null 2>&1 & KEEPER=$!

echo "config,scenario,mode,block,run,ms" > $RAW
echo "config,phase,block,temp_c" > $TEMPS
: > $LOG
hackathon() {   # config mode block
    local g=X25519 bin=./pqc_client; [ "$2" = hybrid ] && g=X25519MLKEM768
    [ "$1" = COMBINED ] && bin=./pqc_client_bm
    PQC_PAUSE_MS=200 $bin $g $BLOCK $SERVER > last_cb.out 2>&1
    grep -m1 'Key exchange' last_cb.out | sed "s/^/  $1 hackathon $2 block $3: /" >> $LOG
    grep 'Handshake took' last_cb.out | awk -v c=$1 -v m=$2 -v b=$3 '{print c ",hackathon," m "," b "," NR "," $3}' >> $RAW
}
own() {         # config mode block
    local args
    if [ "$2" = legacy ]; then
        [ "$1" = BASE ] && bin=./hybrid_client_std || bin=./hybrid_client_bm
        args="X25519 rsa $BLOCK keys"
    elif [ "$1" = BASE ]; then
        bin=./hybrid_client_opt; args="X25519MLKEM768 hybrid44 $BLOCK keys binary"
    else
        bin=./hybrid_client_bm; args="X25519MLKEM768 hybrid44 $BLOCK keys certext par"
    fi
    RAW=1 $bin 192.168.2.1 8444 $args > last_cb.out 2>&1
    grep -m1 -E 'PQ signature|FAILED|missing' last_cb.out | cut -c1-110 | sed "s/^/  $1 own $2 block $3: /" >> $LOG
    grep -c FAILED last_cb.out | sed "s/^/  $1 own $2 block $3 failures: /" >> $LOG
    grep '^RAW,' last_cb.out | grep -v FAILED | awk -F, -v c=$1 -v m=$2 -v b=$3 '{print c ",own," m "," b "," $2 "," $5}' >> $RAW
}

echo "start $(date -u +%FT%TZ) temp=$(temp)C" | tee -a $LOG
for cfg in BASE COMBINED; do        # idle temperature of each setting, back to back
    apply $cfg; sleep 120
    echo "$cfg,idle,0,$(temp)" >> $TEMPS
    echo "== $cfg: governor=$(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) idle temp after 120 s $(temp) C" | tee -a $LOG
done
for ((b = 1; b <= BLOCKS; b++)); do
    if ((b % 2)); then order="BASE COMBINED"; else order="COMBINED BASE"; fi
    for cfg in $order; do
        apply $cfg; sleep 5
        echo "$cfg,load,$b,$(temp)" >> $TEMPS
        for s in hackathon own; do
            if ((b % 2)); then $s $cfg legacy $b; $s $cfg hybrid $b; else $s $cfg hybrid $b; $s $cfg legacy $b; fi
        done
    done
    echo "block $b/$BLOCKS done, temp $(temp) C" | tee -a $LOG
done
tail -n +2 $RAW | cut -d, -f1-3 | sort | uniq -c | tee -a $LOG
echo COMBINED_DONE
