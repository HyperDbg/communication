/**************************************************************************

Copyright (c) 2001-2016, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

#include "e1000.h"
#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER
#include "DeviceSupport.h"
#endif
#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifndef INTEL_KDNET
#include "UndiCommonLib.h"
#endif
#endif


extern GIG_DRIVER_DATA  *e1000_NIC_Data;  // is actually an array of structures

//
// Global variables for blocking IO
//
STATIC BOOLEAN          gInitializeLock = TRUE;
#ifndef INTEL_KDNET
STATIC EFI_LOCK         gLock;
#endif

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifndef INTEL_KDNET

INT32
_ReadPhyRegister16Ex(
    IN  struct e1000_hw           *hw,
    IN  UINT32             Page,
    IN  UINT32             Offset,
    OUT UINT16*            Value
    );
INT32
_WritePhyRegister16Ex(
    IN  struct e1000_hw           *hw,
    IN  UINT32             Page,
    IN  UINT32             Offset,
    IN  UINT16             Data
    );
    
#else // for kdnet - we define the functions here instead of just forward-declaring them

INT32
_ReadPhyRegister16Ex(
    IN  struct e1000_hw           *hw,
    IN  UINT32             Page,
    IN  UINT32             Offset,
    OUT UINT16*            Value
    )
{
    //
    // See e1000_hw.c and e1000_hw.h for bit field specifications for PHY page register
    //
    Page = (Page & 0x07FF) << 5;
    Offset = ((Offset & 0x1F) | Page);

    return e1000_read_phy_reg(hw, Offset, Value);
}

INT32
_WritePhyRegister16Ex(
    IN  struct e1000_hw           *hw,
    IN  UINT32             Page,
    IN  UINT32             Offset,
    IN  UINT16             Data
    )
{
    /* see sdk/adapters/module0/e1000_hw.c and e1000_hw.h for bit field
     * specifications for PHY page register */
    Page = (Page & 0x07FF) << 5;
    Offset = ((Offset & 0x1F) | Page);

    return e1000_write_phy_reg(hw, Offset, Data);
}

#endif // INTEL_KDNET
#endif // INTEL_KDNET_1G_CLIENT_MERGE_MARKER

#ifndef INTEL_KDNET

VOID
_DisplayBuffersAndDescriptors (
  GIG_DRIVER_DATA *GigAdapterInfo
  );

EFI_STATUS
ProcessFoD (
  IN GIG_DRIVER_DATA *GigAdapter,
  IN EFI_SYSTEM_TABLE *SystemTable
  );

#endif

//
// Local Functions
//
BOOLEAN
e1000_DownShift (
  GIG_DRIVER_DATA *GigAdapter
  );

UINTN
e1000_Shutdown (
  GIG_DRIVER_DATA *GigAdapter
  );

UINTN
e1000_SetFilter (
  GIG_DRIVER_DATA *GigAdapter,
  UINT16          NewFilter,
  UINT64          cpb,
  UINT32          cpbsize
  );

UINTN
e1000_Statistics (
  GIG_DRIVER_DATA *GigAdapter,
  UINT64          DBaddr,
  UINT16          DBsize
  );

VOID
e1000_TxRxConfigure (
  GIG_DRIVER_DATA *GigAdapter
  );

UINTN
e1000_SetInterruptState (
  GIG_DRIVER_DATA *GigAdapter
  );

BOOLEAN
e1000_WaitForAutoNeg (
  IN GIG_DRIVER_DATA *GigAdapter
  );

//
// end function prototypes
//

