#pragma once
// Maximus-style 3-band compressor + look-ahead-free soft limiter.
// Modes A..D mimic the character of the four Soundgoodizer presets:
//   A: balanced, mild glue          B: low-end focused (kick/bass)
//   C: bright/airy, more highs      D: heavy "loud" smash
// Amount (0..1) drives input gain, compression depth and final loudness.
#include <cmath>
#include <algorithm>
#include <array>

namespace mx {

constexpr float kPi = 3.14159265358979323846f;
inline float dbToLin(float d) { return std::pow(10.0f, d * 0.05f); }
inline float linToDb(float l) { return 20.0f * std::log10(std::max(l, 1e-9f)); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void lowpass(float fc,float sr,float q=0.70710678f){
        float w=2*kPi*fc/sr,c=std::cos(w),s=std::sin(w),al=s/(2*q),a0=1+al;
        b0=(1-c)/2/a0;b1=(1-c)/a0;b2=b0;a1=-2*c/a0;a2=(1-al)/a0;}
    void highpass(float fc,float sr,float q=0.70710678f){
        float w=2*kPi*fc/sr,c=std::cos(w),s=std::sin(w),al=s/(2*q),a0=1+al;
        b0=(1+c)/2/a0;b1=-(1+c)/a0;b2=b0;a1=-2*c/a0;a2=(1-al)/a0;}
    void peak(float fc,float sr,float gdb,float q){
        float A=std::pow(10.0f,gdb/40.0f),w=2*kPi*fc/sr,c=std::cos(w),s=std::sin(w),al=s/(2*q),a0=1+al/A;
        b0=(1+al*A)/a0;b1=-2*c/a0;b2=(1-al*A)/a0;a1=-2*c/a0;a2=(1-al/A)/a0;}
    void highshelf(float fc,float sr,float gdb){
        float A=std::pow(10.0f,gdb/40.0f),w=2*kPi*fc/sr,c=std::cos(w),s=std::sin(w);
        float al=s/2*std::sqrt(2.0f),tA=2*std::sqrt(A)*al;
        float a0=(A+1)-(A-1)*c+tA;
        b0=A*((A+1)+(A-1)*c+tA)/a0;b1=-2*A*((A-1)+(A+1)*c)/a0;b2=A*((A+1)+(A-1)*c-tA)/a0;
        a1=2*((A-1)-(A+1)*c)/a0;a2=((A+1)-(A-1)*c-tA)/a0;}
    inline float process(float x){float y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
    void reset(){z1=z2=0;}
};

// 4th-order Linkwitz-Riley split (two cascaded 2nd-order Butterworths)
struct LR4 {
    Biquad lp1,lp2,hp1,hp2;
    void set(float fc,float sr){lp1.lowpass(fc,sr);lp2.lowpass(fc,sr);hp1.highpass(fc,sr);hp2.highpass(fc,sr);}
    inline void split(float x,float&lo,float&hi){lo=lp2.process(lp1.process(x));hi=hp2.process(hp1.process(x));}
    void reset(){lp1.reset();lp2.reset();hp1.reset();hp2.reset();}
};

struct BandComp {
    float env=0;           // linked envelope (dB domain handled in process)
    float gr=0;            // last gain reduction in dB (<=0)
    inline float process(float l,float r,float thrDb,float ratio,float atk,float rel,float makeup){
        float det=std::max(std::fabs(l),std::fabs(r));
        float c=det>env?atk:rel; env=c*env+(1-c)*det;
        float lv=linToDb(env), over=lv-thrDb;
        float g=0; if(over>0) g=-over*(1-1/ratio);
        gr=g; return dbToLin(g+makeup);
    }
};

struct Params { float amount=0.5f; int mode=0; float mix=1.0f; float ceilingDb=-0.3f; };

class MaxCore {
public:
    void prepare(float sr){
        sampleRate=sr;
        xo[0].set(150.0f,sr); xo[1].set(2500.0f,sr);
        for(int c=0;c<2;++c){xoL[c].set(150.0f,sr);xoH[c].set(2500.0f,sr);
            // allpass-compensation: low band also passes the high crossover's phase
            xoLcomp[c].set(2500.0f,sr);}
        hp30[0].highpass(25.0f,sr,0.7071f);hp30[1].highpass(25.0f,sr,0.7071f);
        setMode(mode);
        reset();
    }
    void reset(){
        for(int c=0;c<2;++c){xoL[c].reset();xoH[c].reset();xoLcomp[c].reset();hp30[c].reset();
            for(auto&b:tone[c])b.reset();}
        for(auto&b:comp)b=BandComp{};
        limEnv=0; peakGr=0;
    }
    void setMode(int m){
        mode=std::clamp(m,0,3);
        const float sr=sampleRate;
        // per-mode EQ voicing (low shelf bump via peak, presence, air)
        static const float lowDb[4] ={ 1.5f, 4.0f, 0.5f, 3.0f};
        static const float lowHz[4] ={ 70.0f,60.0f,80.0f,65.0f};
        static const float airDb[4] ={ 1.0f,-0.5f, 3.5f, 2.0f};
        for(int c=0;c<2;++c){
            tone[c][0].peak(lowHz[mode],sr,lowDb[mode],0.9f);
            tone[c][1].highshelf(7000.0f,sr,airDb[mode]);
        }
    }
    void setSampleRate(float sr){ if(sr!=sampleRate) prepare(sr); }

    // process stereo in place-ish (in -> out)
    void process(const float*inL,const float*inR,float*outL,float*outR,int n,const Params&p){
        if(p.mode!=mode) setMode(p.mode);
        const float a=std::clamp(p.amount,0.0f,1.0f);
        // mode tables: threshold offsets (dB) per band, ratio, attack/release (ms), drive (dB)
        static const float ratioLo[4]={2.5f,4.0f,2.0f,5.0f};
        static const float ratioMd[4]={2.5f,2.5f,2.5f,4.0f};
        static const float ratioHi[4]={2.0f,1.8f,3.0f,4.0f};
        static const float driveMax[4]={14.0f,14.0f,15.0f,20.0f};
        static const float atkMs[4]={8.0f,12.0f,4.0f,3.0f};
        static const float relMs[4]={120.0f,160.0f,90.0f,70.0f};
        const float drive=dbToLin(a*driveMax[mode]);
        const float thr=-6.0f-a*18.0f;                       // threshold sinks with Amount
        const float ratios[3]={ratioLo[mode],ratioMd[mode],ratioHi[mode]};
        const float atk=std::exp(-1.0f/(atkMs[mode]*0.001f*sampleRate));
        const float rel=std::exp(-1.0f/(relMs[mode]*0.001f*sampleRate));
        const float mkK[4]={0.85f,0.80f,0.90f,0.95f};         // makeup fraction of expected GR
        const float ceil=dbToLin(p.ceilingDb);
        const float limRel=std::exp(-1.0f/(0.06f*sampleRate));
        float blockPeak=0, blockGr=0;

        for(int i=0;i<n;++i){
            float dry[2]={inL[i],inR[i]}, b[3][2];
            for(int c=0;c<2;++c){
                float s=hp30[c].process(dry[c])*drive;
                s=tone[c][1].process(tone[c][0].process(s));
                float lo,rest,mid,hi;
                xoL[c].split(s,lo,rest);
                xoH[c].split(rest,mid,hi);
                // phase-align low band with the high crossover (LR4 allpass)
                float dummyHi; float lo2;
                xoLcomp[c].split(lo,lo2,dummyHi); lo=lo2+dummyHi;
                b[0][c]=lo;b[1][c]=mid;b[2][c]=hi;
            }
            float outS[2]={0,0};
            for(int k=0;k<3;++k){
                float g=comp[k].process(b[k][0],b[k][1],thr+(k==0?2.0f:k==2?-2.0f:0.0f),ratios[k],atk,rel,-thr*(1.0f-1.0f/ratios[k])*mkK[mode]*a);
                blockGr=std::min(blockGr,comp[k].gr);
                outS[0]+=b[k][0]*g; outS[1]+=b[k][1]*g;
            }
            // soft clip then fast peak limiter to ceiling
            float pk=0;
            for(int c=0;c<2;++c){ outS[c]=softClip(outS[c]); pk=std::max(pk,std::fabs(outS[c])); }
            float lg=1.0f;
            float c2=pk>limEnv?0.0f:limRel; limEnv=c2*limEnv+(1-c2)*pk; // instant attack
            if(limEnv>ceil) lg=ceil/limEnv;
            for(int c=0;c<2;++c){
                float wet=outS[c]*lg;
                wet=std::clamp(wet,-ceil,ceil);
                float o=dry[c]*(1.0f-p.mix)+wet*p.mix;
                (c==0?outL:outR)[i]=o;
                blockPeak=std::max(blockPeak,std::fabs(o));
            }
        }
        peakGr=blockGr; lastPeak=blockPeak;
    }
    float getGainReductionDb() const { return peakGr; }
    float getLastPeak() const { return lastPeak; }
    int getMode() const { return mode; }

private:
    static inline float softClip(float x){
        // transparent below ~0.7, smooth saturation above
        float ax=std::fabs(x); if(ax<0.7f) return x;
        float s=x<0?-1.0f:1.0f; float t=(ax-0.7f)/0.3f;
        return s*(0.7f+0.3f*std::tanh(t));
    }
    float sampleRate=44100.0f; int mode=0;
    LR4 xo[2], xoL[2], xoH[2], xoLcomp[2];
    Biquad hp30[2]; std::array<Biquad,2> tone[2];
    BandComp comp[3];
    float limEnv=0, peakGr=0, lastPeak=0;
};

} // namespace mx
