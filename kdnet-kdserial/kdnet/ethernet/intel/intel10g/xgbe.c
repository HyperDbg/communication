/**************************************************************************

Copyright (c) 2001-2016, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/


#include "xgbe.h"
#ifndef INTEL_KDNET
#include "DeviceSupport.h"
#endif

extern XGBE_DRIVER_DATA *XgbeData;

//
// Global variables for blocking IO
//
STATIC BOOLEAN          gInitializeLock = TRUE;
#ifndef INTEL_KDNET
STATIC EFI_LOCK         gLock;
#endif

#ifdef INTEL_KDNET
EFI_STATUS
GetEfiError(
  IN UINT32 Val
  )
{
  return EFIERR(Val);
}
#endif

//
// Local Functions
//

#ifndef INTEL_KDNET
VOID
_DisplayBuffersAndDescriptors (
  IN XGBE_DRIVER_DATA *XgbeAdapter
  );
#endif

UINTN
XgbeShutdown (
  IN XGBE_DRIVER_DATA *XgbeAdapter
  );

UINTN
XgbeStatistics (
  IN XGBE_DRIVER_DATA *XgbeAdapter,
  IN UINT64           DBaddr,
  IN UINT16           DBsize
  );

VOID
XgbeTxRxConfigure (
  IN XGBE_DRIVER_DATA *XgbeAdapter
  );


BOOLEAN
IsAltMacAddrSupported(
    IN UNDI_PRIVATE_DATA *UndiPrivateData
)
{
    UINT16  BackupMacPointer;

    //
    // Check to see if the backup MAC address location pointer is set
    //
    ixgbe_read_eeprom(&UndiPrivateData->NicInfo.hw, IXGBE_ALT_MAC_ADDR_PTR, &BackupMacPointer);

    if (BackupMacPointer == 0xFFFF || BackupMacPointer == 0x0000) {
        //
        //  Alternate Mac Address not supported if 0x37 pointer is not initialized to a value
        //  other than 0x0000 or 0xffff
        //
        return FALSE;
    } else {
        return TRUE;
    }

}






u8 *
_XgbeIterateMcastMacAddr (
  IN struct ixgbe_hw *hw,
  IN u8              **mc_addr_ptr,
  IN u32             *vmdq
  )
/*++

Routine Description:

  Itterate over list of multicast MAC addresses, and gets the current
  MAC address from the first address in the list
Arguments:

  hw          - Shared code hardware structure
  mc_addr_ptr - Pointer to table of multicast addresses
  vmdq        - VMDQ pointer

Returns:
  Pointer to current MAC address

--*/
{
  u8  *CurrentMac;

  CurrentMac = *mc_addr_ptr;
  *mc_addr_ptr += PXE_MAC_LENGTH;

  DEBUGPRINT(XGBE, ("Current MC MAC Addr = %02x %02x %02x %02x %02x %02x",
    CurrentMac[0], CurrentMac[1], CurrentMac[2], CurrentMac[3], CurrentMac[4], CurrentMac[5]));
  DEBUGWAIT (XGBE);

  return CurrentMac;
}

VOID
XgbeBlockIt (
  IN XGBE_DRIVER_DATA *XgbeAdapter,
  IN UINT32           flag
  )
/*++

Routine Description:

Arguments:
  XgbeAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  flag                             - Block flag
Returns:

--*/
{
  if (XgbeAdapter->Block != NULL) {
    (*XgbeAdapter->Block) (XgbeAdapter->Unique_ID, flag);
  } else {
#ifndef INTEL_KDNET
    if (gInitializeLock) {
      EfiInitializeLock (&gLock, TPL_NOTIFY);
      gInitializeLock = FALSE;
    }

    if (flag != 0) {
      EfiAcquireLock (&gLock);
    } else {
      EfiReleaseLock (&gLock);
    }
#endif
  }
}

#ifdef INTEL_KDNET

