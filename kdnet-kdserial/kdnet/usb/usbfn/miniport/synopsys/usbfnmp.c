/*++

Copyright (c) Microsoft Corporation

Module Name:

    UsbFnMp.c

Abstract:

    KdNet UsbFn Miniport implementation for Synopsys Ufx controller

Author:

    Karel Daniheka (kareld)
    Todd Sharpe (toshar)
    Alexis Suarez (alsuarez)

--*/

#include "pch.h"
#include <KdNetUsbFnMpSynopsys.h>
#include <logger.h>
#include <stdlib.h>

//
//------------------------------------------------------------------------------
//

#define DMA_BUFFER_SIZE         0x4000
#define DMA_BUFFERS             28
#define DMA_BUFFER_OFFSET       0

#define ENDPOINTS               8
#define TRB_PER_ENDPOINT        255

#define OEM_BUFFER_SIZE         0x0100

//
//------------------------------------------------------------------------------
//

//MiniPort Specific Loadoptions
#define SYNOPSYS_ENDPOINT_DELAY_OPTION    "SYNOPSYS_ENDPOINT_DELAY"
#define SYNOPSYS_ENDPOINT_DELAY_MIN 20
#define SYNOPSYS_ENDPOINT_DELAY_MAX 2000

//
//------------------------------------------------------------------------------
//

#define INREG32(x)              READ_REGISTER_ULONG((PULONG)x)
#define OUTREG32(x, y)          WRITE_REGISTER_ULONG((PULONG)x, y)
#define SETREG32(x, y)          OUTREG32(x, INREG32(x)|(y))
#define CLRREG32(x, y)          OUTREG32(x, INREG32(x)&~(y))
#define MASKREG32(x, y, z)      OUTREG32(x, (INREG32(x)&~(y))|(z))

//
//------------------------------------------------------------------------------
//

#define INVALID_INDEX           (ULONG)-1

#ifndef _countof
#define _countof(x)              (sizeof(x)/sizeof(x[0]))
#endif

//------------------------------------------------------------------------------

#pragma pack(push, 1)

#define MAX_OEM_WRITE_ENTRIES               16
#define OEM_SIGNATURE                       'FIX2'
#define SET_SEL_FIXED_LENGTH                6
#define MAX_EXIT_LATENCY_VALUE              125
#define MAX_SET_SEL_LENGTH                  (SET_SEL_FIXED_LENGTH + 2)

typedef struct {
    UINT16 PortType;
    UINT16 Reserved;
    UINT32 Signature;
    UINT32 WriteCount;
    struct {
        UINT8 BaseAddressRegister;
        UINT8 Phase;
        UINT16 Reserved;
        UINT32 Offset;
        UINT32 AndValue;
        UINT32 OrValue;
    } Data[MAX_OEM_WRITE_ENTRIES];
} ACPI_DEBUG_DEVICE_OEM_DATA, *PACPI_DEBUG_DEVICE_OEM_DATA;

#pragma pack(pop)

//------------------------------------------------------------------------------

//Intel specific
#define PHY_SOFT_RESET_MASK 0x80000000//Baytrail (BYT) specific reset flag for PCI
#define VID_INTEL 0x8086
#define DEVID_BAYTRAIL 0x0F37
#define DEVID_CHERRYTRAIL 0x22B7

//
//  Helper to determine the current device speed
//

#define IS_SS_BUS(_BusSpeed) (((_BusSpeed == UsbBusSpeedSuper)) ? (TRUE) : (FALSE))
#define IS_HS_BUS(_BusSpeed) (((_BusSpeed == UsbBusSpeedHigh)) ? (TRUE) : (FALSE))

//
//  TRB queue related macros
//

//
// Number of TRB used for data transfers
//

#define MAX_NUMBER_OF_TRBS        (TRB_PER_ENDPOINT)

typedef struct {
    PTRB  PtrTrb;
    ULONG BufferSize;
    ULONG BufferIndex;
    ULONG EndpointPos;
    struct {
        ULONG IsLastTrb: 1;  // Trb is marked as last
        ULONG IsTrbUsed: 1;  // Trb has not been released yet.
        ULONG IsLinked: 1;   // Trb is linked to the buffer endpoint list
        ULONG IsNoCompletion: 1; // Trb does not need completion
        ULONG UnUsed: 28;
    } Status;

} TRB_DATA, *PTRB_DATA;

typedef struct _TransferQueue {
	TRB_DATA TrbData[MAX_NUMBER_OF_TRBS + 1];
	ULONG Head;
	ULONG Tail;
    ULONG Size;
} TransferQueue, *PTransferQueue;

#define TransferQueueSize(Queue)              ((Queue)->Size)
#define IsHeadTransferQueue(Queue, Index)     ((Queue)->Head == (Index))
#define IsTransferQueueEmpty(Queue)           ((Queue)->Head == (Queue)->Tail)
#define GetQueueTail(Queue)                   ((Queue)->Tail)
#define GetQueueHead(Queue)                   ((Queue)->Head)
#define EmptyQueue(Queue)                     ((Queue)->Head = (Queue)->Tail)

//
//  This is the maximum time to wait for the xferNotReady event to arrive
//  before submitting a Setup Status Stage packet (TRB with NO_DATA).
//  This is a timeout to prevent getting stuck inside of the ongoing setup
//  phase waiting for the xferNotReady event(avoid looping forever), 
//

#define XFER_NOT_READY_TIMEOUT          4000            // 4ms
#define TIMEOUT_STALL                   100             // 100 usec

//
//------------------------------------------------------------------------------
//

typedef struct _DMA_MEMORY {

    //
    // Event Buffer - expected to begin on a 4K boundary
    //

    DECLSPEC_ALIGN(4096)
    GLOBAL_EVENT EventBuffer[1024];

    DECLSPEC_ALIGN(4096)
    TRB Trb[ENDPOINTS][TRB_PER_ENDPOINT + 1];

    DECLSPEC_ALIGN(4096)
    UCHAR Buffers[DMA_BUFFERS][DMA_BUFFER_SIZE];

    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
} DMA_MEMORY, *PDMA_MEMORY;

typedef struct _USBFNMP_CONTEXT {
    PCSR_REGISTERS UsbCtrl;
    PVOID Gpio;

    PDMA_MEMORY Dma;
    PHYSICAL_ADDRESS DmaPa;

    ULONG EventIndex;
    BOOLEAN SetupActive;
    ULONG StalledMask;

    ULONG TrbInIndex[ENDPOINTS];
    ULONG TrbOutIndex[ENDPOINTS];
    ULONG TrbFree[ENDPOINTS];

    UCHAR XferRscIdx[ENDPOINTS];

    BOOLEAN Attached;
    BOOLEAN Configured;
    BOOLEAN PendingDetach;
    BOOLEAN PendingReset;

    USBFNMP_BUS_SPEED Speed;

    ULONG FirstBuffer[ENDPOINTS];
    ULONG LastBuffer[ENDPOINTS];
    ULONG NextBuffer[DMA_BUFFERS];
    ULONG BufferSize[DMA_BUFFERS];
    ULONG FreeBuffer;

    //Controller
    USHORT VendorID;
    USHORT DeviceID;
    KD_NAMESPACE_ENUM NameSpace;
    BOOLEAN IsZLPSupported;

    //
    //  Transfer TRB Queue
    //

    TRB_DATA TrbElement;
    TransferQueue Transfer[ENDPOINTS];

    //
    //  USB 3.0 Set_SEL descriptor request.
    //  These fields describe the SET_SEL descriptor request (Dir: Host->Device 9.4.12 USB 3.0)
    //  The Host wants to set the system exit latency between the Host->Dev & Dev->Host, 
    //  and the device should accept these parameters by programming the USB core with those parameters.
    //

    UINT32 SysExitLatencyLength;

    //
    //  Make the length up to 8 bytes per USB 3.0 spec.
    //

    UCHAR SysExitLatencyParams[MAX_SET_SEL_LENGTH];
    BOOLEAN IsInProgressDataPhase;
    PENDPOINT_EVENT_TYPE DeviceEventType;
} USBFNMP_CONTEXT, *PUSBFNMP_CONTEXT;

NTSTATUS WaitForDeviceEventOnEP(
    _In_ PUSBFNMP_CONTEXT Context,
    _In_ ULONG Event);

//
//------------------------------------------------------------------------------
//

UCHAR
gOemData[OEM_BUFFER_SIZE];

ULONG
gOemDataLength;

ULONG
gEndpointWriteDelay = 100;	// 100 usec

//
//------------------------------------------------------------------------------
//

BOOLEAN IsLinkDown(
    _In_ ULONG BusSpeed,
    _In_ ULONG UsbLinkState)
{
   if ((IS_SS_BUS(BusSpeed) && (UsbLinkState == USB_LINK_STATE_SS_DISABLED)) ||
       (IS_HS_BUS(BusSpeed) && (UsbLinkState == USB_LINK_STATE_DISCONNECTED))) {
       return TRUE;

   } else {
       return FALSE;
   }
}

//
//------------------------------------------------------------------------------
//

BOOLEAN IsLinkUp(
    _In_ ULONG BusSpeed,
    _In_ ULONG UsbLinkState)
{
   if ((IS_SS_BUS(BusSpeed) && (UsbLinkState == USB_LINK_STATE_RX_DETECT)) ||
       (IS_HS_BUS(BusSpeed) && (UsbLinkState == USB_LINK_STATE_ON))) {
       return TRUE;

   } else {
       return FALSE;
   }
}

//
//------------------------------------------------------------------------------
//

VOID InitTrbQueue(_Out_ PTransferQueue Queue, _In_ ULONG Size)
{

    NT_ASSERT((Queue != NULL) && (Size != 0));

    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->Size = Size;
}

//
//------------------------------------------------------------------------------
//

BOOLEAN IsQueueTrbFull(_In_ PTransferQueue Queue)
{

    NT_ASSERT(Queue != NULL);

    ULONG Index;

    Index = (Queue->Tail + 1) % Queue->Size;
    return (Queue->Head == Index);
}

