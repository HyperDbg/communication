/*++

Copyright (c) Microsoft Corporation

Module Name:

    KdNetUsbFn.h

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
KdUsbFnInitializeLibrary (
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_opt_z_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device
    );

ULONG
KdUsbFnGetHardwareContextSize (
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device
    );

NTSTATUS
KdUsbFnInitializeController (
    _Inout_ PKDNET_SHARED_DATA KdNet
    );

VOID
KdUsbFnShutdownController (
    _Inout_ PVOID Buffer
    );

NTSTATUS
KdUsbFnGetTxPacket (
    _Inout_ PVOID Buffer,
    _Out_ PULONG Handle
    );

NTSTATUS
KdUsbFnSendTxPacket (
    _Inout_ PVOID Buffer,
    ULONG Handle,
    ULONG Length
    );

NTSTATUS
KdUsbFnGetRxPacket (
    _Inout_ PVOID Buffer,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length
    );

VOID
KdUsbFnReleaseRxPacket (
    _Inout_ PVOID Buffer,
    ULONG Handle
    );

PVOID
KdUsbFnGetPacketAddress (
    _Inout_ PVOID Buffer,
    ULONG Handle
    );

ULONG
KdUsbFnGetPacketLength(
    _Inout_ PVOID Buffer,
    ULONG Handle
    );

