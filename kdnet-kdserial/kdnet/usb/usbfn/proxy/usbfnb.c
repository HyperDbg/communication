/*++

Copyright (c) Microsoft Corporation

Module Name:

    kdnetusbfnb.c

Abstract:

    Network Kernel Debugger Extension Buffered Packet Proxy. This code
    is proxy for KdUsbFn extension buffering send and received packets
    in local cached memory. It is intended to be used on platforms
    which doesn't support unaligned access to strongly ordered memory.

--*/

#include <ntddk.h>
#include <process.h>
#include <kdnetshareddata.h>
#include <kdnetextensibility.h>
#include <kdnetusbfnb.h>
#include <kdnetusbfn.h>
#include <logger.h>
#include <transferqueue.h>

//------------------------------------------------------------------------------

#define INVALID_HANDLE      (ULONG)-1

#define TX_HANDLE           0x00000001
#define RX_HANDLE           0x00000101

#define BUFFER_OFFSET       2
#define BUFFER_SIZE         2048

ULONG
TxHandle;

ULONG
RxHandle;

UCHAR
TxBuffer[BUFFER_SIZE];

#define TX_COOKIE    'TxBg'

ULONG
TxCookie = TX_COOKIE;

UCHAR
RxBuffer[BUFFER_SIZE];

#define RX_COOKIE    'RxBg'

ULONG
RxCookie = RX_COOKIE;

#ifdef PROXY_QUEUE
TransferQueue RxTransfer;

TransferQueue TxTransfer;
#endif

//------------------------------------------------------------------------------

#define ASSERTBG(x, y)     if (!(x))                                           \
    KeBugCheckEx(THREAD_STUCK_IN_DEVICE_DRIVER, 2, y, 0, 0)

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE
void InitTransferQueue(_Out_ PTransferQueue Queue, _In_ ULONG Size)
{

    NT_ASSERT((Queue != NULL) && (Size != 0) && (Size <= MAX_NUMBER_OF_TRANSFERS));

    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->Size = Size;
    return;
}

//------------------------------------------------------------------------------

void EnqueueTransfer(_Inout_ PTransferQueue Queue)
{

    NT_ASSERT(Queue != NULL);

    Queue->Packet[Queue->Tail].Status.IsTransferUsed = 1;
    Queue->Tail = (Queue->Tail + 1) % Queue->Size;
    return;
}

//------------------------------------------------------------------------------

void DequeueTransfer(_Inout_ PTransferQueue Queue)
{

    NT_ASSERT(Queue != NULL);

    Queue->Packet[Queue->Head].Status.IsTransferUsed = 0;
    Queue->Head = (Queue->Head + 1) % Queue->Size;
    return;
}

//------------------------------------------------------------------------------

BOOLEAN IsPacketTransferFull(_In_ PTransferQueue Queue)
{

    NT_ASSERT(Queue != NULL);

    ULONG Index;

    Index = (Queue->Tail + 1) % Queue->Size;
    return (Queue->Head == Index);
}

#endif

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
KdUsbFnBInitializeLibrary (
    PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    PCHAR LoaderOptions,
    PDEBUG_DEVICE_DESCRIPTOR Device
    )
{
    return KdUsbFnInitializeLibrary(ImportTable, LoaderOptions, Device);
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
ULONG
KdUsbFnBGetHardwareContextSize (
    PDEBUG_DEVICE_DESCRIPTOR Device
    )
{
    return KdUsbFnGetHardwareContextSize(Device);
}

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
NTSTATUS
KdUsbFnBInitializeController (
    PKDNET_SHARED_DATA KdNet
    )
{

    InitTransferQueue(&RxTransfer, MAX_NUMBER_OF_TRANSFERS);
    InitTransferQueue(&TxTransfer, MAX_NUMBER_OF_TRANSFERS);
    return KdUsbFnInitializeController(KdNet);
}

#else


_Use_decl_annotations_
NTSTATUS
KdUsbFnBInitializeController (
    PKDNET_SHARED_DATA KdNet
    )
{
    TxHandle = INVALID_HANDLE;
    RxHandle = INVALID_HANDLE;
    return KdUsbFnInitializeController(KdNet);
}

#endif

//------------------------------------------------------------------------------

_Use_decl_annotations_
VOID
KdUsbFnBShutdownController (
    PVOID Buffer
    )
{
    KdUsbFnShutdownController(Buffer);
}

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
NTSTATUS
KdUsbFnBGetTxPacket (
    PVOID Buffer,
    PULONG Handle
    )
{

    NTSTATUS Status;
    ULONG Index;

    Status = STATUS_IO_TIMEOUT;
    if (IsPacketTransferFull(&TxTransfer)) {

        LOG_TRACE("The TransferTx Queue is full");

        KdNetErrorString = L"Unable to get a Tx packet.";
        goto KdUsbFnBGetTxPacketEnd;
    }

    Index = GetTransferQueueTail(&TxTransfer);
    Status = KdUsbFnGetTxPacket(Buffer, &TxTransfer.Packet[Index].Handle);
    if (NT_SUCCESS(Status)) {
        *Handle = Index | TRANSMIT_HANDLE;
        EnqueueTransfer(&TxTransfer);
    }

KdUsbFnBGetTxPacketEnd:
    return Status;
}

#else

_Use_decl_annotations_
NTSTATUS
KdUsbFnBGetTxPacket (
    PVOID Buffer,
    PULONG Handle
    )
{
    NTSTATUS Status;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 1);
    ASSERTBG(TxHandle == INVALID_HANDLE, 2);

    // Get packet from normal extension
    Status = KdUsbFnGetTxPacket(Buffer, &TxHandle);
    if (!NT_SUCCESS(Status)) goto KdUsbFnBGetTxPacketEnd;

    *Handle = TX_HANDLE;

KdUsbFnBGetTxPacketEnd:
    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 3);
    return Status;
}