BOOLEAN
IsAltMacAddrSupported(
	UNDI_PRIVATE_DATA	*UndiPrivateData
)
{
	UINT16	BackupMacPointer;

	//
	// Check to see if the backup MAC address location pointer is set
	//
	e1000_read_nvm (&UndiPrivateData->NicInfo.hw, NVM_ALT_MAC_ADDR_PTR, 1, &BackupMacPointer);

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



VOID
e1000_BlockIt (
  IN GIG_DRIVER_DATA  *GigAdapter,
  UINT32              flag
  )
/*++

Routine Description:
  Implements IO blocking when reading DMA memory.

Arguments:
  GigAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on.
  flag                             - Block flag
Returns:

--*/
{
  if (GigAdapter->Block != NULL) {
    (*GigAdapter->Block) (GigAdapter->Unique_ID, flag);
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

VOID
e1000_MapMem (
  IN GIG_DRIVER_DATA  *GigAdapter,
  IN UINT64           VirtualAddress,
  IN UINT32           Size,
#ifndef INTEL_KDNET
  OUT UINTN           *MappedAddress
#else
  OUT UINT64          *MappedAddress
#endif
  )
/*++

Routine Description:
  Maps a virtual address to a physical address.  This is necessary for runtime functionality and also
  on some platforms that have a chipset that cannot allow DMAs above 4GB.

Arguments:
  GigAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on.
  VirtualAddress                   - Virtual Address of the data buffer to map.
  Size                             - Minimum size of the buffer to map.
  MappedAddress                    - Pointer to store the address of the mapped buffer.
Returns:

--*/
{
  if (GigAdapter->MapMem != NULL) {
#ifdef INTEL_KDNET
    *MappedAddress = 0;
#endif
    (*GigAdapter->MapMem) (
      GigAdapter->Unique_ID,
      VirtualAddress,
      Size,
      TO_DEVICE,
      (UINT64) MappedAddress
      );
    //
    // Workaround: on some systems mapmem may not be implemented correctly, so just
    // pass back the original address
    //
    if (*MappedAddress == 0) {
#ifndef INTEL_KDNET
      *((UINTN*) MappedAddress) = (UINTN) VirtualAddress;
#else
      *MappedAddress = VirtualAddress;
#endif
    }
  } else {
#ifndef INTEL_KDNET
      *((UINTN*) MappedAddress) = (UINTN) VirtualAddress;
#else
      *MappedAddress = VirtualAddress;
#endif
  }
}

VOID
e1000_UnMapMem (
  IN GIG_DRIVER_DATA  *GigAdapter,
  IN UINT64           VirtualAddress,
  IN UINT32           Size,
  IN UINT64           MappedAddress
  )
/*++

Routine Description:
  Maps a virtual address to a physical address.  This is necessary for runtime functionality and also
  on some platforms that have a chipset that cannot allow DMAs above 4GB.

Arguments:
  GigAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on.
  VirtualAddress                   - Virtual Address of the data buffer to map.
  Size                             - Minimum size of the buffer to map.
  MappedAddress                    - Pointer to store the address of the mapped buffer.
Returns:

--*/
{
  if (GigAdapter->UnMapMem != NULL) {
    (*GigAdapter->UnMapMem) (
      GigAdapter->Unique_ID,
      VirtualAddress,
      Size,
      TO_DEVICE,
      (UINT64) MappedAddress
      );
  }
}

VOID
e1000_MemCopy (
    IN UINT8* Dest,
    IN UINT8* Source,
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
Returns:
  VOID
--*/

{
  UINT32 BytesToCopy;
  UINT32 IntsToCopy;
  UINTN* SourcePtr;
  UINTN* DestPtr;
  UINT8* SourceBytePtr;
  UINT8* DestBytePtr;


  IntsToCopy = Count / sizeof(UINTN);
  BytesToCopy = Count % sizeof(UINTN);
#ifdef EFI64
  //
  // Itanium cannot handle memory accesses that are not naturally aligned.  Determine
  // if 64-bit copy is even possible with these start addresses.
  //
  if (( ( ((UINTN) Source) & 0x0007) != 0) || (( ((UINTN) Dest) & 0x0007) != 0) ) {
    IntsToCopy = 0;
    BytesToCopy = Count;
  }
#endif

  SourcePtr = (UINTN*) Source;
  DestPtr = (UINTN*) Dest;

  while (IntsToCopy > 0) {
    *DestPtr = *SourcePtr;
    SourcePtr++;
    DestPtr++;
    IntsToCopy--;
  }

  //
  // Copy the leftover bytes.
  //
  SourceBytePtr = (UINT8*) SourcePtr;
  DestBytePtr = (UINT8*) DestPtr;
  while (BytesToCopy > 0) {
    *DestBytePtr = *SourceBytePtr;
    SourceBytePtr++;
    DestBytePtr++;
    BytesToCopy--;
  }
}

BOOLEAN
e1000_DownShift (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Wait for up to 15 seconds for two pair downshift to complete

Arguments:
  GigAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..

Returns:
  TRUE if two pair downshift was successful and link was established
  FALSE otherwise

--*/
{
  UINTN   i;
  UINT32  Status;

  DEBUGPRINT (E1000, ("e1000_DownShift: Attempting downshift\n"));

  i = 0;
  for (i = 0; i < 15; i++) {
    DelayInMilliseconds (1000);
    Status = E1000_READ_REG (&GigAdapter->hw, E1000_STATUS);
    DEBUGPRINT(E1000, ("Status = %x\n", Status));
    if ((Status & E1000_STATUS_LU) != 0) {
      DEBUGPRINT(E1000, ("Successfully established link\n"));
      return TRUE;
    }
  }

  return FALSE;
}

UINTN
e1000_Statistics (
  GIG_DRIVER_DATA *GigAdapter,
  UINT64          DBaddr,
  UINT16          DBsize
  )
/*++

Routine Description:
  copies the stats from our local storage to the protocol storage.
  which means it will read our read and clear numbers, so some adding is required before
  we copy it over to the protocol.

Arguments:
  GigAdapter                   - Pointer to the NIC data structure information
                                    which the UNDI driver is layering on..
  DBaddr                           - The data block address
  DBsize                           - The data block size

Returns:
  PXE Status code

--*/
{
  PXE_DB_STATISTICS     *DbPtr;
  struct e1000_hw       *hw;
  struct e1000_hw_stats *st;
  UINTN                 stat;

  hw  = &GigAdapter->hw;
  st  = &GigAdapter->stats;

#define UPDATE_OR_RESET_STAT(sw_reg, hw_reg) \
  do { \
    st->sw_reg = DBaddr ? st->sw_reg + E1000_READ_REG (hw, hw_reg) : 0; \
  } while (0)

  {
    UPDATE_OR_RESET_STAT (crcerrs, E1000_CRCERRS);
  }

  UPDATE_OR_RESET_STAT (gprc, E1000_GPRC);
  UPDATE_OR_RESET_STAT (bprc, E1000_BPRC);
  UPDATE_OR_RESET_STAT (mprc, E1000_MPRC);
  UPDATE_OR_RESET_STAT (roc, E1000_ROC);
  UPDATE_OR_RESET_STAT (prc64, E1000_PRC64);
  UPDATE_OR_RESET_STAT (prc127, E1000_PRC127);
  UPDATE_OR_RESET_STAT (prc255, E1000_PRC255);
  UPDATE_OR_RESET_STAT (prc511, E1000_PRC511);
  UPDATE_OR_RESET_STAT (prc1023, E1000_PRC1023);
  UPDATE_OR_RESET_STAT (prc1522, E1000_PRC1522);

  UPDATE_OR_RESET_STAT (symerrs, E1000_SYMERRS);
  UPDATE_OR_RESET_STAT (mpc, E1000_MPC);
  UPDATE_OR_RESET_STAT (scc, E1000_SCC);
  UPDATE_OR_RESET_STAT (ecol, E1000_ECOL);
  UPDATE_OR_RESET_STAT (mcc, E1000_MCC);
  UPDATE_OR_RESET_STAT (latecol, E1000_LATECOL);
  UPDATE_OR_RESET_STAT (dc, E1000_DC);
  UPDATE_OR_RESET_STAT (sec, E1000_SEC);
  UPDATE_OR_RESET_STAT (rlec, E1000_RLEC);
  UPDATE_OR_RESET_STAT (xonrxc, E1000_XONRXC);
  UPDATE_OR_RESET_STAT (xontxc, E1000_XONTXC);
  UPDATE_OR_RESET_STAT (xoffrxc, E1000_XOFFRXC);
  UPDATE_OR_RESET_STAT (xofftxc, E1000_XOFFTXC);
  UPDATE_OR_RESET_STAT (fcruc, E1000_FCRUC);
  UPDATE_OR_RESET_STAT (gptc, E1000_GPTC);
  UPDATE_OR_RESET_STAT (rnbc, E1000_RNBC);
  UPDATE_OR_RESET_STAT (ruc, E1000_RUC);
  UPDATE_OR_RESET_STAT (rfc, E1000_RFC);
  UPDATE_OR_RESET_STAT (rjc, E1000_RJC);
  UPDATE_OR_RESET_STAT (tpr, E1000_TPR);
  UPDATE_OR_RESET_STAT (ptc64, E1000_PTC64);
  UPDATE_OR_RESET_STAT (ptc127, E1000_PTC127);
  UPDATE_OR_RESET_STAT (ptc255, E1000_PTC255);
  UPDATE_OR_RESET_STAT (ptc511, E1000_PTC511);
  UPDATE_OR_RESET_STAT (ptc1023, E1000_PTC1023);
  UPDATE_OR_RESET_STAT (ptc1522, E1000_PTC1522);
  UPDATE_OR_RESET_STAT (mptc, E1000_MPTC);
  UPDATE_OR_RESET_STAT (bptc, E1000_BPTC);

  //
  // used for adaptive IFS
  //
  hw->mac.tx_packet_delta = E1000_READ_REG (hw, E1000_TPT);
  st->tpt                 = DBaddr ? st->tpt + hw->mac.tx_packet_delta : 0;
  hw->mac.collision_delta = E1000_READ_REG (hw, E1000_COLC);
  st->colc                = DBaddr ? st->colc + hw->mac.collision_delta : 0;

  {
    UPDATE_OR_RESET_STAT (algnerrc, E1000_ALGNERRC);
    UPDATE_OR_RESET_STAT (rxerrc, E1000_RXERRC);
    UPDATE_OR_RESET_STAT (tncrs, E1000_TNCRS);
    UPDATE_OR_RESET_STAT (cexterr, E1000_CEXTERR);
    UPDATE_OR_RESET_STAT (tsctc, E1000_TSCTC);
    UPDATE_OR_RESET_STAT (tsctfc, E1000_TSCTFC);
  }

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

#ifndef INTEL_KDNET // review
#define SET_SUPPORT(S) \
  do { \
    stat = PXE_STATISTICS_##S; \
    DbPtr->Supported |= (UINT64)(1 << stat); \
  } while (0)
#else
#define SET_SUPPORT(S) \
  do { \
    stat = PXE_STATISTICS_##S; \
    DbPtr->Supported |= ((UINT64)1 << stat); \
  } while (0)
#endif
#define UPDATE_EFI_STAT(S, b) \
  do { \
    SET_SUPPORT (S); \
    DbPtr->Data[stat] = st->b; \
  } while (0)

    {
      UPDATE_EFI_STAT (RX_TOTAL_FRAMES, tpr);
    }

  UPDATE_EFI_STAT (RX_GOOD_FRAMES, gprc);
  UPDATE_EFI_STAT (RX_UNDERSIZE_FRAMES, ruc);
  UPDATE_EFI_STAT (RX_OVERSIZE_FRAMES, roc);
  UPDATE_EFI_STAT (RX_DROPPED_FRAMES, rnbc);
  SET_SUPPORT (RX_UNICAST_FRAMES);
  DbPtr->Data[stat] = (st->gprc - st->bprc - st->mprc);
  UPDATE_EFI_STAT (RX_BROADCAST_FRAMES, bprc);
  UPDATE_EFI_STAT (RX_MULTICAST_FRAMES, mprc);
  SET_SUPPORT (RX_CRC_ERROR_FRAMES);
  DbPtr->Data[stat] = (st->crcerrs + st->algnerrc);
  UPDATE_EFI_STAT (TX_TOTAL_FRAMES, tpt);
  UPDATE_EFI_STAT (TX_GOOD_FRAMES, gptc);
  SET_SUPPORT (TX_UNICAST_FRAMES);
  DbPtr->Data[stat] = (st->gptc - st->bptc - st->mptc);
  UPDATE_EFI_STAT (TX_BROADCAST_FRAMES, bptc);
  UPDATE_EFI_STAT (TX_MULTICAST_FRAMES, mptc);
  UPDATE_EFI_STAT (COLLISIONS, colc);

  return PXE_STATCODE_SUCCESS;
};


UINTN
e1000_Transmit (
  GIG_DRIVER_DATA *GigAdapter,
  UINT64          cpb,
  UINT16          opflags
  )
/*++

Routine Description:
  Takes a command block pointer (cpb) and sends the frame.  Takes either one fragment or many
  and places them onto the wire.  Cleanup of the send happens in the function UNDI_Status in DECODE.C

Arguments:
  GigAdapter  - Pointer to the instance data
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
  E1000_TRANSMIT_DESCRIPTOR   *TransmitDescriptor;
  UINT32                      i;
  INT16                       WaitMsec;

/*  DEBUGPRINT(E1000, ("e1000_Transmit\n"));
  DEBUGPRINT(E1000, ("TCTL=%X\r\n", E1000_READ_REG(&GigAdapter->hw, E1000_TCTL)));
  for (i = 0; i < DEFAULT_TX_DESCRIPTORS; i++) {
    DEBUGPRINT(E1000, ("buffer=%X ", GigAdapter->tx_ring[i].buffer_addr));
    DEBUGPRINT(E1000, ("len=%X ", GigAdapter->tx_ring[i].lower.flags.length));
    DEBUGPRINT(E1000, ("cmd=%X ", GigAdapter->tx_ring[i].lower.flags.cmd));
    DEBUGPRINT(E1000, ("status=%X ", GigAdapter->tx_ring[i].upper.fields.status));
    DEBUGPRINT(E1000, ("special=%X\n", GigAdapter->tx_ring[i].upper.fields.special));
  }
*/

  //
  // Transmit buffers must be freed by the upper layer before we can transmit any more.
  //
  if (GigAdapter->TxBufferUnmappedAddr[GigAdapter->cur_tx_ind] != 0) {
    DEBUGPRINT(CRITICAL, ("TX buffers have all been used! cur_tx=%d\n", GigAdapter->cur_tx_ind));
    for (i = 0; i < DEFAULT_TX_DESCRIPTORS; i++) {
      DEBUGPRINT(CRITICAL, ("%x ", GigAdapter->TxBufferUnmappedAddr[i]));
    }
    DEBUGWAIT(CRITICAL);

    //
    // According to UEFI spec we should return PXE_STATCODE_BUFFER_FULL, but SNP is not implemented to recognize this
    // callback.
    //
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
  TransmitDescriptor = &GigAdapter->tx_ring[GigAdapter->cur_tx_ind];

  //
  // Opflags will tell us if this Tx has fragments
  // So far the linear case (the no fragments case, the else on this if) is the majority
  // of all frames sent.
  //
  if (opflags & PXE_OPFLAGS_TRANSMIT_FRAGMENTED) {
    //
    // this count cannot be more than 8;
    //
    DEBUGPRINT(E1000, ("Fragments %x\n", TxFrags->FragCnt));

    //
    // for each fragment, give it a descriptor, being sure to keep track of the number used.
    //
    for (i = 0; i < TxFrags->FragCnt; i++) {
      //
      // Put the size of the fragment in the descriptor
      //
      GigAdapter->TxBufferUnmappedAddr[GigAdapter->cur_tx_ind] = TxFrags->FragDesc[i].FragAddr;
      e1000_MapMem(
        GigAdapter,
        GigAdapter->TxBufferUnmappedAddr[GigAdapter->cur_tx_ind],
        TxFrags->FragDesc[i].FragLen,
#ifndef INTEL_KDNET
        (UINTN*) &TransmitDescriptor->buffer_addr
#else
        &TransmitDescriptor->buffer_addr
#endif
        );

      TransmitDescriptor->lower.flags.length  = (UINT16) TxFrags->FragDesc[i].FragLen;
      TransmitDescriptor->lower.data = (E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS);

      if (GigAdapter->VlanEnable) {
        DEBUGPRINT (VLAN, ("1: Setting VLAN tag = %d\n", GigAdapter->VlanTag));
        TransmitDescriptor->upper.fields.special = GigAdapter->VlanTag;
        TransmitDescriptor->lower.data |= E1000_TXD_CMD_VLE;
      }

      //
      // If this is the last fragment we must also set the EOP bit
      //
      if ((i + 1) == TxFrags->FragCnt) {
        TransmitDescriptor->lower.data |= E1000_TXD_CMD_EOP;
      }
      //
      // move our software counter passed the frame we just used, watching for wrapping
      //
      DEBUGPRINT(E1000, ("Advancing TX pointer %x\n", GigAdapter->cur_tx_ind));
      GigAdapter->cur_tx_ind++;
      if (GigAdapter->cur_tx_ind >= DEFAULT_TX_DESCRIPTORS) {
        GigAdapter->cur_tx_ind = 0;
      }
      TransmitDescriptor = &GigAdapter->tx_ring[GigAdapter->cur_tx_ind];
    }
  } else {
    DEBUGPRINT(E1000, ("No Fragments\n"));

    GigAdapter->TxBufferUnmappedAddr[GigAdapter->cur_tx_ind] = TxBuffer->FrameAddr;
    e1000_MapMem(
      GigAdapter,
      GigAdapter->TxBufferUnmappedAddr[GigAdapter->cur_tx_ind],
      TxBuffer->DataLen + TxBuffer->MediaheaderLen,
#ifndef INTEL_KDNET
      (UINTN*) &TransmitDescriptor->buffer_addr
#else
      &TransmitDescriptor->buffer_addr
#endif
      );

    DEBUGPRINT(E1000, ("Packet buffer at %x\n", TransmitDescriptor->buffer_addr));

    //
    // Set the proper bits to tell the chip that this is the last descriptor in the send,
    // and be sure to tell us when its done.
    // EOP - End of packet
    // IFCs - Insert FCS (Ethernet CRC)
    // RS - Report Status
    //
    TransmitDescriptor->lower.data = (E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS);
    TransmitDescriptor->upper.fields.status = 0;
    if (GigAdapter->VlanEnable) {
      DEBUGPRINT (VLAN, ("2: Setting VLAN tag = %d\n", GigAdapter->VlanTag));
      TransmitDescriptor->upper.fields.special = GigAdapter->VlanTag;
      TransmitDescriptor->lower.data |= E1000_TXD_CMD_VLE;
    }
    TransmitDescriptor->lower.data |= E1000_TXD_CMD_EOP;
    TransmitDescriptor->lower.flags.length  = (UINT16) ((UINT16) TxBuffer->DataLen + TxBuffer->MediaheaderLen);

    DEBUGPRINT(E1000, ("BuffAddr=%x, ", TransmitDescriptor->buffer_addr));
    DEBUGPRINT(E1000, ("Cmd=%x,", TransmitDescriptor->lower.flags.cmd));
    DEBUGPRINT(E1000, ("Cso=%x,", TransmitDescriptor->lower.flags.cso));
    DEBUGPRINT(E1000, ("Len=%x,", TransmitDescriptor->lower.flags.length));
    DEBUGPRINT(E1000, ("Status=%x,", TransmitDescriptor->upper.fields.status));
    DEBUGPRINT(E1000, ("Special=%x,", TransmitDescriptor->upper.fields.special));
    DEBUGPRINT(E1000, ("Css=%x\n", TransmitDescriptor->upper.fields.css));

    //
    // In the zero fragment case, we need to add the header size to the payload size to accurately tell the hw how big is the packet.
    //
    //
    // Move our software counter passed the frame we just used, watching for wrapping
    //
    GigAdapter->cur_tx_ind++;
    if (GigAdapter->cur_tx_ind >= DEFAULT_TX_DESCRIPTORS) {
      GigAdapter->cur_tx_ind = 0;
    }
  }

#if (DBG_LVL&TX)
  DEBUGPRINT(TX, ("Packet length = %d\n", TransmitDescriptor->lower.flags.length));
  DEBUGPRINT(TX, ("Packet data:\n"));
  for (i = 0; i < 32; i++) {
    DEBUGPRINT(TX, ("%x ", ((UINT16 *) ((UINTN) TransmitDescriptor->buffer_addr))[i]));
  }
#endif

  //
  // Turn on the blocking function so we don't get swapped out
  // Then move the Tail pointer so the HW knows to start processing the TX we just setup.
  //
  DEBUGWAIT(E1000);
  e1000_BlockIt (GigAdapter, TRUE);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDT(0), GigAdapter->cur_tx_ind);
  e1000_BlockIt (GigAdapter, FALSE);

  //
  // If the opflags tells us to wait for the packet to hit the wire, we will wait.
  //
  if ((opflags & PXE_OPFLAGS_TRANSMIT_BLOCK) != 0) {
    WaitMsec = 1000;

    while ((TransmitDescriptor->upper.fields.status & E1000_TXD_STAT_DD) == 0) {
      DelayInMilliseconds (10);
      WaitMsec -= 10;
      if (WaitMsec <= 0) {
        break;
      }
    }

    //
    // If we waited for a while, and it didn't finish then the HW must be bad.
    //
    if ((TransmitDescriptor->upper.fields.status & E1000_TXD_STAT_DD) == 0) {
      DEBUGPRINT(CRITICAL, ("ERROR: Network device transmit failure\n"));
      return PXE_STATCODE_DEVICE_FAILURE;
    } else {
      DEBUGPRINT(E1000, ("Transmit success\n"));
    }
  }

  return PXE_STATCODE_SUCCESS;
};

UINTN
e1000_Receive (
  GIG_DRIVER_DATA *GigAdapter,
  UINT64          cpb,
  UINT64          db
  )
/*++

Routine Description:
  Copies the frame from our internal storage ring (As pointed to by GigAdapter->rx_ring) to the command block
  passed in as part of the cpb parameter.  The flow:  Ack the interrupt, setup the pointers, find where the last
  block copied is, check to make sure we have actually received something, and if we have then we do a lot of work.
  The packet is checked for errors, size is adjusted to remove the CRC, adjust the amount to copy if the buffer is smaller
  than the packet, copy the packet to the EFI buffer, and then figure out if the packet was targetted at us, broadcast, multicast
  or if we are all promiscuous.  We then put some of the more interesting information (protocol, src and dest from the packet) into the
  db that is passed to us.  Finally we clean up the frame, set the return value to _SUCCESS, and inc the cur_rx_ind, watching
  for wrapping.  Then with all the loose ends nicely wrapped up, fade to black and return.

Arguments:
  GigAdapter - pointer to the driver data
  cpb - Pointer (Ia-64 friendly) to the command parameter block.  The frame will be placed inside of it.
  db - The data buffer.  The out of band method of passing pre-digested information to the protocol.

Returns:
  PXE_STATCODE, either _NO_DATA if there is no data, or _SUCCESS if we passed the goods to the protocol.

--*/
{
  PXE_CPB_RECEIVE           *CpbReceive;
  PXE_DB_RECEIVE            *DbReceive;
  PXE_FRAME_TYPE            PacketType;
  E1000_RECEIVE_DESCRIPTOR  *ReceiveDescriptor;
  ETHER_HEADER              *EtherHeader;
  PXE_STATCODE              StatCode;
  UINT16                    i;
  UINT16                    TempLen;
  UINT8                     *PacketPtr;
#ifdef INTEL_KDNET
  UINT8                     *ReceiveBuffer;
#endif
#if (DBG_LVL&CRITICAL)
#if (DBG_LVL&RX)
  UINT32                    Rdh;
  UINT32                    Rdt;
#endif
#endif


  PacketType  = PXE_FRAME_TYPE_NONE;
  StatCode    = PXE_STATCODE_NO_DATA;
  i           = 0;

  //
  // acknowledge the interrupts
  //
  E1000_READ_REG (&GigAdapter->hw, E1000_ICR);

/*
  DEBUGPRINT(E1000, ("e1000_Receive\n"));
  DEBUGPRINT(E1000, ("RCTL=%X ", E1000_READ_REG(&GigAdapter->hw, E1000_RCTL)));
  DEBUGPRINT(E1000, ("RDH0=%x ", (UINT16) E1000_READ_REG (&GigAdapter->hw, E1000_RDH(0))));
  DEBUGPRINT(E1000, ("RDT0=%x\n", (UINT16) E1000_READ_REG (&GigAdapter->hw, E1000_RDT(0))));
  DEBUGPRINT(E1000, ("RDBAL=%x desc=%X\n",  E1000_READ_REG (&GigAdapter->hw, E1000_RDBAL),
    (UINTN) GigAdapter->rx_ring));
  rar_low = E1000_READ_REG_ARRAY(&GigAdapter->hw, E1000_RA, 0);
  rar_high = E1000_READ_REG_ARRAY(&GigAdapter->hw, E1000_RA, 1);
  DEBUGPRINT(E1000, ("Receive Addr = %X %X\n", rar_high, rar_low));

  for (i = 0; i < DEFAULT_RX_DESCRIPTORS; i++) {
    DEBUGPRINT(E1000, ("buffer=%X ", GigAdapter->rx_ring[i].buffer_addr));
    DEBUGPRINT(E1000, ("csum=%X ", GigAdapter->rx_ring[i].csum));
    DEBUGPRINT(E1000, ("special=%X ", GigAdapter->rx_ring[i].special));
    DEBUGPRINT(E1000, ("status=%X ", GigAdapter->rx_ring[i].status));
    DEBUGPRINT(E1000, ("len=%X ", GigAdapter->rx_ring[i].length));
    DEBUGPRINT(E1000, ("err=%X\n", GigAdapter->rx_ring[i].errors));
  }
*/

  //
  // Make quick copies of the buffer pointers so we can use them without fear of corrupting the originals
  //
  CpbReceive  = (PXE_CPB_RECEIVE *) (UINTN) cpb;
  DbReceive   = (PXE_DB_RECEIVE *) (UINTN) db;

  //
  // Get a pointer to the buffer that should have a rx in it, IF one is really there.
  //
  ReceiveDescriptor = &GigAdapter->rx_ring[GigAdapter->cur_rx_ind];
#ifdef INTEL_KDNET
  ReceiveBuffer = &GigAdapter->local_rx_buffer[GigAdapter->cur_rx_ind].RxBuffer[0];
#endif

#if (DBG_LVL&CRITICAL)
#if (DBG_LVL&RX)
  if (ReceiveDescriptor->buffer_addr != GigAdapter->DebugRxBuffer[GigAdapter->cur_rx_ind]) {
    DEBUGPRINT(CRITICAL, ("GetStatus ERROR: Rx buff mismatch on desc %d: expected %X, actual %X\n",
      GigAdapter->cur_rx_ind,
      GigAdapter->DebugRxBuffer[GigAdapter->cur_rx_ind],
      ReceiveDescriptor->buffer_addr
      ));
  }

  Rdt = E1000_READ_REG (&GigAdapter->hw, E1000_RDT(0));
  Rdh = E1000_READ_REG (&GigAdapter->hw, E1000_RDH(0));
  if (Rdt == Rdh) {
    DEBUGPRINT(CRITICAL, ("Receive ERROR: RX Buffers Full!\n"));
  }
#endif
#endif

  if ((ReceiveDescriptor->status & (E1000_RXD_STAT_EOP | E1000_RXD_STAT_DD)) != 0) {
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
      e1000_MemCopy ((INT8 *) (UINTN) CpbReceive->BufferAddr, (INT8 *) (UINTN) ReceiveDescriptor->buffer_addr, TempLen);
#else
      // Always use the virtual address for the copy.
      e1000_MemCopy ((INT8 *) (UINTN) CpbReceive->BufferAddr, ReceiveBuffer, TempLen);
#endif

      PacketPtr = (UINT8 *) (UINTN) CpbReceive->BufferAddr;

#if (DBG_LVL & RX)
      DEBUGPRINT(RX, ("Packet Data \n"));
      for (i = 0; i < TempLen; i++) {
        DEBUGPRINT(RX, ("%x ", PacketPtr[i]));
      }
      DEBUGPRINT(RX, ("\n"));
#endif
      //
      // Fill the DB with needed information
      //
      DbReceive->FrameLen       = ReceiveDescriptor->length;  // includes header
      DbReceive->MediaHeaderLen = PXE_MAC_HEADER_LEN_ETHER;

#ifndef INTEL_KDNET
      EtherHeader = (ETHER_HEADER *) (UINTN) ReceiveDescriptor->buffer_addr;
#else
      EtherHeader = (ETHER_HEADER *)ReceiveBuffer;
#endif

      //
      // Figure out if the packet was meant for us, was a broadcast, multicast or we
      // recieved a frame in promiscuous mode.
      //
      if (E1000_COMPARE_MAC(EtherHeader->dest_addr, GigAdapter->hw.mac.addr) == 0) {
        PacketType = PXE_FRAME_TYPE_UNICAST;
        DEBUGPRINT(E1000, ("unicast packet for us.\n"));
      } else
      if (E1000_COMPARE_MAC(EtherHeader->dest_addr, GigAdapter->BroadcastNodeAddress) == 0) {
        PacketType = PXE_FRAME_TYPE_BROADCAST;
        DEBUGPRINT(E1000, ("broadcast packet.\n"));
      } else {
        //
        // That leaves multicast or we must be in promiscuous mode.   Check for the Mcast bit in the address.
        // otherwise its a promiscuous receive.
        //
        if ((EtherHeader->dest_addr[0] & 1) == 1) {
          PacketType = PXE_FRAME_TYPE_MULTICAST;
          DEBUGPRINT(E1000, ("multicast packet.\n"));
        } else {
          PacketType = PXE_FRAME_TYPE_PROMISCUOUS;
          DEBUGPRINT(E1000, ("unicast promiscuous.\n"));
        }
      }
      DbReceive->Type = PacketType;
      DEBUGPRINT(E1000, ("PacketType %x\n", PacketType));

      //
      // Put the protocol (UDP, TCP/IP) in the data buffer.
      //
      DbReceive->Protocol = EtherHeader->type;
      DEBUGPRINT(E1000, ("protocol %x\n", EtherHeader->type));

      E1000_COPY_MAC(DbReceive->SrcAddr, EtherHeader->src_addr);
      E1000_COPY_MAC(DbReceive->DestAddr, EtherHeader->dest_addr);

      StatCode = PXE_STATCODE_SUCCESS;
    } else {
      DEBUGPRINT(CRITICAL, ("ERROR: Received zero sized packet or receive error!\n"));
    }
    //
    // Clean up the packet
    //
    ReceiveDescriptor->status = 0;
    ReceiveDescriptor->length = 0;

    //
    // Move the current cleaned buffer pointer, being careful to wrap it as needed.  Then update the hardware,
    // so it knows that an additional buffer can be used.
    //
    E1000_WRITE_REG (&GigAdapter->hw, E1000_RDT(0), GigAdapter->cur_rx_ind);
    GigAdapter->cur_rx_ind++;
    if (GigAdapter->cur_rx_ind == DEFAULT_RX_DESCRIPTORS) {
      GigAdapter->cur_rx_ind = 0;
    }
  }

  return StatCode;
};

UINTN
e1000_SetInterruptState (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Allows the protocol to control our interrupt behaviour.

Arguments:
  GigAdapter  - Pointer to the driver structure

Returns:
  PXE_STATCODE_SUCCESS

--*/
{
  UINT32  SetIntMask;

  SetIntMask = 0;

  DEBUGPRINT(E1000, ("e1000_SetInterruptState\n"));

  //
  // Start with no Interrupts.
  //
  E1000_WRITE_REG (&GigAdapter->hw, E1000_IMC, 0xFFFFFFFF);

  //
  // Mask the RX interrupts
  //
  if (GigAdapter->int_mask & PXE_OPFLAGS_INTERRUPT_RECEIVE) {
    SetIntMask = (E1000_ICR_RXT0 | E1000_ICR_RXSEQ | E1000_ICR_RXDMT0 | E1000_ICR_RXO | E1000_ICR_RXCFG | SetIntMask);
    DEBUGPRINT(E1000, ("Mask the RX interrupts\n"));
  }

  //
  // Mask the TX interrupts
  //
  if (GigAdapter->int_mask & PXE_OPFLAGS_INTERRUPT_TRANSMIT) {
    SetIntMask = (E1000_ICR_TXDW | E1000_ICR_TXQE | SetIntMask);
    DEBUGPRINT(E1000, ("Mask the TX interrupts\n"));
  }

  //
  // Mask the CMD interrupts
  //
  if (GigAdapter->int_mask & PXE_OPFLAGS_INTERRUPT_COMMAND) {
    SetIntMask =
      (
        E1000_ICR_GPI_EN0 |
        E1000_ICR_GPI_EN1 |
        E1000_ICR_GPI_EN2 |
        E1000_ICR_GPI_EN3 |
        E1000_ICR_LSC |
        SetIntMask
      );
    DEBUGPRINT(E1000, ("Mask the CMD interrupts\n"));
  }

  //
  // Now we have all the Ints we want, so let the hardware know.
  //
  E1000_WRITE_REG (&GigAdapter->hw, E1000_IMS, SetIntMask);

  return PXE_STATCODE_SUCCESS;
};

UINTN
e1000_Shutdown (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Stop the hardware and put it all (including the PHY) into a known good state.

Arguments:
  GigAdapter  - Pointer to the driver structure

Returns:
  PXE_STATCODE_SUCCESS

--*/
{
  UINT32 Reg;

  DEBUGPRINT(E1000, ("e1000_Shutdown - adapter stop\n"));

  //
  // Disable the transmit and receive DMA
  //
  e1000_ReceiveDisable (GigAdapter);
#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER
  e1000_TransmitDisable (GigAdapter);
#endif

  Reg = E1000_READ_REG (&GigAdapter->hw, E1000_TCTL);
  Reg = (Reg & ~E1000_TCTL_EN);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TCTL, Reg);

  //
  // Disable the receive unit so the hardware does not continue to DMA packets to memory.
  // Also release the software semaphore.
  //
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, 0);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_SWSM, 0);
  e1000_PciFlush(&GigAdapter->hw);

  //
  // This delay is to ensure in flight DMA and receive descriptor flush
  // have time to complete.
  //
  DelayInMilliseconds (10);

  GigAdapter->ReceiveStarted = FALSE;
  GigAdapter->Rx_Filter = 0;

  return PXE_STATCODE_SUCCESS;
};

UINTN
e1000_Reset (
  GIG_DRIVER_DATA *GigAdapter,
  UINT16          OpFlags
  )
/*++

Routine Description:
  Resets the hardware and put it all (including the PHY) into a known good state.

Arguments:
  GigAdapter    - The pointer to our context data
  OpFlags           - The information on what else we need to do.

Returns:
  PXE_STATCODE_SUCCESS

--*/
{

  UINT32            TempReg;

  DEBUGPRINT(E1000, ("e1000_Reset\n"));

  TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_STATUS);

  //
  // Spanning Tree Workaround:
  // If link is up and valid then we will not do a PHY reset. This ensures
  // we do not need to drop link and spanning tree does not need to restart.
  // If link is up and we see Gigabit Half-Duplex then we know link is invalid
  // and we need to do a PHY reset.
  //
  GigAdapter->hw.phy.reset_disable = TRUE;

  if ((TempReg & E1000_STATUS_LU) != 0) {
    if (((TempReg & E1000_STATUS_FD) == 0) && ((TempReg & E1000_STATUS_SPEED_MASK) == E1000_STATUS_SPEED_1000)) {
      DEBUGPRINT(E1000, ("BAD LINK - 1Gig/Half - Enabling PHY reset\n"));
      GigAdapter->hw.phy.reset_disable = FALSE;
      //
      // Since link is in a bad state we also need to make sure that we do a full reset down below
      //
      GigAdapter->HwInitialized = FALSE;
    }
  }

  //
  // Put the E1000 into a known state by resetting the transmit
  // and receive units of the E1000 and masking/clearing all
  // interrupts.
  // If the hardware has already been started then don't bother with a reset
  // We want to make sure we do not have to restart autonegotiation and two-pair
  // downshift.
  //
  if (GigAdapter->HwInitialized == FALSE) {
    e1000_reset_hw (&GigAdapter->hw);

    //
    // Now that the structures are in place, we can configure the hardware to use it all.
    //
    if (e1000_init_hw (&GigAdapter->hw) == 0) {
      DEBUGPRINT(E1000, ("e1000_init_hw success\n"));
    } else {
      DEBUGPRINT(CRITICAL, ("Hardware Init failed\n"));
      return PXE_STATCODE_NOT_STARTED;
    }
  }
  else
  {
    DEBUGPRINT(E1000, ("Skipping adapter reset\n"));
  }


  if ((OpFlags & PXE_OPFLAGS_RESET_DISABLE_FILTERS) == 0) {
    UINT16  SaveFilter;

    SaveFilter = GigAdapter->Rx_Filter;

    //
    // if we give the filter same as Rx_Filter, this routine will not set mcast list
    // (it thinks there is no change)
    // to force it, we will reset that flag in the Rx_Filter
    //
    GigAdapter->Rx_Filter &= (~PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST);
    e1000_SetFilter (GigAdapter, SaveFilter, (UINT64) 0, (UINT32) 0);
  }

  if (OpFlags & PXE_OPFLAGS_RESET_DISABLE_INTERRUPTS) {
    GigAdapter->int_mask = 0; // disable the interrupts
  }

  e1000_SetInterruptState (GigAdapter);

  return PXE_STATCODE_SUCCESS;
}

VOID
e1000_LanFunction (
    GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:

  PCIe function to LAN port mapping.

Arguments:

  GigAdapter - Pointer to adapter structure

Returns:

  VOID

--*/
{
  GigAdapter->LanFunction = (E1000_READ_REG(&GigAdapter->hw, E1000_STATUS) & E1000_STATUS_LAN_ID_MASK) >> E1000_STATUS_LAN_ID_OFFSET;
  DEBUGPRINT (INIT, ("PCI function %d is LAN port %d \n", GigAdapter->Function, GigAdapter->LanFunction));
  DEBUGWAIT (INIT);
}

VOID
e1000_SetSpeedDuplex(
    GIG_DRIVER_DATA *GigAdapter
    )
/*++

Routine Description:
  GigAdapter - Sets the force speed and duplex settings for the adapter based on
                   EEPROM settings and data set by the SNP

Arguments:
  GigAdapter - Pointer to adapter structure

Returns:
  VOID

--*/
{
  UINT16        SetupOffset;
  UINT16        ConfigOffset;
  UINT16        SetupWord;
  UINT16        CustomConfigWord;

  DEBUGPRINT(E1000, ("e1000_SetSpeedDuplex\n"));

  //
  // Copy forced speed and duplex settings to shared code structure
  //
  if ((GigAdapter->LinkSpeed == 10) && (GigAdapter->DuplexMode == PXE_FORCE_HALF_DUPLEX)) {
    DEBUGPRINT(E1000, ("Force 10-Half\n"));
    GigAdapter->hw.phy.reset_disable    = FALSE;
    GigAdapter->hw.mac.autoneg              = 0;
    GigAdapter->hw.mac.forced_speed_duplex  = ADVERTISE_10_HALF;
    GigAdapter->HwInitialized = FALSE;
  }

  if ((GigAdapter->LinkSpeed == 100) && (GigAdapter->DuplexMode == PXE_FORCE_HALF_DUPLEX)) {
    DEBUGPRINT(E1000, ("Force 100-Half\n"));
    GigAdapter->hw.phy.reset_disable    = FALSE;
    GigAdapter->hw.mac.autoneg              = 0;
    GigAdapter->hw.mac.forced_speed_duplex  = ADVERTISE_100_HALF;
    GigAdapter->HwInitialized = FALSE;
  }

  if ((GigAdapter->LinkSpeed == 10) && (GigAdapter->DuplexMode == PXE_FORCE_FULL_DUPLEX)) {
    DEBUGPRINT(E1000, ("Force 10-Full\n"));
    GigAdapter->hw.phy.reset_disable    = FALSE;
    GigAdapter->hw.mac.autoneg              = 0;
    GigAdapter->hw.mac.forced_speed_duplex  = ADVERTISE_10_FULL;
    GigAdapter->HwInitialized = FALSE;
  }

  if ((GigAdapter->LinkSpeed == 100) && (GigAdapter->DuplexMode == PXE_FORCE_FULL_DUPLEX)) {
    DEBUGPRINT(E1000, ("Force 100-Full\n"));
    GigAdapter->hw.phy.reset_disable    = FALSE;
    GigAdapter->hw.mac.autoneg              = 0;
    GigAdapter->hw.mac.forced_speed_duplex  = ADVERTISE_100_FULL;
    GigAdapter->HwInitialized = FALSE;
  }

  //
  // Check for forced speed and duplex
  // The EEPROM settings will override settings passed by the SNP
  //

  // Configure offsets depending on function number
  //
  switch(GigAdapter->Function) {
	case 0:
	  ConfigOffset = CONFIG_CUSTOM_WORD;
	  SetupOffset  = SETUP_OPTIONS_WORD;
	  break;
	case 1:
	  ConfigOffset = CONFIG_CUSTOM_WORD_LANB;
	  SetupOffset  = SETUP_OPTIONS_WORD_LANB;
	  break;
	case 2:
	  ConfigOffset = CONFIG_CUSTOM_WORD_LANC;
	  SetupOffset  = SETUP_OPTIONS_WORD_LANC;
	  break;
	case 3:
	  ConfigOffset = CONFIG_CUSTOM_WORD_LAND;
	  SetupOffset  = SETUP_OPTIONS_WORD_LAND;
	  break;
	default:
	  ConfigOffset = CONFIG_CUSTOM_WORD;
	  SetupOffset  = SETUP_OPTIONS_WORD;
  }

  e1000_read_nvm(&GigAdapter->hw, SetupOffset, 1, &SetupWord);
  e1000_read_nvm(&GigAdapter->hw, ConfigOffset, 1, &CustomConfigWord);

  if ((CustomConfigWord & SIG_MASK) == SIG) {
    switch (SetupWord & (FSP_MASK | FDP_FULL_DUPLEX_BIT))
    {
      case (FDP_FULL_DUPLEX_BIT | FSP_100MBS):
        DEBUGPRINT(E1000, ("Forcing 100 Full from EEPROM\n"));
        GigAdapter->hw.phy.reset_disable = FALSE;
        GigAdapter->hw.mac.autoneg = 0;
        GigAdapter->hw.mac.forced_speed_duplex = ADVERTISE_100_FULL;
        GigAdapter->HwInitialized = FALSE;
        break;
      case (FDP_FULL_DUPLEX_BIT | FSP_10MBS):
        DEBUGPRINT(E1000, ("Forcing 10 Full from EEPROM\n"));
        GigAdapter->hw.phy.reset_disable = FALSE;
        GigAdapter->hw.mac.autoneg = 0;
        GigAdapter->hw.mac.forced_speed_duplex = ADVERTISE_10_FULL;
        GigAdapter->HwInitialized = FALSE;
        break;
      case (FSP_100MBS):
        DEBUGPRINT(E1000, ("Forcing 100 Half from EEPROM\n"));
        GigAdapter->hw.phy.reset_disable = FALSE;
        GigAdapter->hw.mac.autoneg = 0;
        GigAdapter->hw.mac.forced_speed_duplex = ADVERTISE_100_HALF;
        GigAdapter->HwInitialized = FALSE;
        break;
      case (FSP_10MBS):
        DEBUGPRINT(E1000, ("Forcing 10 Half from EEPROM\n"));
        GigAdapter->hw.phy.reset_disable = FALSE;
        GigAdapter->hw.mac.autoneg = 0;
        GigAdapter->hw.mac.forced_speed_duplex = ADVERTISE_10_HALF;
        GigAdapter->HwInitialized = FALSE;
        break;
      default:
        GigAdapter->hw.mac.autoneg = 1;
        break;
    }
  }
}


#define PCI_CLASS_MASK          0xFF00
#define PCI_SUBCLASS_MASK       0x00FF
EFI_STATUS
e1000_FirstTimeInit (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This function is called as early as possible during driver start to ensure the
  hardware has enough time to autonegotiate when the real SNP device initialize call
  is made.

Arguments:
  GigAdapter - Pointer to adapter structure

Returns:
  EFI_STATUS

--*/
{
  PCI_CONFIG_HEADER *PciConfigHeader;
  UINT32            *TempBar;
  UINT8             BarIndex;
  EFI_STATUS        Status;
  UINT32            ScStatus;
  UINT32            Reg;
  UINT16            i;

  DEBUGPRINT(E1000, ("e1000_FirstTimeInit\n"));

  GigAdapter->DriverBusy = FALSE;

  //
  // Read all the registers from the device's PCI Configuration space
  //
#ifndef INTEL_KDNET
  GigAdapter->PciIo->Pci.Read (
    GigAdapter->PciIo,
    EfiPciIoWidthUint32,
    0,
    MAX_PCI_CONFIG_LEN,
    GigAdapter->PciConfig
    );
#else

    GetDevicePciDataByOffset(&GigAdapter->PciConfig,
                             0,
                             MAX_PCI_CONFIG_LEN * sizeof(UINT32));

#endif

  PciConfigHeader = (PCI_CONFIG_HEADER *) GigAdapter->PciConfig;

  //
  // Enumerate through the PCI BARs for the device to determine which one is
  // the IO BAR.  Save the index of the BAR into the adapter info structure.
  //
  TempBar = &PciConfigHeader->BaseAddressReg_0;
  for (BarIndex = 0; BarIndex <= 5; BarIndex++) {
    DEBUGPRINT(E1000, ("BAR = %X\n", *TempBar));
    if ((*TempBar & PCI_BAR_MEM_MASK) == PCI_BAR_MEM_64BIT) {
      //
      // This is a 64-bit memory bar, skip this and the
      // next bar as well.
      //
      TempBar++;
    }

    //
    // Find the IO BAR and save it's number into IoBar
    //
    if ((*TempBar & PCI_BAR_IO_MASK) == PCI_BAR_IO_MODE) {
      //
      // Here is the IO Bar - save it to the Gigabit adapter struct.
      //
      GigAdapter->IoBarIndex = BarIndex;
      break;
    }

    //
    // Advance the pointer to the next bar in PCI config space
    //
    TempBar++;
  }

#ifndef INTEL_KDNET
  GigAdapter->PciIo->GetLocation (
    GigAdapter->PciIo,
    &GigAdapter->Segment,
    &GigAdapter->Bus,
    &GigAdapter->Device,
    &GigAdapter->Function
    );
#else
  GetDeviceLocation(&GigAdapter->Segment,
                    &GigAdapter->Bus,
                    &GigAdapter->Device,
                    &GigAdapter->Function);
#endif

  DEBUGPRINT(INIT, ("GigAdapter->IoBarIndex = %X\n", GigAdapter->IoBarIndex));
  DEBUGPRINT(INIT, ("PCI Command Register = %X\n", PciConfigHeader->Command));
  DEBUGPRINT(INIT, ("PCI Status Register = %X\n", PciConfigHeader->Status));
  DEBUGPRINT(INIT, ("PCI VendorID = %X\n", PciConfigHeader->VendorID));
  DEBUGPRINT(INIT, ("PCI DeviceID = %X\n", PciConfigHeader->DeviceID));
  DEBUGPRINT(INIT, ("PCI SubVendorID = %X\n", PciConfigHeader->SubVendorID));
  DEBUGPRINT(INIT, ("PCI SubSystemID = %X\n", PciConfigHeader->SubSystemID));
  DEBUGPRINT(INIT, ("PCI Segment = %X\n", GigAdapter->Segment));
  DEBUGPRINT(INIT, ("PCI Bus = %X\n", GigAdapter->Bus));
  DEBUGPRINT(INIT, ("PCI Device = %X\n", GigAdapter->Device));
  DEBUGPRINT(INIT, ("PCI Function = %X\n", GigAdapter->Function));

  ZeroMem (GigAdapter->BroadcastNodeAddress, PXE_MAC_LENGTH);
  SetMem (GigAdapter->BroadcastNodeAddress, PXE_HWADDR_LEN_ETHER, 0xFF);

  //
  // Initialize all parameters needed for the shared code
  //
  GigAdapter->hw.hw_addr                = (UINT8*) (UINTN) PciConfigHeader->BaseAddressReg_0;
  GigAdapter->hw.back                   = GigAdapter;
  GigAdapter->hw.vendor_id              = PciConfigHeader->VendorID;
  GigAdapter->hw.device_id              = PciConfigHeader->DeviceID;
  GigAdapter->hw.revision_id            = (UINT8) PciConfigHeader->RevID;
  GigAdapter->hw.subsystem_vendor_id    = PciConfigHeader->SubVendorID;
  GigAdapter->hw.subsystem_device_id    = PciConfigHeader->SubSystemID;
  GigAdapter->hw.revision_id            = (UINT8) PciConfigHeader->RevID;

  GigAdapter->hw.mac.autoneg            = TRUE;
  GigAdapter->hw.fc.current_mode        = e1000_fc_full;
  GigAdapter->hw.fc.requested_mode      = e1000_fc_full;

  GigAdapter->hw.phy.autoneg_wait_to_complete = FALSE;
  GigAdapter->hw.phy.reset_disable      = FALSE;
  GigAdapter->hw.phy.autoneg_advertised = E1000_ALL_SPEED_DUPLEX;
  GigAdapter->hw.phy.autoneg_mask       = AUTONEG_ADVERTISE_SPEED_DEFAULT;


  GigAdapter->PciClass    = (UINT8)((PciConfigHeader->ClassID & PCI_CLASS_MASK) >> 8);
  GigAdapter->PciSubClass = (UINT8)(PciConfigHeader->ClassID) & PCI_SUBCLASS_MASK;
  //
  // We need to set the IO bar to zero for the shared code because the EFI PCI protocol
  // gets the BAR for us.
  //
  GigAdapter->hw.io_base               = 0;

  //
  //
  //  This variable is set only to make the flash shared code work on ICH8.
  //  Set to 1 because the flash BAR will always be BAR 1.
  //
  GigAdapter->hw.flash_address         = (UINT8*) ((UINTN)1);

  if (e1000_set_mac_type (&GigAdapter->hw) != E1000_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("Unsupported MAC type!\n"));
    return EFI_UNSUPPORTED;
  }

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifdef INTEL_KDNET
  if (IsClient1GDevice(GigAdapter))
#endif
  {
  if (GigAdapter->hw.mac.type >= e1000_pch_spt)
  {
#ifndef E1000_CTRL_EXT_DPG_EN
#define E1000_CTRL_EXT_DPG_EN (1 << 3)
#endif // E1000_CTRL_EXT_DPG_EN
    Reg = E1000_READ_REG(&GigAdapter->hw, E1000_CTRL_EXT);
    Reg &= ~E1000_CTRL_EXT_DPG_EN;
    E1000_WRITE_REG(&GigAdapter->hw, E1000_CTRL_EXT, Reg);
  }
  }
#endif // INTEL_KDNET_1G_CLIENT_MERGE_MARKER

  if (e1000_setup_init_funcs (&GigAdapter->hw, TRUE) != E1000_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("e1000_setup_init_funcs failed!\n"));
    return EFI_UNSUPPORTED;
  }

  e1000_LanFunction (GigAdapter);


  DEBUGPRINT(E1000, ("Calling e1000_get_bus_info\n"));
  if (e1000_get_bus_info (&GigAdapter->hw) != E1000_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("Could not read bus information\n"));
    return EFI_UNSUPPORTED;
  }

  DEBUGPRINT(E1000, ("Calling e1000_read_mac_addr\n"));
  if (e1000_read_mac_addr (&GigAdapter->hw) != E1000_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("Could not read MAC address\n"));
    return EFI_UNSUPPORTED;
  }

  DEBUGPRINT(INIT, ("MAC Address: "));
  for (i = 0; i < 6; i++) {
    DEBUGPRINT(INIT, ("%2x ", GigAdapter->hw.mac.perm_addr[i]));
  }
  DEBUGPRINT(INIT, ("\n"));
  