//
//------------------------------------------------------------------------------
//

VOID EnqueueTrb(_Out_ PTransferQueue Queue, _In_opt_ PTRB_DATA TrbItem)
{

    NT_ASSERT(Queue != NULL);

    if (TrbItem != NULL) {
       Queue->TrbData[Queue->Tail] = *TrbItem;
    }

    Queue->Tail = (Queue->Tail + 1) % Queue->Size;
    return;
}

//
//------------------------------------------------------------------------------
//

VOID DequeueTrb(_Inout_ PTransferQueue Queue, _Out_opt_ PTRB_DATA TrbItem)
{

    NT_ASSERT(Queue != NULL);

    if (TrbItem != NULL) {
        *TrbItem = Queue->TrbData[Queue->Head];
    }

    Queue->Head = (Queue->Head + 1) % Queue->Size;
    return;
}

//
//------------------------------------------------------------------------------
//

static
VOID
HardwarePatch(
    PUSBFNMP_CONTEXT Context,
    UINT8 Phase)
{
    ULONG Index;
    PACPI_DEBUG_DEVICE_OEM_DATA OemData = (PVOID)gOemData;
    PUCHAR Address;
    UINT32 Data;


    if ((gOemDataLength < (3 * sizeof(UINT32))) ||
        (OemData->Signature != OEM_SIGNATURE)) {
        goto HardwarePatchExit;
    }

    for (Index = 0; Index < OemData->WriteCount; Index++) {
        if (OemData->Data[Index].Phase != Phase) continue;
        switch (OemData->Data[Index].BaseAddressRegister) {
        case 0:
            Address = (PUCHAR)Context->UsbCtrl + OemData->Data[Index].Offset;
            Data = INREG32((PULONG)Address);
            Data &= OemData->Data[Index].AndValue;
            Data |= OemData->Data[Index].OrValue;
            OUTREG32((PULONG)Address, Data);
            break;
        case 1:
            if (Context->Gpio == NULL) break;
            Address = (PUCHAR)Context->Gpio + OemData->Data[Index].Offset;
            Data = INREG32((PULONG)Address);
            Data &= OemData->Data[Index].AndValue;
            Data |= OemData->Data[Index].OrValue;
            OUTREG32((PULONG)Address, Data);
            break;
        case 255:
            KeStallExecutionProcessor(1000);
            break;
        }
    }

HardwarePatchExit:
    return;
 }


//
//------------------------------------------------------------------------------
//

__inline
static
PHYSICAL_ADDRESS
DmaPa(
    _In_ PUSBFNMP_CONTEXT Context,
    _In_ PVOID VirtualAddress)
{
    PHYSICAL_ADDRESS pa = Context->DmaPa;
    pa.QuadPart += (PUCHAR)VirtualAddress - (PUCHAR)Context->Dma;
    return pa;
}

//
//------------------------------------------------------------------------------
//

__inline
static
ULONG
BufferIndexFromAddress(
    _In_ PUSBFNMP_CONTEXT Context,
    PVOID Buffer)
{
    ULONG Offset, Index;

    Offset = (ULONG)((PUCHAR)Buffer - DMA_BUFFER_OFFSET - (PUCHAR)Context->Dma->Buffers);
    Index = Offset / DMA_BUFFER_SIZE;
    if ((Index >= DMA_BUFFERS) || ((Offset - Index * DMA_BUFFER_SIZE) != 0)) {
        Index = INVALID_INDEX;
    }
    return Index;
}

//
//------------------------------------------------------------------------------
//

static
VOID
EndpointWaitWhileBusy(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG Index)
{
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    PDEVICE_ENDPOINT_COMMAND_REGISTERS Registers;
    DEVICE_ENDPOINT_COMMAND_REGISTER Cmd;

    Registers = &UsbCtrl->DeviceRegisters.Endpoints.EndpointCommand[Index];
    do {
        Cmd.AsUlong32 = INREG32(&Registers->EndpointCommand.AsUlong32);
    } while (Cmd.CommandActive != 0);
}

//
//------------------------------------------------------------------------------
//

static
VOID
EndpointCommand(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG Index,
    DEVICE_ENDPOINT_COMMAND_REGISTER Command,
    ULONG Parameter0,
    ULONG Parameter1,
    ULONG Parameter2)
{
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    PDEVICE_ENDPOINT_COMMAND_REGISTERS Registers;
    DEVICE_ENDPOINT_COMMAND_REGISTER Cmd;

    Registers = &UsbCtrl->DeviceRegisters.Endpoints.EndpointCommand[Index];

    do {
        Cmd.AsUlong32 = INREG32(&Registers->EndpointCommand.AsUlong32);
        KeStallExecutionProcessor(gEndpointWriteDelay);
    } while (Cmd.CommandActive != 0);

    Command.CommandActive = 1;
    OUTREG32(&Registers->Parameter0, Parameter0);
    OUTREG32(&Registers->Parameter1, Parameter1);
    OUTREG32(&Registers->Parameter2, Parameter2);
    OUTREG32(&Registers->EndpointCommand.AsUlong32, Command.AsUlong32);
}

//
//------------------------------------------------------------------------------
//

static
VOID
EndpointCommandEx(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG Index,
    DEVICE_ENDPOINT_COMMAND_REGISTER Command,
    ULONG Parameter0,
    ULONG Parameter1,
    ULONG Parameter2,
    ULONG Active,
    ULONG IOnCompletion)
{
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    PDEVICE_ENDPOINT_COMMAND_REGISTERS Registers;
    DEVICE_ENDPOINT_COMMAND_REGISTER Cmd;

    Registers = &UsbCtrl->DeviceRegisters.Endpoints.EndpointCommand[Index];

    do {
        Cmd.AsUlong32 = INREG32(&Registers->EndpointCommand.AsUlong32);
        KeStallExecutionProcessor(gEndpointWriteDelay);
    } while (Cmd.CommandActive != 0);

    Command.CommandActive = Active;
    Command.InterruptOnCompletion = IOnCompletion;
    OUTREG32(&Registers->Parameter0, Parameter0);
    OUTREG32(&Registers->Parameter1, Parameter1);
    OUTREG32(&Registers->Parameter2, Parameter2);
    OUTREG32(&Registers->EndpointCommand.AsUlong32, Command.AsUlong32);
}

//
//------------------------------------------------------------------------------
//

__inline
static
PTRB
AllocFreeTrb(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG EndpointIndex,
    ULONG BufferIndex)
{
    PTRB Trb;
    ULONG TrbInIndex;

    // Get TRB from list
    if (Context->TrbFree[EndpointIndex] == 0) {
        LOG_TRACE("TrbInIndex[%d]:%d, TrbOutIndex[%d]=%d, TrbFree{%d]=%d", 
                  EndpointIndex, Context->TrbInIndex[EndpointIndex], 
                  EndpointIndex, Context->TrbOutIndex[EndpointIndex],
                  EndpointIndex, Context->TrbFree[EndpointIndex]);
        Trb = NULL;
        goto AllocFreeTrbEnd;
    }
    TrbInIndex = Context->TrbInIndex[EndpointIndex];
    Trb = &Context->Dma->Trb[EndpointIndex][TrbInIndex];

    ((PULONG)Trb)[0] = 0;
    ((PULONG)Trb)[1] = 0;
    ((PULONG)Trb)[2] = 0;
    ((PULONG)Trb)[3] = 0;

    Context->TrbInIndex[EndpointIndex]++;
    if (Context->TrbInIndex[EndpointIndex] >= TRB_PER_ENDPOINT) {
        Context->TrbInIndex[EndpointIndex] = 0;
#ifndef USE_LINK_TRB
        // Last TRB should be marked as such
        if (EndpointIndex > 1) Trb->LastTrb = 1;
#endif
    }

    if ((EndpointIndex == 0) || (EndpointIndex == 1)) {
        Context->TrbElement.PtrTrb = Trb;
        Context->TrbElement.BufferSize = 0;
        Context->TrbElement.BufferIndex = BufferIndex;
        Context->TrbElement.EndpointPos = EndpointIndex;
        Context->TrbElement.Status.IsNoCompletion = 0;
        EnqueueTrb(&Context->Transfer[EndpointIndex], &Context->TrbElement);
    }
    Context->TrbFree[EndpointIndex]--;

AllocFreeTrbEnd:
    return Trb;
}

//
//------------------------------------------------------------------------------
//

__inline
static
PTRB
ReleaseDoneTrb(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG EndpointIndex)
{
    PTRB Trb;
    ULONG TrbOutIndex;
#ifndef USE_LINK_TBD
    PHYSICAL_ADDRESS pa;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;
#endif

    if (Context->TrbFree[EndpointIndex] >= TRB_PER_ENDPOINT) {
        LOG_TRACE("TrbFree > TRB_PER_ENDPOINT");
        Trb = NULL;
        goto ReleaseDoneTrbEnd;
    }

    TrbOutIndex = Context->TrbOutIndex[EndpointIndex];
    Trb = &Context->Dma->Trb[EndpointIndex][TrbOutIndex];
    if (Trb->HardwareOwned != 0) {
        LOG_TRACE("Trb->HardwareOwned: %d, Trb->TrbStatus: %d", Trb->HardwareOwned, Trb->TrbStatus);
        Trb = NULL;
        goto ReleaseDoneTrbEnd;
    }

    Context->TrbOutIndex[EndpointIndex]++;
    if (Context->TrbOutIndex[EndpointIndex] >= TRB_PER_ENDPOINT) {
        Context->TrbOutIndex[EndpointIndex] = 0;
#ifndef USE_LINK_TRB
        // We reach end of TRB list - restart end point from beginning
        if (EndpointIndex > 1) {
            pa = DmaPa(Context, &Context->Dma->Trb[EndpointIndex][0]);
            Command.AsUlong32 = 0;
            Command.CommandType = ENDPOINT_CMD_START_TRANSFER;
            Command.InterruptOnCompletion = 1;
            EndpointCommand(
                Context, EndpointIndex, Command, pa.HighPart, pa.LowPart, 0
                );
        }
#endif

    }
    Context->TrbFree[EndpointIndex]++;

ReleaseDoneTrbEnd:
    return Trb;
}

