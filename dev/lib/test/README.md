# dev/lib/test — unit-level 6502 micro-tests

*[← dev/lib index](../README.md)*  ·  harness: [`../../../tools/test_lib_micro.py`](../../../tools/test_lib_micro.py)

Execution-level unit tests for the routines under `dev/lib/`. Unlike the
top-level suite (which validates libs **indirectly**, only when some
`sketchs/` build happens to link them), each driver here loads **one** lib,
runs it headless inside POM1, and asserts **real values** read back from a RAM
"result mailbox" — no golden hashes, no rendering. Registered as the CMake test
`lib_micro_tests` (skips `77` without cc65 or `build/POM1`).

## Why the peripheral drivers had no test

Not because they are untestable. `a1io/`, `sd/`, `wifi/`, `gt6144/` and
`text40/` had no driver here because **three separate things in this harness
stopped at the same place**, and each had to be fixed before the first one
could run:

1. **The include list was hardcoded** (`ASM_INCLUDE_DIRS`) and did not contain
   them, so `ca65` could not even find their own `.inc`. It now also gets the
   directory of every module a driver names, so a new module needs no edit here.
2. **Only ONE of the two integration models was implemented.** `dev/lib/README.md`
   is explicit that a file arrives either as a separately-compiled object
   (`LIBS:`) or as a textual `.include` — and *every* peripheral driver is the
   second kind. `.import` finds no export in them and `LIBS:` would link a
   second copy. The `INCLUDES:` key is that missing model.
3. **A driver could only use what its preset happened to plug.** `ENABLE:` and
   `ARGS:` fix that — and `ARGS: --rtc-freeze` is what makes a clock assertable
   at all, since a ticking one has no expected value.

One companion rule comes with `INCLUDES:`, and `micro.cfg` already documented
it: put the driver's `main` in **`.segment "ENTRY"`**. An included lib's code
lands in `CODE`, so a `main` in `CODE` sits behind it and `--run 0300` enters
the library instead of the test.

## How a run works

`tools/test_lib_micro.py` exploits POM1's headless CLI executing its verbs in
argv order:

```
POM1 --headless --preset N --silicon-strict \
     --load 0300:test.bin --run 0300 --step NSTEPS \
     --snapshot-save out.snap --dump-tms-frame scratch.png --dump-after-cycles 1000
```

The driver is assembled/compiled (`ca65`+`ld65` for `.s`, `cl65` for `.c`),
cold-started, single-stepped `NSTEPS` instructions, then a full machine snapshot
is saved. The harness parses the snapshot's `MEM` section and compares the
mailbox bytes against the `EXPECT:` lines in the driver header. The trailing
frame dump is just an exit ticket so the process terminates cleanly.

Every run also asserts **zero `[TMS9918 DROP`** lines on stderr — so each
micro-test doubles as a silicon-strict pacing check for the routine (catches
unpadded back-to-back VDP access; not marginal 9–16c pacing — see the caveat in
the harness docstring).

## The mailbox contract ([`micro/micro.cfg`](micro/micro.cfg))

```
$0000-$00FF  ZEROPAGE   driver + lib .exportzp slots
$0300-$0DFF  CODE+RODATA loaded via --load 0300, entered at 0300
$0E00-$0EFF  BSS        lib scratch (tri_*, shadowcast map/vis…)
$0F00-$0F7F  MAILBOX    never allocated by ld65; driver writes raw absolute
                        bytes here ($0F00 = magic $A5 written LAST). Keep BSS < $0F00.
```

## Driver header contract

Comment lines (`;` for `.s`, `*`/`//` for `.c`):

```
POM1-LIB-MICRO-TEST          required marker
LIBS:  path ...              lib sources, relative to dev/lib
CFG:   micro.cfg             linker cfg
PRESET: 1                    --preset index (default 1)
MODE:  codetank              .c only: build a 32 KB CodeTank ROM, boot 4000R
LOAD/RUN: 0300               .s load/entry address (default 0300)
STEPS: 120000                --step budget (driver spins at end)
EXPECT: ADDR B0 B1 ...       hex bytes expected at ADDR (repeatable)
```

## Current drivers ([`micro/`](micro/))

