/*++

Copyright (c) Microsoft Corporation

Module Name:

    kdextension.c

Abstract:

    Network Kernel Debug Extensibility Support.  This module implements the
    extensibility entry points.

Author:

    Joe Ballantyne (joeball)
    Fredrik Grohn (fgrohn)
    Karel Danihelka (kareld)

--*/

#include "pch.h"
#include <logger.h>

//------------------------------------------------------------------------------

#define KDQCOM_SUBTYPE_CHIPIDEA_USBFN       1
#define KDQCOM_SUBTYPE_CHIPIDEA_AX88772     2
#define KDQCOM_SUBTYPE_CHIPIDEA_USBFNB      3
#define KDQCOM_SUBTYPE_SYNOPSYS_USBFN       4
#define KDQCOM_SUBTYPE_SYNOPSYS_USBFNB      5

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;
KDNET_EXTENSIBILITY_EXPORTS KdNetExtensibilityExports;
KDNET_USBFNMP_EXPORTS KdNetUsbFnMpExports;

//------------------------------------------------------------------------------

VOID
DriverEntry (
    __in PDRIVER_OBJECT DriverObject,
    __in PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine is a dummy function that allows the extensibility module to
    link against the kernel mode GS buffer overflow library.

Arguments:

    DriverObject - Supplies a driver object.

    RegistryPath - Supplies the driver's registry path.

Return Value:

    None.

--*/

{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
}

//------------------------------------------------------------------------------

VOID
KdSetHibernateRange (
    VOID
    )
{
    //
    // Mark kdnet extension code.
    //

    PoSetHiberRange(NULL,
                    PO_MEM_BOOT_PHASE,
                    (PVOID)(ULONG_PTR)KdSetHibernateRange,
                    0,
                    'endK');
}

//------------------------------------------------------------------------------

NTSTATUS
KdInitializeController(
    __inout PKDNET_SHARED_DATA KdNet
    )
{
    NTSTATUS Status;
    USHORT PortType;

    if (KdNetExtensibilityExports.KdInitializeController != NULL) {
        Status = KdNetExtensibilityExports.KdInitializeController(KdNet);
        goto KdInitializeControllerEnd;
    }

    if ((KdNet == NULL) || (KdNet->Device == NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        KdNetErrorString = L"Invalid Parameters.";
        goto KdInitializeControllerEnd;
    }

    if ((KdNet->Device->OemData != NULL) &&
        (KdNet->Device->OemDataLength >= sizeof(USHORT))) {
        
        PortType = *(PUSHORT)KdNet->Device->OemData;
    
    } else {
        PortType = KDQCOM_SUBTYPE_CHIPIDEA_USBFN;    
    }

    switch (PortType)
        {
        case KDQCOM_SUBTYPE_CHIPIDEA_USBFN:
        default:
            KdNetExtensibilityExports.KdInitializeController = KdUsbFnInitializeController;
            KdNetExtensibilityExports.KdShutdownController = KdUsbFnShutdownController;
            KdNetExtensibilityExports.KdGetTxPacket = KdUsbFnGetTxPacket;
            KdNetExtensibilityExports.KdSendTxPacket = KdUsbFnSendTxPacket;
            KdNetExtensibilityExports.KdGetRxPacket = KdUsbFnGetRxPacket;
            KdNetExtensibilityExports.KdReleaseRxPacket = KdUsbFnReleaseRxPacket;
            KdNetExtensibilityExports.KdGetPacketAddress = KdUsbFnGetPacketAddress;
            KdNetExtensibilityExports.KdGetPacketLength = KdUsbFnGetPacketLength;
            KdNetExtensibilityExports.KdGetHardwareContextSize = KdUsbFnGetHardwareContextSize;
            KdNetUsbFnMpExports.InitializeLibrary = UsbFnMpChipideaInitializeLibrary;
            KdNetUsbFnMpExports.DeinitializeLibrary = UsbFnMpChipideaDeinitializeLibrary;
            KdNetUsbFnMpExports.GetHardwareContextSize = UsbFnMpChipideaGetHardwareContextSize;
            KdNetUsbFnMpExports.StartController = UsbFnMpChipideaStartController;
            KdNetUsbFnMpExports.StopController = UsbFnMpChipideaStopController;
            KdNetUsbFnMpExports.AllocateTransferBuffer = UsbFnMpChipideaAllocateTransferBuffer;
            KdNetUsbFnMpExports.FreeTransferBuffer = UsbFnMpChipideaFreeTransferBuffer;
            KdNetUsbFnMpExports.ConfigureEnableEndpoints = UsbFnMpChipideaConfigureEnableEndpoints;
            KdNetUsbFnMpExports.GetEndpointMaxPacketSize = UsbFnMpChipideaGetEndpointMaxPacketSize;
            KdNetUsbFnMpExports.GetMaxTransferSize = UsbFnMpChipideaGetMaxTransferSize;
            KdNetUsbFnMpExports.GetEndpointStallState = UsbFnMpChipideaGetEndpointStallState;
            KdNetUsbFnMpExports.SetEndpointStallState = UsbFnMpChipideaSetEndpointStallState;
            KdNetUsbFnMpExports.Transfer = UsbFnMpChipideaTransfer;
            KdNetUsbFnMpExports.AbortTransfer = UsbFnMpChipideaAbortTransfer;
            KdNetUsbFnMpExports.EventHandler = UsbFnMpChipideaEventHandler;
            KdNetUsbFnMpExports.TimeDelay = UsbFnMpChipideaTimeDelay;
            KdNetUsbFnMpExports.IsZLPSupported = UsbFnMpChipideaIsZLPSupported;
            break;
        case KDQCOM_SUBTYPE_CHIPIDEA_USBFNB:
            KdNetExtensibilityExports.KdInitializeController = KdUsbFnBInitializeController;
            KdNetExtensibilityExports.KdShutdownController = KdUsbFnBShutdownController;
            KdNetExtensibilityExports.KdGetTxPacket = KdUsbFnBGetTxPacket;
            KdNetExtensibilityExports.KdSendTxPacket = KdUsbFnBSendTxPacket;
            KdNetExtensibilityExports.KdGetRxPacket = KdUsbFnBGetRxPacket;
            KdNetExtensibilityExports.KdReleaseRxPacket = KdUsbFnBReleaseRxPacket;
            KdNetExtensibilityExports.KdGetPacketAddress = KdUsbFnBGetPacketAddress;
            KdNetExtensibilityExports.KdGetPacketLength = KdUsbFnBGetPacketLength;
            KdNetExtensibilityExports.KdGetHardwareContextSize = KdUsbFnBGetHardwareContextSize;
            KdNetUsbFnMpExports.InitializeLibrary = UsbFnMpChipideaInitializeLibrary;
            KdNetUsbFnMpExports.DeinitializeLibrary = UsbFnMpChipideaDeinitializeLibrary;
            KdNetUsbFnMpExports.GetHardwareContextSize = UsbFnMpChipideaGetHardwareContextSize;
            KdNetUsbFnMpExports.StartController = UsbFnMpChipideaStartController;
            KdNetUsbFnMpExports.StopController = UsbFnMpChipideaStopController;
            KdNetUsbFnMpExports.AllocateTransferBuffer = UsbFnMpChipideaAllocateTransferBuffer;
            KdNetUsbFnMpExports.FreeTransferBuffer = UsbFnMpChipideaFreeTransferBuffer;
            KdNetUsbFnMpExports.ConfigureEnableEndpoints = UsbFnMpChipideaConfigureEnableEndpoints;
            KdNetUsbFnMpExports.GetEndpointMaxPacketSize = UsbFnMpChipideaGetEndpointMaxPacketSize;
            KdNetUsbFnMpExports.GetMaxTransferSize = UsbFnMpChipideaGetMaxTransferSize;
            KdNetUsbFnMpExports.GetEndpointStallState = UsbFnMpChipideaGetEndpointStallState;
            KdNetUsbFnMpExports.SetEndpointStallState = UsbFnMpChipideaSetEndpointStallState;
            KdNetUsbFnMpExports.Transfer = UsbFnMpChipideaTransfer;
            KdNetUsbFnMpExports.AbortTransfer = UsbFnMpChipideaAbortTransfer;
            KdNetUsbFnMpExports.EventHandler = UsbFnMpChipideaEventHandler;
            KdNetUsbFnMpExports.TimeDelay = UsbFnMpChipideaTimeDelay;
            KdNetUsbFnMpExports.IsZLPSupported = UsbFnMpChipideaIsZLPSupported;
            break;
        case KDQCOM_SUBTYPE_SYNOPSYS_USBFN:
            KdNetExtensibilityExports.KdInitializeController = KdUsbFnInitializeController;
            KdNetExtensibilityExports.KdShutdownController = KdUsbFnShutdownController;
            KdNetExtensibilityExports.KdGetTxPacket = KdUsbFnGetTxPacket;
            KdNetExtensibilityExports.KdSendTxPacket = KdUsbFnSendTxPacket;
            KdNetExtensibilityExports.KdGetRxPacket = KdUsbFnGetRxPacket;
            KdNetExtensibilityExports.KdReleaseRxPacket = KdUsbFnReleaseRxPacket;
            KdNetExtensibilityExports.KdGetPacketAddress = KdUsbFnGetPacketAddress;
            KdNetExtensibilityExports.KdGetPacketLength = KdUsbFnGetPacketLength;
            KdNetExtensibilityExports.KdGetHardwareContextSize = KdUsbFnGetHardwareContextSize;
            KdNetUsbFnMpExports.InitializeLibrary = UsbFnMpSynopsysInitializeLibrary;
            KdNetUsbFnMpExports.DeinitializeLibrary = UsbFnMpSynopsysDeinitializeLibrary;
            KdNetUsbFnMpExports.GetHardwareContextSize = UsbFnMpSynopsysGetHardwareContextSize;
            KdNetUsbFnMpExports.StartController = UsbFnMpSynopsysStartController;
            KdNetUsbFnMpExports.StopController = UsbFnMpSynopsysStopController;
            KdNetUsbFnMpExports.AllocateTransferBuffer = UsbFnMpSynopsysAllocateTransferBuffer;
            KdNetUsbFnMpExports.FreeTransferBuffer = UsbFnMpSynopsysFreeTransferBuffer;
            KdNetUsbFnMpExports.ConfigureEnableEndpoints = UsbFnMpSynopsysConfigureEnableEndpoints;
            KdNetUsbFnMpExports.GetEndpointMaxPacketSize = UsbFnMpSynopsysGetEndpointMaxPacketSize;
            KdNetUsbFnMpExports.GetMaxTransferSize = UsbFnMpSynopsysGetMaxTransferSize;
            KdNetUsbFnMpExports.GetEndpointStallState = UsbFnMpSynopsysGetEndpointStallState;
            KdNetUsbFnMpExports.SetEndpointStallState = UsbFnMpSynopsysSetEndpointStallState;
            KdNetUsbFnMpExports.Transfer = UsbFnMpSynopsysTransfer;
            KdNetUsbFnMpExports.AbortTransfer = UsbFnMpSynopsysAbortTransfer;
            KdNetUsbFnMpExports.EventHandler = UsbFnMpSynopsysEventHandler;
            KdNetUsbFnMpExports.TimeDelay = UsbFnMpSynopsysTimeDelay;
            KdNetUsbFnMpExports.IsZLPSupported = UsbFnMpSynopsysIsZLPSupported;
            break;
        case KDQCOM_SUBTYPE_SYNOPSYS_USBFNB:
            KdNetExtensibilityExports.KdInitializeController = KdUsbFnBInitializeController;
            KdNetExtensibilityExports.KdShutdownController = KdUsbFnBShutdownController;
            KdNetExtensibilityExports.KdGetTxPacket = KdUsbFnBGetTxPacket;
            KdNetExtensibilityExports.KdSendTxPacket = KdUsbFnBSendTxPacket;
            KdNetExtensibilityExports.KdGetRxPacket = KdUsbFnBGetRxPacket;
            KdNetExtensibilityExports.KdReleaseRxPacket = KdUsbFnBReleaseRxPacket;
            KdNetExtensibilityExports.KdGetPacketAddress = KdUsbFnBGetPacketAddress;
            KdNetExtensibilityExports.KdGetPacketLength = KdUsbFnBGetPacketLength;
            KdNetExtensibilityExports.KdGetHardwareContextSize = KdUsbFnBGetHardwareContextSize;
            KdNetUsbFnMpExports.InitializeLibrary = UsbFnMpSynopsysInitializeLibrary;
            KdNetUsbFnMpExports.DeinitializeLibrary = UsbFnMpSynopsysDeinitializeLibrary;
            KdNetUsbFnMpExports.GetHardwareContextSize = UsbFnMpSynopsysGetHardwareContextSize;
            KdNetUsbFnMpExports.StartController = UsbFnMpSynopsysStartController;
            KdNetUsbFnMpExports.StopController = UsbFnMpSynopsysStopController;
            KdNetUsbFnMpExports.AllocateTransferBuffer = UsbFnMpSynopsysAllocateTransferBuffer;
            KdNetUsbFnMpExports.FreeTransferBuffer = UsbFnMpSynopsysFreeTransferBuffer;
            KdNetUsbFnMpExports.ConfigureEnableEndpoints = UsbFnMpSynopsysConfigureEnableEndpoints;
            KdNetUsbFnMpExports.GetEndpointMaxPacketSize = UsbFnMpSynopsysGetEndpointMaxPacketSize;
            KdNetUsbFnMpExports.GetMaxTransferSize = UsbFnMpSynopsysGetMaxTransferSize;
            KdNetUsbFnMpExports.GetEndpointStallState = UsbFnMpSynopsysGetEndpointStallState;
            KdNetUsbFnMpExports.SetEndpointStallState = UsbFnMpSynopsysSetEndpointStallState;
            KdNetUsbFnMpExports.Transfer = UsbFnMpSynopsysTransfer;
            KdNetUsbFnMpExports.AbortTransfer = UsbFnMpSynopsysAbortTransfer;
            KdNetUsbFnMpExports.EventHandler = UsbFnMpSynopsysEventHandler;
            KdNetUsbFnMpExports.TimeDelay = UsbFnMpSynopsysTimeDelay;
            KdNetUsbFnMpExports.IsZLPSupported = UsbFnMpSynopsysIsZLPSupported;
            break;
        }

    Status = KdNetExtensibilityExports.KdInitializeController(KdNet);

KdInitializeControllerEnd:
    return Status;
}

//------------------------------------------------------------------------------

VOID
KdShutdownController(
    __inout PVOID Buffer
    )
{
    KdNetExtensibilityExports.KdShutdownController(Buffer);
}

//------------------------------------------------------------------------------

NTSTATUS
KdGetTxPacket(
    __inout PVOID Buffer,
    __out PULONG Handle
    )
{
    return KdNetExtensibilityExports.KdGetTxPacket(Buffer, Handle);
}

//------------------------------------------------------------------------------

NTSTATUS
KdSendTxPacket(
    __inout PVOID Buffer,
    ULONG Handle,
    ULONG Length
    )
{
    return KdNetExtensibilityExports.KdSendTxPacket(Buffer, Handle, Length);
}

//------------------------------------------------------------------------------

NTSTATUS
KdGetRxPacket(
    __inout PVOID Buffer,
    __out PULONG Handle,
    __out PVOID *Packet,
    __out PULONG Length
    )
{
    return KdNetExtensibilityExports.KdGetRxPacket(
        Buffer, Handle, Packet, Length
        );
}

//------------------------------------------------------------------------------

VOID
KdReleaseRxPacket(
    __inout PVOID Buffer,
    ULONG Handle
    )
{
    KdNetExtensibilityExports.KdReleaseRxPacket(Buffer, Handle);
}

//------------------------------------------------------------------------------

PVOID
KdGetPacketAddress (
    __inout PVOID Buffer,
    ULONG Handle
    )
{
    return KdNetExtensibilityExports.KdGetPacketAddress(Buffer, Handle);
}

//------------------------------------------------------------------------------

ULONG
KdGetPacketLength(
    __inout PVOID Buffer,
    ULONG Handle
    )
{
    return KdNetExtensibilityExports.KdGetPacketLength(Buffer, Handle);
}

//------------------------------------------------------------------------------

ULONG
KdGetHardwareContextSize (
    __in PDEBUG_DEVICE_DESCRIPTOR Device
    )

/*++

Routine Description:

    This function returns the required size of the hardware context in bytes.

Arguments:

    Device - Supplies a pointer to the debug device descriptor.

Return Value:

    None.

--*/

{
    return KdNetExtensibilityExports.KdGetHardwareContextSize(Device);
}

//------------------------------------------------------------------------------

NTSTATUS
KdInitializeLibrary (
    __in PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    __in_opt PSTR LoaderOptions,
    __inout PDEBUG_DEVICE_DESCRIPTOR Device
    )
{
    BOOLEAN Aligned;
    BOOLEAN Cached;
    ULONG Length;
    PKDNET_EXTENSIBILITY_EXPORTS Exports;
    NTSTATUS Status = STATUS_SUCCESS;


    __security_init_cookie();

    //
    // Check parameters
    //

    if ((ImportTable == NULL) ||
        (ImportTable->FunctionCount != KDNET_EXT_IMPORTS)) {
        Status = STATUS_INVALID_PARAMETER;
        goto KdInitializeLibraryEnd;
    }

    // Save import jump table
    KdNetExtensibilityImports = ImportTable;

    Exports = KdNetExtensibilityImports->Exports;
    if ((Exports == NULL) || (Exports->FunctionCount != KDNET_EXT_EXPORTS)) {
        Status = STATUS_INVALID_PARAMETER;
        goto KdInitializeLibraryEnd;
    }

    //
    // Return the function pointers this KDNET extensibility module exports.
    //

    Exports->KdInitializeController = KdInitializeController;
    Exports->KdShutdownController = KdShutdownController;
    Exports->KdSetHibernateRange = KdSetHibernateRange;
    Exports->KdGetRxPacket = KdGetRxPacket;
    Exports->KdReleaseRxPacket = KdReleaseRxPacket;
    Exports->KdGetTxPacket = KdGetTxPacket;
    Exports->KdSendTxPacket = KdSendTxPacket;
    Exports->KdGetPacketAddress = KdGetPacketAddress;
    Exports->KdGetPacketLength = KdGetPacketLength;
    Exports->KdGetHardwareContextSize = KdGetHardwareContextSize;
    Exports->DebugSerialOutputInit = DebugSerialOutputInit;
    Exports->DebugSerialOutputByte = DebugSerialOutputByte;

    //
    // Start with most optimistic memory requirements.
    //

    Length = Device->Memory.Length = 0;
    Cached = Device->Memory.Cached = TRUE;
    Aligned = Device->Memory.Aligned = FALSE;

    //
    // Call first sub-extension type with first controller.
    //

    KdNetUsbFnMpExports.InitializeLibrary = UsbFnMpChipideaInitializeLibrary;
    KdNetUsbFnMpExports.GetHardwareContextSize = UsbFnMpChipideaGetHardwareContextSize;
    Status = KdUsbFnInitializeLibrary(ImportTable, LoaderOptions, Device);
    if (NT_SUCCESS(Status)) {
        if (Device->Memory.Length > Length) {
            Length = Device->Memory.Length;
        }

        Cached &= Device->Memory.Cached;
        Aligned |= Device->Memory.Aligned;
    }

    //
    // Call first sub-extension type with second controller.
    //

    KdNetUsbFnMpExports.InitializeLibrary = UsbFnMpSynopsysInitializeLibrary;
    KdNetUsbFnMpExports.GetHardwareContextSize = UsbFnMpSynopsysGetHardwareContextSize;
    Status = KdUsbFnInitializeLibrary(ImportTable, LoaderOptions, Device);
    if (NT_SUCCESS(Status)) {
        if (Device->Memory.Length > Length) {
            Length = Device->Memory.Length;
        }

        Cached &= Device->Memory.Cached;
        Aligned |= Device->Memory.Aligned;
    }

    //
    // Update memory requirement.
    //

    Device->Memory.Length = Length;
    Device->Memory.Cached = Cached;
    Device->Memory.Aligned = Aligned;

    Status = STATUS_SUCCESS;

KdInitializeLibraryEnd:
    return Status;
}

//------------------------------------------------------------------------------