//
//------------------------------------------------------------------------------
//

static
VOID
SetDeviceAddress(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG Address)
{
    DEVICE_CONFIGURATION_REGISTER DCFG;
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;

    DCFG.AsUlong32 = INREG32(
        &UsbCtrl->DeviceRegisters.Common.Configuration.AsUlong32
        );
    DCFG.DeviceAddress = Address;
    OUTREG32(
        &UsbCtrl->DeviceRegisters.Common.Configuration.AsUlong32,
        DCFG.AsUlong32
        );
}

//
//------------------------------------------------------------------------------
//

static
VOID
SetDevHostPeriodicParams(
    _Inout_ PUSBFNMP_CONTEXT Context,
    _In_reads_bytes_(MAX_SET_SEL_LENGTH) PUCHAR ExitLatency)
{

    DEVICE_COMMAND_REGISTERS DevCmd;
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    ULONG Parameter;

   //
   //  Process the Host periodic parameters (containing the Exit latency)
   //  by setting the value according to the USB synopsys controller
   //  6.3.1.6 Device Generic Command Register [0xC714]
   //  Command:02h.
   //

    Parameter = 0;
    if ((ExitLatency[0] != 0) && (ExitLatency[2] != 0)) {
        Parameter = ExitLatency[4];

    } else if ((ExitLatency[0] != 0) && (ExitLatency[2] == 0)) {
        Parameter = ExitLatency[1];    
    }

    //
    //  Basically, if the parameter value sent by the host is > 125 macrosec, 
    //  then the value that needs to be programmed is 0.
    //  This is specified in the Sysnopsys controller manual 6.3.1.6.1.
    //

    if (Parameter > MAX_EXIT_LATENCY_VALUE) {
        Parameter = 0;
    }

    DevCmd.Parameter = Parameter;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.DeviceCommand.Parameter, DevCmd.Parameter);
    DevCmd.Command.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.DeviceCommand.Command.AsUlong32);
    DevCmd.Command.CommandType = DEVICE_COMMAND_SET_PERIODIC_PARAMETERS;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.DeviceCommand.Command.AsUlong32,
             DevCmd.Command.AsUlong32);
    return; 
}

//
//------------------------------------------------------------------------------
//

static
VOID
EndpointConfigure(
    _Inout_ PUSBFNMP_CONTEXT Context,
    _In_ BOOLEAN Modify,
    _In_ PUSB_ENDPOINT_DESCRIPTOR Descriptor,
    _In_opt_ PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR CompanionDescriptor)
{
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;
    ENDPOINT_SET_CONFIGURATION_PARAMETER_0 Param0;
    ENDPOINT_SET_CONFIGURATION_PARAMETER_1 Param1;
    DEVICE_ENDPOINT_ENABLE_REGISTER Enable;
    ULONG Number;
    ULONG Index;
    PTRB Trb;
    PHYSICAL_ADDRESS pa;

    Number = Descriptor->bEndpointAddress & USB_ENDPOINT_ADDRESS_MASK;
    Index = Number << 1;
    if (USB_ENDPOINT_DIRECTION_IN(Descriptor->bEndpointAddress)) {
        Index++;
    }

    if (Index >= ENDPOINTS) {
        goto EndpointConfigureEnd;
    }

    //
    // See section 6.3.2.5.1
    //

    Param0.AsUlong32 = 0;
    Param0.EndpointType = Descriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK;
    Param0.MaxPacketSize = Descriptor->wMaxPacketSize;
    if (USB_ENDPOINT_DIRECTION_IN(Descriptor->bEndpointAddress)) {
        Param0.FifoNumber = Number;
    }

    //
    //  Set the burstsize for the Bulk EP
    //

    if (CompanionDescriptor != NULL) {
        Param0.BurstSize = CompanionDescriptor->bMaxBurst;
    }

    //
    // Endpoint 0 should be modified after soft reset, not initialized.
    //

    if (Modify) {
        Param0.ConfigAction = ENDPOINT_CONFIGURE_MODIFY;
    } else {
        Param0.ConfigAction = ENDPOINT_CONFIGURE_INITIALIZE;
    }
    Param1.AsUlong32 = 0;
    Param1.XferCompleteEnable = 1;
    Param1.XferInProgressEnable = 1;
    if (Number == 0) {
        Param1.XferNotReadyEnable = 1;
    }

    if (Descriptor->bInterval > 0) {
        Param1.IntervalM1 = Descriptor->bInterval - 1;
    }

    Param1.UsbEndpointNumber = Number;
    if (USB_ENDPOINT_DIRECTION_IN(Descriptor->bEndpointAddress)) {
        Param1.UsbEndpointDirection = 1;
    }

    if (CompanionDescriptor != NULL) {
        Param1.StreamCapable = (CompanionDescriptor->bmAttributes.Bulk.MaxStreams != 0) ? 1 : 0;
        Param1.StreamEventEnable = 0;
        Param1.ExternalBufferControl = 0;
    }

    //
    // Send configure command. Control endpoints have two
    // physical endpoints.
    //

    Command.AsUlong32 = 0;
    Command.CommandType = ENDPOINT_CMD_SET_CONFIGURATION;
    EndpointCommand(
        Context, Index, Command, Param0.AsUlong32, Param1.AsUlong32, 0
        );

    //
    // Send transfer resource configure command if this is a new
    // endpoint configuration.
    //

    if (!Modify) {

        Trb = Context->Dma->Trb[Index];
        RtlZeroMemory(Trb, sizeof(Context->Dma->Trb[Index]));

        Trb[TRB_PER_ENDPOINT].TrbControl = TRB_CONTROL_LINK;
        Trb[TRB_PER_ENDPOINT].HardwareOwned = 1;

        Context->TrbInIndex[Index] = 0;
        Context->TrbOutIndex[Index] = 0;
        Context->TrbFree[Index] = TRB_PER_ENDPOINT;

        Command.AsUlong32 = 0;
        Command.CommandType = ENDPOINT_CMD_TRANSFER_RESOURCE_CONFIG;
        EndpointCommand(Context, Index, Command, 1, 0, 0);
        EndpointWaitWhileBusy(Context, Index);

        //
        // Enable the logical endpoints that are enable in the new configuration (DALEPENA)
        //

        Enable = INREG32(&UsbCtrl->DeviceRegisters.Endpoints.EndpointEnable);
        Enable |= 1 << Index;
        OUTREG32(&UsbCtrl->DeviceRegisters.Endpoints.EndpointEnable, Enable);

        if (Number > 0) {
            pa = DmaPa(Context, Trb);
            Command.AsUlong32 = 0;
            Command.CommandType = ENDPOINT_CMD_START_TRANSFER;
            Command.InterruptOnCompletion = 1;
            EndpointCommand(
                Context, Index, Command, pa.HighPart, pa.LowPart, 0
                );
        }
    }

EndpointConfigureEnd:
    return;
}

//
//------------------------------------------------------------------------------
//

static
VOID
SetupControlTransfer(
    _In_ PUSBFNMP_CONTEXT Context,
    _In_ ULONG TrbType,
    _In_ BOOLEAN IsStatusData)
{

    PTRB Trb;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;
    PHYSICAL_ADDRESS pa;
    ULONG SetupPacketLength;

    // Allocate TRB
    Trb = AllocFreeTrb(Context, 0, INVALID_INDEX);
    if (Trb == NULL) {
        goto SetupControlTransferEnd;
    }

    SetupPacketLength = Context->Dma->SetupPacket.wLength;
    pa = DmaPa(Context, &Context->Dma->SetupPacket);
    if (IsStatusData != FALSE) {
        Trb->BufferPointerLow = pa.LowPart;
        Trb->BufferPointerHigh = pa.HighPart;
        Trb->BufferSize = sizeof(USB_DEFAULT_PIPE_SETUP_PACKET);

    } else {
        LOG_TRACE("Setup packet length:%d", SetupPacketLength);
        Context->Dma->SetupPacket.wLength = 0;
        pa.LowPart = 0;
        pa.HighPart = 0;
        Trb->BufferPointerLow = 0;
        Trb->BufferPointerHigh = 0;
        Trb->BufferSize = 0;
    }

    Trb->TrbControl = TrbType;
    Trb->LastTrb = 1;
    if (IsStatusData != FALSE) {
        Trb->InterruptOnComplete = 1;
        Trb->HardwareOwned = 1;
    }

    pa = DmaPa(Context, Trb);
    Command.AsUlong32 = 0;
    Command.CommandType = ENDPOINT_CMD_START_TRANSFER;
    EndpointCommand(Context, 0, Command, pa.HighPart, pa.LowPart, 0);

SetupControlTransferEnd:
    return;
}

//
//------------------------------------------------------------------------------
//

static
VOID
ResetController(
    _In_ PUSBFNMP_CONTEXT Context)
{
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    DEVICE_CONTROL_REGISTER DCTL;
    DEVICE_STATUS_REGISTER DSTS;
    DEVICE_CONFIGURATION_REGISTER DCFG;
    DEVICE_EVENT_ENABLE_REGISTER DEVTEN;
    GLOBAL_CORE_CONTROL_REGISTER GCTL;
    GLOBAL_EVENT_BUFFER_SIZE_REGISTER GEVTSIZ0;
    GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER EVNTCOUNT0;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;
    USB_ENDPOINT_DESCRIPTOR Descriptor;

    //Cherrytrail (CHT) specific
    int i;
    PDEVICE_ENDPOINT_COMMAND_REGISTERS Registers;

    //Baytrail (BYT) specific
    ULONG PhyRegister;

    LOG_TRACE("Synopsys Controller Speed: %d", Context->Speed);

    //
    // If the BIOS left the controller running, it needs to be stopped before
    // resetting
    //
    DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
    if (DCTL.RunStop == 1)
    {
        DCTL.RunStop = 0;
        OUTREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32, DCTL.AsUlong32);


        DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
        DCTL.RunStop = 0;
        OUTREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32, DCTL.AsUlong32);

        do {
            DSTS.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Status.AsUlong32);
        } while (DSTS.DeviceControllerHalted == 0);

    }