VOID
ixgbe_MapMem (
  IN XGBE_DRIVER_DATA  *XgbeAdapter,
  IN UINT64           VirtualAddress,
  IN UINT32           Size,
  OUT UINT64          *MappedAddress
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
  if (XgbeAdapter->Map_Mem != NULL) {
    *MappedAddress = 0;
    (*XgbeAdapter->Map_Mem) (
      XgbeAdapter->Unique_ID,
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
  } else {
      *MappedAddress = VirtualAddress;
  }
}

VOID
ixgbe_UnMapMem (
  IN XGBE_DRIVER_DATA  *XgbeAdapter,
  IN UINT64           VirtualAddress,
  IN UINT32           Size,
  IN UINT64           MappedAddress
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
  if (XgbeAdapter->UnMap_Mem != NULL) {
    (*XgbeAdapter->UnMap_Mem) (
      XgbeAdapter->Unique_ID,
      VirtualAddress,
      Size,
      TO_DEVICE,
      (UINT64) MappedAddress
      );
  }
}

#endif

VOID
XgbeMemCopy (
  IN UINT8  *Dest,
  IN UINT8  *Source,
  IN UINT32 Count
  )
/*++
Routine Description:
   This is the drivers copy function so it does not need to rely on the BootServices
   copy which goes away at runtime. This copy function allows 64-bit or 32-bit copies
   depending on platform architecture.  On Itanium we must check that both addresses
   are naturally aligned before attempting a 64-bit copy.

Arguments:
  Dest - Destination memory pointer to copy data to.
  Source - Source memory pointer.
  Count - Number of bytes to copy.

Returns:

  VOID
--*/
{
  UINT32  BytesToCopy;
  UINT32  IntsToCopy;
  UINTN   *SourcePtr;
  UINTN   *DestPtr;
  UINT8   *SourceBytePtr;
  UINT8   *DestBytePtr;

  IntsToCopy  = Count / sizeof (UINTN);
  BytesToCopy = Count % sizeof (UINTN);
#ifdef EFI64
  //
  // Itanium cannot handle memory accesses that are not naturally aligned.  Determine
  // if 64-bit copy is even possible with these start addresses.
  //
  if (((((UINTN) Source) & 0x0007) != 0) || ((((UINTN) Dest) & 0x0007) != 0)) {
    IntsToCopy  = 0;
    BytesToCopy = Count;
  }
#endif

  SourcePtr = (UINTN *) Source;
  DestPtr   = (UINTN *) Dest;

  while (IntsToCopy > 0) {
    *DestPtr = *SourcePtr;
    SourcePtr++;
    DestPtr++;
    IntsToCopy--;
  }

  //
  // Copy the leftover bytes.
  //
  SourceBytePtr = (UINT8 *) SourcePtr;
  DestBytePtr   = (UINT8 *) DestPtr;
  while (BytesToCopy > 0) {
    *DestBytePtr = *SourceBytePtr;
    SourceBytePtr++;
    DestBytePtr++;
    BytesToCopy--;
  }
}

UINTN
XgbeStatistics (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT64           DBaddr,
  UINT16           DBsize
  )
/*++

Routine Description:
  copies the stats from our local storage to the protocol storage.
  which means it will read our read and clear numbers, so some adding is required before
  we copy it over to the protocol.

Arguments:
  XgbeAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  DBaddr                           - The data block address
  DBsize                           - The data block size

Returns:
  PXE Status code

--*/
{
  PXE_DB_STATISTICS     *DbPtr;
  struct ixgbe_hw       *hw;
  struct ixgbe_hw_stats *st;
  UINTN                 stat;

  hw  = &XgbeAdapter->hw;
  st  = &XgbeAdapter->stats;

#define UPDATE_OR_RESET_STAT(sw_reg, hw_reg) \
   st->sw_reg = (DBaddr ? (st->sw_reg + (IXGBE_READ_REG (hw, hw_reg))) : 0)

  UPDATE_OR_RESET_STAT (tpr, IXGBE_TPR);
  UPDATE_OR_RESET_STAT (gprc, IXGBE_GPRC);
  UPDATE_OR_RESET_STAT (ruc, IXGBE_RUC);
  UPDATE_OR_RESET_STAT (ruc, IXGBE_ROC);
  UPDATE_OR_RESET_STAT (rnbc[0], IXGBE_RNBC (0));
  UPDATE_OR_RESET_STAT (bprc, IXGBE_BPRC);
  UPDATE_OR_RESET_STAT (mprc, IXGBE_MPRC);
  UPDATE_OR_RESET_STAT (tpt, IXGBE_TPT);
  UPDATE_OR_RESET_STAT (gptc, IXGBE_GPTC);
  UPDATE_OR_RESET_STAT (bptc, IXGBE_BPTC);
  UPDATE_OR_RESET_STAT (mptc, IXGBE_MPTC);
  UPDATE_OR_RESET_STAT (crcerrs, IXGBE_CRCERRS);

  if (!DBaddr) {
    return PXE_STATCODE_SUCCESS;
  }

  DbPtr = (PXE_DB_STATISTICS *) (UINTN) DBaddr;

  //
  // Fill out the OS statistics structure
  // To Add/Subtract stats, include/delete the lines in pairs.
  // E.g., adding a new stat would entail adding these two lines:
  // stat = PXE_STATISTICS_NEW_STAT_XXX;         SET_SUPPORT;
  //     DbPtr->Data[stat] = st->xxx;
  //
  DbPtr->Supported = 0;

#ifdef INTEL_KDNET
#define SET_SUPPORT(S) \
  do { \
    stat = PXE_STATISTICS_##S; \
    DbPtr->Supported |= ((UINT64)1 << stat); \
  } while (0)
#else
#define SET_SUPPORT(S) \
  do { \
    stat = PXE_STATISTICS_##S; \
    DbPtr->Supported |= (UINT64) (1 << stat); \
  } while (0)
#endif
#define UPDATE_EFI_STAT(S, b) \
  do { \
    SET_SUPPORT (S); \
    DbPtr->Data[stat] = st->b; \
  } while (0)

  UPDATE_EFI_STAT (RX_TOTAL_FRAMES, tpr);
  UPDATE_EFI_STAT (RX_GOOD_FRAMES, gprc);
  UPDATE_EFI_STAT (RX_UNDERSIZE_FRAMES, ruc);
  UPDATE_EFI_STAT (RX_OVERSIZE_FRAMES, roc);
  UPDATE_EFI_STAT (RX_DROPPED_FRAMES, rnbc[0]);
  SET_SUPPORT (RX_UNICAST_FRAMES);
  DbPtr->Data[stat] = (st->gprc - st->bprc - st->mprc);
  UPDATE_EFI_STAT (RX_BROADCAST_FRAMES, bprc);
  UPDATE_EFI_STAT (RX_MULTICAST_FRAMES, mprc);
  UPDATE_EFI_STAT (RX_CRC_ERROR_FRAMES, crcerrs);
  UPDATE_EFI_STAT (TX_TOTAL_FRAMES, tpt);
  UPDATE_EFI_STAT (TX_GOOD_FRAMES, gptc);
  SET_SUPPORT (TX_UNICAST_FRAMES);
  DbPtr->Data[stat] = (st->gptc - st->bptc - st->mptc);
  UPDATE_EFI_STAT (TX_BROADCAST_FRAMES, bptc);
  UPDATE_EFI_STAT (TX_MULTICAST_FRAMES, mptc);

  return PXE_STATCODE_SUCCESS;
};

UINTN
XgbeTransmit (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT64           cpb,
  UINT16           opflags
  )
/*++

Routine Description:
  Takes a command block pointer (cpb) and sends the frame.  Takes either one fragment or many
  and places them onto the wire.  Cleanup of the send happens in the function UNDI_Status in DECODE.C

Arguments:
  XgbeAdapter  - Pointer to the instance data
  cpb             - The command parameter block address.  64 bits since this is Itanium(tm)
                    processor friendly
  opflags         - The operation flags, tells if there is any special sauce on this transmit

Returns:
  PXE_STATCODE_SUCCESS if the frame goes out,
  PXE_STATCODE_DEVICE_FAILURE if it didn't
  PXE_STATCODE_BUSY if they need to call again later.

--*/
{
  PXE_CPB_TRANSMIT_FRAGMENTS  *TxFrags;
  PXE_CPB_TRANSMIT            *TxBuffer;
  struct ixgbe_legacy_tx_desc *TransmitDescriptor;
  UINT32                      i;
  INT16                       WaitMsec;

  //
  // Transmit buffers must be freed by the upper layer before we can transmit any more.
  //
  if (XgbeAdapter->TxBufferUsed[XgbeAdapter->cur_tx_ind]) {
    DEBUGPRINT (CRITICAL, ("TX buffers have all been used!\n"));
    return PXE_STATCODE_QUEUE_FULL;
  }

  //
  // Make some short cut pointers so we don't have to worry about typecasting later.
  // If the TX has fragments we will use the
  // tx_tpr_f pointer, otherwise the tx_ptr_l (l is for linear)
  //
  TxBuffer  = (PXE_CPB_TRANSMIT *) (UINTN) cpb;
  TxFrags   = (PXE_CPB_TRANSMIT_FRAGMENTS *) (UINTN) cpb;

  //
  // quicker pointer to the next available Tx descriptor to use.
  //
  TransmitDescriptor = &XgbeAdapter->tx_ring[XgbeAdapter->cur_tx_ind];

  //
  // Opflags will tell us if this Tx has fragments
  // So far the linear case (the no fragments case, the else on this if) is the majority
  // of all frames sent.
  //
  if (opflags & PXE_OPFLAGS_TRANSMIT_FRAGMENTED) {
    //
    // this count cannot be more than 8;
    //
    DEBUGPRINT (TX, ("Fragments %x\n", TxFrags->FragCnt));

    //
    // for each fragment, give it a descriptor, being sure to keep track of the number used.
    //
    for (i = 0; i < TxFrags->FragCnt; i++) {
      //
      // Put the size of the fragment in the descriptor
      //
#ifndef INTEL_KDNET
      TransmitDescriptor->buffer_addr                     = TxFrags->FragDesc[i].FragAddr;
#else
      XgbeAdapter->TxBufferUnmappedAddr[XgbeAdapter->cur_tx_ind] = TxFrags->FragDesc[i].FragAddr;
      ixgbe_MapMem(
        XgbeAdapter,
        XgbeAdapter->TxBufferUnmappedAddr[XgbeAdapter->cur_tx_ind],
        TxFrags->FragDesc[i].FragLen,
        &TransmitDescriptor->buffer_addr
        );
#endif

      TransmitDescriptor->lower.flags.length              = (UINT16) TxFrags->FragDesc[i].FragLen;
      TransmitDescriptor->lower.data                      = (IXGBE_TXD_CMD_IFCS | IXGBE_TXD_CMD_RS);
      if (XgbeAdapter->VlanEnable) {
        DEBUGPRINT (TX, ("1: Setting VLAN tag = %d\n", XgbeAdapter->VlanTag));
        TransmitDescriptor->upper.fields.vlan = XgbeAdapter->VlanTag;
        TransmitDescriptor->lower.data |= IXGBE_TXD_CMD_VLE;
      }

      XgbeAdapter->TxBufferUsed[XgbeAdapter->cur_tx_ind]  = TxFrags->FragDesc[i].FragAddr;

      //
      // If this is the last fragment we must also set the EOP bit
      //
      if ((i + 1) == TxFrags->FragCnt) {
        TransmitDescriptor->lower.data |= IXGBE_TXD_CMD_EOP;
      }

      //
      // move our software counter passed the frame we just used, watching for wrapping
      //
      DEBUGPRINT (TX, ("Advancing TX pointer %x\n", XgbeAdapter->cur_tx_ind));
      XgbeAdapter->cur_tx_ind++;
      if (XgbeAdapter->cur_tx_ind >= /* kdnet change from == */ DEFAULT_TX_DESCRIPTORS) {
        XgbeAdapter->cur_tx_ind = 0;
      }

      TransmitDescriptor = &XgbeAdapter->tx_ring[XgbeAdapter->cur_tx_ind];
    }
  } else {
#ifndef INTEL_KDNET
    TransmitDescriptor->buffer_addr = TxBuffer->FrameAddr;
#else
    XgbeAdapter->TxBufferUnmappedAddr[XgbeAdapter->cur_tx_ind] = TxBuffer->FrameAddr;
    ixgbe_MapMem(
      XgbeAdapter,
      XgbeAdapter->TxBufferUnmappedAddr[XgbeAdapter->cur_tx_ind],
      TxBuffer->DataLen + TxBuffer->MediaheaderLen,
      &TransmitDescriptor->buffer_addr
      );
#endif
    DEBUGPRINT (TX, ("Packet buffer at %x\n", TransmitDescriptor->buffer_addr));

    //
    // Set the proper bits to tell the chip that this is the last descriptor in the send,
    // and be sure to tell us when its done.
    // EOP - End of packet
    // IFCs - Insert FCS (Ethernet CRC)
    // RS - Report Status
    //
    TransmitDescriptor->lower.data          = (IXGBE_TXD_CMD_EOP | IXGBE_TXD_CMD_IFCS | IXGBE_TXD_CMD_RS);
    TransmitDescriptor->upper.fields.status = 0;
    TransmitDescriptor->lower.flags.length  = (UINT16) ((UINT16) TxBuffer->DataLen + TxBuffer->MediaheaderLen);
    if (XgbeAdapter->VlanEnable) {
      DEBUGPRINT (TX, ("1: Setting VLAN tag = %d\n", XgbeAdapter->VlanTag));
      TransmitDescriptor->upper.fields.vlan = XgbeAdapter->VlanTag;
      TransmitDescriptor->lower.data |= IXGBE_TXD_CMD_VLE;
    }

    DEBUGPRINT (TX, ("BuffAddr=%x, ", TransmitDescriptor->buffer_addr));
    DEBUGPRINT (TX, ("Cmd=%x,", TransmitDescriptor->lower.flags.cmd));
    DEBUGPRINT (TX, ("Cso=%x,", TransmitDescriptor->lower.flags.cso));
    DEBUGPRINT (TX, ("Len=%x,", TransmitDescriptor->lower.flags.length));
    DEBUGPRINT (TX, ("Status=%x,", TransmitDescriptor->upper.fields.status));
    DEBUGPRINT (TX, ("Css=%x\n", TransmitDescriptor->upper.fields.css));
    DEBUGPRINT (TX, ("vlan=%x,", TransmitDescriptor->upper.fields.vlan));

    XgbeAdapter->TxBufferUsed[XgbeAdapter->cur_tx_ind] = TxBuffer->FrameAddr;

    //
    // Move our software counter passed the frame we just used, watching for wrapping
    //
    XgbeAdapter->cur_tx_ind++;
    if (XgbeAdapter->cur_tx_ind >= /* kdnet change from == */ DEFAULT_TX_DESCRIPTORS) {
      XgbeAdapter->cur_tx_ind = 0;
    }

    DEBUGPRINT (TX, ("Packet length = %d\n", TransmitDescriptor->lower.flags.length));
    DEBUGPRINT (TX, ("Packet data:\n"));
    for (i = 0; i < 32; i++) {
      DEBUGPRINT (TX, ("%x ", ((UINT16 *) ((UINTN) TransmitDescriptor->buffer_addr))[i]));
    }
  }

  //
  // Turn on the blocking function so we don't get swapped out
  // Then move the Tail pointer so the HW knows to start processing the TX we just setup.
  //
  XgbeBlockIt (XgbeAdapter, TRUE);
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDT (0), XgbeAdapter->cur_tx_ind);
  XgbeBlockIt (XgbeAdapter, FALSE);

  //
  // If the opflags tells us to wait for the packet to hit the wire, we will wait.
  //
  if ((opflags & PXE_OPFLAGS_TRANSMIT_BLOCK) != 0) {
    WaitMsec = 1000;

    while ((TransmitDescriptor->upper.fields.status & IXGBE_TXD_STAT_DD) == 0) {
      DelayInMilliseconds (10);
      WaitMsec -= 10;
      if (WaitMsec <= 0) {
        break;
      }
    }

    //
    // If we waited for a while, and it didn't finish then the HW must be bad.
    //
    if ((TransmitDescriptor->upper.fields.status & IXGBE_TXD_STAT_DD) == 0) {
      DEBUGPRINT (CRITICAL, ("Device failure\n"));
      return PXE_STATCODE_DEVICE_FAILURE;
    } else {
      DEBUGPRINT (TX, ("Transmit success\n"));
    }
  }

  return PXE_STATCODE_SUCCESS;
};

