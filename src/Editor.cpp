#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "Editor.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdarg>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
using std::min;   // gdiplus.h expects unqualified min/max
using std::max;
#include <gdiplus.h>

namespace
{
    using namespace Gdiplus;
    constexpr int W = HydroampfEditor::kWidth, H = HydroampfEditor::kHeight;
    constexpr float kPi = 3.14159265f;

    // ---- layout ----
    constexpr float kBodyX = 10.0f, kBodyY = 10.0f, kBodyW = W - 20.0f, kBodyH = H - 20.0f;
    constexpr float kSX = 26.0f, kSY = 26.0f, kSW = W - 52.0f, kSH = 180.0f;                // glass screen
    constexpr float kKX = 118.0f, kKY = 288.0f;                                             // knob centre
    constexpr float kFaceR = 38.0f, kRingR = 45.0f, kBezelR = 55.0f, kTickR = 66.0f;
    constexpr float kKeyW = 46.0f, kKeyH = 60.0f, kKeyGap = 8.0f, kKeyY = 246.0f;
    constexpr float kKeyX0 = W - 30.0f - (4 * kKeyW + 3 * kKeyGap);
    constexpr float kStripY = H - 40.0f;
    // needle geometry (pivot far below the screen, like a real VU meter)
    constexpr float kArcApex = kSY + 70.0f, kArcR = 300.0f, kPivX = kSX + kSW * 0.5f, kPivY = kArcApex + kArcR;
    constexpr float kArcHalf = 34.0f;    // degrees each side of vertical

    // Diagnostics: %TEMP%\Hydroampf.log
    void hlog(const char* fmt, ...) {
        char path[MAX_PATH + 32]; DWORD n = GetTempPathA(MAX_PATH, path); if (!n || n > MAX_PATH) return;
        std::strcat(path, "Hydroampf.log");
        FILE* f = std::fopen(path, "a"); if (!f) return;
        va_list a; va_start(a, fmt); std::vfprintf(f, fmt, a); va_end(a); std::fputc('\n', f); std::fclose(f);
    }
    bool g_gdiOk = false; int g_paints = 0;

    constexpr UINT_PTR kTimer = 1;
    const wchar_t* kClass = L"HydroampfEditorV6";
    int   g_open = 0;
    ULONG_PTR g_gdiToken = 0;
    HINSTANCE g_inst = nullptr;

    // glow colour per mode (A orange, B amber, C coral, D hot red)
    const int kAccent[4][3] = { { 255, 112, 42 }, { 255, 170, 50 }, { 255, 92, 66 }, { 240, 56, 92 } };
    inline Color acc(int m, int a = 255, float k = 1.0f) {
        return Color((BYTE)a, (BYTE)std::min(255.0f, kAccent[m][0] * k), (BYTE)std::min(255.0f, kAccent[m][1] * k), (BYTE)std::min(255.0f, kAccent[m][2] * k));
    }
    inline Color lerpC(const Color& a, const Color& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color((BYTE)(a.GetA() + (b.GetA() - a.GetA()) * t), (BYTE)(a.GetR() + (b.GetR() - a.GetR()) * t),
                     (BYTE)(a.GetG() + (b.GetG() - a.GetG()) * t), (BYTE)(a.GetB() + (b.GetB() - a.GetB()) * t));
    }