#endif

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
NTSTATUS
KdUsbFnBSendTxPacket (
    PVOID Buffer,
    ULONG Handle,
    ULONG Length
    )
{

    NTSTATUS Status;
    PVOID Packet;
    ULONG Index;

    // Strip extra flags
    Index = Handle & ~HANDLE_FLAGS;
    TxTransfer.Packet[Index].Handle |= (Handle & HANDLE_FLAGS);
    Packet = KdUsbFnGetPacketAddress(Buffer, TxTransfer.Packet[Index].Handle);
    RtlCopyMemory(Packet, &TxTransfer.Packet[Index].Buffer[0], Length);
    Status = KdUsbFnSendTxPacket(Buffer, TxTransfer.Packet[Index].Handle, Length);
    DequeueTransfer(&TxTransfer);
    return Status;
}

#else

_Use_decl_annotations_
NTSTATUS
KdUsbFnBSendTxPacket (
    PVOID Buffer,
    ULONG Handle,
    ULONG Length
    )
{
    NTSTATUS Status;
    PVOID Packet;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 4);

    // Strip extra flags
    Handle &= ~HANDLE_FLAGS;

    ASSERTBG((Handle == TX_HANDLE) && (TxHandle != INVALID_HANDLE), 5);

    ASSERTBG(Length <= (sizeof(TxBuffer) - BUFFER_OFFSET), 6);
    if (Length > (sizeof(TxBuffer) - BUFFER_OFFSET)) {
        Length = sizeof(TxBuffer) - BUFFER_OFFSET;
    }        

    Packet = KdUsbFnGetPacketAddress(Buffer, TxHandle);
    RtlCopyMemory(Packet, &TxBuffer[BUFFER_OFFSET], Length);
    Status = KdUsbFnSendTxPacket(Buffer, TxHandle, Length);

    TxHandle = INVALID_HANDLE;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 7);
    return Status;
}

#endif

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
NTSTATUS
KdUsbFnBGetRxPacket (
    PVOID Buffer,
    PULONG Handle,
    PVOID *Packet,
    PULONG Length
    )
{

    NTSTATUS Status;
    ULONG Index;

    Status = STATUS_IO_TIMEOUT;
    if (IsPacketTransferFull(&RxTransfer)) {
        KdNetErrorString = L"Unable to receive more Rx Transfers.";
        goto KdUsbFnBGetRxPacketEnd;
    }

    Index = GetTransferQueueTail(&RxTransfer);
    Status = KdUsbFnGetRxPacket(Buffer, &RxTransfer.Packet[Index].Handle, Packet, Length);
    if (NT_SUCCESS(Status)) {
        RtlCopyMemory(&RxTransfer.Packet[Index].Buffer[0], *Packet, *Length);
        *Handle = Index;
        *Packet = &RxTransfer.Packet[Index].Buffer[0];
        EnqueueTransfer(&RxTransfer);
    }

KdUsbFnBGetRxPacketEnd:
    return Status;
}

#else