UINTN
XgbeReceive (
  XGBE_DRIVER_DATA *XgbeAdapter,
  PXE_CPB_RECEIVE  *CpbReceive,
  PXE_DB_RECEIVE   *DbReceive
  )
/*++

Routine Description:
  Copies the frame from our internal storage ring (As pointed to by XgbeAdapter->rx_ring) to the command block
  passed in as part of the cpb parameter.  The flow:  Ack the interrupt, setup the pointers, find where the last
  block copied is, check to make sure we have actually received something, and if we have then we do a lot of work.
  The packet is checked for errors, size is adjusted to remove the CRC, adjust the amount to copy if the buffer is smaller
  than the packet, copy the packet to the EFI buffer, and then figure out if the packet was targetted at us, broadcast, multicast
  or if we are all promiscuous.  We then put some of the more interesting information (protocol, src and dest from the packet) into the
  db that is passed to us.  Finally we clean up the frame, set the return value to _SUCCESS, and inc the cur_rx_ind, watching
  for wrapping.  Then with all the loose ends nicely wrapped up, fade to black and return.

Arguments:
  XgbeAdapter - pointer to the driver data
  CpbReceive  - Pointer (Ia-64 friendly) to the command parameter block.  The frame will be placed inside of it.
  DbReceive   - The data buffer.  The out of band method of passing pre-digested information to the protocol.

Returns:
  PXE_STATCODE, either _NO_DATA if there is no data, or _SUCCESS if we passed the goods to the protocol.

--*/
{
  PXE_FRAME_TYPE              PacketType;
  struct ixgbe_legacy_rx_desc *ReceiveDescriptor;
  ETHER_HEADER                *EtherHeader;
  PXE_STATCODE                StatCode;
  UINT16                      i;
  UINT16                      TempLen;
  UINT8                       *PacketPtr;
#ifdef INTEL_KDNET
  UINT8                       *ReceiveBuffer;
#endif

  PacketType  = PXE_FRAME_TYPE_NONE;
  StatCode    = PXE_STATCODE_NO_DATA;
  i           = 0;

  DEBUGPRINT (RX, ("RCTL=%X\n", IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RXDCTL (0))));

  //
  // acknowledge the interrupts
  //
  DEBUGPRINT (RX, ("XgbeReceive  "));
  IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_EICR);
  DEBUGPRINT (RX, ("RDH0 = %x  ", (UINT16) IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RDH (0))));
  DEBUGPRINT (RX, ("RDT0 = %x  ", (UINT16) IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RDT (0))));

  //
  // Get a pointer to the buffer that should have a rx in it, IF one is really there.
  //
  ReceiveDescriptor = &XgbeAdapter->rx_ring[XgbeAdapter->cur_rx_ind];
#ifdef INTEL_KDNET
  ReceiveBuffer = &XgbeAdapter->local_rx_buffer[XgbeAdapter->cur_rx_ind].RxBuffer[0];
#endif

  if ((ReceiveDescriptor->status & (IXGBE_RXD_STAT_EOP | IXGBE_RXD_STAT_DD)) != 0) {
    DEBUGPRINT (RX, ("XgbeReceive Packet Data at address %0x \n", CpbReceive->BufferAddr));
    //
    // Just to make sure we don't try to copy a zero length, only copy a positive sized packet.
    //
    if ((ReceiveDescriptor->length != 0) && (ReceiveDescriptor->errors == 0)) {
      //
      // If the buffer passed us is smaller than the packet, only copy the size of the buffer.
      //
      TempLen = ReceiveDescriptor->length;
      if (ReceiveDescriptor->length > (INT16) CpbReceive->BufferLen) {
        TempLen = (UINT16) CpbReceive->BufferLen;
      }

      //
      // Copy the packet from our list to the EFI buffer.
      //
#ifndef INTEL_KDNET
      XgbeMemCopy ((INT8 *) (UINTN) CpbReceive->BufferAddr, (INT8 *) (UINTN) ReceiveDescriptor->buffer_addr, TempLen);
#else
      XgbeMemCopy ((INT8 *) (UINTN) CpbReceive->BufferAddr, ReceiveBuffer, TempLen);
#endif

      PacketPtr = (UINT8 *) (UINTN) CpbReceive->BufferAddr;
      DEBUGPRINT (RX, ("XgbeReceive Packet Data at address %0x \n", CpbReceive->BufferAddr));
      for (i = 0; i < 40; i++) {
        DEBUGPRINT (RX, ("%x ", PacketPtr[i]));
      }

      DEBUGPRINT (RX, ("\n"));
      DEBUGWAIT (RX);

      //
      // Fill the DB with needed information
      //
      DbReceive->FrameLen       = ReceiveDescriptor->length;  // includes header
      DbReceive->MediaHeaderLen = PXE_MAC_HEADER_LEN_ETHER;

#ifndef INTEL_KDNET
      EtherHeader               = (ETHER_HEADER *) (UINTN) ReceiveDescriptor->buffer_addr;
#else
      EtherHeader = (ETHER_HEADER *)ReceiveBuffer;
#endif

      //
      // Figure out if the packet was meant for us, was a broadcast, multicast or we
      // recieved a frame in promiscuous mode.
      //
      for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
        if (EtherHeader->dest_addr[i] != XgbeAdapter->hw.mac.perm_addr[i]) {
          DEBUGPRINT (RX, ("Packet is not specifically for us\n"));
          break;
        }
      }

      //
      // if we went the whole length of the header without breaking out then the packet is
      // directed at us.
      //
      if (i >= PXE_HWADDR_LEN_ETHER) {
        DEBUGPRINT (RX, ("Packet is for us\n"));
        PacketType = PXE_FRAME_TYPE_UNICAST;
      } else {
        //
        // Compare it against our broadcast node address
        //
        for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
          if (EtherHeader->dest_addr[i] != XgbeAdapter->BroadcastNodeAddress[i]) {
            DEBUGPRINT (RX, ("Packet is not our broadcast\n"));
            break;
          }
        }

        //
        // If we went the whole length of the header without breaking out then the packet is directed at us via broadcast
        //
        if (i >= PXE_HWADDR_LEN_ETHER) {
          PacketType = PXE_FRAME_TYPE_BROADCAST;
        } else {
          //
          // That leaves multicast or we must be in promiscuous mode.   Check for the Mcast bit in the address.
          // otherwise its a promiscuous receive.
          //
          if ((EtherHeader->dest_addr[0] & 1) == 1) {
            PacketType = PXE_FRAME_TYPE_MULTICAST;
          } else {
            PacketType = PXE_FRAME_TYPE_PROMISCUOUS;
          }
        }
      }

      DbReceive->Type = PacketType;
      DEBUGPRINT (RX, ("PacketType %x\n", PacketType));

      //
      // Put the protocol (UDP, TCP/IP) in the data buffer.
      //
      DbReceive->Protocol = EtherHeader->type;
      DEBUGPRINT (RX, ("protocol %x\n", EtherHeader->type));

      DEBUGPRINT (RX, ("Dest Address: "));
      for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
        DEBUGPRINT (RX, ("%x", (UINT32) EtherHeader->dest_addr[i]));
        DbReceive->SrcAddr[i]   = EtherHeader->src_addr[i];
        DbReceive->DestAddr[i]  = EtherHeader->dest_addr[i];
      }

      DEBUGPRINT (RX, ("\n"));
      StatCode = PXE_STATCODE_SUCCESS;
    } else {
      DEBUGPRINT(RX, ("ERROR: ReceiveDescriptor->length=%x, ReceiveDescriptor->errors=%x \n",
        ReceiveDescriptor->length, ReceiveDescriptor->errors));
      /* Go through all the error bits - these are only valid when EOP and DD are set */
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_CE)   DEBUGPRINT(CRITICAL, ("CE Error\n"));
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_LE)   DEBUGPRINT(CRITICAL, ("LE Error\n"));
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_PE)   DEBUGPRINT(CRITICAL, ("PE Error\n"));
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_OSE)  DEBUGPRINT(CRITICAL, ("OSE Error\n"));
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_USE)  DEBUGPRINT(CRITICAL, ("USE Error\n"));
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_TCPE) DEBUGPRINT(CRITICAL, ("TCP Error\n"));
      if (ReceiveDescriptor->errors & IXGBE_RXDADV_ERR_IPE)  DEBUGPRINT(CRITICAL, ("IP Error\n"));
    }
    //
    // Clean up the packet
    //
    ReceiveDescriptor->status = 0;
    ReceiveDescriptor->length = 0;
    ReceiveDescriptor->errors = 0;

    //
    // Move the current cleaned buffer pointer, being careful to wrap it as needed.  Then update the hardware,
    // so it knows that an additional buffer can be used.
    //
    IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDT (0), XgbeAdapter->cur_rx_ind);
    XgbeAdapter->cur_rx_ind++;
    if (XgbeAdapter->cur_rx_ind == DEFAULT_RX_DESCRIPTORS) {
      XgbeAdapter->cur_rx_ind = 0;
    }
  }

  return StatCode;
};

