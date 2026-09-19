> This README documents the current, working version of the project: a
> one-knob-style processor with A/B/C/D character presets, a Mix control,
> and a real brick-wall Ceiling limiter with a live reduction meter.

## Features

- **One-knob workflow** — a single big **DRIVE** knob controls both the
  input gain into the clipper (0–40 dB) and how much distortion character
  is blended in. At `0%` the signal passes through completely clean — no
  waveshaping at all. Turn it up to gradually blend in the fold/crush
  character, all the way to a full wall-of-fuzz at `100%`.
- **Four character presets (A / B / C / D)** selectable with one click:

  | Preset | Name     | Character                                |
  |:------:|----------|-------------------------------------------|
  |   A    | Balanced | Neutral tone, moderate fold-back           |
  |   B    | Warm     | Darker tone, gentler fold-back             |
  |   C    | Bright   | Brighter tone, stronger fold-back          |
  |   D    | Savage   | Brightest/hottest tone, maximum fold-back  |

- **MIX** knob — dry/wet blend.
- **CEILING** knob — a real brick-wall safety limiter (−6 to 0 dB, default
  ≈ −0.3 dB). Anything that would exceed the ceiling is clamped instead of
  clipping the DAW's output.
- **Live REDUCTION meter** — shows how hard the ceiling limiter is working
  in real time (refreshed at ~25 fps).
- Ships **clean by default**: `DRIVE = 0%`, preset **A (Balanced)** — you
  have to intentionally dial in distortion, it won't surprise you.
- Native **Win32 + GDI** UI (no JUCE/framework dependency), themed in
  Hydroampf green (`#0EBA90`).

## Signal chain

```
input → gain (DRIVE) → asymmetric soft-clip (tanh) → fold-back/crush
      → tone tilt filter → dry/wet mix (MIX) → brick-wall limiter (CEILING) → output
```

## Parameters (host-automatable)

| # | Name    | Range         | Notes                                             |
|---|---------|---------------|----------------------------------------------------|
| 0 | Drive   | 0–100 %       | Macro: gain + distortion amount                    |
| 1 | Shock   | 0–100 %       | Legacy/reserved slot, not used by the current DSP  |
| 2 | Tone    | 0–100 %       | Set by the A/B/C/D presets, not a free knob in the UI |
| 3 | Mix     | 0–100 %       | Dry/wet blend                                      |
| 4 | Ceiling | −6 to 0 dB    | Brick-wall limiter threshold                       |

> The A/B/C/D presets are a UI convenience — they set `Tone` plus an
> internal fold-back multiplier — and are **not** separate automation
> lanes in the host.

---

## Requirements

- **Windows 10/11** (Vegas Pro itself is Windows-only, so that's the only
  platform this actually needs to run on)
- **Visual Studio 2022** with the *Desktop development with C++* workload
  (Community edition is fine), **or** MinGW-w64
- **CMake ≥ 3.16**
- **VS Code** with the **CMake Tools** and **C/C++** extensions (optional
  but recommended — you can also drive CMake from a plain terminal)

## Building

### Option A — VS Code + CMake Tools (recommended)

1. Open the project folder in VS Code.
2. `Ctrl+Shift+P` → **CMake: Select a Kit** → pick your **Visual Studio
   2022** kit, **x64** variant (not x86 — Vegas Pro is almost always
   64-bit these days).
3. `Ctrl+Shift+P` → **CMake: Configure**.
4. `Ctrl+Shift+P` → **CMake: Build** (or `Ctrl+Shift+B`).
5. The DLL lands at `build/Debug/Hydroampf.dll` (or `build/Release/...`
   depending on the selected variant).

### Option B — plain terminal, Visual Studio generator

This is the most reliable method — it lets MSBuild manage the compiler
environment for you, so you don't need to worry about `INCLUDE`/`LIB`
paths at all:

```powershell
cd Hydroampf
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\Hydroampf.dll`.

### Option C — Ninja from a Developer command prompt

If you prefer Ninja, you must run *both* commands from a **"Developer
PowerShell for VS 2022"** session (search for it in the Start menu) so that
`cl.exe` can find the standard headers:

```powershell
cd Hydroampf
cmake -S . -B build -G Ninja
cmake --build build
```

### Option D — cross-compiling from Linux/macOS

```bash
sudo apt install mingw-w64   # or: brew install mingw-w64
cd Hydroampf
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
cmake --build build
```

## Installing into Vegas Pro

1. Copy `Hydroampf.dll` into your VST2 plugin folder, e.g.
   `C:\Program Files\Common Files\VST2` or `C:\VSTPlugins`.
2. In Vegas Pro: **Options → Preferences → VST Effects**, confirm that
   folder is in the scan path, then rescan.
3. Hydroampf appears under Audio FX as a 2-in/2-out insert effect.

---

## Troubleshooting

