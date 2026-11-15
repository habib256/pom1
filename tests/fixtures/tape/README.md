# Cassette fixtures

Third-party recordings that POM1's cassette import must keep loading through
the real Woz ACI ROM. Used by `tests/aci_tape_loading_test.cpp` in its
`--robustness` mode (ctest `aci_tape_robustness`).

| File | What it is |
|---|---|
| `c2t_hello.mon` | The program, as a Woz Monitor dump: 20 bytes at `$0300-$0313` that print `HI` and return to the Monitor. Load with `C100R` then `0300.0313R`. |
| `c2t_hello_44100_8bit.wav` | `c2t_hello.mon` encoded by **c2t**, 44 100 Hz, 8-bit mono. |
| `c2t_hello_11025_8bit.wav` | The same, 8-bit mono — **8 000 Hz** despite its name (the header says `0x1F40`). One sample is 128 CPU cycles against a 238-cycle "0" half-period: the hardest rate POM1 is asked to read. Kept under its original name. |

**Provenance.** Generated with c2t by Egan Ford (<https://github.com/datajerk/c2t>,
`c2t -1 c2t_hello.mon out.wav`), an encoder independent of both POM1 and the
ACI ROM — which is the point: these pin interoperability with a tape POM1 did
not write. Copied from the RDK Apple-1 repository
(<https://github.com/Old-Wirehead/rdk-apple1>, `tests/data/`, commit `0ef0d79`,
GPL-3.0, same licence as POM1).

The test also derives damaged variants of the 44.1 kHz file in memory (level,
DC, hum, noise, phase-inverted stereo, one-sided stereo, tape speed, low-pass);
nothing derived is stored here.