UINTN
XgbeSetInterruptState (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Allows the protocol to control our interrupt behaviour.

Arguments:
  XgbeAdapter  - Pointer to the driver structure

Returns:
  PXE_STATCODE_SUCCESS

--*/
{
  UINT32  SetIntMask;

  SetIntMask = 0;

  DEBUGPRINT (XGBE, ("XgbeSetInterruptState\n"));

  if (XgbeAdapter->IntMask & PXE_OPFLAGS_INTERRUPT_RECEIVE) {
    SetIntMask |= (IXGBE_EICS_RTX_QUEUE);
    DEBUGPRINT (XGBE, ("Mask the RX interrupts\n"));
  }

  //
  // Mask the TX interrupts
  //
  if (XgbeAdapter->IntMask & PXE_OPFLAGS_INTERRUPT_TRANSMIT) {
    SetIntMask |= (IXGBE_EIMS_RTX_QUEUE);
    DEBUGPRINT (XGBE, ("Mask the TX interrupts\n"));
  }

  //
  // Mask the CMD interrupts
  //
  if (XgbeAdapter->IntMask & PXE_OPFLAGS_INTERRUPT_COMMAND) {
    SetIntMask |= (IXGBE_EIMS_LSC);
    DEBUGPRINT (XGBE, ("Mask the CMD interrupts\n"));
  }

  //
  // Now we have all the Ints we want, so let the hardware know.
  //
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_EIMS, SetIntMask);

  return PXE_STATCODE_SUCCESS;
};

UINTN
XgbeShutdown (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Stop the hardware and put it all (including the PHY) into a known good state.

Arguments:
  XgbeAdapter  - Pointer to the driver structure

Returns:
  PXE_STATCODE_SUCCESS

--*/
{
  DEBUGPRINT (XGBE, ("XgbeShutdown - adapter stop\n"));

  XgbeReceiveDisable (XgbeAdapter);

  XgbeClearRegBits (XgbeAdapter, IXGBE_TXDCTL (0), IXGBE_TXDCTL_ENABLE);

  XgbeAdapter->RxFilter = 0;

  return PXE_STATCODE_SUCCESS;
};

UINTN
XgbeReset (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT16           OpFlags
  )
/*++

Routine Description:
  Resets the hardware and put it all (including the PHY) into a known good state.

Arguments:
  XgbeAdapter    - The pointer to our context data
  OpFlags           - The information on what else we need to do.

Returns:
  PXE_STATCODE_SUCCESS

--*/
{
  EFI_STATUS    Status;

  //
  // Put the XGBE into a known state by resetting the transmit
  // and receive units of the XGBE and masking/clearing all
  // interrupts.
  // If the hardware has already been started then don't bother with a reset.
  //
  if (XgbeAdapter->HwInitialized == FALSE) {
    //
    // Now that the structures are in place, we can configure the hardware to use it all.
    //
    Status = XgbeInitHw (XgbeAdapter);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("XgbeInitHw returns %r\n", Status));
      return PXE_STATCODE_NOT_STARTED;
    }
  } else {
    DEBUGPRINT (XGBE, ("Skipping adapter reset\n"));
  }

  if ((OpFlags & PXE_OPFLAGS_RESET_DISABLE_FILTERS) == 0) {
    UINT16  SaveFilter;

    SaveFilter = XgbeAdapter->RxFilter;

    //
    // if we give the filter same as RxFilter, this routine will not set mcast list
    // (it thinks there is no change)
    // to force it, we will reset that flag in the RxFilter
    //
    XgbeAdapter->RxFilter &= (~PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST);
    XgbeSetFilter (XgbeAdapter, SaveFilter);
  }

  if (OpFlags & PXE_OPFLAGS_RESET_DISABLE_INTERRUPTS) {
    XgbeAdapter->IntMask = 0; // disable the interrupts
  }

  XgbeSetInterruptState (XgbeAdapter);

  return PXE_STATCODE_SUCCESS;
}

VOID
XgbeLanFunction (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:

  PCIe function to LAN port mapping.

Arguments:

  XgbeAdapter - Pointer to adapter structure

Returns:

  VOID

--*/
{
  XgbeAdapter->LanFunction = (IXGBE_READ_REG(&XgbeAdapter->hw, IXGBE_STATUS) & IXGBE_STATUS_LAN_ID) >> IXGBE_STATUS_LAN_ID_SHIFT;
  DEBUGPRINT (INIT, ("PCI function %d is LAN port %d \n", XgbeAdapter->Function, XgbeAdapter->LanFunction));
  DEBUGWAIT (INIT);
}

EFI_STATUS
XgbePciInit (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  This function performs PCI-E initialization for the device.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  EFI_STATUS

--*/
{
  EFI_STATUS  Status;
#ifndef INTEL_KDNET
  UINT64      NewCommand;
  UINTN       Seg;
  UINT64      Result;

  NewCommand = 0;
  Result = 0;
#else
  Status = EFI_SUCCESS;
#endif

#ifndef INTEL_KDNET

  //
  // Get the PCI Command options that are supported by this controller.
  //
  Status = XgbeAdapter->PciIo->Attributes (
                        XgbeAdapter->PciIo,
                        EfiPciIoAttributeOperationSupported,
                        0,
                        &Result
                        );

  DEBUGPRINT(XGBE, ("Attributes supported %x\n", Result));

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("Attributes returns %X\n", NewCommand));
    return Status;
  }

  //
  // Set the PCI Command options to enable device memory mapped IO,
  // port IO, and bus mastering.
  //
  Status = XgbeAdapter->PciIo->Attributes (
                        XgbeAdapter->PciIo,
                        EfiPciIoAttributeOperationEnable,
                        Result & (EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE),
                        &NewCommand
                        );

  DEBUGPRINT(XGBE, ("Attributes enabled %x\n", Result));
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("Attributes returns %X\n", NewCommand));
    return Status;
  }

#endif

#ifndef INTEL_KDNET
  XgbeAdapter->PciIo->GetLocation (
                        XgbeAdapter->PciIo,
                        &Seg,
                        &XgbeAdapter->Bus,
                        &XgbeAdapter->Device,
                        &XgbeAdapter->Function
                        );
#else
  GetDeviceLocation(&XgbeAdapter->Segment,
                    &XgbeAdapter->Bus,
                    &XgbeAdapter->Device,
                    &XgbeAdapter->Function);
#endif

  //
  // Read all the registers from the device's PCI Configuration space
  //
#ifndef INTEL_KDNET
  XgbeAdapter->PciIo->Pci.Read (
                            XgbeAdapter->PciIo,
                            EfiPciIoWidthUint32,
                            0,
                            MAX_PCI_CONFIG_LEN,
                            XgbeAdapter->PciConfig
                            );
#else

  GetDevicePciDataByOffset(&XgbeAdapter->PciConfig,
                            0,
                            MAX_PCI_CONFIG_LEN * sizeof(UINT32));

#endif

#ifndef INTEL_KDNET
  //
  // Allocate memory for transmit and receive resources.
  //
  Status = XgbeAdapter->PciIo->AllocateBuffer (
                                XgbeAdapter->PciIo,
                                AllocateAnyPages,
                                EfiBootServicesData,
                                UNDI_MEM_PAGES (MEMORY_NEEDED),
                                (VOID **) &XgbeAdapter->MemoryPtr,
                                0
                                );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("PCI IO AllocateBuffer returns %X\n", Status));
    DEBUGWAIT (INIT);
    return Status;
  }
#else

  XgbeAdapter->MemoryPtr = (UINT64)(UINTN)GetDeviceMemory();

#endif

  return Status;
}


#define PCI_CLASS_MASK          0xFF00
#define PCI_SUBCLASS_MASK       0x00FF
EFI_STATUS
XgbeFirstTimeInit (
  UNDI_PRIVATE_DATA *XgbePrivate
  )
/*++

Routine Description:
  This function is called as early as possible during driver start to ensure the
  hardware has enough time to autonegotiate when the real SNP device initialize call
  is made.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  PXE_STATCODE

--*/
{
  PCI_CONFIG_HEADER *PciConfigHeader;
  EFI_STATUS        Status;
  XGBE_DRIVER_DATA  *XgbeAdapter;
  INT32             ScStatus;
  UINT32            Reg;
  UINT16            i;

  DEBUGPRINT (CRITICAL, ("XgbeFirstTimeInit\n"));

  XgbeAdapter             = &XgbePrivate->NicInfo;

  XgbeAdapter->DriverBusy = FALSE;
  XgbeAdapter->ReceiveStarted = FALSE;
  PciConfigHeader         = (PCI_CONFIG_HEADER *) &XgbeAdapter->PciConfig[0];

  ZeroMem (XgbeAdapter->BroadcastNodeAddress, PXE_MAC_LENGTH);
  SetMem (XgbeAdapter->BroadcastNodeAddress, PXE_HWADDR_LEN_ETHER, 0xFF);

  DEBUGPRINT(XGBE, ("PciConfigHeader->VendorID = %X\n", PciConfigHeader->VendorID));
  DEBUGPRINT(XGBE, ("PciConfigHeader->DeviceID = %X\n", PciConfigHeader->DeviceID));


  //
  // Initialize all parameters needed for the shared code
  //
  XgbeAdapter->hw.hw_addr                       = 0;
  XgbeAdapter->hw.back                          = XgbeAdapter;
  XgbeAdapter->hw.vendor_id                     = PciConfigHeader->VendorID;
  XgbeAdapter->hw.device_id                     = PciConfigHeader->DeviceID;
  XgbeAdapter->hw.revision_id                   = (UINT8) PciConfigHeader->RevID;
  XgbeAdapter->hw.subsystem_vendor_id           = PciConfigHeader->SubVendorID;
  XgbeAdapter->hw.subsystem_device_id           = PciConfigHeader->SubSystemID;
  XgbeAdapter->hw.revision_id                   = (UINT8) PciConfigHeader->RevID;
  XgbeAdapter->hw.adapter_stopped               = TRUE;
  XgbeAdapter->hw.fc.requested_mode             = ixgbe_fc_default;

  XgbeAdapter->PciClass    = (UINT8)((PciConfigHeader->ClassID & PCI_CLASS_MASK) >> 8);
  XgbeAdapter->PciSubClass = (UINT8)(PciConfigHeader->ClassID) & PCI_SUBCLASS_MASK;

  ScStatus = ixgbe_init_shared_code (&XgbeAdapter->hw);
  if (ScStatus != IXGBE_SUCCESS) {
    DEBUGPRINT (CRITICAL, ("Error initializing shared code! %d\n", -ScStatus));

    //
    // This is the only condition where we will fail.  We need to support SFP hotswap
    // which may produce an error if the SFP module is missing.
    //
    if (ScStatus == IXGBE_ERR_DEVICE_NOT_SUPPORTED ||
        ScStatus == IXGBE_ERR_SFP_NOT_SUPPORTED)
      return EFI_UNSUPPORTED;
  }

  XgbeLanFunction (XgbeAdapter);

  DEBUGPRINT(XGBE, ("Calling ixgbe_get_mac_addr\n"));
  if (ixgbe_get_mac_addr (&XgbeAdapter->hw, XgbeAdapter->hw.mac.perm_addr) != IXGBE_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("Could not read MAC address\n"));
    return EFI_UNSUPPORTED;
  }

  // Copy perm_addr to addr. Needed in HII. Print it if requested.
  DEBUGPRINT(INIT, ("MAC Address: "));
  for (i = 0; i < 6; i++) {
    XgbeAdapter->hw.mac.addr[i] = XgbeAdapter->hw.mac.perm_addr[i];
    DEBUGPRINT(INIT, ("%2x ", XgbeAdapter->hw.mac.perm_addr[i]));
  }
  DEBUGPRINT(INIT, ("\n"));