#if defined(_X86_) || defined(_AMD64_)

    //Intel platform workarounds
    LOG_TRACE("%04x %04x %d %04x", Context->VendorID, Context->DeviceID, DMA_BUFFERS, DMA_BUFFER_SIZE);
    if ((Context->NameSpace == KdNameSpacePCI) && (Context->VendorID == VID_INTEL)) {
        switch (Context->DeviceID) {
            //BYT
            // This is a temporary workaround to set the PCI clock on the baytrail
            // This should be removed once the BYT firmware is updated to do this.
            case DEVID_BAYTRAIL:
                LOG_TRACE("BYT");
                PhyRegister = INREG32(&UsbCtrl->GlobalRegisters.Phy.USB3PIPEControl[0]);
                PhyRegister |= PHY_SOFT_RESET_MASK;
                OUTREG32(&UsbCtrl->GlobalRegisters.Phy.USB3PIPEControl[0], PhyRegister);

                //Note, these must be "out" assembly instructions to function properly
                _outpd(0xcf8, 0x8000b0a0);
                _outpd(0xcfc, 0x0);

                PhyRegister &= ~PHY_SOFT_RESET_MASK;
                OUTREG32(&UsbCtrl->GlobalRegisters.Phy.USB3PIPEControl[0], PhyRegister);
                break;
        }
    }

    //For all x86/amd64, reset endpoints since we can't assume they are clear on reset
    //Targets: CHT, SoFIA
    for (i = 0; i < 32; i++) {
        Registers = &UsbCtrl->DeviceRegisters.Endpoints.EndpointCommand[i];
        OUTREG32(&Registers->Parameter0, 0);
        OUTREG32(&Registers->Parameter1, 0);
        OUTREG32(&Registers->Parameter2, 0);
        OUTREG32(&Registers->EndpointCommand.AsUlong32, 0);
    }

#endif

    //
    // 1. 'Soft Reset' the core
    //
    DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
    DCTL.CoreSoftReset = 1;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32, DCTL.AsUlong32);
    while (DCTL.CoreSoftReset == 1) {
        DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
    }

    // Apply hardware patch from ACPI DBG2 table
    HardwarePatch(Context, 1);

    // 2. Ensure the controller operates in device mode.
    //
    GCTL.AsUlong32 = INREG32(&UsbCtrl->GlobalRegisters.Common.CoreControl.AsUlong32);
    GCTL.PortCapabilityDirection = PORT_CAPABILITY_DEVICE_CONFIGURATION;
    OUTREG32(&UsbCtrl->GlobalRegisters.Common.CoreControl.AsUlong32, GCTL.AsUlong32);
    INREG32(&UsbCtrl->GlobalRegisters.Common.CoreControl.AsUlong32);

    //
    // 3. Program the Event Buffers.
    //

    OUTREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventBufferAddress.Hi,
             Context->DmaPa.HighPart);
    OUTREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventBufferAddress.Lo,
             Context->DmaPa.LowPart);

    //
    // Program the Size of the Event buffer
    //

    GEVTSIZ0.AsUlong32 = INREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventSize.AsUlong32);
    GEVTSIZ0.InterruptMask = 0;
    GEVTSIZ0.SizeInBytes = sizeof(Context->Dma->EventBuffer);
    OUTREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventSize.AsUlong32, GEVTSIZ0.AsUlong32);

    //
    // Program the Count of the events in the event buffer
    //

    EVNTCOUNT0.AsUlong32 = INREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventCount.AsUlong32);
    EVNTCOUNT0.Count = 0;
    OUTREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventCount.AsUlong32, EVNTCOUNT0.AsUlong32);

    Context->EventIndex = 0;

    //
    // 4. Program DCFG Register
    //

    DCFG.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Configuration.AsUlong32);
    DCFG.MaxDeviceSpeed = DEVICE_SPEED_SUPER_SPEED;
    DCFG.LPMCapable = 1;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.Configuration.AsUlong32, DCFG.AsUlong32);

    //
    // 5. Enable Device Events that should raise interrupt
    //

    DEVTEN.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.EnabledEvents.AsUlong32);
    DEVTEN.USBReset = 1;
    DEVTEN.ConnectionDone = 1;
    DEVTEN.USBLinkStateChange = 1;
    DEVTEN.ErraticError = 1;
    DEVTEN.Suspend = 1;
    DEVTEN.WakeUp = 1;
    DEVTEN.Disconnect = 1;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.EnabledEvents.AsUlong32, DEVTEN.AsUlong32);

    //
    // 6. Endpoint 0 setup
    //

    Command.AsUlong32 = 0;
    Command.CommandType = ENDPOINT_CMD_START_CONFIGURATION;
    EndpointCommand(Context, 0, Command, 0, 0, 0);

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.bLength = sizeof(Descriptor);
    Descriptor.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
    Descriptor.bEndpointAddress = 0;
    Descriptor.bmAttributes = USB_ENDPOINT_TYPE_CONTROL;
    Descriptor.wMaxPacketSize = 512;
    EndpointConfigure(Context, FALSE, &Descriptor, NULL);
    Descriptor.bEndpointAddress = USB_ENDPOINT_DIRECTION_MASK;
    EndpointConfigure(Context, FALSE, &Descriptor, NULL);
    SetupControlTransfer(Context, TRB_CONTROL_SETUP, TRUE);

    //
    // Apply hardware patch from ACPI DBG2 table
    //

    HardwarePatch(Context, 2);

    //
    // 7. Setting RS bit
    //

    DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
    DCTL.RunStop = 1;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32, DCTL.AsUlong32);
}

//
//------------------------------------------------------------------------------
//

static
VOID
XferDone(
    _In_ PUSBFNMP_CONTEXT Context,
    ULONG EndpointIndex,
    _Out_writes_to_(1, 0) PUSBFNMP_MESSAGE Message,
    _Out_writes_to_(1, 0) PUSBFNMP_MESSAGE_PAYLOAD Payload)
{
    PTRB Trb;
    ULONG BufferIndex;
    ULONG Transferred;

    // Get done transefer descriptor
    Trb = ReleaseDoneTrb(Context, EndpointIndex);

    //
    // Get the next Trb in the queue, so ensure that the Trb
    // matches with the enqueued DMA buffer index.
    //

    DequeueTrb(&Context->Transfer[EndpointIndex], &Context->TrbElement);

    if ((Trb == NULL) || (Trb != Context->TrbElement.PtrTrb)) {
        LOG_TRACE("Trb=NULL or Trb != Context->TrbElement.PtrTrb");
        goto XferDoneExit;
    }

    //
    //  Is this a SETUP packet?
    //  The SETUP packet has a separated setup Trb Dma buffer, so it does not
    //  use the DMA buffers for bulk In/Out Trbs, since there is only one 
    //  control transfers physical pipe that is used in both directions 
    //  as In/Out pipes.
    //  
    //

    if ((EndpointIndex == 0) && (Context->TrbElement.BufferIndex == INVALID_INDEX)) {

        RtlCopyMemory(&Payload->udr,
                      &Context->Dma->SetupPacket,
                      sizeof(Payload->udr));

        // Special processing for set address command needed
        if ((Payload->udr.bmRequestType.Type == BMREQUEST_STANDARD) &&
            (Payload->udr.bRequest == USB_REQUEST_SET_ADDRESS)) {
            SetDeviceAddress(Context, Payload->udr.wValue.W);

        } else if ((Payload->udr.bmRequestType.Type == BMREQUEST_STANDARD) &&
                   (Payload->udr.bRequest == USB_REQUEST_SET_SEL)) {
            if (Payload->udr.wLength == SET_SEL_FIXED_LENGTH) {

                Context->IsInProgressDataPhase = TRUE;
                Context->SysExitLatencyLength = Payload->udr.wLength;
                SetupControlTransfer(Context, TRB_CONTROL_STATUS_DATA, TRUE);
            }

        } else if ((Trb->TrbControl == TRB_CONTROL_STATUS_DATA) &&
                   (Context->IsInProgressDataPhase != FALSE)) {

            NT_ASSERT(Context->SysExitLatencyLength == SET_SEL_FIXED_LENGTH);

            RtlCopyMemory(&Context->SysExitLatencyParams, &Payload->utr.Buffer, Context->SysExitLatencyLength);
            SetDevHostPeriodicParams(Context, Context->SysExitLatencyParams);
            Context->IsInProgressDataPhase = FALSE;
            Context->SysExitLatencyLength = 0;
            goto XferDoneExit;
        }

        *Message = UsbMsgSetupPacket;
        goto XferDoneExit;
    }

    // If it wasn't SETUP packet transfer buffer should be valid
    if (Context->TrbElement.BufferIndex == INVALID_INDEX) {
        LOG_TRACE("Context->TrbElement.BufferIndex = INVALID_INDEX");
        goto XferDoneExit;
    }

    //
    // The TRB->BUFSIZ (see page 654 about BUfSIZ-the total remaining size of the buffer descriptor/trb) 
    // represents the amount of the buffer that has not been transferred yet. 
    // So the requested bufferSize - the buffer that is not transferred
    // will give you the amount of the actual transferred in the current TRB.
    //

    Transferred = Context->BufferSize[Context->TrbElement.BufferIndex] - Trb->BufferSize;
    //LOG("%d/%d %d", EndpointIndex >> 1, EndpointIndex & 1, Transferred);

    // Prepare message and payload
    if ((EndpointIndex & 1) != 0)
        {
        *Message = UsbMsgEndpointStatusChangedTx;
        Payload->utr.Direction = UsbEndpointDirectionDeviceTx;
        }
    else
        {
        *Message = UsbMsgEndpointStatusChangedRx;
        Payload->utr.Direction = UsbEndpointDirectionDeviceRx;
        }
    Payload->utr.TransferStatus = UsbTransferStatusComplete;
    Payload->utr.EndpointIndex = (UCHAR)(EndpointIndex >> 1);
    Payload->utr.BytesTransferred = Transferred;
    Payload->utr.Buffer = Context->Dma->Buffers[Context->TrbElement.BufferIndex];
    Payload->utr.NumberOfTransfers = 1;

    // Activate Setup Packet transfer when this ends control transfer
    if ((EndpointIndex < 2) && (
            (Trb->TrbControl == TRB_CONTROL_STATUS_DATA) ||
            (Trb->TrbControl == TRB_CONTROL_STATUS_NO_DATA))) {
        SetupControlTransfer(Context, TRB_CONTROL_SETUP, TRUE);
    }

XferDoneExit:
    return;
}

