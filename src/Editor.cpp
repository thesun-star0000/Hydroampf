#include "Editor.h"
#include "Hydroampf.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cwchar>

#if defined(_WIN32)
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM

namespace
{
    constexpr int kWinW = 380;
    constexpr int kWinH = 540;

    constexpr int kKnobCx = kWinW / 2;
    constexpr int kKnobCy = 220;
    constexpr int kKnobR  = 95;

    constexpr int kBtnY = 350;
    constexpr int kBtnW = 66;
    constexpr int kBtnH = 46;
    constexpr int kBtnGap = 14;

    constexpr int kModeLabelY = 408;

    constexpr int kMeterY = 448;
    constexpr int kMeterW = 300;
    constexpr int kMeterH = 10;

    constexpr int kSmallKnobY = 495;
    constexpr int kSmallKnobR = 34;
    constexpr int kMixCx = 95;
    constexpr int kCeilCx = kWinW - 95;

    constexpr UINT_PTR kTimerId = 1;

    // Hydroampf green theme, built around the requested accent #0EBA90
    constexpr COLORREF kAccent   = RGB(0x0E, 0xBA, 0x90);
    constexpr COLORREF kAccentDk = RGB(0x08, 0x6E, 0x55);
    constexpr COLORREF kAccentLt = RGB(0x6C, 0xE8, 0xC9);
    constexpr COLORREF kBg       = RGB(0x08, 0x12, 0x10);
    constexpr COLORREF kBg2      = RGB(0x0C, 0x1A, 0x17);
    constexpr COLORREF kPanel    = RGB(0x10, 0x21, 0x1C);
    constexpr COLORREF kPanelLn  = RGB(0x1B, 0x36, 0x2F);
    constexpr COLORREF kText     = RGB(0xE8, 0xFB, 0xF4);
    constexpr COLORREF kTextDim  = RGB(0x7E, 0xAE, 0xA0);

    const wchar_t* kClassName = L"HydroampfEditorWnd";

    int btnX(int i)
    {
        int totalW = kBtnW * 4 + kBtnGap * 3;
        int startX = (kWinW - totalW) / 2;
        return startX + i * (kBtnW + kBtnGap);
    }

    HFONT makeFont(int size, int weight, const wchar_t* face = L"Segoe UI")
    {
        return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    }

    void drawTextCentered(HDC dc, int cx, int y, int h, const wchar_t* text, COLORREF color, HFONT font)
    {
        HFONT old = static_cast<HFONT>(SelectObject(dc, font));
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        RECT r{ cx - 200, y, cx + 200, y + h };
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_TOP);
        SelectObject(dc, old);
    }

    void drawKnob(HDC dc, int cx, int cy, int radius, float value)
    {
        HPEN trackPen = CreatePen(PS_SOLID, 7, kAccentDk);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, trackPen));
        HBRUSH nullBrush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, nullBrush));
        Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);

        auto toRad = [](float deg) { return deg * 3.14159265f / 180.0f; };
        float startAngle = 135.0f;
        float sweep = 270.0f * std::clamp(value, 0.0f, 1.0f);
        float endAngle = startAngle + sweep;

        HPEN valPen = CreatePen(PS_SOLID, 7, kAccent);
        SelectObject(dc, valPen);
        int x1 = cx + int(radius * std::cos(toRad(startAngle)));
        int y1 = cy + int(radius * std::sin(toRad(startAngle)));
        int x2 = cx + int(radius * std::cos(toRad(endAngle)));
        int y2 = cy + int(radius * std::sin(toRad(endAngle)));
        if (sweep > 0.5f)
            Arc(dc, cx - radius, cy - radius, cx + radius, cy + radius, x1, y1, x2, y2);

        float pointerAngle = startAngle + sweep;
        int px = cx + int((radius - 12) * std::cos(toRad(pointerAngle)));
        int py = cy + int((radius - 12) * std::sin(toRad(pointerAngle)));
        HPEN ptrPen = CreatePen(PS_SOLID, 3, kAccentLt);
        SelectObject(dc, ptrPen);
        MoveToEx(dc, cx, cy, nullptr);
        LineTo(dc, px, py);

        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(trackPen);
        DeleteObject(valPen);
        DeleteObject(ptrPen);
    }
}

