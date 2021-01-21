/**************************************************************************

Copyright (c) 2001-2016, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

#include "e1000.h"
#ifndef INTEL_KDNET
#include <Uefi\UEfiPxe.h>
#else
#include "EfiPxe.h"
#include "DeviceSupport.h"
#endif

//
// Global variables defined outside this file
//
extern PXE_SW_UNDI            *e1000_pxe_31;
extern UNDI_PRIVATE_DATA  *e1000_UNDI32DeviceList[MAX_NIC_INTERFACES];

//
// Global variables defined in this file
//
UNDI_CALL_TABLE               e1000_api_table[PXE_OPCODE_LAST_VALID + 1] = {
  {
    PXE_CPBSIZE_NOT_USED,
    PXE_DBSIZE_NOT_USED,
    0,
    (UINT16) (ANY_STATE),
    e1000_UNDI_GetState
  },
  {
    (UINT16) (DONT_CHECK),
    PXE_DBSIZE_NOT_USED,
    0,
    (UINT16) (ANY_STATE),
    e1000_UNDI_Start
  },
  {
    PXE_CPBSIZE_NOT_USED,
    PXE_DBSIZE_NOT_USED,
    0,
    MUST_BE_STARTED,
    e1000_UNDI_Stop
  },
  {
    PXE_CPBSIZE_NOT_USED,
    sizeof (PXE_DB_GET_INIT_INFO),
    0,
    MUST_BE_STARTED,
    e1000_UNDI_GetInitInfo
  },
  {
    PXE_CPBSIZE_NOT_USED,
    sizeof (PXE_DB_GET_CONFIG_INFO),
    0,
    MUST_BE_STARTED,
    e1000_UNDI_GetConfigInfo
  },
  {
    sizeof (PXE_CPB_INITIALIZE),
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    MUST_BE_STARTED,
    e1000_UNDI_Initialize
  },
  {
    PXE_CPBSIZE_NOT_USED,
    PXE_DBSIZE_NOT_USED,
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_Reset
  },
  {
    PXE_CPBSIZE_NOT_USED,
    PXE_DBSIZE_NOT_USED,
    0,
    MUST_BE_INITIALIZED,
    e1000_UNDI_Shutdown
  },
  {
    PXE_CPBSIZE_NOT_USED,
    PXE_DBSIZE_NOT_USED,
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_Interrupt
  },
  {
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_RecFilter
  },
  {
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_StnAddr
  },
  {
    PXE_CPBSIZE_NOT_USED,
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_Statistics
  },
  {
    sizeof (PXE_CPB_MCAST_IP_TO_MAC),
    sizeof (PXE_DB_MCAST_IP_TO_MAC),
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_ip2mac
  },
  {
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_NVData
  },
  {
    PXE_CPBSIZE_NOT_USED,
    (UINT16) (DONT_CHECK),
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_Status
  },
  {
    (UINT16) (DONT_CHECK),
    PXE_DBSIZE_NOT_USED,
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_FillHeader
  },
  {
    (UINT16) (DONT_CHECK),
    PXE_DBSIZE_NOT_USED,
    (UINT16) (DONT_CHECK),
    MUST_BE_INITIALIZED,
    e1000_UNDI_Transmit
  },
  {
    sizeof (PXE_CPB_RECEIVE),
    sizeof (PXE_DB_RECEIVE),
    0,
    MUST_BE_INITIALIZED,
    e1000_UNDI_Receive
  }
};

//
// External Functions
//
extern
UINTN
e1000_Reset (
  GIG_DRIVER_DATA *GigAdapter,
  UINT16          Opflags
  );

extern
UINTN
e1000_Shutdown (
  GIG_DRIVER_DATA *GigAdapter
  );

extern
UINTN
e1000_SetFilter (
  GIG_DRIVER_DATA *GigAdapter,
  UINT16          NewFilter,
  UINT64          cpb,
  UINT32          cpbsize
  );

extern
int
e1000_Statistics (
  GIG_DRIVER_DATA *GigAdapter,
  UINT64          db,
  UINT16          dbsize
  );

extern
BOOLEAN
e1000_WaitForAutoNeg (
  IN GIG_DRIVER_DATA *GigAdapter
  );

extern
int
e1000_SetInterruptState (
  GIG_DRIVER_DATA *GigAdapter
  );

VOID
e1000_UNDI_GetState (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine determines the operational state of the UNDI.  It updates the state flags in the
  Command Descriptor Block based on information derived from the AdapterInfo instance data.

  To ensure the command has completed successfully, CdbPtr->StatCode will contain the result of
  the command execution.

  The CdbPtr->StatFlags will contain a STOPPED, STARTED, or INITIALIZED state once the command
  has successfully completed.

  Keep in mind the AdapterInfo->State is the active state of the adapter (based on software
  interrogation), and the CdbPtr->StateFlags is the passed back information that is reflected
  to the caller of the UNDI API.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  DEBUGPRINT(DECODE, ("e1000_UNDI_GetState\n"));
  DEBUGWAIT(DECODE);

  CdbPtr->StatFlags |= GigAdapter->State;
  CdbPtr->StatFlags |= PXE_STATFLAGS_COMMAND_COMPLETE;

  CdbPtr->StatCode = PXE_STATCODE_SUCCESS;

  return ;
}

VOID
e1000_UNDI_Start (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to change the operational state of the Gigabit UNDI from stopped to started.
  It will do this as long as the adapter's state is PXE_STATFLAGS_GET_STATE_STOPPED, otherwise
  the CdbPtr->StatFlags will reflect a command failure, and the CdbPtr->StatCode will reflect the
  UNDI as having already been started.

  This routine is modified to reflect the undi 1.1 specification changes. The
  changes in the spec are mainly in the callback routines, the new spec adds
  3 more callbacks and a unique id.
  Since this UNDI supports both old and new undi specifications,
  The NIC's data structure is filled in with the callback routines (depending
  on the version) pointed to in the caller's CpbPtr.  This seeds the Delay,
  Virt2Phys, Block, and Mem_IO for old and new versions and Map_Mem, UnMap_Mem
  and Sync_Mem routines and a unique id variable for the new version.
  This is the function which an external entity (SNP, O/S, etc) would call
  to provide it's I/O abstraction to the UNDI.

  It's final action is to change the AdapterInfo->State to PXE_STATFLAGS_GET_STATE_STARTED.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  PXE_CPB_START_31  *CpbPtr_31;

  DEBUGPRINT(DECODE, ("e1000_UNDI_Start\n"));
  DEBUGWAIT(DECODE);

  //
  // check if it is already started.
  //
  if (GigAdapter->State != PXE_STATFLAGS_GET_STATE_STOPPED) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_ALREADY_STARTED;
    return ;
  }

#ifndef INTEL_KDNET
  if (CdbPtr->CPBsize != sizeof (PXE_CPB_START_30) && CdbPtr->CPBsize != sizeof (PXE_CPB_START_31)) {
#else
  if (CdbPtr->CPBsize != sizeof (PXE_CPB_START) && CdbPtr->CPBsize != sizeof (PXE_CPB_START_31)) {
#endif
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  CpbPtr_31 = (PXE_CPB_START_31 *) (UINTN) (CdbPtr->CPBaddr);

  GigAdapter->Delay     = (bsptr) (UINTN) CpbPtr_31->Delay;
  GigAdapter->Virt2Phys = (virtphys) (UINTN) CpbPtr_31->Virt2Phys;
  GigAdapter->Block     = (block) (UINTN) CpbPtr_31->Block;
  GigAdapter->MemIo     = (mem_io) (UINTN) CpbPtr_31->Mem_IO;
  GigAdapter->MapMem    = (map_mem) (UINTN) CpbPtr_31->Map_Mem;
  GigAdapter->UnMapMem  = (unmap_mem) (UINTN) CpbPtr_31->UnMap_Mem;
  GigAdapter->SyncMem   = (sync_mem) (UINTN) CpbPtr_31->Sync_Mem;
  GigAdapter->Unique_ID = CpbPtr_31->Unique_ID;
  DEBUGPRINT(DECODE, ("CpbPtr_31->Unique_ID = %x\n", CpbPtr_31->Unique_ID));

  GigAdapter->State = PXE_STATFLAGS_GET_STATE_STARTED;

  CdbPtr->StatFlags     = PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode      = PXE_STATCODE_SUCCESS;

  return ;
}

VOID
e1000_UNDI_Stop (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to change the operational state of the UNDI from started to stopped.
  It will not do this if the adapter's state is PXE_STATFLAGS_GET_STATE_INITIALIZED, otherwise
  the CdbPtr->StatFlags will reflect a command failure, and the CdbPtr->StatCode will reflect the
  UNDI as having already not been shut down.

  The NIC's data structure will have the Delay, Virt2Phys, and Block, pointers zero'd out..

  It's final action is to change the AdapterInfo->State to PXE_STATFLAGS_GET_STATE_STOPPED.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{

  DEBUGPRINT(DECODE, ("e1000_UNDI_Stop\n"));
  DEBUGWAIT(DECODE);

  if (GigAdapter->State == PXE_STATFLAGS_GET_STATE_INITIALIZED) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_NOT_SHUTDOWN;
    return ;
  }

  GigAdapter->Delay         = 0;
  GigAdapter->Virt2Phys     = 0;
  GigAdapter->Block         = 0;

  GigAdapter->MapMem        = 0;
  GigAdapter->UnMapMem      = 0;
  GigAdapter->SyncMem       = 0;

  GigAdapter->State         = PXE_STATFLAGS_GET_STATE_STOPPED;

  CdbPtr->StatFlags             = PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode              = PXE_STATCODE_SUCCESS;

  return ;
}

VOID
e1000_UNDI_GetInitInfo (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to retrieve the initialization information that is needed by drivers and
  applications to initialize the UNDI.  This will fill in data in the Data Block structure that is
  pointed to by the caller's CdbPtr->DBaddr.  The fields filled in are as follows:

  MemoryRequired, FrameDataLen, LinkSpeeds[0-3], NvCount, NvWidth, MediaHeaderLen, HWaddrLen,
  MCastFilterCnt, TxBufCnt, TxBufSize, RxBufCnt, RxBufSize, IFtype, Duplex, and LoopBack.

  In addition, the CdbPtr->StatFlags ORs in that this NIC supports cable detection.  (APRIORI knowledge)

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  PXE_DB_GET_INIT_INFO  *DbPtr;

  DEBUGPRINT(DECODE, ("e1000_UNDI_GetInitInfo\n"));
  DEBUGWAIT(DECODE);

  DbPtr                 = (PXE_DB_GET_INIT_INFO *) (UINTN) (CdbPtr->DBaddr);

  DbPtr->MemoryRequired = 0;
  DbPtr->FrameDataLen   = PXE_MAX_TXRX_UNIT_ETHER;
  //
  // First check for FIBER, Links are 1000,0,0,0
  //
  if (GigAdapter->hw.phy.media_type == e1000_media_type_copper ) {
    DbPtr->LinkSpeeds[0]  = 10;
    DbPtr->LinkSpeeds[1]  = 100;
    DbPtr->LinkSpeeds[2]  = 1000;
    DbPtr->LinkSpeeds[3]  = 0;
  } else {
    DbPtr->LinkSpeeds[0]  = 1000;
    DbPtr->LinkSpeeds[1]  = 0;
    DbPtr->LinkSpeeds[2]  = 0;
    DbPtr->LinkSpeeds[3]  = 0;
  }

  DbPtr->NvCount        = MAX_EEPROM_LEN;
  DbPtr->NvWidth        = 4;
  DbPtr->MediaHeaderLen = PXE_MAC_HEADER_LEN_ETHER;
  DbPtr->HWaddrLen      = PXE_HWADDR_LEN_ETHER;
  DbPtr->MCastFilterCnt = MAX_MCAST_ADDRESS_CNT;

  DbPtr->TxBufCnt       = DEFAULT_TX_DESCRIPTORS;
  DbPtr->TxBufSize      = sizeof (E1000_TRANSMIT_DESCRIPTOR);
  DbPtr->RxBufCnt       = DEFAULT_RX_DESCRIPTORS;
  DbPtr->RxBufSize      = sizeof (E1000_RECEIVE_DESCRIPTOR) + sizeof (LOCAL_RX_BUFFER);

  DbPtr->IFtype         = PXE_IFTYPE_ETHERNET;
#ifndef INTEL_KDNET
  DbPtr->SupportedDuplexModes         = PXE_DUPLEX_ENABLE_FULL_SUPPORTED | PXE_DUPLEX_FORCE_FULL_SUPPORTED;
  DbPtr->SupportedLoopBackModes       = 0;
#else
  DbPtr->Duplex         = PXE_DUPLEX_ENABLE_FULL_SUPPORTED | PXE_DUPLEX_FORCE_FULL_SUPPORTED;
  DbPtr->LoopBack       = 0;
#endif

  CdbPtr->StatFlags |= (PXE_STATFLAGS_CABLE_DETECT_SUPPORTED |
                        PXE_STATFLAGS_GET_STATUS_NO_MEDIA_SUPPORTED);

  CdbPtr->StatFlags |= PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode = PXE_STATCODE_SUCCESS;

#ifndef INTEL_KDNET
  if (GigAdapter->UNDIEnabled == FALSE) {
      CdbPtr->StatCode = PXE_STATCODE_BUSY;
  }
#endif

  return ;
}

VOID
e1000_UNDI_GetConfigInfo (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to retrieve the configuration information about the NIC being controlled by
  this driver.  This will fill in data in the Data Block structure that is pointed to by the caller's CdbPtr->DBaddr.
  The fields filled in are as follows:

  DbPtr->pci.BusType, DbPtr->pci.Bus, DbPtr->pci.Device, and DbPtr->pci.

  In addition, the DbPtr->pci.Config.Dword[0-63] grabs a copy of this NIC's PCI configuration space.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  PXE_DB_GET_CONFIG_INFO  *DbPtr;

  DEBUGPRINT(DECODE, ("e1000_UNDI_GetConfigInfo\n"));
  DEBUGWAIT(DECODE);

  DbPtr               = (PXE_DB_GET_CONFIG_INFO *) (UINTN) (CdbPtr->DBaddr);

  DbPtr->pci.BusType  = PXE_BUSTYPE_PCI;
  DbPtr->pci.Bus      = (UINT16) GigAdapter->Bus;
  DbPtr->pci.Device   = (UINT8) GigAdapter->Device;
  DbPtr->pci.Function = (UINT8) GigAdapter->Function;
  DEBUGPRINT(DECODE, (
    "Bus %x, Device %x, Function %x\n",
    GigAdapter->Bus,
    GigAdapter->Device,
    GigAdapter->Function
    ));

  CopyMem (DbPtr->pci.Config.Dword, &GigAdapter->PciConfig, MAX_PCI_CONFIG_LEN * sizeof (UINT32));

  CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode = PXE_STATCODE_SUCCESS;

  return ;
}

VOID
e1000_UNDI_Initialize (
  IN  PXE_CDB     *CdbPtr,
  GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine resets the network adapter and initializes the Gigabit UNDI using the parameters
  supplied in the CPB.  This command must be issued before the network adapter can be setup to
  transmit and receive packets.

  Once the memory requirements of the UNDI are obtained by using the GetInitInfo command, a block
  of non-swappable memory may need to be allocated.  The address of this memory must be passed to
  UNDI during the Initialize in the CPB.  This memory is used primarily for transmit and receive buffers.

  The fields CableDetect, LinkSpeed, Duplex, LoopBack, MemoryPtr, and MemoryLength are set with
  information that was passed in the CPB and the NIC is initialized.

  If the NIC initialization fails, the CdbPtr->StatFlags are updated with PXE_STATFLAGS_COMMAND_FAILED
  Otherwise, AdapterInfo->State is updated with PXE_STATFLAGS_GET_STATE_INITIALIZED showing the state of
  the UNDI is now initialized.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  AdapterInfo       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  PXE_CPB_INITIALIZE  *CpbPtr;
  PXE_DB_INITIALIZE   *DbPtr;
#ifdef INTEL_KDNET
  UINT16 Speed;
  UINT16 Duplex;
#endif

  DEBUGPRINT(DECODE, ("e1000_UNDI_Initialize\n"));
  DEBUGWAIT(DECODE);

#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_Initialize called when driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  if ((CdbPtr->OpFlags != PXE_OPFLAGS_INITIALIZE_DETECT_CABLE) &&
      (CdbPtr->OpFlags != PXE_OPFLAGS_INITIALIZE_DO_NOT_DETECT_CABLE)
      ) {
    DEBUGPRINT(CRITICAL, ("INVALID CDB\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  //
  // Check if it is already initialized
  //
  if (GigAdapter->State == PXE_STATFLAGS_GET_STATE_INITIALIZED) {
    DEBUGPRINT(DECODE, ("ALREADY INITIALIZED\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_ALREADY_INITIALIZED;
    return ;
  }

  CpbPtr  = (PXE_CPB_INITIALIZE *) (UINTN) CdbPtr->CPBaddr;
  DbPtr   = (PXE_DB_INITIALIZE *) (UINTN) CdbPtr->DBaddr;

  //
  // Default behaviour is to detect the cable, if the 3rd param is 1,
  // do not do that
  //
  GigAdapter->CableDetect = (UINT8) ((CdbPtr->OpFlags == (UINT16) PXE_OPFLAGS_INITIALIZE_DO_NOT_DETECT_CABLE) ? (UINT8) 0 : (UINT8) 1);
  DEBUGPRINT(DECODE, ("CdbPtr->OpFlags = %X\n", CdbPtr->OpFlags));
  GigAdapter->LinkSpeed     = (UINT16) CpbPtr->LinkSpeed;
#ifndef INTEL_KDNET
  GigAdapter->DuplexMode    = CpbPtr->DuplexMode;
  GigAdapter->LoopBack      = CpbPtr->LoopBackMode;
#else
  GigAdapter->DuplexMode    = CpbPtr->Duplex;
  GigAdapter->LoopBack      = CpbPtr->LoopBack;
#endif

  DEBUGPRINT(DECODE, ("CpbPtr->TxBufCnt = %X\n", CpbPtr->TxBufCnt));
  DEBUGPRINT(DECODE, ("CpbPtr->TxBufSize = %X\n", CpbPtr->TxBufSize));
  DEBUGPRINT(DECODE, ("CpbPtr->RxBufCnt = %X\n", CpbPtr->RxBufCnt));
  DEBUGPRINT(DECODE, ("CpbPtr->RxBufSize = %X\n", CpbPtr->RxBufSize));

  if (GigAdapter->CableDetect != 0) {
    DEBUGPRINT(DECODE, ("Setting wait_autoneg_complete\n"));
    GigAdapter->hw.phy.autoneg_wait_to_complete = TRUE;
  } else {
    GigAdapter->hw.phy.autoneg_wait_to_complete = FALSE;
  }

  CdbPtr->StatCode = (PXE_STATCODE) e1000_Initialize (GigAdapter);

  //
  // We allocate our own memory for transmit and receive so set MemoryUsed to 0.
  //
  DbPtr->MemoryUsed = 0;
  DbPtr->TxBufCnt   = DEFAULT_TX_DESCRIPTORS;
  DbPtr->TxBufSize  = sizeof (E1000_TRANSMIT_DESCRIPTOR);
  DbPtr->RxBufCnt   = DEFAULT_RX_DESCRIPTORS;
  DbPtr->RxBufSize  = sizeof (E1000_RECEIVE_DESCRIPTOR) + sizeof (LOCAL_RX_BUFFER);

  if (CdbPtr->StatCode != PXE_STATCODE_SUCCESS) {
    DEBUGPRINT(CRITICAL, ("e1000_Initialize failed! Statcode = %X\n", CdbPtr->StatCode));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
  } else {
    GigAdapter->State = PXE_STATFLAGS_GET_STATE_INITIALIZED;
  }

  //
  // If no link is detected we want to set the driver state back to _GET_STATE_STARTED so
  // that the SNP will not try to restart the driver.
  //
  if (e1000_WaitForAutoNeg (GigAdapter) == TRUE) {
#ifdef INTEL_KDNET
    if (e1000_get_speed_and_duplex(&GigAdapter->hw, &Speed, &Duplex) == E1000_SUCCESS) {
      DbPtr->LinkSpeed = Speed;
      DbPtr->LinkDuplex = Duplex;
    }
#endif

    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE;
  } else {
    CdbPtr->StatFlags |= PXE_STATFLAGS_INITIALIZED_NO_MEDIA;
    CdbPtr->StatCode = PXE_STATCODE_NOT_STARTED;
    GigAdapter->State = PXE_STATFLAGS_GET_STATE_STARTED;
  }

  GigAdapter->hw.mac.get_link_status = TRUE;

  return ;
}

VOID
e1000_UNDI_Reset (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine resets the network adapter and initializes the Gigabit UNDI using the
  parameters supplied in the CPB.  The transmit and receive queues are emptied and any
  pending interrupts are cleared.

  If the NIC reset fails, the CdbPtr->StatFlags are updated with PXE_STATFLAGS_COMMAND_FAILED

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver
                      is layering on..

Returns:
  None

--*/
{
  DEBUGPRINT(DECODE, ("e1000_UNDI_Reset\n"));
  DEBUGWAIT(DECODE);

#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_Reset called when driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  if (CdbPtr->OpFlags != PXE_OPFLAGS_NOT_USED &&
      CdbPtr->OpFlags != PXE_OPFLAGS_RESET_DISABLE_INTERRUPTS &&
      CdbPtr->OpFlags != PXE_OPFLAGS_RESET_DISABLE_FILTERS
      ) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  CdbPtr->StatCode = PXE_STATCODE_SUCCESS; //(UINT16) e1000_Reset (GigAdapter, CdbPtr->OpFlags);

  if (CdbPtr->StatCode != PXE_STATCODE_SUCCESS) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
  } else {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE;
  }
}

