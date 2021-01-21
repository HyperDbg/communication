/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    kdintel.h

Abstract:

    Intel Network Kernel Debug Extensibility header.

Author:

    Joe Ballantyne (joeball)

--*/

#define PCI_VID_INTEL 0x8086

//
// kdintel.c
//

ULONG
IntelGetHardwareContextSize (
    __in PDEBUG_DEVICE_DESCRIPTOR Device
    );

NTSTATUS
IntelInitializeLibrary (
    __in_opt PCHAR LoaderOptions,
    __inout PDEBUG_DEVICE_DESCRIPTOR Device
    );

NTSTATUS
IntelInitializeController(
    __in PKDNET_SHARED_DATA Adapter
    );