intptr_t HydroampfEditor::getRect(ERect** rect)
{
    static ERect r{ 0, 0, kWinH, kWinW };
    *rect = &r;
    return 1;
}

HydroampfEditor::HydroampfEditor(Hydroampf* owner) : plugin(owner) {}
HydroampfEditor::~HydroampfEditor() { close(); }

static LRESULT CALLBACK RawWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    HydroampfEditor* self = reinterpret_cast<HydroampfEditor*>(
        GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, w, l);
    return static_cast<LRESULT>(self->wndProc(h, msg, w, l));
}

intptr_t HydroampfEditor::wndProc(void* hwndVoid, unsigned msg, uintptr_t wParam, intptr_t lParam)
{
    HWND h = static_cast<HWND>(hwndVoid);
    switch (msg)
    {
        case WM_PAINT:
            paint();
            return 0;
        case WM_LBUTTONDOWN:
            handleMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            SetCapture(h);
            return 0;
        case WM_MOUSEMOVE:
            if (draggingKnob >= 0) handleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            handleMouseUp();
            ReleaseCapture();
            return 0;
        case WM_TIMER:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        default:
            return DefWindowProcW(h, msg, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
    }
}

intptr_t HydroampfEditor::open(void* parentWindow)
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = RawWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBg);
    RegisterClassW(&wc); // ok if it fails because already registered

    HWND parent = static_cast<HWND>(parentWindow);
    HWND h = CreateWindowExW(0, kClassName, L"Hydroampf",
                              WS_CHILD | WS_VISIBLE,
                              0, 0, kWinW, kWinH,
                              parent, nullptr, hInst, nullptr);
    hwnd = h;
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetTimer(h, kTimerId, 40, nullptr); // ~25fps for the live reduction meter
    ShowWindow(h, SW_SHOW);
    return 1;
}

void HydroampfEditor::close()
{
    if (hwnd)
    {
        KillTimer(static_cast<HWND>(hwnd), kTimerId);
        DestroyWindow(static_cast<HWND>(hwnd));
        hwnd = nullptr;
    }
}

void HydroampfEditor::idle() {}

