#pragma once

#ifdef RED4EXT_PLATFORM_MACOS

// dlopen(nullptr) is RTLD_DEFAULT and will see RTLD_GLOBAL plugins.
// The main executable is looked up with RTLD_MAIN_ONLY instead.
void* Red4extLookupSymbol(void* handle, const char* name);

#endif