#ifndef INTEL_KDNET

  Reg = IXGBE_READ_REG(&XgbeAdapter->hw, IXGBE_CTRL_EXT);
  if ((Reg & IXGBE_CTRL_EXT_DRV_LOAD) != 0) {
    DEBUGPRINT (CRITICAL, ("iSCSI Boot detected on port!\n"));
    return EFI_ACCESS_DENIED;
  }

#else

//
// Do NOT check if the external driver bit is set, since this
// code sets that bit itself.  (In bootmgr, winload, and the OS, and each time
// we do NOT want to prevent the next incarnation of this code from loading
// again and taking over the hardware.)  Just take over the hardware.  Period.
//
  UNREFERENCED_1PARAMETER(Reg);

#endif

  //
  // Clear the Wake-up status register in case there has been a power management event
  //
  IXGBE_WRITE_REG(&XgbeAdapter->hw, IXGBE_WUS, 0);


  Status = XgbeInitHw (XgbeAdapter);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("XgbeInitHw returns %r\n", Status));
    return Status;
  }

  XgbeSetRegBits (XgbeAdapter, IXGBE_CTRL_EXT, IXGBE_CTRL_EXT_DRV_LOAD);

  return EFI_SUCCESS;
};

EFI_STATUS
XgbeInitHw (
  XGBE_DRIVER_DATA  *XgbeAdapter
  )
/*++

Routine Description:
  Initializes the hardware and sets up link.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  EFI_STATUS

--*/
{
  INT32             ScStatus;
  ixgbe_link_speed  Speed;

  //
  // Now that the structures are in place, we can configure the hardware to use it all.
  //

  ScStatus = ixgbe_init_hw (&XgbeAdapter->hw);
  if (ScStatus == 0) {
    DEBUGPRINT (XGBE, ("ixgbe_init_hw success\n"));
    XgbeAdapter->HwInitialized = TRUE;
  } else if (ScStatus == IXGBE_ERR_SFP_NOT_PRESENT) {
    DEBUGPRINT (CRITICAL, ("ixgbe_init_hw returns IXGBE_ERR_SFP_NOT_PRESENT\n"));
    XgbeAdapter->HwInitialized = TRUE;
  } else {
    DEBUGPRINT (CRITICAL, ("Hardware Init failed = %d\n", -ScStatus));
    XgbeAdapter->HwInitialized = FALSE;
    return EFI_DEVICE_ERROR;
  }
  DEBUGWAIT (XGBE);

  //
  // 82599+ silicons support 100Mb autonegotiation which is not supported
  // with 82598. This is why we initialize Speed parameter in different way.
  //
  if (XgbeAdapter->hw.mac.type == ixgbe_mac_82598EB) {
    Speed = IXGBE_LINK_SPEED_82598_AUTONEG;
  } else {
    Speed = IXGBE_LINK_SPEED_82599_AUTONEG;
  }

  if (ixgbe_setup_link(&XgbeAdapter->hw, Speed, FALSE) != 0) {
    DEBUGPRINT(CRITICAL, ("ixgbe_setup_link fails\n"));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

PXE_STATCODE
XgbeInitialize (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Initializes the gigabit adapter, setting up memory addresses, MAC Addresses,
  Type of card, etc.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  PXE_STATCODE

--*/
{
  UINT32        *TempBar;
  PXE_STATCODE  PxeStatcode;
  EFI_STATUS    Status;

  PxeStatcode = PXE_STATCODE_SUCCESS;
  TempBar     = NULL;

  ZeroMem ((VOID *) ((UINTN) XgbeAdapter->MemoryPtr), MEMORY_NEEDED);

  //
  // If the hardware has already been started then don't bother with a reset
  // We want to make sure we do not have to restart link negotiation.
  //
  if (XgbeAdapter->HwInitialized == FALSE) {

    //
    // Now that the structures are in place, we can configure the hardware to use it all.
    //
    Status = XgbeInitHw (XgbeAdapter);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("XgbeInitHw returns %r\n", Status));
      PxeStatcode = PXE_STATCODE_NOT_STARTED;
    }
  } else {
    DEBUGPRINT (XGBE, ("Skipping adapter reset\n"));
    PxeStatcode = PXE_STATCODE_SUCCESS;
  }

  //
  // If we reset the adapter then reinitialize the TX and RX rings
  //
  if (PxeStatcode == PXE_STATCODE_SUCCESS) {
    XgbeTxRxConfigure (XgbeAdapter);
  }

  return PxeStatcode;
};

VOID
XgbeTxRxConfigure (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Initializes the transmit and receive resources for the adapter.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  VOID

--*/
{
  UINT32  TempReg;
  UINT64  MemAddr;
  UINT32  *MemPtr;
  UINT16  i;

  DEBUGPRINT (XGBE, ("XgbeTxRxConfigure\n"));

  XgbeReceiveDisable (XgbeAdapter);

  //
  // Setup the receive ring
  //
  XgbeAdapter->rx_ring = (struct ixgbe_legacy_rx_desc *) (UINTN) ((XgbeAdapter->MemoryPtr + BYTE_ALIGN_64) & 0xFFFFFFFFFFFFFF80);

  //
  // Setup TX ring
  //
  XgbeAdapter->tx_ring = (struct ixgbe_legacy_tx_desc *) ((UINT8 *) XgbeAdapter->rx_ring + (sizeof (struct ixgbe_legacy_rx_desc) * DEFAULT_RX_DESCRIPTORS));
  DEBUGPRINT(XGBE, (
    "Rx Ring %x Tx Ring %X  RX size %X \n",
    XgbeAdapter->rx_ring,
    XgbeAdapter->tx_ring,
    (sizeof (struct ixgbe_legacy_rx_desc) * DEFAULT_RX_DESCRIPTORS)
    ));

#ifdef INTEL_KDNET
  ZeroMem ((VOID *) XgbeAdapter->TxBufferUnmappedAddr, DEFAULT_TX_DESCRIPTORS * sizeof(UINT64));
#endif

  for (i = 0; i < DEFAULT_TX_DESCRIPTORS; i++) {
    XgbeAdapter->TxBufferUsed[i] = FALSE;
  }

  //
  // Since we already have the size of the TX Ring, use it to setup the local receive buffers
  //
  XgbeAdapter->local_rx_buffer = (LOCAL_RX_BUFFER *) ((UINT8 *) XgbeAdapter->tx_ring + (sizeof (struct ixgbe_legacy_tx_desc) * DEFAULT_TX_DESCRIPTORS));
#ifdef INTEL_KDNET
  //
  // Align the receive buffers to a 2048 byte boundary so that none of them
  // cross a page boundary.  This ensures that each one will require only one
  // descriptor.
  //
  XgbeAdapter->local_rx_buffer = (LOCAL_RX_BUFFER *)(UINTN)((((UINT64)XgbeAdapter->local_rx_buffer) + BYTE_ALIGN_2048) & ~(UINT64)BYTE_ALIGN_2048);
#endif
  DEBUGPRINT(XGBE, (
    "Tx Ring %x Added %x\n",
    XgbeAdapter->tx_ring,
    ((UINT8 *) XgbeAdapter->tx_ring + (sizeof (struct ixgbe_legacy_tx_desc) * DEFAULT_TX_DESCRIPTORS))
    ));
  DEBUGPRINT(XGBE, (
    "Local Rx Buffer %X size %X\n",
    XgbeAdapter->local_rx_buffer,
    (sizeof (struct ixgbe_legacy_tx_desc) * DEFAULT_TX_DESCRIPTORS)
    ));

  //
  // now to link the RX Ring to the local buffers
  //
  for (i = 0; i < DEFAULT_RX_DESCRIPTORS; i++) {
#ifndef INTEL_KDNET
    XgbeAdapter->rx_ring[i].buffer_addr = (UINT64) ((UINTN) XgbeAdapter->local_rx_buffer[i].RxBuffer);
#else
    ixgbe_MapMem(
      XgbeAdapter,
      (UINT64)&XgbeAdapter->local_rx_buffer[i].RxBuffer[0],
      sizeof(XgbeAdapter->local_rx_buffer[i].RxBuffer),
      &XgbeAdapter->rx_ring[i].buffer_addr);
#endif
    DEBUGPRINT (XGBE, ("Rx Local Buffer %X\n", (XgbeAdapter->rx_ring[i]).buffer_addr));
  }

#ifndef INTEL_KDNET
  _DisplayBuffersAndDescriptors (XgbeAdapter);
#endif

  //
  // Setup the RDBA, RDLEN
  //
#ifndef INTEL_KDNET
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDBAL (0), (UINT32) (UINTN) (XgbeAdapter->rx_ring));
#else
  ixgbe_MapMem(
    XgbeAdapter,
    (UINT64)(UINTN)XgbeAdapter->rx_ring,
    sizeof(XgbeAdapter->rx_ring),
    &MemAddr);

  MemPtr = (UINT32 *)&MemAddr;
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDBAL(0), *MemPtr);
#endif

  //
  // Set the MemPtr to the high dword of the rx_ring so we can store it in RDBAH0.
  // Right shifts do not seem to work with the EFI compiler so we do it like this for now.
  //
#ifndef INTEL_KDNET
  MemAddr = (UINT64) (UINTN) XgbeAdapter->rx_ring;
  MemPtr  = &((UINT32) MemAddr);
#endif
  MemPtr++;
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDBAH (0), *MemPtr);
  DEBUGPRINT (XGBE, ("Rdbal0 %X\n", (UINT32) IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RDBAL (0))));
  DEBUGPRINT (XGBE, ("RdBah0 %X\n", (UINT32) IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RDBAH (0))));
  DEBUGPRINT (XGBE, ("Rx Ring %X\n", XgbeAdapter->rx_ring));

  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDH (0), 0);
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDH (0), 0);
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDT (0), 0);

  //
  // We must wait for the receive unit to be enabled before we move
  // the tail descriptor or the hardware gets confused.
  //
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDT (0), 0);

  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDLEN (0), (sizeof (struct ixgbe_legacy_rx_desc) * DEFAULT_RX_DESCRIPTORS));

  if (XgbeAdapter->VlanEnable) {
    DEBUGPRINT (CRITICAL, ("1: Setting VLAN tag = %d\n", XgbeAdapter->VlanTag));
    ixgbe_clear_vfta(&XgbeAdapter->hw);
#ifdef INTEL_KDNET
    ixgbe_set_vfta(&XgbeAdapter->hw, XgbeAdapter->VlanTag, 0, TRUE, FALSE);
#else
    ixgbe_set_vfta(&XgbeAdapter->hw, XgbeAdapter->VlanTag, 0, TRUE);
#endif
    XgbeSetRegBits (XgbeAdapter, IXGBE_VLNCTRL, IXGBE_VLNCTRL_VFE);
    XgbeSetRegBits (XgbeAdapter, IXGBE_RXDCTL(0), IXGBE_RXDCTL_VME);
  }

  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_SRRCTL (0), IXGBE_SRRCTL_DESCTYPE_LEGACY);

    XgbeSetRegBits (XgbeAdapter, IXGBE_RXDCTL (0), IXGBE_RXDCTL_ENABLE);
    i = 0;
    do {
      TempReg = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RXDCTL (0));
      i++;
      if ((TempReg & IXGBE_RXDCTL_ENABLE) != 0) {
        DEBUGPRINT (XGBE, ("RX queue enabled, after attempt i = %d\n", i));
        break;
      }

      DelayInMicroseconds (XgbeAdapter, 1);
    } while (i < 1000);

    if (i >= 1000) {
      DEBUGPRINT (CRITICAL, ("Enable RX queue failed!\n"));
    }
  }

  //
  // Point the current working rx buffer to the top of the list
  //
  XgbeAdapter->cur_rx_ind     = 0;
  XgbeAdapter->cur_tx_ind     = 0;  // currently usable buffer
  XgbeAdapter->xmit_done_head = 0;  // the last cleaned buffer
