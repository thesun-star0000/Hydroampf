#include "Hydroampf.h"
#include "Editor.h"
#include <cstring>

namespace
{
    inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
}

Hydroampf::Hydroampf(audioMasterCallback master) : hostCallback(master)
{
    params[kParamDrive]  = 0.0f;   // clean by default - turn up for distortion
    params[kParamShock]  = 0.0f;   // legacy/unused slot
    params[kParamTone]   = 0.5f;   // preset A (Balanced)
    params[kParamMix]    = 1.0f;
    params[kParamOutput] = 0.95f;  // Ceiling ~ -0.3 dB by default
    currentPreset = 0;
    presetShockMul = 0.7f;

    effect.dispatcher = [](AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt) -> intptr_t
    {
        return static_cast<Hydroampf*>(e->object)->dispatch(op, idx, val, ptr, opt);
    };
    effect.process = [](AEffect* e, float** in, float** out, int32_t n)
    {
        static_cast<Hydroampf*>(e->object)->process(in, out, n, false);
    };
    effect.processReplacing = [](AEffect* e, float** in, float** out, int32_t n)
    {
        static_cast<Hydroampf*>(e->object)->process(in, out, n, true);
    };
    effect.setParameter = [](AEffect* e, int32_t idx, float v)
    {
        static_cast<Hydroampf*>(e->object)->setParameter(idx, v);
    };
    effect.getParameter = [](AEffect* e, int32_t idx) -> float
    {
        return static_cast<Hydroampf*>(e->object)->getParameter(idx);
    };

    effect.object = this;
    effect.uniqueID = kHydroampfUID;
    effect.version = 1000;
    effect.numParams = kNumParams;
    effect.numPrograms = 1;
    effect.numInputs = 2;
    effect.numOutputs = 2;
    effect.flags = effFlagsCanReplacing | effFlagsHasEditor;
}

Hydroampf::~Hydroampf() = default;

const char* Hydroampf::presetLabel(int index)
{
    switch (index)
    {
        case 0: return "BALANCED";
        case 1: return "WARM";
        case 2: return "BRIGHT";
        case 3: return "SAVAGE";
        default: return "BALANCED";
    }
}

void Hydroampf::setPreset(int index)
{
    if (index < 0 || index > 3) return;
    currentPreset = index;
    switch (index)
    {
        case 0: params[kParamTone] = 0.5f; presetShockMul = 0.7f; break; // Balanced
        case 1: params[kParamTone] = 0.2f; presetShockMul = 0.5f; break; // Warm/dark, gentler fold
        case 2: params[kParamTone] = 0.8f; presetShockMul = 0.8f; break; // Bright
        case 3: params[kParamTone] = 0.9f; presetShockMul = 1.0f; break; // Savage - max fold-back
    }
}

// --- the actual "SHOCK" character: cascaded asymmetric clip + foldback -----
// driveAmt/shock both range 0..1 and act as a genuine "how much distortion"
// control: at driveAmt == 0 (and therefore shock == 0) this returns the
// input completely untouched aside from linear gain, so DRIVE at 0% is a
// clean, undistorted signal. Turning DRIVE up blends the character in.
float Hydroampf::shapeSample(float x, float driveGain, float driveAmt, float shock) const
{
    float s = x * driveGain;

    if (driveAmt <= 0.0001f && shock <= 0.0001f)
        return s; // fully clean - no waveshaping applied at all

    float bias = 0.15f * shock;
    float pos = std::tanh((s + bias) * 1.6f);
    float neg = std::tanh((s - bias) * 1.3f);
    float distorted = (s >= 0.0f) ? pos : neg;

    if (shock > 0.001f)
    {
        float thresh = 1.0f - 0.6f * shock;
        if (distorted > thresh)  distorted = thresh - (distorted - thresh);
        if (distorted < -thresh) distorted = -thresh - (distorted + thresh);
        distorted = std::clamp(distorted, -1.2f, 1.2f);
    }

    float amount = std::clamp(std::max(driveAmt, shock), 0.0f, 1.0f);
    return s * (1.0f - amount) + distorted * amount;
}

