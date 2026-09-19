#include "Hydroampf.h"

VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster)
{
    Hydroampf* plugin = new Hydroampf(audioMaster);
    return plugin->getAEffect();
}

// Some older hosts (and Vegas Pro's legacy VST scanner in particular) still
// look for the symbol name "main" on Windows instead of VSTPluginMain.
// MSVC forbids a function literally named "main" from returning anything
// but int, so we give it a different C++ name and export it under the
// symbol name "main" via the .def file instead (see Hydroampf.def).
#if defined(_WIN32)
extern "C" __declspec(dllexport) AEffect* HydroampfLegacyMainEntry(audioMasterCallback audioMaster)
{
    return VSTPluginMain(audioMaster);
}
#endif