void HydroampfEditor::paint()
{
    HWND h = static_cast<HWND>(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);

    RECT client; GetClientRect(h, &client);
    HBRUSH bgBrush = CreateSolidBrush(kBg);
    FillRect(dc, &client, bgBrush);
    DeleteObject(bgBrush);
    SetBkMode(dc, TRANSPARENT);

    // subtle secondary panel glow behind the title, echoing the reference art
    HBRUSH glowBrush = CreateSolidBrush(kBg2);
    HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, nullPen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, glowBrush));
    Ellipse(dc, kKnobCx - 160, kKnobCy - 140, kKnobCx + 160, kKnobCy + 180);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(glowBrush);

    HFONT titleFont = makeFont(30, FW_BOLD);
    drawTextCentered(dc, kWinW / 2, 22, 36, L"HYDROAMPF", kText, titleFont);
    DeleteObject(titleFont);

    HFONT subFont = makeFont(13, FW_NORMAL);
    drawTextCentered(dc, kWinW / 2, 58, 20, L"ONE-KNOB SHOCK PROCESSOR", kTextDim, subFont);
    DeleteObject(subFont);

    // --- big macro knob ---------------------------------------------------
    float driveVal = plugin->getParameter(kParamDrive);
    drawKnob(dc, kKnobCx, kKnobCy, kKnobR, driveVal);

    wchar_t pctText[16];
    swprintf(pctText, 16, L"%.0f %%", driveVal * 100.0f);
    HFONT bigFont = makeFont(34, FW_BOLD);
    drawTextCentered(dc, kKnobCx, kKnobCy - 20, 40, pctText, kText, bigFont);
    DeleteObject(bigFont);

    // --- preset buttons A/B/C/D --------------------------------------------
    int preset = plugin->getPreset();
    const wchar_t* letters[4] = { L"A", L"B", L"C", L"D" };
    HFONT btnFont = makeFont(18, FW_BOLD);
    for (int i = 0; i < 4; ++i)
    {
        RECT r{ btnX(i), kBtnY, btnX(i) + kBtnW, kBtnY + kBtnH };
        bool selected = (i == preset);

        HBRUSH fill = CreateSolidBrush(selected ? kAccent : kPanel);
        HPEN border = CreatePen(PS_SOLID, 1, selected ? kAccentLt : kPanelLn);
        HPEN op = static_cast<HPEN>(SelectObject(dc, border));
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(dc, fill));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, 10, 10);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(fill);
        DeleteObject(border);

        SelectObject(dc, btnFont);
        SetTextColor(dc, selected ? kBg : kText);
        DrawTextW(dc, letters[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DeleteObject(btnFont);

    // mode label
    char labelA[32];
    std::snprintf(labelA, sizeof(labelA), "%s", Hydroampf::presetLabel(preset));
    wchar_t wlabel[32];
    MultiByteToWideChar(CP_UTF8, 0, labelA, -1, wlabel, 32);
    HFONT modeFont = makeFont(15, FW_BOLD);
    drawTextCentered(dc, kWinW / 2, kModeLabelY, 22, wlabel, kAccent, modeFont);
    DeleteObject(modeFont);

    // --- reduction meter -----------------------------------------------
    int meterX = (kWinW - kMeterW) / 2;
    RECT meterBg{ meterX, kMeterY, meterX + kMeterW, kMeterY + kMeterH };
    HBRUSH meterBgBrush = CreateSolidBrush(kPanel);
    HPEN meterBorder = CreatePen(PS_SOLID, 1, kPanelLn);
    HPEN mop = static_cast<HPEN>(SelectObject(dc, meterBorder));
    HBRUSH mob = static_cast<HBRUSH>(SelectObject(dc, meterBgBrush));
    RoundRect(dc, meterBg.left, meterBg.top, meterBg.right, meterBg.bottom, 6, 6);
    SelectObject(dc, mop);
    SelectObject(dc, mob);
    DeleteObject(meterBgBrush);
    DeleteObject(meterBorder);

    float reduction = plugin->getMeterReduction();
    int fillW = int(kMeterW * std::clamp(reduction, 0.0f, 1.0f));
    if (fillW > 2)
    {
        HBRUSH fillBrush = CreateSolidBrush(kAccent);
        HPEN np = static_cast<HPEN>(GetStockObject(NULL_PEN));
        HPEN op2 = static_cast<HPEN>(SelectObject(dc, np));
        HBRUSH ob2 = static_cast<HBRUSH>(SelectObject(dc, fillBrush));
        RoundRect(dc, meterX, kMeterY, meterX + fillW, kMeterY + kMeterH, 6, 6);
        SelectObject(dc, op2);
        SelectObject(dc, ob2);
        DeleteObject(fillBrush);
    }

    HFONT smallLabelFont = makeFont(12, FW_NORMAL);
    drawTextCentered(dc, kWinW / 2, kMeterY + kMeterH + 4, 18, L"REDUCTION", kTextDim, smallLabelFont);

    // --- small knobs: MIX / CEILING ----------------------------------------
    float mixVal = plugin->getParameter(kParamMix);
    float ceilVal = plugin->getParameter(kParamOutput);

    drawKnob(dc, kMixCx, kSmallKnobY, kSmallKnobR, mixVal);
    drawKnob(dc, kCeilCx, kSmallKnobY, kSmallKnobR, ceilVal);

    wchar_t mixText[16], ceilText[16];
    swprintf(mixText, 16, L"%.0f %%", mixVal * 100.0f);
    float ceilDb = -6.0f + ceilVal * 6.0f;
    swprintf(ceilText, 16, L"%.1f dB", ceilDb);

    HFONT valFont = makeFont(13, FW_BOLD);
    drawTextCentered(dc, kMixCx, kSmallKnobY - 8, 18, mixText, kText, valFont);
    drawTextCentered(dc, kCeilCx, kSmallKnobY - 8, 18, ceilText, kText, valFont);
    DeleteObject(valFont);

    drawTextCentered(dc, kMixCx, kSmallKnobY + kSmallKnobR + 8, 16, L"MIX", kTextDim, smallLabelFont);
    drawTextCentered(dc, kCeilCx, kSmallKnobY + kSmallKnobR + 8, 16, L"CEILING", kTextDim, smallLabelFont);
    DeleteObject(smallLabelFont);

    EndPaint(h, &ps);
}

void HydroampfEditor::handleMouseDown(int x, int y)
{
    // big knob
    {
        int dx = x - kKnobCx, dy = y - kKnobCy;
        if (dx * dx + dy * dy <= kKnobR * kKnobR)
        {
            draggingKnob = 0;
            dragStartY = static_cast<float>(y);
            dragStartVal = plugin->getParameter(kParamDrive);
            return;
        }
    }
    // small knobs
    {
        int dx = x - kMixCx, dy = y - kSmallKnobY;
        if (dx * dx + dy * dy <= kSmallKnobR * kSmallKnobR)
        {
            draggingKnob = 1;
            dragStartY = static_cast<float>(y);
            dragStartVal = plugin->getParameter(kParamMix);
            return;
        }
    }
    {
        int dx = x - kCeilCx, dy = y - kSmallKnobY;
        if (dx * dx + dy * dy <= kSmallKnobR * kSmallKnobR)
        {
            draggingKnob = 2;
            dragStartY = static_cast<float>(y);
            dragStartVal = plugin->getParameter(kParamOutput);
            return;
        }
    }
    // preset buttons
    for (int i = 0; i < 4; ++i)
    {
        int left = btnX(i), top = kBtnY;
        if (x >= left && x <= left + kBtnW && y >= top && y <= top + kBtnH)
        {
            plugin->setPreset(i);
            InvalidateRect(static_cast<HWND>(hwnd), nullptr, FALSE);
            return;
        }
    }
}

void HydroampfEditor::handleMouseMove(int x, int y)
{
    if (draggingKnob < 0) return;
    float delta = (dragStartY - static_cast<float>(y)) / 150.0f; // drag up = increase
    float newVal = dragStartVal + delta;
    if (newVal < 0.0f) newVal = 0.0f;
    if (newVal > 1.0f) newVal = 1.0f;

    int paramIndex = (draggingKnob == 0) ? kParamDrive
                    : (draggingKnob == 1) ? kParamMix
                    : kParamOutput;
    plugin->setParameter(paramIndex, newVal);
    InvalidateRect(static_cast<HWND>(hwnd), nullptr, FALSE);
}

void HydroampfEditor::handleMouseUp()
{
    draggingKnob = -1;
}

#else // ------------------------------------------------------------------
// Vegas Pro is Windows-only, so on non-Windows platforms the editor is a
// harmless no-op stub.

intptr_t HydroampfEditor::getRect(ERect** rect) { *rect = nullptr; return 0; }
HydroampfEditor::HydroampfEditor(Hydroampf* owner) : plugin(owner) {}
HydroampfEditor::~HydroampfEditor() {}
intptr_t HydroampfEditor::open(void*) { return 0; }
void HydroampfEditor::close() {}
void HydroampfEditor::idle() {}
void HydroampfEditor::paint() {}
void HydroampfEditor::handleMouseDown(int, int) {}
void HydroampfEditor::handleMouseMove(int, int) {}
void HydroampfEditor::handleMouseUp() {}

#endif
