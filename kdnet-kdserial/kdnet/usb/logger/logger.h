/*++

Copyright (c) Microsoft Corporation

Module Name:

    logger.h

Abstract:

    Simple interface for (serial) debug output

Author:

    Karel Danihelka (KarelD)

--*/

#pragma once

#if defined(TRACE_DBG_PRINT)

#include <tracing.h>
#define DbgPrintf KdPrintf

#endif


//------------------------------------------------------------------------------

#define LOGGING_ZONE_NONE       0x0
#define LOGGING_ZONE_ERRORS     0x1
#define LOGGING_ZONE_WARNINGS   0x2
#define LOGGING_ZONE_INFO       0x4  

//------------------------------------------------------------------------------

VOID
DebugOutputInit(
    PDEBUG_DEVICE_DESCRIPTOR pDevice,
    ULONG index
    );

VOID
DebugOutputByte(
    const UCHAR byte
    );

//------------------------------------------------------------------------------

VOID
DbgSetZones(
    ULONG zones
    );

VOID
DbgEtwInit(
    );

#if !defined(TRACE_DBG_PRINT)

VOID
DbgPrintf(
    _Printf_format_string_ PSTR formatString,
    ...
    );

#endif

VOID
DbgPrintfWithZones(
    ULONG zones,
    _In_z_ PSTR formatString,
    ...
);

//------------------------------------------------------------------------------

#define LOGIF(Zones, Format, ...)       DbgPrintfWithZones(                 \
    Zones, "%s: " Format "\r\n", __FUNCTION__, __VA_ARGS__                  \
    )

#define LOG(Format, ...)                DbgPrintf(" %s: " Format "\r\n",    \
    __FUNCTION__, __VA_ARGS__                                               \
    )

#define LOG_TRACE(fmt, ...) \
    if (KdDbgPrintf != NULL) { \
        KdDbgPrintf("[%s:%d] " fmt "\r\n", __FUNCTION__, __LINE__, __VA_ARGS__); \
    }

//------------------------------------------------------------------------------

