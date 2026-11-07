// CLAP wrapper for Hydroampf. Build as Hydroampf.clap (a renamed DLL on Windows).
#include "sdk/clap_min.h"
#include "Engine.h"
#include "Editor.h"
#include <cstring>
#include <new>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// flush-to-zero / denormals-are-zero while processing (restored on every exit path)
struct CsrGuard {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    unsigned old; CsrGuard() : old(_mm_getcsr()) { _mm_setcsr(old | 0x8040u); } ~CsrGuard() { _mm_setcsr(old); }
#endif
};

constexpr const char* kId = "com.hydroampf.maximizer";
const char* kFeatures[] = { "audio-effect", "limiter", "compressor", "stereo", nullptr };
const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT, kId, "Hydroampf Maximizer", "Hydroampf Audio", "", "", "", "2.0.0",
    "One-knob multiband maximizer with A/B/C/D voicings and a live waveform", kFeatures };

// CLAP-side value <-> engine-normalised value
double toClap(int id, float eng)   { return id == kPMode ? double(int(eng * 3.0f + 0.5f)) : double(eng); }
float  fromClap(int id, double v)  { return id == kPMode ? float(v) / 3.0f : float(v); }

struct Plugin {
    clap_plugin_t plugin;
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    Engine engine;
    HydroampfEditor* editor = nullptr;
    std::atomic<uint32_t> dirty{0};          // bit per param changed by the GUI, to be sent to the host
    bool processing = false;
};
Plugin* P(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }

void sendDirty(Plugin* pl, const clap_output_events_t* out) {
    if (!out) return;
    uint32_t d = pl->dirty.exchange(0);
    for (int i = 0; i < kPNumParams; ++i) if (d & (1u << i)) {
        clap_event_param_value_t e{};
        e.header.size = sizeof(e); e.header.time = 0; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = 0;
        e.param_id = (clap_id)i; e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = toClap(i, pl->engine.get(i));
        out->try_push(out, &e.header);
    }
}
void handleEvents(Plugin* pl, const clap_input_events_t* in) {
    if (!in) return;
    for (uint32_t i = 0, n = in->size(in); i < n; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) {
            auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
            if (e->param_id < (clap_id)kPNumParams) pl->engine.set((int)e->param_id, fromClap((int)e->param_id, e->value));
        }
    }
}

// ---- audio ports ----
uint32_t apCount(const clap_plugin_t*, bool) { return 1; }
bool apGet(const clap_plugin_t*, uint32_t i, bool isInput, clap_audio_port_info_t* info) {
    if (i != 0) return false;
    info->id = isInput ? 0 : 1; std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN; info->channel_count = 2; info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 1 : 0; return true;
}
const clap_plugin_audio_ports_t kAudioPorts = { apCount, apGet };

// ---- params ----
uint32_t pCount(const clap_plugin_t*) { return kPNumParams; }
bool pInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
    if (i >= (uint32_t)kPNumParams) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = i; info->flags = CLAP_PARAM_IS_AUTOMATABLE | (i == kPMode ? CLAP_PARAM_IS_STEPPED : 0);
    std::snprintf(info->name, sizeof(info->name), "%s", Engine::paramName((int)i));
    info->min_value = 0; info->max_value = (i == kPMode) ? 3.0 : 1.0;
    static const double defs[kPNumParams] = { 0.5, 0.0, 1.0, 0.95 };
    info->default_value = defs[i]; return true;
}
bool pGet(const clap_plugin_t* p, clap_id id, double* out) {
    if (id >= (clap_id)kPNumParams) return false;
    *out = toClap((int)id, P(p)->engine.get((int)id));
    return true;
}
bool pText(const clap_plugin_t* p, clap_id id, double v, char* out, uint32_t cap) {
    if (id >= (clap_id)kPNumParams) return false;
    Engine::textFor((int)id, fromClap((int)id, v), out, cap); (void)p; return true;
}
bool pFromText(const clap_plugin_t*, clap_id id, const char* t, double* out) {
    if (id >= (clap_id)kPNumParams) return false;
    if (id == kPMode) { char c = t[0]; if (c >= 'a' && c <= 'd') c -= 32; if (c < 'A' || c > 'D') return false; *out = c - 'A'; return true; }
    double v = std::atof(t);
    if (id == kPCeiling) v = (v + 6.0) / 6.0; else v /= 100.0;
    *out = v < 0 ? 0 : (v > 1 ? 1 : v); return true;
}
void pFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
    handleEvents(P(p), in); sendDirty(P(p), out);
}
const clap_plugin_params_t kParams = { pCount, pInfo, pGet, pText, pFromText, pFlush };

