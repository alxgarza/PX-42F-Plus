---
name: fault
description: Decode a PX-42F crash from its boot-screen report (FAULT coreN PC=… LR=… A=… C=… S=… CHECKPOINT) to source lines, the faulting instruction and the fault type. Use when the user reports a FAULT or sends a photo of one.
argument-hint: PC LR [A] [C]
---

Decode the crash `$ARGUMENTS` (if no arguments, read the values from the user's message or
photo: `FAULT coreN PC=… LR=… A=… C=… S=… <checkpoint>`).

1. Run `tools/fault.sh PC LR A C`. It needs `build/PX-42F-Plus.elf` from the same build that
   was flashed — if the code changed since, say the decode may be off (check the ELF time
   stamp it prints against when the user flashed).
2. Read the source around the PC and the LR call site.
3. Interpret, using the report's fields:
   - `C` is the CFSR (the script names the bits): PRECISERR + BFARVALID → `A` is the bad
     data address; UNDEFINSTR/INVSTATE → executing garbage (often a corrupted return
     address or stack); UNALIGNED with a stack op → corrupted SP.
   - `S` = core 1 stack high-water mark (bytes) — near 32768 means stack overflow.
   - The checkpoint (`debug_checkpoint.h`) is the last thing the main loop marked.
   - A PC in the middle of an instruction, or a stack access just past 0x20082000 (top of
     core 0's stack in SCRATCH_Y), means stack/return-address corruption, not a bug in that
     function — look for what ran just before (interrupts, sleep, flash writes; see the
     "Hard-won rules" in CLAUDE.md).
4. Explain the cause plainly and propose the fix; don't guess past what the evidence shows.
