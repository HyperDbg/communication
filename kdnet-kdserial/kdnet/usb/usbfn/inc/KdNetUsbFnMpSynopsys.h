/*++

Copyright (c) Microsoft Corporation

Module Name:

    KdNetUsbFnMp.h

Abstract:

    Defines KDNet UsbFn Miniport Interface. This interface abstracts underlying
    USB Function controller hardware. Interface is designed to be consistent
    with UEFI Usb Function Protocol with goal to simplify its implementation.

Author:

    Karel Daniheka (kareld)

--*/

#pragma once

NTSTATUS
UsbFnMpSynopsysInitializeLibrary(
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_z_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_ PULONG ContextLength,
    _Out_ PULONG DmaLength,
    _Out_ PULONG DmaAlignment);

VOID
UsbFnMpSynopsysDeinitializeLibrary(
    VOID);

NTSTATUS
UsbFnMpSynopsysGetMemoryRequirements(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_ PULONG ContextLength,
    _Out_ PULONG DmaLength,
    _Out_ PULONG DmaAlignment);

ULONG
UsbFnMpSynopsysGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
UsbFnMpSynopsysStartController(
    _In_ PVOID Miniport,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _In_ PVOID DmaBuffer);

NTSTATUS
UsbFnMpSynopsysStopController(
    _In_ PVOID Miniport);
    
NTSTATUS
UsbFnMpSynopsysAllocateTransferBuffer(
    _In_ PVOID Miniport,
    ULONG Size,
    _Out_ PVOID* TransferBuffer);

NTSTATUS
UsbFnMpSynopsysFreeTransferBuffer(
    _In_ PVOID Miniport,
    _In_ PVOID TransferBuffer);

NTSTATUS
UsbFnMpSynopsysConfigureEnableEndpoints(
    _In_ PVOID Miniport,
    _In_ PUSBFNMP_DEVICE_INFO DeviceInfo);

NTSTATUS
UsbFnMpSynopsysGetEndpointMaxPacketSize(
    _In_ PVOID Miniport,
    UINT8 EndpointType,
    USB_DEVICE_SPEED BusSpeed,
    _Out_ PUINT16 MaxPacketSize);

NTSTATUS
UsbFnMpSynopsysGetMaxTransferSize(
    _In_ PVOID Miniport,
    _Out_ PULONG MaxTransferSize);

NTSTATUS
UsbFnMpSynopsysGetEndpointStallState(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    _Out_ PBOOLEAN Stalled);

NTSTATUS
UsbFnMpSynopsysSetEndpointStallState(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    BOOLEAN StallEndPoint);

NTSTATUS
UsbFnMpSynopsysTransfer(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    _Inout_ PULONG BufferSize,
    _Inout_ PVOID Buffer);

NTSTATUS
UsbFnMpSynopsysAbortTransfer(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction);

NTSTATUS
UsbFnMpSynopsysEventHandler(
    _In_ PVOID Miniport,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Inout_ PULONG PayloadSize,
    _Out_writes_to_(1, 0) PUSBFNMP_MESSAGE_PAYLOAD Payload);

NTSTATUS
UsbFnMpSynopsysTimeDelay(
    _In_ PVOID Miniport,
    _Out_opt_ PULONG Delta,
    _Inout_ PULONG Base);

NTSTATUS
UsbFnMpSynopsysIsZLPSupported(
    _In_ PVOID Miniport,
    _Out_ PBOOLEAN IsSupported);
