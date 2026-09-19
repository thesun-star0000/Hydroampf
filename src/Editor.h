#pragma once
#include "sdk/vestige.h"

class Hydroampf;

// Native Win32/GDI editor laid out like the reference design: big center
// "macro" knob, four A/B/C/D preset buttons with a mode label, a live
// reduction meter bar, and two small knobs (Mix / Ceiling) - all in the
// Hydroampf green theme built around #0EBA90.
class HydroampfEditor
{
public:
    explicit HydroampfEditor(Hydroampf* owner);
    ~HydroampfEditor();

    static intptr_t getRect(ERect** rect);
    intptr_t open(void* parentWindow);
    void close();
    void idle();

#if defined(_WIN32)
    intptr_t wndProc(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam);
#endif

private:
    Hydroampf* plugin;
    void* hwnd = nullptr;

    // -1 = nothing, 0 = big knob, 1 = mix knob, 2 = ceiling knob
    int   draggingKnob = -1;
    float dragStartY = 0.0f;
    float dragStartVal = 0.0f;

    void paint();
    void handleMouseDown(int x, int y);
    void handleMouseMove(int x, int y);
    void handleMouseUp();
};