#ifndef INTEL_KDNET

  Reg = E1000_READ_REG(&GigAdapter->hw, E1000_CTRL_EXT);
  if ((Reg & E1000_CTRL_EXT_DRV_LOAD) != 0) {
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

#endif


  ScStatus = e1000_reset_hw (&GigAdapter->hw);
  if (ScStatus != E1000_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("e1000_reset_hw returns %d\n", ScStatus));
    return EFI_DEVICE_ERROR;
  }

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifdef INTEL_KDNET
  if (IsClient1GDevice(GigAdapter))
#endif
  {
	if (GigAdapter->hw.mac.type == e1000_pch2lan ||
		GigAdapter->hw.mac.type == e1000_pch_lpt ||
		GigAdapter->hw.mac.type == e1000_pch_spt ||
		GigAdapter->hw.mac.type == e1000_pch_cnp)
	{
		UINT16 phy_reg;
		UINT32 phy_addr_orig = GigAdapter->hw.phy.addr;

		GigAdapter->hw.phy.addr = 1 << E1000_MDIC_PHY_SHIFT;
		
		_ReadPhyRegister16Ex(&GigAdapter->hw, 772, 20, &phy_reg);
		phy_reg &= ~(1 << 12);
		_WritePhyRegister16Ex(&GigAdapter->hw, 772, 20, phy_reg);

		GigAdapter->hw.phy.addr = phy_addr_orig;

		if (GigAdapter->hw.mac.type != e1000_pch2lan)
		{
			GigAdapter->hw.phy.ops.read_reg_locked(&GigAdapter->hw, I217_PROXY_CTRL, &phy_reg);
			phy_reg &= ~0xF;
			GigAdapter->hw.phy.ops.write_reg_locked(&GigAdapter->hw, I217_PROXY_CTRL, phy_reg);
		}
	}
  }
