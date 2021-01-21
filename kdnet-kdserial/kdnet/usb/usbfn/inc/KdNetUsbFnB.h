/*++

Copyright (c) Microsoft Corporation

Module Name:

    KdNetUsbFnB.h

Abstract:

    Kernel Debug Network Transport Extensibility For Usb Function
    Controllers Interface.

    It is in fact same API as generic Kernel Debug Network
    Extensibility API. It needs to be specified to workaround issue
    with build which makes difficult to export
Author:

    Karel Danihelka (kareld)

--*/

#pragma once

NTSTATUS
KdUsbFnBInitializeLibrary (
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_opt_z_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device
    );

ULONG
KdUsbFnBGetHardwareContextSize (
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device
    );

NTSTATUS
KdUsbFnBInitializeController (
    _Inout_ PKDNET_SHARED_DATA KdNet
    );

VOID
KdUsbFnBShutdownController (
    _Inout_ PVOID Buffer
    );

NTSTATUS
KdUsbFnBGetTxPacket (
    _Inout_ PVOID Buffer,
    _Out_ PULONG Handle
    );

NTSTATUS
KdUsbFnBSendTxPacket (
    _Inout_ PVOID Buffer,
    ULONG Handle,
    ULONG Length
    );

NTSTATUS
KdUsbFnBGetRxPacket (
    _Inout_ PVOID Buffer,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length
    );

VOID
KdUsbFnBReleaseRxPacket (
    _Inout_ PVOID Buffer,
    ULONG Handle
    );

PVOID
KdUsbFnBGetPacketAddress (
    _Inout_ PVOID Buffer,
    ULONG Handle
    );

ULONG
KdUsbFnBGetPacketLength(
    _Inout_ PVOID Buffer,
    ULONG Handle
    );

