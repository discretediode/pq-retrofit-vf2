#!/bin/bash
# Run micro_bitmanip.sh twice at a fixed 1.5 GHz clock (performance governor) so the
# builds are compared without frequency scaling noise; restore the default afterwards.
POL=/sys/devices/system/cpu/cpufreq/policy0
OUT=${1:-bitmanip_micro_fixed1500.txt}
restore() { echo ondemand | sudo tee $POL/scaling_governor >/dev/null; echo 312500 | sudo tee $POL/scaling_min_freq >/dev/null;
            echo "restored: $(cat $POL/scaling_governor) min=$(cat $POL/scaling_min_freq) temp=$(awk '{print $1/1000}' /sys/class/thermal/thermal_zone0/temp)C" | tee -a $OUT; }
trap restore EXIT
echo performance | sudo tee $POL/scaling_governor >/dev/null || exit 1
sleep 3
{ echo "fixed clock: governor=$(cat $POL/scaling_governor) cur=$(cat $POL/scaling_cur_freq) kHz temp=$(awk '{print $1/1000}' /sys/class/thermal/thermal_zone0/temp)C"
  for pass in 1 2; do echo; echo "==================== PASS $pass"; ./micro_bitmanip.sh; done; } > $OUT 2>&1
echo MICRO_FIXED_DONE
