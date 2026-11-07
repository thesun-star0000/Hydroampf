// Tiny fake CLAP host: loads Hydroampf.clap, creates the plugin, sets params and processes audio.
#include "../src/sdk/clap_min.h"
#include <dlfcn.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>
static uint32_t evSize(const clap_input_events_t*){return 0;}
static const clap_event_header_t* evGet(const clap_input_events_t*,uint32_t){return nullptr;}
static int pushed=0;
static bool evPush(const clap_output_events_t*,const clap_event_header_t*){++pushed;return true;}
static const void* hostExt(const clap_host_t*,const char*){return nullptr;}
int main(int,char**argv){
    void*lib=dlopen(argv[1],RTLD_NOW); if(!lib){printf("dlopen fail %s\n",dlerror());return 1;}
    auto*entry=(const clap_plugin_entry_t*)dlsym(lib,"clap_entry"); if(!entry){puts("no clap_entry");return 1;}
    entry->init("x");
    auto*f=(const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    printf("plugins: %u  name: %s\n",f->get_plugin_count(f),f->get_plugin_descriptor(f,0)->name);
    clap_host_t host{{1,2,2},nullptr,"test","t","","1",hostExt,nullptr,nullptr,nullptr};
    const clap_plugin_t*p=f->create_plugin(f,&host,"com.hydroampf.maximizer");
    p->init(p); p->activate(p,48000,32,512); p->start_processing(p);
    auto*prm=(const clap_plugin_params_t*)p->get_extension(p,CLAP_EXT_PARAMS);
    for(uint32_t i=0;i<prm->count(p);++i){clap_param_info_t in; prm->get_info(p,i,&in); double v; prm->get_value(p,in.id,&v); char t[64]; prm->value_to_text(p,in.id,v,t,64);
        printf(" param %u %s [%.1f..%.1f] = %s\n",in.id,in.name,in.min_value,in.max_value,t);}
    auto*ap=(const clap_plugin_audio_ports_t*)p->get_extension(p,CLAP_EXT_AUDIO_PORTS);
    printf(" audio ports in=%u out=%u\n",ap->count(p,true),ap->count(p,false));
    // process a decaying kick loop
    int n=48000; std::vector<float> L(n),R(n),oL(n),oR(n);
    for(int i=0;i<n;++i){double t=(i%12000)/48000.0; float s=0.5f*std::sin(2*M_PI*(45+120*std::exp(-t*30))*t)*std::exp(-t*7); L[i]=R[i]=s;}
    clap_input_events_t in{nullptr,evSize,evGet}; clap_output_events_t out{nullptr,evPush};
    float*ip[2]={L.data(),R.data()}; float*op[2]={oL.data(),oR.data()};
    clap_audio_buffer_t ib{ip,nullptr,2,0,0}, ob{op,nullptr,2,0,0};
    clap_process_t pr{0,512,nullptr,&ib,&ob,1,1,&in,&out};
    double pk=0,ss=0; 
    for(int o=0;o+512<=n;o+=512){ ip[0]=L.data()+o;ip[1]=R.data()+o;op[0]=oL.data()+o;op[1]=oR.data()+o; if(p->process(p,&pr)!=CLAP_PROCESS_CONTINUE){puts("process err");return 1;}}
    for(int i=0;i<n;++i){pk=std::max(pk,(double)std::fabs(oL[i]));ss+=oL[i]*oL[i];}
    printf(" processed OK: peak %.2f dBFS  rms %.2f dBFS\n",20*log10(pk),10*log10(ss/n));
    // state roundtrip
    auto*st=(const clap_plugin_state_t*)p->get_extension(p,CLAP_EXT_STATE); std::vector<char> buf;
    clap_ostream_t os{&buf,[](const clap_ostream_t*s,const void*b,uint64_t sz)->int64_t{auto*v=(std::vector<char>*)s->ctx;v->insert(v->end(),(const char*)b,(const char*)b+sz);return (int64_t)sz;}};
    bool ok=st->save(p,&os); printf(" state save: %s (%zu bytes)\n",ok?"ok":"FAIL",buf.size());
    p->stop_processing(p); p->deactivate(p); p->destroy(p); entry->deinit(); puts("CLAP host test passed"); return 0;}