_Use_decl_annotations_
NTSTATUS
KdUsbFnBGetRxPacket (
    PVOID Buffer,
    PULONG Handle,
    PVOID *Packet,
    PULONG Length
    )
{
    NTSTATUS Status;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 8);
    ASSERTBG(RxHandle == INVALID_HANDLE, 9);

    Status = KdUsbFnGetRxPacket(Buffer, &RxHandle, Packet, Length);
    if (NT_SUCCESS(Status))
        {
        RtlCopyMemory(&RxBuffer[BUFFER_OFFSET], *Packet, *Length);
        *Handle = RX_HANDLE;
        *Packet = &RxBuffer[BUFFER_OFFSET];
        }

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 10);
    return Status;
}

#endif


//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
VOID
KdUsbFnBReleaseRxPacket (
    PVOID Buffer,
    ULONG Handle
    )
{
    if (IsTransferQueueEmpty(&RxTransfer)) {
        
        NT_ASSERT(FALSE);
        
        //
        //  There is no any packet to release
        //
        
        KdNetErrorString = L"Unable to release a transfer (Queue is empty).";

        LOG_TRACE("Unable to release a transfer (Queue is empty).");

        goto KdUsbFnBReleaseRxPacketEnd;
    }

    if (IsHeadTransferQueue(&RxTransfer, Handle)) {
        KdNetErrorString = L"This transfer has been already released";

        LOG_TRACE("This transfer has been already released");

        goto KdUsbFnBReleaseRxPacketEnd;
    }

    KdUsbFnReleaseRxPacket(Buffer, RxTransfer.Packet[Handle].Handle);
    DequeueTransfer(&RxTransfer);

KdUsbFnBReleaseRxPacketEnd:
    return;
}

#else

_Use_decl_annotations_
VOID
KdUsbFnBReleaseRxPacket (
    PVOID Buffer,
    ULONG Handle
    )
{
    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 11);
    ASSERTBG((Handle == RX_HANDLE) && (RxHandle != INVALID_HANDLE), 12);

    KdUsbFnReleaseRxPacket(Buffer, RxHandle);
    RxHandle = INVALID_HANDLE;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 13);
}

#endif

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
PVOID
KdUsbFnBGetPacketAddress (
    PVOID Buffer,
    ULONG Handle
    )
{

    UNREFERENCED_PARAMETER(Buffer);

    PVOID Address;
    ULONG Index;

    Index = Handle & ~HANDLE_FLAGS;
    if ((Handle & TRANSMIT_HANDLE) != 0) {

        NT_ASSERT(!IsTransferQueueEmpty(&TxTransfer));

        Address = &TxTransfer.Packet[Index].Buffer[0];

    } else {

        NT_ASSERT(!IsTransferQueueEmpty(&RxTransfer));

        Address = &RxTransfer.Packet[Index].Buffer[0];
    }

    return Address;
}

#else

_Use_decl_annotations_
PVOID
KdUsbFnBGetPacketAddress (
    PVOID Buffer,
    ULONG Handle
    )
{
    PVOID Address;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 14);

    UNREFERENCED_PARAMETER(Buffer);

    // Strip extra flags
    Handle &= ~HANDLE_FLAGS;

    // Buffer address varies by handle
    if (Handle == TX_HANDLE) {
        Address = &TxBuffer[BUFFER_OFFSET];
    }
    else if (Handle == RX_HANDLE) {
        Address = &RxBuffer[BUFFER_OFFSET];
    }
    else {
        Address = NULL;
    }

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 15);
    return Address;
}

#endif

//------------------------------------------------------------------------------

#ifdef PROXY_QUEUE

_Use_decl_annotations_
ULONG
KdUsbFnBGetPacketLength(
    PVOID Buffer,
    ULONG Handle
    )
{

    ULONG Length;
    ULONG Index;
    
    Index = Handle & ~HANDLE_FLAGS;
    if ((Handle & TRANSMIT_HANDLE) != 0) {
        Length = KdUsbFnGetPacketLength(Buffer, TxTransfer.Packet[Index].Handle);

    } else {
        Length = KdUsbFnGetPacketLength(Buffer, RxTransfer.Packet[Index].Handle);
    }

    return Length;
}

#else

_Use_decl_annotations_
ULONG
KdUsbFnBGetPacketLength(
    PVOID Buffer,
    ULONG Handle
    )
{
    ULONG Length;
    
    Length = 0;

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 16);

    // Strip extra flags
    Handle &= ~HANDLE_FLAGS;

    // Forward request with real handle
    if (Handle == TX_HANDLE) {
        Length = KdUsbFnGetPacketLength(Buffer, TxHandle);
    }
    else if (Handle == RX_HANDLE) {
        Length = KdUsbFnGetPacketLength(Buffer, RxHandle);
    }

    ASSERTBG((TxCookie != TX_COOKIE) || (RxCookie != TX_COOKIE), 17);
    return Length;
}

#endif

//------------------------------------------------------------------------------
