/*++

Copyright (c) Microsoft Corporation

Module Name:

    kdextension.c

Abstract:

    Network Kernel Debug Extensibility Support.  This module implements the
    extensibility entry points.

Author:

    Joe Ballantyne (joeball)

--*/

#include "pch.h"

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4127 4310)

PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

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

    return;
}

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

NTSTATUS
KdGetTxPacket (
    __in PVOID Adapter,
    __out PULONG Handle
)

/*++

Routine Description:

    This function acquires the hardware resources needed to send a packet and
    returns a handle to those resources.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

    Handle - Supplies a pointer to the handle for the packet for which hardware
        resources have been reserved.

Return Value:

    STATUS_SUCCESS when hardware resources have been successfully reserved.

    STATUS_IO_TIMEOUT if the hardware resources could not be reserved.

    STATUS_INVALID_PARAMETER if an invalid Handle pointer or Adapter is passed.

--*/

{
    return UndiGetTxPacket(Adapter, Handle);
}

NTSTATUS
KdSendTxPacket (
    __in PVOID Adapter,
    ULONG Handle,
    ULONG Length
)

/*++

Routine Description:

    This function sends the packet associated with the passed Handle out to the
    network.  It does not return until the packet has been sent.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

    Handle - Supplies the handle of the packet to send.

    Length - Supplies the length of the packet to send.

Return Value:

    STATUS_SUCCESS when a packet has been successfully sent.

    STATUS_IO_TIMEOUT if the packet could not be sent within 100ms.

    STATUS_INVALID_PARAMETER if an invalid Handle or Adapter is passed.

--*/

{
    return UndiSendTxPacket(Adapter, Handle, Length);
}

NTSTATUS
KdGetRxPacket (
    __in PVOID Adapter,
    __out PULONG Handle,
    __out PVOID *Packet,
    __out PULONG Length
)

/*++

Routine Description:

    This function returns the next available received packet to the caller.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

    Handle - Supplies a pointer to the handle for this packet.  This handle
        will be used to release the resources associated with this packet back
        to the hardware.

    Packet - Supplies a pointer that will be written with the address of the
        start of the packet.

    Length - Supplies a pointer that will be written with the length of the
        recieved packet.

Return Value:

    STATUS_SUCCESS when a packet has been received.

    STATUS_IO_TIMEOUT otherwise.

--*/

{
    return UndiGetRxPacket(Adapter, Handle, Packet, Length);
}

VOID
KdReleaseRxPacket (
    __in PVOID Adapter,
    ULONG Handle
)

/*++

Routine Description:

    This function reclaims the hardware resources used for the packet
    associated with the passed Handle.  It reprograms the hardware to use those
    resources to receive another packet.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

    Handle - Supplies the handle of the packet whose resources should be
        reclaimed to receive another packet.

Return Value:

    None.

--*/

{
    UndiReleaseRxPacket(Adapter, Handle);
}

PVOID
KdGetPacketAddress (
    __in PVOID Adapter,
    ULONG Handle
)

/*++

Routine Description:

    This function returns a pointer to the first byte of a packet associated
    with the passed handle.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

    Handle - Supplies a handle to the packet for which to return the
        starting address.

Return Value:

    Pointer to the first byte of the packet.

--*/

{
    return UndiGetPacketAddress(Adapter, Handle);
}

ULONG
KdGetPacketLength (
    __in PVOID Adapter,
    ULONG Handle
)

/*++

Routine Description:

    This function returns the length of the packet associated with the passed
    handle.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

    Handle - Supplies a handle to the packet for which to return the
        length.

Return Value:

    The length of the packet.

--*/

{
    return UndiGetPacketLength(Adapter, Handle);
}

NTSTATUS
KdInitializeController(
    __in PVOID Adapter
    )

/*++

Routine Description:

    This function initializes the Network controller.  The controller is setup
    to send and recieve packets at the fastest rate supported by the hardware
    link.  Packet send and receive will be functional on successful exit of
    this routine.  The controller will be initialized with Interrupts masked
    since all debug devices must operate without interrupt support.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

Return Value:

    STATUS_SUCCESS on successful initialization.  Appropriate failure code if
    initialization fails.

--*/

{
    return IntelInitializeController(Adapter);
}

VOID
KdShutdownController (
    __in PVOID Adapter
    )

/*++

Routine Description:

    This function shuts down the Network controller.  No further packets can
    be sent or received until the controller is reinitialized.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

Return Value:

    None.

--*/

{
    UndiShutdownController(Adapter);
}

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
    return IntelGetHardwareContextSize(Device);
}

NTSTATUS
KdInitializeLibrary (
    __in PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    __in_opt PCHAR LoaderOptions,
    __inout PDEBUG_DEVICE_DESCRIPTOR Device
    )
{
    NTSTATUS Status;
    PKDNET_EXTENSIBILITY_EXPORTS Exports;

    __security_init_cookie();
    Status = STATUS_SUCCESS;
    KdNetExtensibilityImports = ImportTable;
    if ((KdNetExtensibilityImports == NULL) ||
        (KdNetExtensibilityImports->FunctionCount != KDNET_EXT_IMPORTS)) {

        Status = STATUS_INVALID_PARAMETER;
        goto KdInitializeLibraryEnd;
    }

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

#if TRACE_DBG_PRINT

    InitializeLogTiming();
    TRACE_ENABLE();

#endif

    //
    // Return the hardware context size required to support this device.
    //

    Status = IntelInitializeLibrary(LoaderOptions, Device);

KdInitializeLibraryEnd:
    return Status;
}

#pragma warning(default:4127 4310)
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

