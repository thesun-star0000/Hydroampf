#include "Hydroampf.h"
#include "Editor.h"
#include <cstring>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  #include <xmmintrin.h>
  #include <pmmintrin.h>
  #define HYDRO_HAS_SSE 1
#endif

namespace {
// Flush-to-zero / denormals-are-zero while processing: avoids CPU spikes on decaying tails.
struct DenormalGuard {
#ifdef HYDRO_HAS_SSE
    unsigned old;
    DenormalGuard() : old(_mm_getcsr()) { _mm_setcsr(old | 0x8040u); }
    ~DenormalGuard() { _mm_setcsr(old); }
#endif
};
}

Hydroampf::Hydroampf(audioMasterCallback master) : hostCallback(master)
{
    effect.dispatcher = [](AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt) -> intptr_t
        { return static_cast<Hydroampf*>(e->object)->dispatch(op, idx, val, ptr, opt); };
    effect.process = [](AEffect* e, float** in, float** out, int32_t n)
        { static_cast<Hydroampf*>(e->object)->process(in, out, n, false); };
    effect.processReplacing = [](AEffect* e, float** in, float** out, int32_t n)
        { static_cast<Hydroampf*>(e->object)->process(in, out, n, true); };
    effect.setParameter = [](AEffect* e, int32_t idx, float v)
        { static_cast<Hydroampf*>(e->object)->setParameter(idx, v); };
    effect.getParameter = [](AEffect* e, int32_t idx) -> float
        { return static_cast<Hydroampf*>(e->object)->getParameter(idx); };

    effect.object = this;
    effect.uniqueID = kHydroampfUID;
    effect.version = 2000;
    effect.initialDelay = engine.latencySamples();
    effect.numParams = kPNumParams;
    effect.numPrograms = 1;
    effect.numInputs = 2;
    effect.numOutputs = 2;
    effect.flags = effFlagsCanReplacing | effFlagsHasEditor;
}

Hydroampf::~Hydroampf() = default;

void Hydroampf::uiSetParam(int index, float value)
{
    engine.set(index, value);
    if (hostCallback)
        hostCallback(&effect, audioMasterAutomate, index, 0, nullptr, engine.get(index));
}

void Hydroampf::process(float** in, float** out, int32_t n, bool replacing)
{
    if (n <= 0 || !in || !out || !in[0] || !in[1] || !out[0] || !out[1]) return;
    DenormalGuard guard;
    if (!replacing) {                       // legacy accumulate path
        tmpL.assign(n, 0.f); tmpR.assign(n, 0.f);
        engine.process(in[0], in[1], tmpL.data(), tmpR.data(), n);
        for (int i = 0; i < n; ++i) { out[0][i] += tmpL[i]; out[1][i] += tmpR[i]; }
        return;
    }
    engine.process(in[0], in[1], out[0], out[1], n);
}

intptr_t Hydroampf::dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
    char* txt = static_cast<char*>(ptr);
    switch (opcode)
    {
        case effOpen: return 0;
        case effClose: delete this; return 0;
        case effSetSampleRate: engine.prepare(opt); effect.initialDelay = engine.latencySamples(); return 0;
        case effSetBlockSize: return 0;
        case effMainsChanged: engine.reset(); effect.initialDelay = engine.latencySamples(); return 0;
        case effStartProcess: case effStopProcess: engine.reset(); return 0;
        case effGetTailSize: return engine.latencySamples();
        case effGetProgramNameIndexed: if (index != 0) return 0; std::snprintf(txt, 64, "Default"); return 1;
        case effGetParameterProperties: case effGetInputProperties: case effGetOutputProperties:
        case effBeginSetProgram: case effEndSetProgram: case effSetBypass: case effSetProcessPrecision: return 0;
        case effGetVendorString: std::snprintf(txt, 64, "Hydroampf Audio"); return 1;
        case effGetProductString:
        case effGetEffectName:   std::snprintf(txt, 64, "Hydroampf Maximizer"); return 1;
        case effGetVendorVersion: return 2000;
        case effGetVstVersion: return 2400;
        case effGetPlugCategory: return kPlugCategEffect;
        case effCanBeAutomated: return 1;
        case effCanDo: {
            const char* q = txt;
            if (q && (!std::strcmp(q, "plugAsChannelInsert") || !std::strcmp(q, "plugAsSend") ||
                      !std::strcmp(q, "2in2out"))) return 1;
            return 0; }
        case effGetParamName:    std::snprintf(txt, 64, "%s", Engine::paramName(index)); return 0;
        case effGetParamLabel:   txt[0] = 0; return 0;
        case effGetParamDisplay: engine.paramText(index, txt, 64); return 0;
        case effGetProgramName:  std::snprintf(txt, 64, "Default"); return 0;
        case effSetProgramName: case effSetProgram: case effGetProgram: return 0;

        case effEditGetRect: return HydroampfEditor::getRect(reinterpret_cast<ERect**>(ptr));
        case effEditOpen: {
            if (!editor) editor = new HydroampfEditor(&engine,
                [](void* u, int i, float v){ static_cast<Hydroampf*>(u)->uiSetParam(i, v); }, this);
            intptr_t ok = editor->open(ptr);
            // ask the host to size its window to our editor (Vegas Pro otherwise may show a cropped, scrollable view)
            if (ok && hostCallback)
                hostCallback(&effect, audioMasterSizeWindow, HydroampfEditor::kWidth, HydroampfEditor::kHeight, nullptr, 0.0f);
            return ok; }
        case effEditClose:
            if (editor) { editor->close(); delete editor; editor = nullptr; }
            return 0;
        case effEditIdle: if (editor) editor->idle(); return 0;
        default: return 0;
    }
}
