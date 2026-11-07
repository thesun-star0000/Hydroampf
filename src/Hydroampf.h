#pragma once
#include "sdk/vestige.h"
#include <vector>
#include "Engine.h"

constexpr int32_t kHydroampfUID = ('H' << 24) | ('y' << 16) | ('d' << 8) | ('B');

class HydroampfEditor;

// VST2 wrapper around the shared Engine.
class Hydroampf
{
public:
    explicit Hydroampf(audioMasterCallback master);
    ~Hydroampf();
    AEffect* getAEffect() { return &effect; }

    intptr_t dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    void setParameter(int32_t index, float value) { engine.set(index, value); }
    float getParameter(int32_t index) const { return engine.get(index); }
    void process(float** in, float** out, int32_t n, bool replacing);

    // Called by the editor when the user moves a control: updates the engine and
    // tells the host (automation / undo).
    void uiSetParam(int index, float value);

    Engine engine;
    audioMasterCallback hostCallback;
    AEffect effect{};
    HydroampfEditor* editor = nullptr;

private:
    std::vector<float> tmpL, tmpR;
};
