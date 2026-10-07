#ifdef RED4EXT_PLATFORM_MACOS

#include "MainLookup.hpp"

#include <dlfcn.h>

void* Red4extLookupSymbol(void* handle, const char* name)
{
    static void* mainHandle = dlopen(nullptr, RTLD_LAZY);
    if (handle == nullptr || handle == RTLD_DEFAULT || handle == RTLD_MAIN_ONLY || handle == mainHandle)
    {
        return dlsym(RTLD_MAIN_ONLY, name);
    }

    return dlsym(handle, name);
}

#endif