#endif // INTEL_KDNET_1G_CLIENT_MERGE_MARKER

  //
  // Now that the structures are in place, we can configure the hardware to use it all.
  //
  ScStatus = e1000_init_hw (&GigAdapter->hw);
  if (ScStatus == E1000_SUCCESS) {
    DEBUGPRINT(E1000, ("e1000_init_hw success\n"));
    Status = EFI_SUCCESS;
#ifdef INTEL_KDNET
    GigAdapter->Server1GDevice = ScanDeviceIdsForServer(GigAdapter);
#endif
    GigAdapter->HwInitialized = TRUE;
  } else {
    DEBUGPRINT(CRITICAL, ("Hardware Init failed status=%x\n", ScStatus));
    GigAdapter->HwInitialized = FALSE;
    Status = EFI_DEVICE_ERROR;
  }

  //
  // According to EAS the transmit and receive head and tail can only be written by
  // software after a hardware reset and before enabling transmit and receive units.
  //
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RDH(0), 0);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDH(0), 0);
  GigAdapter->cur_tx_ind = 0;
  GigAdapter->xmit_done_head = 0;
  GigAdapter->cur_rx_ind = 0;

#ifndef NO_82571_SUPPORT
  //
  // On 82571 based adapters if either port is reset then the MAC address will be loaded into the EEPROM
  // If the user overrides the default MAC address using the StnAddr command then the 82571 will reset the MAC address
  // the next time either port is reset.  This check resets the MAC address to the default value specified by the user.
  //
  if (GigAdapter->hw.mac.type == e1000_82571 && GigAdapter->MacAddrOverride) {
    DEBUGPRINT(E1000, ("RESETING STATION ADDRESS\n"));
    e1000_rar_set (&GigAdapter->hw, GigAdapter->hw.mac.addr, 0);
  }