//
//------------------------------------------------------------------------------
//

static
VOID
XferAbort(
    _In_ PUSBFNMP_CONTEXT Context,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Out_writes_to_(1, 0) PUSBFNMP_MESSAGE_PAYLOAD Payload)
{
    ULONG EndpointIndex, BufferIndex;

    for (EndpointIndex = 0; EndpointIndex < ENDPOINTS; EndpointIndex++) {
        DequeueTrb(&Context->Transfer[EndpointIndex], &Context->TrbElement);

        if (Context->TrbElement.BufferIndex == INVALID_INDEX) {
            continue;
        }

        // Prepare message and payload
        if ((EndpointIndex & 1) != 0)
            {
            *Message = UsbMsgEndpointStatusChangedTx;
            Payload->utr.Direction = UsbEndpointDirectionDeviceTx;
            }
        else
            {
            *Message = UsbMsgEndpointStatusChangedRx;
            Payload->utr.Direction = UsbEndpointDirectionDeviceRx;
            }
        Payload->utr.TransferStatus = UsbTransferStatusAborted;
        Payload->utr.EndpointIndex = (UCHAR)(EndpointIndex >> 1);
        Payload->utr.BytesTransferred = 0;
        Payload->utr.Buffer = Context->Dma->Buffers[Context->TrbElement.BufferIndex];
    }
}

//
//------------------------------------------------------------------------------
//

static
VOID
ConnectionDone(
    _In_ PUSBFNMP_CONTEXT Context,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Out_ PUSBFNMP_MESSAGE_PAYLOAD Payload)
{
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    DEVICE_STATUS_REGISTER DSTS;
    GLOBAL_CORE_CONTROL_REGISTER GCTL;
    ULONG Value;
    PULONG OtherPhyRegister;
    ULONG SuspendBit;
    ULONG RAMClkSel;
    USHORT MaxPacketSize;
    USB_ENDPOINT_DESCRIPTOR Descriptor;

    //
    // Read the device speed.
    //
    DSTS.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Status.AsUlong32);

    //
    // Choose appropriate Clk Frequency and disable other Speed.
    //
    switch (DSTS.ConnectedSpeed) {
    case DEVICE_SPEED_SUPER_SPEED:
        Context->Speed = UsbBusSpeedSuper;
        OtherPhyRegister = &UsbCtrl->GlobalRegisters.Phy.USB3PIPEControl[0];
        SuspendBit =  USB2_PHY_CONFIG_SUSPEND_PHY_BIT;
        RAMClkSel = RAM_SELECT_PIPE_CLOCK;
        MaxPacketSize = 512;
        *Message = UsbMsgBusEventSpeed;
        Payload->ubs = UsbBusSpeedSuper;
        break;
    default:
    case DEVICE_SPEED_FULL_SPEED:
    case DEVICE_SPEED_FULL_SPEED_OTHER:
        Context->Speed = UsbBusSpeedFull;
        OtherPhyRegister = &UsbCtrl->GlobalRegisters.Phy.USB2PhyConfig[0];
        SuspendBit =  USB3_PIPECONTROL_SUSPEND_SS_PHY_BIT;
        RAMClkSel = RAM_SELECT_BUS_CLOCK;
        MaxPacketSize = 64;
        *Message = UsbMsgBusEventSpeed;
        Payload->ubs = UsbBusSpeedFull;
        break;
    case DEVICE_SPEED_HIGH_SPEED:
        Context->Speed = UsbBusSpeedHigh;
        OtherPhyRegister = &UsbCtrl->GlobalRegisters.Phy.USB2PhyConfig[0];
        SuspendBit =  USB3_PIPECONTROL_SUSPEND_SS_PHY_BIT;
        RAMClkSel = RAM_SELECT_BUS_CLOCK;
        MaxPacketSize = 64;
        *Message = UsbMsgBusEventSpeed;
        Payload->ubs = UsbBusSpeedHigh;
        break;
    case DEVICE_SPEED_LOW_SPEED:
        Context->Speed = UsbBusSpeedLow;
        OtherPhyRegister = &UsbCtrl->GlobalRegisters.Phy.USB2PhyConfig[0];
        SuspendBit =  USB3_PIPECONTROL_SUSPEND_SS_PHY_BIT;
        RAMClkSel = RAM_SELECT_BUS_CLOCK;
        MaxPacketSize = 8;
        *Message = UsbMsgBusEventSpeed;
        Payload->ubs = UsbBusSpeedLow;
        break;
    }

    LOG_TRACE("DSTS Connected Speed=%d", DSTS.ConnectedSpeed);

    GCTL.AsUlong32 = INREG32(&UsbCtrl->GlobalRegisters.Common.CoreControl.AsUlong32);
    GCTL.RAMClockSelect = RAMClkSel;
    OUTREG32(&UsbCtrl->GlobalRegisters.Common.CoreControl.AsUlong32, GCTL.AsUlong32);

    Value = INREG32(OtherPhyRegister);
    Value |= 1 << SuspendBit;
    OUTREG32(OtherPhyRegister, Value);

    // Apply hardware patch from ACPI DBG2 table
    HardwarePatch(Context, 3);

    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.bLength = sizeof(Descriptor);
    Descriptor.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
    Descriptor.bEndpointAddress = 0;
    Descriptor.bmAttributes = USB_ENDPOINT_TYPE_CONTROL;
    Descriptor.wMaxPacketSize = MaxPacketSize;
    EndpointConfigure(Context, TRUE, &Descriptor, NULL);
    Descriptor.bEndpointAddress = USB_ENDPOINT_DIRECTION_MASK;
    EndpointConfigure(Context, TRUE, &Descriptor, NULL);
}

//
//------------------------------------------------------------------------------
//

static
BOOLEAN
CleanConnection(
    _In_ PUSBFNMP_CONTEXT Context,
    BOOLEAN Disconnect)
{
    BOOLEAN PendingAborts = FALSE;
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    DEVICE_CONTROL_REGISTER DCTL;
    BOOLEAN SetupPacketNeeded = FALSE;
    ULONG EndpointIndex;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;

    // See 8.1.7
    if (Disconnect) {
        DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
        DCTL.LinkStateChangeRequest = 5;
        OUTREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32, DCTL.AsUlong32);
    }

    // Stop active endpoints
    for (EndpointIndex = 0; EndpointIndex < ENDPOINTS; EndpointIndex++) {
        if (Context->FirstBuffer[EndpointIndex] == INVALID_INDEX) continue;

        PendingAborts = TRUE;
        if (EndpointIndex < 2) SetupPacketNeeded = TRUE;

        Command.AsUlong32 = 0;
        Command.CommandType = ENDPOINT_CMD_END_TRANSFER;
        Command.Parameter = Context->XferRscIdx[EndpointIndex];
        EndpointCommand(Context, EndpointIndex, Command, 0, 0, 0);
    }

    // Unstall stalled endpoints
    for (EndpointIndex = 0; EndpointIndex < ENDPOINTS; EndpointIndex++) {
        if ((Context->StalledMask & (1 << EndpointIndex)) == 0) continue;
        Command.AsUlong32 = 0;
        Command.CommandType = ENDPOINT_CMD_CLEAR_STALL;
        EndpointCommand(Context, EndpointIndex, Command, 0, 0, 0);
    }

    // Reset clears address to zero
    SetDeviceAddress(Context, 0);

    // Start setup packet transfer when needed
    if (SetupPacketNeeded) {
        SetupControlTransfer(Context, TRB_CONTROL_SETUP, TRUE);
    }

    return PendingAborts;
}

//
//------------------------------------------------------------------------------
//

static
VOID
XferComplete(
    _In_ PUSBFNMP_CONTEXT Context,
    ENDPOINT_TRANSFER_COMPLETE_EVENT Event,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Out_ PUSBFNMP_MESSAGE_PAYLOAD Payload)
{
    XferDone(Context, Event.PhysicalEndpointNumber, Message, Payload);
}

//
//------------------------------------------------------------------------------
//

static
VOID
XferInProgress(
    _In_ PUSBFNMP_CONTEXT Context,
    ENDPOINT_TRANSFER_IN_PROGRESS_EVENT Event,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Out_ PUSBFNMP_MESSAGE_PAYLOAD Payload
    )
{
    XferDone(Context, Event.PhysicalEndpointNumber, Message, Payload);
}

//
//------------------------------------------------------------------------------
//