#ifndef INTEL_KDNET
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDBAL (0), (UINT32) (UINTN) (XgbeAdapter->tx_ring));
  MemAddr = (UINT64) (UINTN) XgbeAdapter->tx_ring;
  MemPtr  = &((UINT32) MemAddr);
#else
  ixgbe_MapMem(
    XgbeAdapter,
    (UINT64)(UINTN)XgbeAdapter->tx_ring,
    sizeof(XgbeAdapter->tx_ring),
    &MemAddr);

  MemPtr = (UINT32 *)&MemAddr;
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDBAL (0), (UINT32) (UINTN) *MemPtr);
#endif
  MemPtr++;
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDBAH (0), *MemPtr);
  DEBUGPRINT (XGBE, ("TdBah0 %X\n", *MemPtr));
  DEBUGWAIT (XGBE);
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_TDLEN (0), (sizeof (struct ixgbe_legacy_tx_desc) * DEFAULT_TX_DESCRIPTORS));
  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    XgbeSetRegBits (XgbeAdapter, IXGBE_DMATXCTL, IXGBE_DMATXCTL_TE);
  }

  XgbeSetRegBits (XgbeAdapter, IXGBE_TXDCTL (0), IXGBE_TXDCTL_ENABLE | IXGBE_TX_PAD_ENABLE);


  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    i = 0;
    do {
      TempReg = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_TXDCTL (0));
      i++;
      if ((TempReg & IXGBE_TXDCTL_ENABLE) != 0) {
        DEBUGPRINT (XGBE, ("TX queue enabled, after attempt i = %d\n", i));
        break;
      }

      DelayInMicroseconds (XgbeAdapter, 1);
    } while (i < 1000);
    if (i >= 1000) {
      DEBUGPRINT (CRITICAL, ("Enable TX queue failed!\n"));
    }
  }

  XgbePciFlush (XgbeAdapter);
}

VOID
XgbeSetFilter (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT16           NewFilter
  )
/*++

Routine Description:
  Sets receive filters.

Arguments:
  XgbeAdapter       - Pointer to the adapter structure
  NewFilter         - A PXE_OPFLAGS bit field indicating what filters to use.

Returns:
  None

--*/
{
  UINT32                  Fctrl;
  UINT32                  FctrlInitial;

  DEBUGPRINT (RXFILTER, ("XgbeSetFilter: "));

  Fctrl = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_FCTRL);
  FctrlInitial = Fctrl;

  if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS) {
    Fctrl |= IXGBE_FCTRL_UPE;
    DEBUGPRINT (RXFILTER, ("IXGBE_FCTRL_UPE "));
  }

  if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST) {
    Fctrl |= IXGBE_FCTRL_BAM;
    DEBUGPRINT (RXFILTER, ("IXGBE_FCTRL_BAM "));
  }

  if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) {
    Fctrl |= IXGBE_FCTRL_MPE;
    DEBUGPRINT (RXFILTER, ("IXGBE_FCTRL_MPE "));
  }

  XgbeAdapter->RxFilter |= NewFilter;
  DEBUGPRINT (RXFILTER, (", RxFilter=%08x, FCTRL=%08x\n", XgbeAdapter->RxFilter, Fctrl));

  if (Fctrl != FctrlInitial) {
    //
    // Filter has changed - disable receiver and write the new value
    //
    if (XgbeAdapter->ReceiveStarted == TRUE) {
      XgbeReceiveDisable (XgbeAdapter);
    }
    IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_FCTRL, Fctrl);
  }

  if (XgbeAdapter->RxFilter != 0) {
    XgbeReceiveEnable (XgbeAdapter);
  }

  DEBUGWAIT(XGBE);
  return;
};

UINTN
XgbeClearFilter (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT16           NewFilter
  )
/*++

Routine Description:
  Clears receive filters.

Arguments:
  XgbeAdapter       - Pointer to the adapter structure
  NewFilter         - A PXE_OPFLAGS bit field indicating what filters to clear.

Returns:
  None

--*/
{
  UINT32                  Fctrl;
  UINT32                  FctrlInitial;

  DEBUGPRINT (RXFILTER, ("XgbeClearFilter %x: ", NewFilter));

  Fctrl = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_FCTRL);
  FctrlInitial = Fctrl;

  if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS) {
    Fctrl &= ~IXGBE_FCTRL_UPE;
    DEBUGPRINT (RXFILTER, ("IXGBE_FCTRL_UPE "));
  }

  if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST) {
    Fctrl &= ~IXGBE_FCTRL_BAM;
    DEBUGPRINT (RXFILTER, ("IXGBE_FCTRL_BAM "));
  }

  if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) {
    //
    // add the MPE bit to the variable to be written to the RCTL
    //
    Fctrl &= ~IXGBE_FCTRL_MPE;
    DEBUGPRINT (RXFILTER, ("IXGBE_FCTRL_MPE "));
  }

  XgbeAdapter->RxFilter &= ~NewFilter;
  DEBUGPRINT (RXFILTER, (", RxFilter=%08x, FCTRL=%08x\n", XgbeAdapter->RxFilter, Fctrl));

  if (Fctrl != FctrlInitial) {
    //
    // Filter has changed - disable receiver and write the new value
    //
    if (XgbeAdapter->ReceiveStarted == TRUE) {
      XgbeReceiveDisable (XgbeAdapter);
    }
    IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_FCTRL, Fctrl);
  }

  if (XgbeAdapter->RxFilter != 0) {
    XgbeReceiveEnable (XgbeAdapter);
  }

  DEBUGPRINT (XGBE, ("XgbeClearFilter done.\n"));
  DEBUGWAIT(XGBE);
  return 0;
};


VOID
XgbeSetMcastList (
  XGBE_DRIVER_DATA *XgbeAdapter
  )
{
  BOOLEAN ReceiveStarted;

  //
  // We must save the value of ReceiveStarted to determine whether to
  // restart the receiver.  XgbeReceiveDisable will reset this value to
  // false.
  //
  ReceiveStarted = XgbeAdapter->ReceiveStarted;
  if (XgbeAdapter->ReceiveStarted == TRUE) {
    XgbeReceiveDisable (XgbeAdapter);
  }

  if (XgbeAdapter->McastList.Length == 0) {
    DEBUGPRINT (RXFILTER, ("Resetting multicast list\n"));
    XgbeAdapter->RxFilter &= ~PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST;
    ixgbe_disable_mc(&XgbeAdapter->hw);
    if (ReceiveStarted) {
      XgbeReceiveEnable (XgbeAdapter);
    }
    return;
  }

  XgbeAdapter->RxFilter |= PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST;

  DEBUGPRINT (RXFILTER, ("Update multicast list, count=%d\n", XgbeAdapter->McastList.Length));

  ixgbe_update_mc_addr_list (
    &XgbeAdapter->hw,
    (UINT8*) &XgbeAdapter->McastList.McAddr[0][0],
    XgbeAdapter->McastList.Length,
    _XgbeIterateMcastMacAddr,
    true
    );

  ixgbe_enable_mc(&XgbeAdapter->hw);

  //
  //  Assume that if we are updating the MC list that we want to also
  // enable the receiver.
  //
  XgbeReceiveEnable (XgbeAdapter);

}


