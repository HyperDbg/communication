/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    pch.h

Abstract:

    Network Kernel Debugger Extensibility DLL precompiled headers.

Author:

    Karel Danihelka (kareld)

--*/

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4201 4214)

#include <nthal.h>
#include <process.h>
#include <smbios.h>

typedef unsigned char BYTE;
typedef BYTE *PBYTE;
#include <minwindef.h>
#include <symcrypt.h>

//
// For ARM platform force strictly aligned copy as unaligned access 
// isn't supported on Cortex A7 by hardware for stricty ordered memory
// used by DMA transfers (as it is today).
//

#if (_ARM_) || (_ARM64_)
#undef RtlCopyMemory
#define RtlCopyMemory(d, s, l)  _memcpy_strict_align(d, s, l)
#endif

#pragma warning(default:4201 4214)
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif
