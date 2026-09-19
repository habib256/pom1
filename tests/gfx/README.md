# tests/gfx — golden-image regression fixtures

*[← POM1 documentation index](../../doc/README.md)*

Frozen inputs + reference renders for POM1's headless graphics-regression
capture (`--dump-gen2-frame` / `--dump-tms-frame`, see [`doc/CLI.md`](../../doc/CLI.md)).
Driven by [`tools/test_gfx_regress.py`](../../tools/test_gfx_regress.py) and the
`gfx_regress_*` ctest entries.

## Why a *frozen* binary

The `.bin` here is committed so the regression tests the **emulator's renderer**,
not the cc65 toolchain: a CI box with a different cc65 would assemble slightly
different bytes and produce a different frame, which would be a false failure.
Loading a frozen `.bin` isolates the render path. The capture is deterministic
(`--dump-after-cycles`, a host-independent point in emulated time), so the golden
PNG matches byte-for-byte on any machine.

## Files

- `hgr_testcard.bin` — frozen GEN2 HGR test card (built from
  `sketchs/gen2/demo_hgr_mire/`, loads + runs at `$E000`).
- `hgr_testcard_gen2.png` — golden render (280×192) of the above on preset 11
  (Uncle Bernie GEN2 HGR) after 2,000,000 emulated cycles.
- `vsplits.apl` — **Uncle Bernie's own** vertical-split demo, byte for byte as he
  sent it (Applefritter PM, 16 sept. 2026). A WOZMON dump, `280R`: fills the
  text page with `$80-$FF`, then every field syncs to V-blank on HST0, counts
  H-blanks and throws TEXT/GR so a 32-scanline text window scrolls one line per
  field over LORES. Driven by `gen2_vsplits_smoke` (not a golden image — the
  test asserts the motion and the top text row, which hold for any power-on
  phase). Also a good manual check: `--preset 11 --load 0280:tests/gfx/vsplits.apl
  --run 0280`.

## Regenerate a golden (after an intended renderer change)

```sh
python3 tools/test_gfx_regress.py --card gen2 --preset 11 \
    --load 0xE000:tests/gfx/hgr_testcard.bin --run 0xE000 \
    --golden tests/gfx/hgr_testcard_gen2.png --update
```

Review the new image before committing — a golden update is a deliberate "the
render changed on purpose" act.
