# Hydroampf

A loud, aggressive amp/distortion **VST2** effect plugin for **Vegas Pro**,
built as the "twin brother" of the meme classic *YOU WA SHOCK!* — same idea
(insanely punchy, in-your-face distortion), own sound and its own green
identity built around **#0EBA90**.

## What's inside

The plugin now follows a "one-knob" workflow: a single big **DRIVE** macro
sets the overall intensity, four **A/B/C/D** presets shape the character,
and two small knobs (**MIX**, **CEILING**) round it out.

- `DRIVE` — the big center knob, 0–100%. Controls input gain into the
  clipper (0–40 dB) **and** how much distortion character is blended in. At
  `0%` the signal passes through completely clean — no waveshaping at all.
  Turn it up to gradually blend in the "YOU WA SHOCK!"-style fold/crush,
  all the way to full wall-of-fuzz at `100%`.
- **Presets A / B / C / D** — pick the tonal character (also sets an
  internal "how hard the fold-back hits" amount that scales with DRIVE):
  - **A — Balanced**: neutral tone, moderate fold-back
  - **B — Warm**: darker tone, gentler fold-back
  - **C — Bright**: brighter tone, stronger fold-back
  - **D — Savage**: brightest/hottest tone, maximum fold-back
- `MIX` — dry/wet blend
- `CEILING` — a real brick-wall safety limiter (−6 to 0 dB, default
  ≈ −0.3 dB). Anything that would exceed the ceiling is clamped, and the
  **REDUCTION** meter shows how hard it's working in real time.

`DRIVE` defaults to `0%` and preset **A (Balanced)** is selected by default,
so straight out of the box Hydroampf passes audio through clean/undistorted.

The DSP is a cascaded asymmetric soft-clip (tanh-based, for an amp-like
growl and even-order harmonics) followed by a foldback/hard-crush stage,
plus a one-pole tilt filter for tone shaping and the brick-wall ceiling
limiter at the end of the chain.

The editor is a **native Win32 window** (no external UI framework needed),
drawn with GDI in the Hydroampf green theme (`#0EBA90`): a big macro knob,
A/B/C/D preset buttons, a live reduction meter (refreshed ~25 times/sec via
a Win32 timer), and two small knobs for Mix/Ceiling.

## Project layout

```
Hydroampf/
├── CMakeLists.txt
├── cmake/mingw-w64-toolchain.cmake   (only needed for cross-compiling from Linux/macOS)
├── src/
│   ├── sdk/vestige.h        # minimal, self-written VST2-ABI-compatible header
│   ├── Hydroampf.h/.cpp     # DSP + plugin glue
│   ├── Editor.h/.cpp        # Win32/GDI GUI
│   ├── PluginMain.cpp       # DLL entry point (VSTPluginMain / main)
│   └── Hydroampf.def        # explicit DLL export table
└── .vscode/                 # CMake Tools config for VS Code
```

### About `sdk/vestige.h`

Steinberg no longer distributes the official VST2 SDK, and its headers are
proprietary, so this project does **not** include or copy any Steinberg
source. `vestige.h` is a from-scratch, minimal re-implementation of just the
fixed binary layout (`AEffect` struct + opcode numbers) that any VST2 host —
Vegas Pro included — expects a plugin DLL to expose. This is the same
approach several open-source "vestige"-style headers in the hobbyist audio
community use. It only implements the opcodes Hydroampf actually needs
(open/close, sample rate, editor open/close/idle, get/set parameter, process,
processReplacing, and the basic identification opcodes). If you later want
full VST2 program-chunk (preset file) support or MIDI-triggered parameters,
you'll need to extend it with the corresponding opcodes.

**Recommended alternative:** if you don't specifically need the legacy VST2
format, consider porting this to **VST3** (Steinberg's SDK is open-source
and free) or wrapping the DSP in **JUCE**, which handles VST3/AU/CLAP hosts
for you. Vegas Pro has supported VST3 since v18, so unless you're targeting
an older Vegas version, VST3 is the safer long-term choice.

## Building on Windows (recommended)

1. Install **Visual Studio 2022** (Desktop C++ workload) *or* **MinGW-w64**.
2. Install the VS Code extensions **CMake Tools** and **C/C++**.
3. Open this folder in VS Code.
4. Press `Ctrl+Shift+P` → **CMake: Select a Kit** → pick your Visual Studio
   or MinGW kit.
5. Press `Ctrl+Shift+P` → **CMake: Build** (or use the `Ctrl+Shift+B` default
   build task). This produces `build/Hydroampf.dll`.

## Cross-compiling from Linux/macOS (optional)

Install `mingw-w64`, then:

```bash
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
cmake --build build
```

## Installing into Vegas Pro

1. Copy `Hydroampf.dll` into your VST2 plugin folder, e.g.
   `C:\Program Files\Common Files\VST2` or `C:\VSTPlugins`.
2. In Vegas Pro: **Options → Preferences → VST Effects**, add/confirm that
   folder is in the scan path, then rescan.
3. Hydroampf will show up under Audio FX as a 2-in/2-out insert effect.

## Notes / next steps

- This is a from-scratch educational/hobbyist implementation, not a
  commercial-grade product — there's no preset save/load (program chunks)
  yet, and only stereo in/out is wired up.
- If Vegas Pro's plugin scanner is picky about your specific build (some
  older Vegas versions are stricter about calling convention / bitness),
  make sure you build a **64-bit** DLL to match a 64-bit Vegas Pro install.
- The A/B/C/D presets are a UI convenience (they set the internal tone/
  fold-back character) and are not separate host-automatable parameters —
  only `DRIVE`, `MIX`, and `CEILING` are automatable VST parameters.
- Push `DRIVE` toward 100% and pick preset **D — Savage** for the full
  "YOU WA SHOCK!"-style wall-of-distortion effect.
