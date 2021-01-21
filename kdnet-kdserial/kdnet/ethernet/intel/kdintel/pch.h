/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    pch.h

Abstract:

    Network Kernel Debugger Extensibility DLL precompiled headers.

Author:

    Joe Ballantyne (joeball)

--*/

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4201 4214)

#include <nthal.h>
#include <process.h>
#include "kdnetshareddata.h"
#include "kdundi.h"
#include "kdintel.h"
#include "kdnetextensibility.h"
#include "tracing.h"

#pragma warning(default:4201 4214)
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