void Hydroampf::process(float** inputs, float** outputs, int32_t numFrames, bool replacing)
{
    const float driveAmt  = params[kParamDrive];
    const float driveGain = dbToGain(driveAmt * 40.0f); // 0..40 dB
    const float shock     = std::clamp(driveAmt * presetShockMul, 0.0f, 1.0f);
    const float mix       = params[kParamMix];
    const float tilt      = (params[kParamTone] - 0.5f) * 2.0f; // -1..1

    const float ceilingDb  = -6.0f + params[kParamOutput] * 6.0f; // -6..0 dB
    const float ceilingLin = dbToGain(ceilingDb);

    constexpr float kPi = 3.14159265358979323846f;
    const float cutoff = 1500.0f * std::pow(4.0f, tilt); // ~375Hz..6kHz
    const float a = std::exp(-2.0f * kPi * cutoff / sampleRate);

    float blockReduction = 0.0f;

    for (int32_t ch = 0; ch < 2; ++ch)
    {
        float* in = inputs[ch];
        float* out = outputs[ch];
        float& toneState = (ch == 0) ? toneStateL : toneStateR;

        for (int32_t i = 0; i < numFrames; ++i)
        {
            float dry = in[i];
            float wet = shapeSample(dry, driveGain, driveAmt, shock);

            toneState = a * toneState + (1.0f - a) * wet;
            float lp = toneState;
            float hp = wet - lp;
            float toned = (tilt >= 0.0f)
                ? wet + tilt * hp
                : wet + tilt * (wet - hp);

            float mixed = dry * (1.0f - mix) + toned * mix;

            // real safety limiter: brickwall clamp to the CEILING level
            float absMixed = std::fabs(mixed);
            float limited = mixed;
            if (absMixed > ceilingLin)
            {
                limited = std::copysign(ceilingLin, mixed);
                blockReduction = std::max(blockReduction, absMixed - ceilingLin);
            }

            out[i] = replacing ? limited : (out[i] + limited);
        }
    }

    // simple ballistics for the UI meter: instant attack, slow decay
    float prev = meterReduction.load(std::memory_order_relaxed);
    float next = std::max(std::min(blockReduction * 8.0f, 1.0f), prev * 0.9f);
    meterReduction.store(next, std::memory_order_relaxed);
}

void Hydroampf::setParameter(int32_t index, float value)
{
    if (index < 0 || index >= kNumParams) return;
    params[index] = std::clamp(value, 0.0f, 1.0f);
}

float Hydroampf::getParameter(int32_t index) const
{
    if (index < 0 || index >= kNumParams) return 0.0f;
    return params[index];
}

void Hydroampf::getParamName(int32_t index, char* text) const
{
    static const char* names[kNumParams] = { "Drive", "Shock", "Tone", "Mix", "Ceiling" };
    std::snprintf(text, 64, "%s", (index >= 0 && index < kNumParams) ? names[index] : "");
}

void Hydroampf::getParamLabel(int32_t index, char* text) const
{
    static const char* labels[kNumParams] = { "%", "", "", "%", "dB" };
    std::snprintf(text, 64, "%s", (index >= 0 && index < kNumParams) ? labels[index] : "");
}

void Hydroampf::getParamDisplay(int32_t index, char* text) const
{
    switch (index)
    {
        case kParamDrive:  std::snprintf(text, 64, "%.0f", params[index] * 100.0f); break;
        case kParamShock:  std::snprintf(text, 64, "%.0f", params[index] * 100.0f); break;
        case kParamTone:   std::snprintf(text, 64, "%.0f", (params[index] - 0.5f) * 200.0f); break;
        case kParamMix:    std::snprintf(text, 64, "%.0f", params[index] * 100.0f); break;
        case kParamOutput: std::snprintf(text, 64, "%.1f", -6.0f + params[index] * 6.0f); break;
        default: std::snprintf(text, 64, "%.2f", params[index]); break;
    }
}

intptr_t Hydroampf::dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
    switch (opcode)
    {
        case effOpen: return 0;
        case effClose:
            delete this;
            return 0;
        case effSetSampleRate:
            setSampleRate(opt);
            return 0;
        case effSetBlockSize:
        case effMainsChanged:
            return 0;
        case effGetVendorString:
            std::snprintf(static_cast<char*>(ptr), 64, "Hydroampf Audio");
            return 0;
        case effGetProductString:
            std::snprintf(static_cast<char*>(ptr), 64, "Hydroampf");
            return 0;
        case effGetEffectName:
            std::snprintf(static_cast<char*>(ptr), 64, "Hydroampf");
            return 0;
        case effGetVendorVersion: return 1000;
        case effGetVstVersion: return 2400;
        case effGetPlugCategory: return kPlugCategEffect;
        case effCanBeAutomated: return 1;
        case effCanDo:
        {
            const char* q = static_cast<const char*>(ptr);
            if (q && (std::strcmp(q, "plugAsChannelInsert") == 0 ||
                      std::strcmp(q, "plugAsSend") == 0 ||
                      std::strcmp(q, "1in1out") == 0 ||
                      std::strcmp(q, "2in2out") == 0))
                return 1;
            return 0;
        }
        case effGetParamName:
            getParamName(index, static_cast<char*>(ptr));
            return 0;
        case effGetParamLabel:
            getParamLabel(index, static_cast<char*>(ptr));
            return 0;
        case effGetParamDisplay:
            getParamDisplay(index, static_cast<char*>(ptr));
            return 0;
        case effGetProgramName:
            std::snprintf(static_cast<char*>(ptr), 64, "Default");
            return 0;
        case effSetProgramName:
        case effSetProgram:
        case effGetProgram:
            return 0;

        case effEditGetRect:
            return HydroampfEditor::getRect(reinterpret_cast<ERect**>(ptr));
        case effEditOpen:
            if (!editor) editor = new HydroampfEditor(this);
            return editor->open(ptr);
        case effEditClose:
            if (editor) { editor->close(); delete editor; editor = nullptr; }
            return 0;
        case effEditIdle:
            if (editor) editor->idle();
            return 0;

        default:
            return 0;
    }
}
