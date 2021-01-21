/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    kdundi.h

Abstract:

    Network Kernel Debug UNDI support libary header.

Author:

    Joe Ballantyne (joeball)

--*/

#define MAX_PKT_SIZE 2048

typedef struct _PKT_BUFF
{
    union
    {
        unsigned char   buffer [MAX_PKT_SIZE];
    } u;
} PKT_BUFF, *PPKT_BUFF;

typedef struct _UNDI_ADAPTER {
    PKT_BUFF UndiTxBuffer[1];
    PKT_BUFF UndiRxBuffer[1];
    ULONG UndiTxPacketLength;
    ULONG UndiRxPacketLength;
    PKDNET_SHARED_DATA KdNet;
    ULONG WaitForTxPacket;
} UNDI_ADAPTER, *PUNDI_ADAPTER;

//
// undi.c
//

NTSTATUS
UndiInitializeController(
    __in PKDNET_SHARED_DATA Adapter
    );

VOID
UndiShutdownController (
    __in PUNDI_ADAPTER Adapter
    );

NTSTATUS
UndiGetRxPacket (
    __in PUNDI_ADAPTER Adapter,
    __out PULONG Handle,
    __out PVOID *Packet,
    __out PULONG Length
);

VOID
UndiReleaseRxPacket (
    __in PUNDI_ADAPTER Adapter,
    ULONG Handle
);

NTSTATUS
UndiGetTxPacket (
    __in PUNDI_ADAPTER Adapter,
    __out PULONG Handle
);

NTSTATUS
UndiSendTxPacket (
    __in PUNDI_ADAPTER Adapter,
    ULONG Handle,
    ULONG Length
);

PVOID
UndiGetPacketAddress (
    __in PUNDI_ADAPTER Adapter,
    ULONG Handle
);

ULONG
UndiGetPacketLength (
    __in PUNDI_ADAPTER Adapter,
    ULONG Handle
);

