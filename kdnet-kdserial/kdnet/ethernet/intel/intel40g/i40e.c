/**************************************************************************

Copyright (c) 2001-2019, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

/** @file
  FVL specific functions implementation.

  Contains hardware initialization functions, implementation of C Shared code
  defined memory management functions, internal register read/write functions.
  Also contains implementation hw dependant functionality used internally
  by UNDI commands.

  Copyright (C) 2016 Intel Corporation. <BR>
  All rights reserved. This software and associated documentation
  (if any) is furnished undr a license and may only be used or
  copied in accordance with the terms of the license. Except as
  permitted by such license, no part of this software or
  documentation may be reproduced, stored in a retrieval system, or
  transmitted in any form or by any means without the express
  written consent of Intel Corporation.

**/

#include "I40e.h"
#include <intrin.h>

#ifndef INTEL_KDNET
#include "EepromConfig.h"
#endif
#include "DeviceSupport.h"

#ifndef INTEL_KDNET
extern EFI_DRIVER_BINDING_PROTOCOL gUndiDriverBinding;
#endif
extern EFI_GUID                    gEfiStartStopProtocolGuid;
//
// Global variables for blocking IO
//
STATIC BOOLEAN          gInitializeLock = TRUE;
#ifndef INTEL_KDNET
STATIC EFI_LOCK         gLock;
#endif

VOID
i40eBlockIt(
    IN  I40E_DRIVER_DATA  *AdapterInfo,
    IN  UINT32            flag
);


#ifdef INTEL_KDNET

VOID
KdNetUNDIMapMem (
    UINT64 Context,
    UINT64 VirtualAddress,
    UINT32 Size,
    UINT32 Direction,
    UINT64 MappedAddressPtr);

VOID
i40eMapMem(
    IN I40E_DRIVER_DATA  *AdapterInfo,
    IN UINT64            VirtualAddress,
    IN UINT32            Size,
    OUT UINT64           *MappedAddress
)
/*++

Routine Description:
Maps a virtual address to a physical address.  This is necessary for runtime functionality and also
on some platforms that have a chipset that cannot allow DMAs above 4GB.

Arguments:
AdapterInfo                   - Pointer to the NIC data structure information
which the UNDI driver is layering on.
VirtualAddress                   - Virtual Address of the data buffer to map.
Size                             - Minimum size of the buffer to map.
MappedAddress                    - Pointer to store the address of the mapped buffer.
Returns:

--*/
{
    if (AdapterInfo->Map_Mem != NULL) {
        *MappedAddress = 0;
        (*AdapterInfo->Map_Mem) (
            AdapterInfo->Unique_ID,
            VirtualAddress,
            Size,
            TO_DEVICE,
            (UINT64)MappedAddress
            );
        //
        // Workaround: on some systems mapmem may not be implemented correctly, so just
        // pass back the original address
        //
        if (*MappedAddress == 0) {
            *MappedAddress = VirtualAddress;
        }
    }
    else {
        //*MappedAddress = (KdGetPhysicalAddress((VOID *)(UINTN)VirtualAddress)).QuadPart;
        KdNetUNDIMapMem((UINT64)(UINTN)NULL, VirtualAddress, Size, TO_DEVICE, (UINT64)(UINTN)(VOID *)MappedAddress);
        if (*MappedAddress == 0) {
            *MappedAddress = VirtualAddress;
        }
    }
}
#endif


#ifdef INTEL_KDNET
VOID
i40eUnMapMem(
    IN I40E_DRIVER_DATA  *AdapterInfo,
    IN UINT64            VirtualAddress,
    IN UINT32            Size,
    IN UINT64            MappedAddress
)
/*++

Routine Description:
Maps a virtual address to a physical address.  This is necessary for runtime functionality and also
on some platforms that have a chipset that cannot allow DMAs above 4GB.

Arguments:
XgbeAdapter                   - Pointer to the NIC data structure information
which the UNDI driver is layering on.
VirtualAddress                   - Virtual Address of the data buffer to map.
Size                             - Minimum size of the buffer to map.
MappedAddress                    - Pointer to store the address of the mapped buffer.
Returns:

--*/
{
    if (AdapterInfo->UnMap_Mem != NULL) {
        (*AdapterInfo->UnMap_Mem) (
            AdapterInfo->Unique_ID,
            VirtualAddress,
            Size,
            TO_DEVICE,
            (UINT64)MappedAddress
            );
    }
}
#endif


/**
  This is the drivers copy function so it does not need to rely on the BootServices
  copy which goes away at runtime. This copy function allows 64-bit or 32-bit copies
  depending on platform architecture.  On Itanium we must check that both addresses
  are naturally aligned before attempting a 64-bit copy.

  @param[in]  Dest    Destination memory pointer.
  @param[in]  Source  Source memory pointer.
  @param[in]  Count   Number of bytes to copy.

  @retval     VOID
**/
VOID
i40eMemCopy(
    IN  UINT8   *Dest,
    IN  UINT8   *Source,
    IN  UINT32  Count
)
{
    UINT32  BytesToCopy;
    UINT32  IntsToCopy;
    UINTN   *SourcePtr;
    UINTN   *DestPtr;
    UINT8   *SourceBytePtr;
    UINT8   *DestBytePtr;

    IntsToCopy = Count / sizeof(UINTN);
    BytesToCopy = Count % sizeof(UINTN);
#ifdef EFI64
    //
    // Itanium cannot handle memory accesses that are not naturally aligned.  Determine
    // if 64-bit copy is even possible with these start addresses.
    //
    if (((((UINTN)Source) & 0x0007) != 0) || ((((UINTN)Dest) & 0x0007) != 0)) {
        IntsToCopy = 0;
        BytesToCopy = Count;
    }
#endif

    SourcePtr = (UINTN *)Source;
    DestPtr = (UINTN *)Dest;

    while (IntsToCopy > 0) {
        *DestPtr = *SourcePtr;
        SourcePtr++;
        DestPtr++;
        IntsToCopy--;
    }

    //
    // Copy the leftover bytes.
    //
    SourceBytePtr = (UINT8 *)SourcePtr;
    DestBytePtr = (UINT8 *)DestPtr;
    while (BytesToCopy > 0) {
        *DestBytePtr = *SourceBytePtr;
        SourceBytePtr++;
        DestBytePtr++;
        BytesToCopy--;
    }
}


//
// Each context sub-line consists of 128 bits (16 bytes) of data
//
#define SUB_LINE_LENGTH         0x10
#define LANCTXCTL_QUEUE_TYPE_TX 0x1
#define LANCTXCTL_QUEUE_TYPE_RX 0x0

#ifndef INTEL_KDNET

EFI_STATUS
HMCDump(
    I40E_DRIVER_DATA *AdapterInfo,
    UINT16           QueueNumber,
    enum i40e_hmc_lan_rsrc_type hmc_type
)
{
    UINT32   ControlReg;
    UINT32   byte_length = 0;
    UINT32   queue_type = 0;
    UINT32   sub_line = 0;

    switch (hmc_type)
    {
    case I40E_HMC_LAN_RX:
        byte_length = I40E_HMC_OBJ_SIZE_RXQ;
        queue_type = LANCTXCTL_QUEUE_TYPE_RX;
        break;
    case I40E_HMC_LAN_TX:
        byte_length = I40E_HMC_OBJ_SIZE_TXQ;
        queue_type = LANCTXCTL_QUEUE_TYPE_TX;
        break;
    default:
        return EFI_DEVICE_ERROR;
    }

#ifdef FORTVILLE_A0_SUPPORT
    Print(L"I40E_GLGEN_CSR_DEBUG_C_CSR_ADDR_PROT_MASK value before %x\n",
        i40eRead32(AdapterInfo, I40E_GLGEN_CSR_DEBUG_C));
    i40eWrite32(AdapterInfo, I40E_GLGEN_CSR_DEBUG_C, i40eRead32(AdapterInfo,
        I40E_GLGEN_CSR_DEBUG_C) & (~(I40E_GLGEN_CSR_DEBUG_C_CSR_ADDR_PROT_MASK)));
    Print(L"I40E_GLGEN_CSR_DEBUG_C_CSR_ADDR_PROT_MASK value after %x\n",
        i40eRead32(AdapterInfo, I40E_GLGEN_CSR_DEBUG_C));
#endif

    for (sub_line = 0; sub_line < (byte_length / SUB_LINE_LENGTH); sub_line++)
    {
        ControlReg = ((UINT32)QueueNumber << I40E_PFCM_LANCTXCTL_QUEUE_NUM_SHIFT)
            | ((UINT32)queue_type << I40E_PFCM_LANCTXCTL_QUEUE_TYPE_SHIFT)
            | ((UINT32)sub_line << I40E_PFCM_LANCTXCTL_SUB_LINE_SHIFT)
            | ((UINT32)0 << I40E_PFCM_LANCTXCTL_OP_CODE_SHIFT);

#ifdef FORTVILLE_A0_SUPPORT
        i40eWrite32(AdapterInfo, I40E_PFCM_LANCTXCTL(AdapterInfo->Function), ControlReg);
#else
        i40eWrite32(AdapterInfo, I40E_PFCM_LANCTXCTL, ControlReg);
#endif
        Print(L"I40E_PFCM_LANCTXCTL = %x\n", ControlReg);
#ifdef FORTVILLE_A0_SUPPORT
        while ((i40eRead32(AdapterInfo, I40E_PFCM_LANCTXSTAT(AdapterInfo->Function)) & I40E_PFCM_LANCTXSTAT_CTX_DONE_MASK) == 0);
#else
        while ((i40eRead32(AdapterInfo, I40E_PFCM_LANCTXSTAT) & I40E_PFCM_LANCTXSTAT_CTX_DONE_MASK) == 0);
#endif
        Print(L"HMC function %d, Queue %d, Type %d, sub_line %x: %x %x %x %x\n",
            AdapterInfo->Function,
            QueueNumber,
            queue_type,
            sub_line,
#ifdef FORTVILLE_A0_SUPPORT
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(0, AdapterInfo->Function)),
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(1, AdapterInfo->Function)),
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(2, AdapterInfo->Function)),
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(3, AdapterInfo->Function)));
#else
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(0)),
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(1)),
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(2)),
            i40eRead32(AdapterInfo, I40E_PFCM_LANCTXDATA(3)));
#endif
    }

#ifdef FORTVILLE_A0_SUPPORT
    i40eWrite32(AdapterInfo, I40E_GLGEN_CSR_DEBUG_C,
        i40eRead32(AdapterInfo, I40E_GLGEN_CSR_DEBUG_C) | (I40E_GLGEN_CSR_DEBUG_C_CSR_ADDR_PROT_MASK));
#endif

    return EFI_SUCCESS;

}
#endif


/**
  Copies the frame from one of the Rx buffers to the command block
  passed in as part of the cpb parameter.

  The flow:  Ack the interrupt, setup the pointers, find where the last
  block copied is, check to make sure we have actually received something,
  and if we have then we do a lot of work. The packet is checked for errors,
  adjust the amount to copy if the buffer is smaller than the packet,
  copy the packet to the EFI buffer, and then figure out if the packet was
  targetted at us, broadcast, multicast or if we are all promiscuous.
  We then put some of the more interesting information (protocol, src and dest
  from the packet) into the db that is passed to us.  Finally we clean up
  the frame, set the return value to _SUCCESS, and inc the index, watching
  for wrapping.  Then with all the loose ends nicely wrapped up,
  fade to black and return.

  @param[in]  AdapterInfo Pointer to the driver data
  @param[in]  CpbReceive  Pointer (Ia-64 friendly) to the command parameter block.
                          The frame will be placed inside of it.
  @param[in]  DbReceive   The data buffer.  The out of band method of passing
                          pre-digested information to the protocol.

  @retval     PXE_STATCODE, either _NO_DATA if there is no data,
              or _SUCCESS if we passed the goods to the protocol.
**/
UINTN
i40eReceive(
    IN  I40E_DRIVER_DATA  *AdapterInfo,
    PXE_CPB_RECEIVE       *CpbReceive,
    PXE_DB_RECEIVE        *DbReceive,
    UINT16 queue
)
{
    PXE_FRAME_TYPE              PacketType;
    union i40e_32byte_rx_desc   *ReceiveDescriptor;
    ETHER_HEADER                *EtherHeader;
    PXE_STATCODE                StatCode;
    UINT16                      i;
    UINT16                      TempLen;
    UINT8                       *PacketPtr;
    UINT32                      RxStatus;
    UINT32                      RxError;
    UINT16                      RxPacketLength;
    UINT16                      RxHeaderLength;
    UINT16                      RxSph;
    UINT16                      RxPType;
    UINT64                      DescQWord;

    PacketType = PXE_FRAME_TYPE_NONE;
    StatCode = PXE_STATCODE_NO_DATA;

    //
    // Get a pointer to the buffer that should have a rx in it, IF one is really there.
    //
    ReceiveDescriptor = I40E_RX_DESC(&AdapterInfo->vsi.RxRing[queue], AdapterInfo->vsi.RxRing[queue].next_to_use);

    DescQWord = ReceiveDescriptor->wb.qword1.status_error_len;
    RxStatus = (UINT32)((DescQWord & I40E_RXD_QW1_STATUS_MASK) >> I40E_RXD_QW1_STATUS_SHIFT);

    if ((RxStatus & (1 << I40E_RX_DESC_STATUS_DD_SHIFT)) != 0) {
        RxPacketLength = (UINT16)((DescQWord & I40E_RXD_QW1_LENGTH_PBUF_MASK) >> I40E_RXD_QW1_LENGTH_PBUF_SHIFT);
        RxHeaderLength = (UINT16)((DescQWord & I40E_RXD_QW1_LENGTH_HBUF_MASK) >> I40E_RXD_QW1_LENGTH_HBUF_SHIFT);
        RxSph = (UINT16)((DescQWord & I40E_RXD_QW1_LENGTH_SPH_MASK) >> I40E_RXD_QW1_LENGTH_SPH_SHIFT);
        RxError = (UINT32)((DescQWord & I40E_RXD_QW1_ERROR_MASK) >> I40E_RXD_QW1_ERROR_SHIFT);
        RxPType = (UINT16)((DescQWord & I40E_RXD_QW1_PTYPE_MASK) >> I40E_RXD_QW1_PTYPE_SHIFT);

        //
        // Just to make sure we don't try to copy a zero length, only copy a positive sized packet.
        //
        if ((RxPacketLength != 0) && (RxError == 0)) {
            //
            // If the buffer passed us is smaller than the packet, only copy the size of the buffer.
            //
            TempLen = RxPacketLength;
            if (RxPacketLength > (INT16)CpbReceive->BufferLen) {
                TempLen = (UINT16)CpbReceive->BufferLen;
                AdapterInfo->hw.numPktOverflow++;
            }
            //
            // Copy the packet from our list to the EFI buffer.
            //
            i40eMemCopy(
                (INT8 *)(UINTN)CpbReceive->BufferAddr,
                AdapterInfo->vsi.RxRing[queue].BufferAddresses[AdapterInfo->vsi.RxRing[queue].next_to_use],
                TempLen
            );

            PacketPtr = (UINT8 *)(UINTN)CpbReceive->BufferAddr;
            DEBUGDUMP(RX, ("%02x:%02x:%02x:%02x:%02x:%02x %02x:%02x:%02x:%02x:%02x:%02x %02x%02x %02x %02x\n",
                PacketPtr[0x0], PacketPtr[0x1], PacketPtr[0x2], PacketPtr[0x3], PacketPtr[0x4], PacketPtr[0x5],
                PacketPtr[0x6], PacketPtr[0x7], PacketPtr[0x8], PacketPtr[0x9], PacketPtr[0xA], PacketPtr[0xB],
                PacketPtr[0xC], PacketPtr[0xD], PacketPtr[0xE], PacketPtr[0xF]));

            //
            // Fill the DB with needed information
            //
            DbReceive->FrameLen = RxPacketLength;  // includes header
            DbReceive->MediaHeaderLen = PXE_MAC_HEADER_LEN_ETHER;

            EtherHeader = (ETHER_HEADER *)(UINTN)PacketPtr;

            //
            // Figure out if the packet was meant for us, was a broadcast, multicast or we
            // recieved a frame in promiscuous mode.
            //
            for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
                if (EtherHeader->dest_addr[i] != AdapterInfo->hw.mac.perm_addr[i]) {
                    break;
                }
            }

            //
            // if we went the whole length of the header without breaking out then the packet is
            // directed at us.
            //
            if (i >= PXE_HWADDR_LEN_ETHER) {
                PacketType = PXE_FRAME_TYPE_UNICAST;
            }
            else {
                //
                // Compare it against our broadcast node address
                //
                for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
                    if (EtherHeader->dest_addr[i] != AdapterInfo->BroadcastNodeAddress[i]) {
                        break;
                    }
                }

                //
                // If we went the whole length of the header without breaking out then the packet is directed at us via broadcast
                //
                if (i >= PXE_HWADDR_LEN_ETHER) {
                    PacketType = PXE_FRAME_TYPE_BROADCAST;
                }
                else {
                    //
                    // That leaves multicast or we must be in promiscuous mode.   Check for the Mcast bit in the address.
                    // otherwise its a promiscuous receive.
                    //
                    if ((EtherHeader->dest_addr[0] & 1) == 1) {
                        PacketType = PXE_FRAME_TYPE_MULTICAST;
                    }
                    else {
                        PacketType = PXE_FRAME_TYPE_PROMISCUOUS;
                    }
                }
            }

            DEBUGPRINT(RX, ("Status %x, Length %d, PacketType = %d\n", RxStatus, RxPacketLength, PacketType));

            DbReceive->Type = PacketType;

            //
            // Put the protocol (UDP, TCP/IP) in the data buffer.
            //
            DbReceive->Protocol = EtherHeader->type;

            for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
                DbReceive->SrcAddr[i] = EtherHeader->src_addr[i];
                DbReceive->DestAddr[i] = EtherHeader->dest_addr[i];
            }

            DEBUGDUMP(RX, ("RxRing.next_to_use: %x, BufAddr: %x\n",
                AdapterInfo->vsi.RxRing[queue].next_to_use,
                (UINT64)(AdapterInfo->vsi.RxRing[queue].BufferAddresses[AdapterInfo->vsi.RxRing[queue].next_to_use])));

            AdapterInfo->hw.numRxSuccessful++;
            StatCode = PXE_STATCODE_SUCCESS;
        }
        else {
            DEBUGPRINT(CRITICAL, ("ERROR: RxPacketLength: %x, RxError: %x \n", RxPacketLength, RxError));
        }
        //
        // Clean up the packet and restore the buffer address
        //
        AdapterInfo->hw.numRxTotal++;
        ReceiveDescriptor->wb.qword1.status_error_len = 0;

        i40eMapMem(
            AdapterInfo,
            (UINT64)(UINTN)AdapterInfo->vsi.RxRing[queue].BufferAddresses[AdapterInfo->vsi.RxRing[queue].next_to_use],
            0,
            &ReceiveDescriptor->read.pkt_addr);

        //
        // Move the current cleaned buffer pointer, being careful to wrap it as needed.  Then update the hardware,
        // so it knows that an additional buffer can be used.
        //
        i40eWrite32(AdapterInfo, I40E_QRX_TAIL(queue), AdapterInfo->vsi.RxRing[queue].next_to_use);

        AdapterInfo->vsi.RxRing[queue].next_to_use++;
        if (AdapterInfo->vsi.RxRing[queue].next_to_use == AdapterInfo->vsi.RxRing[queue].count) {
            AdapterInfo->vsi.RxRing[queue].next_to_use = 0;
        }
    }
    return StatCode;
};