static
VOID
EndpointCommandComplete(
    _In_ PUSBFNMP_CONTEXT Context,
    ENDPOINT_COMMAND_COMPLETE_EVENT Event)
{
    ULONG EndpointIndex;

    EndpointIndex = Event.PhysicalEndpointNumber;
    switch (Event.CommandType) {
    case ENDPOINT_CMD_START_TRANSFER:
        Context->XferRscIdx[EndpointIndex] = (UCHAR)Event.XferRscIdx;
        break;
    }
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysGetMemoryRequirements(
    PDEBUG_DEVICE_DESCRIPTOR Device,
    PULONG ContextLength,
    PULONG DmaLength,
    PULONG DmaAlignment)
{
    UNREFERENCED_PARAMETER(Device);

    *ContextLength = sizeof(USBFNMP_CONTEXT);
    *DmaLength = sizeof(DMA_MEMORY);
    *DmaAlignment = PAGE_SIZE;

    return STATUS_SUCCESS;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysInitializeLibrary(
    PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    PCHAR LoaderOptions,
    PDEBUG_DEVICE_DESCRIPTOR Device,
    PULONG ContextLength,
    PULONG DmaLength,
    PULONG DmaAlignment)
{
	PCHAR EndpointDelay;
    UNREFERENCED_PARAMETER(ImportTable);

	if (LoaderOptions != NULL) {
		EndpointDelay = strstr(LoaderOptions, SYNOPSYS_ENDPOINT_DELAY_OPTION);
		if ((EndpointDelay != NULL) && (
			(EndpointDelay[sizeof(SYNOPSYS_ENDPOINT_DELAY_OPTION) - 1] == '='))) {
			
			gEndpointWriteDelay = atol(&EndpointDelay[sizeof(SYNOPSYS_ENDPOINT_DELAY_OPTION)]);
			if (gEndpointWriteDelay < SYNOPSYS_ENDPOINT_DELAY_MIN)
			{
				LOG_TRACE("Endpoint delay %d was lower than minimum delay. Increasing to %d", gEndpointWriteDelay, SYNOPSYS_ENDPOINT_DELAY_MIN);
				gEndpointWriteDelay = SYNOPSYS_ENDPOINT_DELAY_MIN;	
			}
			else if (gEndpointWriteDelay > SYNOPSYS_ENDPOINT_DELAY_MAX)
			{
				LOG_TRACE("Endpoint delay %d was higher than maximum delay. Decreasing to %d", gEndpointWriteDelay, SYNOPSYS_ENDPOINT_DELAY_MAX);
				gEndpointWriteDelay = SYNOPSYS_ENDPOINT_DELAY_MAX;
			}
		}
	}

    return UsbFnMpSynopsysGetMemoryRequirements(Device,
                                                ContextLength,
                                                DmaLength,
                                                DmaAlignment);
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
ULONG
UsbFnMpSynopsysGetHardwareContextSize(
    PDEBUG_DEVICE_DESCRIPTOR Device)
{
    ULONG ContextLength;
    ULONG DmaLength;
    ULONG DmaAlignment;

    ContextLength = 0;
    DmaLength = 0;
    DmaAlignment = 0;
    UsbFnMpSynopsysGetMemoryRequirements(Device,
                                         &ContextLength,
                                         &DmaLength,
                                         &DmaAlignment);

    ContextLength += (PAGE_SIZE - 1);
    ContextLength &= ~(PAGE_SIZE - 1);
    DmaLength += (PAGE_SIZE - 1);
    DmaLength &= ~(PAGE_SIZE - 1);

    return (ContextLength + DmaLength);
}

//
//------------------------------------------------------------------------------
//

VOID
UsbFnMpSynopsysDeinitializeLibrary(
    VOID)
{
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysStartController(
    PVOID Miniport,
    PDEBUG_DEVICE_DESCRIPTOR Device,
    PVOID DmaBuffer)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG EndpointIndex, BufferIndex;

    // Setup miniport context
    if (Device != NULL) {
        Context->UsbCtrl = (PVOID)Device->BaseAddress[0].TranslatedAddress;
        if (Device->BaseAddress[1].Valid) {
            Context->Gpio = (PVOID)Device->BaseAddress[1].TranslatedAddress;
        }
        else {
            Context->Gpio = NULL;
        }

        Context->Dma = DmaBuffer;
        Context->DmaPa = KdGetPhysicalAddress(DmaBuffer);

        Context->VendorID = Device->VendorID;
        Context->DeviceID = Device->DeviceID;
        Context->NameSpace = Device->NameSpace;

        //
        // Data on OemData pointer is accessible only on first initialization,
        // so make our own copy for subsequent initializations.
        //
        if ((gOemDataLength == 0) && (Device->OemData != NULL) &&
            (Device->OemDataLength < OEM_BUFFER_SIZE)) {
            RtlCopyMemory(gOemData, Device->OemData, Device->OemDataLength);
            gOemDataLength = Device->OemDataLength;
        }

    }

    Context->Attached = FALSE;
    Context->Configured = FALSE;
    Context->PendingReset = FALSE;
    Context->PendingDetach = FALSE;
    Context->Speed = UsbBusSpeedUnknown;

    RtlZeroMemory(Context->Dma, sizeof(*Context->Dma));

    for (BufferIndex = 0; BufferIndex < DMA_BUFFERS; BufferIndex++) {
        Context->NextBuffer[BufferIndex] = BufferIndex + 1;
        }
    Context->NextBuffer[DMA_BUFFERS - 1] = INVALID_INDEX;
    Context->FreeBuffer = 0;

    for (EndpointIndex = 0; EndpointIndex < ENDPOINTS; EndpointIndex++) {
        Context->FirstBuffer[EndpointIndex] = INVALID_INDEX;
        Context->LastBuffer[EndpointIndex] = INVALID_INDEX;
    }

    for (EndpointIndex = 0; EndpointIndex < ENDPOINTS; EndpointIndex++) {
        InitTrbQueue(&Context->Transfer[EndpointIndex], MAX_NUMBER_OF_TRBS + 1);
    }

    // And finally start hardware
    ResetController(Context);

    return STATUS_SUCCESS;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysStopController(
    PVOID Miniport)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    DEVICE_CONTROL_REGISTER DCTL;
    DEVICE_STATUS_REGISTER DSTS;
    GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER EVNTCOUNT0;
    ULONG64 Frequency;
    ULONG64 Start;
    NTSTATUS Status;
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;

    Status = STATUS_UNSUCCESSFUL;

    //
    // Get the current time and cycle counter frequency so that the runtime of
    // this routine can be bounded.
    //

    Start = KdReadCycleCounter(&Frequency);

    //
    // Tell the controller to stop running.
    //

    DCTL.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32);
    DCTL.RunStop = 0;
    OUTREG32(&UsbCtrl->DeviceRegisters.Common.Control.AsUlong32, DCTL.AsUlong32);

    //
    // Wait for the controller to stop, but bail if it takes longer than 250ms.
    //

    do {

        //
        // Acknowledge any pending events while waiting for the controller to
        // shutdown.  Unacknowledged pending events will prevent the controller
        // from shutting down.
        //

        EVNTCOUNT0.AsUlong32 = INREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventCount.AsUlong32);

        if (EVNTCOUNT0.AsUlong32 > 0) {
           OUTREG32(&UsbCtrl->GlobalRegisters.EventBuffers[0].EventCount.AsUlong32, EVNTCOUNT0.AsUlong32);
        }

        DSTS.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Status.AsUlong32);
        if (DSTS.DeviceControllerHalted != 0) {
            LOG_TRACE("Exit by halting Controller");
            Status = STATUS_SUCCESS;
            break;
        }

    } while ((KdReadCycleCounter(NULL) - Start) < (Frequency / 4));

    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysAllocateTransferBuffer(
    PVOID Miniport,
    ULONG Size,
    PVOID* TransferBuffer)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG Index;

    Index = Context->FreeBuffer;
    if ((Size > DMA_BUFFER_SIZE) || (Index == INVALID_INDEX)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto UsbFnMpAllocateTransferBufferEnd;
        }

    Context->FreeBuffer = Context->NextBuffer[Index];
    Context->NextBuffer[Index] = INVALID_INDEX;
    *TransferBuffer = &Context->Dma->Buffers[Index][DMA_BUFFER_OFFSET];

    Status = STATUS_SUCCESS;

UsbFnMpAllocateTransferBufferEnd:
    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysFreeTransferBuffer(
    PVOID Miniport,
    PVOID TransferBuffer)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG BufferIndex;

    BufferIndex = BufferIndexFromAddress(Context, TransferBuffer);
    if (BufferIndex == INVALID_INDEX) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpFreeTransferBufferEnd;
    }

    // Check if buffer isn't in transfer
    if (Context->NextBuffer[BufferIndex] != INVALID_INDEX) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpFreeTransferBufferEnd;
    }

    // Add it to free ones...
    Context->NextBuffer[BufferIndex] = Context->FreeBuffer;
    Context->FreeBuffer = BufferIndex;

    Status = STATUS_SUCCESS;

UsbFnMpFreeTransferBufferEnd:
    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysConfigureEnableEndpoints(
    PVOID Miniport,
    PUSBFNMP_DEVICE_INFO DeviceInfo)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;
    PUSBFNMP_CONFIGURATION_INFO ConfigurationInfo;
    ULONG Interfaces, InterfaceIndex;
    PUSBFNMP_INTERFACE_INFO InterfaceInfo;
    ULONG Endpoints, EndpointIndex;
    PUSB_ENDPOINT_DESCRIPTOR Descriptor;
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR CompanionDescriptor;
    DEVICE_ENDPOINT_ENABLE_REGISTER Enable;

    // There should be at least one configuration
    if (DeviceInfo->DeviceDescriptor->bNumConfigurations < 1) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpConfigureEnableEndpointsEnd;
    }

    Command.AsUlong32 = 0;
    Command.CommandType = ENDPOINT_CMD_START_CONFIGURATION;
    Command.Parameter = 2;
    EndpointCommand(Context, 0, Command, 0, 0, 0);

    // Iterate over all interfaces in first configuration.
    ConfigurationInfo = DeviceInfo->ConfigurationInfoTable[0];
    Interfaces = ConfigurationInfo->ConfigDescriptor->bNumInterfaces;
    for (InterfaceIndex = 0; InterfaceIndex < Interfaces; InterfaceIndex++) {
        InterfaceInfo = ConfigurationInfo->InterfaceInfoTable[InterfaceIndex];

        // Iterate over all endpoints in this interface.
        Endpoints = InterfaceInfo->InterfaceDescriptor->bNumEndpoints;
        for (EndpointIndex = 0; EndpointIndex < Endpoints; EndpointIndex++) {
            Descriptor = InterfaceInfo->EndpointDescriptorTable[EndpointIndex];
            CompanionDescriptor = InterfaceInfo->EndpointCompanionDescriptor[EndpointIndex];
            EndpointConfigure(Context, FALSE, Descriptor, CompanionDescriptor);
        }
    }

    if (IS_SS_BUS(Context->Speed)) {

        //
        //  Ensure to enable the control endpoint
        //

        EndpointWaitWhileBusy(Context, 0);
        Enable = INREG32(&Context->UsbCtrl->DeviceRegisters.Endpoints.EndpointEnable);
        Enable |= 0x3;
        OUTREG32(&Context->UsbCtrl->DeviceRegisters.Endpoints.EndpointEnable, Enable);
        EndpointWaitWhileBusy(Context, 0);
    }

    // We are now configured
    Context->Configured = TRUE;

    // Done
    Status = STATUS_SUCCESS;