#endif

  Reg = E1000_READ_REG(&GigAdapter->hw, E1000_CTRL_EXT);
  Reg |= E1000_CTRL_EXT_DRV_LOAD;
  E1000_WRITE_REG(&GigAdapter->hw, E1000_CTRL_EXT, Reg);

  return Status;
};


PXE_STATCODE
e1000_Initialize (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Initializes the gigabit adapter, setting up memory addresses, MAC Addresses,
  Type of card, etc.

Arguments:
  GigAdapter - Pointer to adapter structure

Returns:
  PXE_STATCODE

--*/
{
  UINT32            *TempBar;
  PXE_STATCODE      PxeStatcode;

  DEBUGPRINT(E1000, ("e1000_Initialize\n"));

  PxeStatcode = PXE_STATCODE_SUCCESS;
  TempBar = NULL;

  ZeroMem ((VOID *) ((UINTN) GigAdapter->MemoryPtr), MEMORY_NEEDED);


  DEBUGWAIT (E1000);

  e1000_SetSpeedDuplex(GigAdapter);

  //
  // If the hardware has already been initialized then don't bother with a reset
  // We want to make sure we do not have to restart autonegotiation and two-pair
  // downshift.
  //
  if (GigAdapter->HwInitialized == FALSE) {
    DEBUGPRINT(E1000, ("Initializing hardware!\n"));

    if (e1000_init_hw (&GigAdapter->hw) == 0) {
      DEBUGPRINT(E1000, ("e1000_init_hw success\n"));
      PxeStatcode = PXE_STATCODE_SUCCESS;
      GigAdapter->HwInitialized      = TRUE;
    } else {
      DEBUGPRINT(CRITICAL, ("Hardware Init failed\n"));
      PxeStatcode = PXE_STATCODE_NOT_STARTED;
    }
  }
  else
  {
    DEBUGPRINT(E1000, ("Skipping adapter reset\n"));
    PxeStatcode = PXE_STATCODE_SUCCESS;
  }

  if (PxeStatcode == PXE_STATCODE_SUCCESS) {
    e1000_TxRxConfigure (GigAdapter);
  }

  //
  // Re-read the MAC address.  The CLP configured MAC address is being reset by
  // hardware to the factory address after init, so we need to reset it here.
  //
  if (e1000_read_mac_addr (&GigAdapter->hw) != E1000_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("Could not read MAC address.\n"));
  }

  DEBUGWAIT (E1000);

  return PxeStatcode;
};

VOID
e1000_TxRxConfigure (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Initializes the transmit and receive resources for the adapter.

Arguments:
  GigAdapter - Pointer to adapter structure

Returns:
  VOID

--*/
{
  UINT32  TempReg;
  UINT64  MemAddr;
  UINT32  *MemPtr;
  UINT16  i;

  DEBUGPRINT(E1000, ("e1000_TxRxConfigure\n"));

  e1000_ReceiveDisable(GigAdapter);

  //
  // Setup the receive ring
  //
  GigAdapter->rx_ring = (E1000_RECEIVE_DESCRIPTOR *) (UINTN)
    ( (GigAdapter->MemoryPtr + BYTE_ALIGN_64) & 0xFFFFFFFFFFFFFF80 );

  //
  // Setup TX ring
  //
  GigAdapter->tx_ring = (E1000_TRANSMIT_DESCRIPTOR *) ((UINT8 *) GigAdapter->rx_ring + (sizeof (E1000_RECEIVE_DESCRIPTOR) * DEFAULT_RX_DESCRIPTORS));
  DEBUGPRINT(E1000, (
    "Rx Ring %x Tx Ring %X  RX size %X \n",
    GigAdapter->rx_ring,
    GigAdapter->tx_ring,
    (sizeof (E1000_RECEIVE_DESCRIPTOR) * DEFAULT_RX_DESCRIPTORS)
    ));

  ZeroMem ((VOID *) GigAdapter->TxBufferUnmappedAddr, DEFAULT_TX_DESCRIPTORS * sizeof(UINT64));

  //
  // Since we already have the size of the TX Ring, use it to setup the local receive buffers
  //
  GigAdapter->local_rx_buffer = (LOCAL_RX_BUFFER *) ((UINT8 *) GigAdapter->tx_ring + (sizeof (E1000_TRANSMIT_DESCRIPTOR) * DEFAULT_TX_DESCRIPTORS));

#ifdef INTEL_KDNET
  //
  // Align the receive buffers to a 2048 byte boundary so that none of them
  // cross a page boundary.  This ensures that each one will require only one
  // descriptor.
  //
  GigAdapter->local_rx_buffer = (LOCAL_RX_BUFFER *)(UINTN)((((UINT64)GigAdapter->local_rx_buffer) + BYTE_ALIGN_2048) & ~(UINT64)BYTE_ALIGN_2048);
#endif
  DEBUGPRINT(E1000, (
    "Tx Ring %x Added %x\n",
    GigAdapter->tx_ring,
    ((UINT8 *) GigAdapter->tx_ring + (sizeof (E1000_TRANSMIT_DESCRIPTOR) * DEFAULT_TX_DESCRIPTORS))
    ));
  DEBUGPRINT(E1000, (
    "Local Rx Buffer %X size %X\n",
    GigAdapter->local_rx_buffer,
    (sizeof (E1000_TRANSMIT_DESCRIPTOR) * DEFAULT_TX_DESCRIPTORS)
    ));

  //
  // now to link the RX Ring to the local buffers
  //
  for (i = 0; i < DEFAULT_RX_DESCRIPTORS; i++) {
#ifndef INTEL_KDNET
    GigAdapter->rx_ring[i].buffer_addr = (UINT64) ((UINTN)GigAdapter->local_rx_buffer[i].RxBuffer);
#else
    e1000_MapMem(
      GigAdapter,
      (UINT64)&GigAdapter->local_rx_buffer[i].RxBuffer[0],
      sizeof(GigAdapter->local_rx_buffer[i].RxBuffer),
      &GigAdapter->rx_ring[i].buffer_addr);
#endif
    GigAdapter->DebugRxBuffer[i] = GigAdapter->rx_ring[i].buffer_addr;
    GigAdapter->rx_ring[i].status = E1000_RXD_STAT_IXSM;
    DEBUGPRINT(E1000, ("Rx Local Buffer %X\n", (GigAdapter->rx_ring[i]).buffer_addr));
  }
  //
  // Setup the RDBA, RDLEN
  //
#ifndef INTEL_KDNET
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RDBAL(0), (UINT32) (UINTN) (GigAdapter->rx_ring));
#else
  e1000_MapMem(
    GigAdapter,
    (UINT64)(UINTN)GigAdapter->rx_ring,
    sizeof(GigAdapter->rx_ring),
    &MemAddr);

  MemPtr = (UINT32 *)&MemAddr;
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RDBAL(0), *MemPtr);
#endif

  //
  // Set the MemPtr to the high dword of the rx_ring so we can store it in RDBAH0.
  // Right shifts do not seem to work with the EFI compiler so we do it like this for now.
  //