/**
  Takes a command block pointer (cpb) and sends the frame.

  Takes either one fragment or many and places them onto the wire.
  Cleanup of the send happens in the function UNDI_Status in Decode.c

  @param[in]  AdapterInfo   Pointer to the instance data
  @param[in]  cpb           The command parameter block address.
                            64 bits since this is Itanium(tm) processor friendly
  @param[in]  opflags       The operation flags, tells if there is any special
                            sauce on this transmit

  @retval     PXE_STATCODE_SUCCESS if the frame goes out,
              PXE_STATCODE_DEVICE_FAILURE if it didn't
              PXE_STATCODE_BUSY if they need to call again later.
**/
UINTN
i40eTransmit(
    I40E_DRIVER_DATA *AdapterInfo,
    UINT64           cpb,
    UINT16           opflags,
    UINT16           queue
)
{
    PXE_CPB_TRANSMIT_FRAGMENTS  *TxFrags;
    PXE_CPB_TRANSMIT            *TxBuffer;
    struct i40e_tx_desc         *TransmitDescriptor;
    UINT32                      TDCommand = 0;
    UINT32                      TDOffset = 0;
    UINT32                      TDTag = 0;
    INT32                       WaitMsec;
    UINT32                      i;
    UINT16                      Size;
    UINT8                       *PacketPtr;

    //
    // Transmit buffers must be freed by the upper layer before we can transmit any more.
    //
    if (AdapterInfo->vsi.TxRing[queue].TxBufferUsed[AdapterInfo->vsi.TxRing[queue].next_to_use] != 0) {
        DEBUGPRINT(CRITICAL, ("TX buffers have all been used!\n"));
        return PXE_STATCODE_QUEUE_FULL;
    }

    //
    // Make some short cut pointers so we don't have to worry about typecasting later.
    // If the TX has fragments we will use the
    // tx_tpr_f pointer, otherwise the tx_ptr_l (l is for linear)
    //
    TxBuffer = (PXE_CPB_TRANSMIT *)(UINTN)cpb;
    TxFrags = (PXE_CPB_TRANSMIT_FRAGMENTS *)(UINTN)cpb;

    if (AdapterInfo->VlanEnable) {
        TDCommand |= I40E_TX_DESC_CMD_IL2TAG1;
        TDTag = AdapterInfo->VlanTag;
    }

    //
    // quicker pointer to the next available Tx descriptor to use.
    //
    TransmitDescriptor = I40E_TX_DESC(&AdapterInfo->vsi.TxRing[queue], AdapterInfo->vsi.TxRing[queue].next_to_use);

    //
    // Opflags will tell us if this Tx has fragments
    // So far the linear case (the no fragments case, the else on this if) is the majority
    // of all frames sent.
    //
    if (opflags & PXE_OPFLAGS_TRANSMIT_FRAGMENTED) {
        //
        // this count cannot be more than 8;
        //
        DEBUGPRINT(TX, ("Fragments %x\n", TxFrags->FragCnt));

        //
        // for each fragment, give it a descriptor, being sure to keep track of the number used.
        //
        for (i = 0; i < TxFrags->FragCnt; i++) {
            //
            // Put the size of the fragment in the descriptor
            //
            i40eMapMem(
                AdapterInfo,
                (UINT64)(UINTN)TxFrags->FragDesc[i].FragAddr,
                0,
                &TransmitDescriptor->buffer_addr);
            Size = (UINT16)TxFrags->FragDesc[i].FragLen;

            TransmitDescriptor->cmd_type_offset_bsz = I40E_TX_DESC_DTYPE_DATA
                | ((UINT64)TDCommand << I40E_TXD_QW1_CMD_SHIFT)
                | ((UINT64)TDOffset << I40E_TXD_QW1_OFFSET_SHIFT)
                | ((UINT64)Size << I40E_TXD_QW1_TX_BUF_SZ_SHIFT)
                | ((UINT64)TDTag << I40E_TXD_QW1_L2TAG1_SHIFT);

            AdapterInfo->vsi.TxRing[queue].TxBufferUsed[AdapterInfo->vsi.TxRing[queue].next_to_use] = TxFrags->FragDesc[i].FragAddr;

            //
            // If this is the last fragment we must also set the EOP bit
            //
            if ((i + 1) == TxFrags->FragCnt) {
                TransmitDescriptor->cmd_type_offset_bsz |= (UINT64)I40E_TXD_CMD << I40E_TXD_QW1_CMD_SHIFT;
            }

            //
            // move our software counter passed the frame we just used, watching for wrapping
            //
            DEBUGPRINT(TX, ("Advancing TX pointer %x\n", AdapterInfo->vsi.TxRing[queue].next_to_use));
            AdapterInfo->vsi.TxRing[queue].next_to_use++;
            if (AdapterInfo->vsi.TxRing[queue].next_to_use == AdapterInfo->vsi.TxRing[queue].count) {
                AdapterInfo->vsi.TxRing[queue].next_to_use = 0;
            }

            TransmitDescriptor = I40E_TX_DESC(&AdapterInfo->vsi.TxRing[queue], AdapterInfo->vsi.TxRing[queue].next_to_use);
        }
    }
    else {
        i40eMapMem(
            AdapterInfo,
            (UINT64)(UINTN)TxBuffer->FrameAddr,
            0,
            &TransmitDescriptor->buffer_addr);

        Size = (UINT16)((UINT16)TxBuffer->DataLen + TxBuffer->MediaheaderLen);
        TransmitDescriptor->cmd_type_offset_bsz = I40E_TX_DESC_DTYPE_DATA
            | ((UINT64)TDCommand << I40E_TXD_QW1_CMD_SHIFT)
            | ((UINT64)TDOffset << I40E_TXD_QW1_OFFSET_SHIFT)
            | ((UINT64)Size << I40E_TXD_QW1_TX_BUF_SZ_SHIFT)
            | ((UINT64)TDTag << I40E_TXD_QW1_L2TAG1_SHIFT);
        TransmitDescriptor->cmd_type_offset_bsz |= (UINT64)I40E_TXD_CMD << I40E_TXD_QW1_CMD_SHIFT;
        AdapterInfo->vsi.TxRing[queue].TxBufferUsed[AdapterInfo->vsi.TxRing[queue].next_to_use] = TransmitDescriptor->buffer_addr;
        //
        // Move our software counter passed the frame we just used, watching for wrapping
        //
        AdapterInfo->vsi.TxRing[queue].next_to_use++;
        if (AdapterInfo->vsi.TxRing[queue].next_to_use == AdapterInfo->vsi.TxRing[queue].count) {
            AdapterInfo->vsi.TxRing[queue].next_to_use = 0;
        }
        DEBUGDUMP(TX, ("Length = %d, Buffer addr %x, cmd_type_offset_bsz %x \n",
            Size,
            TransmitDescriptor->buffer_addr,
            TransmitDescriptor->cmd_type_offset_bsz));

        PacketPtr = (UINT8 *)(UINTN)TransmitDescriptor->buffer_addr;
        DEBUGDUMP(TX, ("%02x:%02x:%02x:%02x:%02x:%02x %02x:%02x:%02x:%02x:%02x:%02x %02x%02x %02x %02x\n",
            PacketPtr[0x0], PacketPtr[0x1], PacketPtr[0x2], PacketPtr[0x3], PacketPtr[0x4], PacketPtr[0x5],
            PacketPtr[0x6], PacketPtr[0x7], PacketPtr[0x8], PacketPtr[0x9], PacketPtr[0xA], PacketPtr[0xB],
            PacketPtr[0xC], PacketPtr[0xD], PacketPtr[0xE], PacketPtr[0xF]));

    }

    i40eBlockIt(AdapterInfo, TRUE);
    i40eWrite32(AdapterInfo, I40E_QTX_TAIL(queue), AdapterInfo->vsi.TxRing[queue].next_to_use);
    i40eBlockIt(AdapterInfo, FALSE);
    //
    // If the opflags tells us to wait for the packet to hit the wire, we will wait.
    //
    if ((opflags & PXE_OPFLAGS_TRANSMIT_BLOCK) != 0) {
        WaitMsec = 10000;

        while ((TransmitDescriptor->cmd_type_offset_bsz & I40E_TX_DESC_DTYPE_DESC_DONE) == 0) {
            DelayInMicroseconds(AdapterInfo, 10);
            WaitMsec -= 10;
            if (WaitMsec <= 0) {
                break;
            }
        }

        //
        // If we waited for a while, and it didn't finish then the HW must be bad.
        //
        if ((TransmitDescriptor->cmd_type_offset_bsz & I40E_TX_DESC_DTYPE_DESC_DONE) == 0) {
            DEBUGPRINT(CRITICAL, ("Device failure\n"));
            return PXE_STATCODE_DEVICE_FAILURE;
        }
        else {
            DEBUGPRINT(TX, ("Transmit success\n"));
        }
    }
    return PXE_STATCODE_SUCCESS;
};


/**
  Free TX buffers that have been transmitted by the hardware.

  @param[in]  AdapterInfo  Pointer to the NIC data structure information which
                           the UNDI driver is layering on.
  @param[in]  NumEntries   Number of entries in the array which can be freed.
  @param[in]  TxBuffer     Array to pass back free TX buffer

  @retval     Number of TX buffers written.
**/
UINT16
i40eFreeTxBuffers(
    I40E_DRIVER_DATA    *AdapterInfo,
    IN UINT16           NumEntries,
    OUT UINT64          *TxBuffer
)
{
    struct i40e_tx_desc  *TransmitDescriptor;
    UINT16               i;
    UINT16               queue;
    i = 0;

    for (queue = 0; queue <= I40E_LAN_QP_INDEX; queue++)
    {
        do {
            if (i >= NumEntries) {
                DEBUGPRINT(TX, ("Exceeded number of DB entries, i=%d, NumEntries=%d\n", i, NumEntries));
                break;
            }

            TransmitDescriptor = I40E_TX_DESC(&AdapterInfo->vsi.TxRing[queue], AdapterInfo->vsi.TxRing[queue].next_to_clean);

            DEBUGPRINT(TX, ("TXDesc:%d Addr:%x, ctob: %x\n", AdapterInfo->vsi.TxRing[queue].next_to_clean, TransmitDescriptor->buffer_addr, TransmitDescriptor->cmd_type_offset_bsz));

            if ((TransmitDescriptor->cmd_type_offset_bsz & I40E_TX_DESC_DTYPE_DESC_DONE) != 0) {
                if (AdapterInfo->vsi.TxRing[queue].TxBufferUsed[AdapterInfo->vsi.TxRing[queue].next_to_clean] == 0) {
                    DEBUGPRINT(CRITICAL, ("ERROR: TX buffer complete without being marked used!\n"));
                    break;
                }

                DEBUGPRINT(TX, ("Cleaning buffer address %d, %x\n", i, TxBuffer[i]));
                TxBuffer[i] = AdapterInfo->vsi.TxRing[queue].TxBufferUsed[AdapterInfo->vsi.TxRing[queue].next_to_clean];
                i++;

                AdapterInfo->vsi.TxRing[queue].TxBufferUsed[AdapterInfo->vsi.TxRing[queue].next_to_clean] = 0;
                TransmitDescriptor->cmd_type_offset_bsz &= ~I40E_TXD_QW1_DTYPE_MASK;

                AdapterInfo->vsi.TxRing[queue].next_to_clean++;
                if (AdapterInfo->vsi.TxRing[queue].next_to_clean >= AdapterInfo->vsi.TxRing[queue].count) {
                    AdapterInfo->vsi.TxRing[queue].next_to_clean = 0;
                }
                AdapterInfo->hw.numTx++;
            }
            else {
                DEBUGPRINT(TX, ("TX Descriptor %d not done\n", AdapterInfo->vsi.TxRing[queue].next_to_clean));
                break;
            }
        } while (AdapterInfo->vsi.TxRing[queue].next_to_use != AdapterInfo->vsi.TxRing[queue].next_to_clean);
    }
    return i;
}

enum i40e_status_code
    I40eReceiveStart(
        IN I40E_DRIVER_DATA *AdapterInfo,
        UINT32 queue
    );

enum i40e_status_code
    I40eReceiveStop(
        IN I40E_DRIVER_DATA *AdapterInfo,
        UINT32 queue
    );


/**
  Sets receive filters.

  @param[in]  AdapterInfo  Pointer to the adapter structure
  @param[in]  NewFilter    A PXE_OPFLAGS bit field indicating what filters to use.

  @retval     VOID
**/
VOID
i40eSetFilter(
    I40E_DRIVER_DATA  IN  *AdapterInfo,
    UINT16            IN  NewFilter
)

{
    BOOLEAN                 ChangedPromiscuousFlag;
    BOOLEAN                 ChangedMulticastPromiscuousFlag;
    BOOLEAN                 ChangedBroadcastFlag;
    enum i40e_status_code   I40eStatus;

    DEBUGPRINT(RXFILTER, ("NewFilter %x= \n", NewFilter));

    ChangedPromiscuousFlag = FALSE;
    ChangedMulticastPromiscuousFlag = FALSE;
    ChangedBroadcastFlag = FALSE;

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS) {
        if (!AdapterInfo->vsi.EnablePromiscuous) {
            ChangedPromiscuousFlag = TRUE;
        }
        AdapterInfo->vsi.EnablePromiscuous = TRUE;
        DEBUGPRINT(RXFILTER, ("  Promiscuous\n"));
    }

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST) {
        if (!AdapterInfo->vsi.EnableBroadcast) {
            ChangedBroadcastFlag = TRUE;
        }
        AdapterInfo->vsi.EnableBroadcast = TRUE;
        DEBUGPRINT(RXFILTER, ("  Broadcast\n"));
    }

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) {
        if (!AdapterInfo->vsi.EnableMulticastPromiscuous) {
            ChangedMulticastPromiscuousFlag = TRUE;
        }
        AdapterInfo->vsi.EnableMulticastPromiscuous = TRUE;
        DEBUGPRINT(RXFILTER, ("  MulticastPromiscuous\n"));
    }

    if (!AdapterInfo->DriverBusy) {
        if (ChangedPromiscuousFlag || ChangedMulticastPromiscuousFlag || ChangedBroadcastFlag) {
            I40eStatus = i40e_aq_set_vsi_unicast_promiscuous(
                &AdapterInfo->hw, AdapterInfo->vsi.seid,
                AdapterInfo->vsi.EnablePromiscuous,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_set_vsi_unicast_promiscuous returned %d\n", I40eStatus));
            }

            I40eStatus = i40e_aq_set_vsi_multicast_promiscuous(
                &AdapterInfo->hw, AdapterInfo->vsi.seid,
                AdapterInfo->vsi.EnablePromiscuous || AdapterInfo->vsi.EnableMulticastPromiscuous,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_set_vsi_multicast_promiscuous returned %d\n", I40eStatus));
            }

            I40eStatus = i40e_aq_set_vsi_broadcast(
                &AdapterInfo->hw,
                AdapterInfo->vsi.seid,
                AdapterInfo->vsi.EnablePromiscuous || AdapterInfo->vsi.EnableBroadcast,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_set_vsi_broadcast returned %d\n", I40eStatus));
            }
        }
    }

    AdapterInfo->RxFilter |= NewFilter;

    return;
};


