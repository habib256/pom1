# CLAUDE.md

New here? Read [`ARCHITECTURE.md`](ARCHITECTURE.md) first — the five concepts and the
dependency rule, in one pass. **This file is the index of invariants**: the rules a change
must not break, each one short, with a pointer to where its full story lives. It is
deliberately kept small because it is loaded into every session.

Architecture / invariants / gotchas for the **emulator side** of POM1. User walkthrough → `README.md`; open work → `TODO.md`; history → `CHANGELOG.md` / `git log`. **Full doc map → [`doc/README.md`](doc/README.md)**.

**Contents:** [Overview](#project-overview) · [Build](#build--run) · [Architecture](#architecture) · [Invariants](#invariants--gotchas) · [Memory map](#memory-map) · [Testing](#testing) · [Version bump](#version-string-locations)

### The six reference docs this file points into

| Doc | What moved there |
|---|---|
| [`doc/BUILD.md`](doc/BUILD.md) | Configure options, `-DPOM1_DEVTOOLS=OFF`, warnings & sanitizers, graphics backends, the GLES tier, WASM, the `.sketch.json` contract. |
| [`doc/UI_ARCHITECTURE.md`](doc/UI_ARCHITECTURE.md) | Docking, `PomRenderer`, interface zoom, the CRT stack, Screen/keyboard, the Bench and the four editors, and the extracted decision seams. |
| [`doc/PRESETS.md`](doc/PRESETS.md) | The 13 machines + external preset files, `applyMachineConfig`, per-preset ini, window geometry and fullscreen. |
| [`doc/LOADERS_AND_HOST.md`](doc/LOADERS_AND_HOST.md) | Memory-image / cassette / hex dialects, the audio seam, the log sink, tape rules. |
| [`doc/TESTING.md`](doc/TESTING.md) | The two lanes, per-module coverage, the CMake test model, the ratchets, and the catalogue of load-bearing pins. |
| [`doc/CLI.md`](doc/CLI.md) · [`doc/COMMAND_PORT.md`](doc/COMMAND_PORT.md) | CLI flags (impl `CliDispatcher.cpp`); the `--cmd-port` scripting channel (impl `CommandPort.cpp`, client `tools/pom1_control.py`). |

- Apple 1 software (BASIC, SID tunes, microSD shell tools, games) → `sketchs/doc/APPLE1DEV.md` + `sketchs/doc/Programming_Apple1_ASM.md`.
- DevBench / cc65 details → [`doc/DEVBENCH.md`](doc/DEVBENCH.md) + [`doc/CC65_WASM.md`](doc/CC65_WASM.md).
- GEN2 HGR card → [`doc/GEN2_RELEASE.md`](doc/GEN2_RELEASE.md).
- 6502 ASM sources for every shipped program → `dev/` (`lib/{apple1,m6502,tms9918,gen2,gen2c,games,…}/`, `cc65/`, `codetank/` = cartridge composition ONLY) and `sketchs/<card>/<name>/` (every program source, single- or multi-file). Compiled artefacts land under `software/<dir>/` — that's what POM1 loads. Official release packages bundle `dev/` (linker cfgs + runtime libs) next to the cc65 toolchain so the in-app DevBench compiles asm/C; a bare source build without cc65 omits both.

## Project Overview

Apple 1 emulator (Dear ImGui, MOS 6502 + display + keyboard + ACI cassette) plus expansion cards: Uncle Bernie's GEN2 HGR, P-LAB A1-SID (6581/8580), TMS9918, microSD (65C22+ATMEGA), **IEC daughterboard** (1541 drive on microSD's spare VIA pins), MODEM BBS (65C51+TCP), Terminal Card, A1-IO & RTC, Juke-Box, CodeTank, Rich Dreher's CFFA1, SWTPC GT-6144 + PR-40 (1976, Jobs' *Interface Age* mod), **Uncle Bernie's Extended ACI** ($C500 page). Linux / macOS / Windows / Web (Emscripten).

## Build & Run

```bash
./setup_pom1.sh              # one-time deps (Linux/macOS)
cd build && cmake .. && make # the app target is `pom1_imgui`; the output file is `POM1`
./run_emulator.sh            # runs from repo root
```

**Full build reference → [`doc/BUILD.md`](doc/BUILD.md).** What must not be forgotten here:

- **`imgui/` is NOT vendored** — a local clone of the **docking branch**, pinned by the repo-root **`IMGUI_VERSION`** file (`<git tag> <IMGUI_VERSION_NUM floor>`). Bump it **there and nowhere else**; `tools/check_imgui_pin.sh` (ctest `imgui_pin_sync`) fails any file that disagrees. `tools/ensure_imgui.sh` owns acquisition.
- **The development environment is optional — `-DPOM1_DEVTOOLS=OFF`.** The editors, DevBench and BASIC compilers are a *second product* in the same process (~18 300 lines). The emulator is complete without them. Source list `POM1_DEVTOOLS_SOURCES` in `CMakeLists.txt`, macro `POM1_DEVTOOLS` in `src/POM1Build.h`. The include directories come off the target too, so an editor include escaping its `#if` fails to compile rather than quietly pulling 87 files back in.
- **Graphics backend** is the `POM1_RENDERER` cache option (`opengl` / `metal`): Metal on macOS-non-WASM, OpenGL everywhere else. Single seam in `src/PomRenderer.h` — **no direct `gl*` calls outside `PomRenderer_GL.cpp` / `PomRenderer_Metal.mm`**.
- **`POM1_GL_ES`** (`src/POM1Build.h`) says *"we speak GLES"*, **not** *"we are a browser"*. On under WASM and natively via `-DPOM1_GLES=ON` (Raspberry Pi 4/5). Keep any new shader body inside **GLSL 1.30** or the desktop-GL fallback cascade (3.2→3.1→3.0, 150→140→130) is a lie.
- **Warnings:** POM1's own sources build `-Wall -Wextra` (`/W4`), applied per source file; vendored code is not ours to keep clean. All five CI tiers carry `-DPOM1_WERROR=ON` (CI only, never a local default). Sanitizers: `-DPOM1_SANITIZE=address,undefined|thread`; fuzzers: `-DPOM1_FUZZERS=ON`. Both are nightly jobs.
- **One sketch, one manifest — `.sketch.json`**, pinned by ctest `sketch_manifests_sync`. `Pom1BenchCc65::probeAsmProject` reads it FIRST and falls back to parsing the sibling Makefile **literally**. A sketch naming its config as `CFG :=` builds one binary under `make` and a different one in the DevBench. `EXTRA_ASM` is the one vocabulary for "modules this sketch assembles", in literal relative paths.
- **WASM:** `source emsdk_env.sh && emcmake cmake .. && emmake make && emrun POM1.html`. The `sketchs/` + `dev/` staging copies are **deny-lists**, never allow-lists — an allow-list makes a newly-introduced extension vanish from the web build only, silently.

## Architecture

All C++ sources under **`src/`** (vendored deps in `src/third_party/`). `imgui/`, `tests/`, `tools/` stay at repo root. File references below are bare basenames — find them under `src/`.

### Parmigiani's golden rule — "one board at a time"

