#!/bin/bash
# Confirmation run: two CPU configs alternated block by block (so both see the same
# network conditions), legacy vs hybrid, both scenarios. Restores the default at the end.
# Usage: ./bench_cfg_ab.sh BLOCK BLOCKS PREFIX CONFIG_A CONFIG_B   (CONFIG = name:governor:min_khz)
BLOCK=$1; BLOCKS=$2; PREFIX=$3; A=$4; B=$5
POL=/sys/devices/system/cpu/cpufreq/policy0
SERVER=193.30.40.242
RAW=${PREFIX}_raw.csv; TEMPS=${PREFIX}_temps.csv; LOG=${PREFIX}.log
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }
set_cpu() { echo "$1" | sudo tee $POL/scaling_governor >/dev/null && echo "$2" | sudo tee $POL/scaling_min_freq >/dev/null; }
apply() { IFS=: read -r name gov min <<< "$1"; set_cpu $gov $min || { echo "cannot set $1" | tee -a $LOG; exit 1; }; }
restore() { kill $KEEPER 2>/dev/null; set_cpu ondemand 312500; echo "restored: $(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq)" | tee -a $LOG; }
trap restore EXIT
( while sleep 240; do sudo -n -v; done ) >/dev/null 2>&1 & KEEPER=$!

echo "config,scenario,mode,block,run,ms" > $RAW
echo "config,phase,elapsed_s,temp_c" > $TEMPS
: > $LOG
hackathon() {
    local g=X25519; [ "$2" = hybrid ] && g=X25519MLKEM768
    PQC_PAUSE_MS=200 ./pqc_client $g $BLOCK $SERVER > last.out 2>&1
    grep -m1 'Key exchange' last.out | sed "s/^/  $1 hackathon $2 block $3: /" >> $LOG
    grep 'Handshake took' last.out | awk -v c=$1 -v m=$2 -v b=$3 '{print c ",hackathon," m "," b "," NR "," $3}' >> $RAW
}
own() {
    if [ "$2" = hybrid ]; then
        RAW=1 ./hybrid_client_opt 192.168.2.1 8444 X25519MLKEM768 hybrid44 $BLOCK keys binary > last.out 2>&1
    else
        RAW=1 ./hybrid_client_std 192.168.2.1 8444 X25519 rsa $BLOCK keys > last.out 2>&1
    fi
    grep -m1 -E 'PQ signature|FAILED' last.out | sed "s/^/  $1 own $2 block $3: /" >> $LOG
    grep '^RAW,' last.out | grep -v FAILED | awk -F, -v c=$1 -v m=$2 -v b=$3 '{print c ",own," m "," b "," $2 "," $5}' >> $RAW
}

# Idle temperature of each config, measured back to back in the same session.
for cfg in "$A" "$B"; do
    apply "$cfg"
    for ((t = 0; t <= 120; t += 5)); do echo "$name,idle,$t,$(temp)" >> $TEMPS; sleep 5; done
    echo "== $name ($gov, min $min): idle temp after 120 s $(temp) C" | tee -a $LOG
done

for ((b = 1; b <= BLOCKS; b++)); do
    if ((b % 2)); then order="$A $B"; else order="$B $A"; fi
    for cfg in $order; do
        apply "$cfg"; sleep 5
        echo "$name,load,$b,$(temp)" >> $TEMPS
        for s in hackathon own; do
            if ((b % 2)); then $s $name legacy $b; $s $name hybrid $b; else $s $name hybrid $b; $s $name legacy $b; fi
        done
    done
    echo "block $b/$BLOCKS done, temp $(temp) C" | tee -a $LOG
done
tail -n +2 $RAW | cut -d, -f1-3 | sort | uniq -c | tee -a $LOG
echo CFGAB_DONE
