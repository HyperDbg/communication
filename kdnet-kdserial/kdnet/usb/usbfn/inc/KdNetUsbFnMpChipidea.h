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
UsbFnMpChipideaInitializeLibrary(
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_ PULONG ContextLength,
    _Out_ PULONG DmaLength,
    _Out_ PULONG DmaAlignment);

VOID
UsbFnMpChipideaDeinitializeLibrary();

NTSTATUS
UsbFnMpChipideaGetMemoryRequirements(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_ PULONG ContextLength,
    _Out_ PULONG DmaLength,
    _Out_ PULONG DmaAlignment);

ULONG
UsbFnMpChipideaGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
UsbFnMpChipideaStartController(
    _In_ PVOID Miniport,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _In_ PVOID DmaBuffer);

NTSTATUS
UsbFnMpChipideaStopController(
    _In_ PVOID Miniport);
    
NTSTATUS
UsbFnMpChipideaAllocateTransferBuffer(
    _In_ PVOID Miniport,
    ULONG Size,
    _Out_ PVOID* TransferBuffer);

NTSTATUS
UsbFnMpChipideaFreeTransferBuffer(
    _In_ PVOID Miniport,
    _In_ PVOID TransferBuffer);

NTSTATUS
UsbFnMpChipideaConfigureEnableEndpoints(
    _In_ PVOID Miniport,
    _In_ PUSBFNMP_DEVICE_INFO DeviceInfo);

NTSTATUS
UsbFnMpChipideaGetEndpointMaxPacketSize(
    _In_ PVOID Miniport,
    UINT8 EndpointType,
    USB_DEVICE_SPEED BusSpeed,
    _Out_ PUINT16 MaxPacketSize);

NTSTATUS
UsbFnMpChipideaGetMaxTransferSize(
    _In_ PVOID Miniport,
    _Out_ PULONG MaxTransferSize);

NTSTATUS
UsbFnMpChipideaGetEndpointStallState(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    _Out_ PBOOLEAN Stalled);

NTSTATUS
UsbFnMpChipideaSetEndpointStallState(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    BOOLEAN StallEndPoint);

NTSTATUS
UsbFnMpChipideaTransfer(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    _Inout_ PULONG BufferSize,
    _Inout_ PVOID Buffer);

NTSTATUS
UsbFnMpChipideaAbortTransfer(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction);

NTSTATUS
UsbFnMpChipideaEventHandler(
    _In_ PVOID Miniport,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Inout_ PULONG PayloadSize,
    _Out_writes_to_(1, 0) PUSBFNMP_MESSAGE_PAYLOAD Payload);

NTSTATUS
UsbFnMpChipideaTimeDelay(
    _In_ PVOID Miniport,
    _Out_opt_ PULONG Delta,
    _Inout_updates_to_(0, 0) PULONG Base);

NTSTATUS
UsbFnMpChipideaIsZLPSupported(
    _In_ PVOID Miniport,
    _Out_ PBOOLEAN IsSupported);
