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

#include <usbspec.h>

typedef struct _USBFNMP_INTERFACE_INFO {
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PUSB_ENDPOINT_DESCRIPTOR *EndpointDescriptorTable;
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR *EndpointCompanionDescriptor;
} USBFNMP_INTERFACE_INFO, *PUSBFNMP_INTERFACE_INFO;

typedef struct _USBFNMP_CONFIGURATION_INFO {
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    PUSBFNMP_INTERFACE_INFO *InterfaceInfoTable;
} USBFNMP_CONFIGURATION_INFO, *PUSBFNMP_CONFIGURATION_INFO;

typedef struct _USBFNMP_DEVICE_INFO {
    PUSB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSBFNMP_CONFIGURATION_INFO *ConfigurationInfoTable;
} USBFNMP_DEVICE_INFO, *PUSBFNMP_DEVICE_INFO;

typedef enum _USBFNMP_ENDPOINT_DIRECTION {
    UsbEndpointDirectionHostOut = 0,
    UsbEndpointDirectionHostIn,
    UsbEndpointDirectionDeviceTx = UsbEndpointDirectionHostIn,
    UsbEndpointDirectionDeviceRx = UsbEndpointDirectionHostOut
} USBFNMP_ENDPOINT_DIRECTION;

typedef enum _USBFNMP_MESSAGE {
    UsbMsgNone = 0,
    UsbMsgSetupPacket,
    UsbMsgEndpointStatusChangedRx,
    UsbMsgEndpointStatusChangedTx,
    UsbMsgBusEventReset,
    UsbMsgBusEventDetach,
    UsbMsgBusEventAttach,
    UsbMsgBusEventSuspend,
    UsbMsgBusEventResume,
    UsbMsgBusEventSpeed,
    UsbMsgBusEventLinkDown,
    UsbMsgBusEventLinkUp
} USBFNMP_MESSAGE, *PUSBFNMP_MESSAGE;

typedef enum _USBFNMP_TRANSFER_STATUS {
    UsbTransferStatusUnknown = 0,
    UsbTransferStatusComplete,
    UsbTransferStatusAborted,
    UsbTransferStatusActive,
    UsbTransferStatusNone
} USBFNMP_TRANSFER_STATUS, *PUSBFNMP_TRANSFER_STATUS;

typedef struct _USBFNMP_TRANSFER_RESULT {
    ULONG BytesTransferred;
    USBFNMP_TRANSFER_STATUS TransferStatus;
    UINT8 EndpointIndex;
    USBFNMP_ENDPOINT_DIRECTION Direction;
    PVOID Buffer;
    ULONG NumberOfTransfers;
} USBFNMP_TRANSFER_RESULT, *PUSBFNMP_TRANSFER_RESULT;

typedef enum _USBFNMP_BUS_SPEED {
    UsbBusSpeedUnknown = 0,
    UsbBusSpeedLow,
    UsbBusSpeedFull,
    UsbBusSpeedHigh,
    UsbBusSpeedSuper,
    UsbBusSpeedMaximum = UsbBusSpeedSuper
} USBFNMP_BUS_SPEED, *PUSBFNMP_BUS_SPEED;

typedef union _USBFNMP_MESSAGE_PAYLOAD {
    USB_DEFAULT_PIPE_SETUP_PACKET udr;
    USBFNMP_TRANSFER_RESULT utr;
    USBFNMP_BUS_SPEED ubs;
} USBFNMP_MESSAGE_PAYLOAD, *PUSBFNMP_MESSAGE_PAYLOAD;

//------------------------------------------------------------------------------

typedef
NTSTATUS
(*PFN_USBFNMP_INITIALIZE_LIBRARY)(
    _In_ PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    _In_opt_ PCHAR LoaderOptions,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_ PULONG ContextLength,
    _Out_ PULONG DmaLength,
    _Out_ PULONG DmaAlignment);

typedef
VOID
(*PFN_USBFNMP_DEINITIALIZE_LIBRARY)();

