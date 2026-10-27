#!/bin/bash
# Decodes a PX-42F fault screen: tools/fault.sh PC LR [A] [C]
# (hex, with or without 0x). Needs build/PX-42F-Plus.elf from the SAME build
# that was flashed.
set -e
cd "$(dirname "$0")/.."
ELF=build/PX-42F-Plus.elf
[ -f "$ELF" ] || { echo "no $ELF -- build first"; exit 1; }
hex() { echo "0x${1#0x}"; }
PC=$(hex "$1"); LR=$(hex "$2")
echo "ELF: $ELF ($(stat -f '%Sm' "$ELF"))"
echo "== PC $PC"
arm-none-eabi-addr2line -f -C -i -e "$ELF" "$PC"
# LR has the Thumb bit set and points after the call: look one byte back.
LRCALL=$(printf '0x%x' $(( LR - 1 )))
echo "== LR $LR (call site)"
arm-none-eabi-addr2line -f -C -i -e "$ELF" "$LRCALL"
echo "== code around PC"
START=$(printf '0x%x' $(( (PC & ~1) - 24 ))); STOP=$(printf '0x%x' $(( (PC & ~1) + 16 )))
arm-none-eabi-objdump -d "$ELF" --start-address="$START" --stop-address="$STOP" | tail -n +7
if [ -n "$3" ]; then echo "== A (fault address) $(hex "$3")"; fi
if [ -n "$4" ]; then
  C=$(( $(hex "$4") ))
  echo "== C (CFSR) $(hex "$4"):"
  names=(IACCVIOL DACCVIOL - MUNSTKERR MSTKERR MLSPERR - MMARVALID
         IBUSERR PRECISERR IMPRECISERR UNSTKERR STKERR LSPERR - BFARVALID
         UNDEFINSTR INVSTATE INVPC NOCP STKOF - - - UNALIGNED DIVBYZERO)
  for i in "${!names[@]}"; do
    [ "${names[$i]}" != "-" ] && (( C & (1 << i) )) && echo "   ${names[$i]}"
  done
  true
fi
