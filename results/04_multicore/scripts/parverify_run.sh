#!/bin/bash
# Multicore feasibility: parverify at three CPU clock settings x two OpenSSL builds.
# Usage: ./parverify_run.sh ITERS PAUSE_MS PREFIX      (needs sudo; restores the default)
ITERS=$1; PAUSE=$2; PREFIX=$3
POL=/sys/devices/system/cpu/cpufreq/policy0
RAW=${PREFIX}_raw.csv; LOG=${PREFIX}.log
temp() { awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp; }
set_cpu() { echo "$1" | sudo tee $POL/scaling_governor >/dev/null && echo "$2" | sudo tee $POL/scaling_min_freq >/dev/null; }
restore() { kill $KEEPER 2>/dev/null; set_cpu ondemand 312500; echo "restored: $(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) temp=$(temp)C" | tee -a $LOG; }
trap restore EXIT
( while sleep 240; do sudo -n -v; done ) >/dev/null 2>&1 & KEEPER=$!

echo "label,pair,mode,iter,us,cpu_classical,cpu_pq" > $RAW
: > $LOG
for clk in default:ondemand:312500 min750:ondemand:750000 fixed1500:performance:312500; do
    IFS=: read -r cname gov min <<< "$clk"
    set_cpu $gov $min || { echo "cannot set $clk" | tee -a $LOG; exit 1; }
    sleep 5
    for lib in opt bitmanip; do
        echo "== clock=$cname ($gov, min $min) lib=$lib start $(date -u +%T) temp=$(temp)C" | tee -a $LOG
        ./parverify_$lib $ITERS $PAUSE ${lib}_${cname} > last_pv.out 2>&1
        grep '^#' last_pv.out | tee -a $LOG
        grep '^RAW,' last_pv.out | cut -d, -f2- >> $RAW
    done
done
echo "rows: $(($(wc -l < $RAW) - 1))" | tee -a $LOG
echo PARVERIFY_DONE