// ---- state ----
bool sSave(const clap_plugin_t* p, const clap_ostream_t* s) {
    float v[kPNumParams]; for (int i = 0; i < kPNumParams; ++i) v[i] = P(p)->engine.get(i);
    const char* b = reinterpret_cast<const char*>(v); uint64_t left = sizeof(v);
    while (left) { int64_t w = s->write(s, b, left); if (w <= 0) return false; b += w; left -= (uint64_t)w; }
    return true;
}
bool sLoad(const clap_plugin_t* p, const clap_istream_t* s) {
    float v[kPNumParams]; char* b = reinterpret_cast<char*>(v); uint64_t left = sizeof(v);
    while (left) { int64_t r = s->read(s, b, left); if (r <= 0) return false; b += r; left -= (uint64_t)r; }
    for (int i = 0; i < kPNumParams; ++i) P(p)->engine.set(i, v[i]);
    return true;
}
const clap_plugin_state_t kState = { sSave, sLoad };

// ---- latency ----
uint32_t latGet(const clap_plugin_t* p) { return (uint32_t)P(p)->engine.latencySamples(); }
const clap_plugin_latency_t kLatency = { latGet };

// ---- gui (Windows embedded only) ----
void guiSet(void* u, int i, float v) {
    auto* pl = static_cast<Plugin*>(u);
    pl->engine.set(i, v); pl->dirty.fetch_or(1u << i);
    if (pl->hostParams && pl->hostParams->request_flush) pl->hostParams->request_flush(pl->host);
}
bool gApi(const clap_plugin_t*, const char* api, bool floating) {
#if defined(_WIN32)
    return !floating && api && !std::strcmp(api, CLAP_WINDOW_API_WIN32);
#else
    (void)api; (void)floating; return false;
#endif
}
bool gPref(const clap_plugin_t*, const char** api, bool* floating) { *api = CLAP_WINDOW_API_WIN32; *floating = false; return true; }
bool gCreate(const clap_plugin_t* p, const char* api, bool floating) {
    if (!gApi(p, api, floating)) return false;
    Plugin* pl = P(p); if (!pl->editor) pl->editor = new HydroampfEditor(&pl->engine, guiSet, pl); return true;
}
void gDestroy(const clap_plugin_t* p) { Plugin* pl = P(p); if (pl->editor) { pl->editor->close(); delete pl->editor; pl->editor = nullptr; } }
bool gScale(const clap_plugin_t*, double) { return false; }
bool gSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { *w = HydroampfEditor::kWidth; *h = HydroampfEditor::kHeight; return true; }
bool gCanResize(const clap_plugin_t*) { return false; }
bool gHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool gAdjust(const clap_plugin_t*, uint32_t* w, uint32_t* h) { *w = HydroampfEditor::kWidth; *h = HydroampfEditor::kHeight; return true; }
bool gSetSize(const clap_plugin_t*, uint32_t, uint32_t) { return true; }
bool gParent(const clap_plugin_t* p, const clap_window_t* w) {
    Plugin* pl = P(p); if (!pl->editor || !w) return false; return pl->editor->open(w->win32) != 0;
}
bool gTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void gTitle(const clap_plugin_t*, const char*) {}
bool gShow(const clap_plugin_t*) { return true; }
bool gHide(const clap_plugin_t*) { return true; }
const clap_plugin_gui_t kGui = { gApi, gPref, gCreate, gDestroy, gScale, gSize, gCanResize, gHints, gAdjust,
                                 gSetSize, gParent, gTransient, gTitle, gShow, gHide };

