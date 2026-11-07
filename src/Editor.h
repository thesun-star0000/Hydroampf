#pragma once
#include "sdk/vestige.h"
#include "Engine.h"

// Win32 + GDI+ (anti-aliased) editor, shared by the VST2 and CLAP wrappers.
// Light hardware-style box (aspect ~1.16:1): black glass screen with an analog glowing VU needle (output power)
// and a faint waveform, one big AMOUNT knob with dotted scale, and four tactile A-B-C-D keys.
class HydroampfEditor
{
public:
    typedef void (*ParamSetter)(void* user, int index, float normalised);

    HydroampfEditor(Engine* engine, ParamSetter setter, void* user);
    ~HydroampfEditor();

    static constexpr int kWidth = 470, kHeight = 404;
    static intptr_t getRect(ERect** rect);

    intptr_t open(void* parentWindow);
    void close();
    void idle() {}

#if defined(_WIN32)
    intptr_t wndProc(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam);
#endif

private:
    Engine* eng;
    ParamSetter setParam;
    void* setUser;
    void* hwnd = nullptr;
    void* fontFamily = nullptr;     // Gdiplus::FontFamily*, created in open(), freed in close()

    int   dragParam = -1;           // engine param being dragged
    float dragStartY = 0, dragStartVal = 0;
    int   hover = -1;               // 0..3 params, 10+mode pad, 20/21 mode arrows
    bool  tracking = false;

    // display state (UI thread only)
    double dispHead = 0.0;
    long long lastTick = 0;
    float meterIn = -90.f, meterOut = -90.f, meterGr = 0.f, needle = -60.f;

    void paint();
    void mouseDown(int x, int y, bool dbl);
    void mouseMove(int x, int y, bool shift);
    void mouseUp();
    void wheel(int x, int y, int delta);
    int  hit(int x, int y) const;
};