/**
  Clears receive filters.

  @param[in]  AdapterInfo  Pointer to the adapter structure
  @param[in]  NewFilter    A PXE_OPFLAGS bit field indicating what filters to clear.

  @retval     VOID
**/
VOID
i40eClearFilter(
    I40E_DRIVER_DATA   *AdapterInfo,
    UINT16             NewFilter
)
{
    BOOLEAN                 ChangedPromiscuousFlag;
    BOOLEAN                 ChangedMulticastPromiscuousFlag;
    BOOLEAN                 ChangedBroadcastFlag;
    enum i40e_status_code   I40eStatus;

    ChangedPromiscuousFlag = FALSE;
    ChangedMulticastPromiscuousFlag = FALSE;
    ChangedBroadcastFlag = FALSE;

    DEBUGPRINT(RXFILTER, ("NewFilter %x= \n", NewFilter));

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS) {
        if (AdapterInfo->vsi.EnablePromiscuous) {
            ChangedPromiscuousFlag = TRUE;
        }
        AdapterInfo->vsi.EnablePromiscuous = FALSE;
        DEBUGPRINT(RXFILTER, ("  Promiscuous\n"));
    }

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST) {
        if (AdapterInfo->vsi.EnableBroadcast) {
            ChangedBroadcastFlag = TRUE;
        }
        AdapterInfo->vsi.EnableBroadcast = FALSE;
        DEBUGPRINT(RXFILTER, ("  Broadcast\n"));
    }

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) {
        if (AdapterInfo->vsi.EnableMulticastPromiscuous) {
            ChangedMulticastPromiscuousFlag = TRUE;
        }
        AdapterInfo->vsi.EnableMulticastPromiscuous = FALSE;
        DEBUGPRINT(RXFILTER, ("  MulticastPromiscuous\n"));
    }

    if (!AdapterInfo->DriverBusy) {
        if (ChangedPromiscuousFlag || ChangedMulticastPromiscuousFlag || ChangedBroadcastFlag) {
            I40eStatus = i40e_aq_set_vsi_unicast_promiscuous(
                &AdapterInfo->hw, AdapterInfo->vsi.seid,
                AdapterInfo->vsi.EnablePromiscuous,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_set_vsi_unicast_promiscuous returned %d\n", I40eStatus));
            }

            I40eStatus = i40e_aq_set_vsi_multicast_promiscuous(
                &AdapterInfo->hw, AdapterInfo->vsi.seid,
                AdapterInfo->vsi.EnableMulticastPromiscuous || AdapterInfo->vsi.EnablePromiscuous,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_set_vsi_multicast_promiscuous returned %d\n", I40eStatus));
            }

            I40eStatus = i40e_aq_set_vsi_broadcast(
                &AdapterInfo->hw,
                AdapterInfo->vsi.seid,
                AdapterInfo->vsi.EnableBroadcast || AdapterInfo->vsi.EnablePromiscuous,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_set_vsi_broadcast returned %d\n", I40eStatus));
            }
        }
    }

    AdapterInfo->RxFilter &= ~NewFilter;

    return;
};


VOID
I40eSetMcastList(
    I40E_DRIVER_DATA *AdapterInfo
)
{
    struct i40e_aqc_remove_macvlan_element_data     MacVlanElementsToRemove[MAX_MCAST_ADDRESS_CNT];
    struct i40e_aqc_add_macvlan_element_data        MacVlanElementsToAdd[MAX_MCAST_ADDRESS_CNT];
    enum i40e_status_code                           I40eStatus = I40E_SUCCESS;
    UINTN                                           i;
    
    DEBUGDUMP(INIT, ("SM(%d,%d):", AdapterInfo->vsi.McastListToProgram.Length, AdapterInfo->vsi.CurrentMcastList.Length));

    DEBUGPRINT(RXFILTER, ("McastListToProgram.Length = %d\n",
        AdapterInfo->vsi.McastListToProgram.Length));
    DEBUGPRINT(RXFILTER, ("CurrentMcastList.Length = %d\n",
        AdapterInfo->vsi.CurrentMcastList.Length));

    if (!AdapterInfo->DriverBusy) {
        //
        // Remove existing elements from the Forwarding Table
        //
        if (AdapterInfo->vsi.CurrentMcastList.Length > 0) {
            for (i = 0; i < AdapterInfo->vsi.CurrentMcastList.Length; i++) {
                DEBUGPRINT(RXFILTER, ("Remove MAC %d, %x:%x:%x:%x:%x:%x\n",
                    i,
                    AdapterInfo->vsi.CurrentMcastList.McAddr[i][0],
                    AdapterInfo->vsi.CurrentMcastList.McAddr[i][1],
                    AdapterInfo->vsi.CurrentMcastList.McAddr[i][2],
                    AdapterInfo->vsi.CurrentMcastList.McAddr[i][3],
                    AdapterInfo->vsi.CurrentMcastList.McAddr[i][4],
                    AdapterInfo->vsi.CurrentMcastList.McAddr[i][5]
                    ));
                CopyMem(
                    MacVlanElementsToRemove[i].mac_addr,
                    &AdapterInfo->vsi.CurrentMcastList.McAddr[i],
                    6
                );
                MacVlanElementsToRemove[i].vlan_tag = 0;
                MacVlanElementsToRemove[i].flags = I40E_AQC_MACVLAN_DEL_IGNORE_VLAN | I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
            }

            I40eStatus = i40e_aq_remove_macvlan(
                &AdapterInfo->hw,
                AdapterInfo->vsi.seid,
                MacVlanElementsToRemove,
                AdapterInfo->vsi.CurrentMcastList.Length,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_remove_macvlan returned %d, aq error = %d\n",
                    I40eStatus, AdapterInfo->hw.aq.asq_last_status));
                for (i = 0; i < AdapterInfo->vsi.CurrentMcastList.Length; i++) {
                    DEBUGPRINT(CRITICAL, ("i40e_aq_remove_macvlan %d, %d\n", i, MacVlanElementsToRemove[i].error_code));
                }
            }
        }

        //
        // Add new elements to the Forwarding Table
        //
        if (AdapterInfo->vsi.McastListToProgram.Length > 0) {
            for (i = 0; i < AdapterInfo->vsi.McastListToProgram.Length; i++) {
                DEBUGPRINT(RXFILTER, ("Add MAC %d, %x:%x:%x:%x:%x:%x\n",
                    i,
                    AdapterInfo->vsi.McastListToProgram.McAddr[i][0],
                    AdapterInfo->vsi.McastListToProgram.McAddr[i][1],
                    AdapterInfo->vsi.McastListToProgram.McAddr[i][2],
                    AdapterInfo->vsi.McastListToProgram.McAddr[i][3],
                    AdapterInfo->vsi.McastListToProgram.McAddr[i][4],
                    AdapterInfo->vsi.McastListToProgram.McAddr[i][5]
                    ));
                CopyMem(
                    MacVlanElementsToAdd[i].mac_addr,
                    &AdapterInfo->vsi.McastListToProgram.McAddr[i],
                    6
                );
                MacVlanElementsToAdd[i].vlan_tag = 0;
                MacVlanElementsToAdd[i].flags = I40E_AQC_MACVLAN_ADD_IGNORE_VLAN | I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
            }

            I40eStatus = i40e_aq_add_macvlan(
                &AdapterInfo->hw,
                AdapterInfo->vsi.seid,
                MacVlanElementsToAdd,
                AdapterInfo->vsi.McastListToProgram.Length,
                NULL
            );
            if (I40eStatus != I40E_SUCCESS) {
                DEBUGPRINT(CRITICAL, ("i40e_aq_add_macvlan returned %d, aq error = %d\n",
                    I40eStatus, AdapterInfo->hw.aq.asq_last_status));
                for (i = 0; i < AdapterInfo->vsi.McastListToProgram.Length; i++) {
                    DEBUGPRINT(RXFILTER, ("i40e_aq_add_macvlan %d, %d\n", i, MacVlanElementsToAdd[i].match_method));
                }
            }
        }
    }
    //
    // Update CurrentMcastList
    //
    CopyMem(
        AdapterInfo->vsi.CurrentMcastList.McAddr,
        AdapterInfo->vsi.McastListToProgram.McAddr,
        AdapterInfo->vsi.McastListToProgram.Length * PXE_MAC_LENGTH
    );
    AdapterInfo->vsi.CurrentMcastList.Length = AdapterInfo->vsi.McastListToProgram.Length;
}


/**
  Start Rx and Tx rings

  Enable rings by using Queue enable registers

  @param[in]  AdapterInfo  Pointer to the adapter structure

  @retval     i40e_status_code
**/
enum i40e_status_code
    I40eReceiveStart(
        IN I40E_DRIVER_DATA *AdapterInfo,
        UINT32              queue
    )
{
    struct i40e_hw  *Hw;
    UINTN           j;

    Hw = &AdapterInfo->hw;
    Hw->numReceiveStart++;
    
    //
    // Tell HW that we intend to enable the Tx queue
    //
    i40e_pre_tx_queue_cfg(Hw, queue, TRUE);

    //
    // Enable both Tx and Rx queues by setting proper bits in I40E_QTX_ENA 
    // and I40E_QRX_ENA registers. Wait and check if status bits are changed.
    //
    wr32(Hw, I40E_QTX_ENA(queue), (rd32(Hw, I40E_QTX_ENA(queue)) | I40E_QTX_ENA_QENA_REQ_MASK));
    wr32(Hw, I40E_QRX_ENA(queue), (rd32(Hw, I40E_QRX_ENA(queue)) | I40E_QRX_ENA_QENA_REQ_MASK));

#define START_RINGS_TIMEOUT 100

    for (j = 0; j < START_RINGS_TIMEOUT; j++) {
        if (rd32(Hw, I40E_QTX_ENA(queue)) & I40E_QTX_ENA_QENA_STAT_MASK) {
            break;
        }
#ifndef INTEL_KDNET
        gBS->Stall(10);
#else
        KeStallExecutionProcessor(10);
#endif
    }
    if (j >= START_RINGS_TIMEOUT) {
        DEBUGPRINT(CRITICAL, ("Tx ring enable timed out, value %x\n",
            rd32(Hw, I40E_QTX_ENA(queue))));
        return I40E_ERR_TIMEOUT;
    }
    else {
        DEBUGPRINT(INIT, ("Tx ring enabled\n"));
    }

    //
    // Wait for the Rx queue status
    //
    for (; j < START_RINGS_TIMEOUT; j++) {
        if (rd32(Hw, I40E_QRX_ENA(queue)) & I40E_QRX_ENA_QENA_STAT_MASK) {
            break;
        }
#ifndef INTEL_KDNET
        gBS->Stall(10);
#else
        KeStallExecutionProcessor(10);
#endif
    }
    if (j >= START_RINGS_TIMEOUT) {
        DEBUGPRINT(CRITICAL, ("Rx ring enable timed out, value %x\n",
            rd32(Hw, I40E_QRX_ENA(queue))));
        return I40E_ERR_TIMEOUT;
    }
    else {
        DEBUGPRINT(INIT, ("Rx ring enabled\n"));
    }

    AdapterInfo->ReceiveStarted = TRUE;

    return I40E_SUCCESS;
}


enum i40e_status_code
    I40eReceiveStop(
        IN I40E_DRIVER_DATA *AdapterInfo,
        UINT32              queue
    )
{

    struct i40e_hw             *Hw;
    UINTN                      j;
    UINT32                     Reg = 0;
    enum i40e_status_code      Status;
    
    Hw = &AdapterInfo->hw;
    Hw->numReceiveStop++;

    if (AdapterInfo->hw.mac.type == I40E_MAC_X722) {
        Reg = rd32(Hw, I40E_PRTDCB_TC2PFC_RCB);
        wr32(Hw, I40E_PRTDCB_TC2PFC_RCB, 0);
    }
    //
    // Tell HW that we intend to disable the Tx queue
    //
    i40e_pre_tx_queue_cfg(Hw, queue, FALSE);

    //
    // Disable both Tx and Rx queues by setting proper bits in I40E_QTX_ENA 
    // and I40E_QRX_ENA registers. Wait and check if status bits are changed.
    //
    wr32(Hw, I40E_QTX_ENA(queue), (rd32(Hw, I40E_QTX_ENA(queue)) & ~I40E_QTX_ENA_QENA_REQ_MASK));
    wr32(Hw, I40E_QRX_ENA(queue), (rd32(Hw, I40E_QRX_ENA(queue)) & ~I40E_QRX_ENA_QENA_REQ_MASK));


    for (j = 0; j < STOP_RINGS_TIMEOUT; j++) {
        if (!(rd32(Hw, I40E_QTX_ENA(queue)) & I40E_QTX_ENA_QENA_STAT_MASK)) {
            break;
        }
#ifndef INTEL_KDNET
        gBS->Stall(10);
#else
        KeStallExecutionProcessor(10);
#endif
    }
    if (j >= STOP_RINGS_TIMEOUT) {
        DEBUGPRINT(
            CRITICAL, ("Tx ring disable timed out, value %x\n",
                rd32(Hw, I40E_QTX_ENA(queue)))
        );
        Status = I40E_ERR_TIMEOUT;
        goto ON_EXIT;
    }
    DEBUGPRINT(INIT, ("Tx ring disabled\n"));

    //
    // Use the remaining delay time to check the Rx queue status
    //
    for (j = 0; j < STOP_RINGS_TIMEOUT; j++) {
        if (!(rd32(Hw, I40E_QRX_ENA(queue)) & I40E_QRX_ENA_QENA_STAT_MASK)) {
            break;
        }
#ifndef INTEL_KDNET
        gBS->Stall(10);
#else
        KeStallExecutionProcessor(10);
#endif
    }
    if (j >= STOP_RINGS_TIMEOUT) {
        DEBUGPRINT(
            CRITICAL, ("Rx ring disable timed out, value %x\n",
                rd32(Hw, I40E_QRX_ENA(queue)))
        );
        Status = I40E_ERR_TIMEOUT;
        goto ON_EXIT;
    }
    DEBUGPRINT(INIT, ("Rx ring disabled\n"));

#ifndef INTEL_KDNET
    gBS->Stall(50000);
#else
    KeStallExecutionProcessor(50000);
#endif

    AdapterInfo->ReceiveStarted = FALSE;
    Status = I40E_SUCCESS;

ON_EXIT:
    if (AdapterInfo->hw.mac.type == I40E_MAC_X722) {
        wr32(Hw, I40E_PRTDCB_TC2PFC_RCB, Reg);
    }
    return Status;
}


EFI_STATUS
I40eSetupPfQueues(
    IN I40E_DRIVER_DATA *AdapterInfo
)
{
    UINT32 Reg;
    UINT16 FirstQueue;
    UINT16 LastQueue;

    Reg = i40e_read_rx_ctl(&AdapterInfo->hw, I40E_PFLAN_QALLOC);

    FirstQueue = (Reg & I40E_PFLAN_QALLOC_FIRSTQ_MASK) >> I40E_PFLAN_QALLOC_FIRSTQ_SHIFT;
    LastQueue = (Reg & I40E_PFLAN_QALLOC_LASTQ_MASK) >> I40E_PFLAN_QALLOC_LASTQ_SHIFT;

    DEBUGPRINT(INIT, ("PF Queues - first %x, last: %x\n", FirstQueue, LastQueue));

    AdapterInfo->vsi.base_queue = FirstQueue;

    return EFI_SUCCESS;

}

/**
  Configure transmit and receive descriptor rings in HMC context

  @param[in]  AdapterInfo  A pointer to structure containing driver data

  @retval     EFI_STATUS EFI_SUCCESS if procedure returned succesfully,
              otherwise error code
 **/
EFI_STATUS
I40eConfigureTxRxQueues(
    IN I40E_DRIVER_DATA *AdapterInfo,
    UINT16              queue
)
{
    enum   i40e_status_code    I40eStatus = I40E_SUCCESS;
    struct i40e_hw             *Hw;
    struct i40e_hmc_obj_txq    TxHmcContext;
    struct i40e_hmc_obj_rxq    RxHmcContext;
    union i40e_32byte_rx_desc  *ReceiveDescriptor;
    UINTN                      i;
#ifndef INTEL_KDNET
    EFI_STATUS                 Status = EFI_SUCCESS;
#else
    enum i40e_status_code       ret_code;
#endif
    UINT32                     QTxCtrl;
    UINT32                     QRxTail;
    
    DEBUGPRINT(INIT, ("\n"));
    Hw = &AdapterInfo->hw;

    //
    // Now associate the queue with the PCI function
    //
    QTxCtrl = I40E_QTX_CTL_PF_QUEUE;
    QTxCtrl |= ((Hw->bus.func << I40E_QTX_CTL_PF_INDX_SHIFT) & I40E_QTX_CTL_PF_INDX_MASK);
    wr32(Hw, I40E_QTX_CTL(queue), QTxCtrl);

    //
    // Clear the context structure before use
    //
    ZeroMem(&TxHmcContext, sizeof(struct i40e_hmc_obj_txq));

    TxHmcContext.new_context = 1;
    TxHmcContext.base = (UINT64)((AdapterInfo->vsi.TxRing[queue].desc.pa + 127) >> 7);
    TxHmcContext.qlen = AdapterInfo->vsi.TxRing[queue].count;
    
    //
    // Disable FCoE
    //
    TxHmcContext.fc_ena = 0;
    TxHmcContext.timesync_ena = 0;
    TxHmcContext.fd_ena = 0;
    TxHmcContext.alt_vlan_ena = 0;

    //
    // By default all traffic is assigned to TC0
    //
    TxHmcContext.rdylist = AdapterInfo->vsi.info.qs_handle[0];
    TxHmcContext.rdylist_act = 0;

    //
    // Clear the context in the HMC
    //
#ifdef DIRECT_QUEUE_CTX_PROGRAMMING
  //
  // Do nothing
  //
#else
    I40eStatus = i40e_clear_lan_tx_queue_context(Hw, queue);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Failed to clear LAN Tx queue context on Tx ring, error: %d\n",
            I40eStatus));
        return EFI_DEVICE_ERROR;
    }