#ifndef INTEL_KDNET
  MemAddr = (UINT64) (UINTN) GigAdapter->rx_ring;
  MemPtr  = &((UINT32) MemAddr);
#endif
  MemPtr++;
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RDBAH(0), *MemPtr);

  E1000_WRITE_REG (&GigAdapter->hw, E1000_RDLEN(0), (sizeof (E1000_RECEIVE_DESCRIPTOR) * DEFAULT_RX_DESCRIPTORS));

  DEBUGPRINT(E1000, ("Rdbal0 %X\n", (UINT32) E1000_READ_REG(&GigAdapter->hw, E1000_RDBAL(0))));
  DEBUGPRINT(E1000, ("RdBah0 %X\n", (UINT32) E1000_READ_REG(&GigAdapter->hw, E1000_RDBAH(0))));
  DEBUGPRINT(E1000, ("Rx Ring %X\n", GigAdapter->rx_ring));
  //
  // Set the transmit tail equal to the head pointer (we do not want hardware to try to
  // transmit packets yet).
  //
  GigAdapter->cur_tx_ind = (UINT16) E1000_READ_REG (&GigAdapter->hw, E1000_TDH(0));
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDT(0), GigAdapter->cur_tx_ind);
  GigAdapter->xmit_done_head = GigAdapter->cur_tx_ind;

  GigAdapter->cur_rx_ind = (UINT16) E1000_READ_REG(&GigAdapter->hw, E1000_RDH(0));
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RDT(0), GigAdapter->cur_rx_ind);

  if (GigAdapter->hw.mac.type != e1000_82575 &&
     GigAdapter->hw.mac.type != e1000_82576 &&
     GigAdapter->hw.mac.type != e1000_82580
     && GigAdapter->hw.mac.type != e1000_i350
     && GigAdapter->hw.mac.type  != e1000_i354
     && GigAdapter->hw.mac.type != e1000_i210
     && GigAdapter->hw.mac.type != e1000_i211
    )
  {
    E1000_WRITE_REG (&GigAdapter->hw, E1000_SRRCTL (0), E1000_SRRCTL_DESCTYPE_LEGACY);

    e1000_SetRegBits (GigAdapter, E1000_RXDCTL (0), E1000_RXDCTL_QUEUE_ENABLE);
    i = 0;
    do {
      TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_RXDCTL (0));
      i++;
      if ((TempReg & E1000_RXDCTL_QUEUE_ENABLE) != 0) {
        DEBUGPRINT (E1000, ("RX queue enabled, after attempt i = %d\n", i));
        break;
      }

      DelayInMicroseconds (GigAdapter, 1);
    } while (i < 1000);

    if (i >= 1000) {
      DEBUGPRINT (CRITICAL, ("Enable RX queue failed!\n"));
    }
  }


#ifndef NO_82575_SUPPORT
  if (GigAdapter->hw.mac.type != e1000_82575 &&
     GigAdapter->hw.mac.type != e1000_82576 &&
     GigAdapter->hw.mac.type != e1000_82580
     && GigAdapter->hw.mac.type != e1000_i350
     && GigAdapter->hw.mac.type  != e1000_i354
     && GigAdapter->hw.mac.type != e1000_i210
     && GigAdapter->hw.mac.type != e1000_i211
    )
#endif
  {
    //
    // Set the software tail pointer just behind head to give hardware the entire ring
    //
    if (GigAdapter->cur_rx_ind == 0) {
      E1000_WRITE_REG (&GigAdapter->hw, E1000_RDT(0), DEFAULT_RX_DESCRIPTORS - 1);
    } else {
      E1000_WRITE_REG (&GigAdapter->hw, E1000_RDT(0), GigAdapter->cur_rx_ind - 1);
    }
  }

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifdef INTEL_KDNET
  if (IsClient1GDevice(GigAdapter))
#endif
  {  
  //Setup WA for HSD LADSW [Bug][5535020][Jacksonville SW 1][SPT-H] DOS NDIS2 driver - hang during file copy
  if(e1000_pch_spt == GigAdapter->hw.mac.type ||
     e1000_pch_cnp == GigAdapter->hw.mac.type) {
    // Fextnvm7 @ 0xe4[2] = 1
    UINT32 reg = E1000_READ_REG (&GigAdapter->hw, E1000_FEXTNVM7);
    reg |= 4;
    E1000_WRITE_REG (&GigAdapter->hw, E1000_FEXTNVM7, reg);
    // Fextnvm9 @ 0x5bb4[13:12] = 11
#define E1000_FEXTNVM9	0x5BB4  /* Future Extended NVM 9 - RW */
	reg = E1000_READ_REG (&GigAdapter->hw, E1000_FEXTNVM9);
	reg |= 0x1800;
    E1000_WRITE_REG (&GigAdapter->hw, E1000_FEXTNVM9, reg);
    
    if(e1000_pch_spt == GigAdapter->hw.mac.type) { // Not applicable for CNP
		// MULR = 2
		// 0x5BBC[13]      = 0x1
		// 0x3840[29..28] = 0x2
		reg = E1000_READ_REG (&GigAdapter->hw, E1000_FEXTNVM11);
		reg |= (1 << 13);
		E1000_WRITE_REG (&GigAdapter->hw, E1000_FEXTNVM11, reg);	

		reg = E1000_READ_REG (&GigAdapter->hw, E1000_TARC(0));
#ifndef E1000_TARC0_CB_MULTIQ_2_REQ
#define E1000_TARC0_CB_MULTIQ_2_REQ (1 << 29)
#endif // E1000_TARC0_CB_MULTIQ_2_REQ
		reg |= E1000_TARC0_CB_MULTIQ_2_REQ;
		E1000_WRITE_REG (&GigAdapter->hw, E1000_TARC(0), reg);
	}
  }
  }
#endif // INTEL_KDNET_1G_CLIENT_MERGE_MARKER

  //
  // Zero out PSRCTL to use default packet size settings in RCTL
  //
  E1000_WRITE_REG (&GigAdapter->hw, E1000_PSRCTL, 0);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_MRQC, 0);


#ifndef INTEL_KDNET
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDBAL(0), (UINT32) (UINTN) (GigAdapter->tx_ring));
  MemAddr = (UINT64) (UINTN) GigAdapter->tx_ring;
  MemPtr  = &((UINT32) MemAddr);
#else
  e1000_MapMem(
    GigAdapter,
    (UINT64)(UINTN)GigAdapter->tx_ring,
    sizeof(GigAdapter->tx_ring),
    &MemAddr);

  MemPtr = (UINT32 *)&MemAddr;
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDBAL(0), *MemPtr);
#endif
  MemPtr++;
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDBAH(0), *MemPtr);
  DEBUGPRINT(E1000, ("TdBah0 %X\n", *MemPtr));
  DEBUGWAIT(E1000);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TDLEN(0), (sizeof (E1000_TRANSMIT_DESCRIPTOR) * DEFAULT_TX_DESCRIPTORS));

  if (GigAdapter->hw.mac.type == e1000_82580
     || GigAdapter->hw.mac.type == e1000_i350
     || GigAdapter->hw.mac.type  == e1000_i354
     || GigAdapter->hw.mac.type == e1000_i210
     || GigAdapter->hw.mac.type == e1000_i211
  )
  {
    e1000_SetRegBits (GigAdapter, E1000_TXDCTL (0), E1000_TXDCTL_QUEUE_ENABLE);

    for (i = 0; i < 1000; i++) {
      TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_TXDCTL (0));
      if ((TempReg & E1000_TXDCTL_QUEUE_ENABLE) != 0) {
        DEBUGPRINT (E1000, ("TX queue enabled, after attempt i = %d\n", i));
        break;
      }

      DelayInMicroseconds (GigAdapter, 1);
    }
    if (i >= 1000) {
      DEBUGPRINT (CRITICAL, ("Enable TX queue failed!\n"));
    }
  }


  TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_TCTL);
  TempReg = (TempReg | E1000_TCTL_EN | E1000_TCTL_PSP);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_TCTL, TempReg);

  e1000_PciFlush(&GigAdapter->hw);
}

#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER

VOID
RxEnable (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Enables Rx unit.

Arguments:
  GigAdapter       - Pointer to the adapter structure

Returns:
  None

--*/
{
  UINT32 RctlReg;

  RctlReg = E1000_READ_REG (&GigAdapter->hw, E1000_RCTL);
  RctlReg |= E1000_RCTL_EN;
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, RctlReg);
}

VOID
RxDisable (
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Disables Rx unit.

Arguments:
  GigAdapter       - Pointer to the adapter structure

Returns:
  None

--*/
{
  UINT32 RctlReg;

#ifndef NO_82574_SUPPORT
  if ((GigAdapter->hw.mac.type == e1000_82574)
    || (GigAdapter->hw.mac.type == e1000_82583)){
  return;
  }
#endif

  if (GigAdapter->hw.mac.type == e1000_82575){
    return;
  }

  RctlReg = E1000_READ_REG (&GigAdapter->hw, E1000_RCTL);
  RctlReg &= ~E1000_RCTL_EN;
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, RctlReg);
}

#endif // INTEL_KDNET_1G_SERVER_MERGE_MARKER

UINTN
e1000_SetFilter (
  GIG_DRIVER_DATA *GigAdapter,
  UINT16          NewFilter,
  UINT64          cpb,
  UINT32          cpbsize
  )
/*++

Routine Description:
  Stops the receive unit.

Arguments:
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..
  NewFilter           - A PXE_OPFLAGS bit field indicating what filters to use.
  cpb                  -
Returns:
  None

--*/
// GC_TODO:    cpbsize - add argument and description to function comment
{
  PXE_CPB_RECEIVE_FILTERS *CpbReceiveFilter;
  UINT32                  UpdateRCTL;
  UINT16                  CfgFilter;
  UINT16                  OldFilter;
  UINT16                  MulticastCount;
  UINT16                  i;
  UINT16                  j;

  DEBUGPRINT(E1000, ("e1000_SetFilter\n"));

#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER
#ifdef INTEL_KDNET
  if (IsServer1GDevice(GigAdapter))
#endif
  {
  CpbReceiveFilter = (PXE_CPB_RECEIVE_FILTERS *) (UINTN) cpb;
  OldFilter = GigAdapter->Rx_Filter;

  //
  // only these bits need a change in the configuration
  // actually change in bcast requires configure but we ignore that change
  //
  CfgFilter = PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS |
    PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST |
    PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST;

  if ((OldFilter & CfgFilter) != (NewFilter & CfgFilter)) {

    UpdateRCTL = E1000_READ_REG (&GigAdapter->hw, E1000_RCTL);

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS) {
      //
      // add the UPE bit to the variable to be written to the RCTL
      //
      UpdateRCTL |= E1000_RCTL_UPE;
    }

    //if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST) {
      //
      // add the BAM bit to the variable to be written to the RCTL
      //
      //UpdateRCTL |= E1000_RCTL_BAM;
    //}

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) {
      //
      // add the MPE bit to the variable to be written to the RCTL
      //
      UpdateRCTL |= E1000_RCTL_MPE;
    }
    //
    // Put the card into the proper mode...
    // Rx unit needs to be disabled and re-enabled
    // while changing filters.
    //
    if (GigAdapter->ReceiveStarted == TRUE) {
      RxDisable (GigAdapter);
    }

    UpdateRCTL |= E1000_RCTL_BAM;
    GigAdapter->Rx_Filter = NewFilter;
    E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, UpdateRCTL);

    if (GigAdapter->ReceiveStarted == TRUE) {
      RxEnable (GigAdapter);
    }
  }

  //
  // check if mcast setting changed
  //
  if ((
        (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST) !=
        (OldFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST)
  ) ||
      (CpbReceiveFilter != NULL)
      ) {


    //
    // copy the list
    //
    if (CpbReceiveFilter != NULL) {
      UINT8 McAddrList[MAX_MCAST_ADDRESS_CNT][ETH_ADDR_LEN];

      MulticastCount = GigAdapter->McastList.Length = (UINT16) (cpbsize / PXE_MAC_LENGTH);
      DEBUGPRINT(E1000, ("E1000: MulticastCount=%d\n", MulticastCount));

      ZeroMem(GigAdapter->McastList.McAddr, MAX_MCAST_ADDRESS_CNT*PXE_MAC_LENGTH);
      CopyMem(
        GigAdapter->McastList.McAddr,
        (VOID*)(UINTN) CpbReceiveFilter->MCastList,
        cpbsize
        );

      //
      // Copy the multicast address list into a form that can be accepted by the
      // shared code.
      //
      for (i = 0; (i < MulticastCount && i < MAX_MCAST_ADDRESS_CNT); i++) {
        DEBUGPRINT(E1000, ("E1000: MulticastAddress %d:", i));
        for (j = 0; j < ETH_ADDR_LEN; j++) {
          McAddrList[i][j] = GigAdapter->McastList.McAddr[i][j];
          DEBUGPRINT(E1000, ("%02x", CpbReceiveFilter->MCastList[i][j]));
        }
        DEBUGPRINT(E1000, ("\n"));
      }

      e1000_BlockIt (GigAdapter, TRUE);
      //
      //Rx unit needs to be disabled while changing filters.
      //
      if (GigAdapter->ReceiveStarted == TRUE) {
        RxDisable (GigAdapter);
      }
      e1000_update_mc_addr_list (
        &GigAdapter->hw,
        &McAddrList[0][0],
        MulticastCount
        );
      if (GigAdapter->ReceiveStarted == TRUE) {
        RxEnable (GigAdapter);
      }
      e1000_BlockIt (GigAdapter, FALSE);

    }

    //
    // are we setting the list or resetting??
    //
    if ((NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST) != 0) {
      DEBUGPRINT(E1000, ("E1000: Creating new multicast list.\n"));
      GigAdapter->Rx_Filter |= PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST;
    } else {
      DEBUGPRINT(E1000, ("E1000: Disabling multicast list.\n"));
      GigAdapter->Rx_Filter &= (~PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST);
    }
  }

  if (NewFilter != 0) {
    //
    // Enable unicast and start the RU
    //
    GigAdapter->Rx_Filter |= (NewFilter | PXE_OPFLAGS_RECEIVE_FILTER_UNICAST);
    e1000_ReceiveEnable (GigAdapter);
  } else {
    //
    // may be disabling everything!
    //
    GigAdapter->Rx_Filter = NewFilter;
    e1000_ReceiveDisable (GigAdapter);
  }

  }