Claudio PARMIGIANI (P-LAB designer): on real hardware exactly ONE P-LAB card is plugged. The 6502 bus has no arbitration and many P-LAB cards overlap windows (A1-SID `$C800-$CFFF` vs TMS9918 `$CC00/$CC01`; A1-IO RTC `$2000-$200F` vs GEN2 HGR `$2000-$3FFF`; Juke-Box claims `$4000-$BFFF`; microSD's Applesoft Lite RAM window `$6000-$7FFF` vs CodeTank `$4000-$7FFF` — plugging either evicts the other, TMS9918 host stays). POM1 **breaks this on purpose** in the "Multiplexing Fantasy" preset — fantasy, not buildable. Mutex rules elsewhere mirror real bus conflicts. When adding a card, honour the rule and document any intentional coexistence.

### Core layers

- **M6502** (CPU). `op = quint16`, `tmp = int` (carry/borrow via bit 8). BCD ADC propagates low→high carry from `(A & 0xF0)`, not the unadjusted sum. `run(maxCycles)` **returns actual cycle count** (overshoot up to 6) — wallclock pacers must deduct. 24-slot PC control-flow-edge ring + `dumpPcTrace(tag)`. `setTestMode(true)` = flat 64 KB for the Klaus harness only. **At most one interrupt per instruction boundary** — NMI takes priority over IRQ (7 cycles, 3 bytes pushed); never both. **Undocumented multi-byte opcodes advance PC by their real NMOS operand length** (`Unoff2`/`Unoff3` dispatch + matching disassembler addressing mode, mnemonic `???`) — never the no-op 1-byte fallback that desynced the stream.
- **CpuClock.h** — `POM1_CPU_CLOCK_HZ = 1 022 727` (14.31818 MHz ÷ 14). Single source of truth. `BeamClock.h` is the shared cycle→(line, tick) clock both video engines now use (`lineTickAt` is defined during blanking; `beamPosAt`'s `x` is not — sorting beam events by `x` reorders the VBlank events that set up the next frame).
- **Memory** — 64 KB. Owns every peripheral (`unique_ptr` + enable flag). MMIO dispatched via **`PeripheralBus`**; `memRead/memWrite` only handle PIA `$D0xx` aliasing, ROM write-protect, OOR strict mode, the cassette write-sniffer, `DisplayDevice::onChar` + TerminalCard hook, then raw `mem[]`. **Redundant-ROM-load guards**: skip the Woz Monitor reload when `mem[$FF00..$FF01] == D8 58`; skip SD CARD OS reload when `mem[$8000..$8001] == A9 00`.
- **The `Memory` facade is FROZEN** — no new public method, no new `Memory.h` include (`memory_public_methods` = 190, ratcheted). A file-local helper walking a registry `Memory` already owns is not a facade addition. `EmulationController`'s per-card passthroughs come off one at a time as their last caller goes.
- **Gen2VideoScanner owns the soft-switch journal** — the per-frame record of mid-line mode/page flips the renderer replays to split a frame, sitting beside the cycle counter that dates its entries.
- **PeripheralBus** — central MMIO dispatch. Peripherals register `(name, range, priority, onRead, onWrite)`. O(1) hot path via `pageMask[256]` bitmap, `std::stable_sort` by priority (TMS9918 wins over SID at `$CC00/$CC01` via priority 10). `onWrite = {}` → pass-through to RAM; explicit no-op = block. **A handler that writes `mem[]` itself must call `Memory::isRomWriteProtected(addr)` first** — `bus.tryWrite` answers at the TOP of `memWrite`, so its own ROM guard never runs. GEN2's `$C200-$C7FF` handler falls through to flat RAM for what it doesn't decode, which is what made the extended ACI PROM at `$C500` writable (pinned by `extended_aci_smoke` part A).
- **Card identity** — `CardTypes.h` owns the stable, append-only `CardId`, allocation-free `CardSet`, and `CardDescriptor`. Descriptors live in `Memory::cardSlots()`, the load-bearing snapshot registry: **topology work extends that registry, never a parallel card table**. `BusConflicts.h` keys on `CardId`, not display-name strings. A daughterboard requirement is deliberately not a conflict; overlapping cards resolved by bus priority are coexistence, not exclusion.
- **Card topology policy** — `CardTopology.{h,cpp}` is allocation-free pure policy over `CardSet`: active conflicts, gate a candidate, deterministically resolve a requested set. `TopologyMode::Fantasy` permits multiplexing; Strict additionally rejects the SID/TMS9918 overlap. The Silicon Strict UI consumes it by `CardId` and contains no card-name comparisons. `Memory::setXxxEnabled()` remain compatibility entry points but obtain attach/detach decisions from `planCardToggle()`; they own only hardware effects (bus handles, ROM mirrors, audio sinks, resets).
- **Whole-card transitions** — `CardTopology::planConfiguration()` closes dependencies, rejects the entire Strict request on any resulting conflict, and returns deterministic detach/configure/attach arrays (reverse `CardId` order detaches daughters before hosts). `EmulationController::applyCardConfiguration()` executes the plan under **one** `stateMutex` acquisition and publishes once; validation rejection mutates nothing. GUI deferred card changes travel as one `CardConfigurationRequest`. Single-card UI/CLI changes use `EmulationController::setCardEnabled(CardId, bool)`.
- **Peripheral lifecycle** — `Peripheral` tracks `Constructed → Attached → Reset → Active`; transitions are idempotent and skipped phases fail. ACI is special: it stays `Reset` until its cassette source is actually registered with the mixer, and returns to `Reset` when removed.
- **EmulationController** — façade over CPU + Memory + emulation thread, across **4 TUs**: `EmulationController.cpp` (CPU thread, run/stop/reset, step, breakpoints, key injection, pokes, the slice/pacing loop), `_State.cpp` (memory images, snapshots, rewind, ROM re-loads), `_Machine.cpp` (fidelity + diagnostic knobs), `_Cards.cpp` (cassette/audio + per-card passthroughs). The split is pure code motion. Native = dedicated thread; WASM = `pumpEmulationMainThread()`. Slice cap 6000 cycles desktop, 50 000 WASM.
- **Mutex order applies to every TU**: `stateMutex > rewindMutex > keyboard.keyMutex > publisher.snapshotMutex` (ranks 30/25/20/10 in `LockOrder.h`, pinned by `lock_order_smoke`, enforced at runtime in test/Debug builds and compiled out under `NDEBUG`). **Rewind capture is split across the two outer locks**: copy under `stateMutex`, release, delta-encode under `rewindMutex` alone; a `rewindGeneration_` bump retires a blob staged before a timeline edit.
- **`stateMutex` is a `PriorityMutex`, and its yield must actually block.** At MAX speed the emulation thread is a ~100 % duty-cycle holder: it releases and re-takes the lock within nanoseconds, and glibc's `std::mutex` is not FIFO, so a waiter is starved. `std::this_thread::yield()` **does not fix this** — the waiter is not runnable-and-competing, it is blocked in `futex_wait`, and yielding an already-free core on a multi-core host changes no scheduling decision; measured, it left the UI thread parked for tens of seconds while the emulation thread burned a core. `PriorityMutex::yieldToWaiters()` sleeps in 50 µs slices until the queue drains (capped), which puts the emulation thread *off* the mutex long enough for a woken waiter to win. Only entered when someone is queued. Pinned by `measured_cpu_rate_smoke` (4.4 s flat, was 18–135 s) and `concurrent_frontends_smoke` (topology swaps 1/27/5 → 22/60/46/23/35).
- **Snapshot restore is all-or-nothing — validate, then apply.** `Memory::loadSnapshotFromBuffer` runs `pom1::validateSnapshot()` (pure, `SnapshotIO.h`) before a byte of machine state moves, and `Memory::loadSnapshot(path)` **delegates to the buffer path** so a file and a rewind blob share the gate. The gate covers SHAPE, not a card's payload grammar — several cards restore fields before they can reject what follows — so the apply pass is wrapped in a **rollback** (16 µs to serialise against the 452 µs a restore already costs).
- **`pom1::readFileBounded()` (`FileBytes.h`) is the one place that reads a whole file for a parser** — memory images, cassette containers and snapshots. It checks the size **before** reading, because slurping the file IS the allocation the limit exists to prevent.
- **SnapshotPublisher** — SPSC slot, page-level dirty copy (1 bit per 256 B page, contiguous runs → single memcpy). Idle Wozmon = zero copy. TMS9918's 16 KB skipped when unplugged.
- **A rewind seek reads no ROM, and must keep not doing so** (`rewind_seek_cost_smoke`: zero ROM reads, 741 µs per seek). The guard lives in FIVE places, and the sharp part is `snapshotRestoreInProgress`: FLAGS is written AFTER MEM, so anything a setter does to memory during a restore lands **on top of** the 64 KB just put back. A new setter that clears its window without checking it would wipe restored RAM, and only while rewinding.
- **RewindBuffer** — recording is **OFF by default** (capture serialises ~80 KB 4×/s under `stateMutex`). Budget from host RAM via `defaultRewindBudgetBytes()` (~1/16 physical, clamped to [16 MB, 128 MB]). KEYFRAME + DELTA frames in segments; eviction drops whole leading segments. Touched only under `rewindMutex`.
- **ResourceLocator** (`src/ResourceLocator.{h,cpp}`) — **one** search order for POM1's data: the working directory and three ancestors, then the executable's directory and the packaged layouts around it (macOS `.app` `Resources/`, AppImage `share/POM1/`), deduplicated. **`defaultLocator()` returns BY VALUE and recomputes the cwd half per call** — the working directory is live state. An **absolute** path is returned as given, never rewritten. `rootedAt(dir)` is how a caller says "look ONLY here". **Every consumer routes through it**; `tools/check_resource_probes.py` (ctest `resource_probes_sync`) fails the 53rd hand-rolled `"x", "../x", "../../x"` walk.
- **Audio is a service the core is HANDED, never one it builds** — `pom1::IAudioService` (`src/AudioService.h`). `main_imgui.cpp` owns the `AudioDevice` (GUI **and** headless) and passes it down through `MainWindow_ImGui` → `EmulationController` → `Memory`. `initializeAudioHardware` defaults to **false** so a bare test core opens no OS sound device. `~Memory` unregisters its sources — an injected service outlives the machine. `pom1::NullAudioService` is the in-memory double.
- **DisplayDevice** — abstract `onChar(char)` sink for `$D012`. Injected via `Memory::setDisplayDevice()` so tests/peripherals can tee. How long PB7 stays busy afterwards is `TerminalTiming.h` (`FixedDelay` 17045 cycles is the default and what every shipped program is validated on; `FieldSync` 17030 is reasoned from the schematic, off everywhere, armed by `--display-field-sync`).

### UI (ImGui)

**Full UI reference → [`doc/UI_ARCHITECTURE.md`](doc/UI_ARCHITECTURE.md)** — docking, `PomRenderer`, interface zoom, the CRT stack, `Screen_ImGui`, the keyboard map, the Bench, the four editors, `NativeFileDialog`. The rules that bite:

- **`MainWindow_ImGui` is one class across 14 TUs** (`MainWindow_ImGui.cpp` + `_Dock`, `_Layout`, `_Presets`, `_Menu`, `_Dialogs`, `_Settings`, `_Tutorials`, `_HardwareWindows`, `_SiliconStrict`, `_FileDialogs`, `_DebugWindows`, `_Keyboard`, `_TMS9918Inspector`), all but `_Keyboard` sharing `MainWindow_Internal.h`.
- **Anything that is a decision rather than a draw call belongs OUTSIDE the UI.** The UI is in no test binary, which left POM1's largest layer uncovered — and that is exactly where the august-2026 bugs lived. Nine pure seams carry that logic today, each with its own test and **no ImGui and no GLFW**: `Apple1KeyMap`, `FullscreenExpand`, `WindowGeometry`, `LayoutDecisions.h`, `PresetDecisions.h`, `ShortcutTable.h`, `SoftwareDirRules.h`, `CommandPalette.h`, `TerminalTiming.h`. **Grow this seam when adding UI logic.**
- **The UI holds no copy of the machine's topology.** Sixteen `bool xEnabled` mirrors are gone; a second copy is a thing that can disagree, and that family is where the BBS auto-dial regression came from. Three members replace them: `currentCards()`, `cardPlugged(CardId)`, `setCardPlugged(CardId, bool)` — the last issues the command **and re-reads the snapshot**, so the rest of the frame sees the cascades it triggered.
- **`pom1::StagedCardConfiguration` is the UI's staging rail.** `request.cards` is an **ABSOLUTE** target set — `MachineCoordinator` detaches every card the request does not name — so the DTO alone cannot tell *"nothing staged"* from *"stage the empty machine"*, and the unguarded repeat swept every card off the bus (that is what broke BBS auto-dial). An untouched transaction commits **nothing**; the **first** `stage()` seeds cards and board options from the LIVE machine so an amend adds rather than replaces; `clear()` resets everything **except `mode`**.
- **`MachinePresets.{h,cpp}` must stay UI-free** — it may include peripheral headers but never `imgui.h`, `GLFW/glfw3.h` or any `MainWindow_*` header. `cli_dispatcher_smoke`'s real assertion is that it LINKS.
- **Interface zoom:** POM1's own chrome constants are **not** covered by `ScaleAllSizes` — every use site multiplies through `detail::uiPx()`, and every explicit widget size is authored at 100 % while the glyph inside it is the live font. Portable widgets read the same factor from `ImGuiStyle` (`FontScaleMain × FontScaleDpi`), never `MainWindow_Internal.h`.
- **Docking:** `renderDockSpace()` **must run before the first dockable `Begin()`** of the frame. Multi-viewport stays OFF. Widgets must not call `SetNextWindowSize(..., FirstUseEver)` — it overrides the preset.
- **Keyboard:** keys go GLFW → Apple 1 directly, bypassing `InputQueueCharacters`. **The binding table must never hold a CTRL+letter chord** — it dispatches before the Apple-1 sees the key, silently making that control code untypeable (`static_assert` in `MainWindow_Keyboard.cpp` + ctest `shortcuts_sync`). **Host Backspace sends `_` (`$5F`), never `$08`.**

### Peripherals

One `.cpp/.h` pair per card under `src/`. Bus windows + priorities → the [Memory map](#memory-map); cycle/timing details live in each peripheral header. Card-specific docs: **GEN2 HGR** → [`doc/GEN2_RELEASE.md`](doc/GEN2_RELEASE.md); **TMS9918 sprites** → `sketchs/doc/TMS9918-SPRITE_BEST_PRACTICES.md`; **SWTPC** → [`doc/SWTPC_GT-6144.md`](doc/SWTPC_GT-6144.md) + [`doc/SWTPC_PR-40.md`](doc/SWTPC_PR-40.md); **telemetry** → [`doc/TELEMETRY_SIDE_CHANNEL.md`](doc/TELEMETRY_SIDE_CHANNEL.md).

- **CodeTank** — P-LAB ROM **daughterboard** of the TMS9918 (no standalone bus presence); cascades both ways. 4 release carts under `roms/codetank/`, rebuilt by `tools/build_codetank_rom.py`; burn gate `tools/verify_codetank_roms.py`. `CODETANKDEV.rom` is generated, never committed.
- **Extended ACI** — Uncle Bernie's improved Gen-2 cassette interface. **No new MMIO**: a second PROM page at `$C500-$C5FF` beside Woz's untouched `$C100`. `C500R` relocates the stock ACI ROM into the **stack page** and re-vectors its `JSR $C1F1` to `$C5F1`, which frees exactly the bytes the relocated code needs for a stack — **touch either patch and the firmware overwrites its own stack**. Daughter page of the ACI, cascading both ways, plugged by default wherever the ACI is *except* the two historically faithful 1976 machines and the CC65 bench. `Memory` stays pure mechanism: `setACIEnabled(true)` does NOT auto-plug it, so `--disable xaci` can't be undone by a later deferred plug.
- **IEC daughterboard** — piggybacks on microSD's VIA PORTB bits 2-6; cascade-enables microSD. **A D64 is a LINKED structure and nothing records how long a chain is**, so every walk in `D64Image` uses **cycle detection** (`VisitedSectors`, 683 bits), never a hop counter — the counters it replaced were wrong in both directions. BAM free counts are clamped to the geometry rather than believed.
- **SID** — wraps libresidfp (GPL-2.0+, vendored). **The chip is built on first use by the private `Memory::sidChip()`, never by `Memory`'s constructor** (~120 ms for the FIRST chip in a process, 0.46 ms after). Every access goes through `sidChip()` so no caller observes a null. `EmulationController`'s constructor warms it **before any lock** — paying that inside `stateMutex` parks the audio callback and the render thread. Cycle-driven SPSC ring, drop-newest on overflow.
- **TerminalCard** — TCP loopback :6502. **The listener follows the PLUG, nothing else** — it used to open from `reset()`, which every core runs regardless of enablement, so every POM1 process and test binary bound the port with the card unplugged.
- **WiFiModem** — 65C51 + ESP8266 AT emulation, TELNET IAC filtering, non-blocking TCP. Desktop only.
- **CommandPort** — **not a card**: the dev-only scripting channel (`--cmd-port N`, headless + loopback only). One request line in, one reply line out. `expect` blocks on the emulated display instead of sleeping. Two rules a harness must know: **anchor on what FOLLOWS the answer**, and **a prompt character is not always a prompt**. Snapshots go on the HEAP inside `execute()` — `sizeof(EmulationSnapshot)` is 260 KB against a 512 KB thread stack, and the compiler reserves every branch's frame at entry.
- **TelemetryPort** — **not a card**: a dev-only side channel at `$C440-$C443` in the `$C4xx` A9=0 dead zone GEN2's decoder is structurally blind to, at priority 30. Off unless `--telemetry-port` arms it.

## Invariants & gotchas

### Presets

**Full preset / layout reference → [`doc/PRESETS.md`](doc/PRESETS.md).** The rules:

- `--preset N` indexes `kMachinePresets[]` in `MachinePresets.cpp` — **13 entries, indices 0-12**. The README preset table must stay in lockstep. Pinned by `preset_ram_profiles_smoke`, which parses the table as TEXT (move the table and you must move that test's path argument too).
- **The preset table is a REGISTRY, not an array.** External preset files are appended after the thirteen and are addressable by index like any other. **Any caller holding a user-supplied index must use `machinePresetAt(int)`**; `kMachinePresets[index]` is only safe for an index from a named constant. Two dead assumptions, both pinned by `preset_registry_smoke`: *"default = last"* (use `kDefaultPresetId`) and `isFantasyPreset(presetIdFromIndex(i))` as the way to ask whether a machine multiplexes (use `machinePresetMode(int)` — an external preset has no `PresetId`).
- **Indices 0-2 are DevBench profiles**, each MIRRORING an existing preset's machine config exactly, Extended ACI included — enforced by `preset_ram_profiles_smoke`. Give a bench a card its model lacks and that test fails by design: you'd be developing against a machine your users do not have.
- **Keep POM1 Fantasy last** — default-picker, banner and OS window-size floor all key off `kMachinePresetCount - 1`.
- **No TMS9918 preset without CodeTank** (daughterboard rule). A1-SID, A1-IO & RTC, Wi-Fi Modem and Juke-Box have **no dedicated preset** — plug from the Hardware menu or `--enable`.
- **External preset files** (`--preset-file`, `src/PresetFile.h`): the parser is pure; **card names are exactly `--enable`'s**; dependencies are closed **before** validation; the mode is in the FILE; and an unknown key or card **refuses the whole preset** — a refused file boots **nothing**, because falling back to a default machine would run the user's program on hardware they did not describe.
- **`applyMachineConfig(int)`**: ROM selection uses **preset config, not live flags** (live flags are still false at deferred plug time). `applyPendingLayout()` runs before `Begin()` with `ImGuiCond_FirstUseEver`. **Fullscreen is a SESSION property, never a per-preset one**, and everything keyed on "does the window fill the screen?" must call `osWindowIsFullscreen()`, not the `fullscreen` member (which misses macOS' native fullscreen space).
- **Auto-enable by source dir** of a loaded file is the table `pom1::softwaredir::kRules` (`src/SoftwareDirRules.h`), read forwards by `performMemoryLoad` and backwards by the file picker. The match is on a path COMPONENT under either separator; FIRST rule wins.

### MMIO

- `$D010` (KBD) — last key with bit 7 set; read clears strobe. `$D011` (KBDCR) — bit 7 = 1 when key ready.
- **PIA register banking** — each port hides TWO registers behind one address, selected by **bit 2 of its control register**: clear → the direction register, set → the data register. **POM1 seeds the POST-reset state** (`CRA=CRB=$A7`, `DDRB=$7F`, `DDRA=$00`) rather than the silicon's all-zero power-on, and this is **load-bearing**: POM1 jumps straight into programs without executing the Monitor's reset, and a zeroed CRB leaves the DDRs banked in — the Monitor's ECHO then writes characters into DDRB and **hangs forever** on its `BIT $D012 / BMI`. **The four shadow registers ride in the snapshot's `MEM` section (v6+)** and must stay there: they are NOT reconstructible from the 64 KB image. Pinned by `pia_ddr_smoke`; `extended_aci_smoke` part C is the end-to-end proof (Bernie's Codebreaker probes DDRB and prints *"I WANT TO RUN ON A REAL APPLE-1 !"* when an emulator gets it wrong).
- `$D012` (DSP) — write triggers the display callback + busy counter **except raw `$7F`** (the Monitor's reset-time DDR setup would paint a spurious `_`). Real hardware latches only on PB7=1; POM1 is permissive so emulator-era demos still render. TerminalCard + PR-40 sniffers get every write unfiltered.
- PIA 6821 incomplete decoding aliases `$D0xx` → `$D010-$D013` by low 2 bits — **all four registers**, so `$D0x3` is CRB, not RAM.
- **`Memory::configureResetVectors(addr)` only writes `$FFFC/$FFFD` (RES)**. NMI and IRQ stay at the authentic WozMonitor.rom values so P-LAB programs installing an IRQ trampoline at `$0000` route correctly.

### Loaders, containers and host services

**Full reference → [`doc/LOADERS_AND_HOST.md`](doc/LOADERS_AND_HOST.md).** The rules:

- **`loadHexDump` has THREE parsers, picked by CONTENT**, and they live in `src/MemoryImageLoader.{h,cpp}`, **pure** — bytes and a name in, a `MemoryImage` out; `Memory::loadHexDump` is *parse → decide → apply*. **Nothing partial ever escapes**: a rejected image drops every span. The default (legacy WOZMON) parser joins lines and recovers boundaries heuristically; a **TurboType `.TUR`** takes a **line-structured** parse (selected by extension *or* a lone `T` line — a marker-less `.tur` left to the legacy parser gets shredded into 60+ bogus zones); **Intel HEX** takes `src/IntelHexFile.h`, detected **structurally, never by extension**, and once the first record validates POM1 **commits** rather than falling back. Applying deliberately bypasses the ROM write-protect — a WOZMON dump is the user typing at the Monitor.
- **Address tokens go through a checked accumulator, never `strtol`** — `long` is 64-bit on macOS/Linux and 32-bit on Windows, so an over-wide token saturated differently on each and quietly wrote bytes into page zero.
- **Which extensions reach `loadHexDump` is `pom1::isHexDumpPath`** (`src/HexDumpFile.h`): `.txt` `.hex` `.apl` `.mon` `.tur`, one list, four call sites. `CodeBench.cpp` repeats them literally on purpose (portable module, no POM1 headers).
- **WAV and AIFF are parsed by POM1, not miniaudio** — `src/PcmFile.{h,cpp}`, pure. **AIFF PCM is signed at every width** including 8-bit (WAV's 8-bit is unsigned — confusing them is a half-scale DC offset the pulse decoder reads as one unbroken level). Chunk sizes are compared against the bytes **remaining**, never `offset+size`, which wraps on wasm32. `.aiff` takes the **pulse path unconditionally** — it is cassette DATA, never deck music.
- **A load warns when a plugged card answers inside it, and never unplugs one for that** (`src/CardShadowing.h`, pure). The discriminator is `CardCapability::Video`: `evictStorageCards` can safely unplug microSD/CFFA1 before a graphics load, but on a GEN2 machine the framebuffer IS `$2000-$3FFF` and an HGR picture loaded there is the point of the card.
- **The audio callback must not allocate and must not wait on slow work.** `AudioDevice::mixSources` runs on the OS audio thread; its scratch is sized ONCE and a larger request is mixed in chunks. SID is the reference (lock-free SPSC ring, `chipMutex` deliberately not taken). This is neither a lock-order inversion nor a data race, so `LockOrder.h` and TSan are both blind to it — **no CI job substitutes for reading the call path**.
- **`~CassetteDevice()` is not `= default`** — a mounted stream tape owns a live `ma_decoder`, and only `ma_decoder_uninit` releases the backend's buffers and file handle.
- **`logs/pom1.log` is the only log sink that outlives the process.** `pom1_macos_provision_user_data_dir()` must run BEFORE `initDefaultTeeLogger()` — it is what `chdir`s into the user data dir, and the sink resolves its relative path against the working directory.
- **OOR (`ramKB < 64`)**: strict mode returns `$FF` and drops writes in `[ramKB*1024, $8000)` — matches a real Apple-1 with no RAM board.
- **`--tape` / `--save-tape` are consumed on BOTH paths** (GUI and headless). A tape that is IN but not ROLLING warns once past 20 000 consecutive `$C081` polls — File ▸ Load Tape loads and does **not** press PLAY, unlike `--tape`.
- **The DevBench's TMS auto-link reads CODE, not comments** — both the symbol scan and the "defined locally" test run on a comment-stripped copy, and "defined locally" means a label **starting a line**.

## Memory Map

```
$0000-$00FF  Zero page
$0100-$01FF  Stack
$0200-$1FFF  User RAM (programs typically load at $0280 or $0300)
$0000-$7FFF  microSD card RAM expansion — 32 KB contiguous (manual §6.1, three
             allocation jumpers fitted); 36 KB with the $E000-$EFFF bank
$2000-$200F  A1-IO RTC VIA 65C22 (mutex with GEN2 below)
$2000-$3FFF  GEN2 HGR page 1 framebuffer (8 KB)
$4000-$5FFF  GEN2 HGR page 2 framebuffer ($C255 selects; mutex with CodeTank/Juke-Box windows)
$4000-$7FFF  CodeTank ROM (16 KB half of 32 KB 28c256; daughterboard of TMS9918, mutex with Juke-Box)
$4000-$BFFF  Juke-Box ROM (RAM-16/ROM-32 jumper; up to 512 KB paged via $CA00)
$5000-$7FFF  EhBASIC 2.22 flashed into RAM (NOT a ROM — cold `5000R` / warm
             `5003R`; programs live $0300-$4FFF; needs ≥32 KB; mutex with the
             microSD Applesoft window / CodeTank / Juke-Box)
$6000-$7FFF  Applesoft Lite in microSD CARD RAM (NOT a ROM — cold `6000R` / warm `6003R`)
$8000-$BFFF  Juke-Box ROM (RAM-32/ROM-16 jumper; Sx bit 4 of $CA00)
$8000-$9FFF  SD CARD OS ROM (microSD; same EEPROM serves the IEC daughterboard)
$9000-$AFDF  CFFA1 firmware ROM
$A000-$A00F  microSD VIA 65C22
$AFE0-$AFFF  CFFA1 ATA/IDE regs (A4 undecoded; ID at $AFDC/$AFDD)
$B000-$B003  MODEM BBS ACIA 65C51
$C000-$C0FF  ACI I/O ($C081 input, $C000 flip-flop)
$C100-$C1FF  Woz ACI ROM
$C500-$C5FF  Uncle Bernie's EXTENDED ACI PROM page (2nd half of the ACI's 512x4
             pair; daughter page — cascade-plugs/unplugs with the ACI)
$C250-$C257  GEN2 soft switches (READ toggles + returns HST0 in D7; writes no-op;
             mirrors across $C2/$C3/$C6/$C7xx where A4=1 — SEL = $Cxxx & !A11 & A9 & A4)
$C440-$C443  Telemetry side channel (DEV-ONLY virtual device, not real hardware;
             priority 30 so it wins over GEN2's broad $C200-$C7FF handler; armed
             only by --telemetry-port)
$C800-$CFFF  A1-SID (29 regs, & 0x1F)
$CA00        Juke-Box Px/Sx bank-select latch (write-only; mutex with SID)
$CC00-$CC1F  A1-AUDIO SE (excludes TMS9918)
$CC00/$CC01  TMS9918 DATA / CTRL (priority 10, wins over A1-SID)
$D00A        SWTPC GT-6144 command port (write-only; bus wins over PIA alias)
$D010-$D013  KBD/DDRA · CRA · DSP/DDRB · CRB (alias $D0F0-F3, $D030-33, …;
             CRx bit 2 banks direction vs data — see Invariants)
$E000-$EFFF  Integer BASIC ROM (mutex with Microsoft BASIC below — one socket)
$E000-$FEFF  Microsoft BASIC ROM (OSI lineage, 8 KB, FLOATING POINT; cold E000R,
             warm E003R; loaded from Settings → Memory Settings → ROM Loading)
$FF00-$FFFF  Woz Monitor ROM + vectors ($FFFA-$FFFF)
```

## Testing

**Full testing reference → [`doc/TESTING.md`](doc/TESTING.md)** — the CMake test model, per-module coverage, the ratchets, and the catalogue of load-bearing pins with the defect each one exists to prevent.

`ctest` from `build/` (native-only, opt-out `-DPOM1_ENABLE_TESTS=OFF`). Inventory in `tests/CMakeLists.txt`; `ctest -N` lists exact names.

```bash
ctest                       # full suite (~3 min wall time)
ctest -L emulator           # the release gate — green with no cc65 and no editors
ctest -L devtools           # editors, DevBench/cc65 pipeline, BASIC compilers
tools/coverage.py --gate    # per-module line+branch coverage (nightly CI job)
ctest --output-on-failure   #  … -R klaus -V for one test
```

- **Two lanes, and every declared test carries exactly one** (`LABELS`, assigned at the bottom of `tests/CMakeLists.txt` from `POM1_DEVTOOLS_TESTS`). `emulator` (118) is what a release must pass and is green on a machine with **no cc65** (toolchain-dependent tests skip with 77). `devtools` (28) is the development environment's own lane. The rule for the list is **subject, not name**. **Absent from the list means `emulator`**, deliberately.
- **Coverage is measured per MODULE, never as one percentage.** A single figure over 44 500 measurable lines averages a cycle-exact 6502 against 14 400 lines of ImGui that no test binary links: it moves for the wrong reasons and is improved by testing whatever is easiest. Only the first four modules are GATED (cpu/parsers/topology/snapshot, 90/90/85/85), set just under what the suite measures so they catch a module **losing** coverage. **The extracted UI seams stay in the `ui` module on purpose** — reclassifying them would improve the figure by moving the work.
- **A `constexpr` seam needs runtime calls as well as `static_assert`s** — instrumentation cannot see a compile-time evaluation, so a fully-proved header reads as 30 % covered. Assert each property twice, once folded and once through a `volatile` launder.
- **Architecture trend guard** — `tools/check_architecture.py` (ctest `architecture_check`) rejects any new direct include from the core/device set toward ImGui or `MainWindow`, and reports ceilings. **`tools/architecture_baseline.json` is a ratchet, not a target architecture: lower a ceiling when a refactor improves it; never raise one merely to make CI green.** A raise must name what bought it, in `CHANGELOG.md` — see the entries for the audio seam, the lazy SID, the card-shadow warning, the dynamic preset table and `PriorityMutex::yieldToWaiters()`. Two sibling ratchets: the **devtools boundary** (17 edges, four `MainWindow_*` files — the list can only shrink) and the **frozen facades** (`memory_public_methods` 190, `controller_public_methods` 203 — counts, not lines, because a facade shrinks by losing methods).
- **The scripting control channel is the shape to copy**: twenty new externally-reachable verbs, `controller_public_methods` moved by **zero**. If the next debug consumer needs a new public method, that is the signal the facade is missing something its existing callers also want.
- **CMake test model** — the 24 device sources every integration test needs (`POM1_TEST_CORE_FILES`) compile once into the `pom1_test_devices` OBJECT library. Its separate identity is deliberate: tests inherit `-UNDEBUG` while the Release app keeps `NDEBUG`, so sharing objects would silently disarm test assertions or arm them in production. A configure-time pass aborts if object use and layer declaration diverge.
- **Windows reserves 1 MB of stack per thread; Linux and macOS give 8.** `CMakeLists.txt` raises MSVC's reserve to 8 MB. `sizeof(EmulationSnapshot)` is 260 KB and `sizeof(EmulationController)` 261 KB, so a test holding a controller and three snapshots by value is over the line by arithmetic — and it dies with **no output at all**, because the CRT discards buffered stdout. Reach for `clang -Wframe-larger-than=32768` before believing a Windows-only crash is a logic bug.
- **The seven `tools/test_*_telnet.py` harnesses run in `ctest -L emulator`** (48 s for 198 assertions, all `RUN_SERIAL`), through `--cmd-port` via `tools/pom1_control.py`, which launches and reaps its own headless POM1 on a free port. Converting them from manual scripts found six defects. The lesson worth keeping is about **pinning generated content**: a hand-written list of a generated ROM's contents rots at every rebuild, and rotted unnoticed precisely because the test never ran.
- **The first test to execute the POM1 binary absorbs the cold start none of the others see** (currently `gfx_regress_gen2_testcard`): on Windows, the on-first-execute virus scan plus paging a freshly linked exe off disk — a tail of one job in six, up to 61 s. Budget it accordingly, print the elapsed time on a **pass** too (a distribution nobody can see is one nobody can threshold), and never hand a subprocess `DEVNULL` and no timeout: that throws away the one artefact that could say where POM1 stopped.

New invariant tests follow `tests/peripheral_bus_smoke_test.cpp` — `<cassert>` + `add_test` suffices; GTest/Catch2 only once multi-threaded tests land.

## Version string locations

**Single source of truth: the repo-root `VERSION` file.** To bump: edit `VERSION`, run `tools/set_version.sh` (syncs the static docs), rebuild. Never hardcode the version anywhere else.

- **C++** reads it via the generated `PomVersion.h` (`POM1_VERSION_STRING`) — CMake `configure_file`s `cmake/PomVersion.h.in` from `VERSION` into the build dir (on the global include path). Used by `src/main_imgui.cpp`, `src/MainWindow_Dialogs.cpp`, `src/Screen_ImGui.cpp`.
- **CMake** reads `VERSION` for `MACOSX_BUNDLE_BUNDLE_VERSION` / `MACOSX_BUNDLE_SHORT_VERSION_STRING`.
- **Packaging scripts** read `VERSION` at runtime (override via `POM1_VERSION` env, e.g. the release workflow's git tag): `package_macos_release.sh`, `package_windows_release.bat`, `packaging/linux/build_appimage.sh`.
- **Static docs** carry a literal `POM1 v<version>` that `tools/set_version.sh` rewrites and `tools/check_version_sync.sh` (ctest **`version_sync`**) guards: `README.md` (title), `build-wasm/shell.html` (3×: `<meta>`, `<title>`, `.sub` header), `packaging/windows/README.txt` (header).