#endif

    // 
    // Set the context in the HMC
    //
#ifdef DIRECT_QUEUE_CTX_PROGRAMMING
    I40eStatus = i40e_set_lan_tx_queue_context_directly(Hw, AdapterInfo->vsi.base_queue, &TxHmcContext);
    //HMCDump(AdapterInfo, 0, I40E_HMC_LAN_TX);
#else
    I40eStatus = i40e_set_lan_tx_queue_context(Hw, queue, &TxHmcContext);
#endif
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Failed to set LAN Tx queue context on Tx ring, error: %d\n",
            I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    //
    // Clear the context structure first
    //
    ZeroMem(&RxHmcContext, sizeof(struct i40e_hmc_obj_rxq));

    AdapterInfo->vsi.RxRing[queue].rx_buf_len = I40E_RXBUFFER_2048;
    // 
    // No packet split
    //
    AdapterInfo->vsi.RxRing[queue].rx_hdr_len = 0;

    RxHmcContext.head = 0;
    RxHmcContext.cpuid = 0;
    RxHmcContext.dbuff = (UINT8)(AdapterInfo->vsi.RxRing[queue].rx_buf_len >> I40E_RXQ_CTX_DBUFF_SHIFT);
    RxHmcContext.hbuff = (UINT8)(AdapterInfo->vsi.RxRing[queue].rx_hdr_len >> I40E_RXQ_CTX_HBUFF_SHIFT);
    RxHmcContext.base = (UINT64)((AdapterInfo->vsi.RxRing[queue].desc.pa + 127) >> 7);
    RxHmcContext.qlen = AdapterInfo->vsi.RxRing[queue].count;
    
    //
    // 32 byte descriptors in use
    //
    RxHmcContext.dsize = 1;
    RxHmcContext.dtype = I40E_RX_DTYPE_NO_SPLIT;
    RxHmcContext.hsplit_0 = I40E_HMC_OBJ_RX_HSPLIT_0_NO_SPLIT;

    RxHmcContext.rxmax = 0x2800;
    RxHmcContext.tphrdesc_ena = 0;
    RxHmcContext.tphwdesc_ena = 0;
    RxHmcContext.tphdata_ena = 0;
    RxHmcContext.tphhead_ena = 0;
    RxHmcContext.lrxqthresh = (I40E_NUM_TX_RX_DESCRIPTORS / 64);
    RxHmcContext.crcstrip = 1;
    RxHmcContext.l2tsel = 1;
    RxHmcContext.showiv = 1;
    //
    // No FCoE
    //
    RxHmcContext.fc_ena = 0;
    RxHmcContext.prefena = 1;
    
    //
    // Clear the context in the HMC
    //
#ifdef DIRECT_QUEUE_CTX_PROGRAMMING
#else
    I40eStatus = i40e_clear_lan_rx_queue_context(Hw, queue);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Failed to clear LAN Rx queue context on Rx ring, error: %d\n",
            I40eStatus));
        return EFI_DEVICE_ERROR;
    }
#endif

    //
    // Set the context in the HMC
    //
#ifdef DIRECT_QUEUE_CTX_PROGRAMMING
    I40eStatus = i40e_set_lan_rx_queue_context_directly(Hw, AdapterInfo->vsi.base_queue, &RxHmcContext);
    //HMCDump(AdapterInfo, 0, I40E_HMC_LAN_RX);
#else
    I40eStatus = i40e_set_lan_rx_queue_context(Hw, queue, &RxHmcContext);
#endif

    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Failed to set LAN Rx queue context on Rx ring, error: %d\n",
            I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    // 
    // Initialize tail register 
    // Workaround for Rx queue initialization:
    // The tail should be cleared and only then it should be set to the end of the queue
    //
    i40eWrite32(AdapterInfo, I40E_QRX_TAIL(queue), 0);
    i40eWrite32(AdapterInfo, I40E_QRX_TAIL(queue), AdapterInfo->vsi.RxRing[queue].count - 1);
    AdapterInfo->vsi.RxRing[queue].next_to_use = 0;

    QRxTail = i40eRead32(AdapterInfo, I40E_QRX_TAIL(queue));
    DEBUGPRINT(INIT, ("QRXTail %d\n", QRxTail));

    //
    // Determine the overall size of memory needed for receive buffers and allocate memory
    //
    AdapterInfo->vsi.RxRing[queue].RxBuffer.size = AdapterInfo->vsi.RxRing[queue].count * (AdapterInfo->vsi.RxRing[queue].rx_buf_len + RECEIVE_BUFFER_ALIGN_SIZE);
    AdapterInfo->vsi.RxRing[queue].RxBuffer.size = ALIGN(
        AdapterInfo->vsi.RxRing[queue].RxBuffer.size,
        4096
    );

#ifndef INTEL_KDNET
    Status = AdapterInfo->PciIo->AllocateBuffer(
        AdapterInfo->PciIo,
        AllocateAnyPages,
        EfiBootServicesData,
        UNDI_MEM_PAGES(AdapterInfo->vsi.RxRing[queue].RxBuffer.size),
        (VOID **)&AdapterInfo->vsi.RxRing[queue].RxBuffer.va,
        0
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("Unable to allocate memory for the Rx buffers, size=%d\n",
            AdapterInfo->vsi.RxRing[queue].RxBuffer.size));
        return Status;
    }
#else
    ret_code = i40e_allocate_dma_mem(Hw, &AdapterInfo->vsi.RxRing[queue].RxBuffer,
        i40e_mem_rx_data,
        AdapterInfo->vsi.RxRing[queue].RxBuffer.size,
        I40E_RX_DATA_ALIGNMENT);

    if (ret_code)
        return EFI_OUT_OF_RESOURCES;
#endif

    AdapterInfo->vsi.RxRing[queue].RxBuffer.va = 
        (VOID *)((UINT8 *)AdapterInfo->vsi.RxRing[queue].RxBuffer.va + RECEIVE_BUFFER_PADDING);
        
    //
    // Link the RX Descriptors to the receive buffers and cleanup descriptors
    //
    for (i = 0; i < AdapterInfo->vsi.RxRing[queue].count; i++) {
        ReceiveDescriptor = I40E_RX_DESC(&AdapterInfo->vsi.RxRing[queue], i);
        AdapterInfo->vsi.RxRing[queue].BufferAddresses[i] = (UINT8 *)AdapterInfo->vsi.RxRing[queue].RxBuffer.va + i * (AdapterInfo->vsi.RxRing[queue].rx_buf_len + RECEIVE_BUFFER_ALIGN_SIZE);
        i40eMapMem(
            AdapterInfo,
            (UINT64)(UINTN)AdapterInfo->vsi.RxRing[queue].BufferAddresses[i],
            0,
            &ReceiveDescriptor->read.pkt_addr);
        ReceiveDescriptor->read.hdr_addr = 0;
        ReceiveDescriptor->wb.qword1.status_error_len = 0;
    }

    return EFI_SUCCESS;
}


/**
  Free resources allocated for transmit and receive descriptor rings and remove
  HMC contexts

  @param[in]  AdapterInfo  A pointer to structure containing driver data

  @retval     EFI_STATUS EFI_SUCCESS if procedure returned succesfully,
              otherwise error code
 **/
EFI_STATUS
I40eFreeTxRxQueues(
    IN I40E_DRIVER_DATA *AdapterInfo,
    UINT16              queue
)
{
    enum   i40e_status_code    I40eStatus;
    struct i40e_hw             *Hw;
    EFI_STATUS                 Status;

    DEBUGPRINT(INIT, ("\n"));

    Hw = &AdapterInfo->hw;
    I40eStatus = I40E_SUCCESS;
    Status = EFI_DEVICE_ERROR;

#ifndef DIRECT_QUEUE_CTX_PROGRAMMING
    //
    // Clear the context in the HMC
    //
    I40eStatus = i40e_clear_lan_tx_queue_context(Hw, queue);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Failed to clear LAN Tx queue context on Tx ring, error: %d\n",
            I40eStatus));
        return EFI_DEVICE_ERROR;
    }
    //
    // Clear the context in the HMC
    //
    I40eStatus = i40e_clear_lan_rx_queue_context(Hw, queue);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Failed to clear LAN Rx queue context on Rx ring, error: %d\n",
            I40eStatus));
        return EFI_DEVICE_ERROR;
    }
#endif

#ifdef INTEL_KDNET
    Status = EFI_SUCCESS;
#else
    Status = AdapterInfo->PciIo->FreeBuffer(
        AdapterInfo->PciIo,
        UNDI_MEM_PAGES(AdapterInfo->vsi.RxRing[queue].RxBuffer.Size),
        (VOID *)AdapterInfo->vsi.RxRing[queue].RxBuffer.Data);
#endif

    return Status;
}


/**
 Allocate memory resources for the Tx and Rx descriptors

 @param[in]  AdapterInfo  A pointer to structure containing driver data

 @retval     EFI_STATUS EFI_SUCCESS if procedure returned succesfully,
             otherwise error code
**/
EFI_STATUS
I40eSetupTxRxResources(
    IN  I40E_DRIVER_DATA  *AdapterInfo,
    UINT16                queue
)
{
    UINTN       i;
#ifndef INTEL_KDNET
    EFI_STATUS  Status = EFI_SUCCESS;
#else
    enum i40e_status_code ret_code;
    struct  i40e_hw      *hw;
#endif

#ifdef INTEL_KDNET
    hw = &AdapterInfo->hw;
#endif

    AdapterInfo->vsi.TxRing[queue].count = AdapterInfo->vsi.num_desc;
    AdapterInfo->vsi.TxRing[queue].size = 0;

    AdapterInfo->vsi.RxRing[queue].count = AdapterInfo->vsi.num_desc;
    AdapterInfo->vsi.RxRing[queue].size = 0;

    //
    // This block is for Tx descriptiors
    // Round up to nearest 4K
    AdapterInfo->vsi.TxRing[queue].size = AdapterInfo->vsi.TxRing[queue].count * sizeof(struct i40e_tx_desc);
    AdapterInfo->vsi.TxRing[queue].size = ALIGN(AdapterInfo->vsi.TxRing[queue].size, 4096);

#ifndef INTEL_KDNET
    //
    // Allocate memory
    //
    Status = AdapterInfo->PciIo->AllocateBuffer(
        AdapterInfo->PciIo,
        AllocateAnyPages,
        EfiBootServicesData,
        UNDI_MEM_PAGES(AdapterInfo->vsi.TxRing[queue].size),
        (VOID **)&AdapterInfo->vsi.TxRing[queue].desc.va,
        0
    );
    AdapterInfo->vsi.TxRing[queue].desc.size = AdapterInfo->vsi.TxRing[queue].size;
    DEBUGPRINT(INIT, ("AdapterInfo->vsi.tx_ring.desc: %X\n", AdapterInfo->vsi.TxRing[queue].desc.va));

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("Unable to allocate memory for the Tx descriptor ring, size=%d\n",
            AdapterInfo->vsi.TxRing[queue].size));
        return Status;
    }
#else
    ret_code = i40e_allocate_dma_mem(hw, &AdapterInfo->vsi.TxRing[queue].desc,
        i40e_mem_tx_ring,
        AdapterInfo->vsi.TxRing[queue].size,
        I40E_TXRX_DESC_ALIGNMENT);

    if (ret_code)
        return EFI_OUT_OF_RESOURCES;
#endif

    AdapterInfo->vsi.TxRing[queue].next_to_use = 0;
    AdapterInfo->vsi.TxRing[queue].next_to_clean = 0;

    //
    // All available transmit descriptors are free by default
    //
    for (i = 0; i < AdapterInfo->vsi.TxRing[queue].count; i++) {
        AdapterInfo->vsi.TxRing[queue].TxBufferUsed[i] = 0;
    }

    //
    //  This block is for Rx descriptors
    //  Use 16 byte descriptors as we are in PXE MODE.
    //

    /* Round up to nearest 4K */
    AdapterInfo->vsi.RxRing[queue].size = AdapterInfo->vsi.RxRing[queue].count * sizeof(union i40e_32byte_rx_desc);

    AdapterInfo->vsi.RxRing[queue].size = ALIGN(AdapterInfo->vsi.RxRing[queue].size, 4096);

    //
    // Allocate memory
    //
#ifndef INTEL_KDNET
    Status = AdapterInfo->PciIo->AllocateBuffer(
        AdapterInfo->PciIo,
        AllocateAnyPages,
        EfiBootServicesData,
        UNDI_MEM_PAGES(AdapterInfo->vsi.RxRing[queue].size),
        (VOID **)&AdapterInfo->vsi.RxRing[queue].desc.va,
        0
    );
    AdapterInfo->vsi.RxRing[queue].desc.size = AdapterInfo->vsi.RxRing[queue].size;
    DEBUGPRINT(INIT, ("AdapterInfo->vsi.rx_ring.desc: %X\n", AdapterInfo->vsi.RxRing[queue].desc.va));

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("Unable to allocate memory for the Rx descriptor ring, size=%d\n",
            AdapterInfo->vsi.RxRing[queue].size));
        return Status;
    }
#else
    ret_code = i40e_allocate_dma_mem(hw, &AdapterInfo->vsi.RxRing[queue].desc,
        i40e_mem_rx_ring,
        AdapterInfo->vsi.RxRing[queue].size,
        I40E_TXRX_DESC_ALIGNMENT);

    if (ret_code)
        return EFI_OUT_OF_RESOURCES;
#endif

    AdapterInfo->vsi.RxRing[queue].next_to_clean = 0;
    AdapterInfo->vsi.RxRing[queue].next_to_use = 0;

    return EFI_SUCCESS;
}


/**
 Free memory resources for the Tx and Rx descriptors

 @param[in]  AdapterInfo  A pointer to structure containing driver data

 @retval     EFI_STATUS EFI_SUCCESS if procedure returned succesfully,
             otherwise error code
**/
EFI_STATUS
I40eFreeTxRxResources(
    IN  I40E_DRIVER_DATA  *AdapterInfo,
    UINT16                queue
)
{
    EFI_STATUS  Status;

#ifdef INTEL_KDNET
    Status = EFI_SUCCESS;
#else
    Status = AdapterInfo->PciIo->FreeBuffer(
        AdapterInfo->PciIo,
        UNDI_MEM_PAGES(AdapterInfo->vsi.TxRing[queue].size),
        AdapterInfo->vsi.TxRing[queue].desc);
#endif

    AdapterInfo->vsi.TxRing[queue].desc.va = NULL;

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("Unable to free memory for the Tx descriptor ring %r, size=%d\n",
            Status, AdapterInfo->vsi.TxRing[queue].size));
        return Status;
    }

#ifdef INTEL_KDNET
    Status = EFI_SUCCESS;
#else
    Status = AdapterInfo->PciIo->FreeBuffer(
        AdapterInfo->PciIo,
        UNDI_MEM_PAGES(AdapterInfo->vsi.RxRing[queue].size),
        AdapterInfo->vsi.RxRing[queue].desc);
#endif

    AdapterInfo->vsi.RxRing[queue].desc.va = NULL;

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("Unable to free memory for the Rx descriptor ring, size=%d\n",
            AdapterInfo->vsi.RxRing[queue].size));
        return Status;
    }

    return Status;
}