#endif // INTEL_KDNET_1G_SERVER_MERGE_MARKER

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifdef INTEL_KDNET
  if (IsClient1GDevice(GigAdapter))
#endif
  {
  CpbReceiveFilter = (PXE_CPB_RECEIVE_FILTERS *) (UINTN) cpb;
  OldFilter = GigAdapter->Rx_Filter;

  //
  // only these bits need a change in the configuration
  // actually change in bcast requires configure but we ignore that change
  //
  CfgFilter = PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS |
    PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST |
    PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST;

  if ((OldFilter & CfgFilter) != (NewFilter & CfgFilter)) {
    //
    // Put the card into the proper mode...
    //
    if (GigAdapter->ReceiveStarted == TRUE) {
      e1000_ReceiveDisable (GigAdapter);

    }

    UpdateRCTL = E1000_READ_REG (&GigAdapter->hw, E1000_RCTL);

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS) {
      //
      // add the UPE bit to the variable to be written to the RCTL
      //
      UpdateRCTL |= E1000_RCTL_UPE;
    }

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST) {
      //
      // add the BAM bit to the variable to be written to the RCTL
      //
      UpdateRCTL |= E1000_RCTL_BAM;
    }

    if (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) {
      //
      // add the MPE bit to the variable to be written to the RCTL
      //
      UpdateRCTL |= E1000_RCTL_MPE;
    }

    UpdateRCTL |= E1000_RCTL_BAM;
    GigAdapter->Rx_Filter = NewFilter;
    E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, UpdateRCTL);
  }

  //
  // check if mcast setting changed
  //
  if ((
        (NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST) !=
        (OldFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST)
  ) ||
      (CpbReceiveFilter != NULL)
      ) {


    //
    // copy the list
    //
    if (CpbReceiveFilter != NULL) {
      UINT8 McAddrList[MAX_MCAST_ADDRESS_CNT][ETH_ADDR_LEN];

      MulticastCount = GigAdapter->McastList.Length = (UINT16) (cpbsize / PXE_MAC_LENGTH);
      DEBUGPRINT(E1000, ("E1000: MulticastCount=%d\n", MulticastCount));

      ZeroMem(GigAdapter->McastList.McAddr, MAX_MCAST_ADDRESS_CNT*PXE_MAC_LENGTH);
      CopyMem(
        GigAdapter->McastList.McAddr,
        (VOID*)(UINTN) CpbReceiveFilter->MCastList,
        cpbsize
        );

      //
      // Copy the multicast address list into a form that can be accepted by the
      // shared code.
      //
      for (i = 0; (i < MulticastCount && i < MAX_MCAST_ADDRESS_CNT); i++) {
        DEBUGPRINT(E1000, ("E1000: MulticastAddress %d:", i));
        for (j = 0; j < ETH_ADDR_LEN; j++) {
          McAddrList[i][j] = GigAdapter->McastList.McAddr[i][j];
          DEBUGPRINT(E1000, ("%02x", CpbReceiveFilter->MCastList[i][j]));
        }
        DEBUGPRINT(E1000, ("\n"));
      }

      e1000_BlockIt (GigAdapter, TRUE);
      e1000_update_mc_addr_list (
        &GigAdapter->hw,
        &McAddrList[0][0],
        MulticastCount
        );
      e1000_BlockIt (GigAdapter, FALSE);

    }

    //
    // are we setting the list or resetting??
    //
    if ((NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST) != 0) {
      DEBUGPRINT(E1000, ("E1000: Creating new multicast list.\n"));
      GigAdapter->Rx_Filter |= PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST;
    } else {
      DEBUGPRINT(E1000, ("E1000: Disabling multicast list.\n"));
      GigAdapter->Rx_Filter &= (~PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST);
    }

    if (GigAdapter->ReceiveStarted == TRUE) {
      e1000_ReceiveDisable (GigAdapter);
    }
  }

  if (NewFilter != 0) {
    //
    // Enable unicast and start the RU
    //
    GigAdapter->Rx_Filter |= (NewFilter | PXE_OPFLAGS_RECEIVE_FILTER_UNICAST);
    e1000_ReceiveEnable (GigAdapter);
  } else {
    //
    // may be disabling everything!
    //
    if (GigAdapter->ReceiveStarted == TRUE) {
      e1000_ReceiveDisable (GigAdapter);
    }

    GigAdapter->Rx_Filter = NewFilter;
  }
  }
#endif // INTEL_KDNET_1G_CLIENT_MERGE_MARKER

  return 0;
};

VOID
e1000_ReceiveDisable (
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Stops the receive unit.

Arguments:
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  E1000_RECEIVE_DESCRIPTOR *ReceiveDesc;
#ifndef NO_82571_SUPPORT // kdnet fix: this ifndef is missing upstream
  UINT32                   TempReg;
#endif
  UINTN                    i;
  UINT32                   RxdCtl;


  DEBUGPRINT(E1000, ("e1000_ReceiveDisable\n"));

  if (GigAdapter->ReceiveStarted == FALSE) {
    DEBUGPRINT(CRITICAL, ("Receive unit already disabled!\n"));
    return;
  }

#ifndef NO_82571_SUPPORT // kdnet fix: this ifndef is missing upstream
  if (GigAdapter->hw.mac.type == e1000_82571) {
    TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_RCTL);
    TempReg &= ~E1000_RCTL_EN;
    E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, TempReg);
  }
#endif

  //
  // On I82575 the ring must be reset when the recieve unit is disabled.
  //
  if (GigAdapter->hw.mac.type == e1000_82575
    || GigAdapter->hw.mac.type == e1000_82576
#ifndef NO_82580_SUPPORT
    || GigAdapter->hw.mac.type == e1000_82580
#endif
    || GigAdapter->hw.mac.type == e1000_i350
    || GigAdapter->hw.mac.type  == e1000_i354
     || GigAdapter->hw.mac.type == e1000_i210
     || GigAdapter->hw.mac.type == e1000_i211
    )
  {
    e1000_ClearRegBits(GigAdapter, E1000_RXDCTL(0), E1000_RXDCTL_QUEUE_ENABLE);

    #define MAX_QUEUE_DISABLE_TIME  200
    i = 0;
    do {
#ifndef INTEL_KDNET
      gBS->Stall(1);
#else
      KeStallExecutionProcessor(1);
#endif
      RxdCtl = E1000_READ_REG (&GigAdapter->hw, E1000_RXDCTL(0));

      i++;
      if (i >= MAX_QUEUE_DISABLE_TIME) {
          break ;
      }
    } while((RxdCtl & E1000_RXDCTL_QUEUE_ENABLE) != 0);
    DEBUGPRINT (E1000, ("Receiver Disabled\n"));

    E1000_WRITE_REG (&GigAdapter->hw, E1000_RDH(0), 0);
    E1000_WRITE_REG (&GigAdapter->hw, E1000_RDT(0), 0);
    GigAdapter->cur_rx_ind = 0;
  }

	if (GigAdapter->hw.mac.type == e1000_82575
     || GigAdapter->hw.mac.type == e1000_82576
#ifndef NO_82580_SUPPORT
	 || GigAdapter->hw.mac.type == e1000_82580
#endif
	 || GigAdapter->hw.mac.type == e1000_i350
     || GigAdapter->hw.mac.type  == e1000_i354
     || GigAdapter->hw.mac.type == e1000_i210
     || GigAdapter->hw.mac.type == e1000_i211
#ifndef NO_82571_SUPPORT
	 || GigAdapter->hw.mac.type == e1000_82571
#endif
	)
	{
		//
		// Clean up any left over packets
		//
		ReceiveDesc = GigAdapter->rx_ring;
		for (i = 0; i < DEFAULT_RX_DESCRIPTORS; i++) {
			ReceiveDesc->length = 0;
			ReceiveDesc->status = 0;
			ReceiveDesc->errors = 0;
			ReceiveDesc++;
		}
	}

  GigAdapter->ReceiveStarted = FALSE;
  return ;
}

VOID
e1000_ReceiveEnable (
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Starts the receive unit.

Arguments:
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  UINT32  TempReg;
  UINTN   i;

  DEBUGPRINT(E1000, ("e1000_ReceiveEnable\n"));

  if (GigAdapter->ReceiveStarted == TRUE) {
    DEBUGPRINT(CRITICAL, ("Receive unit already started!\n"));
    return;
  }

  GigAdapter->Int_Status  = 0;
  TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_RCTL);
  TempReg |= (E1000_RCTL_EN | E1000_RCTL_BAM);
  E1000_WRITE_REG (&GigAdapter->hw, E1000_RCTL, TempReg);

  //
  // Move the tail descriptor to begin receives on I82575
  //
#ifndef NO_82575_SUPPORT
  if (GigAdapter->hw.mac.type == e1000_82575
#ifndef NO_82576_SUPPORT
    || GigAdapter->hw.mac.type == e1000_82576
#ifndef NO_82580_SUPPORT
    || GigAdapter->hw.mac.type == e1000_82580
#endif
    || GigAdapter->hw.mac.type == e1000_i350
    || GigAdapter->hw.mac.type == e1000_i354
     || GigAdapter->hw.mac.type == e1000_i210
     || GigAdapter->hw.mac.type == e1000_i211
#endif
    ) {
    if (GigAdapter->hw.mac.type == e1000_82575) {
      e1000_rx_fifo_flush_82575(&GigAdapter->hw);
    }

    e1000_SetRegBits(GigAdapter, E1000_RXDCTL(0), E1000_RXDCTL_QUEUE_ENABLE);

    #define MAX_QUEUE_ENABLE_TIME   200
    i = 0;
    do {
#ifndef INTEL_KDNET
      gBS->Stall(1);
#else
      KeStallExecutionProcessor(1);
#endif
      TempReg = E1000_READ_REG (&GigAdapter->hw, E1000_RXDCTL(0));

      i++;
      if (i >= MAX_QUEUE_ENABLE_TIME) {
          break ;
      }
    } while((TempReg & E1000_RXDCTL_QUEUE_ENABLE) == 0);

    E1000_WRITE_REG (&GigAdapter->hw, E1000_RDT(0), DEFAULT_RX_DESCRIPTORS - 1);
    E1000_WRITE_REG (&GigAdapter->hw, E1000_RDH(0), 0);
    GigAdapter->cur_rx_ind = (UINT16) E1000_READ_REG(&GigAdapter->hw, E1000_RDH(0));

  }
#endif

  GigAdapter->ReceiveStarted = TRUE;
}

#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER
VOID
e1000_TransmitDisable (
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  Stops the transmit unit.

Arguments:
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  UINTN                    i;
  UINT32                   TxdCtl;

  DEBUGPRINT(E1000, ("e1000_TransmitDisable\n"));

  //
  // From "Intel(R) 82575EB Gigabit Ethernet Controller Software Developer's Manual and EEPROM Guide":
  //
  // 3.6.1 Dynamic Queue Enabling and Disabling
  // Transmit queues can be dynamically enabled or disabled provided the following procedure is followed:
  // Enabling: Follow the per queue initialization previously described.
  // Disabling:
  // - Stop storing packet for transmission in this queue.
  // - Wait until the head of the queue (TDH) is equal to the tail (TDT). For example, the queue is empty.
  // - Disable the queue by clearing TXDCTL.ENABLE.
  // The Tx path can be disabled only after all Tx queues are disabled.
  //
  // From the TXDCTL.ENABLE description:
  // When disabling a queue, this bit is cleared only after all activity at the queue stopped.
  //
  // So we just poll TXDCTL.ENABLE until it becomes 0.
  //
  switch(GigAdapter->hw.mac.type)
  {
#ifndef NO_82575_SUPPORT
    case e1000_82575:
    case e1000_82576:
#endif
#ifndef NO_82580_SUPPORT
    case e1000_82580:
#endif
    case e1000_i350:
    case e1000_i354:
    case e1000_i210:
    case e1000_i211:
      e1000_ClearRegBits(GigAdapter, E1000_TXDCTL(0), E1000_TXDCTL_QUEUE_ENABLE);
      i = 0;
      #define MAX_QUEUE_DISABLE_TIME  200
      do {
#ifndef INTEL_KDNET
        gBS->Stall(1);
#else
        KeStallExecutionProcessor(1);
#endif
        TxdCtl = E1000_READ_REG (&GigAdapter->hw, E1000_TXDCTL(0));
      } while((++i < MAX_QUEUE_DISABLE_TIME) && ((TxdCtl & E1000_TXDCTL_QUEUE_ENABLE) != 0));
      DEBUGPRINT (E1000, ("Transmitter Disabled\n"));
      break;
  }
}
#endif // INTEL_KDNET_1G_SERVER_MERGE_MARKER