VOID
XgbeReceiveDisable (
  IN XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Stops the receive unit.

Arguments:
  XgbeAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  struct ixgbe_legacy_rx_desc *ReceiveDesc;
  UINTN                       i;
  UINT32                      RxdCtl;

  DEBUGPRINT (XGBE, ("XgbeReceiveDisable\n"));

  if (XgbeAdapter->ReceiveStarted == FALSE) {
    DEBUGPRINT(CRITICAL, ("Receive unit already disabled!\n"));
    return;
  }

  XgbeClearRegBits(XgbeAdapter, IXGBE_RXDCTL(0), IXGBE_RXDCTL_ENABLE);
  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    do {
#ifndef INTEL_KDNET
      gBS->Stall(1);
#else
      KeStallExecutionProcessor(1);
#endif
      RxdCtl = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RXDCTL(0));
    } while((RxdCtl & IXGBE_RXDCTL_ENABLE) != 0);
    DEBUGPRINT (XGBE, ("Receiver Disabled\n"));
  }

  ixgbe_disable_rx (&XgbeAdapter->hw);
  //
  // Reset the transmit and receive descriptor rings
  //
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDH (0), 0);
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDT (0), 0);
  XgbeAdapter->cur_rx_ind = 0;

  //
  // Clean up any left over packets
  //
  ReceiveDesc = XgbeAdapter->rx_ring;
  for (i = 0; i < DEFAULT_RX_DESCRIPTORS; i++) {
    ReceiveDesc->length = 0;
    ReceiveDesc->status = 0;
    ReceiveDesc->errors = 0;
    ReceiveDesc++;
  }

  XgbeAdapter->ReceiveStarted = FALSE;

  return ;
}

VOID
XgbeReceiveEnable (
  IN XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Starts the receive unit.

Arguments:
  XgbeAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  UINT32  TempReg;

  DEBUGPRINT (XGBE, ("XgbeReceiveEnable\n"));

  if (XgbeAdapter->ReceiveStarted == TRUE) {
    DEBUGPRINT(CRITICAL, ("Receive unit already started!\n"));
    return;
  }
  XgbeAdapter->IntStatus = 0;

  if (XgbeAdapter->VlanEnable)
    XgbeSetRegBits (XgbeAdapter, IXGBE_RXDCTL (0), IXGBE_RXDCTL_ENABLE | IXGBE_RXDCTL_VME);
  XgbeSetRegBits(XgbeAdapter, IXGBE_RXDCTL(0), IXGBE_RXDCTL_ENABLE);
  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    do {
#ifndef INTEL_KDNET
      gBS->Stall(1);
#else
      KeStallExecutionProcessor(1);
#endif
      TempReg = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_RXDCTL(0));
    } while((TempReg & IXGBE_RXDCTL_ENABLE) == 0);
  }

  //
  // Advance the tail descriptor to tell the hardware it can use the descriptors
  //
  IXGBE_WRITE_REG (&XgbeAdapter->hw, IXGBE_RDT (0), DEFAULT_RX_DESCRIPTORS - 1);

  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    DEBUGPRINT (XGBE, ("Disabling SECRX.\n"));
    XgbeSetRegBits (XgbeAdapter, IXGBE_SECRXCTRL, IXGBE_SECRXCTRL_RX_DIS);
    do {
      TempReg = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_SECRXSTAT);
      DEBUGPRINT (XGBE, ("SEC_RX = %x\n", TempReg));
    } while ((TempReg & IXGBE_SECRXSTAT_SECRX_RDY) == 0);
    DEBUGPRINT (XGBE, ("SEC_RX has been disabled.\n"));
  }

  ixgbe_enable_rx (&XgbeAdapter->hw);

  if ((XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X540)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550)
   || (XgbeAdapter->hw.mac.type == ixgbe_mac_X550EM_x)
   ){
    XgbeClearRegBits (XgbeAdapter, IXGBE_SECRXCTRL, IXGBE_SECRXCTRL_RX_DIS);
  }

  XgbeAdapter->ReceiveStarted = TRUE;
}

VOID
DelayInMicroseconds (
  IN XGBE_DRIVER_DATA  *XgbeAdapter,
  UINT32               MicroSeconds
  )
/*++

Routine Description:

Arguments:
  XgbeAdapter                     - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  MicroSeconds                    - Time to delay in Microseconds.
Returns:

--*/
{
  if (XgbeAdapter->Delay != NULL) {
    (*XgbeAdapter->Delay) (XgbeAdapter->Unique_ID, MicroSeconds);
  } else {

#ifndef INTEL_KDNET

    gBS->Stall (MicroSeconds);

#else

    if (MicroSeconds > 0xffffffff) {
        MicroSeconds = 0xffffffff;
    }

    KeStallExecutionProcessor((UINT32)MicroSeconds);

#endif

  }
}

UINT32
IxgbeHtonl (
  IN UINT32 Dword
  )
/*++

Routine Description:
  Swaps the bytes from machine order to network order (Big Endian)

Arguments:
  Dword              - 32-bit input value

Returns:
  Results           - Big Endian swapped value

--*/
{
  UINT8   Buffer[4];
  UINT32  *Result;

  DEBUGPRINT (XGBE, ("IxgbeHtonl = %x\n", Dword));

  Buffer[3] = (UINT8) Dword;
  Dword     = Dword >> 8;
  Buffer[2] = (UINT8) Dword;
  Dword     = Dword >> 8;
  Buffer[1] = (UINT8) Dword;
  Dword     = Dword >> 8;
  Buffer[0] = (UINT8) Dword;

  Result    = (UINT32 *) Buffer;
  DEBUGPRINT (XGBE, ("IxgbeHtonl result %x\n", *Result));
  DEBUGWAIT (XGBE);

  return *Result;
}

UINT32
XgbeInDword (
  IN XGBE_DRIVER_DATA *XgbeAdapter,
  IN UINT32           Port
  )
/*++

Routine Description:
  This function calls the MemIo callback to read a dword from the device's
  address space

Arguments:
  XgbeAdapter       - Adapter structure
  Port              - Address to read from

Returns:
  Results           - The data read from the port.

--*/
{
  UINT32  Results;

  MemoryFence ();
#ifndef INTEL_KDNET
  XgbeAdapter->PciIo->Mem.Read (
                            XgbeAdapter->PciIo,
                            EfiPciIoWidthUint32,
                            0,
                            Port,
                            1,
                            (VOID *) (&Results)
                            );
#else
  ReadDeviceMemory((VOID *) (&Results),
                            0,
                            Port,
                            sizeof(UINT32),
                            1);
#endif
  MemoryFence ();
  return Results;
}

VOID
XgbeOutDword (
  IN XGBE_DRIVER_DATA *XgbeAdapter,
  IN UINT32           Port,
  IN UINT32           Data
  )
/*++

Routine Description:
  This function calls the MemIo callback to write a word from the device's
  address space

Arguments:
  XgbeAdapter       - Adapter structure
  Data              - Data to write to Port
  Port              - Address to write to

Returns:
  none

--*/
{
  UINT32  Value;

  Value = Data;

  MemoryFence ();

#ifndef INTEL_KDNET
  XgbeAdapter->PciIo->Mem.Write (
                            XgbeAdapter->PciIo,
                            EfiPciIoWidthUint32,
                            0,
                            Port,
                            1,
                            (VOID *) (&Value)
                            );
#else
  WriteDeviceMemory((VOID *) (&Value),
                            0,
                            Port,
                            sizeof(UINT32),
                            1);

#endif

  MemoryFence ();
  return ;
}

UINT32
XgbeSetRegBits (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT32           Register,
  UINT32           BitMask
  )
/*++

Routine Description:
  Sets specified bits in a device register

Arguments:
  XgbeAdapter       - Pointer to the device instance
  Register          - Register to write
  BitMask           - Bits to set

Returns:
  Data              - Returns the value read from the PCI register.

--*/
{
  UINT32 TempReg;

  TempReg = IXGBE_READ_REG (&XgbeAdapter->hw, Register);
  TempReg |= BitMask;
  IXGBE_WRITE_REG (&XgbeAdapter->hw, Register, TempReg);

  return TempReg;
}

UINT32
XgbeClearRegBits (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT32           Register,
  UINT32           BitMask
  )
/*++

Routine Description:
  Clears specified bits in a device register

Arguments:
  XgbeAdapter       - Pointer to the device instance
  Register          - Register to write
  BitMask           - Bits to clear

Returns:
  Data              - Returns the value read from the PCI register.

--*/

{
  UINT32 TempReg;

  TempReg = IXGBE_READ_REG (&XgbeAdapter->hw, Register);
  TempReg &= ~BitMask;
  IXGBE_WRITE_REG (&XgbeAdapter->hw, Register, TempReg);

  return TempReg;
}


UINT16
XgbeReadPci16 (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT32           Offset
  )
/*++

Routine Description:
  This function calls the EFI PCI IO protocol to read a value from the device's PCI
  register space.

Arguments:
  XgbeAdapter       - Pointer to the shared code hw structure.
  Offset            - Which register to read from.

Returns:
  Data              - Returns the value read from the PCI register.

--*/
{
  UINT16  Data;

  MemoryFence ();

#ifndef INTEL_KDNET

  XgbeAdapter->PciIo->Pci.Read (
                            XgbeAdapter->PciIo,
                            EfiPciIoWidthUint16,
                            Offset,
                            1,
                            (VOID *) (&Data)
                            );
#else

  GetDevicePciDataByOffset(&Data, Offset, sizeof(UINT16));

#endif

  MemoryFence ();
  return Data;
}