/**
 Get the current switch configuration from the device and
 extract a few useful SEID values.

 @param[in]  AdapterInfo  A pointer to structure containing driver data

 @retval     EFI_STATUS EFI_SUCCESS if procedure returned succesfully,
             otherwise error code
**/
EFI_STATUS
i40eReadSwitchConfiguration(
    IN  I40E_DRIVER_DATA  *AdapterInfo
)
{
    struct i40e_aqc_get_switch_config_resp  *SwitchConfig;
    UINT8                                   AqBuffer[I40E_AQ_LARGE_BUF];
    enum i40e_status_code                   I40eStatus;
    int                                     i;
    UINT16                                  StartSeid;

    if (AdapterInfo == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    SwitchConfig = (struct i40e_aqc_get_switch_config_resp *)AqBuffer;
    StartSeid = 0;
    I40eStatus = i40e_aq_get_switch_config(&AdapterInfo->hw,
        SwitchConfig,
        sizeof(AqBuffer),
        &StartSeid,
        NULL);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("get switch config failed %d aq_err=%x\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }

    if (SwitchConfig->header.num_reported == 0) {
        DEBUGPRINT(CRITICAL, ("No data returned in the SwitchConfig \n"));
        return EFI_DEVICE_ERROR;
    }

    for (i = 0; i < SwitchConfig->header.num_reported; i++) {
        DEBUGPRINT(INIT, ("type=%d seid=%d uplink=%d downlink=%d\n",
            SwitchConfig->element[i].element_type,
            SwitchConfig->element[i].seid,
            SwitchConfig->element[i].uplink_seid,
            SwitchConfig->element[i].downlink_seid)
        );

        switch (SwitchConfig->element[i].element_type) {
        case I40E_SWITCH_ELEMENT_TYPE_MAC:
            AdapterInfo->mac_seid = SwitchConfig->element[i].seid;
            break;
        case I40E_SWITCH_ELEMENT_TYPE_VEB:
            AdapterInfo->veb_seid = SwitchConfig->element[i].seid;
            break;
        case I40E_SWITCH_ELEMENT_TYPE_PF:
            AdapterInfo->pf_seid = SwitchConfig->element[i].seid;
            AdapterInfo->main_vsi_seid = SwitchConfig->element[i].uplink_seid;
            break;
        case I40E_SWITCH_ELEMENT_TYPE_VSI:
            AdapterInfo->main_vsi_seid = SwitchConfig->element[i].seid;
            AdapterInfo->pf_seid = SwitchConfig->element[i].downlink_seid;
            AdapterInfo->mac_seid = SwitchConfig->element[i].uplink_seid;
            break;
        case I40E_SWITCH_ELEMENT_TYPE_VF:
        case I40E_SWITCH_ELEMENT_TYPE_EMP:
        case I40E_SWITCH_ELEMENT_TYPE_BMC:
        case I40E_SWITCH_ELEMENT_TYPE_PE:
        case I40E_SWITCH_ELEMENT_TYPE_PA:
            /* ignore these for now */
            break;
        default:
            DEBUGPRINT(CRITICAL, ("Unknown element type=%d seid=%d\n",
                SwitchConfig->element[i].element_type,
                SwitchConfig->element[i].seid));
            break;
        }
    }

    return EFI_SUCCESS;
}


//TODO: Add the return value to function

/**
  Turn on vlan stripping for the vsi

  @param[in]  AdapterInfo  A pointer to structure containing driver data

  @retval    EFI_STATUS
**/
EFI_STATUS
i40eDisableVlanStripping(
    IN  I40E_DRIVER_DATA  *AdapterInfo
)
{
    struct i40e_vsi_context  VsiContext;
    enum i40e_status_code    I40eStatus;

    if ((AdapterInfo->vsi.info.valid_sections & I40E_AQ_VSI_PROP_VLAN_VALID) == I40E_AQ_VSI_PROP_VLAN_VALID) {
        if ((AdapterInfo->vsi.info.port_vlan_flags & I40E_AQ_VSI_PVLAN_EMOD_MASK) == I40E_AQ_VSI_PVLAN_EMOD_MASK) {
            DEBUGPRINT(INIT, ("VLAN stripping already disabled\n"));
            return EFI_SUCCESS;
        }
    }

    AdapterInfo->vsi.info.valid_sections |= I40E_AQ_VSI_PROP_VLAN_VALID;
    AdapterInfo->vsi.info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL
        | I40E_AQ_VSI_PVLAN_EMOD_NOTHING;

    VsiContext.seid = AdapterInfo->main_vsi_seid;
    CopyMem(&VsiContext.info, &AdapterInfo->vsi.info, sizeof(AdapterInfo->vsi.info));

    I40eStatus = i40e_aq_update_vsi_params(&AdapterInfo->hw, &VsiContext, NULL);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("Update vsi failed, aq_err=%d\n",
            AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

#ifdef SV_SUPPORT
STATIC UINT8  DebugBuf[4096];


VOID
DumpInternalFwHwData(
    I40E_DRIVER_DATA  *AdapterInfo
)
{
    enum i40e_status_code        I40eStatus;
    UINT32                       i, ii;
    UINT32                       StartIndex, NextStartIndex;
    UINT16                       RetBufSize;
    UINT8                        TableId, NextTableId;

    TableId = 0;
    StartIndex = 0;
    ii = 0;

    DEBUGDUMP(INIT, ("\n%d: ", TableId));

    while (1) {
        I40eStatus = i40e_aq_debug_dump(
            &AdapterInfo->hw,
            I40E_AQ_CLUSTER_ID_TXSCHED,
            TableId,
            StartIndex,
            sizeof(DebugBuf),
            DebugBuf,
            &RetBufSize,
            &NextTableId,
            &NextStartIndex,
            NULL
        );
        if (I40eStatus != I40E_SUCCESS) {
            DEBUGPRINT(CRITICAL, ("i40e_aq_debug_dump failed, aq_err=%d\n",
                AdapterInfo->hw.aq.asq_last_status));
            return;
        }

        //
        // Print data to console
        //
        for (i = 0; i < RetBufSize; i++) {
            //      if ((ii++ % 32) == 0) {
            //        DEBUGDUMP(INIT, ("\n"));
            //      }
            DEBUGDUMP(INIT, ("%02x", DebugBuf[i]));
        }

        if (TableId == NextTableId) {
            //
            // Advance index, table remains the same
            //
            StartIndex = NextStartIndex;
        }
        else if (NextTableId != 0xFF) {
            //
            // Next table
            //
            TableId = NextTableId;
            StartIndex = 0;
            DEBUGDUMP(INIT, ("\n%d: ", TableId));
            ii = 0;
        }
        else {
            //
            // This is the end
            //
            return;
        }
    }

}
#endif

#define DumpVsiCtxRegister(_RegName) \
  DEBUGDUMP (INIT, (#_RegName"(%x) = %x\n", \
    ##_RegName## (AdapterInfo->vsi.id), rd32 (&AdapterInfo->hw, ##_RegName## (VsiId))) \
    );

#define DumpRegister(_RegName) \
      DEBUGDUMP (INIT, (#_RegName"(%x) = %x\n", \
    ##_RegName##, rd32 (&AdapterInfo->hw, ##_RegName##)) \
        );

#if (0)
VOID
DumpVsiContext(
    I40E_DRIVER_DATA  *AdapterInfo,
    UINT32            VsiId
)
{
    UINTN i;

#define I40E_VSI_L2TAGSTXVALID(_VSI)    (0x00042800 + ((_VSI) * 4))

    DumpVsiCtxRegister(I40E_VSI_TAR);
    DumpVsiCtxRegister(I40E_VSI_TAIR);
    DumpVsiCtxRegister(I40E_VSI_TIR_0);
    DumpVsiCtxRegister(I40E_VSI_TIR_1);
    DumpVsiCtxRegister(I40E_VSI_TIR_2);
    DumpVsiCtxRegister(I40E_VSI_L2TAGSTXVALID);
    DumpVsiCtxRegister(I40E_VSI_TSR);
    DumpVsiCtxRegister(I40E_VSI_TUPR);
    DumpVsiCtxRegister(I40E_VSI_TUPIOM);
    DumpVsiCtxRegister(I40E_VSI_RUPR);
    DumpVsiCtxRegister(I40E_VSI_SRCSWCTRL);
    DumpVsiCtxRegister(I40E_VSI_RXSWCTRL);
    DumpVsiCtxRegister(I40E_VSI_VSI2F);
    DumpVsiCtxRegister(I40E_VSI_PORT);
    DumpVsiCtxRegister(I40E_VSILAN_QBASE);
    DumpVsiCtxRegister(I40E_VSIQF_CTL);

    DEBUGDUMP(INIT, ("I40E_VSILAN_QTABLE (0..7): "));
    for (i = 0; i < 8; i++) {
        DEBUGDUMP(INIT, ("%x ", rd32(&AdapterInfo->hw, I40E_VSILAN_QTABLE(i, VsiId))));
    }
    DEBUGDUMP(INIT, ("\n"));

    DumpVsiCtxRegister(I40E_VSIQF_CTL);

    DEBUGDUMP(INIT, ("I40E_VSIQF_TCREGION (0..3): "));
    for (i = 0; i < 4; i++) {
        DEBUGDUMP(INIT, ("%x ", rd32(&AdapterInfo->hw, I40E_VSIQF_TCREGION(i, VsiId))));
    }
    DEBUGDUMP(INIT, ("\n"));

    DumpRegister(I40E_PRT_L2TAGSEN);
    DumpRegister(I40E_PRT_TDPUL2TAGSEN);
    DumpRegister(I40E_PRT_PPRSL2TAGSEN);

    DEBUGDUMP(INIT, ("I40E_GLTDPU_L2TAGCTRL (0..7): "));
    for (i = 0; i < 8; i++) {
        DEBUGDUMP(INIT, ("%x ", rd32(&AdapterInfo->hw, I40E_GLTDPU_L2TAGCTRL(i))));
    }
    DEBUGDUMP(INIT, ("\n"));

    DEBUGDUMP(INIT, ("I40E_GL_SWT_L2TAGTXIB (0..7): "));
    for (i = 0; i < 8; i++) {
        DEBUGDUMP(INIT, ("%x ", rd32(&AdapterInfo->hw, I40E_GL_SWT_L2TAGTXIB(i))));
    }
    DEBUGDUMP(INIT, ("\n"));

    DEBUGDUMP(INIT, ("I40E_GL_SWT_L2TAGRXEB (0..7): "));
    for (i = 0; i < 8; i++) {
        DEBUGDUMP(INIT, ("%x ", rd32(&AdapterInfo->hw, I40E_GL_SWT_L2TAGRXEB(i))));
    }
    DEBUGDUMP(INIT, ("\n"));
}
#endif


/**
 * I40eSetupPFSwitch - setup the initial LAN and VMDq switch
 * @pf: board private
 *
 * This adds the VEB into the internal switch, makes sure the main
 * LAN VSI is connected correctly, allocates and connects all the
 * VMDq VSIs, and sets the base queue index for each VSI.
 *
 * Returns 0 on success, negative value on failure
 **/
EFI_STATUS
I40eSetupPFSwitch(
    I40E_DRIVER_DATA *AdapterInfo
)
{
    enum i40e_status_code                    I40eStatus;
    EFI_STATUS                               Status;
    struct i40e_vsi_context                  vsi_ctx;
    struct i40e_aqc_add_macvlan_element_data MacVlan;
    struct i40e_filter_control_settings      FilterControlSettings;
    UINT8                                    cnt = 0;

    I40eStatus = I40E_SUCCESS;
    Status = EFI_SUCCESS;

    //
    // Read the default switch configuration
    //
    Status = i40eReadSwitchConfiguration(AdapterInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("i40eReadSwitchConfiguration returned %r\n", Status));
        return Status;
    }

    //
    // Get main VSI parameters
    //
    I40eStatus = I40eGetVsiParams(AdapterInfo, &vsi_ctx);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_get_vsi_params returned %d, aq_err %d\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }

    DEBUGPRINT(INIT, ("VSI params: vsi_number=%d, vsi_ctx.info.qs_handle[0]=%d\n",
        vsi_ctx.vsi_number,
        vsi_ctx.info.qs_handle[0])
    );

    AdapterInfo->vsi.id = vsi_ctx.vsi_number;

    //
    // Determine which queues are used by this PF
    //
    I40eSetupPfQueues(AdapterInfo);

    //
    //  Set number of queue pairs we use to 1
    //
    AdapterInfo->num_lan_qps = I40E_LAN_QP_INDEX+1;

    //
    // Check if VSI has enough queue pairs
    //
    if ((AdapterInfo->hw.func_caps.num_tx_qp < AdapterInfo->num_lan_qps)
        || (AdapterInfo->hw.func_caps.num_rx_qp < AdapterInfo->num_lan_qps))
    {
        DEBUGPRINT(CRITICAL, ("Not enough qps available\n"));
        return EFI_DEVICE_ERROR;
    }

    //
    // Store VSI parameters in VSI structure
    //
    AdapterInfo->vsi.type = I40E_VSI_MAIN;
    AdapterInfo->vsi.flags = 0;
    AdapterInfo->vsi.num_queue_pairs = AdapterInfo->num_lan_qps;
    AdapterInfo->vsi.num_desc = I40E_NUM_TX_RX_DESCRIPTORS;
    AdapterInfo->vsi.seid = AdapterInfo->main_vsi_seid;
    CopyMem(&AdapterInfo->vsi.info, &vsi_ctx.info, sizeof(vsi_ctx.info));

    ZeroMem(&FilterControlSettings, sizeof(FilterControlSettings));
    FilterControlSettings.hash_lut_size = I40E_HASH_LUT_SIZE_128;
    FilterControlSettings.enable_ethtype = TRUE;
    FilterControlSettings.enable_macvlan = TRUE;
    I40eStatus = i40e_set_filter_control(&AdapterInfo->hw, &FilterControlSettings);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_set_filter_control returned %d, aq_err %d\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }

    //
    // HSD 5281411 <FVL B0> VLANs are not working with latest NVMs
    // Starting with NVM 4.2.2 firmware default filter setting is no
    // longer accept tagged packets
    // by default
    //
    SetMem(&MacVlan, sizeof(struct i40e_aqc_add_macvlan_element_data), 0);
    MacVlan.flags = I40E_AQC_MACVLAN_ADD_IGNORE_VLAN | I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
    CopyMem(MacVlan.mac_addr, &AdapterInfo->hw.mac.addr, sizeof(MacVlan.mac_addr));
    I40eStatus = i40e_aq_add_macvlan(&AdapterInfo->hw, AdapterInfo->main_vsi_seid,
        &MacVlan, 1, NULL);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_add_macvlan returned %d, aq_err %d\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }

    SetMem(&MacVlan, sizeof(struct i40e_aqc_add_macvlan_element_data), 0);
    MacVlan.flags = I40E_AQC_MACVLAN_ADD_IGNORE_VLAN | I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
    for (cnt = 0; cnt < 6; cnt++)
    {
        MacVlan.mac_addr[cnt] = 0xFF;
    }

    I40eStatus = i40e_aq_add_macvlan(&AdapterInfo->hw, AdapterInfo->main_vsi_seid,
        &MacVlan, 1, NULL);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_add_macvlan returned %d, aq_err %d\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }

    I40eStatus = i40e_aq_set_mac_config(&AdapterInfo->hw, 0x5F2, TRUE, I40E_AQ_SET_MAC_CONFIG_PACING_NONE, NULL);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_add_macvlan returned %d, aq_err %d\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }

    //
    // Configure VLAN stripping on Rx packets
    //
    Status = i40eDisableVlanStripping(AdapterInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("i40eDisableVlanStripping returned %r\n", Status));
        return Status;
    }

    return Status;
}


EFI_STATUS
i40ePciInit(
    I40E_DRIVER_DATA *AdapterInfo
)
/*++

Routine Description:
  This function performs PCI-E initialization for the device.

Arguments:
  AdapterInfo - Pointer to adapter structure

Returns:
  EFI_STATUS

--*/
{
#ifdef INTEL_KDNET
    EFI_STATUS  Status = EFI_SUCCESS;
#else
    EFI_STATUS  Status;
    UINT64      NewCommand;
    UINT64      Result;
    BOOLEAN     PciAttributesSaved;

    NewCommand = 0;
    Result = 0;

    PciAttributesSaved = FALSE;
#endif

#ifndef INTEL_KDNET
    //
    // Save original PCI attributes
    //
    Status = AdapterInfo->PciIo->Attributes(
        AdapterInfo->PciIo,
        EfiPciIoAttributeOperationGet,
        0,
        &AdapterInfo->OriginalPciAttributes
    );

    if (EFI_ERROR(Status)) {
        goto error;
    }
    PciAttributesSaved = TRUE;

    //
    // Get the PCI Command options that are supported by this controller.
    //
    Status = AdapterInfo->PciIo->Attributes(
        AdapterInfo->PciIo,
        EfiPciIoAttributeOperationSupported,
        0,
        &Result
    );

    DEBUGPRINT(INIT, ("Attributes supported %x\n", Result));

    if (!EFI_ERROR(Status)) {
        //
        // Set the PCI Command options to enable device memory mapped IO,
        // port IO, and bus mastering.
        //
        Status = AdapterInfo->PciIo->Attributes(
            AdapterInfo->PciIo,
            EfiPciIoAttributeOperationEnable,
            Result & (EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE),
            &NewCommand
        );
    }
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("PciIo->Attributes returned %r\n", Status));
        goto error;
    }
#endif

#ifndef INTEL_KDNET
    AdapterInfo->PciIo->GetLocation(
        AdapterInfo->PciIo,
        &AdapterInfo->Segment,
        &AdapterInfo->Bus,
        &AdapterInfo->Device,
        &AdapterInfo->Function
    );
#else
    GetDeviceLocation(&AdapterInfo->Segment,
        &AdapterInfo->Bus,
        &AdapterInfo->Device,
        &AdapterInfo->Function);
#endif

    //
    // Read all the registers from the device's PCI Configuration space
    //
#ifndef INTEL_KDNET
    AdapterInfo->PciIo->Pci.Read(
        AdapterInfo->PciIo,
        EfiPciIoWidthUint32,
        0,
        MAX_PCI_CONFIG_LEN,
        AdapterInfo->PciConfig
    );
    return Status;
#else
    GetDevicePciDataByOffset(&AdapterInfo->PciConfig,
        0,
        MAX_PCI_CONFIG_LEN * sizeof(UINT32));

#endif

#ifndef INTEL_KDNET
    error :

          if (PciAttributesSaved) {
              //
              // Restore original PCI attributes
              //
              AdapterInfo->PciIo->Attributes(
                  AdapterInfo->PciIo,
                  EfiPciIoAttributeOperationSet,
                  AdapterInfo->OriginalPciAttributes,
                  NULL
              );
          }
#endif

          return Status;
}


EFI_STATUS
I40eReadMacAddress(
    I40E_DRIVER_DATA  *AdapterInfo
)
{
    enum i40e_status_code   I40eStatus;
    struct i40e_hw          *hw;

    hw = &AdapterInfo->hw;

    //
    // Get current MAC Address using the shared code function
    //
    I40eStatus = i40e_get_mac_addr(hw, hw->mac.addr);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_get_mac_addr returned %d\n", I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    //
    // Assume this is also a permanent address and save it for the future
    //
    CopyMem(hw->mac.perm_addr, hw->mac.addr, ETHER_MAC_ADDR_LEN);

    DEBUGPRINT(INIT, ("MAC Address = %02x:%02x:%02x:%02x:%02x:%02x\n",
        AdapterInfo->hw.mac.addr[0],
        AdapterInfo->hw.mac.addr[1],
        AdapterInfo->hw.mac.addr[2],
        AdapterInfo->hw.mac.addr[3],
        AdapterInfo->hw.mac.addr[4],
        AdapterInfo->hw.mac.addr[5]));

    return EFI_SUCCESS;
}


