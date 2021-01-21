/*++

Copyright (c) Microsoft Corporation

Module Name:

    UsbFnMp.c

Abstract:

    KdNet UsbFn Miniport implementation for ChipIdea controller

Author:

    Karel Daniheka (kareld)

--*/

#include "pch.h"
#include <logger.h>

//------------------------------------------------------------------------------

#define DMA_BUFFER_SIZE         0x4000
#define DMA_BUFFERS             32

#define DMA_BUFFER_OFFSET       0

#define OEM_BUFFER_SIZE         0x0100

//------------------------------------------------------------------------------

#define DETACH_STALL            20000           // 20 ms

#define STOP_TIMEOUT            1000            // 1 ms
#define RESET_TIMEOUT           1000            // 1 ms
#define PRIME_TIMEOUT           1000            // 1 ms

#define FLUSH_COUNTER           1000000
#define FLUSH_RETRY             8

#define CONFIGURE_TIMEOUT       4000000         // 4 seconds
#define TX_TIMEOUT              1000000         // 1 second
#define RX_TIMEOUT              8000000         // 8 seconds

#define RESTART_COUNTER         4096

//------------------------------------------------------------------------------

#define INREG32(x)              READ_REGISTER_ULONG((PULONG)x)
#define OUTREG32(x, y)          WRITE_REGISTER_ULONG((PULONG)x, y)
#define SETREG32(x, y)          OUTREG32(x, INREG32(x)|(y))
#define CLRREG32(x, y)          OUTREG32(x, INREG32(x)&~(y))
#define MASKREG32(x, y, z)      OUTREG32(x, (INREG32(x)&~(y))|(z))

//------------------------------------------------------------------------------

#define INVALID_INDEX           (ULONG)-1

#ifndef countof
#define countof(x)              (sizeof(x)/sizeof(x[0]))
#endif

//------------------------------------------------------------------------------

#pragma pack(push, 1)

#define MAX_OEM_WRITE_ENTRIES               8
#define DEBUG_DEVICE_OEM_PORT_USBFN         0x0001
#define OEM_SIGNATURE                       'FIX1'

typedef struct {
    UINT16 PortType;
    UINT16 Reserved;
    UINT32 Signature;
    UINT32 WriteCount;
    struct {
        UINT8 BaseAddressRegister;
        UINT8 Reserved;
        UINT16 Offset;
        UINT32 AndValue;
        UINT32 OrValue;
    } Data[MAX_OEM_WRITE_ENTRIES];
} ACPI_DEBUG_DEVICE_OEM_DATA, *PACPI_DEBUG_DEVICE_OEM_DATA;

#pragma pack(pop)

//------------------------------------------------------------------------------

#define HWEPTS                              32

typedef struct _DMA {

    //
    // Given the first endpoint head the controller expects to
    // find an array of endpoint heads.
    // The expected order is the receive endpoint head, followed
    // by the transmit endpoint head, followed by the same pair
    // for the next endpoint: EP0 receive, EP0 transmit, EP1 receive, etc.
    //
    // N.b. This array is expected to begin on a 4K boundary.
    //

    DECLSPEC_ALIGN(4096)
    CHIPIDEA_USB_dQH QH[HWEPTS];

    //
    // Transfer descriptors need 32-byte alignment.
    //

    DECLSPEC_ALIGN(32)
    CHIPIDEA_USB_dTD TD[DMA_BUFFERS];

    DECLSPEC_ALIGN(4096)
    UCHAR Buffers[DMA_BUFFERS][DMA_BUFFER_SIZE];

} DMA_MEMORY, *PDMA_MEMORY;

typedef struct _USBFNMP_CONTEXT {
    PCHIPIDEA_USB_CTRL UsbCtrl;
    PDMA_MEMORY Dma;
    PVOID Gpio;

    PHYSICAL_ADDRESS DmaPa;

    ULONG Epts;
    BOOLEAN Attached;
    BOOLEAN Configured;
    BOOLEAN PendingDetach;
    BOOLEAN PendingReset;
    ULONG AttachTime;

    USBFNMP_BUS_SPEED Speed;

    ULONG FirstBuffer[HWEPTS];
    ULONG LastBuffer[HWEPTS];
    ULONG NextBuffer[DMA_BUFFERS];
    ULONG FreeBuffer;
    ULONG Time[32];

    BOOLEAN WaitForReset;
    ULONG RestartCounter;
    BOOLEAN IsZLPSupported;
} USBFNMP_CONTEXT, *PUSBFNMP_CONTEXT;

//------------------------------------------------------------------------------

//
// This buffer contains the OEM data that is copied from Device PDEBUG_DEVICE_DESCRIPTOR field
//

UCHAR
gOemData[OEM_BUFFER_SIZE];

ULONG
gOemDataLength;

//------------------------------------------------------------------------------

__inline
static
UINT32
TDPhysicalAddress(
    PUSBFNMP_CONTEXT Context,
    ULONG Index
    )
{
    UINT32 PA;

    PA = Context->DmaPa.LowPart + offsetof(DMA_MEMORY, TD);
    PA += Index * sizeof(CHIPIDEA_USB_dTD);
    return PA;
}

//------------------------------------------------------------------------------

__inline
static
ULONG
BufferIndexFromAddress(
    PUSBFNMP_CONTEXT Context,
    PVOID Buffer
    )
{
    ULONG Offset, Index;

    Offset = (ULONG)((PUINT8)Buffer - DMA_BUFFER_OFFSET - (PUINT8)Context->Dma->Buffers);
    Index = Offset / DMA_BUFFER_SIZE;
    if ((Index >= DMA_BUFFERS) || ((Offset - Index * DMA_BUFFER_SIZE) != 0)) {
        Index = INVALID_INDEX;
    }
    return Index;
}

//------------------------------------------------------------------------------

#ifdef DUMP_HARDWARE
static
VOID
DumpHardware(
    PUSBFNMP_CONTEXT Context
    )
{
    PCHIPIDEA_USB_CTRL UsbCtrl = Context->UsbCtrl;
    ULONG Index, BufferIndex, Delta;
    PCHIPIDEA_USB_dTD EpTD;

    LOG_TRACE("===\r\n");

    LOG_TRACE("USBSTS=%08x PORTSC=%08x\r\n",
        INREG32(&UsbCtrl->USBSTS), INREG32(&UsbCtrl->PORTSC[0])
        );

    LOG_TRACE("EPSTATUS=%08x EPCOMPLETE=%08x EPPRIME=%08x\r\n",
        INREG32(&UsbCtrl->ENDPTSTATUS), INREG32(&UsbCtrl->ENDPTCOMPLETE),
        INREG32(&UsbCtrl->ENDPTPRIME)
        );

    LOG_TRACE("...\r\n");

    for (Index = 0; Index < Context->Epts; Index++) {

        LOG_TRACE("EP%d: %d %d %08x\r\n",
            Index, Context->FirstBuffer[Index], Context->LastBuffer[Index],
            Context->Time[Index]
            );

        // Is there pending buffer/transfer?
        BufferIndex = Context->FirstBuffer[Index];
        if (BufferIndex == INVALID_INDEX) continue;

        // Is buffer/transfer completed?
        EpTD = &Context->Dma->TD[BufferIndex];

        UsbFnMpChipideaTimeDelay(Context, &Delta, &Context->Time[Index]);

        LOG_TRACE("EPCTRL=%08x TIME=%08x/%08x\r\n",
            INREG32(&UsbCtrl->ENDPTCTRL[Index >> 1]), Context->Time[Index],
            Delta
            );

        LOG_TRACE("QH[%d]=%08x.%08x.%08x.%08x:%08x.%08x.%08x.%08x.%08x\r\n",
            Index,
            Context->Dma->QH[Index].DATA[0], Context->Dma->QH[Index].DATA[1],
            Context->Dma->QH[Index].DATA[2], Context->Dma->QH[Index].DATA[3],
            Context->Dma->QH[Index].DATA[4], Context->Dma->QH[Index].DATA[5],
            Context->Dma->QH[Index].DATA[6], Context->Dma->QH[Index].DATA[7],
            Context->Dma->QH[Index].DATA[9]
            );

        LOG_TRACE("TD[%08x]=%08x.%08x:%08x.%08x.%08x.%08x.%08x\r\n",
            TDPhysicalAddress(Context, BufferIndex),
            EpTD->DATA[0], EpTD->DATA[1], EpTD->DATA[2], EpTD->DATA[3],
            EpTD->DATA[4], EpTD->DATA[5], EpTD->DATA[6]
            );

        LOG_TRACE("---\r\n");
        }

    LOG_TRACE("...\r\n");
}

#endif

//------------------------------------------------------------------------------

static
ULONG
AbortTransfers(
    PUSBFNMP_CONTEXT Context,
    ULONG Index
    )
{
    ULONG Count = 0;
    ULONG BufferIndex;

    // Remove active flag in all TD's
    BufferIndex = Context->FirstBuffer[Index];
    while (BufferIndex != INVALID_INDEX) {
        PCHIPIDEA_USB_dTD EpTD = &Context->Dma->TD[BufferIndex];
        if ((EpTD->Control & USB_dTD_ACTIVE) != 0) {
            EpTD->Control &= ~USB_dTD_ACTIVE;
            EpTD->Control |= USB_dTD_HALTED;
            }
        Count++;
        BufferIndex = Context->NextBuffer[BufferIndex];
    }
    return Count;
}

//------------------------------------------------------------------------------

static
VOID
HandleDetach(
    PUSBFNMP_CONTEXT Context
    )
{
    PCHIPIDEA_USB_CTRL UsbCtrl = Context->UsbCtrl;
    ULONG Base, Delta;

    //
    // Clear any setup tokens (write a 1 to all bits that are one)
    //
    OUTREG32(&UsbCtrl->ENDPTSETUPSTAT, INREG32(&UsbCtrl->ENDPTSETUPSTAT));

    //
    // Clear endpoint complete registers
    //
    OUTREG32(&UsbCtrl->ENDPTCOMPLETE, INREG32(&UsbCtrl->ENDPTCOMPLETE));

    //
    // Flush all endpoints
    //
    UsbFnMpChipideaTimeDelay(Context, NULL, &Base);
    while (INREG32(&UsbCtrl->ENDPTPRIME) != 0) {
        UsbFnMpChipideaTimeDelay(Context, &Delta, &Base);
        if (Delta > PRIME_TIMEOUT) break;
    }

    OUTREG32(&UsbCtrl->ENDPTFLUSH, 0xFFFFFFFF);
}

//------------------------------------------------------------------------------

static
VOID
HandleReset(
    PUSBFNMP_CONTEXT Context
    )
{
    PCHIPIDEA_USB_CTRL UsbCtrl = Context->UsbCtrl;
    ULONG Base, Delta;

    //
    // Clear any setup tokens (write a 1 to all bits that are one)
    //
    OUTREG32(&UsbCtrl->ENDPTSETUPSTAT, INREG32(&UsbCtrl->ENDPTSETUPSTAT));

    //
    // Clear endpoint complete registers
    //
    OUTREG32(&UsbCtrl->ENDPTCOMPLETE, INREG32(&UsbCtrl->ENDPTCOMPLETE));

    //
    // Flush all endpoints
    //
    UsbFnMpChipideaTimeDelay(Context, NULL, &Base);
    while (INREG32(&UsbCtrl->ENDPTPRIME) != 0) {
        UsbFnMpChipideaTimeDelay(Context, &Delta, &Base);
        if (Delta > PRIME_TIMEOUT) break;
    }
    OUTREG32(&UsbCtrl->ENDPTFLUSH, 0xFFFFFFFF);

    // Wait until reset is done
    UsbFnMpChipideaTimeDelay(Context, NULL, &Base);
    while ((INREG32(&UsbCtrl->PORTSC[0]) & USB_PORTSC_PR) != 0) {
        UsbFnMpChipideaTimeDelay(Context, &Delta, &Base);
        if (Delta > RESET_TIMEOUT) break;
    }

    // Read bus speed
    switch (INREG32(&UsbCtrl->PORTSC[0]) & USB_PORTSC_PSPD) {
        case USB_PORTSC_PSPD_LOW:
            Context->Speed = UsbBusSpeedLow;
            break;
        case USB_PORTSC_PSPD_FULL:
        default:
            Context->Speed = UsbBusSpeedFull;
            break;
        case USB_PORTSC_PSPD_HIGH:
            Context->Speed = UsbBusSpeedHigh;
            break;
    }

}


//------------------------------------------------------------------------------

static
VOID
HardwarePatch(
    PUSBFNMP_CONTEXT Context
    )
{
    ULONG Index;
    PACPI_DEBUG_DEVICE_OEM_DATA OemData = (PVOID)gOemData;
    PUCHAR Address;
    UINT32 Data;


    if ((gOemDataLength < sizeof(ACPI_DEBUG_DEVICE_OEM_DATA)) ||
        (OemData->Signature != OEM_SIGNATURE)) {
        goto HardwarePatchExit;
    }

    for (Index = 0; Index < OemData->WriteCount; Index++) {
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

//------------------------------------------------------------------------------

static
NTSTATUS
RestartController(
    PUSBFNMP_CONTEXT Context
    )
{
    NTSTATUS Status;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    ULONG Count, Base, Delta;
    UINT32 BufferPa;
    ULONG Index;
    ULONG BufferIndex;
    PCHIPIDEA_USB_dQH EpQH;
    PCHIPIDEA_USB_dTD EpTD;

    LOG_TRACE("Chipidea Controller Speed: %d", Context->Speed);
    
    Status = STATUS_NO_SUCH_DEVICE;
    // Shortcut
    UsbCtrl = Context->UsbCtrl;

    // Stop USB engine...
    CLRREG32(&UsbCtrl->USBCMD, USB_USBCMD_RS);
    UsbFnMpChipideaTimeDelay(Context, NULL, &Base);
    while ((INREG32(&UsbCtrl->USBCMD) & USB_USBCMD_RS) != 0) {
        UsbFnMpChipideaTimeDelay(Context, &Delta, &Base);
        if (Delta > STOP_TIMEOUT) {
            goto ResetControllerEnd;
        }
    }

    // Disconnect
    CLRREG32(&UsbCtrl->OTGSC, USB_OTGSC_OT);

    // Reset USB OTG port
    SETREG32(&UsbCtrl->USBCMD, USB_USBCMD_RST);
    UsbFnMpChipideaTimeDelay(Context, NULL, &Base);
    while ((INREG32(&UsbCtrl->USBCMD) & USB_USBCMD_RST) != 0) {
        UsbFnMpChipideaTimeDelay(Context, &Delta, &Base);
        if (Delta > STOP_TIMEOUT) {
            goto ResetControllerEnd;
        }
    }

    // Abort pending transfers if there is any...
    Count = 0;
    for (Index = 0; Index < HWEPTS; Index++) {
        Count += AbortTransfers(Context, Index);
    }

    // All endpoints must not have control type regardless enable
    for (Index = 1; Index < Context->Epts; Index++) {
        OUTREG32(&UsbCtrl->ENDPTCTRL[Index],
                 USB_ENDPTCTRL_TXT_BULK | USB_ENDPTCTRL_RXT_BULK);
        }

    // Set device mode
    OUTREG32(&UsbCtrl->USBMODE, USB_USBMODE_CM_DEVICE | USB_USBMODE_SLOM);

    // Initialize buffer lists, TDs fixed part
    EpTD = &Context->Dma->TD[0];
    BufferPa = Context->DmaPa.LowPart + offsetof(DMA_MEMORY, Buffers);
    for (BufferIndex = 0; BufferIndex < DMA_BUFFERS; BufferIndex++) {
        EpTD->BP[0] = BufferPa;
        EpTD->BP[1] = (BufferPa & ~(PAGE_SIZE - 1)) + 1 * PAGE_SIZE;
        EpTD->BP[2] = (BufferPa & ~(PAGE_SIZE - 1)) + 2 * PAGE_SIZE;
        EpTD->BP[3] = (BufferPa & ~(PAGE_SIZE - 1)) + 3 * PAGE_SIZE;
        EpTD->BP[4] = (BufferPa & ~(PAGE_SIZE - 1)) + 4 * PAGE_SIZE;
        BufferPa += DMA_BUFFER_SIZE;
        EpTD++;
    }

    // Initialize control endpoint Rx
    EpQH = &Context->Dma->QH[0];
    EpQH->Config = (64 << 16);
    EpQH->CurrentTD = 0;
    EpQH->NextTD = USB_dTD_T;
    EpQH->Control = 0;

    // And Tx
    EpQH = &Context->Dma->QH[1];
    EpQH->Config = (64 << 16);
    EpQH->CurrentTD = 0;
    EpQH->NextTD = USB_dTD_T;
    EpQH->Control = 0;

    // Set pointer to list of dQH
    OUTREG32(&UsbCtrl->ENDPOINTLISTADDR, Context->DmaPa.LowPart);

    // Clear all bits
    OUTREG32(&UsbCtrl->USBSTS, INREG32(&UsbCtrl->USBSTS));

    // Apply hardware patch from ACPI DBG2 table
    HardwarePatch(Context);

    // Re-connect
    KeStallExecutionProcessor(DETACH_STALL);

    if (Context->RestartCounter < RESTART_COUNTER) {
        // Attach to Bus
        SETREG32(&UsbCtrl->OTGSC, USB_OTGSC_OT);
        // Start USB engine...
        SETREG32(&UsbCtrl->USBCMD, USB_USBCMD_RS);
        // Increment restart counter
        Context->RestartCounter++;
    }

    // We aren't configured anymore
    Context->Configured = FALSE;
    Context->WaitForReset = TRUE;

    // Done
    Status = STATUS_SUCCESS;

ResetControllerEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaGetMemoryRequirements(
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

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaInitializeLibrary(
    PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    PCHAR LoaderOptions,
    PDEBUG_DEVICE_DESCRIPTOR Device,
    PULONG ContextLength,
    PULONG DmaLength,
    PULONG DmaAlignment)
{
    UNREFERENCED_PARAMETER(ImportTable);
    UNREFERENCED_PARAMETER(LoaderOptions);

    return UsbFnMpChipideaGetMemoryRequirements(Device,
                                                ContextLength,
                                                DmaLength,
                                                DmaAlignment);
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
ULONG
UsbFnMpChipideaGetHardwareContextSize(
    PDEBUG_DEVICE_DESCRIPTOR Device)
{
    ULONG ContextLength;
    ULONG DmaLength;
    ULONG DmaAlignment;

    ContextLength = 0;
    DmaLength = 0;
    DmaAlignment = 0;
    UsbFnMpChipideaGetMemoryRequirements(Device,
                                         &ContextLength,
                                         &DmaLength,
                                         &DmaAlignment);

    ContextLength += (PAGE_SIZE - 1);
    ContextLength &= (PAGE_SIZE - 1);
    DmaLength += (PAGE_SIZE - 1);
    DmaLength &= (PAGE_SIZE - 1);

    return (ContextLength + DmaLength);
}

//------------------------------------------------------------------------------

VOID
UsbFnMpChipideaDeinitializeLibrary()
{
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaStartController(
    PVOID Miniport,
    PDEBUG_DEVICE_DESCRIPTOR Device,
    PVOID DmaBuffer)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    ULONG Index, BufferIndex;

    // Setup miniport context
    if (Device != NULL) {

        Context->UsbCtrl = (PVOID)Device->BaseAddress[0].TranslatedAddress;
        if (Device->BaseAddress[1].Valid) {
            Context->Gpio = (PVOID)Device->BaseAddress[1].TranslatedAddress;
            }
        else
            {
            Context->Gpio = NULL;
            }

        //
        // Data on OemData pointer is accessible only on first initialization,
        // so make our own copy for subsequent initializations.
        //
        if ((gOemDataLength == 0) && (Device->OemData != NULL) &&
            (Device->OemDataLength < OEM_BUFFER_SIZE)) {
            RtlCopyMemory(gOemData, Device->OemData, Device->OemDataLength);
            gOemDataLength = Device->OemDataLength;
        }

        Context->Dma = DmaBuffer;
        Context->DmaPa = KdGetPhysicalAddress(DmaBuffer);
    }

    Context->Attached = FALSE;
    Context->Configured = FALSE;
    Context->PendingReset = FALSE;
    Context->PendingDetach = FALSE;
    Context->Speed = UsbBusSpeedUnknown;

    Context->WaitForReset = FALSE;
    Context->RestartCounter = 0;

    RtlZeroMemory(Context->Dma, sizeof(DMA_MEMORY));

    for (BufferIndex = 0; BufferIndex < DMA_BUFFERS; BufferIndex++) {
        Context->NextBuffer[BufferIndex] = BufferIndex + 1;
    }
    Context->NextBuffer[DMA_BUFFERS - 1] = INVALID_INDEX;
    Context->FreeBuffer = 0;

    for (Index = 0; Index < HWEPTS; Index++) {
        Context->FirstBuffer[Index] = INVALID_INDEX;
        Context->LastBuffer[Index] = INVALID_INDEX;
    }

    // Controller registers address shortcut
    UsbCtrl = Context->UsbCtrl;

    // Check if we can detect controller and it support device mode
    if (((INREG32(&UsbCtrl->ID) & USB_ID_MASK) != USB_ID_VALUE) ||
        ((INREG32(&UsbCtrl->HWDEVICE) & USB_HWDEVICE_DC) == 0)) {
        Status = STATUS_NO_SUCH_DEVICE;
        goto UsbFnMpStartControllerEnd;
    }

    // Enable USB GP Timer 0
    OUTREG32(&UsbCtrl->GPTIMER0LD, 0x00FFFFFF);
    OUTREG32(&UsbCtrl->GPTIMER0CTRL, USB_GPT_RUN | USB_GPT_REPEAT);

    // Read number of supported endpoints
    Context->Epts = (INREG32(&UsbCtrl->HWDEVICE) & USB_HWDEVICE_EP_MASK)
        >> USB_HWDEVICE_EP_SHIFT;

    // And finally start hardware
    Status = RestartController(Context);

UsbFnMpStartControllerEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaStopController(
    PVOID Miniport)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    ULONG Base, Delta;

    // Controller registers address shortcut
    UsbCtrl = Context->UsbCtrl;

    // Stop USB engine...
    CLRREG32(&UsbCtrl->USBCMD, USB_USBCMD_RS);
    UsbFnMpChipideaTimeDelay(Context, NULL, &Base);
    while ((INREG32(&UsbCtrl->USBCMD) & USB_USBCMD_RS) != 0) {
        UsbFnMpChipideaTimeDelay(Context, &Delta, &Base);
        if (Delta > STOP_TIMEOUT) break;
    }

    // Disconnect
    CLRREG32(&UsbCtrl->OTGSC, USB_OTGSC_OT);

    // Done
    return STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaAllocateTransferBuffer(
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

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaFreeTransferBuffer(
    PVOID Miniport,
    PVOID TransferBuffer)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    ULONG BufferIndex;

    Status = STATUS_INVALID_PARAMETER;
    BufferIndex = BufferIndexFromAddress(Context, TransferBuffer);
    if (BufferIndex == INVALID_INDEX) {
        goto UsbFnMpFreeTransferBufferEnd;
    }

    // Check if buffer isn't in transfer
    if (Context->NextBuffer[BufferIndex] != INVALID_INDEX) {
        goto UsbFnMpFreeTransferBufferEnd;
    }

    // Add it to free ones...
    Context->NextBuffer[BufferIndex] = Context->FreeBuffer;
    Context->FreeBuffer = BufferIndex;

    Status = STATUS_SUCCESS;

UsbFnMpFreeTransferBufferEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaConfigureEnableEndpoints(
    PVOID Miniport,
    PUSBFNMP_DEVICE_INFO DeviceInfo)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    PUSBFNMP_CONFIGURATION_INFO ConfigurationInfo;
    ULONG Interfaces, InterfaceIndex;
    PUSBFNMP_INTERFACE_INFO InterfaceInfo;
    ULONG Endpoints, EndpointIndex;
    PUSB_ENDPOINT_DESCRIPTOR Descriptor;
    ULONG Index;
    PCHIPIDEA_USB_dQH EpQH;
    UINT32 Data;

    // There should be at least one configuration
    if (DeviceInfo->DeviceDescriptor->bNumConfigurations < 1) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpConfigureEnableEndpointsEnd;
    }

    // Shortcut
    UsbCtrl = Context->UsbCtrl;

    // Iterate over all interfaces in first configuration.
    ConfigurationInfo = DeviceInfo->ConfigurationInfoTable[0];
    Interfaces = ConfigurationInfo->ConfigDescriptor->bNumInterfaces;
    for (InterfaceIndex = 0; InterfaceIndex < Interfaces; InterfaceIndex++) {
        InterfaceInfo = ConfigurationInfo->InterfaceInfoTable[InterfaceIndex];

        // Iterate over all endpoints in this interface.
        Endpoints = InterfaceInfo->InterfaceDescriptor->bNumEndpoints;
        for (EndpointIndex = 0; EndpointIndex < Endpoints; EndpointIndex++) {
            Descriptor = InterfaceInfo->EndpointDescriptorTable[EndpointIndex];

            // Determine associated QH and bit mask
            Index = (Descriptor->bEndpointAddress & 0x0F) << 1;
            if (USB_ENDPOINT_DIRECTION_IN(Descriptor->bEndpointAddress)) {
                Index++;
            }

            // Initialize QH
            EpQH = &Context->Dma->QH[Index];
            EpQH->Config = (Descriptor->wMaxPacketSize << 16);
            EpQH->CurrentTD = 0;
            EpQH->NextTD = USB_dTD_T;
            EpQH->Control = 0;

            if (USB_ENDPOINT_DIRECTION_OUT(Descriptor->bEndpointAddress)) {
                EpQH->Config |= USB_dQH_ZLT;
                Context->IsZLPSupported = TRUE;
            }

            // Configure and enable end point
            Index = (Descriptor->bEndpointAddress & 0x0F);
            if (Index >= Context->Epts) {
                Status = STATUS_INVALID_PARAMETER;
                goto UsbFnMpConfigureEnableEndpointsEnd;
            }
            if (USB_ENDPOINT_DIRECTION_IN(Descriptor->bEndpointAddress)) {
                Data  = INREG32(&UsbCtrl->ENDPTCTRL[Index]) & 0x0000FFFF;
                Data |= USB_ENDPTCTRL_TXE | USB_ENDPTCTRL_TXR;
                Data |= (Descriptor->bmAttributes & 0x03) << 18;
                OUTREG32(&UsbCtrl->ENDPTCTRL[Index], Data);
            } else {
                Data = INREG32(&UsbCtrl->ENDPTCTRL[Index]) & 0xFFFF0000;
                Data |= USB_ENDPTCTRL_RXE | USB_ENDPTCTRL_RXR;
                Data |= (Descriptor->bmAttributes & 0x03) << 2;
                OUTREG32(&UsbCtrl->ENDPTCTRL[Index], Data);
            }

        }
    }

    // We are now configured
    Context->Configured = TRUE;

    // Done
    Status = STATUS_SUCCESS;

UsbFnMpConfigureEnableEndpointsEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaGetEndpointMaxPacketSize(
    PVOID Miniport,
    UINT8 EndpointType,
    USB_DEVICE_SPEED BusSpeed,
    PUINT16 MaxPacketSize)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Miniport);

    switch (BusSpeed) {
        case UsbLowSpeed:
        case UsbSuperSpeed:
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
    }

    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaGetMaxTransferSize(
    PVOID Miniport,
    PULONG MaxTransferSize)
{
    UNREFERENCED_PARAMETER(Miniport);

    *MaxTransferSize = DMA_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaGetEndpointStallState(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    PBOOLEAN Stalled)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    UINT32 Data;

    UsbCtrl = Context->UsbCtrl;
    if (EndpointIndex == 0) {
        Data = INREG32(&UsbCtrl->ENDPTCTRL[0]);
        *Stalled = (Data & (USB_ENDPTCTRL_TXS | USB_ENDPTCTRL_RXS)) != 0;
    } else if (Direction == UsbEndpointDirectionDeviceRx) {
        Data = INREG32(&UsbCtrl->ENDPTCTRL[EndpointIndex]);
        *Stalled = (Data & USB_ENDPTCTRL_RXS) != 0;
    } else {
        Data = INREG32(&UsbCtrl->ENDPTCTRL[EndpointIndex]);
        *Stalled = (Data & USB_ENDPTCTRL_TXS) != 0;
    }

    return STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaSetEndpointStallState(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    BOOLEAN StallEndPoint)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;

    UsbCtrl = Context->UsbCtrl;
    if (EndpointIndex == 0) {
        if (StallEndPoint) {
           SETREG32(
               &UsbCtrl->ENDPTCTRL[0], USB_ENDPTCTRL_TXS | USB_ENDPTCTRL_RXS
               );
        } else {
            MASKREG32(
                &UsbCtrl->ENDPTCTRL[0], USB_ENDPTCTRL_TXS | USB_ENDPTCTRL_RXS,
                USB_ENDPTCTRL_TXR | USB_ENDPTCTRL_RXR
                );
        }
    } else if (Direction == UsbEndpointDirectionDeviceRx) {
        if (StallEndPoint) {
            SETREG32(&UsbCtrl->ENDPTCTRL[EndpointIndex], USB_ENDPTCTRL_RXS);
        } else {
            MASKREG32(
                &UsbCtrl->ENDPTCTRL[EndpointIndex], USB_ENDPTCTRL_RXS,
                USB_ENDPTCTRL_RXR
                );
        }
    } else {
        if (StallEndPoint) {
            SETREG32(&UsbCtrl->ENDPTCTRL[EndpointIndex], USB_ENDPTCTRL_TXS);
        } else {
            MASKREG32(
                &UsbCtrl->ENDPTCTRL[EndpointIndex], USB_ENDPTCTRL_TXS,
                USB_ENDPTCTRL_TXR
                );
        }
    }

    return STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaTransfer(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction,
    PULONG BufferSize,
    PVOID Buffer)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    ULONG Index;
    ULONG BufferIndex, FirstBufferIndex, LastBufferIndex;
    PCHIPIDEA_USB_dQH EpQH;
    PCHIPIDEA_USB_dTD EpTD;
    UINT32 PrimeMask;
    ULONG Base, Delta;

    // Shortcut
    UsbCtrl = Context->UsbCtrl;

    // Get buffer index
    BufferIndex = BufferIndexFromAddress(Context, Buffer);
    if ((BufferIndex == INVALID_INDEX) ||
        (Context->NextBuffer[BufferIndex] != INVALID_INDEX)) {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpTransferEnd;
    }

    // Get index (address) and prime mask
    Index = EndpointIndex << 1;
    if (Direction == UsbEndpointDirectionDeviceTx) {
        Index++;
        PrimeMask = 1 << (EndpointIndex + 16);
    } else {
        PrimeMask = 1 << EndpointIndex;
    }

    // Update TD
    EpTD = &Context->Dma->TD[BufferIndex];
    EpTD->NextTD = USB_dTD_T;
    EpTD->Control = ((*BufferSize) << 16) | USB_dTD_ACTIVE;
    EpTD->BP[0] &= ~USB_dTD_OFFSET_MASK;
    EpTD->BP[0] |= ((UINT32)Buffer) & USB_dTD_OFFSET_MASK;
    EpTD->BP[1] &= ~USB_dTD_OFFSET_MASK;
    EpTD->BP[2] &= ~USB_dTD_OFFSET_MASK;
    EpTD->BP[3] &= ~USB_dTD_OFFSET_MASK;
    EpTD->BP[4] &= ~USB_dTD_OFFSET_MASK;
    EpTD->UserData = *BufferSize;

    // Depending if endpoint queue is empty or not
    Context->NextBuffer[BufferIndex] = INVALID_INDEX;
    FirstBufferIndex = Context->FirstBuffer[Index];
    if (FirstBufferIndex != INVALID_INDEX) {

        // Link buffer
        LastBufferIndex = Context->LastBuffer[Index];
        Context->NextBuffer[LastBufferIndex] = BufferIndex;
        Context->LastBuffer[Index] = BufferIndex;

        // Attach TD
        EpTD = &Context->Dma->TD[LastBufferIndex];
        EpTD->NextTD = TDPhysicalAddress(Context, BufferIndex);

        // Check if there is need to prime end point
        if ((INREG32(&UsbCtrl->ENDPTPRIME) & PrimeMask) == 0) {
            UINT32 StatusData;

            UsbFnMpTimeDelay(Miniport, NULL, &Base);
            do {
                SETREG32(&UsbCtrl->USBCMD, USB_USBCMD_ATDTW);
                StatusData = INREG32(&UsbCtrl->ENDPTSTATUS);
                UsbFnMpTimeDelay(Miniport, &Delta, &Base);
                if (Delta > PRIME_TIMEOUT) {
                    RestartController(Context);
                    break;
                }
            } while ((INREG32(&UsbCtrl->USBCMD) & USB_USBCMD_ATDTW) == 0);
            CLRREG32(&UsbCtrl->USBCMD, USB_USBCMD_ATDTW);

            if ((StatusData & PrimeMask) == 0) {
                EpQH = &Context->Dma->QH[Index];
                EpQH->NextTD = TDPhysicalAddress(Context, BufferIndex);
                EpQH->Control = 0;

                // Prime/start endpoint & wait until done...
                OUTREG32(&UsbCtrl->ENDPTCOMPLETE, PrimeMask);
                OUTREG32(&UsbCtrl->ENDPTPRIME, PrimeMask);
            }
        }

    } else {

        // Link buffer
        Context->FirstBuffer[Index] = BufferIndex;
        Context->LastBuffer[Index] = BufferIndex;

        EpQH = &Context->Dma->QH[Index];
        EpQH->NextTD = TDPhysicalAddress(Context, BufferIndex);
        EpQH->Control = 0;

        // Prime/start endpoint & wait until done...
        OUTREG32(&UsbCtrl->ENDPTCOMPLETE, PrimeMask);
        OUTREG32(&UsbCtrl->ENDPTPRIME, PrimeMask);

    }

    // Update activity time stamp on end point
    UsbFnMpTimeDelay(Miniport, NULL, &Context->Time[Index]);

    // We are done
    Status = STATUS_SUCCESS;

UsbFnMpTransferEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaAbortTransfer(
    PVOID Miniport,
    UINT8 EndpointIndex,
    USBFNMP_ENDPOINT_DIRECTION Direction)
{
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    ULONG Index, Retry, Counter;
    UINT32 PrimeMask;

    // Shortcus
    UsbCtrl = Context->UsbCtrl;

    Index = EndpointIndex << 1;
    if (Direction == UsbEndpointDirectionHostIn) {
        Index++;
        PrimeMask = 1 << (EndpointIndex + 16);
    } else {
        PrimeMask = 1 << EndpointIndex;
    }

    Retry = 0;
    do {
        OUTREG32(&UsbCtrl->ENDPTFLUSH, PrimeMask);
        Counter = 0;
        while (INREG32(&UsbCtrl->ENDPTFLUSH) == 0)
            {
            if (Counter++ > FLUSH_COUNTER) break;
            }
        if (Retry++ > FLUSH_RETRY) break;
    } while ((INREG32(&UsbCtrl->ENDPTSTATUS) & PrimeMask) != 0);

    // Abort transfers if they exist
    AbortTransfers(Context, Index);

    return STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaEventHandler(
    PVOID Miniport,
    PUSBFNMP_MESSAGE Message,
    PULONG PayloadSize,
    PUSBFNMP_MESSAGE_PAYLOAD Payload)
{
    NTSTATUS Status;
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl;
    UINT32 UsbStatus;
    UINT32 PortSc;
    ULONG Index, Delta;


    // Set shortcut
    UsbCtrl = Context->UsbCtrl;

    //
    // First look for any completed transfer. It should be done before
    // looking to pending port change as there can be finished transfer
    // we want report before port change one. We also ignore hardware
    // complete bits as we can have our own queue...
    //
    for (Index = 0; Index < Context->Epts; Index++) {
        PCHIPIDEA_USB_dQH EpQH;
        PCHIPIDEA_USB_dTD EpTD;
        ULONG BufferIndex;
        ULONG NextBufferIndex;
        UINT32 Control;
        UINT32 Mask;

        // Is there pending buffer/transfer?
        BufferIndex = Context->FirstBuffer[Index];
        if (BufferIndex == INVALID_INDEX) continue;

        // Is buffer/transfer completed?
        EpTD = &Context->Dma->TD[BufferIndex];
        Control = EpTD->Control;
        if ((Control & USB_dTD_ACTIVE) != 0) {

            UsbFnMpTimeDelay(Miniport, &Delta, &Context->Time[Index]);
            if (((Index & 1) != 0) && (Delta > TX_TIMEOUT)) {
                RestartController(Context);
                *Message = UsbMsgNone;
                *PayloadSize = 0;
                Status = STATUS_SUCCESS;
                goto UsbFnMpEventHandlerEnd;
            }
            continue;
        }

        // Remove complete buffer/transfer from endpoint list
        NextBufferIndex = Context->NextBuffer[BufferIndex];
        Context->FirstBuffer[Index] = NextBufferIndex;
        if (NextBufferIndex == INVALID_INDEX) {
            Context->LastBuffer[Index] = INVALID_INDEX;
        }
        Context->NextBuffer[BufferIndex] = INVALID_INDEX;

        // Fill message payload
        Payload->utr.EndpointIndex = (UINT8)(Index >> 1);
        Payload->utr.Buffer = &Context->Dma->Buffers[BufferIndex][0];
        if ((Control & USB_dTD_STATUS_MASK) == 0) {
            Payload->utr.BytesTransferred = EpTD->UserData - (Control >> 16);
            Payload->utr.TransferStatus = UsbTransferStatusComplete;
        } else {
            Payload->utr.BytesTransferred = 0;
            Payload->utr.TransferStatus = UsbTransferStatusAborted;
            if (NextBufferIndex != INVALID_INDEX) {

                if ((Index & 1) == 0) {
                    Mask = 1 << (Index >> 1);
                } else {
                    Mask = 1 << (16 + (Index >> 1));
                }

                EpQH = &Context->Dma->QH[Index];
                EpQH->NextTD = TDPhysicalAddress(Context, NextBufferIndex);
                EpQH->Control = 0;

                // Prime/start endpoint & wait until done...
                OUTREG32(&UsbCtrl->ENDPTCOMPLETE, Mask);
                OUTREG32(&UsbCtrl->ENDPTPRIME, Mask);
            }
        }
        if ((Index & 1) == 0) {
            *Message = UsbMsgEndpointStatusChangedRx;
            Payload->utr.Direction = UsbEndpointDirectionDeviceRx;
        } else {
            *Message = UsbMsgEndpointStatusChangedTx;
            Payload->utr.Direction = UsbEndpointDirectionDeviceTx;
        }

        // Set transfer start
        UsbFnMpTimeDelay(Miniport, NULL, &Context->Time[Index]);

        // And we are done...
        *PayloadSize = sizeof(Payload->utr);
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    // There can be USD detach pending...
    if (Context->PendingDetach) {
        Context->PendingDetach = FALSE;
        Context->Attached = FALSE;
        Context->Configured = FALSE;
        Context->WaitForReset = FALSE;
        *Message = UsbMsgBusEventDetach;
        *PayloadSize = 0;
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    // There can be USB reset pending...
    if (Context->PendingReset) {
        Context->PendingReset = FALSE;
        Context->Configured = FALSE;
        Context->WaitForReset = FALSE;
        *Message = UsbMsgBusEventReset;
        *PayloadSize = 0;
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    // Get port status
    UsbStatus = INREG32(&UsbCtrl->USBSTS);

    // Check for attach/detach. It should be done before check for reset...
    // Ideal world solution will only check for CCS bit. However on some
    // hardware (QRD8x26) it doesn't work and this bit always set.
    // In addition to this problem SUSP bit apparently oscilate when
    // device isn't connected to bus. To workaround this problem
    // we will use reset URI to detect attach.

    PortSc = INREG32(&UsbCtrl->PORTSC[0]);

#if USE_CCS_FOR_ATTACH
    if (((PortSc & USB_PORTSC_CCS) != 0) &&
        ((PortSc & USB_PORTSC_SUSP) == 0) &&
        !Context->Attached) {

        Context->Attached = TRUE;
        UsbFnMpTimeDelay(Miniport, NULL, &Context->AttachTime);
        *Message = UsbMsgBusEventAttach;
        *PayloadSize = 0;
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }
#else
    if (!Context->Attached && ((UsbStatus & USB_USBSTS_URI) != 0)) {

        Context->Attached = TRUE;
        UsbFnMpTimeDelay(Miniport, NULL, &Context->AttachTime);
        *Message = UsbMsgBusEventAttach;
        *PayloadSize = 0;
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }
#endif

    // Or is it deattach event?
    if ((((PortSc & USB_PORTSC_CCS) == 0) || ((PortSc & USB_PORTSC_SUSP) != 0))
        && Context->Attached) {
        ULONG Transfers = 0;

        // Handle detach
        HandleDetach(Context);

        // Abort pending transfers if there is any...
        for (Index = 0; Index < HWEPTS; Index++) {
            Transfers += AbortTransfers(Context, Index);
        }

        // If there is pending transfer postpone event
        if (Transfers > 0) {
            Context->PendingDetach = TRUE;
            *Message = UsbMsgNone;
            *PayloadSize = 0;
            Status = STATUS_SUCCESS;
            goto UsbFnMpEventHandlerEnd;
        }

        // We aren't attached nor configured
        Context->Attached = FALSE;
        Context->Configured = FALSE;
        Context->WaitForReset = FALSE;

        *Message = UsbMsgBusEventDetach;
        *PayloadSize = 0;

        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    // Check for bus reset
    if ((UsbStatus & USB_USBSTS_URI) != 0) {
        ULONG Transfers = 0;

        // Clear status bit & handle reset
        OUTREG32(&UsbCtrl->USBSTS, USB_USBSTS_URI);
        HandleReset(Context);

        // Abort pending transfers if there is any...
        for (Index = 0; Index < 32; Index++) {
            Transfers += AbortTransfers(Context, Index);
        }

        // If there is pending transfer postpone event
        if (Transfers > 0) {
            Context->PendingReset = TRUE;
            *Message = UsbMsgNone;
            *PayloadSize = 0;
            Status = STATUS_SUCCESS;
            goto UsbFnMpEventHandlerEnd;
        }

        // We aren't configured anymore
        Context->Configured = FALSE;
        Context->WaitForReset = FALSE;

        *Message = UsbMsgBusEventReset;
        *PayloadSize = 0;
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    if (Context->Speed != UsbBusSpeedUnknown) {
        *Message = UsbMsgBusEventSpeed;
        Payload->ubs = Context->Speed;
        *PayloadSize = sizeof(Payload->ubs);
        Context->Speed = UsbBusSpeedUnknown;
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    // Check for setup packet
    if ((INREG32(&UsbCtrl->ENDPTSETUPSTAT) & 0x1) != 0) {
        PCHIPIDEA_USB_dQH EpQH = &Context->Dma->QH[0];

        OUTREG32(&UsbCtrl->ENDPTSETUPSTAT, 0x1);
        while ((INREG32(&UsbCtrl->ENDPTSETUPSTAT) & 0x1) != 0);
        do {
            SETREG32(&UsbCtrl->USBCMD, USB_USBCMD_SUTW);
            ((PUINT32)&Payload->udr)[0] = EpQH->SB[0];
            ((PUINT32)&Payload->udr)[1] = EpQH->SB[1];
        } while ((INREG32(&UsbCtrl->USBCMD) & USB_USBCMD_SUTW) == 0);

        *Message = UsbMsgSetupPacket;
        *PayloadSize = sizeof(Payload->udr);
        if ((Payload->udr.bmRequestType.Type == BMREQUEST_STANDARD) &&
            (Payload->udr.bRequest == USB_REQUEST_SET_ADDRESS)) {
            //
            // Set address, use hardware acceleration.
            //
            USHORT address = Payload->udr.wValue.W;
            OUTREG32(&UsbCtrl->USBADR, (address << 25) | USB_USBADR_ADRA);
        }
        Status = STATUS_SUCCESS;
        goto UsbFnMpEventHandlerEnd;
    }

    // No event to report
    *Message = UsbMsgNone;
    *PayloadSize = 0;
    Status = STATUS_SUCCESS;

UsbFnMpEventHandlerEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaTimeDelay(
    PVOID Miniport,
    PULONG Delta,
    PULONG Base)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUSBFNMP_CONTEXT Context = Miniport;
    PCHIPIDEA_USB_CTRL UsbCtrl = Context->UsbCtrl;
    UINT32 Counter;

    if (Base == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto HwIfcChipIdeaGetTimeEnd;
    }

    Counter = INREG32(&UsbCtrl->GPTIMER0CTRL) & USB_GPT_MASK;
    if (Delta != NULL) {
        *Delta = (*Base - Counter) & USB_GPT_MASK;
    } else {
        *Base = Counter;
    }

HwIfcChipIdeaGetTimeEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
UsbFnMpChipideaIsZLPSupported(
    PVOID Miniport,
    PBOOLEAN IsSupported)
{
    PUSBFNMP_CONTEXT Context;
    NTSTATUS Status;

    if ((IsSupported == NULL) || (Miniport == NULL))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto UsbFnMpChipideaIsZLPSupportedEnd;
    }
    
    Context = Miniport;
    Status = STATUS_SUCCESS;
    *IsSupported = FALSE;
    if (Context->IsZLPSupported != FALSE)
    {
        *IsSupported = TRUE;
    }

UsbFnMpChipideaIsZLPSupportedEnd:
    return Status;
}

//------------------------------------------------------------------------------