UsbFnMpConfigureEnableEndpointsEnd:
    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysGetEndpointMaxPacketSize(
    PVOID Miniport,
    UINT8 EndpointType,
    USB_DEVICE_SPEED BusSpeed,
    PUINT16 MaxPacketSize)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Miniport);

    switch (BusSpeed) {
        case UsbLowSpeed:
        default:
            *MaxPacketSize = 0;
            Status = STATUS_INVALID_PARAMETER;
            break;

        case UsbFullSpeed:
            switch (EndpointType) {
            case USB_ENDPOINT_TYPE_CONTROL:
            case USB_ENDPOINT_TYPE_BULK:
            case USB_ENDPOINT_TYPE_INTERRUPT:
                *MaxPacketSize = 64;
                Status = STATUS_SUCCESS;
                break;
            case USB_ENDPOINT_TYPE_ISOCHRONOUS:
                *MaxPacketSize = 1023;
                Status = STATUS_SUCCESS;
                break;
            default:
                *MaxPacketSize = 0;
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            break;

        case UsbHighSpeed:
            switch (EndpointType) {
            case USB_ENDPOINT_TYPE_CONTROL:
                *MaxPacketSize = 64;
                Status = STATUS_SUCCESS;
                break;
            case USB_ENDPOINT_TYPE_BULK:
                *MaxPacketSize = 512;
                Status = STATUS_SUCCESS;
                break;
            case USB_ENDPOINT_TYPE_INTERRUPT:
                *MaxPacketSize = 1024;
                Status = STATUS_SUCCESS;
                break;
            case USB_ENDPOINT_TYPE_ISOCHRONOUS:
                *MaxPacketSize = 1024;
                Status = STATUS_SUCCESS;
                break;
            default:
                *MaxPacketSize = 0;
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            break;

       case UsbSuperSpeed:
            switch (EndpointType) {
            case USB_ENDPOINT_TYPE_CONTROL:
                *MaxPacketSize = 512;
                Status = STATUS_SUCCESS;
                break;

            case USB_ENDPOINT_TYPE_BULK:
                *MaxPacketSize = 1024;
                Status = STATUS_SUCCESS;
                break;

            case USB_ENDPOINT_TYPE_INTERRUPT:
                *MaxPacketSize = 1024;
                Status = STATUS_SUCCESS;
                break;

            case USB_ENDPOINT_TYPE_ISOCHRONOUS:
                *MaxPacketSize = 1024;
                Status = STATUS_SUCCESS;
                break;

            default:
                *MaxPacketSize = 0;
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            break;
    }

    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysGetMaxTransferSize(
    PVOID Miniport,
    PULONG MaxTransferSize)
{
    UNREFERENCED_PARAMETER(Miniport);

    *MaxTransferSize = DMA_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysGetEndpointStallState(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    PBOOLEAN Stalled)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG EndpointPos;

    EndpointPos = EndpointIndex << 1;
    if (Direction == UsbEndpointDirectionDeviceTx) EndpointPos++;
    if (EndpointPos >= ENDPOINTS) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpEndpointStallStateEnd;
    }

    *Stalled = (Context->StalledMask & (1 << EndpointPos)) != 0;
    Status = STATUS_SUCCESS;

UsbFnMpEndpointStallStateEnd:
    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysSetEndpointStallState(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    BOOLEAN StallEndPoint)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG EndpointPos;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;

    EndpointPos = EndpointIndex << 1;
    if (Direction == UsbEndpointDirectionDeviceTx) EndpointPos++;
    if (EndpointPos >= ENDPOINTS) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpSynopsysSetEndpointStallStateEnd;
    }

    Command.AsUlong32 = 0;
    if (StallEndPoint) {
        Command.CommandType = ENDPOINT_CMD_SET_STALL;
    }
    else {
        Command.CommandType = ENDPOINT_CMD_CLEAR_STALL;
    }
    EndpointCommand(Context, EndpointPos, Command, 0, 0, 0);

    Status = STATUS_SUCCESS;

    //
    //  If we do a set stall command on endpoint 0, then we'll need to ensure
    //  to program the controller to receive new setup packets
    //  (the function SetupControlTransfer also will Setup the TRB structure), otherwise
    //  the device won't be able to continue the link setup phase.
    //  Please see the 8.4 Control transfer Programming model in the 
    //  Synopsys document (Software flow chart for control transfer)
    //  Wait for Host -> Set Stall -> Setup a Control-Setup TRB Start transfer
    //                      or
    //  End transfer  -> Set Stall -> Setup a Control-Setup TRB Start transfer
    //                      .....
    //
    if ((Command.CommandType == ENDPOINT_CMD_SET_STALL) && (EndpointIndex == 0)) {
        SetupControlTransfer(Context, TRB_CONTROL_SETUP, TRUE);
    }

UsbFnMpSynopsysSetEndpointStallStateEnd:
    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysTransfer(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    PULONG BufferSize,
    PVOID Buffer)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG BufferIndex, EndpointPos;
    PTRB Trb;
    PHYSICAL_ADDRESS pa;
    DEVICE_ENDPOINT_COMMAND_REGISTER Command;
    PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;

    // Get buffer index
    BufferIndex = BufferIndexFromAddress(Context, Buffer);
    if ((BufferIndex == INVALID_INDEX) ||
        (Context->NextBuffer[BufferIndex] != INVALID_INDEX)) {
        Status = STATUS_INVALID_PARAMETER;
        LOG_TRACE("BufferIndex == INVALID_INDEX");
        goto UsbFnMpTransferEnd;
    }

    // Get end point index
    EndpointPos = EndpointIndex << 1;
    if (Direction == UsbEndpointDirectionDeviceTx) EndpointPos++;
    if (EndpointPos >= ENDPOINTS) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpTransferEnd;
    }

    // Get free TRB from endpoint circullar list
    Trb = AllocFreeTrb(Context, EndpointPos, BufferIndex);
    if (Trb == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        LOG_TRACE("Trb == NULL");
        goto UsbFnMpTransferEnd;
    }

    pa = DmaPa(Context, Buffer);
    Trb->BufferPointerLow = pa.LowPart;
    Trb->BufferPointerHigh = pa.HighPart;
    Trb->BufferSize = *BufferSize;

    // TRB type is different on control transfers
    if (EndpointIndex == 0) {
        SetupPacket = &Context->Dma->SetupPacket;
        if (SetupPacket->bmRequestType.Dir != 0)
            {
            if (Direction == UsbEndpointDirectionDeviceTx) {
                Trb->TrbControl = TRB_CONTROL_DATA;
            }
            else if (SetupPacket->wLength == 0) {
                Trb->TrbControl = TRB_CONTROL_STATUS_NO_DATA;
            }
            else {
                Trb->TrbControl = TRB_CONTROL_STATUS_DATA;
            }
        }
        else {
            if (Direction != UsbEndpointDirectionDeviceTx) {
                Trb->TrbControl = TRB_CONTROL_DATA;
            }
            else if (SetupPacket->wLength == 0) {
                Trb->TrbControl = TRB_CONTROL_STATUS_NO_DATA;
            }
            else {
                Trb->TrbControl = TRB_CONTROL_STATUS_DATA;
            }
        }

        if ((Trb->TrbControl == TRB_CONTROL_STATUS_DATA) ||
            ((Direction == UsbEndpointDirectionDeviceTx) && 
             (Trb->TrbControl == TRB_CONTROL_STATUS_NO_DATA))) {

            //
            //  The Synopsys doc. states that the SW should wait for at least 
            //  one XferNotReady event before submitting the Status Stage TRB.
            //  This is required to give time to the host to complete the previous 
            //  setup command payload, otherwise the enumeration could fail if the
            //  Status stage packet is sent to soon and the host is not ready to accept 
            //  the Setup Status packet.
            //  Since this applies only to Control Transfer EPs, then here is checked
            //  only Setup Status 2/3 phase stage packets.
            //  Since we're inside of the setup phase waiting on xferNotReady events, then
            //  there should NOT arrive any other events by USB control flow definition, otherwise
            //  the USB device controller & Host code will get stuck inside of the setup 
            //  enumeration phase forever, and this code will fail early on since it means
            //  that there are more DATA setup TRBs pending to be processed instead of setup stage
            //  TRBs.
            //

            Status = WaitForDeviceEventOnEP(Context, EndpointEventXferNotReady);
            if (!NT_SUCCESS(Status)) {
                ReleaseDoneTrb(Context, EndpointPos);
                LOG_TRACE("Failed waiting on device Event");
                goto UsbFnMpTransferEnd;
            }
        }

        Trb->LastTrb = 1;

    } else {
        Trb->TrbControl = TRB_CONTROL_NORMAL;
        if (Direction == UsbEndpointDirectionDeviceRx)
            {
            Trb->ContinueOnShortPacket = 1;
            Trb->InterruptOnShortPacket = 1;
            }
    }
    Trb->InterruptOnComplete = 1;
    Trb->HardwareOwned = 1;

    // Issue Start/Update transfer command
    if (EndpointIndex == 0) {
        pa = DmaPa(Context, Trb);
        Command.AsUlong32 = 0;
        Command.CommandType = ENDPOINT_CMD_START_TRANSFER;
        EndpointCommand(
            Context, EndpointPos, Command, pa.HighPart, pa.LowPart, 0
            );
    }
    else {
        Command.AsUlong32 = 0;
        Command.CommandType = ENDPOINT_CMD_UPDATE_TRANSFER;
        Command.Parameter = Context->XferRscIdx[EndpointPos];
        EndpointCommandEx(Context, EndpointPos, Command, 0, 0, 0, 1, 1);
    }

    // Remember buffer size (to be able compute transfer size)
    Context->BufferSize[BufferIndex] = *BufferSize;

    //
    //  Adds the TRB
    //

    if ((EndpointPos != 0) && (EndpointPos != 1)) {
        Context->TrbElement.PtrTrb = Trb;
        Context->TrbElement.BufferSize = Trb->BufferSize;
        Context->TrbElement.BufferIndex = BufferIndex;
        Context->TrbElement.EndpointPos = EndpointPos;
        Context->TrbElement.Status.IsNoCompletion = 0;
        EnqueueTrb(&Context->Transfer[EndpointPos], &Context->TrbElement);
    }

    // We are done
    Status = STATUS_SUCCESS;

UsbFnMpTransferEnd:
    return Status;
}


