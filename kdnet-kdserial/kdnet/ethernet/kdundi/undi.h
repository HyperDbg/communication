/*++

Copyright (c) Microsoft Corporation

Module Name:

    undi.h

Abstract:

    UNDI Network Kernel Debug Support.

Author:

    Joe Ballantyne (joeball)

--*/

//
// Entry points into the UNDI EFI driver code.
//

typedef EFI_STATUS (*PUNDI_INITIALIZE)(VOID);
typedef EFI_STATUS (*PUNDI_HARDWARE_SUPPORTED)(VOID);
typedef EFI_STATUS (*PUNDI_DRIVER_START)(VOID);
typedef VOID (*PUNDI_API_ENTRY)(IN UINT64 cdb);

#ifdef _UNDI_C

PUNDI_INITIALIZE UndiInitializeDriver = NULL;
PUNDI_HARDWARE_SUPPORTED UndiHardwareSupported = NULL;
PUNDI_DRIVER_START UndiDriverStart = NULL;
PUNDI_API_ENTRY UndiApiEntry = NULL;

#else

extern PUNDI_INITIALIZE UndiInitializeDriver;
extern PUNDI_HARDWARE_SUPPORTED UndiHardwareSupported;
extern PUNDI_DRIVER_START UndiDriverStart;
extern PUNDI_API_ENTRY UndiApiEntry;

#endif

ULONG
UndiGetHardwareContextSize (
    ULONG Length
    );
