// Offline harness: ./render in.wav out.wav mode amount  (16/24/32f stereo wav via simple reader)
#include "../src/dsp/MaxCore.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
int main(int argc,char**argv){
    if(argc<5){std::fprintf(stderr,"usage: render in.raw(f32 stereo) out.raw mode amount [sr]\n");return 1;}
    FILE*f=std::fopen(argv[1],"rb"); std::vector<float> d; float buf[4096]; size_t r;
    while((r=std::fread(buf,4,4096,f))>0) d.insert(d.end(),buf,buf+r); std::fclose(f);
    int mode=std::atoi(argv[3]); float amt=(float)std::atof(argv[4]); float sr=argc>5?(float)std::atof(argv[5]):48000.f;
    mx::MaxCore c; c.prepare(sr); mx::Params p; p.mode=mode; p.amount=amt;
    size_t n=d.size()/2; std::vector<float> L(n),R(n),oL(n),oR(n);
    for(size_t i=0;i<n;++i){L[i]=d[2*i];R[i]=d[2*i+1];}
    for(size_t i=0;i<n;i+=512){int m=(int)std::min<size_t>(512,n-i);c.process(&L[i],&R[i],&oL[i],&oR[i],m,p);}
    std::vector<float> o(2*n); for(size_t i=0;i<n;++i){o[2*i]=oL[i];o[2*i+1]=oR[i];}
    f=std::fopen(argv[2],"wb"); std::fwrite(o.data(),4,o.size(),f); std::fclose(f); return 0;}