| Driver | Lib under test |
|---|---|
| `t01_m1_init_sat.s` | `tms9918/tms9918m1` — `init_vdp_g1` + `disable_sprites` |
| `t02_m1_rw_roundtrip.s` | `tms9918/tms9918m1` — `vdp_set_write` / `vdp_set_read` |
| `t03_pad18_transparency.s` | `tms9918/tms9918_pad` — `pad18` register+flag contract |
| `t04_lr_fill.s` | `basicrt/basicrt_tms` — `lr_fill` / `lr_setw` / `lr_setr` |
| `t05_sprite_attr_terminator.s` | `tms9918/sprite_triangle` — SAT `$D0` terminator dodge |
| `t06_math_vectors.s` | `m6502/math.asm` — shared-ZP arg/tmp window semantics |
| `t07_rand_mod.s` | `games/rogue/dungeon` — `rand_mod` + `prng16` |
| `t08_shadowcast.s` | `games/rogue/shadowcast` — FOV + index math |
| `t09_c_sprite_dodge.c` | `tms9918c` (C) — `tms_set_sprite` / SAT terminator |
| `t10_m1_upload_name_at.s` | `tms9918/tms9918m1` — `name_at_rc` + `vdp_upload_a` |
| `t18_hgr_clear_tables.s` | `gen2/hgr_clear` + the RAM-built tables (`hgr_scanline_ram`, `HGR_PLOT_TABLES_RAM`, `hgr_div7_ram`) |
| `t19_hgr_flip_pages.s` | `gen2/hgr_flip` + `gen2/hgr_pages` — the two double-buffering schemes |
| `t20_hgr_xor.s` | `gen2/hgr_xor` — XOR rectangle / pre-shifted sprite, drawn twice = restored |
| `t21_hgr_clear_rows.s` | `gen2/hgr_clear_rows` — viewport / HUD / visible / row clears keep holes, HUD and the other page |
| `t22_hgr_plot16_line16.s` | `gen2/hgr_plot16` + `hgr_line16` — 280-px X, clipping, XOR keeps the palette bit |
| `t23_hgr_plot_hist.s` / `t24_hgr_plot_hist_fast.s` | `gen2/hgr_plot_hist` — erase history, plain and FAST (`pl_erase_to`, `pl_sprite`) |
| `t25_hgr_hud8.s` | `gen2/hgr_hud8` — differential HUD fields (the 0 − 65535 = +1 wrap trap) |
| `t26_apple1_print_num.s` | `apple1/print_num` — all 256 inputs, Y kept, C not trusted across ECHO |
| `t27_apple1_kbd.s` | `apple1/kbd` — `poll_key` / `wait_key` register + flag contract (KBD/KBDCR in RAM) |
| `t28_apple1_delay.s` | `apple1/delay` — `delay_ms_a` cycle budget, timed by the A1-IO VIA's Timer 1 |
| `t29_sid_player_notes.s` | `sid/sid_player` — frequency of all 96 notes (12-note table + octave shift) |
| `t30_math_div10.s` | `m6502/math` — `div_arg_by_10` over 516 dividends, self-checked |
| `t31_tms_xfer.s` | `tms9918/tms9918_xfer` — store-and-pad helpers + 16-bit fill/copy (fenceposts, registers, flags) |
| `t32_tms_text_charmap.s` | `tms9918/tms9918_text` — pre-reversed `upload_charmap` + fills |
| `t33_tms_m2_glyph.s` | `tms9918/tms9918m2` + `text_bitmap` — init, `plot_set`, `clear_bitmap`, glyph blits |
| `t34_m1_upload16_clear.s` | `tms9918/tms9918m1` — 16-bit `vdp_upload` + `clear_name_table` |
| `t35_m1_pair_sei.s` | `tms9918/tms9918m1` built with `TMS_PAIR_SEI = 1` — the I flag survives the masked pairs |

| `t36_gfx_utoa.s` | `gfx/gfx_num_dec.s` + `gfx_num_signed.s` — the one decimal converter, wide sweep vs an ASCII-counter oracle |
| `t37_gfx_hexstr.s` | `gfx/gfx_num_hex.s` — `gfx_hexstr`, wide sweep vs a nibble-counter oracle |
| `t38_c_hgr_clear.c` | `gen2c/gen2_hgr_clear_asm.s` (C) — unrolled self-modifying clear: lazy page-1 default, page 2, re-patch, window bounds |
| `t39_c_line.c` | `gfx/gfx_line_gen2.s` (C) — GEN2 `gfx_line`, pixel-identical to `gfx_line.c` on 56 lines, both pages |
| `t40_c_lores.c` | `gen2c/gen2_lores_*.s` (C) — LORES blocks, spans, rects, every clip, nibble kept, page 2, holes kept |
| `t41_c_sprmask.c` | `gen2c/gen2_sprmask.s` (C) — masked draw / save / restore, phases 0-6, x >= 256, clips, widths 2/3/4 |
| `t42_c_tables.c` | `gen2c/gen2_tables.s` + `gen2_page.s` (C) — tables, `gen2_hgr_row` for all y, page flips incl. LORES |
| `t43_c_rect.c` | `gen2c/gen2_pixrect_asm.s` + `gen2_fill_rect_asm.s` (C) — 60 pixel / byte rectangles vs a C reference |
| `t44_c_inflate_x2_all.c` | `gen2c/gen2_hgr_x2.s` (C) — x2 inflate, all eight colour codes, guards, wbytes 0 writes nothing |
`t18`–`t25` link with [`micro/micro_gen2.cfg`](micro/micro_gen2.cfg) (code at
`$6000`, `BSS` at `$9000`, preset 11): the GEN2 modules and their tables do not
fit `micro.cfg`'s `$0300-$0DFF` + 256-byte `BSS`.

Each header's `GUARDS:` block names the exact bug class it pins (e.g. `t06` pins
the shared-ZP argument window whose clobber let a `Maze3D` bug survive for
months).

## Adding a test

Drop `tNN_<name>.{s,c}` in [`micro/`](micro/) with the header contract above,
write the routine's inputs, and store outputs at `$0F01+` (magic `$A5` at `$0F00`
written **last**). No CMake edit needed — the harness discovers every driver in
`micro/`. Keep any BSS below `$0F00`.

> **Sibling test**: [`../apple1/test/`](../apple1/test/) is a *structural*
> (link-time) pin, not an execution test — it runs nothing but makes `ld65`
> fail if the `apple1/zp.inc` slot pool drifts.