    void rr(GraphicsPath& p, float x, float y, float w, float h, float r) {
        r = std::min(r, std::min(w, h) * 0.5f);
        p.AddArc(x, y, r * 2, r * 2, 180.0f, 90.0f); p.AddArc(x + w - r * 2, y, r * 2, r * 2, 270.0f, 90.0f);
        p.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0.0f, 90.0f); p.AddArc(x, y + h - r * 2, r * 2, r * 2, 90.0f, 90.0f);
        p.CloseFigure();
    }
    // soft shadow / glow built from stacked translucent rounded rects
    void haze(Graphics& g, float x, float y, float w, float h, float r, int spread, int alphaEach, float dy, const Color& c) {
        for (int k = spread; k >= 1; --k) {
            GraphicsPath p; rr(p, x - k, y - k + dy, w + 2 * k, h + 2 * k, r + k);
            SolidBrush b(Color((BYTE)alphaEach, c.GetR(), c.GetG(), c.GetB())); g.FillPath(&b, &p);
        }
    }
    void shadow(Graphics& g, float x, float y, float w, float h, float r, int spread, int alphaEach, float dy) {
        haze(g, x, y, w, h, r, spread, alphaEach, dy, Color(255, 40, 44, 52));
    }
    void text(Graphics& g, const FontFamily* ff, const wchar_t* s, float x, float y, float px, const Color& col,
              StringAlignment h = StringAlignmentCenter, int style = FontStyleRegular)
    {
        Font font(ff, px, style, UnitPixel);
        StringFormat fmt; fmt.SetAlignment(h); fmt.SetLineAlignment(StringAlignmentCenter);
        fmt.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush br(col);
        g.DrawString(s, -1, &font, PointF(x, y), &fmt, &br);
    }
    // letter-spaced text (centre / left / right anchored)
    void spaced(Graphics& g, const FontFamily* ff, const wchar_t* s, float x, float y, float px, float adv, const Color& col,
                int anchor = 1, int style = FontStyleRegular)
    {
        int n = (int)wcslen(s); float total = adv * (float)n, x0 = anchor == 1 ? x - total * 0.5f : (anchor == 0 ? x : x - total);
        for (int i = 0; i < n; ++i) { wchar_t c[2] = { s[i], 0 }; text(g, ff, c, x0 + adv * ((float)i + 0.5f), y, px, col, StringAlignmentCenter, style); }
    }
    void glowText(Graphics& g, const FontFamily* ff, const wchar_t* s, float x, float y, float px, float adv, int mode, float strength, int anchor = 1) {
        const float o[8][2] = { {-1.5f,0},{1.5f,0},{0,-1.5f},{0,1.5f},{-3,0},{3,0},{0,-3},{0,3} };
        for (int i = 0; i < 8; ++i) spaced(g, ff, s, x + o[i][0], y + o[i][1], px, adv, acc(mode, (int)(26.0f * strength), 1.0f), anchor, FontStyleRegular);
        spaced(g, ff, s, x, y, px, adv, acc(mode, 255, 1.25f), anchor, FontStyleRegular);
    }
    void glowDot(Graphics& g, float x, float y, float r, const Color& core, int mode, float strength) {
        for (int i = 4; i >= 1; --i) {
            float r2 = r + (float)i * 2.4f; SolidBrush b(acc(mode, (int)(24.0f * strength / (float)i + 3.0f)));
            g.FillEllipse(&b, x - r2, y - r2, r2 * 2, r2 * 2);
        }
        SolidBrush c(core); g.FillEllipse(&c, x - r, y - r, r * 2, r * 2);
    }
    inline float keyX(int i) { return kKeyX0 + (float)i * (kKeyW + kKeyGap); }
    inline PointF arcPt(float f, float radius) {   // f: 0..1 along the VU scale
        float a = (-90.0f - kArcHalf + 2.0f * kArcHalf * f) * kPi / 180.0f;
        return PointF(kPivX + radius * std::cos(a), kPivY + radius * std::sin(a));
    }
}

intptr_t HydroampfEditor::getRect(ERect** rect) {
    static ERect r{ 0, 0, (int16_t)kHeight, (int16_t)kWidth }; *rect = &r; return 1;
}

HydroampfEditor::HydroampfEditor(Engine* e, ParamSetter s, void* u) : eng(e), setParam(s), setUser(u) {}
HydroampfEditor::~HydroampfEditor() { close(); }

static LRESULT CALLBACK RawProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<HydroampfEditor*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, m, w, l);
    return (LRESULT)self->wndProc(h, m, w, l);
}

intptr_t HydroampfEditor::wndProc(void* hv, unsigned msg, uintptr_t wp, intptr_t lp) {
    HWND h = (HWND)hv;
    switch (msg) {
        case WM_PAINT:        paint(); return 0;
        case WM_ERASEBKGND:   return 1;
        case WM_LBUTTONDOWN:  mouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false); SetCapture(h); return 0;
        case WM_LBUTTONDBLCLK: mouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true); SetCapture(h); return 0;
        case WM_MOUSEMOVE:
            if (!tracking) { TRACKMOUSEEVENT t{}; t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = h; TrackMouseEvent(&t); tracking = true; }
            mouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_SHIFT) != 0); return 0;
        case WM_MOUSELEAVE:   tracking = false; hover = -1; return 0;
        case WM_LBUTTONUP:    mouseUp(); ReleaseCapture(); return 0;
        case WM_MOUSEWHEEL: {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &p);
            wheel(p.x, p.y, GET_WHEEL_DELTA_WPARAM(wp)); return 0; }
        case WM_TIMER:        InvalidateRect(h, nullptr, FALSE); return 0;
        default: return DefWindowProcW(h, msg, (WPARAM)wp, (LPARAM)lp);
    }
}