typedef
ULONG
(*PFN_USBFNMP_GET_HARDWARE_CONTEXT_SIZE)(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

typedef
NTSTATUS
(*PFN_USBFNMP_START_CONTROLLER)(
    _In_ PVOID Miniport,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _In_ PVOID DmaBuffer);

typedef
NTSTATUS
(*PFN_USBFNMP_STOP_CONTROLLER)(
    _In_ PVOID Miniport);

typedef    
NTSTATUS
(*PFN_USBFNMP_ALLOCATE_TRANSFER_BUFFER)(
    _In_ PVOID Miniport,
    ULONG Size,
    _Out_ PVOID* TransferBuffer);

typedef
NTSTATUS
(*PFN_USBFNMP_FREE_TRANSFER_BUFFER)(
    _In_ PVOID Miniport,
    _In_ PVOID TransferBuffer);

typedef
NTSTATUS
(*PFN_USBFNMP_CONFIGURE_ENABLE_ENDPOINTS)(
    _In_ PVOID Miniport,
    _In_ PUSBFNMP_DEVICE_INFO DeviceInfo);

typedef
NTSTATUS
(*PFN_USBFNMP_GET_ENDPOINT_MAX_PACKET_SIZE)(
    _In_ PVOID Miniport,
    UINT8 EndpointType,
    USB_DEVICE_SPEED BusSpeed,
    _Out_ PUINT16 MaxPacketSize);

typedef
NTSTATUS
(*PFN_USBFNMP_GET_MAX_TRANSFER_SIZE)(
    _In_ PVOID Miniport,
    _Out_ PULONG MaxTransferSize);

typedef
NTSTATUS
(*PFN_USBFNMP_GET_ENDPOINT_STALL_STATE)(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    _Out_ PBOOLEAN Stalled);

typedef
NTSTATUS
(*PFN_USBFNMP_SET_ENDPOINT_STALL_STATE)(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    BOOLEAN StallEndPoint);

typedef
NTSTATUS
(*PFN_USBFNMP_TRANSFER)(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    _Inout_ PULONG BufferSize,
    _Inout_ PVOID Buffer);

typedef
NTSTATUS
(*PFN_USBFNMP_ABORT_TRANSFER)(
    _In_ PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction);

typedef
NTSTATUS
(*PFN_USBFNMP_EVENT_HANDLER)(
    _In_ PVOID Miniport,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Inout_ PULONG PayloadSize,
    _Out_ PUSBFNMP_MESSAGE_PAYLOAD Payload);

typedef
NTSTATUS
(*PFN_USBFNMP_TIME_DELAY)(
    _In_ PVOID Miniport,
    _Out_opt_ PULONG Delta,
    _Out_ PULONG Base);

typedef
NTSTATUS
(*PFN_USBFNMP_IS_ZERO_LENGTH_PACKET)(
    _In_ PVOID Miniport,
    _Out_ PBOOLEAN IsZLPSupported);

//------------------------------------------------------------------------------

typedef struct _KDNET_USBFNMP_EXPORTS {
    PFN_USBFNMP_INITIALIZE_LIBRARY InitializeLibrary;
    PFN_USBFNMP_DEINITIALIZE_LIBRARY DeinitializeLibrary;
    PFN_USBFNMP_GET_HARDWARE_CONTEXT_SIZE GetHardwareContextSize;
    PFN_USBFNMP_START_CONTROLLER StartController;
    PFN_USBFNMP_STOP_CONTROLLER StopController;
    PFN_USBFNMP_ALLOCATE_TRANSFER_BUFFER AllocateTransferBuffer;
    PFN_USBFNMP_FREE_TRANSFER_BUFFER FreeTransferBuffer;
    PFN_USBFNMP_CONFIGURE_ENABLE_ENDPOINTS ConfigureEnableEndpoints;
    PFN_USBFNMP_GET_ENDPOINT_MAX_PACKET_SIZE GetEndpointMaxPacketSize;
    PFN_USBFNMP_GET_MAX_TRANSFER_SIZE GetMaxTransferSize;
    PFN_USBFNMP_GET_ENDPOINT_STALL_STATE GetEndpointStallState;
    PFN_USBFNMP_SET_ENDPOINT_STALL_STATE SetEndpointStallState;
    PFN_USBFNMP_TRANSFER Transfer;
    PFN_USBFNMP_ABORT_TRANSFER AbortTransfer;
    PFN_USBFNMP_EVENT_HANDLER EventHandler;
    PFN_USBFNMP_TIME_DELAY TimeDelay;
    PFN_USBFNMP_IS_ZERO_LENGTH_PACKET IsZLPSupported;
} KDNET_USBFNMP_EXPORTS, *PKDNET_USBFNMP_EXPORTS;

#define UsbFnMpInitializeLibrary KdNetUsbFnMpExports.InitializeLibrary
#define UsbFnMpDeinitializeLibrary KdNetUsbFnMpExports.DeinitializeLibrary
#define UsbFnMpGetHardwareContextSize KdNetUsbFnMpExports.GetHardwareContextSize
#define UsbFnMpStartController KdNetUsbFnMpExports.StartController
#define UsbFnMpStopController KdNetUsbFnMpExports.StopController
#define UsbFnMpAllocateTransferBuffer KdNetUsbFnMpExports.AllocateTransferBuffer
#define UsbFnMpFreeTransferBuffer KdNetUsbFnMpExports.FreeTransferBuffer
#define UsbFnMpConfigureEnableEndpoints KdNetUsbFnMpExports.ConfigureEnableEndpoints
#define UsbFnMpGetEndpointMaxPacketSize KdNetUsbFnMpExports.GetEndpointMaxPacketSize
#define UsbFnMpGetMaxTransferSize KdNetUsbFnMpExports.GetMaxTransferSize
#define UsbFnMpGetEndpointStallState KdNetUsbFnMpExports.GetEndpointStallState
#define UsbFnMpSetEndpointStallState KdNetUsbFnMpExports.SetEndpointStallState
#define UsbFnMpTransfer KdNetUsbFnMpExports.Transfer
#define UsbFnMpAbortTransfer KdNetUsbFnMpExports.AbortTransfer
#define UsbFnMpEventHandler KdNetUsbFnMpExports.EventHandler
#define UsbFnMpTimeDelay KdNetUsbFnMpExports.TimeDelay
#define UsbFnMpIsZeroLengthPacketSupported KdNetUsbFnMpExports.IsZLPSupported

//------------------------------------------------------------------------------

extern KDNET_USBFNMP_EXPORTS KdNetUsbFnMpExports;

//------------------------------------------------------------------------------

