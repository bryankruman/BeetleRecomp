#!/usr/bin/env bash
# Post-process N64Recomp output. Run AFTER `./N64Recomp BeetleRecomp.toml`, BEFORE building.
#
# Works around an N64Recomp codegen quirk: a load into $zero (`lw $zero, ...`) is emitted as
# `0 = MEM_W(...)`, which is not valid C ($zero is the literal 0). Rewrite to `(void)MEM_W(...)`
# so the read still happens (for side effects) but the bogus assignment is dropped.
#
# Second quirk: recomp_overlays.inl emits some relocs with an empty `.type` field
# (`.type =  }`). These are MIPS reloc types beyond N64Recomp's reloc_names table (index > 7,
# e.g. GOT16/CALL16/GPREL32). librecomp's RelocEntryType only covers 0..7 and the recompiled
# code resolves addresses inline via RELOC_HI16/LO16 (the section reloc table is read only by the
# mod system), so these entries are inert for running the base game. Rewrite them to R_MIPS_NONE
# so the table is valid C++.
set -euo pipefail
RF="$(cd "$(dirname "$0")/.." && pwd)/RecompiledFuncs"
[ -d "$RF" ] || { echo "fix-recompiled: $RF not found (run N64Recomp first)"; exit 1; }
before=$( { grep -rhcE '^[[:space:]]*0 = ' "$RF"/*.c 2>/dev/null || true; } | awk '{s+=$1} END{print s+0}')
sed -i -E 's/^([[:space:]]*)0 = /\1(void)/' "$RF"/*.c
echo "fix-recompiled: rewrote $before \$zero-load line(s) to (void)MEM_*(...)"

inl="$RF/recomp_overlays.inl"
if [ -f "$inl" ]; then
    reloc_before=$(grep -c '\.type =[[:space:]]*}' "$inl" || true)
    sed -i 's/\.type =[[:space:]]*}/.type = R_MIPS_NONE }/g' "$inl"
    echo "fix-recompiled: rewrote $reloc_before empty reloc .type field(s) to R_MIPS_NONE"
fi

# Overlay bridge: rename the generated uvDoModuleRelocs DEFINITION so src/main/overlay_bridge.cpp
# can own the `uvDoModuleRelocs` symbol. Its wrapper registers the just-loaded relocatable module
# with librecomp (load_overlay_by_id) so the module's recompiled functions resolve, then calls
# uvDoModuleRelocs_orig. Only the RECOMP_FUNC definition is renamed; all call sites bind to the
# wrapper. Idempotent (no-op once renamed).
if grep -rlq 'RECOMP_FUNC void uvDoModuleRelocs(' "$RF"/*.c 2>/dev/null; then
    sed -i 's/RECOMP_FUNC void uvDoModuleRelocs(/RECOMP_FUNC void uvDoModuleRelocs_orig(/' "$RF"/*.c
    echo "fix-recompiled: renamed uvDoModuleRelocs definition -> uvDoModuleRelocs_orig (overlay bridge)"
fi

# Hardware-register stubs: libultra functions recompiled raw that poke RCP registers (AI/PI/SP),
# which aren't memory-mapped in the recomp. Rename each generated definition so src/main/hw_stubs.cpp
# can own the symbol with a safe stub. Add a name here AND a stub in hw_stubs.cpp together.
for fn in func_8000E460; do
    if grep -rlq "RECOMP_FUNC void ${fn}(" "$RF"/*.c 2>/dev/null; then
        sed -i "s/RECOMP_FUNC void ${fn}(/RECOMP_FUNC void ${fn}__hwstub_orig(/" "$RF"/*.c
        echo "fix-recompiled: stub-renamed ${fn} (raw hardware-register access)"
    fi
done

# ---------------------------------------------------------------------------
# BAR runtime correctness fixes baked into the recompiled MIPS. These are regenerated on every
# N64Recomp run, so we re-apply them here. Each rule is anchored on a STABLE N64 instruction address
# (the `// 0x........:` comment), never on a funcs_NN.c file number, so they survive re-chunking.
# All rules are idempotent (no-op if already applied).
# ---------------------------------------------------------------------------

# (A) Heap cap: _uvMemAllocInit (0x80002A88) sizes the heap as 0x80400000 - gMemBlock (4 MB), and
# func_80005074 (0x80005074) bounds-checks pointers against the same 0x80400000 top. BAR is a 4 MB game
# but the recomp backs 8 MB RDRAM, so the upper [0x80400000,0x80800000) is unused Expansion-Pak space.
# Extend BOTH constants to 0x80800000 to use it (fixes the player-race _uvMemAlloc OOM on Coventry Cove).
# The gfx-manager framebuffer reserve (0x80400000 - size, in func uvgfxmgr 0x869004B0) is LEFT in place,
# so this only ADDS the upper 4 MB as free heap above the framebuffers. Lines look like
#   ctx->rNN = S32(0X8040 << 16);   immediately after the 0x80002AA4 / 0x800050B0 address comment.
heap_n=0
for f in "$RF"/*.c; do
    awk '
        /\/\/ 0x80002AA4:|\/\/ 0x800050B0:/ { armed=1; print; next }
        armed && /S32\(0X8040 << 16\)/      { sub(/0X8040 << 16/, "0X8080 << 16"); armed=0; print; next }
        { armed=0; print }
    ' "$f" > "$f.tmp"
    if ! cmp -s "$f" "$f.tmp"; then mv "$f.tmp" "$f"; heap_n=$((heap_n+1)); else rm -f "$f.tmp"; fi
done
echo "fix-recompiled: extended heap cap 0x80400000 -> 0x80800000 in $heap_n file(s)"

# (B) Controller input: __osPackReadData (func_8000E6E0) loads __osMaxControllers (@0x80032231) into r12
# at 0x8000E718, but BAR's reimplemented osContInit leaves it 0, so ZERO read-button commands get packed
# and no input reaches the game. Force it to MAXCONTROLLERS(4) right after the load so a real
# CONT_CMD_READ_BUTTON is packed (the high-level menu/race input path depends on this).
in_n=0
in_fix='    if ((int32_t)ctx->r12 <= 0) ctx->r12 = 4; // BAR FIX: __osMaxControllers left 0 -> force MAXCONTROLLERS so READ_BUTTON is packed'
for f in "$RF"/*.c; do
    if grep -q '0x8000E718: lbu' "$f" && ! grep -q 'BAR FIX: __osMaxControllers' "$f"; then
        awk -v fix="$in_fix" '
            { print; if (prev ~ /0x8000E718:/ && $0 ~ /ctx->r12 = MEM_BU\(ctx->r4, 0X0\);/) print fix; prev=$0 }
        ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
        in_n=$((in_n+1))
    fi
done
echo "fix-recompiled: applied __osMaxControllers input fix in $in_n file(s)"

# (C) RSPRecomp audio ucode (rsp/aspMain.cpp): same $zero-load codegen quirk as above (`0 = RSP_MEM_*`).
# Rewrite to (void) so the read still happens. Only if the file exists (regenerated by RSPRecomp).
ASP="$(cd "$(dirname "$0")/.." && pwd)/rsp/aspMain.cpp"
if [ -f "$ASP" ]; then
    asp_before=$( { grep -cE '^[[:space:]]*0 = ' "$ASP" || true; } )
    sed -i -E 's/^([[:space:]]*)0 = /\1(void)/' "$ASP"
    echo "fix-recompiled: rewrote $asp_before \$zero-load line(s) in rsp/aspMain.cpp"
fi

# (D) Intro skip: func_intro_004005CC (the attract-cinematic per-segment check at 0x81D005CC) already lets
# the player skip the attract to the main menu by pressing START (0x1000) or A (0x8000). The game reads
# input at 0x81D00618 (ANDs the START bit into r24). Inject a B (0x4000) check right after so B ALSO skips,
# giving press-to-skip on A/B/Start for the attract cinematics.
fb_n=0
fb_fix='    { extern int bar_intro_skip(void); if (bar_intro_skip() && (ctx->r5 & 0X4000)) ctx->r24 = 0X1000; }  // BAR: B also skips the attract cinematic (game already skips on START/A)'
for f in "$RF"/*.c; do
    if grep -q '0x81D00618: andi' "$f" && ! grep -q 'B also skips the attract' "$f"; then
        awk -v fix="$fb_fix" '
            { print; if (prev ~ /0x81D00618:/ && $0 ~ /ctx->r24 = ctx->r5 & 0X1000;/) print fix; prev=$0 }
        ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
        fb_n=$((fb_n+1))
    fi
done
echo "fix-recompiled: applied attract intro-skip (B button) in $fb_n file(s)"

# (E) Audio underrun on state transitions: func_uvcmidi_rom_00400940 (0x85600940) stops the MIDI sequence
# player then SPINS until alSeqpGetState()==0 (player actually stopped) OR a 2.0s timeout. The player state
# only advances when the AUDIO thread runs, but this spin never yields, so under ultramodern's cooperative
# (no-preemption) scheduler the audio thread is starved and the loop burns the FULL 2.0s timeout on EVERY
# state transition (uvSetGameState unloads MIDI each time) -> the host audio queue drains -> underrun
# crackle. Inject a 1ms cooperative yield at the loop top (label L_85600984) so the audio thread gets to
# stop the player and the loop exits in a few ms (matching real-N64 preemptive behaviour).
midi_n=0
midi_fix='    { extern void yield_self_1ms(uint8_t* rdram); yield_self_1ms(rdram); }  // BAR FIX: MIDI stop-wait — pump the audio thread so the seq player actually stops (else this loop spins the full 2.0s timeout, starving audio -> underrun crackle on every state transition)'
for f in "$RF"/*.c; do
    if grep -q '^L_85600984:' "$f" && ! grep -q 'BAR FIX: MIDI stop-wait' "$f"; then
        awk -v fix="$midi_fix" '
            { print; if ($0 ~ /^L_85600984:/) print fix }
        ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
        midi_n=$((midi_n+1))
    fi
done
echo "fix-recompiled: applied MIDI stop-wait yield (no audio underrun on transitions) in $midi_n file(s)"

# (F) GENERAL cooperative preemption (generalizes rule E from one spin loop to ALL long game-thread compute).
# ultramodern has no preemption: a thread holds the run token until it does an os message op, so a heavy
# straight-line per-frame update (physics/AI/collision/gfx-list build — verified to contain NO osRecvMesg/
# osSendMesg in any gameplay module) starves the pri-110 audio manager and underruns the host audio queue.
# Inject a poll of a host-set "should-yield" flag (raised ~500Hz by src/main/bar_preempt.cpp) at EVERY
# function prologue; when set, on a game thread, call yield_self_1ms so the audio manager runs. bar_consume_yield
# self-clears the flag (=> at most one yield per ~2ms tick, not one per call) and gates to game threads, so the
# steady cost is a single relaxed-atomic load per call. Uses yield_self_1ms (NOT yield_self, which would block
# the game thread up to ~16ms). Anchor: the per-function-body line `int c1cs = 0;` (emitted once in every
# recompiled function body; absent from forward declares and from rsp/aspMain.cpp, so the RSP task thread is
# never touched). Idempotent via the BAR-FIX sentinel.
preempt_n=0
preempt_fix='    { extern int bar_consume_yield(uint8_t* rdram); extern void yield_self_1ms(uint8_t* rdram); if (bar_consume_yield(rdram)) yield_self_1ms(rdram); }  // BAR FIX: cooperative-preempt (audio)'
for f in "$RF"/*.c; do
    if grep -q '^    int c1cs = 0;' "$f" && ! grep -q 'BAR FIX: cooperative-preempt' "$f"; then
        awk -v fix="$preempt_fix" '
            { print; if ($0 ~ /^[[:space:]]*int c1cs = 0;[[:space:]]*$/) print fix }
        ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
        preempt_n=$((preempt_n+1))
    fi
done
echo "fix-recompiled: applied cooperative-preempt prologue poll in $preempt_n file(s)"