intptr_t HydroampfEditor::open(void* parent) {
    if (hwnd) return 1;
    // This DLL's own module handle (works for both the .dll and the .clap build).
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&HydroampfEditor::getRect), &mod);
    g_inst = mod ? (HINSTANCE)mod : GetModuleHandleW(nullptr);
    if (g_open == 0) {
        GdiplusStartupInput in; Status st = GdiplusStartup(&g_gdiToken, &in, nullptr);
        g_gdiOk = (st == Ok); hlog("GdiplusStartup status=%d inst=%p", (int)st, (void*)g_inst);
        WNDCLASSW wc{}; wc.style = CS_DBLCLKS; wc.lpfnWndProc = RawProc; wc.hInstance = g_inst; wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        hlog("RegisterClassW -> %u err=%lu", (unsigned)RegisterClassW(&wc), (unsigned long)GetLastError());
    }
    ++g_open;
    auto* ff = new FontFamily(L"Segoe UI");
    if (ff->GetLastStatus() != Ok) { delete ff; ff = new FontFamily(L"Arial"); }
    fontFamily = ff;
    HWND h = CreateWindowExW(0, kClass, L"Hydroampf", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, kWidth, kHeight,
                             (HWND)parent, nullptr, g_inst, nullptr);
    hlog("CreateWindowExW parent=%p -> %p err=%lu", parent, (void*)h, (unsigned long)GetLastError());
    if (!h) {
        delete static_cast<FontFamily*>(fontFamily); fontFamily = nullptr;
        if (--g_open == 0) { UnregisterClassW(kClass, g_inst); GdiplusShutdown(g_gdiToken); }
        return 0;
    }
    hwnd = h; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)this);
    LARGE_INTEGER t; QueryPerformanceCounter(&t); lastTick = t.QuadPart; dispHead = (double)eng->waveHead();
    SetTimer(h, kTimer, 25, nullptr);   // ~40 fps
    return 1;
}

void HydroampfEditor::close() {
    if (!hwnd) return;
    HWND h = (HWND)hwnd; hwnd = nullptr;
    KillTimer(h, kTimer);
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    DestroyWindow(h);
    delete static_cast<FontFamily*>(fontFamily); fontFamily = nullptr;
    if (--g_open == 0) {
        UnregisterClassW(kClass, g_inst);   // never leave a class pointing into an unloaded DLL
        GdiplusShutdown(g_gdiToken);
    }
}

int HydroampfEditor::hit(int xi, int yi) const {
    const float x = (float)xi, y = (float)yi;
    float dx = x - kKX, dy = y - kKY;
    if (dx * dx + dy * dy <= (kTickR + 12.0f) * (kTickR + 12.0f)) return 0;
    for (int i = 0; i < 4; ++i) if (x >= keyX(i) && x <= keyX(i) + kKeyW && y >= kKeyY && y <= kKeyY + kKeyH + 6.0f) return 10 + i;
    { float ax = kSX + 22.0f, ay = kSY + 17.0f; if ((x - ax) * (x - ax) + (y - ay) * (y - ay) <= 13.0f * 13.0f) return 20; }
    { float ax = kSX + 124.0f, ay = kSY + 17.0f; if ((x - ax) * (x - ax) + (y - ay) * (y - ay) <= 13.0f * 13.0f) return 21; }
    return -1;
}

void HydroampfEditor::mouseDown(int x, int y, bool dbl) {
    int k = hit(x, y);
    if (k == 0) {
        if (dbl) { setParam(setUser, kPAmount, 0.5f); return; }
        dragParam = kPAmount; dragStartY = (float)y; dragStartVal = eng->get(kPAmount); return;
    }
    if (k >= 10 && k <= 13) setParam(setUser, kPMode, float(k - 10) / 3.0f);
    if (k == 20) setParam(setUser, kPMode, float((eng->mode() + 3) % 4) / 3.0f);
    if (k == 21) setParam(setUser, kPMode, float((eng->mode() + 1) % 4) / 3.0f);
}
void HydroampfEditor::mouseMove(int x, int y, bool shift) {
    if (dragParam >= 0) {
        float v = std::clamp(dragStartVal + (dragStartY - (float)y) / (shift ? 700.0f : 200.0f), 0.0f, 1.0f);
        setParam(setUser, dragParam, v);
        return;
    }
    hover = hit(x, y);
}
void HydroampfEditor::mouseUp() { dragParam = -1; }
void HydroampfEditor::wheel(int x, int y, int delta) {
    if (hit(x, y) != 0) return;
    setParam(setUser, kPAmount, std::clamp(eng->get(kPAmount) + (delta > 0 ? 0.02f : -0.02f), 0.0f, 1.0f));
}