// ---- plugin ----
bool plInit(const clap_plugin_t* p) {
    Plugin* pl = P(p);
    if (pl->host && pl->host->get_extension)
        pl->hostParams = static_cast<const clap_host_params_t*>(pl->host->get_extension(pl->host, CLAP_EXT_PARAMS));
    return true;
}
void plDestroy(const clap_plugin_t* p) { Plugin* pl = P(p); gDestroy(p); delete pl; }
bool plActivate(const clap_plugin_t* p, double sr, uint32_t, uint32_t) {
    Plugin* pl = P(p); pl->engine.prepare((float)sr);
    if (pl->host && pl->host->get_extension) {
        auto* hl = static_cast<const clap_host_latency_t*>(pl->host->get_extension(pl->host, CLAP_EXT_LATENCY));
        if (hl && hl->changed) hl->changed(pl->host);
    }
    return true;
}
void plDeactivate(const clap_plugin_t*) {}
bool plStart(const clap_plugin_t* p) { P(p)->processing = true; return true; }
void plStop(const clap_plugin_t* p) { P(p)->processing = false; }
void plReset(const clap_plugin_t* p) { P(p)->engine.reset(); }

clap_process_status plProcess(const clap_plugin_t* p, const clap_process_t* pr) {
    Plugin* pl = P(p);
    CsrGuard csrGuard;
    handleEvents(pl, pr->in_events);
    if (pr->audio_inputs_count < 1 || pr->audio_outputs_count < 1) { sendDirty(pl, pr->out_events); return CLAP_PROCESS_CONTINUE; }
    const clap_audio_buffer_t& in = pr->audio_inputs[0]; clap_audio_buffer_t& out = pr->audio_outputs[0];
    if (!in.data32 || !out.data32 || in.channel_count < 1 || out.channel_count < 1) return CLAP_PROCESS_CONTINUE;
    const float* iL = in.data32[0];  const float* iR = in.channel_count > 1 ? in.data32[1] : in.data32[0];
    float* oL = out.data32[0];       float* oR = out.channel_count > 1 ? out.data32[1] : out.data32[0];
    pl->engine.process(iL, iR, oL, oR, (int)pr->frames_count);
    sendDirty(pl, pr->out_events);
    return CLAP_PROCESS_CONTINUE;
}
const void* plExt(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (!std::strcmp(id, CLAP_EXT_PARAMS))      return &kParams;
    if (!std::strcmp(id, CLAP_EXT_STATE))       return &kState;
    if (!std::strcmp(id, CLAP_EXT_LATENCY))     return &kLatency;
    if (!std::strcmp(id, CLAP_EXT_GUI))         return &kGui;
    return nullptr;
}
void plMain(const clap_plugin_t*) {}

// ---- factory / entry ----
uint32_t fCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* fDesc(const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &kDesc : nullptr; }
const clap_plugin_t* fCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!id || std::strcmp(id, kId)) return nullptr;
    Plugin* pl = new (std::nothrow) Plugin(); if (!pl) return nullptr;
    pl->host = host;
    pl->plugin = { &kDesc, pl, plInit, plDestroy, plActivate, plDeactivate, plStart, plStop, plReset, plProcess, plExt, plMain };
    return &pl->plugin;
}
const clap_plugin_factory_t kFactory = { fCount, fDesc, fCreate };

bool eInit(const char*) { return true; }
void eDeinit() {}
const void* eFactory(const char* id) { return (id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID)) ? &kFactory : nullptr; }

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, eInit, eDeinit, eFactory };
