#include "../src/Engine.h"
#include <cstdio>
#include <vector>
int main(int argc,char**argv){
    FILE*f=std::fopen(argv[1],"rb"); std::vector<float> d; float b[4096]; size_t r;
    while((r=std::fread(b,4,4096,f))>0) d.insert(d.end(),b,b+r); std::fclose(f);
    auto*e=new Engine(); e->prepare(48000.f); e->setModeIdx(std::atoi(argv[3])); e->set(kPAmount,(float)std::atof(argv[4]));
    size_t n=d.size()/2; std::vector<float> L(n),R(n),oL(n),oR(n);
    for(size_t i=0;i<n;++i){L[i]=d[2*i];R[i]=d[2*i+1];}
    for(size_t i=0;i<n;i+=512){int m=(int)std::min<size_t>(512,n-i);e->process(&L[i],&R[i],&oL[i],&oR[i],m);}
    std::vector<float> o(2*n); for(size_t i=0;i<n;++i){o[2*i]=oL[i];o[2*i+1]=oR[i];}
    f=std::fopen(argv[2],"wb"); std::fwrite(o.data(),4,o.size(),f); std::fclose(f); return 0;}