/** Configures internal interrupt causes on current PF.
@param[in]   AdapterInfo  Pointer to the NIC data structure information which
the UNDI driver is layering on

@return   Interrupt causes are configured for current PF
**/
VOID
I40eConfigureInterrupts(
    I40E_DRIVER_DATA *AdapterInfo
)
{
    UINT32  RegVal = 0;
    
    //
    // Associate MSI-x vector 1 to RxQ 0.
    // Function operated as PMD will not have MSI-x vector enabled in extended
    // CS or in vector control. This is fine. For PMD the internal intpr logic
    // needs to be enabled to flush the read at descriptor granularity.
    //
    RegVal = rd32(&AdapterInfo->hw, I40E_QINT_RQCTL(0));
    RegVal |= I40E_QINT_RQCTL_ITR_INDX_MASK |
        I40E_QINT_RQCTL_CAUSE_ENA_MASK |
        (0x1 << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
        I40E_QINT_RQCTL_NEXTQ_INDX_MASK |
        I40E_QUEUE_TYPE_RX << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT;
    wr32(&AdapterInfo->hw, I40E_QINT_RQCTL(0), RegVal);

    //
    // Enable intrpt cause and set to no intrpt moderation
    //
    RegVal = I40E_PFINT_DYN_CTLN_INTENA_MASK | I40E_PFINT_DYN_CTLN_CLEARPBA_MASK | I40E_PFINT_DYN_CTLN_ITR_INDX_MASK;
    wr32(&AdapterInfo->hw, I40E_PFINT_DYN_CTLN(0), RegVal);

    // 
    // Configure MSI-x vector 1 linked list to point only to RxQ 0
    //
    RegVal = rd32(&AdapterInfo->hw, I40E_PFINT_LNKLSTN(0));
    RegVal = (0 << I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT) |
        (I40E_QUEUE_TYPE_RX << I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_SHIFT);
    wr32(&AdapterInfo->hw, I40E_PFINT_LNKLSTN(0), RegVal);

    //
    // Disable intrpt automasking for PMD. Since this feature is handled at
    // device granularity, MP device drivers handling other functions of the
    // device should not assume automasking to be enabled by default.
    //
    RegVal = rd32(&AdapterInfo->hw, I40E_GLINT_CTL);
    RegVal |= I40E_GLINT_CTL_DIS_AUTOMASK_N_MASK;
    wr32(&AdapterInfo->hw, I40E_GLINT_CTL, RegVal);
}


/** Disables internal interrupt causes on current PF.
@param[in]   AdapterInfo  Pointer to the NIC data structure information which
the UNDI driver is layering on

@return   Interrupt causes are disabled for current PF
**/
VOID
I40eDisableInterrupts(
    I40E_DRIVER_DATA *AdapterInfo
)
{
    UINT16 queue;

    // Disable all non-queue interrupt causes
    wr32(&AdapterInfo->hw, I40E_PFINT_ICR0_ENA, 0);

    for (queue = 0; queue <= I40E_LAN_QP_INDEX; queue++)
    {
        // Disable receive queue interrupt causes
        wr32(&AdapterInfo->hw, I40E_QINT_RQCTL(queue), 0);
        // Disable transmit queue interrupt
        wr32(&AdapterInfo->hw, I40E_QINT_TQCTL(queue), 0);
    }
}


EFI_STATUS
I40eInitHw(
    I40E_DRIVER_DATA  *AdapterInfo
)
{
    enum i40e_status_code   i40eStatus;
    EFI_STATUS              Status;
    struct i40e_hw          *hw;
    UINT8                   AqFailures = 0;
    UINT16                  queue = 0;
    UINT32                  TmpReg0;
    UINT32                  TmpReg1;

    hw = &AdapterInfo->hw;
    hw->numPartialInit++;
    //
    //  Initialize HMC structure for this Lan function. We need 1 Tx and 1 Rx queue.
    //  FCoE parameters are zeroed
    //
#ifdef DIRECT_QUEUE_CTX_PROGRAMMING
    UNREFERENCED_1PARAMETER(i40eStatus)
#else
    i40eStatus = i40e_init_lan_hmc(hw, I40E_LAN_QP_INDEX+1, I40E_LAN_QP_INDEX+1, 0, 0);
    if (i40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_init_lan_hmc returned %d\n", i40eStatus));
        return EFI_DEVICE_ERROR;
    }

    i40eStatus = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
    if (i40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_configure_lan_hmc returned %d\n", i40eStatus));
        return EFI_DEVICE_ERROR;
    }
#endif

    //
    // Enable LFC mode. We only have 16 descriptors in the Rx ring and such configuration
    // without LFC or PFC enabled cannot handle all boot scenarios due to packet dropping.
    // Do not enable LFC if Receive Link Flow Control or PFC is already enabled
    //
    TmpReg0 = i40eRead32(AdapterInfo, I40E_PRTDCB_MFLCN);
    TmpReg1 = i40eRead32(AdapterInfo, I40E_PRTMAC_HSEC_CTL_RX_ENABLE_PPP);

    //
    // I40E_PRTMAC_HSEC_CTL_RX_ENABLE_PPP contents may be invalid in 10G mode
    //
    if (TmpReg1 == 0xDEADBEEF) {
        TmpReg1 = 0x0;
    }

    if (((TmpReg0 & I40E_PRTDCB_MFLCN_RPFCM_MASK) == 0) &&
        ((TmpReg0 & I40E_PRTDCB_MFLCN_RFCE_MASK) == 0) &&
        ((TmpReg1 & I40E_PRTMAC_HSEC_CTL_RX_ENABLE_PPP_HSEC_CTL_RX_ENABLE_PPP_MASK) == 0)) {
        UINT32 ManagementCtrlReg = 0;
        //
        // We will not check the status here. We need to proceed with initialization 
        // even if FC cannot be turned on.
        //
        ManagementCtrlReg = rd32(&AdapterInfo->hw, I40E_PRT_MNG_MANC);
        if ((ManagementCtrlReg & I40E_PRT_MNG_MANC_EN_BMC2NET_MASK) &&
            (ManagementCtrlReg & I40E_PRT_MNG_MANC_RCV_TCO_EN_MASK)) {
            //
            // If the port is used for management traffic do not set flow control
            // HSD7661281 and HSD7660426
            //
        }
        else {
            AdapterInfo->hw.fc.requested_mode = I40E_FC_NONE;

            AdapterInfo->WaitingForLinkUp = IsLinkUp(AdapterInfo);
            i40e_set_fc(&AdapterInfo->hw, &AqFailures, TRUE);
        }
    }

    Status = I40eSetupPFSwitch(AdapterInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("I40eSetupPFSwitch returned %r\n", Status));
        return Status;
    }

    i40e_add_filter_to_drop_tx_flow_control_frames(hw, AdapterInfo->vsi.seid);

    i40eStatus = i40e_aq_set_port_parameters(hw, 0, FALSE, TRUE, FALSE, NULL);
    if (i40eStatus != I40E_SUCCESS)
    {
        DEBUGPRINT(CRITICAL, ("i40e_aq_set_port_parameters returned %d\n", i40eStatus));
        return EFI_DEVICE_ERROR;
    }

    for (queue = 0; queue <= I40E_LAN_QP_INDEX; queue++)
    {
        Status = I40eSetupTxRxResources(AdapterInfo, queue);
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("I40eSetupTxRxResources returned %r\n", Status));
            return Status;
        }

        Status = I40eConfigureTxRxQueues(AdapterInfo, queue);
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("I40eConfigureTxRxQueues returned %r\n", Status));
            return Status;
        }

        //
        // Bring up the connection 
        // i40e_up_complete in linux driver
        // Configure legacy mode interrupts in the HW: i40e_configure_msi_and_legacy
        //
        I40eReceiveStart(AdapterInfo, queue);
        //  
        //  Enable interrupts generation
        //  i40e_irq_dynamic_enable_icr0
        // 
    }

    I40eConfigureInterrupts(AdapterInfo);

    AdapterInfo->vsi.txRxQP = I40E_LAN_QP_INDEX;
    AdapterInfo->HwInitialized = TRUE;
    return Status;
}


PXE_STATCODE
I40eInitialize(
    I40E_DRIVER_DATA *AdapterInfo
)
{
#ifndef AVOID_HW_REINITIALIZATION
    EFI_STATUS    Status;
#endif
    PXE_STATCODE  PxeStatcode;

    DEBUGPRINT(INIT, ("-->\n"));

    PxeStatcode = PXE_STATCODE_SUCCESS;

    //
    // Do not try to initialize hw again when it is already initialized
    //
    if (AdapterInfo->HwInitialized == FALSE) {
        DEBUGPRINT(INIT, ("Hw is not initialized, calling I40eInitHw\n"));
#ifndef AVOID_HW_REINITIALIZATION    
        Status = I40eInitHw(AdapterInfo);
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("I40eInitHw returns %r\n", Status));
            PxeStatcode = PXE_STATCODE_NOT_STARTED;
        }
#endif
    }
    AdapterInfo->DriverBusy = FALSE;
    return PxeStatcode;
}


PXE_STATCODE
I40eShutdown(
    I40E_DRIVER_DATA *AdapterInfo
)
{
    PXE_STATCODE            PxeStatcode;
    UINT16                  queue;
#ifndef AVOID_HW_REINITIALIZATION    
    enum i40e_status_code   I40eStatus = I40E_SUCCESS;
#endif

    DEBUGPRINT(INIT, ("-->\n"));
#if(0)
    if (AdapterInfo->hw.bus.func == 1) {
        DumpInternalFwHwData(AdapterInfo);
    }
#endif
    AdapterInfo->hw.numShutdown++;

    if (AdapterInfo->HwInitialized == FALSE) {
        PxeStatcode = PXE_STATCODE_SUCCESS;
        return PxeStatcode;
    }

#ifndef AVOID_HW_REINITIALIZATION    
    for (queue = 0; queue <= I40E_LAN_QP_INDEX; queue++)
    {

        I40eStatus = I40eReceiveStop(AdapterInfo, queue);
        if (I40eStatus != I40E_SUCCESS) {
            DEBUGPRINT(CRITICAL, ("I40eReceiveStop returned %d\n", I40eStatus));
        }

        I40eStatus = I40eFreeTxRxQueues(AdapterInfo, queue);
        if (I40eStatus != I40E_SUCCESS) {
            DEBUGPRINT(CRITICAL, ("I40eFreeTxRxQueues returned %d\n", I40eStatus));
        }

        I40eStatus = I40eFreeTxRxResources(AdapterInfo, queue);
        if (I40eStatus != I40E_SUCCESS) {
            DEBUGPRINT(CRITICAL, ("I40eFreeTxRxResources returned %d\n", I40eStatus));
        }
    }
#ifndef DIRECT_QUEUE_CTX_PROGRAMMING
    I40eStatus = i40e_shutdown_lan_hmc(&AdapterInfo->hw);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_shutdown_lan_hmc returned %d\n", I40eStatus));
    }
#endif
    AdapterInfo->HwInitialized = FALSE;
#endif
    PxeStatcode = PXE_STATCODE_SUCCESS;
    return PxeStatcode;
}


PXE_STATCODE
I40eReset(
    I40E_DRIVER_DATA *AdapterInfo,
    UINT16           OpFlags
)
{
    PXE_STATCODE  PxeStatcode;
#ifndef AVOID_HW_REINITIALIZATION    
    EFI_STATUS    Status;
#endif
    AdapterInfo->hw.numReset++;
    DEBUGPRINT(INIT, ("-->\n"));

    //
    // Do not reinitialize the adapter when it has already been initialized
    // This saves the time required for initialization
    //
    if (AdapterInfo->HwInitialized == FALSE) {
#ifndef AVOID_HW_REINITIALIZATION    
        Status = I40eInitHw(AdapterInfo);
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("XgbeInitHw returns %r\n", Status));
            return PXE_STATCODE_NOT_STARTED;
        }
#endif
    }
    else {
        DEBUGPRINT(I40E, ("Skipping adapter reset\n"));
    }

    PxeStatcode = PXE_STATCODE_SUCCESS;
    return PxeStatcode;
}


EFI_STATUS
I40eDiscoverCapabilities(
    I40E_DRIVER_DATA *AdapterInfo
)
/*++

Routine Description:
  Read function capabilities using AQ command.

Arguments:
  AdapterInfo - Pointer to adapter structure

Returns:
  EFI_STATUS

--*/
{
    struct i40e_aqc_list_capabilities_element_resp  *CapabilitiesBuffer;
    EFI_STATUS                                      Status;
    enum i40e_status_code                           I40eStatus;
    UINT16                                          BufferSize;
    UINT16                                          BufferSizeNeeded;

    BufferSize = 40 * sizeof(struct i40e_aqc_list_capabilities_element_resp);

    do {
#ifndef INTEL_KDNET
        Status = gBS->AllocatePool(
            EfiBootServicesData,
            BufferSize,
            (VOID **)&CapabilitiesBuffer
        );
        if (EFI_ERROR(Status)) {
            return Status;
        }
#else
        struct i40e_aqc_list_capabilities_element_resp CapabilitiesBufferArray[40];
        CapabilitiesBuffer = CapabilitiesBufferArray;
        Status = EFI_SUCCESS;
#endif
        I40eStatus = i40e_aq_discover_capabilities(
            &AdapterInfo->hw,
            CapabilitiesBuffer,
            BufferSize,
            &BufferSizeNeeded,
            i40e_aqc_opc_list_func_capabilities,
            NULL
        );
        //
        // Free memory that was required only internally 
        // in i40e_aq_discover_capabilities
        //
#ifndef INTEL_KDNET
        gBS->FreePool(CapabilitiesBuffer);
#endif

        if (AdapterInfo->hw.aq.asq_last_status == I40E_AQ_RC_ENOMEM) {
            //
            // Buffer passed was to small, use buffer size returned by the function
            //
            BufferSize = BufferSizeNeeded;
        }
        else if (AdapterInfo->hw.aq.asq_last_status != I40E_AQ_RC_OK) {
            Status = EFI_DEVICE_ERROR;
            return Status;
        }
    } while (I40eStatus != I40E_SUCCESS);

    return Status;
}


EFI_STATUS
I40eCheckResetDone(
    I40E_DRIVER_DATA  *AdapterInfo,
    UINT32            ResetMask
)
{
    EFI_STATUS      Status;
    UINT32          Reg;
    UINTN           i = 0;
    struct i40e_hw  *hw;

    hw = &AdapterInfo->hw;
    Status = EFI_SUCCESS;


#define I40E_GLNVM_ULD_TIMEOUT    1000000

    // 
    // First wait until device becomes active.
    //
    while (1) {
        Reg = rd32(hw, I40E_GLGEN_RSTAT);
        if ((Reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK) == 0) {
            break;
        }
        DelayInMicroseconds(AdapterInfo, 100);
        if (i++ > I40E_GLNVM_ULD_TIMEOUT) {
            DEBUGPRINT(CRITICAL, ("Device activation error\n"));
            Status = EFI_DEVICE_ERROR;
            break;
        }
    }

    i = 0;

    //
    // Now wait for reset done indication.
    //
    Reg = rd32(hw, I40E_GLNVM_ULD);
    while (1) {
        Reg = rd32(hw, I40E_GLNVM_ULD);
        if ((Reg & ResetMask) == ResetMask) {
            break;
        }
        DelayInMicroseconds(AdapterInfo, 100);
        if (i++ > I40E_GLNVM_ULD_TIMEOUT) {
            DEBUGPRINT(CRITICAL, ("Timeout waiting for reset done\n"));
            Status = EFI_DEVICE_ERROR;
            break;
        }
    }

    return Status;
}


EFI_STATUS
I40eTriggerGlobalReset(
    I40E_DRIVER_DATA  *AdapterInfo
)
{
    UINT32           Reg;
    struct  i40e_hw  *Hw;

    Hw = &AdapterInfo->hw;

    Reg = 0x1 << I40E_GLGEN_RTRIG_GLOBR_SHIFT;
    wr32(Hw, I40E_GLGEN_RTRIG, Reg);


    return EFI_SUCCESS;
}


EFI_STATUS
i40eTmpMfpInitialization(
    I40E_DRIVER_DATA  *AdapterInfo
)
{
    enum  i40e_status_code I40eStatus;
    UINT32                 DWords[30];
    BOOLEAN                ResetNeeded;

    DWords[0] = 0x80000001; // Port enabled
    DWords[1] = 0x80000001; // Advertise 10G speed
    DWords[2] = 0x00000000; // Enable Rx flow control
    DWords[3] = 0x00000000; // Enable Tx flow control
    DWords[4] = 0x80000050; // VLAN Tag for DCC
    DWords[5] = 0x80000001; // Ethernet only
    DWords[6] = 0x80000000; // UP
    DWords[7] = 0x12233222; // Lower MAC DW
    DWords[8] = 0x80005566; // Upper MAC DW
    DWords[9] = 0x80000060; // Outer vlan tag
    DWords[10] = 0x8000000A; // Min BW
    DWords[11] = 0x8000000A; // Max BW
    DWords[12] = 0x80000001; // Boot enabled
    DWords[13] = 0x80000001; // PF enabled
    DWords[14] = 0xC0000000; // SRIOV auto

    I40eStatus = i40e_aq_alternate_write_indirect(&AdapterInfo->hw, 32, 15, DWords);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_alternate_write_indirect returned  %x\n", I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    DWords[7] = 0x12233333; // Lower MAC DW
    DWords[8] = 0x80005566; // Upper MAC DW
    DWords[9] = 0x80000061; // Outer vlan tag

    I40eStatus = i40e_aq_alternate_write_indirect(&AdapterInfo->hw, 32 + 30, 15, DWords);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_alternate_write_indirect returned  %x\n", I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    DWords[7] = 0x12233444; // Lower MAC DW
    DWords[8] = 0x80005566; // Upper MAC DW
    DWords[9] = 0x80000062; // Outer vlan tag
    DWords[13] = 0x80000000; // PF enabled

    I40eStatus = i40e_aq_alternate_write_indirect(&AdapterInfo->hw, 32 + 60, 15, DWords);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_alternate_write_indirect returned  %x\n", I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    DWords[7] = 0x12233555; // Lower MAC DW
    DWords[8] = 0x80005566; // Upper MAC DW
    DWords[9] = 0x80000063; // Outer vlan tag
    DWords[13] = 0x80000000; // PF enabled

    I40eStatus = i40e_aq_alternate_write_indirect(&AdapterInfo->hw, 32 + 90, 15, DWords);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_alternate_write_indirect returned  %x\n", I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    I40eStatus = i40e_aq_alternate_write_done(&AdapterInfo->hw, 0, &ResetNeeded);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_alternate_write_done returned  %x\n", I40eStatus));
        return EFI_DEVICE_ERROR;
    }

    DEBUGPRINT(INIT, ("Done with MFP Init\n"));