VOID
XgbeWritePci16 (
  XGBE_DRIVER_DATA *XgbeAdapter,
  UINT32           Offset,
  UINT16           Data
  )
/*++

Routine Description:
  This function calls the EFI PCI IO protocol to write a value to the device's PCI
  register space.

Arguments:
  XgbeAdapter         - Pointer to the adapter structure.
  Offset              - Which register to read from.
  Data                - Returns the value read from the PCI register.

Returns:
  VOID

--*/
{
  MemoryFence ();

#ifndef INTEL_KDNET

  XgbeAdapter->PciIo->Pci.Write (
                            XgbeAdapter->PciIo,
                            EfiPciIoWidthUint16,
                            Offset,
                            1,
                            (VOID *) (&Data)
                            );

#else

  SetDevicePciDataByOffset(&Data, Offset, sizeof(UINT16));

#endif

  MemoryFence ();
}

VOID
XgbePciFlush (
  IN XGBE_DRIVER_DATA *XgbeAdapter
  )
/*++

Routine Description:
  Flushes a PCI write transaction to system memory.

Arguments:
  XgbeAdapter         - Pointer to the adapter structure.

Returns:
  none

--*/
{
  MemoryFence ();

#ifndef INTEL_KDNET

  XgbeAdapter->PciIo->Flush (XgbeAdapter->PciIo);

#endif

  MemoryFence ();

  return ;
}

UINT16
XgbeFreeTxBuffers (
  IN XGBE_DRIVER_DATA *XgbeAdapter,
  IN UINT16           NumEntries,
  OUT UINT64          *TxBuffer
  )
/*++

Routine Description:
  Free TX buffers that have been transmitted by the hardware.

Arguments:
  XgbeAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on.
  NumEntries           - Number of entries in the array which can be freed.
  TxBuffer             - Array to pass back free TX buffer

Returns:
   Number of TX buffers written.

--*/
{
  struct ixgbe_legacy_tx_desc *TransmitDescriptor;
  UINT32                      Tdh;
  UINT16                      i;

  //
  //  Read the TX head posistion so we can see which packets have been sent out on the wire.
  //
  Tdh = IXGBE_READ_REG (&XgbeAdapter->hw, IXGBE_TDH (0));
  DEBUGPRINT (XGBE, ("TDH = %d, XgbeAdapter->xmit_done_head = %d\n", Tdh, XgbeAdapter->xmit_done_head));

  //
  //  If Tdh does not equal xmit_done_head then we will fill all the transmitted buffer
  // addresses between Tdh and xmit_done_head into the completed buffers array
  //
  i = 0;
  do {
    if (i >= NumEntries) {
      DEBUGPRINT (XGBE, ("Exceeded number of DB entries, i=%d, NumEntries=%d\n", i, NumEntries));
      break;
    }

    TransmitDescriptor = &XgbeAdapter->tx_ring[XgbeAdapter->xmit_done_head];
    if ((TransmitDescriptor->upper.fields.status & IXGBE_TXD_STAT_DD) != 0) {

      if (XgbeAdapter->TxBufferUsed[XgbeAdapter->xmit_done_head] == 0) {
        DEBUGPRINT (CRITICAL, ("ERROR: TX buffer complete without being marked used!\n"));
        break;
      }

      DEBUGPRINT (XGBE, ("Writing buffer address %d, %x\n", i, TxBuffer[i]));
      TxBuffer[i] = XgbeAdapter->TxBufferUsed[XgbeAdapter->xmit_done_head];
      i++;

#ifdef INTEL_KDNET
      ixgbe_UnMapMem (
        XgbeAdapter,
        XgbeAdapter->TxBufferUnmappedAddr[XgbeAdapter->xmit_done_head],
        TransmitDescriptor->lower.flags.length,
        TransmitDescriptor->buffer_addr
        );
      XgbeAdapter->TxBufferUnmappedAddr[XgbeAdapter->xmit_done_head] = 0;
#endif

      XgbeAdapter->TxBufferUsed[XgbeAdapter->xmit_done_head]  = 0;
      TransmitDescriptor->upper.fields.status                 = 0;

      XgbeAdapter->xmit_done_head++;
      if (XgbeAdapter->xmit_done_head >= DEFAULT_TX_DESCRIPTORS) {
        XgbeAdapter->xmit_done_head = 0;
      }
    } else {
      DEBUGPRINT (XGBE, ("TX Descriptor %d not done\n", XgbeAdapter->xmit_done_head));
      break;
    }
  } while (Tdh != XgbeAdapter->xmit_done_head);
  return i;
}

BOOLEAN
IsLinkUp(
  IN XGBE_DRIVER_DATA *XgbeAdapter
)
{
  ixgbe_link_speed        Speed;
  BOOLEAN                 LinkUp;

  ixgbe_check_link(&XgbeAdapter->hw, &Speed, &LinkUp, FALSE);
  return LinkUp;
}


#ifndef INTEL_KDNET

UINT8
GetLinkSpeed(
  IN XGBE_DRIVER_DATA *XgbeAdapter
)
/*++

Routine Description:
  Gets current link speed and duplex from shared code and converts it to UNDI
  driver format

Arguments:
  GigAdapter        - Pointer to the device instance

Returns:
  Data              - Returns the link speed information.

--*/
{
  ixgbe_link_speed    Speed     = 0;
  BOOLEAN             LinkUp;
  UINT8               LinkSpeed = LINK_SPEED_UNKNOWN;

  ixgbe_check_link(&XgbeAdapter->hw, &Speed, &LinkUp, FALSE);
  switch (Speed) {
  case IXGBE_LINK_SPEED_100_FULL:
      LinkSpeed = LINK_SPEED_100FULL;
    break;
  case IXGBE_LINK_SPEED_1GB_FULL:
      LinkSpeed = LINK_SPEED_1000FULL;
    break;
  case IXGBE_LINK_SPEED_10GB_FULL:
      LinkSpeed = LINK_SPEED_10000FULL;
    break;
  default:
    LinkSpeed = LINK_SPEED_UNKNOWN;
  }
  DEBUGPRINT(HII, ("Link Speed Status %x\n", LinkSpeed));
  return LinkSpeed;
}

VOID
BlinkLeds(
  IN XGBE_DRIVER_DATA *XgbeAdapter,
  IN UINT32           Time
)
{
  UINT32    ledctl;
  BOOLEAN   LedOn = FALSE;
  UINT32    i = 0;
  UINT32    BlinkInterval = 200;
  //
  // BlinkInterval value above displayed in ms.
  //

  //
  // IXGBE shared code doesn't save/restore the LEDCTL register when blinking used.
  //
  ledctl = IXGBE_READ_REG(&XgbeAdapter->hw, IXGBE_LEDCTL);

  if(XgbeAdapter->hw.mac.type == ixgbe_mac_X540) {
          /* This is workaround for bad board design made by customer.
           * LED0 is not used on TwinPond, but in customer design the
           * anode of LED2 is connected to LED0, so we have to drive
           * the pin HIGH.
           */
      IXGBE_WRITE_REG(&XgbeAdapter->hw, IXGBE_LEDCTL, (ledctl & ~0xFF) | 0x4E);
  } else if(XgbeAdapter->hw.mac.type == ixgbe_mac_82599EB &&
            XgbeAdapter->hw.device_id == 0x154A) {
          /* This is workaround for bad board design NNT SF QP (SFP+).
           * 0x4E00 set LED1_ON and LED1_INVERT. That would drive LED1
           * pin HIGH. In all NNT the LED1 is pulled to VSS so this setting
           * turn the LED1 OFF. The SF QP (SFP+) board utilize only one led which
           * is unusually connected: anode to LED1, cathode to LED2.
           */
          IXGBE_WRITE_REG(&XgbeAdapter->hw, IXGBE_LEDCTL, (ledctl & ~0xFF00) | 0x4E00);
  }
  if(Time > 0 && BlinkInterval > 0) {
      for(i = 0; i < Time * 1000 ; i += BlinkInterval)
      {
          LedOn = ~LedOn;
          if(LedOn == FALSE) {
          ixgbe_led_off(&XgbeAdapter->hw, 2);
          } else {
          ixgbe_led_on(&XgbeAdapter->hw, 2);
      }
      DelayInMicroseconds(XgbeAdapter, BlinkInterval * 1000);
      }
  }
  IXGBE_WRITE_REG(&XgbeAdapter->hw, IXGBE_LEDCTL, ledctl);
  IXGBE_WRITE_FLUSH(&XgbeAdapter->hw);
}

EFI_STATUS
ReadPbaString(
  IN     XGBE_DRIVER_DATA *XgbeAdapter,
  IN OUT UINT8            *PbaNumber,
  IN     UINT32           PbaNumberSize
)
{
  if (ixgbe_read_pba_string (&XgbeAdapter->hw, PbaNumber, PbaNumberSize) == IXGBE_SUCCESS) {
    return EFI_SUCCESS;
  } else {
    return EFI_DEVICE_ERROR;
  };
}

#endif

/*++

Routine Description:
  Reverse bytes of a word (endianness change)

Arguments:
  Word              - Value to be modified

Returns:
  Data              - Returns the same value in different endianness

--*/
UINT16
IxgbeReverseWord(
  IN UINT16 Word
)
{
  UINT8 SwapBuf;
  UINT8 * Ptr;

  Ptr = (UINT8*)&Word;
  SwapBuf = Ptr[0];
  Ptr[0] = Ptr[1];
  Ptr[1] = SwapBuf;

  return Word;
}

/*++

Routine Description:
  Reverse bytes of a double word (endianness change)

Arguments:
  Word              - Value to be modified

Returns:
  Data              - Returns the same value in different endianness

--*/
UINT32
IxgbeReverseDword(
  IN UINT32 Dword
)
{
  UINT16 SwapBuf;
  UINT16 * Ptr;

  Ptr = (UINT16*)&Dword;
  SwapBuf = Ptr[0];
  Ptr[0] = IxgbeReverseWord(Ptr[1]);
  Ptr[1] = IxgbeReverseWord(SwapBuf);

  return Dword;
}



