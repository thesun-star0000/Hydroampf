# Hydroampf Maximizer (VST2 + CLAP)

One-knob Maximus-style multiband maximizer. DSP = C++ port of the "Press" plugin engine from
Lemon Studio (https://github.com/ItsAlphaNeon/Lemon-Studio), loaded with its Enhancer presets
Mode A / B / C / D (the same four voicings Soundgoodizer offers).

**License: GPL-3.0-or-later** (derived from Lemon Studio, see LICENSE and src/dsp/Maximus.h).
If you distribute the plugin binaries you must also offer this source code.

Controls: AMOUNT (drive of the preset: 0 = neutral gains, 100 = the preset's full pre/post gains - this mapping is
our own choice), MODE A-D, MIX (latency-compensated dry/wet), CEILING. Waveform: input grey, output coloured.

## Build on Windows
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64    (pick the generator you have)
    cmake --build build --config Release
-> build\Release\Hydroampf.dll (VST2) and Hydroampf.clap

## Layout
- src/dsp/Maximus.h, MaximusPresets.h  Maximus port + decoded Mode A-D states
- src/dsp/MaxCore.h                     parameter wrapper (mix, ceiling, dry delay compensation)
- src/Engine.h, Editor.cpp, Hydroampf.cpp (VST2), ClapMain.cpp (CLAP), sdk/
- tools/ offline renderer + fake CLAP host (Linux tests)
- Not ported from Press: linear-phase FIR crossover, oversampling, mid-trigger, solo, exotic map-curve modes.
- Latency (look-ahead, a few ms) is not reported to the host.

## v4 notes
- UI: GDI+ (anti-aliased), smooth 60 fps waveform with sub-column scrolling, IN/OUT meters, mode arrows + A-D pads.
  Drag = adjust, Shift = fine, double-click = reset, mouse wheel = step.
- Vegas Pro: constant latency (4 ms) reported via initialDelay/tail, denormal protection, odd block sizes, Amount smoothing,
  faded mode switches, window class unregistered on close (safe DLL re-scan/unload).
- Windows editor code was syntax-checked against stub headers only; build it with MSVC and report any error.
- tools/vst_host_test.cpp, tools/clap_host_test.cpp: fake hosts used for Linux tests.

## v5
- UI redesigned: compact 380x590 plate, recessed glass waveform display, one large graphite "living" Amount knob (LED ring glows
  with the output level, gentle breathing), raised A-D pads. Mix / Ceiling knobs removed from the UI (they still exist as
  automatable host parameters; defaults 100 % / -0.3 dB).
- Asks the VST host to resize its window (audioMasterSizeWindow) so Vegas Pro does not crop the editor.

## v6
- UI: light hardware-style box (470x404, ~1.16:1 like the reference): black glass screen with a glowing analog VU needle
  (output level, ~100 ms rise / 300 ms fall), INPUT/OUTPUT dB readouts, faint waveform, MODE arrows; big AMOUNT knob with a dotted
  scale (dots light up with the value); four tactile A-D keys (selected key presses in and glows).
