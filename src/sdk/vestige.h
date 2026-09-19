// vestige.h
// ---------------------------------------------------------------------------
// Minimal, independently written re-implementation of the classic "VST2"
// binary plugin ABI (the fixed struct layout / opcode numbers that any host,
// including Vegas Pro, expects a plugin DLL to expose via VSTPluginMain()).
//
// This file contains NO code copied from Steinberg's proprietary VST SDK.
// It only reproduces the numeric opcode IDs and struct memory layout that
// are required for binary compatibility with existing hosts - the same
// approach used by several open-source "vestige"-style headers in the
// hobbyist audio community. Written from scratch for the Hydroampf project.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <cstring>

#if defined(_WIN32)
  #define VST_EXPORT extern "C" __declspec(dllexport)
#else
  #define VST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

struct AEffect;

typedef intptr_t (*audioMasterCallback)(AEffect* effect, int32_t opcode,
                                         int32_t index, intptr_t value,
                                         void* ptr, float opt);

typedef intptr_t (*AEffectDispatcherProc)(AEffect* effect, int32_t opcode,
                                           int32_t index, intptr_t value,
                                           void* ptr, float opt);
typedef void (*AEffectProcessProc)(AEffect* effect, float** inputs,
                                    float** outputs, int32_t sampleFrames);
typedef void (*AEffectProcessDoubleProc)(AEffect* effect, double** inputs,
                                          double** outputs, int32_t sampleFrames);
typedef void (*AEffectSetParameterProc)(AEffect* effect, int32_t index, float parameter);
typedef float (*AEffectGetParameterProc)(AEffect* effect, int32_t index);

// ---- AEffect: the struct every VST2 host reads directly from the DLL -----
struct AEffect
{
    int32_t magic = 0x56737450; // 'PtsV' (matches classic 'VstP' byte order)
    AEffectDispatcherProc dispatcher = nullptr;
    AEffectProcessProc process = nullptr;
    AEffectSetParameterProc setParameter = nullptr;
    AEffectGetParameterProc getParameter = nullptr;

    int32_t numPrograms = 1;
    int32_t numParams = 0;
    int32_t numInputs = 2;
    int32_t numOutputs = 2;

    int32_t flags = 0;

    intptr_t resvd1 = 0;
    intptr_t resvd2 = 0;

    int32_t initialDelay = 0;

    int32_t realQualities = 0;
    int32_t offQualities = 0;
    float ioRatio = 1.0f;

    void* object = nullptr;   // pointer back to our C++ plugin instance
    void* user = nullptr;

    int32_t uniqueID = 0;
    int32_t version = 1000;

    AEffectProcessProc processReplacing = nullptr;
    AEffectProcessDoubleProc processDoubleReplacing = nullptr;

    char future[56] = {};
};

// ---- Flags (bitmask for AEffect::flags) -----------------------------------
enum VstAEffectFlags
{
    effFlagsHasEditor      = 1 << 0,
    effFlagsCanReplacing   = 1 << 4,
    effFlagsProgramChunks  = 1 << 5,
    effFlagsIsSynth        = 1 << 8,
    effFlagsCanDoubleReplacing = 1 << 12,
};

// ---- Host -> Plugin opcodes (subset actually used by Hydroampf) ----------
enum VstEffectOpcodes
{
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effSetProgramName = 4,
    effGetProgramName = 5,
    effGetParamLabel = 6,
    effGetParamDisplay = 7,
    effGetParamName = 8,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effIdentify = 22,
    effGetChunk = 23,
    effSetChunk = 24,
    effProcessEvents = 25,
    effCanBeAutomated = 26,
    effGetPlugCategory = 35,
    effGetEffectName = 45,
    effGetVendorString = 47,
    effGetProductString = 48,
    effGetVendorVersion = 49,
    effCanDo = 51,
    effGetVstVersion = 58,
};

// ---- Plugin -> Host opcodes -------------------------------------------
enum VstMasterOpcodes
{
    audioMasterAutomate = 0,
    audioMasterVersion = 1,
    audioMasterIdle = 3,
    audioMasterSizeWindow = 15,
    audioMasterGetSampleRate = 16,
    audioMasterGetBlockSize = 17,
};

enum VstPlugCategory
{
    kPlugCategEffect = 1,
};

struct ERect
{
    int16_t top, left, bottom, right;
};
