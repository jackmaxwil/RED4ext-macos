#pragma once

#ifdef RED4EXT_PLATFORM_MACOS
#include <cstdint>

#ifndef NO_ERROR
#define NO_ERROR 0L
#endif

extern "C" {
int32_t DetourTransactionBegin();
int32_t DetourTransactionCommit();
int32_t DetourTransactionAbort();
int32_t DetourUpdateThread(void* hThread);
int32_t DetourAttach(void** ppPointer, void* pDetour);
int32_t DetourDetach(void** ppPointer, void* pDetour);

}
#endif