#ifndef INTEL_KDNET
    gBS->Stall(1000000);
#else
    KeStallExecutionProcessor(1000000);
#endif

    return EFI_SUCCESS;

}


BOOLEAN
I40eAquireControllerHw(
    I40E_DRIVER_DATA  *AdapterInfo
)
/*++

Routine Description:
  This function checks if any  other instance of driver is loaded on this PF by
  reading PFGEN_DRUN register.
  If not it writes the bit in the register to let know other components that
  the PF is in use.

Arguments:
  AdapterInfo - Pointer to adapter structure

Returns:
  TRUE - the PF is free to use for Tx/Rx

--*/
{
    UINT32   RegValue;

    RegValue = rd32(&AdapterInfo->hw, I40E_PFGEN_DRUN);
    if (RegValue & I40E_PFGEN_DRUN_DRVUNLD_MASK) {
        //
        // bit set means other driver is loaded on this pf
        //
        return FALSE;
    }
    RegValue |= I40E_PFGEN_DRUN_DRVUNLD_MASK;
    wr32(&AdapterInfo->hw, I40E_PFGEN_DRUN, RegValue);
    return TRUE;
}


VOID
I40eReleaseControllerHw(
    I40E_DRIVER_DATA  *AdapterInfo
)
/*++

Routine Description:
  Release this PF by clearing the bit in PFGEN_DRUN register.

Arguments:
  AdapterInfo - Pointer to adapter structure

Returns:

--*/
{
    UINT32   RegValue;

    RegValue = rd32(&AdapterInfo->hw, I40E_PFGEN_DRUN);
    RegValue &= ~I40E_PFGEN_DRUN_DRVUNLD_MASK;
    wr32(&AdapterInfo->hw, I40E_PFGEN_DRUN, RegValue);
}


static inline void i40e_flex_payload_reg_init(struct i40e_hw *hw)
{
    wr32(hw, I40E_GLQF_ORT(18), 0x00000030);
    wr32(hw, I40E_GLQF_ORT(19), 0x00000030);
    wr32(hw, I40E_GLQF_ORT(26), 0x0000002B);
    wr32(hw, I40E_GLQF_ORT(30), 0x0000002B);
    wr32(hw, I40E_GLQF_ORT(33), 0x000000E0);
    wr32(hw, I40E_GLQF_ORT(34), 0x000000E3);
    wr32(hw, I40E_GLQF_ORT(35), 0x000000E6);
    wr32(hw, I40E_GLQF_ORT(20), 0x00000031);
    wr32(hw, I40E_GLQF_ORT(23), 0x00000031);
    wr32(hw, I40E_GLQF_ORT(63), 0x0000002D);

    /* GLQF_PIT Registers */
    wr32(hw, I40E_GLQF_PIT(16), 0x00007480);
    wr32(hw, I40E_GLQF_PIT(17), 0x00007440);
}


EFI_STATUS
i40eFirstTimeInit(
    I40E_DRIVER_DATA  *AdapterInfo
)
/*++

Routine Description:
  This function is called as early as possible during driver start to ensure the
  hardware has enough time to autonegotiate when the real SNP device initialize call
  is made.

Arguments:
  AdapterInfo - Pointer to adapter structure

Returns:
  EFI_STATUS

--*/
{
    PCI_CONFIG_HEADER      *PciConfigHeader;
    enum  i40e_status_code I40eStatus;
    EFI_STATUS             Status;
    struct  i40e_hw        *hw;

    hw = &AdapterInfo->hw;
    hw->numFullInit++;

    hw->back = AdapterInfo;

    AdapterInfo->DriverBusy = FALSE;
    if (rd32(hw, I40E_GLPCI_CAPSUP) & I40E_GLPCI_CAPSUP_ARI_EN_MASK) {
        DEBUGPRINT(INIT, ("ARI Enabled\n"));
        AdapterInfo->AriCapabilityEnabled = TRUE;
    }

    if (AdapterInfo->AriCapabilityEnabled) {
        //
        // Reinterpret PCI Function and Device when ARI is enabled
        //
        AdapterInfo->Function = (8 * AdapterInfo->Device) + AdapterInfo->Function;
        AdapterInfo->Device = 0;
    }

    AdapterInfo->MediaStatusChecked = FALSE;
    AdapterInfo->LastMediaStatus = FALSE;

    hw->bus.device = (UINT16)AdapterInfo->Device;
    hw->bus.func = (UINT16)AdapterInfo->Function;

    PciConfigHeader = (PCI_CONFIG_HEADER *)&AdapterInfo->PciConfig[0];

    DEBUGPRINT(INIT, ("PCI Command Register = %X\n", PciConfigHeader->Command));
    DEBUGPRINT(INIT, ("PCI Status Register = %X\n", PciConfigHeader->Status));
    DEBUGPRINT(INIT, ("PCI VendorID = %X\n", PciConfigHeader->VendorID));
    DEBUGPRINT(INIT, ("PCI DeviceID = %X\n", PciConfigHeader->DeviceID));
    DEBUGPRINT(INIT, ("PCI SubVendorID = %X\n", PciConfigHeader->SubVendorID));
    DEBUGPRINT(INIT, ("PCI SubSystemID = %X\n", PciConfigHeader->SubSystemID));
    // DEBUGPRINT (INIT, ("PCI Segment = %X\n", AdapterInfo->Segment));
    DEBUGPRINT(INIT, ("PCI Bus = %X\n", AdapterInfo->Bus));
    DEBUGPRINT(INIT, ("PCI Device = %X\n", AdapterInfo->Device));
    DEBUGPRINT(INIT, ("PCI Function = %X\n", AdapterInfo->Function));

    ZeroMem(AdapterInfo->BroadcastNodeAddress, PXE_MAC_LENGTH);
    SetMem(AdapterInfo->BroadcastNodeAddress, PXE_HWADDR_LEN_ETHER, 0xFF);
    ZeroMem(&AdapterInfo->vsi.CurrentMcastList, sizeof(AdapterInfo->vsi.CurrentMcastList));

    //
    // Initialize all parameters needed for the shared code
    //
    hw->hw_addr = (UINT8*)(UINTN)PciConfigHeader->BaseAddressReg_0;
    hw->vendor_id = PciConfigHeader->VendorID;
    hw->device_id = PciConfigHeader->DeviceID;
    hw->revision_id = (UINT8)PciConfigHeader->RevID;
    hw->subsystem_vendor_id = PciConfigHeader->SubVendorID;
    hw->subsystem_device_id = PciConfigHeader->SubSystemID;
    hw->revision_id = (UINT8)PciConfigHeader->RevID;

    hw->adapter_stopped = TRUE;

#define PCI_CLASS_MASK          0xFF00
#define PCI_SUBCLASS_MASK       0x00FF
    AdapterInfo->PciClass = (UINT8)((PciConfigHeader->ClassID & PCI_CLASS_MASK) >> 8);
    AdapterInfo->PciSubClass = (UINT8)(PciConfigHeader->ClassID) & PCI_SUBCLASS_MASK;

#ifndef FORTVILLE_A0_SUPPORT
    if (hw->subsystem_device_id == 0) {
        //
        // Read Subsystem ID from PFPCI_SUBSYSID
        //
        hw->subsystem_device_id = (UINT16)(rd32(hw, 0x000BE100) & 0xFFFF);
    }
#endif /* FORTVILLE_A0_SUPPORT */
    //
    // Find out if this function is already used by legacy component  
    //
    AdapterInfo->UNDIEnabled = I40eAquireControllerHw(AdapterInfo);
    DEBUGPRINT(INIT, ("I40eAquireControllerHw returned %d\n", AdapterInfo->UNDIEnabled));

    //
    // Setup AQ initialization parameters: 32 descriptor rings, 4kB buffers
    //
    hw->aq.num_arq_entries = I40E_NUM_AQ_DESCRIPTORS;
    hw->aq.num_asq_entries = I40E_NUM_AQ_DESCRIPTORS;
    hw->aq.arq_buf_size = I40E_NUM_AQ_BUF_SIZE;
    hw->aq.asq_buf_size = I40E_NUM_AQ_BUF_SIZE;

    I40eStatus = i40e_init_shared_code(hw);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_init_shared_code returned %d\n", I40eStatus));
        Status = EFI_DEVICE_ERROR;
        goto ErrorReleaseController;
    }

    i40e_flex_payload_reg_init(hw);

#ifdef INTEL_KDNET
    //
    // Page aligned device allocated host memory. Allocated as part of 
    // KdGetHardwareContextSize()
    //
    AdapterInfo->MemoryPtr = (UINT64)(UINTN)GetDeviceMemory();
#endif

    if (AdapterInfo->UNDIEnabled) {
        //
        // Do not execute PF Reset if we do not initialize UNDI
        // This would break Tx/Rx of legacy driver working in background 
        //
        i40e_clear_hw(hw);
        I40eStatus = i40e_pf_reset(hw);
        if (I40eStatus != I40E_SUCCESS) {
            DEBUGPRINT(CRITICAL, ("i40e_pf_reset failed %d\n", I40eStatus));
            Status = EFI_DEVICE_ERROR;
            goto ErrorReleaseController;
        }
    }

    DEBUGPRINT(INIT, ("Initializing PF %d\n", hw->bus.func));

    AdapterInfo->FwVersionSupported = TRUE;

    I40eStatus = i40e_init_adminq(hw);
    if (I40eStatus == I40E_ERR_FIRMWARE_API_VERSION) {
        //
        // Firmware version is newer then expected. Refrain from further initialization 
        // and report error status thru the Driver Health Protocol
        //
        AdapterInfo->FwVersionSupported = FALSE;
        return EFI_UNSUPPORTED;
    }

    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_init_adminq returned %d\n", I40eStatus));
        Status = EFI_DEVICE_ERROR;
        goto ErrorReleaseController;
    }

    i40e_clear_pxe_mode(hw);

    DEBUGPRINT(INIT, ("FW API Info: api_maj_ver: %x, api_min_ver: %x\n", hw->aq.api_maj_ver, hw->aq.api_min_ver));

    Status = I40eDiscoverCapabilities(AdapterInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("I40eDiscoverCapabilities(func) returned %r\n", Status));
        goto ErrorReleaseController;
    }

    // Wait at least 40ms due to I2C access issues
#ifndef INTEL_KDNET
    gBS->Stall(100 * 1000);
#else
    KeStallExecutionProcessor(100 * 1000);
#endif

    AdapterInfo->QualifiedSfpModule = IsQualifiedSfpModule(AdapterInfo);

    //
    // Determine existing PF/Port configuration
    // This is to correctly match partitions, pfs and ports 
    //
    Status = ReadMfpConfiguration(AdapterInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("ReadMfpConfiguration returned %r\n", Status));
        goto ErrorReleaseController;
    }

    if (AdapterInfo->UNDIEnabled) {
        return EFI_SUCCESS;
    }
    else {
        return EFI_ACCESS_DENIED;
    }

ErrorReleaseController:
    I40eReleaseControllerHw(AdapterInfo);
    return Status;
};

#define VPDADDR 0xE2
#define VPDDATA 0xE4
#define VPD_READ_MASK  0x0000
#define VPD_WRITE_MASK  0x8000
#define VPD_FLAG_MASK  0x8000

#ifdef VPD_CONFIG_ACCESS
EFI_STATUS
ReadVPDRegion(
    IN I40E_DRIVER_DATA *AdapterInfo,
    UINT32              *Buffer,
    UINTN                BufferSize
)
{
    UINTN   i;
    UINT32  Data;
    UINT16  Address;
    
    for (i = 0; i < BufferSize; i++) {
        Address = VPD_READ_MASK;
        Address |= sizeof(UINT32) * i;
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint16,
            VPDADDR,
            1,
            &Address
        );
        do {
            AdapterInfo->PciIo->Pci.Read(
                AdapterInfo->PciIo,
                EfiPciIoWidthUint16,
                VPDADDR,
                1,
                &Address
            );
        } while ((Address & VPD_FLAG_MASK) != VPD_WRITE_MASK);

        AdapterInfo->PciIo->Pci.Read(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            VPDDATA,
            1,
            &Data
        );
        Buffer[i] = Data;
    }
    return EFI_SUCCESS;
}


EFI_STATUS
WriteVPDRegion(
    IN I40E_DRIVER_DATA *AdapterInfo,
    UINT32              *Buffer,
    UINTN                BufferSize
)
{
    UINTN i;
    UINT32  Data;
    UINT16  Address;
    
    for (i = 0; i < BufferSize; i++) {

        Address = VPD_WRITE_MASK;
        Address |= sizeof(UINT32) * i;
        Data = Buffer[i];
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            VPDDATA,
            1,
            &Data
        );


        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint16,
            VPDADDR,
            1,
            &Address
        );
        do {
            AdapterInfo->PciIo->Pci.Read(
                AdapterInfo->PciIo,
                EfiPciIoWidthUint16,
                VPDADDR,
                1,
                &Address
            );
        } while ((Address & VPD_FLAG_MASK) != VPD_READ_MASK);
    }

    return EFI_SUCCESS;

}
#endif /* VPD_CONFIG_ACCESS */

#define IOADDR 0x98
#define IODATA 0x9C


UINT32
i40eRead32(
    IN I40E_DRIVER_DATA *AdapterInfo,
    IN UINT32           Port
)
/*++

Routine Description:
  This function calls the MemIo callback to read a dword from the device's
  address space

Arguments:
  AdapterInfo       - Adapter structure
  Port              - Address to read from

Returns:
  Results           - The data read from the port.

--*/
{
    UINT32  Results;

#ifdef CONFIG_ACCESS_TO_CSRS
    {
        UINT32 ConfigSpaceAddress;

        ConfigSpaceAddress = Port | 0x80000000;
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            IOADDR,
            1,
            &ConfigSpaceAddress
        );
        AdapterInfo->PciIo->Pci.Read(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            IODATA,
            1,
            &Results
        );
        ConfigSpaceAddress = 0;
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            IOADDR,
            1,
            &ConfigSpaceAddress
        );
    }
#else
    MemoryFence();
#ifndef INTEL_KDNET
    AdapterInfo->PciIo->Mem.Read(
        AdapterInfo->PciIo,
        EfiPciIoWidthUint32,
        0,
        Port,
        1,
        (VOID *)(&Results)
    );
#else
    ReadDeviceMemory((VOID *)(&Results),
        0,
        Port,
        sizeof(UINT32),
        1);
#endif // INTEL_KDNET
    MemoryFence();
#endif
    return Results;
}


VOID
i40eWrite32(
    IN I40E_DRIVER_DATA *AdapterInfo,
    IN UINT32           Port,
    IN UINT32           Data
)
/*++

Routine Description:
  This function calls the MemIo callback to write a word from the device's
  address space

Arguments:
  AdapterInfo       - Adapter structure
  Data              - Data to write to Port
  Port              - Address to write to

Returns:
  none

--*/
{
    UINT32  Value;

    Value = Data;

#ifdef CONFIG_ACCESS_TO_CSRS
    {
        UINT32 ConfigSpaceAddress;

        ConfigSpaceAddress = Port | 0x80000000;
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            IOADDR,
            1,
            &ConfigSpaceAddress
        );
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            IODATA,
            1,
            &Value
        );
        ConfigSpaceAddress = 0;
        AdapterInfo->PciIo->Pci.Write(
            AdapterInfo->PciIo,
            EfiPciIoWidthUint32,
            IOADDR,
            1,
            &ConfigSpaceAddress
        );
    }
#else
    MemoryFence();
#ifndef INTEL_KDNET
    AdapterInfo->PciIo->Mem.Write(
        AdapterInfo->PciIo,
        EfiPciIoWidthUint32,
        0,
        Port,
        1,
        (VOID *)(&Value)
    );
#else
    WriteDeviceMemory((VOID *)(&Value),
        0,
        Port,
        sizeof(UINT32),
        1);

#endif // INTEL_KDNET
    MemoryFence();
#endif
    return;
}


VOID
DelayInMicroseconds(
    IN I40E_DRIVER_DATA  *AdapterInfo,
    UINT32               MicroSeconds
)
/*++

Routine Description:

Arguments:
  AdapterInfo                     - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  MicroSeconds                    - Time to delay in Microseconds.
Returns:

--*/
{
    if (AdapterInfo->Delay != NULL) {
        (*AdapterInfo->Delay) (AdapterInfo->Unique_ID, MicroSeconds);
    }
    else {
#ifndef INTEL_KDNET
        gBS->Stall(MicroSeconds);
#else
        KeStallExecutionProcessor(MicroSeconds);
#endif
    }
}


