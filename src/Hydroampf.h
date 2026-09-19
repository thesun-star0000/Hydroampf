#pragma once
#include "sdk/vestige.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <atomic>

// Unique 4-char plugin ID encoded as int32 ('H','y','d','A')
constexpr int32_t kHydroampfUID =
    ('H' << 24) | ('y' << 16) | ('d' << 8) | ('A');

// The UI exposes DRIVE as one big "macro" knob (0..100%) plus a preset
// (A/B/C/D) that shapes the character, and two small knobs: MIX and CEILING
// (a real safety limiter, not just an output trim). kParamShock is kept only
// so the parameter count/automation slots stay stable; its value is not
// used directly - the effective "shock" amount is derived from DRIVE and
// the selected preset instead.
enum HydroParam
{
    kParamDrive = 0,   // macro amount, 0..1 -> also drives gain into the clipper
    kParamShock,       // legacy slot, currently unused (kept for automation compatibility)
    kParamTone,        // tone character, set by the A/B/C/D presets
    kParamMix,         // dry/wet, 0..1
    kParamOutput,      // now the CEILING control: 0..1 -> -6..0 dB safety limit
    kNumParams
};

class HydroampfEditor; // fwd decl

class Hydroampf
{
public:
    explicit Hydroampf(audioMasterCallback master);
    ~Hydroampf();

    AEffect* getAEffect() { return &effect; }

    // dispatcher entry points -------------------------------------------------
    intptr_t dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    void setParameter(int32_t index, float value);
    float getParameter(int32_t index) const;
    void getParamName(int32_t index, char* text) const;
    void getParamLabel(int32_t index, char* text) const;
    void getParamDisplay(int32_t index, char* text) const;

    void setSampleRate(float sr) { sampleRate = sr; }
    void process(float** inputs, float** outputs, int32_t numFrames, bool replacing);

    // Presets (A/B/C/D): each sets the TONE parameter and an internal
    // "shock multiplier" that decides how aggressive the fold-back gets as
    // the DRIVE macro increases. Not host-automatable by design - purely a
    // UI convenience, same idea as the A/B/C/D buttons in the reference design.
    void setPreset(int index); // 0=A Balanced, 1=B Warm, 2=C Bright, 3=D Savage
    int  getPreset() const { return currentPreset; }
    static const char* presetLabel(int index);

    // For the editor's live meter (updated from the audio thread, read from
    // the UI thread via idle()/timer - hence atomic).
    float getMeterReduction() const { return meterReduction.load(std::memory_order_relaxed); }

    audioMasterCallback hostCallback;
    AEffect effect{};

    HydroampfEditor* editor = nullptr;

private:
    float params[kNumParams];
    float sampleRate = 44100.0f;
    int currentPreset = 0;
    float presetShockMul = 0.7f; // how strongly DRIVE feeds the fold-back stage

    // simple 1-pole tone tilt filter state, per channel
    float toneStateL = 0.0f, toneStateR = 0.0f;

    std::atomic<float> meterReduction{0.0f}; // 0..1, decaying "how hard the ceiling is working"

    float shapeSample(float x, float driveGain, float driveAmt, float shock) const;
};