BOOLEAN
e1000_WaitForAutoNeg (
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine blocks until auto-negotiation completes or times out (after 4.5 seconds).

Arguments:
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  TRUE   - Auto-negotiation completed successfully,
  FALSE  - Auto-negotiation did not complete (i.e., timed out)

--*/
{
  BOOLEAN AutoNegComplete;
  UINTN   i;
  UINT16  Reg;
  UINT32  Status;

  AutoNegComplete   = FALSE;
  Status            = 0;

  DEBUGPRINT(E1000, ("e1000_WaitForAutoNeg\n"));

  if (!GigAdapter->CableDetect) {
    //
    // Caller specified not to detect cable, so we return true.
    //
    DEBUGPRINT(E1000, ("Cable detection disabled.\n"));
    return TRUE;
  }

  //
  // The shared code will wait for autonegotiation, so if link is not up by the time
  // we reach this function then either we have no link, or are trying to two-pair
  // downshift.  In the case of a two pair downshift we will wait up to 30 seconds for
  // link to come back up.
  //
  for (i=0; i<500; i++) {
    Status = E1000_READ_REG (&GigAdapter->hw, E1000_STATUS);
    if ((Status & E1000_STATUS_LU) != 0) {
        if ((E1000_DEV_ID_I210_COPPER == GigAdapter->hw.device_id) ||
            (E1000_DEV_ID_I210_COPPER_FLASHLESS == GigAdapter->hw.device_id) ||
            (E1000_DEV_ID_I211_COPPER == GigAdapter->hw.device_id)) {
          DelayInMilliseconds (1000);
        }
      DEBUGPRINT(E1000, ("Successfully established link on retry %d\n", i));
      return TRUE;
    }
    DelayInMilliseconds (10);
  }
  DEBUGPRINT(E1000, ("Link up not detected\n"));

  if (GigAdapter->hw.phy.type == e1000_phy_igp) {
    DEBUGPRINT(E1000, ("IGP PHY\n"));
    //
    // Workaround: read the PHY register up to three times to see if it comes up
    // if not then we exit.
    //
    for (i = 5; i != 0; i--) {
      e1000_read_phy_reg (&GigAdapter->hw, PHY_1000T_STATUS, &Reg);
      if (Reg != 0) {
        AutoNegComplete = e1000_DownShift (GigAdapter);
        break;
      }
      DelayInMilliseconds (1000);
      Status = E1000_READ_REG (&GigAdapter->hw, E1000_STATUS);
      if ((Status & E1000_STATUS_LU) != 0) {
        AutoNegComplete = TRUE;
        break;
      }
    }

  } else if (GigAdapter->hw.phy.type == e1000_phy_m88){
    //
    // We are on a Marvel PHY that supports 2-pair downshift
    // Check the real time link status bit to see if there is actually a cable connected
    // If so then we will attempt to downshift, if not then we will report failure
    // Wait for up to 1 second for real time link detected
    //
    for (i = 100; i != 0; i--) {
      DEBUGPRINT(E1000, ("."));
      e1000_read_phy_reg (&GigAdapter->hw, M88E1000_PHY_SPEC_STATUS, &Reg);
      if ((Reg & M88E1000_PSSR_LINK) != 0) {
        DEBUGPRINT(E1000, ("e1000_DownShift - Real Time Link Detected\n"));
        AutoNegComplete = e1000_DownShift (GigAdapter);
        break;
      }

      DelayInMilliseconds (10);
    }
  }

  DEBUGPRINT(E1000, ("Return %d\n", AutoNegComplete));
  DEBUGWAIT (E1000);
  return AutoNegComplete;
}

UINT16
e1000_FreeTxBuffers(
  IN GIG_DRIVER_DATA *GigAdapter,
  IN UINT16          NumEntries,
  OUT UINT64         *TxBuffer
  )
  /*++

Routine Description:
  Free TX buffers that have been transmitted by the hardware.

Arguments:
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on.
  NumEntries           - Number of entries in the array which can be freed.
  TxBuffer             - Array to pass back free TX buffer

Returns:
   Number of TX buffers written.

--*/

{
  E1000_TRANSMIT_DESCRIPTOR *TransmitDescriptor;
  UINT32                     Tdh;
  UINT16                     i;

  DEBUGPRINT(E1000, ("e1000_FreeTxBuffers\n"));

  //
  //  Read the TX head posistion so we can see which packets have been sent out on the wire.
  //
  Tdh = E1000_READ_REG (&GigAdapter->hw, E1000_TDH(0));
  DEBUGPRINT(E1000, ("TDH = %d, GigAdapter->xmit_done_head = %d\n", Tdh, GigAdapter->xmit_done_head));

  //
  //  If Tdh does not equal xmit_done_head then we will fill all the transmitted buffer
  // addresses between Tdh and xmit_done_head into the completed buffers array
  //
  i = 0;
  do {
    if (i >= NumEntries) {
      DEBUGPRINT(E1000, ("Exceeded number of DB entries, i=%d, NumEntries=%d\n", i, NumEntries));
      break;
    }

    TransmitDescriptor = &GigAdapter->tx_ring[GigAdapter->xmit_done_head];
    if ((TransmitDescriptor->upper.fields.status & E1000_TXD_STAT_DD) != 0) {

      if (GigAdapter->TxBufferUnmappedAddr[GigAdapter->xmit_done_head] == 0) {
        DEBUGPRINT(CRITICAL, ("ERROR: TX buffer complete without being marked used!\n"));
        break;
      }

      DEBUGPRINT(E1000, ("Writing buffer address %d, %x\n", i, TxBuffer[i]));
      TxBuffer[i] = GigAdapter->TxBufferUnmappedAddr[GigAdapter->xmit_done_head];
      i++;

      e1000_UnMapMem (
        GigAdapter,
        GigAdapter->TxBufferUnmappedAddr[GigAdapter->xmit_done_head],
        TransmitDescriptor->lower.flags.length,
        TransmitDescriptor->buffer_addr
        );
      GigAdapter->TxBufferUnmappedAddr[GigAdapter->xmit_done_head] = 0;
      TransmitDescriptor->upper.fields.status = 0;

      GigAdapter->xmit_done_head++;
      if (GigAdapter->xmit_done_head >= DEFAULT_TX_DESCRIPTORS) {
        GigAdapter->xmit_done_head = 0;
      }
    } else {
      DEBUGPRINT(E1000, ("TX Descriptor %d not done\n", GigAdapter->xmit_done_head));
      break;
    }
  } while (Tdh != GigAdapter->xmit_done_head);
  return i;
}

UINT32
e1000_SetRegBits (
  GIG_DRIVER_DATA *GigAdapter,
  UINT32           Register,
  UINT32           BitMask
  )
/*++

Routine Description:
  Sets specified bits in a device register

Arguments:
  GigAdapter        - Pointer to the device instance
  Register          - Register to write
  BitMask           - Bits to set

Returns:
  Data              - Returns the value read from the PCI register.

--*/
{
  UINT32 TempReg;

  TempReg = E1000_READ_REG (&GigAdapter->hw, Register);
  TempReg |= BitMask;
  E1000_WRITE_REG (&GigAdapter->hw, Register, TempReg);

  return TempReg;
}

UINT32
e1000_ClearRegBits (
  GIG_DRIVER_DATA *GigAdapter,
  UINT32           Register,
  UINT32           BitMask
  )
/*++

Routine Description:
  Clears specified bits in a device register

Arguments:
  GigAdapter        - Pointer to the device instance
  Register          - Register to write
  BitMask           - Bits to clear

Returns:
  Data              - Returns the value read from the PCI register.

--*/

{
  UINT32 TempReg;

  TempReg = E1000_READ_REG (&GigAdapter->hw, Register);
  TempReg &= ~BitMask;
  E1000_WRITE_REG (&GigAdapter->hw, Register, TempReg);

  return TempReg;
}

BOOLEAN
IsLinkUp(
  IN GIG_DRIVER_DATA *GigAdapter
)
{
  UINT32                  Reg;

  Reg = E1000_READ_REG (&GigAdapter->hw, E1000_STATUS);
  if ((Reg & E1000_STATUS_LU) == 0) {
    return FALSE;
  } else {
    return TRUE;
  }
}

#ifndef INTEL_KDNET

UINT8
GetLinkSpeed(
  IN GIG_DRIVER_DATA *GigAdapter
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
  UINT16    Speed     = 0;
  UINT16    Duplex    = 0;
  UINT8     LinkSpeed = LINK_SPEED_UNKNOWN;

  e1000_get_speed_and_duplex(&GigAdapter->hw, &Speed, &Duplex);
  switch (Speed) {
  case SPEED_10:
    if (Duplex == FULL_DUPLEX) {
      LinkSpeed = LINK_SPEED_10FULL;
    } else {
      LinkSpeed = LINK_SPEED_10HALF;
    }
    break;
  case SPEED_100:
    if (Duplex == FULL_DUPLEX) {
      LinkSpeed = LINK_SPEED_100FULL;
    } else {
      LinkSpeed = LINK_SPEED_100HALF;
    }
    break;
  case SPEED_1000:
    if (Duplex == FULL_DUPLEX) {
      LinkSpeed = LINK_SPEED_1000FULL;
    } else {
      LinkSpeed = LINK_SPEED_1000HALF;
    }
    break;
  default:
    LinkSpeed = LINK_SPEED_UNKNOWN;
  }
  DEBUGPRINT(HII, ("Link Speed Status %x\n", LinkSpeed));
  return LinkSpeed;
}

VOID
BlinkLeds(
  IN GIG_DRIVER_DATA *GigAdapter,
  IN UINT32          Seconds
)
{
  e1000_setup_led(&GigAdapter->hw);

  switch (GigAdapter->hw.mac.type) {
    case e1000_pch2lan:
    case e1000_pch_lpt:
    case e1000_pch_spt:
    case e1000_pch_cnp:
#ifdef VALLEYVIEW_HW
    case e1000_soc_vlv:
#endif /* VALLEYVIEW_HW */
    case e1000_pchlan:
#ifndef NO_82574_SUPPORT
    case e1000_82574:
#endif /* NO_82574_SUPPORT */
      {
        UINT32 Miliseconds = Seconds * 1000;

        while (Miliseconds > 0) {
          e1000_led_on(&GigAdapter->hw);
          DelayInMicroseconds(GigAdapter, 200 * 1000);
          Miliseconds -= 200;
          e1000_led_off(&GigAdapter->hw);

          //
          // Do not wait last 200ms with LEDs off.
          //
          if (Miliseconds > 200) {
            DelayInMicroseconds(GigAdapter, 200 * 1000);
            Miliseconds -= 200;
          } else {
            //
            // Break here when the Seconds is an odd number.
            // Break on 'while (Miliseconds > 0)' when the Seconds is an even number.
            //
            break;
          }
        }
      }
      break;

    default:
      e1000_blink_led(&GigAdapter->hw);
      DelayInMicroseconds(GigAdapter, Seconds * 1000 * 1000);
      break;
  }

  e1000_cleanup_led(&GigAdapter->hw);
}

EFI_STATUS
ReadPbaString(
  IN     GIG_DRIVER_DATA *GigAdapter,
  IN OUT UINT8           *PbaNumber,
  IN     UINT32          PbaNumberSize
)
{
  if (e1000_read_pba_string (&GigAdapter->hw, PbaNumber, PbaNumberSize) == E1000_SUCCESS) {
    return EFI_SUCCESS;
  } else {
    return EFI_DEVICE_ERROR;
  };
}

#endif

BOOLEAN
IsSurpriseRemoval(
        IN  GIG_DRIVER_DATA *Adapter
        )
{
#ifndef INTEL_KDNET
    UINT32  Results;

    if(Adapter->SurpriseRemoval == TRUE) {
        return TRUE;
    }

    MemoryFence ();

#ifndef INTEL_KDNET

    Adapter->PciIo->Mem.Read (
      Adapter->PciIo,
      EfiPciIoWidthUint32,
      0,
      E1000_STATUS,
      1,
      (VOID *) (&Results)
      );

#else

    ReadDeviceMemory((VOID *) (&Results),
                     1,
                     E1000_STATUS,
                     sizeof(UINT32),
                     1);

#endif

    MemoryFence ();

    if(Results == INVALID_STATUS_REGISTER_VALUE) {
        Adapter->SurpriseRemoval = TRUE;
        return TRUE;
    }
    return FALSE;
#else
    //
    // 2/2015, merged in IsSupriseRemoval from bootrom code, but disabled
    // IsSurpriseRemoval for kdnet.  On Powerville with IsSupriseRemoval
    // enabled, kdnet would make an initial connection with debugger but then
    // hard lock up the windows boot.
    //
    return FALSE;
#endif
}