#ifndef INTEL_KDNET
enum i40e_status_code
    i40eAllocateMem(
        struct i40e_hw *hw,
        struct i40e_virt_mem *mem,
        u32 size
    )
    /*++
    Routine Description:
        OS specific memory alloc for shared code

    Arguments:
        hw     - pointer to the HW structure
        mem    - ptr to mem struct to fill out
        size   - size of memory requested

    Returns
        i40e_status_code

    --*/
{
    EFI_STATUS  Status;

    if (NULL == mem) {
        return I40E_ERR_PARAM;
    }

    mem->size = size;
    mem->va = NULL;

    Status = gBS->AllocatePool(
        EfiBootServicesData,
        size,
        (VOID **)&mem->va
    );

    if ((NULL != mem->va) && (Status == EFI_SUCCESS)) {
        //DEBUGPRINT(INIT, ("%a: Requested: %d, Allocated size: %d\n",
        //                   __FUNCTION__, size, mem->size));
        ZeroMem(mem->va, size);
        return I40E_SUCCESS;
    }
    else {
        DEBUGPRINT(CRITICAL, ("Error: Requested: %d, Allocated size: %d\n", size, mem->size));
        return I40E_ERR_NO_MEMORY;
    }
}
#endif


enum i40e_status_code
    i40eFreeMem(
        struct i40e_hw *hw,
        struct i40e_virt_mem *mem
    )
    /*++
    Routine Description:
        OS specific memory free for shared code

    Arguments:
        hw     - pointer to the HW structure
        mem    - ptr to mem struct to fill out

    Returns
        i40e_status_code

    --*/
{
    if (NULL == mem) {
        return I40E_ERR_PARAM;
    }

    if (NULL == mem->va) {
        //
        //  Nothing to free
        //
        return I40E_SUCCESS;
    }
#ifndef INTEL_KDNET
    gBS->FreePool((VOID *)mem->va);    
#else
    ZeroMem(mem->va, mem->size);
    mem->va = NULL;
    mem->size = 0;
#endif
    return I40E_SUCCESS;
}


#ifndef INTEL_KDNET
/**
 * i40e_init_spinlock_d - OS specific spinlock init for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
void i40eInitSpinLock(struct i40e_spinlock *sp)
{
#ifndef INTEL_KDNET
    InitializeSpinLock(&sp->SpinLock);
#else
    InitializeSpinLock(&sp->Lock);
#endif
}


/**
 * i40e_acquire_spinlock_d - OS specific spinlock acquire for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
void i40eAcquireSpinLock(struct i40e_spinlock *sp)
{
#ifndef INTEL_KDNET
    AcquireSpinLock(&sp->SpinLock);
#else
    AcquireSpinLock(&sp->Lock);
#endif
}


/**
 * i40e_release_spinlock_d - OS specific spinlock release for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
void i40eReleaseSpinLock(struct i40e_spinlock *sp)
{
#ifndef INTEL_KDNET
    ReleaseSpinLock(&sp->SpinLock);
#else
    ReleaseSpinLock(&sp->Lock);
#endif
}


/**
 * i40e_destroy_spinlock_d - OS specific spinlock destroy for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
void i40eDestroySpinLock(struct i40e_spinlock *sp)
{
    return;
}

#endif


enum i40e_status_code
    i40eAllocateDmaMem(
        struct i40e_hw *hw,
        struct i40e_dma_mem *mem,
        u64 size,
        u32 alignment)
{
    EFI_STATUS        Status;
    I40E_DRIVER_DATA  *AdapterInfo = (I40E_DRIVER_DATA *)hw->back;

    if (NULL == mem) {
        return I40E_ERR_PARAM;
    }

    mem->size = (UINT32)ALIGN(size, alignment);

#ifndef INTEL_KDNET
    //
    // Allocate memory for transmit and receive resources.
    //
    Status = AdapterInfo->PciIo->AllocateBuffer(
        AdapterInfo->PciIo,
        AllocateAnyPages,
        EfiBootServicesData,
        UNDI_MEM_PAGES(mem->size),
        (VOID **)&mem->va,
        0
    );

    mem->pa = (UINT64)mem->va;

    if ((NULL != mem->va) && (EFI_SUCCESS == Status)) {
        Status = I40E_SUCCESS;
    }
    else {
        DEBUGPRINT(CRITICAL, ("Error: Requested: %d, Allocated size: %d\n",
            size, mem->size));
        Status =  I40E_ERR_NO_MEMORY;
    }
#else
    AdapterInfo->MemoryPtr = ALIGN(AdapterInfo->MemoryPtr, alignment);
    mem->va = (VOID *)(UINTN)AdapterInfo->MemoryPtr;
    i40eMapMem(
        AdapterInfo,
        (UINT64)(UINTN)mem->va,
        mem->size,
        &mem->pa);
    AdapterInfo->MemoryPtr += (UINT64)mem->size;

    Status =  EFI_SUCCESS;
#endif
    return Status;
}


enum i40e_status_code
    i40eFreeDmaMem(
        struct i40e_hw *hw,
        struct i40e_dma_mem *mem
    )
{
#ifndef INTEL_KDNET
    EFI_STATUS  Status;
#endif

    I40E_DRIVER_DATA  *AdapterInfo = (I40E_DRIVER_DATA *)hw->back;

    if (NULL == mem) {
        return I40E_ERR_PARAM;
    }
#ifndef INTEL_KDNET
    //
    // Free memory allocated for transmit and receive resources.
    //
    Status = AdapterInfo->PciIo->FreeBuffer(
        AdapterInfo->PciIo,
        UNDI_MEM_PAGES(mem->size),
        (VOID *)mem->va
    );
    if (EFI_ERROR(Status))
    {
        return I40E_ERR_BAD_PTR;
    }
#else
    i40eUnMapMem(
        AdapterInfo,
        (UINT64)mem->va,
        mem->size,
        mem->pa
    );
    mem->va = NULL;
    mem->pa = 0;
    mem->size = 0;
#endif
    return I40E_SUCCESS;
}


VOID
i40eBlockIt(
    IN  I40E_DRIVER_DATA *AdapterInfo,
    IN UINT32           flag
)
/*++

Routine Description:

Arguments:
  AdapterInfo                      - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  flag                             - Block flag
Returns:

--*/
{
    if (AdapterInfo->Block != NULL) {
        (*AdapterInfo->Block) (AdapterInfo->Unique_ID, flag);
    }
    else {
#ifndef INTEL_KDNET
        if (gInitializeLock) {
            EfiInitializeLock(&gLock, TPL_NOTIFY);
            gInitializeLock = FALSE;
        }

        if (flag != 0) {
            EfiAcquireLock(&gLock);
        }
        else {
            EfiReleaseLock(&gLock);
        }
#endif
    }
}


BOOLEAN
IsLinkUp(
    IN  I40E_DRIVER_DATA *AdapterInfo
)
{
    enum i40e_status_code I40eStatus;

    I40eStatus = i40e_aq_get_link_info(&AdapterInfo->hw, TRUE, NULL, NULL);
    if (I40eStatus != I40E_SUCCESS) {
        return FALSE;
    }
    return AdapterInfo->hw.phy.link_info.link_info & I40E_AQ_LINK_UP;
}


EFI_STATUS
GetLinkSpeedCapability(
    IN  I40E_DRIVER_DATA   *AdapterInfo,
    OUT UINT8              *AllowedLinkSpeeds
)
{
    enum i40e_status_code I40eStatus;
    struct i40e_aq_get_phy_abilities_resp PhyAbilites;

    //
    // Use AQ command to retrieve phy capabilities
    //
    I40eStatus = i40e_aq_get_phy_capabilities(
        &AdapterInfo->hw,
        FALSE,
        FALSE,
        &PhyAbilites,
        NULL
    );

    if (I40eStatus != I40E_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }

    *AllowedLinkSpeeds = PhyAbilites.link_speed;

    return EFI_SUCCESS;
}


#ifndef INTEL_KDNET
UINT8
GetLinkSpeed(
    IN  I40E_DRIVER_DATA   *AdapterInfo
)
{
    enum   i40e_aq_link_speed Speed;
    UINT8                     LinkSpeed;

    LinkSpeed = LINK_SPEED_UNKNOWN;

    Speed = i40e_get_link_speed(&AdapterInfo->hw);

    switch (Speed) {
    case I40E_LINK_SPEED_100MB:
        LinkSpeed = LINK_SPEED_100FULL;
        break;
    case I40E_LINK_SPEED_1GB:
        LinkSpeed = LINK_SPEED_1000FULL;
        break;
    case I40E_LINK_SPEED_10GB:
        LinkSpeed = LINK_SPEED_10000FULL;
        break;
    case I40E_LINK_SPEED_20GB:
        LinkSpeed = LINK_SPEED_20000;
        break;
    case I40E_LINK_SPEED_40GB:
        LinkSpeed = LINK_SPEED_40000;
        break;
    default:
        LinkSpeed = LINK_SPEED_UNKNOWN;
    }

    return LinkSpeed;
}
#endif // !kdnet


enum i40e_chip_type
    I40eGetFortvilleChipType(
        IN  I40E_DRIVER_DATA   *AdapterInfo
    )
{
    switch (AdapterInfo->hw.device_id) {
        //
        // X710 10 Gig devices
        //
    case I40E_DEV_ID_SFP_XL710:
    case I40E_DEV_ID_KX_C:
    case I40E_DEV_ID_10G_BASE_T:
    case I40E_DEV_ID_10G_BASE_T4:
        return I40E_CHIP_X710;
        break;
        //
        // XL710 40 Gig devices
        //
    case I40E_DEV_ID_KX_B:
    case I40E_DEV_ID_QSFP_A:
    case I40E_DEV_ID_QSFP_B:
    case I40E_DEV_ID_QSFP_C:
        return I40E_CHIP_XL710;
        break;
    default:
        return I40E_CHIP_UNKNOWN;
    }
}


BOOLEAN
GetEEECapability(
    IN  I40E_DRIVER_DATA   *AdapterInfo
)
{
    enum i40e_status_code                 I40eStatus;
    struct i40e_aq_get_phy_abilities_resp PhyAbilites;

    //
    // Use AQ command to retrieve phy capabilities
    //
    I40eStatus = i40e_aq_get_phy_capabilities(
        &AdapterInfo->hw,
        FALSE,
        FALSE,
        &PhyAbilites,
        NULL
    );

    if (I40eStatus != I40E_SUCCESS) {
        return FALSE;
    }

    if ((PhyAbilites.eee_capability &
        (I40E_AQ_EEE_100BASE_TX | I40E_AQ_EEE_1000BASE_T |
            I40E_AQ_EEE_10GBASE_T | I40E_AQ_EEE_1000BASE_KX |
            I40E_AQ_EEE_10GBASE_KX4 | I40E_AQ_EEE_10GBASE_KR)) != 0) {
        return TRUE;
    }
    return FALSE;
}


BOOLEAN
IsQualifiedSfpModule(
    IN  I40E_DRIVER_DATA   *AdapterInfo
)
/*++

Routine Description:
  Check if current SFP module used for this port is qualified module

Arguments:
  AdapterInfo - Pointer to the NIC data structure information
                which the UNDI driver is layering on..
Returns:
  FALSE if module is not qualified and module qualification is enabled on the port
  otherwise TRUE
--*/
{
    enum i40e_status_code I40eStatus;
    UINTN                 i = 0;

    // Workaround: Admin queue command Get Link Status sometimes fails
    // We need to wait for at least 40 ms before calling Get Link Status again.
    do {

        I40eStatus = i40e_aq_get_link_info(&AdapterInfo->hw, TRUE, NULL, NULL);

        if (I40eStatus == I40E_AQ_RC_EIO) {
#ifndef INTEL_KDNET
            gBS->Stall(50 * 1000);
#else
            KeStallExecutionProcessor(50 * 1000);
#endif
            i++;
        }
        else {
            break;
        }
    } while (i < 10);

    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_get_link_info returned %d\n", I40eStatus));
        return FALSE;
    }

    if ((AdapterInfo->hw.phy.link_info.link_info & I40E_AQ_LINK_UP)) {
        DEBUGPRINT(INIT, ("Link is up \n"));
        //
        // Link is up. Module is qualified
        //
        return TRUE;
    }
    else {
        //
        // Need to check for qualified module here
        //
        if ((AdapterInfo->hw.phy.link_info.link_info & I40E_AQ_MEDIA_AVAILABLE) &&
            (!(AdapterInfo->hw.phy.link_info.an_info & I40E_AQ_QUALIFIED_MODULE)))
            //
            // Unqualified module was detected
            //
            return FALSE;
    }

    //
    // Link is down
    // Module is qualified or qualification process is not available
    //
    return TRUE;
}


VOID
BlinkLeds(
    IN  I40E_DRIVER_DATA *AdapterInfo,
    IN UINT32            Time
)
{
    u32 led_status;

    if (AdapterInfo->hw.device_id == I40E_DEV_ID_10G_BASE_T
        || AdapterInfo->hw.device_id == I40E_DEV_ID_10G_BASE_T4
        ) {
        i40e_blink_phy_link_led(&AdapterInfo->hw, Time, 500);
    }
    else {
        //
        // Get current LED state
        //
        led_status = i40e_led_get(&AdapterInfo->hw);

        //
        // Turn on blinking LEDs and wait required ammount of time
        //
        i40e_led_set(&AdapterInfo->hw, 0xF, TRUE);
        DelayInMicroseconds(AdapterInfo, Time * 1000 * 1000);

        //
        // Restore LED state
        //
        i40e_led_set(&AdapterInfo->hw, led_status, FALSE);
    }
}


EFI_STATUS
ReadMfpConfiguration(
    IN  I40E_DRIVER_DATA   *AdapterInfo
)
/*++

Routine Description:

This functions initialize table containing PF numbers for partitions
related to this port.

Arguments:
  AdapterInfo                      - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  Partition                        - Partition number (1 based)
Returns:
  PCI function number

--*/
{
#ifndef INTEL_KDNET
    UINT32    i;
#else
    UINT8    i;
#endif
    UINT32   PortNumber;
    UINT8    PartitionCount = 0;
    UINT32   PortNum;
    UINT32   FunctionStatus;
    UINT16   PortNumValues[16];
    EFI_STATUS Status;

    Status = EepromGetMaxPfPerPortNumber(
        AdapterInfo,
        &AdapterInfo->PfPerPortMaxNumber
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("EepromGetMaxPfPerPortNumber returned %r\n", Status));
        return Status;
    }

    //
    // Read port number for current PF and save its value
    //
    PortNumber = i40eRead32(
        AdapterInfo,
        I40E_PFGEN_PORTNUM
    );
    PortNumber &= I40E_PFGEN_PORTNUM_PORT_NUM_MASK;

    AdapterInfo->PhysicalPortNumber = PortNumber;

    DEBUGPRINT(INIT, ("PhysicalPortNumber: %d\n", PortNumber));

    EepromReadPortnumValues(AdapterInfo, &PortNumValues[0], 16);

    //
    // Run through all pfs and collect information on pfs (partitions) associated 
    // to the same port as our base partition
    //
    for (i = 0; i < I40E_MAX_PF_NUMBER; i++) {
        PortNum = PortNumValues[i];

        PortNum &= I40E_PFGEN_PORTNUM_PORT_NUM_MASK;

        if (AdapterInfo->hw.func_caps.valid_functions & (1 << i)) {
            FunctionStatus = 1;
        }
        else {
            FunctionStatus = 0;
        }

        if (PortNum == PortNumber) {
            //
            // Partition is connected to the same port as the base partition
            // Save information on the status of this partition
            // 
            if (FunctionStatus != 0) {
                AdapterInfo->PartitionEnabled[PartitionCount] = TRUE;
            }
            else {
                AdapterInfo->PartitionEnabled[PartitionCount] = FALSE;
            }
            //
            // Save PCI function number
            //
            AdapterInfo->PartitionPfNumber[PartitionCount] = i;
            DEBUGPRINT(INIT, ("Partition %d, PF:%d, enabled:%d\n",
                PartitionCount,
                AdapterInfo->PartitionPfNumber[PartitionCount],
                AdapterInfo->PartitionEnabled[PartitionCount]));

            PartitionCount++;
        }
    }
    return EFI_SUCCESS;
}


/** Read VSI parameters

  @param[in]    AdapterInfo         Pointer to the NIC data structure information
                                    which the UNDI driver is layerin

  @param[out]   vsi_ctx             resulting VSI context

  @retval       EFI_SUCCESS         VSI context successfully read
  @retval       EFI_DEVICE_ERROR    VSI context read error

**/
EFI_STATUS
I40eGetVsiParams(
    IN  I40E_DRIVER_DATA        *AdapterInfo,
    OUT struct i40e_vsi_context *vsi_ctx
)
{
    EFI_STATUS I40eStatus;

    ZeroMem(vsi_ctx, sizeof(struct i40e_vsi_context));

    vsi_ctx->seid = AdapterInfo->main_vsi_seid;
    vsi_ctx->pf_num = AdapterInfo->hw.pf_id;
    vsi_ctx->uplink_seid = AdapterInfo->mac_seid;
    vsi_ctx->vf_num = 0;

    I40eStatus = i40e_aq_get_vsi_params(&AdapterInfo->hw, vsi_ctx, NULL);
    if (I40eStatus != I40E_SUCCESS) {
        DEBUGPRINT(CRITICAL, ("i40e_aq_get_vsi_params returned %d, aq_err %d\n",
            I40eStatus, AdapterInfo->hw.aq.asq_last_status));
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}