//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysAbortTransfer(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction)
{
    UNREFERENCED_PARAMETER(Miniport);
    UNREFERENCED_PARAMETER(EndpointIndex);
    UNREFERENCED_PARAMETER(Direction);

    return STATUS_SUCCESS;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysEventHandler(
    _In_ PVOID Miniport,
    _Out_ PUSBFNMP_MESSAGE Message,
    _Out_ PULONG PayloadSize,
    _Out_ PUSBFNMP_MESSAGE_PAYLOAD Payload)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUSBFNMP_CONTEXT Context = Miniport;
    PCSR_REGISTERS UsbCtrl;
    GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER EVNTCOUNT0;
    GLOBAL_EVENT Event;
    ULONG EventSize;
    ULONG UsbLinkState;

    // Set shortcut
    UsbCtrl = Context->UsbCtrl;

    // No event to report
    *Message = UsbMsgNone;
    *PayloadSize = 0;

    // There can be events we process internally
    while (*Message == UsbMsgNone) {

        // First we have process pending commands
        if (Context->PendingReset || Context->PendingDetach) {

            XferAbort(Context, Message, Payload);
            if (*Message != UsbMsgNone) break;

            if (Context->PendingReset) {
                *Message = UsbMsgBusEventReset;
                Context->PendingReset = FALSE;
                break;
            }

            if (Context->PendingDetach) {
                *Message = UsbMsgBusEventDetach;
                Context->PendingDetach = FALSE;
                break;
            }
        }

        // Read event if there is one
        EVNTCOUNT0.AsUlong32 = INREG32(
            &UsbCtrl->GlobalRegisters.EventBuffers[0].EventCount.AsUlong32
            );
        if (EVNTCOUNT0.Count == 0) break;
        Event = Context->Dma->EventBuffer[Context->EventIndex];
        EventSize = 1;

        if (Event.IsDeviceEvent != 0) {
            switch (Event.DeviceEvent.DeviceEventType) {
            case DeviceEventDisconnectDetected:
                Context->PendingDetach = CleanConnection(Context, TRUE);
                if (!Context->PendingDetach) {
                    *Message = UsbMsgBusEventDetach;
                }
                break;

            case DeviceEventUSBReset:
                Context->PendingReset = CleanConnection(Context, FALSE);
                if (!Context->PendingReset) {
                    *Message = UsbMsgBusEventReset;
                }
                break;

            case DeviceEventConnectionDone:
                ConnectionDone(Context, Message, Payload);
                break;

            case DeviceEventUSBLinkStateChange:
                UsbLinkState = Event.DeviceEvent.EventInformation & 0xf;
                LOG_TRACE("UsbLinkState: %d", UsbLinkState);

                //
                // This indicates the USB link status change in order to
                // track cable plug out & in conditions.
                // When the USB status change, then the current event
                // information contains the actual value that indicates the link state.
                // (6.2.8.2, 6-53 & 6-64 tables)
                //

                if (IsLinkDown(Context->Speed, UsbLinkState)) {
                    *Message = UsbMsgBusEventLinkDown;

                }
                else if (IsLinkUp(Context->Speed, UsbLinkState)) {
                    *Message = UsbMsgBusEventLinkUp;
                }
                break;

            case DeviceEventWakeUpEvent:
            case DeviceEventHibernationRequest:
            case DeviceEventSOF:
            case DeviceEventErraticError:
            case DeviceEventCommandComplete:
            case DeviceEventEventBufferOverflow:
            default:
                break;

            case DeviceEventVendorDeviceTestLMP:
                EventSize = 4;
                break;
            }
        }
        else {
            switch (Event.EndpointEvent.EndpointEventType) {
                case EndpointEventXferComplete:
                    XferComplete(
                        Context, Event.EndpointEvent.TransferComplete,
                        Message, Payload
                        );
                    break;
                case EndpointEventXferInProgress:
                    XferInProgress(
                        Context, Event.EndpointEvent.TransferInProgress,
                        Message, Payload
                        );
                    break;
                case EndpointEventXferNotReady:
                    if (Context->DeviceEventType != NULL) {

                        //
                        //  Report the EP xferNotReady event only on Control Transfer EPs.
                        //

                        if (Event.EndpointEvent.TransferNotReady.PhysicalEndpointNumber < 2) {
                            *Message = UsbMsgSetupPacket;
                            *Context->DeviceEventType = EndpointEventXferNotReady;
                        }
                    }
                    break;
                default:
                case EndpointEventStreamEvent:
                    break;
                case EndpointEventCommandComplete:
                    EndpointCommandComplete(
                        Context, Event.EndpointEvent.Command
                        );
                    break;
            }
        }

        // Confirm event processing to hardware

        Context->EventIndex += EventSize;
        if (Context->EventIndex >= _countof(Context->Dma->EventBuffer)) {
            Context->EventIndex = 0;
        }
        OUTREG32(
            &UsbCtrl->GlobalRegisters.EventBuffers[0].EventCount.AsUlong32,
            sizeof(ULONG) * EventSize
            );

    }

    return Status;
}

//
//------------------------------------------------------------------------------
//
//  This function verifies if a device event has been notified by the controller.
//

NTSTATUS WaitForDeviceEventOnEP(
    _In_ PUSBFNMP_CONTEXT Context,
    _In_ ULONG Event)
{

    ULONG64 WaitTime;
    NTSTATUS Status;
    USBFNMP_MESSAGE Message;
    USBFNMP_MESSAGE_PAYLOAD Payload;
    ULONG PayloadSize;
    BOOLEAN IsEventReceived;
    ENDPOINT_EVENT_TYPE ReportDeviceEvent;

    PayloadSize = sizeof(Payload);
    Status = STATUS_UNSUCCESSFUL;
    ReportDeviceEvent = EndpointEventReserved0;
    Context->DeviceEventType = &ReportDeviceEvent;
    IsEventReceived = FALSE;

    //
    //  Wait for the specific event notification to arrive.
    //

    for (WaitTime = 0; WaitTime < XFER_NOT_READY_TIMEOUT; WaitTime += TIMEOUT_STALL) {

        //
        //  The UsbFnMpSynopsysEventHandler function returns NTSTATUS, but it nevers actually fails,
        //  so the below code is just a protection in case, that this function will be changed.
        //

        Status = UsbFnMpSynopsysEventHandler(Context,
                                             &Message,
                                             &PayloadSize,
                                             &Payload);
        if (!NT_SUCCESS(Status)) {
            break;
        }

        // 
        //  Check if the device event arrived 
        //  Currently, the Control EPs are configured for reporting only three events:
        //  xferComplete, xferInProgress, xferNotReady.
        //  Also, the timeout scenario should not be considered as a failure condition, 
        //  since it means that the Host may not be ready or still processing the payload,
        //  which it can happen on USB connections via hubs, which can result only on 
        //  enumeration failures rather than getting stuck inside of the setup phase forever.
        //

        if (*Context->DeviceEventType == Event) {
            IsEventReceived = TRUE;
            break;
        } 

        KeStallExecutionProcessor(TIMEOUT_STALL);
	}

    if (IsEventReceived == FALSE) {
        LOG_TRACE("Timeout waiting on the device event");
    }
    
    Context->DeviceEventType = NULL;
    return Status;
}

//
//------------------------------------------------------------------------------
//

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysTimeDelay(
    PVOID Miniport,
    PULONG Delta,
    PULONG Base)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    PCSR_REGISTERS UsbCtrl = Context->UsbCtrl;
    DEVICE_STATUS_REGISTER DSTS;
    ULONG Frame;
    ULONG Value;

    DSTS.AsUlong32 = INREG32(&UsbCtrl->DeviceRegisters.Common.Status.AsUlong32);

    Frame = DSTS.uFrameNumber;
    if (Delta == NULL) {
        *Base = Frame;
    } else {
        Value = Frame - (*Base & ((1 << 14) - 1));
        if ((LONG)(Frame - Value) < 0) {        
            if ((*Base & (1 << 15)) == 0) {
                *Base |= (1 << 15);
                *Base += (1 << 16);
            }
        } else {
            *Base &= ~(1 << 15);
        }
        *Delta = ((*Base >> 16) << 14) + Value;
        if ((DSTS.ConnectedSpeed == 0) || (DSTS.ConnectedSpeed == 4)) {
            *Delta *= 125;
        }
        else {
            *Delta *= 1000;
        }
    }

    return STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpSynopsysIsZLPSupported(
    PVOID Miniport,
    PBOOLEAN IsSupported)
{
    PUSBFNMP_CONTEXT Context;
    NTSTATUS Status;

    if ((IsSupported == NULL) || (Miniport == NULL))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpSynopsysIsZLPSupportedEnd;
    }
    
    Context = Miniport;
    Status = STATUS_SUCCESS;
    *IsSupported = FALSE;
    if (Context->IsZLPSupported != FALSE)
    {
        *IsSupported = TRUE;
    }

UsbFnMpSynopsysIsZLPSupportedEnd:
    return Status;
}

//------------------------------------------------------------------------------