VOID
e1000_UNDI_Shutdown (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine resets the network adapter and leaves it in a safe state for another driver to
  initialize.  Any pending transmits or receives are lost.  Receive filters and external
  interrupt enables are disabled.  Once the UNDI has been shutdown, it can then be stopped
  or initialized again.

  If the NIC reset fails, the CdbPtr->StatFlags are updated with PXE_STATFLAGS_COMMAND_FAILED

  Otherwise, GigAdapter->State is updated with PXE_STATFLAGS_GET_STATE_STARTED showing
  the state of the NIC as being started.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  //
  // do the shutdown stuff here
  //
  DEBUGPRINT(DECODE, ("e1000_UNDI_Shutdown\n"));
  DEBUGWAIT(DECODE);

#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_Shutdown called when driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  CdbPtr->StatCode =  (UINT16) e1000_Shutdown (GigAdapter);

  if (CdbPtr->StatCode != PXE_STATCODE_SUCCESS) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
  } else {
    GigAdapter->State = PXE_STATFLAGS_GET_STATE_STARTED;
    CdbPtr->StatFlags     = PXE_STATFLAGS_COMMAND_COMPLETE;
  }

  return ;
}

VOID
e1000_UNDI_Interrupt (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine can be used to read and/or change the current external interrupt enable
  settings.  Disabling an external interrupt enable prevents and external (hardware)
  interrupt from being signaled by the network device.  Internally the interrupt events
  can still be polled by using the UNDI_GetState command.

  The resulting information on the interrupt state will be passed back in the CdbPtr->StatFlags.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on.

Returns:
  None

--*/
{
  UINT8 IntMask;

  DEBUGPRINT(DECODE, ("e1000_UNDI_Interrupt\n"));

  IntMask = (UINT8) (UINTN)
    (
      CdbPtr->OpFlags &
        (
          PXE_OPFLAGS_INTERRUPT_RECEIVE |
          PXE_OPFLAGS_INTERRUPT_TRANSMIT |
          PXE_OPFLAGS_INTERRUPT_COMMAND |
          PXE_OPFLAGS_INTERRUPT_SOFTWARE
        )
    );

  switch (CdbPtr->OpFlags & PXE_OPFLAGS_INTERRUPT_OPMASK) {
  case PXE_OPFLAGS_INTERRUPT_READ:
    break;

  case PXE_OPFLAGS_INTERRUPT_ENABLE:
    if (IntMask == 0) {
      CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
      CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
      return ;
    }

    GigAdapter->int_mask = IntMask;
    e1000_SetInterruptState (GigAdapter);
    break;

  case PXE_OPFLAGS_INTERRUPT_DISABLE:
    if (IntMask != 0) {
      GigAdapter->int_mask &= ~(IntMask);
      e1000_SetInterruptState (GigAdapter);
      break;
    }

  //
  // else fall thru.
  //
  default:
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  if ((GigAdapter->int_mask & PXE_OPFLAGS_INTERRUPT_RECEIVE) != 0) {
    CdbPtr->StatFlags |= PXE_STATFLAGS_INTERRUPT_RECEIVE;
  }

  if ((GigAdapter->int_mask & PXE_OPFLAGS_INTERRUPT_TRANSMIT) != 0) {
    CdbPtr->StatFlags |= PXE_STATFLAGS_INTERRUPT_TRANSMIT;
  }

  if ((GigAdapter->int_mask & PXE_OPFLAGS_INTERRUPT_COMMAND) != 0) {
    CdbPtr->StatFlags |= PXE_STATFLAGS_INTERRUPT_COMMAND;
  }

  return ;
}

VOID
e1000_UNDI_RecFilter (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to read and change receive filters and, if supported, read
  and change multicast MAC address filter list.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI
                     driver is layering on..

Returns:
  None

--*/
{

  UINT16                  NewFilter;
  UINT16                  OpFlags;
  PXE_DB_RECEIVE_FILTERS  *DbPtr;

  DEBUGPRINT(DECODE, ("e1000_UNDI_RecFilter\n"));

#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_RecFilter called when driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  OpFlags = CdbPtr->OpFlags;
  NewFilter = (UINT16) (OpFlags & 0x1F);

  switch (OpFlags & PXE_OPFLAGS_RECEIVE_FILTER_OPMASK) {
  case PXE_OPFLAGS_RECEIVE_FILTER_READ:
    //
    // not expecting a cpb, not expecting any filter bits
    //
    if ((NewFilter != 0) || (CdbPtr->CPBsize != 0)) {
      goto BadCdb;
    }

    if ((NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_RESET_MCAST_LIST) == 0) {
      goto JustRead;
    }

    NewFilter |= GigAdapter->Rx_Filter;

    //
    // all other flags are ignored except mcast_reset
    //
    break;

  case PXE_OPFLAGS_RECEIVE_FILTER_ENABLE:
    //
    // there should be atleast one other filter bit set.
    //
    if (NewFilter == 0) {
      //
      // nothing to enable
      //
      goto BadCdb;
    }

    if (CdbPtr->CPBsize != 0) {
      //
      // this must be a multicast address list!
      // don't accept the list unless selective_mcast is set
      // don't accept confusing mcast settings with this
      //
      if (((NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST) == 0) ||
          ((NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_RESET_MCAST_LIST) != 0) ||
          ((NewFilter & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) != 0)
          ) {
        goto BadCdb;
      }
    }

    //
    // check selective mcast case enable case
    //
    if ((OpFlags & PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST) != 0) {
      if (((OpFlags & PXE_OPFLAGS_RECEIVE_FILTER_RESET_MCAST_LIST) != 0) ||
          ((OpFlags & PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST) != 0)
          ) {
        goto BadCdb;

      }

      //
      // if no cpb, make sure we have an old list
      //
      if ((CdbPtr->CPBsize == 0) && (GigAdapter->McastList.Length == 0)) {
        goto BadCdb;
      }
    }

    //
    // if you want to enable anything, you got to have unicast
    // and you have what you already enabled!
    //
    NewFilter |= (PXE_OPFLAGS_RECEIVE_FILTER_UNICAST | GigAdapter->Rx_Filter);

    break;

  case PXE_OPFLAGS_RECEIVE_FILTER_DISABLE:
    //
    // mcast list not expected, i.e. no cpb here!
    //
    if (CdbPtr->CPBsize != PXE_CPBSIZE_NOT_USED) {
      goto BadCdb;  // db with all_multi??
    }

    NewFilter = (UINT16) ((~(CdbPtr->OpFlags & 0x1F)) & GigAdapter->Rx_Filter);

    break;

  default:
    goto BadCdb;
  }

  if ((OpFlags & PXE_OPFLAGS_RECEIVE_FILTER_RESET_MCAST_LIST) != 0) {
    GigAdapter->McastList.Length = 0;
    NewFilter &= (~PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST);
  }

  e1000_SetFilter (GigAdapter, NewFilter, CdbPtr->CPBaddr, CdbPtr->CPBsize);

JustRead:
  DEBUGPRINT(DECODE, ("Read current filter\n"));
  //
  // give the current mcast list
  //
  if ((CdbPtr->DBsize != 0) && (GigAdapter->McastList.Length != 0)) {
    //
    // copy the mc list to db
    //
    UINT16  i;
    UINT16  copy_len;
    UINT8   *ptr1;
    UINT8   *ptr2;

    DbPtr = (PXE_DB_RECEIVE_FILTERS *) (UINTN) CdbPtr->DBaddr;
    ptr1  = (UINT8 *) (&DbPtr->MCastList[0]);

    copy_len = (UINT16) (GigAdapter->McastList.Length * PXE_MAC_LENGTH);

    if (copy_len > CdbPtr->DBsize) {
      copy_len = CdbPtr->DBsize;

    }

    ptr2 = (UINT8 *) (&GigAdapter->McastList.McAddr[0]);
    for (i = 0; i < copy_len; i++) {
      ptr1[i] = ptr2[i];
    }
  }

  //
  // give the stat flags here
  //
  if (GigAdapter->ReceiveStarted) {
    CdbPtr->StatFlags |= (GigAdapter->Rx_Filter | PXE_STATFLAGS_COMMAND_COMPLETE);
  }

  return ;

BadCdb:
  DEBUGPRINT(CRITICAL, ("ERROR: Bad CDB!\n"));
  CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
  CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
  return ;
}

VOID
e1000_UNDI_StnAddr (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to get the current station and broadcast MAC addresses, and to change the
  current station MAC address.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver
                      is layering on.

Returns:
  None

--*/
{

  PXE_CPB_STATION_ADDRESS *CpbPtr;
  PXE_DB_STATION_ADDRESS  *DbPtr;
  UINT16                  i;

  DbPtr = NULL;
  DEBUGPRINT(DECODE, ("e1000_UNDI_StnAddr\n"));

  if (CdbPtr->OpFlags == PXE_OPFLAGS_STATION_ADDRESS_RESET) {
    //
    // configure the permanent address.
    // change the AdapterInfo->CurrentNodeAddress field.
    //
    if (CompareMem (
          GigAdapter->hw.mac.addr,
          GigAdapter->hw.mac.perm_addr,
          PXE_HWADDR_LEN_ETHER
          ) != 0) {
      CopyMem (
        GigAdapter->hw.mac.addr,
        GigAdapter->hw.mac.perm_addr,
        PXE_HWADDR_LEN_ETHER
        );
      e1000_rar_set (&GigAdapter->hw, GigAdapter->hw.mac.addr, 0);
    }
  }

  if (CdbPtr->CPBaddr != (UINT64) 0) {
    CpbPtr = (PXE_CPB_STATION_ADDRESS *) (UINTN) (CdbPtr->CPBaddr);
    GigAdapter->MacAddrOverride = TRUE;

    //
    // configure the new address
    //
    CopyMem (
      GigAdapter->hw.mac.addr,
      CpbPtr->StationAddr,
      PXE_HWADDR_LEN_ETHER
      );

    DEBUGPRINT(DECODE, ("Reassigned address:\n"));
    for (i = 0; i < 6; i++) {
      DEBUGPRINT(DECODE, ("%2x ", CpbPtr->StationAddr[i]));
    }

    e1000_rar_set (&GigAdapter->hw, GigAdapter->hw.mac.addr, 0);
  }

  if (CdbPtr->DBaddr != (UINT64) 0) {
    DbPtr = (PXE_DB_STATION_ADDRESS *) (UINTN) (CdbPtr->DBaddr);

    //
    // fill it with the new values
    //
    ZeroMem (DbPtr->StationAddr, PXE_MAC_LENGTH);
    ZeroMem (DbPtr->PermanentAddr, PXE_MAC_LENGTH);
    ZeroMem (DbPtr->BroadcastAddr, PXE_MAC_LENGTH);
    CopyMem (DbPtr->StationAddr, GigAdapter->hw.mac.addr, PXE_HWADDR_LEN_ETHER);
    CopyMem (DbPtr->PermanentAddr, GigAdapter->hw.mac.perm_addr, PXE_HWADDR_LEN_ETHER);
    CopyMem (DbPtr->BroadcastAddr, GigAdapter->BroadcastNodeAddress, PXE_MAC_LENGTH);
  }

  DEBUGPRINT(DECODE, ("DbPtr->BroadcastAddr ="));
  for (i = 0; i < PXE_MAC_LENGTH; i++) {
    DEBUGPRINT(DECODE, (" %x", DbPtr->BroadcastAddr[i]));
  }

  DEBUGPRINT(DECODE, ("\n"));
  DEBUGWAIT(DECODE);
  CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode  = PXE_STATCODE_SUCCESS;
  return ;
}

VOID
e1000_UNDI_Statistics (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to read and clear the NIC traffic statistics.  This command is supported only
  if the !PXE structure's Implementation flags say so.

  Results will be parsed out in the following manner:
  CdbPtr->DBaddr.Data[0]   R  Total Frames (Including frames with errors and dropped frames)
  CdbPtr->DBaddr.Data[1]   R  Good Frames (All frames copied into receive buffer)
  CdbPtr->DBaddr.Data[2]   R  Undersize Frames (Frames below minimum length for media <64 for ethernet)
  CdbPtr->DBaddr.Data[4]   R  Dropped Frames (Frames that were dropped because receive buffers were full)
  CdbPtr->DBaddr.Data[8]   R  CRC Error Frames (Frames with alignment or CRC errors)
  CdbPtr->DBaddr.Data[A]   T  Total Frames (Including frames with errors and dropped frames)
  CdbPtr->DBaddr.Data[B]   T  Good Frames (All frames copied into transmit buffer)
  CdbPtr->DBaddr.Data[C]   T  Undersize Frames (Frames below minimum length for media <64 for ethernet)
  CdbPtr->DBaddr.Data[E]   T  Dropped Frames (Frames that were dropped because of collisions)
  CdbPtr->DBaddr.Data[14]  T  Total Collision Frames (Total collisions on this subnet)

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  AdapterInfo       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
// GC_TODO:    GigAdapter - add argument and description to function comment
{
  DEBUGPRINT(DECODE, ("e1000_UNDI_Statistics\n"));

  if ((CdbPtr->OpFlags &~(PXE_OPFLAGS_STATISTICS_RESET)) != 0) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode  = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  if ((CdbPtr->OpFlags & PXE_OPFLAGS_STATISTICS_RESET) != 0) {
    //
    // Reset the statistics
    //
    CdbPtr->StatCode = (UINT16) e1000_Statistics (GigAdapter, 0, 0);
  } else {
    CdbPtr->StatCode = (UINT16) e1000_Statistics (GigAdapter, CdbPtr->DBaddr, CdbPtr->DBsize);
  }

  return ;
}

VOID
e1000_UNDI_ip2mac (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to translate a multicast IP address to a multicast MAC address.

  This results in a MAC address composed of 25 bits of fixed data with the upper 23 bits of the IP
  address being appended to it.  Results passed back in the equivalent of CdbPtr->DBaddr->MAC[0-5].

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  PXE_CPB_MCAST_IP_TO_MAC *CpbPtr;
  PXE_DB_MCAST_IP_TO_MAC  *DbPtr;
  UINT32                  IPAddr;
  UINT8                   *TmpPtr;

  CpbPtr  = (PXE_CPB_MCAST_IP_TO_MAC *) (UINTN) CdbPtr->CPBaddr;
  DbPtr   = (PXE_DB_MCAST_IP_TO_MAC *) (UINTN) CdbPtr->DBaddr;

  DEBUGPRINT(DECODE, ("e1000_UNDI_ip2mac\n"));

  if ((CdbPtr->OpFlags & PXE_OPFLAGS_MCAST_IPV6_TO_MAC) != 0) {
#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER
#ifdef INTEL_KDNET
    if (IsServer1GDevice(GigAdapter))
#endif
	{
    UINT8 *IPv6Ptr;

    IPv6Ptr    = (UINT8 *)&CpbPtr->IP.IPv6;

    DbPtr->MAC[0] = 0x33;
    DbPtr->MAC[1] = 0x33;
    DbPtr->MAC[2] = *(IPv6Ptr + 12);
    DbPtr->MAC[3] = *(IPv6Ptr + 13);
    DbPtr->MAC[4] = *(IPv6Ptr + 14);
    DbPtr->MAC[5] = *(IPv6Ptr + 15);
    return;
	}
#endif
#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
#ifdef INTEL_KDNET
    if (IsClient1GDevice(GigAdapter))
#endif
	{
    //
    // for now this is not supported
    //
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode  = PXE_STATCODE_UNSUPPORTED;
    return ;
	}
#endif
  }

  //
  // Take the last 23 bits of IP to generate a multicase IP address.
  //
  IPAddr        = CpbPtr->IP.IPv4;
  TmpPtr        = (UINT8 *) (&IPAddr);

  DbPtr->MAC[0] = 0x01;
  DbPtr->MAC[1] = 0x00;
  DbPtr->MAC[2] = 0x5e;
  DbPtr->MAC[3] = (UINT8) (TmpPtr[1] & 0x7f);
  DbPtr->MAC[4] = (UINT8) TmpPtr[2];
  DbPtr->MAC[5] = (UINT8) TmpPtr[3];

  return ;
}

VOID
e1000_UNDI_NVData (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to read and write non-volatile storage on the NIC (if supported).  The NVRAM
  could be EEPROM, FLASH, or battery backed RAM.

  This is an optional function according to the UNDI specification  (or will be......)

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
  PXE_DB_NVDATA       *DbPtr;
  PXE_CPB_NVDATA_BULK *PxeCpbNvdata;
  UINT32              Result;

  DEBUGPRINT(DECODE, ("e1000_UNDI_NVData\n"));

  if ((GigAdapter->State != PXE_STATFLAGS_GET_STATE_STARTED) &&
      (GigAdapter->State != PXE_STATFLAGS_GET_STATE_INITIALIZED)
      ) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_NOT_STARTED;
    return ;
  }

  if ((CdbPtr->OpFlags == PXE_OPFLAGS_NVDATA_READ) != 0) {
    DbPtr = (PXE_DB_NVDATA *) (UINTN) CdbPtr->DBaddr;
    Result = e1000_read_nvm (&GigAdapter->hw, 0, 256, &DbPtr->Data.Word[0]);
  } else {
    //
    // Begin the write at word 40h so we do not overwrite any vital data
    // All data from address 00h to address CFh will be ignored
    //
    PxeCpbNvdata = (PXE_CPB_NVDATA_BULK *) (UINTN) CdbPtr->CPBaddr;
    Result        = e1000_write_nvm (&GigAdapter->hw, 0x40, 0xBF, &PxeCpbNvdata->Word[0x40]);
  }

  if (Result == E1000_SUCCESS) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE;
    CdbPtr->StatCode = PXE_STATCODE_SUCCESS;
  } else {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_SUCCESS;
  }

  return ;
}

VOID
e1000_UNDI_Status (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine returns the current interrupt status and/or the transmitted buffer addresses.
  If the current interrupt status is returned, pending interrupts will be acknowledged by this
  command.  Transmitted buffer addresses that are written to the DB are removed from the transmit
  buffer queue.

  Normally, this command would be polled with interrupts disabled.

  The transmit buffers are returned in CdbPtr->DBaddr->TxBufer[0 - NumEntries].
  The interrupt status is returned in CdbPtr->StatFlags.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the Gigabit UNDI
                      driver is layering on..

Returns:
  None

--*/
{
  PXE_DB_GET_STATUS         *DbPtr;
  UINT16                    Status;
  UINT16                    NumEntries;
  E1000_RECEIVE_DESCRIPTOR  *RxPtr;
#if (DBG_LVL&CRITICAL)
  UINT32                    Rdh;
  UINT32                    Rdt;
#endif

  DEBUGPRINT(DECODE, ("e1000_UNDI_Status\n"));

#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_Status called when driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  //
  // If the size of the DB is not large enough to store at least one 64 bit
  // complete transmit buffer address and size of the next available receive
  // packet we will return an error.  Per E.4.16 of the EFI spec the DB should
  // have enough space for at least 1 completed transmit buffer.
  //
  if (CdbPtr->DBsize < (sizeof (UINT64) * 2)) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    DEBUGPRINT(CRITICAL, ("Invalid CDB\n"));
    if ((CdbPtr->OpFlags & PXE_OPFLAGS_GET_TRANSMITTED_BUFFERS) != 0) {
      CdbPtr->StatFlags |= PXE_STATFLAGS_GET_STATUS_NO_TXBUFS_WRITTEN;
    }

    return ;
  }

  DbPtr = (PXE_DB_GET_STATUS *) (UINTN) CdbPtr->DBaddr;

  //
  // Fill in size of next available receive packet and
  // reserved field in caller's DB storage.
  //
  RxPtr = &GigAdapter->rx_ring[GigAdapter->cur_rx_ind];

#if (DBG_LVL&CRITICAL)
  if (RxPtr->buffer_addr != GigAdapter->DebugRxBuffer[GigAdapter->cur_rx_ind]) {
    DEBUGPRINT(CRITICAL, ("GetStatus ERROR: Rx buff mismatch on desc %d: expected %X, actual %X\n",
      GigAdapter->cur_rx_ind,
      GigAdapter->DebugRxBuffer[GigAdapter->cur_rx_ind],
      RxPtr->buffer_addr
      ));
  }

  Rdt = E1000_READ_REG (&GigAdapter->hw, E1000_RDT(0));
  Rdh = E1000_READ_REG (&GigAdapter->hw, E1000_RDH(0));
  if (Rdt == Rdh) {
    DEBUGPRINT(CRITICAL, ("GetStatus ERROR: RX Buffers Full!\n"));
  }
#endif

  if ((RxPtr->status & (E1000_RXD_STAT_EOP | E1000_RXD_STAT_DD)) != 0) {
    DEBUGPRINT(DECODE, ("Get Status->We have a Rx Frame at %x\n", GigAdapter->cur_rx_ind));
    DEBUGPRINT(DECODE, ("Frame length = %X\n", RxPtr->length));
    DbPtr->RxFrameLen = RxPtr->length;
    DbPtr->reserved = 0;
  } else {
    DbPtr->RxFrameLen = 0;
    DbPtr->reserved = 0;
  }

  //
  // Fill in the completed transmit buffer addresses so they can be freed by
  // the calling application or driver
  //
  if ((CdbPtr->OpFlags & PXE_OPFLAGS_GET_TRANSMITTED_BUFFERS) != 0) {
    //
    // Calculate the number of entries available in the DB to save the addresses
    // of completed transmit buffers.
    //
    NumEntries = (UINT16) ((CdbPtr->DBsize - sizeof (UINT64)) / sizeof (UINT64));
    DEBUGPRINT(DECODE, ("CdbPtr->DBsize = %d\n", CdbPtr->DBsize));
    DEBUGPRINT(DECODE, ("NumEntries in DbPtr = %d\n", NumEntries));

    //
    // On return NumEntries will be the number of TX buffers written into the DB
    //
    NumEntries = e1000_FreeTxBuffers(GigAdapter, NumEntries, DbPtr->TxBuffer);
    if (NumEntries == 0) {
      CdbPtr->StatFlags |= PXE_STATFLAGS_GET_STATUS_NO_TXBUFS_WRITTEN;
    }

    //
    // The receive buffer size and reserved fields take up the first 64 bits of the DB
    // The completed transmit buffers take up the rest
    //
    CdbPtr->DBsize = (UINT16) (sizeof (UINT64) + NumEntries * sizeof (UINT64));
    DEBUGPRINT(DECODE, ("Return DBsize = %d\n", CdbPtr->DBsize));
  }

  if ((CdbPtr->OpFlags & PXE_OPFLAGS_GET_INTERRUPT_STATUS) != 0) {
    Status = (UINT16) E1000_READ_REG (&GigAdapter->hw, E1000_ICR);
    GigAdapter->Int_Status |= Status;

    //
    // Acknowledge the interrupts.
    //
    E1000_WRITE_REG (&GigAdapter->hw, E1000_IMC, 0xFFFFFFFF);

    //
    // Report all the outstanding interrupts.
    //
    if (GigAdapter->Int_Status &
          (E1000_ICR_RXT0 | E1000_ICR_RXSEQ | E1000_ICR_RXDMT0 | E1000_ICR_RXO | E1000_ICR_RXCFG)
          ) {
      CdbPtr->StatFlags |= PXE_STATFLAGS_GET_STATUS_RECEIVE;
    }

    if (GigAdapter->int_mask & (E1000_ICR_TXDW | E1000_ICR_TXQE)) {
      CdbPtr->StatFlags |= PXE_STATFLAGS_GET_STATUS_TRANSMIT;
    }

    if (GigAdapter->int_mask &
          (E1000_ICR_GPI_EN0 | E1000_ICR_GPI_EN1 | E1000_ICR_GPI_EN2 | E1000_ICR_GPI_EN3 | E1000_ICR_LSC)
          ) {
      CdbPtr->StatFlags |= PXE_STATFLAGS_GET_STATUS_SOFTWARE;
    }
  }

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
	if (IsClient1GDevice(GigAdapter))
	{
	  if (!IsLinkUp(GigAdapter)) {
	    GigAdapter->hw.mac.get_link_status = TRUE;
	  }

	  if (TRUE == GigAdapter->hw.mac.get_link_status) {
	    e1000_check_for_link(&GigAdapter->hw);
	  }
	}  
#endif //INTEL_KDNET_1G_CLIENT_MERGE_MARKER
  //
  // Return current media status
  //
  if ((CdbPtr->OpFlags & PXE_OPFLAGS_GET_MEDIA_STATUS) != 0) {
    if (!IsLinkUp(GigAdapter)) {
      CdbPtr->StatFlags |= PXE_STATFLAGS_GET_STATUS_NO_MEDIA;
    }
  }

  CdbPtr->StatFlags |= PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode = PXE_STATCODE_SUCCESS;

  return ;
}

VOID
e1000_UNDI_FillHeader (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to fill media header(s) in transmit packet(s).
  Copies the MAC address into the media header whether it is dealing
  with fragmented or non-fragmented packets.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  AdapterInfo       - Pointer to the NIC data structure information which the UNDI driver is layering on.

Returns:
  None

--*/
{
  PXE_CPB_FILL_HEADER             *Cpb;
  PXE_CPB_FILL_HEADER_FRAGMENTED  *Cpbf;
  ETHER_HEADER                    *MacHeader;
  UINTN                           i;

  DEBUGPRINT(DECODE, ("e1000_UNDI_FillHeader\n"));

  if (CdbPtr->CPBsize == PXE_CPBSIZE_NOT_USED) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  if ((CdbPtr->OpFlags & PXE_OPFLAGS_FILL_HEADER_FRAGMENTED) != 0) {
    Cpbf = (PXE_CPB_FILL_HEADER_FRAGMENTED *) (UINTN) CdbPtr->CPBaddr;

    //
    // Assume 1st fragment is big enough for the mac header.
    //
    if ((Cpbf->FragCnt == 0) || (Cpbf->FragDesc[0].FragLen < PXE_MAC_HEADER_LEN_ETHER)) {
      //
      // No buffers given.
      //
      CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
      CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
      return ;
    }

    MacHeader = (ETHER_HEADER *) (UINTN) Cpbf->FragDesc[0].FragAddr;

    //
    // We don't swap the protocol bytes.
    //
    MacHeader->type = Cpbf->Protocol;

    DEBUGPRINT(DECODE, ("MacHeader->src_addr = "));
    for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
      MacHeader->dest_addr[i] = Cpbf->DestAddr[i];
      MacHeader->src_addr[i] = Cpbf->SrcAddr[i];
      DEBUGPRINT(DECODE, ("%x ", MacHeader->src_addr[i]));
    }

    DEBUGPRINT(DECODE, ("\n"));
  } else {
    Cpb       = (PXE_CPB_FILL_HEADER *) (UINTN) CdbPtr->CPBaddr;
    MacHeader = (ETHER_HEADER *) (UINTN) Cpb->MediaHeader;

    //
    // We don't swap the protocol bytes.
    //
    MacHeader->type = Cpb->Protocol;

    DEBUGPRINT(DECODE, ("MacHeader->src_addr = "));
    for (i = 0; i < PXE_HWADDR_LEN_ETHER; i++) {
      MacHeader->dest_addr[i] = Cpb->DestAddr[i];
      MacHeader->src_addr[i] = Cpb->SrcAddr[i];
      DEBUGPRINT(DECODE, ("%x ", MacHeader->src_addr[i]));
    }

    DEBUGPRINT(DECODE, ("\n"));
  }

  DEBUGWAIT(DECODE);
  return ;
}

VOID
e1000_UNDI_Transmit (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  This routine is used to place a packet into the transmit queue.  The data buffers given to
  this command are to be considered locked and the application or network driver loses
  ownership of these buffers and must not free or relocate them until the ownership returns.

  When the packets are transmitted, a transmit complete interrupt is generated (if interrupts
  are disabled, the transmit interrupt status is still set and can be checked using the UNDI_Status
  command.

  Some implementations and adapters support transmitting multiple packets with one transmit
  command.  If this feature is supported, the transmit CPBs can be linked in one transmit
  command.

  All UNDIs support fragmented frames, now all network devices or protocols do.  If a fragmented
  frame CPB is given to UNDI and the network device does not support fragmented frames
  (see !PXE.Implementation flag), the UNDI will have to copy the fragments into a local buffer
  before transmitting.


Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_Transmit called when driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  if (CdbPtr->CPBsize == PXE_CPBSIZE_NOT_USED) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  CdbPtr->StatCode = (PXE_STATCODE) e1000_Transmit (GigAdapter, CdbPtr->CPBaddr, CdbPtr->OpFlags);

  CdbPtr->StatCode == PXE_STATCODE_SUCCESS ? (CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE) : (CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED);

  return ;
}

VOID
e1000_UNDI_Receive (
  IN PXE_CDB         *CdbPtr,
  IN GIG_DRIVER_DATA *GigAdapter
  )
/*++

Routine Description:
  When the network adapter has received a frame, this command is used to copy the frame
  into the driver/application storage location.  Once a frame has been copied, it is
  removed from the receive queue.

Arguments:
  CdbPtr            - Pointer to the command descriptor block.
  GigAdapter    - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  None

--*/
{
#ifndef INTEL_KDNET
  if (GigAdapter->DriverBusy == TRUE) {
    DEBUGPRINT (DECODE, ("ERROR: e1000_UNDI_Receive called while driver busy\n"));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_BUSY;
    return ;
  }
#endif

  //
  // Check if RU has started.
  //
  if (GigAdapter->ReceiveStarted == FALSE) {
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode = PXE_STATCODE_NOT_INITIALIZED;
    return ;
  }

  CdbPtr->StatCode = (UINT16) e1000_Receive (GigAdapter, CdbPtr->CPBaddr, CdbPtr->DBaddr);

  CdbPtr->StatCode == PXE_STATCODE_SUCCESS ? (CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE) : (CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED);

  return ;
}


VOID
e1000_UNDI_APIEntry (
  IN  UINT64 cdb
  )
/*++

Routine Description:
  This is the main SW UNDI API entry using the newer nii protocol.
  The parameter passed in is a 64 bit flat model virtual
  address of the cdb.  We then jump into the common routine for both old and
  new nii protocol entries.

Arguments:
  cdb            - Pointer to the command descriptor block.

Returns:
  None

--*/
{
  PXE_CDB         *CdbPtr;
  GIG_DRIVER_DATA *GigAdapter;
  UNDI_CALL_TABLE *tab_ptr;

  if (cdb == (UINT64) 0) {
    return ;
  }

  CdbPtr = (PXE_CDB *) (UINTN) cdb;

  if (CdbPtr->IFnum > e1000_pxe_31->IFcnt) {
    DEBUGPRINT(DECODE, ("Invalid IFnum %d\n", CdbPtr->IFnum));
    CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
    CdbPtr->StatCode  = PXE_STATCODE_INVALID_CDB;
    return ;
  }

  GigAdapter              = &(e1000_UNDI32DeviceList[CdbPtr->IFnum]->NicInfo);
  GigAdapter->VersionFlag = 0x31; // entering from new entry point

  //
  // Check the OPCODE range.
  //
  if ((CdbPtr->OpCode > PXE_OPCODE_LAST_VALID) ||
      (CdbPtr->StatCode != PXE_STATCODE_INITIALIZE) ||
      (CdbPtr->StatFlags != PXE_STATFLAGS_INITIALIZE)
      ) {
    DEBUGPRINT(DECODE, ("Invalid StatCode, OpCode, or StatFlags.\n", CdbPtr->IFnum));
    goto badcdb;
  }

  if (CdbPtr->CPBsize == PXE_CPBSIZE_NOT_USED) {
    if (CdbPtr->CPBaddr != PXE_CPBADDR_NOT_USED) {
      goto badcdb;
    }
  } else if (CdbPtr->CPBaddr == PXE_CPBADDR_NOT_USED) {
    goto badcdb;
  }

  if (CdbPtr->DBsize == PXE_DBSIZE_NOT_USED) {
    if (CdbPtr->DBaddr != PXE_DBADDR_NOT_USED) {
      goto badcdb;
    }
  } else if (CdbPtr->DBaddr == PXE_DBADDR_NOT_USED) {
    goto badcdb;
  }

  //
  // Check if cpbsize and dbsize are as needed.
  // Check if opflags are as expected.
  //
  tab_ptr = &e1000_api_table[CdbPtr->OpCode];

  if (tab_ptr->cpbsize != (UINT16) (DONT_CHECK) && tab_ptr->cpbsize != CdbPtr->CPBsize) {
    goto badcdb;
  }

  if (tab_ptr->dbsize != (UINT16) (DONT_CHECK) && tab_ptr->dbsize != CdbPtr->DBsize) {
    goto badcdb;
  }

  if (tab_ptr->opflags != (UINT16) (DONT_CHECK) && tab_ptr->opflags != CdbPtr->OpFlags) {
    goto badcdb;
  }

  GigAdapter = &(e1000_UNDI32DeviceList[CdbPtr->IFnum]->NicInfo);

  //
  // Check if UNDI_State is valid for this call.
  //
  if (tab_ptr->state != (UINT16) (-1)) {
    //
    // Should atleast be started.
    //
    if (GigAdapter->State == PXE_STATFLAGS_GET_STATE_STOPPED) {
      CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
      CdbPtr->StatCode  = PXE_STATCODE_NOT_STARTED;
      return ;
    }

    //
    // Check if it should be initialized.
    //
    if (tab_ptr->state == 2) {
      if (GigAdapter->State != PXE_STATFLAGS_GET_STATE_INITIALIZED) {
        CdbPtr->StatCode  = PXE_STATCODE_NOT_INITIALIZED;
        CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
        return ;
      }
    }
  }

  //
  // Set the return variable for success case here.
  //
  CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_COMPLETE;
  CdbPtr->StatCode  = PXE_STATCODE_SUCCESS;

  tab_ptr->api_ptr (CdbPtr, GigAdapter);
  return ;

badcdb:
  CdbPtr->StatFlags = PXE_STATFLAGS_COMMAND_FAILED;
  CdbPtr->StatCode  = PXE_STATCODE_INVALID_CDB;
  return ;
}


