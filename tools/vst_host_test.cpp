// Fake VST2 host: loads the shared lib, runs the opcodes Vegas Pro uses, processes odd block sizes.
#include "../src/sdk/vestige.h"
#include <dlfcn.h>
#include <cstdio>
#include <cmath>
#include <vector>
static intptr_t host(AEffect*, int32_t op, int32_t, intptr_t, void*, float) { return op == audioMasterVersion ? 2400 : 0; }
int main(int, char** argv) {
    void* lib = dlopen(argv[1], RTLD_NOW); if (!lib) { puts(dlerror()); return 1; }
    auto entry = (AEffect*(*)(audioMasterCallback))dlsym(lib, "VSTPluginMain"); if (!entry) { puts("no VSTPluginMain"); return 1; }
    AEffect* e = entry(host);
    printf("magic ok: %d  params %d  in/out %d/%d  hasEditor %d\n", e->magic == 0x56737450, e->numParams, e->numInputs, e->numOutputs, (e->flags & effFlagsHasEditor) != 0);
    char s[256];
    e->dispatcher(e, effOpen, 0, 0, 0, 0);
    e->dispatcher(e, effGetEffectName, 0, 0, s, 0); printf("name: %s\n", s);
    e->dispatcher(e, effGetVendorString, 0, 0, s, 0); printf("vendor: %s\n", s);
    printf("category %ld, vstVersion %ld\n", (long)e->dispatcher(e, effGetPlugCategory, 0, 0, 0, 0), (long)e->dispatcher(e, effGetVstVersion, 0, 0, 0, 0));
    for (int i = 0; i < e->numParams; ++i) { char n[64], d[64]; e->dispatcher(e, effGetParamName, i, 0, n, 0); e->dispatcher(e, effGetParamDisplay, i, 0, d, 0); printf("  param %d %s = %s\n", i, n, d); }
    e->dispatcher(e, effSetSampleRate, 0, 0, 0, 44100.f); e->dispatcher(e, effSetBlockSize, 0, 4096, 0, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, 0, 0); e->dispatcher(e, effStartProcess, 0, 0, 0, 0);
    printf("initialDelay @44.1k: %d samples, tail %ld\n", e->initialDelay, (long)e->dispatcher(e, effGetTailSize, 0, 0, 0, 0));
    e->setParameter(e, 0, 1.0f);
    const int sizes[] = {1, 7, 64, 100, 512, 4096, 10000, 3};
    double pk = 0, ss = 0; long cnt = 0; long t = 0; bool finite = true;
    for (int round = 0; round < 40; ++round) for (int n : sizes) {
        std::vector<float> L(n), R(n), oL(n), oR(n); float* in[2] = {L.data(), R.data()}; float* out[2] = {oL.data(), oR.data()};
        for (int i = 0; i < n; ++i, ++t) { double tt = (t % 22050) / 44100.0; L[i] = R[i] = 0.5f * std::sin(2 * M_PI * (45 + 120 * std::exp(-tt * 30)) * tt) * std::exp(-tt * 7); }
        e->processReplacing(e, in, out, n);
        for (int i = 0; i < n; ++i) { if (!std::isfinite(oL[i])) finite = false; pk = std::max(pk, (double)std::fabs(oL[i])); ss += oL[i] * oL[i]; ++cnt; }
    }
    printf("odd block sizes ok, finite=%d, peak %.2f dBFS, rms %.2f dBFS\n", finite, 20 * log10(pk), 10 * log10(ss / cnt));
    for (int m = 0; m < 4; ++m) { e->setParameter(e, 1, m / 3.0f); std::vector<float> a(512, 0.1f), b(512, 0.1f), c(512), d(512); float* in[2] = {a.data(), b.data()}; float* out[2] = {c.data(), d.data()}; e->processReplacing(e, in, out, 512); }
    e->dispatcher(e, effStopProcess, 0, 0, 0, 0); e->dispatcher(e, effMainsChanged, 0, 0, 0, 0); e->dispatcher(e, effClose, 0, 0, 0, 0);
    puts("VST2 host test passed"); return 0; }