These are real build errors encountered (and fixed) while developing this
project — kept here in case you fork/modify the source and hit them again:

| Symptom | Cause | Fix |
|---|---|---|
| `fatal error C1083: cannot open include file: 'cstdint'` | Ninja build run outside a Visual Studio developer environment, so `cl.exe` can't see the standard headers | Use the Visual Studio generator (Option B above), or run Ninja from a "Developer PowerShell for VS 2022" (Option C) |
| `error C3874: return type of "main" must be "int"` | MSVC enforces the standard `int main()` signature for any function literally named `main`, even an exported one | The legacy VST `main` entry point is implemented as `HydroampfLegacyMainEntry` in `PluginMain.cpp` and re-exported under the name `main` via `Hydroampf.def` |
| `error C2065: M_PI: undeclared identifier` | MSVC only defines `M_PI` if `_USE_MATH_DEFINES` is set before including `<cmath>` | The project defines its own `kPi` constant instead of relying on `M_PI` |
| `error C2352 / C3861: GET_X_LPARAM / handleMouseDown ... requires an object` | `wndProc` was accidentally declared `static`, so it couldn't touch instance members, and `<windowsx.h>` (which defines `GET_X_LPARAM`/`GET_Y_LPARAM`) wasn't included | `wndProc` is a normal instance method; `Editor.cpp` includes `<windowsx.h>` |
| `error C2664: LoadCursorW(...): cannot convert LPSTR to LPCWSTR` | Calling `LoadCursorW` directly with `IDC_ARROW` mixes ANSI/Unicode resource macros | Use the `LoadCursor(...)` macro instead, which resolves to the correct `A`/`W` variant automatically |
| Knob/dB text renders as garbage or `0` | `wsprintfW` does **not** support floating-point format specifiers (`%f`), unlike standard `printf` | UI numeric labels are formatted with `swprintf` (from `<cwchar>`) instead of `wsprintfW` |
| DLL builds but the sound is always distorted, even at low knob values | The waveshaper was applied unconditionally regardless of parameter values | `DRIVE`/`SHOCK` now gate the waveshaper: at `0` the signal is passed through completely linearly |

If you hit a **new** build error not listed here, check the CMake/Build
output panel in VS Code (`Ctrl+Shift+U` → select "CMake/Build" from the
dropdown) for the full compiler log before troubleshooting further — it's
almost always the actual `error C####` line right above the `ninja: build
stopped` line.

---

## Project layout

```
Hydroampf/
├── CMakeLists.txt
├── cmake/mingw-w64-toolchain.cmake   # only needed for Option D (cross-compile)
├── src/
│   ├── sdk/vestige.h        # minimal, self-written VST2-ABI-compatible header
│   ├── Hydroampf.h/.cpp     # DSP + plugin glue (parameters, presets, limiter)
│   ├── Editor.h/.cpp        # Win32/GDI GUI
│   ├── PluginMain.cpp       # DLL entry point (VSTPluginMain / main)
│   └── Hydroampf.def        # explicit DLL export table
└── .vscode/                 # CMake Tools config for VS Code
```

### About `sdk/vestige.h`

Steinberg no longer distributes the official VST2 SDK, and its headers are
proprietary, so this project does **not** include or copy any Steinberg
source. `vestige.h` is a from-scratch, minimal re-implementation of just
the fixed binary layout (`AEffect` struct + opcode numbers) that any VST2
host — Vegas Pro included — expects a plugin DLL to expose. This mirrors
the approach several open-source "vestige"-style headers in the hobbyist
audio community use. It only implements the opcodes Hydroampf actually
needs (open/close, sample rate, editor open/close/idle, get/set parameter,
process, processReplacing, and the basic identification opcodes).

**Recommended alternative:** if you don't specifically need the legacy
VST2 format, consider porting this to **VST3** (Steinberg's SDK is
open-source and free) or wrapping the DSP in **JUCE**, which handles
VST3/AU/CLAP hosts for you. Vegas Pro has supported VST3 since v18, so
unless you're targeting an older Vegas install, VST3 is the safer
long-term choice.

## Notes / roadmap

- This is a from-scratch hobbyist/educational implementation, not a
  commercial-grade product — there's no preset save/load (VST program
  chunks) yet, and only stereo in/out is wired up.
- Make sure you build a **64-bit** DLL to match a 64-bit Vegas Pro install
  — some older Vegas versions are strict about matching bitness.
- For the full "YOU WA SHOCK!"-style wall-of-distortion effect: push
  `DRIVE` toward 100% and pick preset **D — Savage**.
- Possible next steps: VST program-chunk preset save/load, a true
  oversampled limiter (the current ceiling is a simple sample-accurate
  clamp, not lookahead), MIDI-mappable macro, VST3 port.

## License

No specific license has been chosen yet for this repository — treat it as
"all rights reserved" until a `LICENSE` file is added, unless the repo
owner has said otherwise elsewhere.