void HydroampfEditor::paint() {
    HWND h = (HWND)hwnd; PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps);
    if (!fontFamily || !g_gdiOk) {
        { HBRUSH br = CreateSolidBrush(RGB(205, 207, 210)); RECT rc{ 0, 0, W, H }; FillRect(wdc, &rc, br); DeleteObject(br); }
        SetBkMode(wdc, 1); SetTextColor(wdc, RGB(160, 40, 40));
        const wchar_t* m = L"Hydroampf: GDI+ failed to start (see %TEMP%\\Hydroampf.log)"; TextOutW(wdc, 14, 20, m, (int)wcslen(m));
        EndPaint(h, &ps); return;
    }
    const FontFamily* ff = static_cast<FontFamily*>(fontFamily);

    // ---- timing / ballistics ----
    LARGE_INTEGER now, freq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    const double dt = std::clamp((double)(now.QuadPart - lastTick) / (double)freq.QuadPart, 0.0, 0.1); lastTick = now.QuadPart;
    const int head = eng->waveHead();
    double target = (double)head - 2.5; if (target < 0) target = 0;
    dispHead += dt * 50.0;
    if (dispHead > target) dispHead = target;
    if (dispHead < target - 3.0) dispHead = target - 1.0;
    auto toDb = [](float lin) { return 20.0f * std::log10(std::max(lin, 1e-5f)); };
    { float d = toDb(eng->peakLin()); meterOut = (d > meterOut) ? d : std::max(d, meterOut - (float)(40.0 * dt)); }
    { float d = toDb(eng->inPeakLin()); meterIn = (d > meterIn) ? d : std::max(d, meterIn - (float)(40.0 * dt)); }
    { // VU-style needle: ~100 ms rise, ~300 ms fall
        float tgt = std::max(-60.0f, meterOut); float tc = (tgt > needle) ? 0.10f : 0.30f;
        needle += (tgt - needle) * (1.0f - (float)std::exp(-dt / tc)); }
    meterGr += (std::min(0.0f, eng->grDb()) - meterGr) * 0.25f;
    const float level = std::clamp((meterOut + 40.0f) / 40.0f, 0.0f, 1.0f);

    // ---- off-screen buffer ----
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, W, H); HBITMAP obmp = (HBITMAP)SelectObject(dc, bmp);
    { HBRUSH br = CreateSolidBrush(RGB(205, 207, 210)); RECT rc{ 0, 0, W, H }; FillRect(dc, &rc, br); DeleteObject(br); }  // off-screen only
    bool paintFailed = false;
    try {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        const int mode = eng->mode();
        const float amt = eng->get(kPAmount);
        const Color ink(255, 70, 73, 80), inkDim(255, 124, 127, 134);

        // ---- room + body ----
        { LinearGradientBrush b(PointF(0, 0), PointF(0, (float)H), Color(255, 216, 218, 221), Color(255, 190, 192, 196)); g.FillRectangle(&b, 0.0f, 0.0f, (float)W, (float)H); }
        shadow(g, kBodyX, kBodyY, kBodyW, kBodyH, 30.0f, 20, 4, 9.0f);
        GraphicsPath body; rr(body, kBodyX, kBodyY, kBodyW, kBodyH, 30.0f);
        { LinearGradientBrush b(PointF(0, kBodyY), PointF(0, kBodyY + kBodyH), Color(255, 238, 239, 241), Color(255, 214, 216, 220)); g.FillPath(&b, &body);
          g.SetClip(&body);   // darker bottom strip like the "MINICOMP" nameplate
          { SolidBrush sb(Color(22, 40, 44, 52)); g.FillRectangle(&sb, kBodyX, kStripY, kBodyW, kBodyH); }
          { Pen dp(Color(50, 40, 44, 52), 1.0f); g.DrawLine(&dp, kBodyX, kStripY, kBodyX + kBodyW, kStripY); Pen hp(Color(140, 255, 255, 255), 1.0f); g.DrawLine(&hp, kBodyX, kStripY + 1.0f, kBodyX + kBodyW, kStripY + 1.0f); }
          g.ResetClip();
          LinearGradientBrush hb(PointF(0, kBodyY), PointF(0, kBodyY + kBodyH * 0.5f), Color(220, 255, 255, 255), Color(0, 255, 255, 255));
          Pen hp(&hb, 2.0f); GraphicsPath q; rr(q, kBodyX + 1.5f, kBodyY + 1.5f, kBodyW - 3.0f, kBodyH - 3.0f, 29.0f); g.DrawPath(&hp, &q);
          Pen ep(Color(70, 60, 64, 72), 1.2f); g.DrawPath(&ep, &body); }
        spaced(g, ff, L"HYDROAMPF", kBodyX + 26.0f, kStripY + 15.0f, 16.0f, 12.5f, ink, 0);
        spaced(g, ff, L"MAX", kBodyX + 150.0f, kStripY + 15.0f, 16.0f, 12.5f, inkDim, 0);
        glowDot(g, kBodyX + kBodyW - 26.0f, kStripY + 15.0f, 2.8f, acc(mode, 140 + (int)(115 * level), 1.1f), mode, 0.4f + 1.4f * level);

        // ---- glass screen ----
        {
            GraphicsPath bezel; rr(bezel, kSX - 6.0f, kSY - 6.0f, kSW + 12.0f, kSH + 12.0f, 20.0f);
            { LinearGradientBrush b(PointF(0, kSY - 6), PointF(0, kSY + kSH + 6), Color(255, 196, 198, 202), Color(255, 232, 233, 236)); g.FillPath(&b, &bezel);
              Pen ep(Color(60, 50, 54, 62), 1.2f); g.DrawPath(&ep, &bezel); }
            GraphicsPath sp; rr(sp, kSX, kSY, kSW, kSH, 15.0f);
            { LinearGradientBrush b(PointF(0, kSY), PointF(0, kSY + kSH), Color(255, 22, 12, 9), Color(255, 6, 4, 4)); g.FillPath(&b, &sp); }
            g.SetClip(&sp);

            // warm haze behind the needle, pulses with the level
            { GraphicsPath hz; hz.AddEllipse(kPivX - 190.0f, kSY + 96.0f - 70.0f, 380.0f, 150.0f);
              PathGradientBrush pb(&hz); pb.SetCenterPoint(PointF(kPivX, kSY + 100.0f)); pb.SetCenterColor(acc(mode, 38 + (int)(110 * level), 0.9f));
              Color sur(0, 255, 100, 30); INT cnt = 1; pb.SetSurroundColors(&sur, &cnt); g.FillPath(&pb, &hz); }

            // faint waveform along the bottom of the glass
            {
                const float mid = kSY + kSH - 30.0f, half = 20.0f; const int kCols = kWaveCols - 12;
                auto col = [&](long idx) -> float { if (idx < 0 || idx >= head || idx < head - (kWaveCols - 4)) return 0.0f; return eng->waveOut((int)idx); };
                auto sample = [&](double pos) -> float {
                    long c0 = (long)std::floor(pos); float f = (float)(pos - (double)c0);
                    auto sm = [&](long c) { return (col(c - 1) + 2.0f * col(c) + col(c + 1)) * 0.25f; };
                    return sm(c0) * (1.0f - f) + sm(c0 + 1) * f;
                };
                const int step = 4, n = (int)(kSW / step) + 1; std::vector<PointF> top((size_t)n), bot((size_t)n);
                for (int i = 0; i < n; ++i) {
                    float x = kSX + (float)(i * step); double u = (double)(i * step) / (double)kSW;
                    float v = std::min(1.0f, sample(dispHead - (double)kCols * (1.0 - u))); v = std::pow(v, 0.85f);
                    top[(size_t)i] = PointF(x, mid - v * half); bot[(size_t)n - 1 - (size_t)i] = PointF(x, mid + v * half);
                }
                GraphicsPath wp; wp.AddCurve(top.data(), n, 0.35f); wp.AddCurve(bot.data(), n, 0.35f); wp.CloseAllFigures();
                SolidBrush fb(acc(mode, 62, 1.0f)); g.FillPath(&fb, &wp);
                Pen ep(acc(mode, 150, 1.1f), 1.0f); ep.SetLineJoin(LineJoinRound); g.DrawPath(&ep, &wp);
            }

            // VU scale: arc, dotted ticks, labels
            { Pen glow(acc(mode, 30, 1.0f), 7.0f); g.DrawArc(&glow, kPivX - kArcR, kPivY - kArcR, kArcR * 2, kArcR * 2, -90.0f - kArcHalf, kArcHalf * 2);
              Pen core(acc(mode, 200, 0.95f), 1.6f); g.DrawArc(&core, kPivX - kArcR, kPivY - kArcR, kArcR * 2, kArcR * 2, -90.0f - kArcHalf, kArcHalf * 2);
              const float redFrom = 0.835f;   // last stretch (-5 .. 0) in hot red
              Pen hot(Color(235, 255, 70, 50), 2.2f); g.DrawArc(&hot, kPivX - kArcR, kPivY - kArcR, kArcR * 2, kArcR * 2, -90.0f - kArcHalf + 2 * kArcHalf * redFrom, 2 * kArcHalf * (1.0f - redFrom)); }
            { static const float fr[5] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 5.0f / 6.0f, 1.0f };
              static const wchar_t* lb[5] = { L"-30", L"-20", L"-10", L"-5", L"0" };
              for (int i = 0; i < 5; ++i) {
                  for (float r = kArcR - 8.0f; r <= kArcR + 18.0f; r += 5.0f) { PointF p = arcPt(fr[i], r); SolidBrush d(acc(mode, 150, 1.0f)); g.FillEllipse(&d, p.X - 0.9f, p.Y - 0.9f, 1.8f, 1.8f); }
                  PointF lp = arcPt(fr[i], kArcR + 36.0f);
                  glowText(g, ff, lb[i], lp.X, lp.Y, 15.0f, 8.6f, mode, 0.5f);
              }
              for (int i = 0; i <= 30; ++i) { if (i % 10 == 0) continue; PointF a = arcPt((float)i / 30.0f, kArcR - 2.0f), b2 = arcPt((float)i / 30.0f, kArcR + 5.0f);
                  Pen mp(acc(mode, 90, 1.0f), 1.0f); g.DrawLine(&mp, a.X, a.Y, b2.X, b2.Y); } }

            // labels / readouts
            glowText(g, ff, L"MAXIMIZER", kSX + kSW * 0.5f, kSY + 112.0f, 19.0f, 14.5f, mode, 0.8f + level);
            spaced(g, ff, L"BY HYDROAMPF", kSX + kSW * 0.5f, kSY + 132.0f, 9.0f, 7.4f, acc(mode, 150, 0.9f));
            { wchar_t lb[16]; swprintf(lb, 16, L"MODE %c", L'A' + mode);
              PointF a1[3] = { PointF(kSX + 25.0f, kSY + 11.0f), PointF(kSX + 25.0f, kSY + 23.0f), PointF(kSX + 18.0f, kSY + 17.0f) };
              PointF a2[3] = { PointF(kSX + 121.0f, kSY + 11.0f), PointF(kSX + 121.0f, kSY + 23.0f), PointF(kSX + 128.0f, kSY + 17.0f) };
              SolidBrush b1(acc(mode, hover == 20 ? 255 : 170, 1.1f)), b2(acc(mode, hover == 21 ? 255 : 170, 1.1f)); g.FillPolygon(&b1, a1, 3); g.FillPolygon(&b2, a2, 3);
              spaced(g, ff, lb, kSX + 73.0f, kSY + 17.0f, 12.0f, 9.4f, acc(mode, 230, 1.1f)); }
            spaced(g, ff, L"dB", kSX + kSW - 16.0f, kSY + 17.0f, 11.0f, 8.0f, acc(mode, 120, 0.9f), 2);
            { wchar_t v[24];
              spaced(g, ff, L"INPUT", kSX + 16.0f, kSY + kSH - 36.0f, 9.5f, 7.6f, acc(mode, 140, 0.9f), 0);
              if (meterIn > -59.0f) swprintf(v, 24, L"%.2f dB", meterIn); else swprintf(v, 24, L"-inf dB");
              spaced(g, ff, v, kSX + 16.0f, kSY + kSH - 20.0f, 12.0f, 8.4f, acc(mode, 235, 1.1f), 0);
              spaced(g, ff, L"OUTPUT", kSX + kSW - 16.0f, kSY + kSH - 36.0f, 9.5f, 7.6f, acc(mode, 140, 0.9f), 2);
              if (meterOut > -59.0f) swprintf(v, 24, L"%.2f dB", meterOut); else swprintf(v, 24, L"-inf dB");
              spaced(g, ff, v, kSX + kSW - 16.0f, kSY + kSH - 20.0f, 12.0f, 8.4f, acc(mode, 235, 1.1f), 2); }

            // the needle
            { float f = std::clamp((needle + 30.0f) / 30.0f, -0.12f, 1.08f);
              PointF tip = arcPt(f, kArcR + 46.0f), root = arcPt(f, kArcR - 150.0f);
              for (int i = 3; i >= 1; --i) { Pen gp(acc(mode, 20 + (int)(22 * level), 1.0f), 2.0f + (float)i * 3.0f); gp.SetStartCap(LineCapRound); gp.SetEndCap(LineCapRound); g.DrawLine(&gp, root.X, root.Y, tip.X, tip.Y); }
              Pen np(acc(mode, 255, 1.1f), 3.0f); g.DrawLine(&np, root.X, root.Y, tip.X, tip.Y);
              Pen cp(Color(255, 255, 238, 214), 1.0f); g.DrawLine(&cp, root.X, root.Y, tip.X, tip.Y); }

            // glass: top shadow, diagonal sheen, bottom rim light
            { LinearGradientBrush sh(PointF(0, kSY), PointF(0, kSY + 22.0f), Color(120, 0, 0, 0), Color(0, 0, 0, 0)); g.FillRectangle(&sh, kSX, kSY, kSW, 22.0f);
              PointF poly[4] = { PointF(kSX, kSY), PointF(kSX + kSW * 0.46f, kSY), PointF(kSX + kSW * 0.20f, kSY + kSH), PointF(kSX, kSY + kSH) };
              SolidBrush sb(Color(13, 255, 255, 255)); g.FillPolygon(&sb, poly, 4); }
            g.ResetClip();
            Pen rim(Color(70, 255, 255, 255), 1.0f); g.DrawPath(&rim, &sp);
        }

        // ---- knob ----
        {
            // recessed bezel
            { GraphicsPath c; c.AddEllipse(kKX - kBezelR, kKY - kBezelR, kBezelR * 2, kBezelR * 2);
              LinearGradientBrush b(PointF(0, kKY - kBezelR), PointF(0, kKY + kBezelR), Color(255, 196, 198, 202), Color(255, 236, 237, 240)); g.FillPath(&b, &c);
              Pen e1(Color(60, 50, 54, 62), 1.5f); g.DrawEllipse(&e1, kKX - kBezelR, kKY - kBezelR, kBezelR * 2, kBezelR * 2);
              Pen e2(Color(190, 255, 255, 255), 1.5f); g.DrawArc(&e2, kKX - kBezelR, kKY - kBezelR, kBezelR * 2, kBezelR * 2, 20.0f, 140.0f); }
            // dotted scale (lit dots up to the value glow in the accent colour)
            const int kDots = 21;
            for (int i = 0; i < kDots; ++i) {
                float t = (float)i / (float)(kDots - 1), a = (135.0f + 270.0f * t) * kPi / 180.0f;
                float x = kKX + kTickR * std::cos(a), y = kKY + kTickR * std::sin(a);
                bool lit = (t <= amt + 0.001f);
                if (lit) glowDot(g, x, y, 1.9f, acc(mode, 255, 1.1f), mode, 0.5f + level); else { SolidBrush d(Color(255, 92, 95, 102)); g.FillEllipse(&d, x - 1.5f, y - 1.5f, 3.0f, 3.0f); }
            }
            spaced(g, ff, L"0", kKX - 52.0f, kKY + 54.0f, 11.0f, 7.0f, ink); spaced(g, ff, L"100", kKX + 52.0f, kKY + 54.0f, 11.0f, 7.0f, ink);
            spaced(g, ff, L"AMOUNT", kKX, kKY + 70.0f, 12.0f, 9.4f, ink);
            // knob body: soft shadow, black ring, white face (warms toward the accent as Amount grows)
            const float breathe = 1.0f + 0.006f * level;
            const float fr = kFaceR * breathe, rr_ = kRingR * breathe;
            shadow(g, kKX - rr_, kKY - rr_, rr_ * 2, rr_ * 2, rr_, 9, 6, 6.0f);
            { SolidBrush b(Color(255, 34, 36, 40)); g.FillEllipse(&b, kKX - rr_, kKY - rr_, rr_ * 2, rr_ * 2);
              Pen e(Color(120, 255, 255, 255), 1.2f); g.DrawArc(&e, kKX - rr_ + 1, kKY - rr_ + 1, rr_ * 2 - 2, rr_ * 2 - 2, 200.0f, 110.0f); }
            { GraphicsPath c; c.AddEllipse(kKX - fr, kKY - fr, fr * 2, fr * 2);
              PathGradientBrush pb(&c); pb.SetCenterPoint(PointF(kKX - fr * 0.25f, kKY - fr * 0.35f));
              pb.SetCenterColor(lerpC(Color(255, 252, 252, 253), acc(mode, 255, 1.15f), amt * 0.42f));
              Color sur = lerpC(Color(255, 204, 206, 211), acc(mode, 255, 0.82f), amt * 0.42f); INT cnt = 1; pb.SetSurroundColors(&sur, &cnt); g.FillPath(&pb, &c);
              LinearGradientBrush sp(PointF(kKX - fr * 0.6f, kKY - fr * 0.9f), PointF(kKX, kKY - fr * 0.2f), Color(150, 255, 255, 255), Color(0, 255, 255, 255));
              g.FillEllipse(&sp, kKX - fr * 0.62f, kKY - fr * 0.9f, fr * 1.24f, fr * 0.7f);
              Pen ep(Color(70, 40, 44, 52), 1.2f); g.DrawEllipse(&ep, kKX - fr, kKY - fr, fr * 2, fr * 2); }
            { float a = (135.0f + 270.0f * amt) * kPi / 180.0f, pr = fr * 0.70f;
              glowDot(g, kKX + pr * std::cos(a), kKY + pr * std::sin(a), 3.0f, acc(mode, 255, 1.1f), mode, 0.6f + 0.8f * level); }
        }

        // ---- mode keys: tactile caps, selected one pressed and glowing ----
        spaced(g, ff, L"MODE", keyX(0) + (4 * kKeyW + 3 * kKeyGap) * 0.5f, kKeyY - 14.0f, 11.0f, 8.6f, ink);
        static const wchar_t* ltr[4] = { L"A", L"B", L"C", L"D" };
        for (int i = 0; i < 4; ++i) {
            const bool sel = (i == mode), hot = (hover == 10 + i);
            const float x = keyX(i), y = kKeyY + (sel ? 3.0f : 0.0f);
            { GraphicsPath base; rr(base, x, kKeyY + 5.0f, kKeyW, kKeyH, 13.0f);
              shadow(g, x, kKeyY + 5.0f, kKeyW, kKeyH, 13.0f, 8, 5, 5.0f);
              SolidBrush bb(Color(255, 160, 163, 169)); g.FillPath(&bb, &base); }
            if (sel) haze(g, x, y, kKeyW, kKeyH, 13.0f, 9, 7, 0.0f, acc(i, 255, 1.0f));
            GraphicsPath cap; rr(cap, x, y, kKeyW, kKeyH, 13.0f);
            Color c1 = sel ? acc(i, 255, 1.12f) : (hot ? Color(255, 246, 247, 249) : Color(255, 238, 239, 241));
            Color c2 = sel ? acc(i, 255, 0.84f) : Color(255, 213, 215, 219);
            LinearGradientBrush b(PointF(0, y), PointF(0, y + kKeyH), c1, c2); g.FillPath(&b, &cap);
            LinearGradientBrush hb(PointF(0, y), PointF(0, y + kKeyH * 0.6f), Color(sel ? 90 : 220, 255, 255, 255), Color(0, 255, 255, 255));
            Pen hp(&hb, 1.5f); GraphicsPath q; rr(q, x + 1, y + 1, kKeyW - 2, kKeyH - 2, 12.0f); g.DrawPath(&hp, &q);
            Pen ep(Color(sel ? 120 : 70, 60, 50, 50), 1.1f); g.DrawPath(&ep, &cap);
            if (sel) { glowText(g, ff, ltr[i], x + kKeyW * 0.5f, y + kKeyH * 0.5f, 24.0f, 14.0f, i, 0.9f); }
            else { text(g, ff, ltr[i], x + kKeyW * 0.5f + 0.8f, y + kKeyH * 0.5f + 1.2f, 22.0f, Color(200, 255, 255, 255), StringAlignmentCenter, FontStyleRegular);
                   text(g, ff, ltr[i], x + kKeyW * 0.5f, y + kKeyH * 0.5f, 22.0f, Color(255, 96, 99, 106), StringAlignmentCenter, FontStyleRegular); }
        }
        { wchar_t b[32]; swprintf(b, 32, L"GR %.1f dB", meterGr); spaced(g, ff, b, keyX(0) + (4 * kKeyW + 3 * kKeyGap) * 0.5f, kKeyY + kKeyH + 34.0f, 11.0f, 8.4f, inkDim); }
        if (g_paints < 3) hlog("paint #%d graphics status=%d head=%d", g_paints, (int)g.GetLastStatus(), head);
    } catch (...) { paintFailed = true; hlog("exception while painting"); }
    ++g_paints;
    if (paintFailed) { SetBkMode(dc, 1); SetTextColor(dc, RGB(160, 40, 40)); const wchar_t* m = L"Hydroampf: paint error (see %TEMP%\\Hydroampf.log)"; TextOutW(dc, 14, 20, m, (int)wcslen(m)); }
    BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obmp); DeleteObject(bmp); DeleteDC(dc);
    EndPaint(h, &ps);
}

#else // non-Windows: headless stub so the DSP/plugin code still builds for testing
intptr_t HydroampfEditor::getRect(ERect** r) { *r = nullptr; return 0; }
HydroampfEditor::HydroampfEditor(Engine* e, ParamSetter s, void* u) : eng(e), setParam(s), setUser(u) {}
HydroampfEditor::~HydroampfEditor() {}
intptr_t HydroampfEditor::open(void*) { return 0; }
void HydroampfEditor::close() {}
void HydroampfEditor::paint() {}
void HydroampfEditor::mouseDown(int, int, bool) {}
void HydroampfEditor::mouseMove(int, int, bool) {}
void HydroampfEditor::mouseUp() {}
void HydroampfEditor::wheel(int, int, int) {}
int HydroampfEditor::hit(int, int) const { return -1; }
#endif
