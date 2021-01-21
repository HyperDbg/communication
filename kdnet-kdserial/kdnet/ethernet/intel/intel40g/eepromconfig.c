/**************************************************************************

Copyright (c) 2001-2019, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

/*++
Copyright (c) 2004-2013, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:
  EepromConfig.c

Abstract:

  This is an example of how a driver might export data to the HII protocol to be
  later utilized by the Setup Protocol

--*/

#include "I40e.h"
#include "EepromConfig.h"
#ifndef INTEL_KDNET
#include "wol.h"
#endif

#ifdef INTEL_KDNET
UINT16 SwapBytes16(UINT16 Value)
{
    UINT16 retval = 0;
    retval |= (Value >> 8) & 0x00ff;
    retval |= (Value << 8) & 0xff00;
    return retval;
}
#endif


#pragma warning (push)
#pragma warning (disable:4242)

EFI_STATUS 
I40eWriteNvmBuffer(
  I40E_DRIVER_DATA   *AdapterInfo,
  UINT8              ModulePointer,
  UINT32             Offset,
  UINT16              Words, 
  VOID               *Data
)
{
  enum i40e_status_code      I40eStatus;
  struct i40e_arq_event_info e;
  u16                        pending;
  UINT32                     WaitCnt;

  i40e_acquire_nvm(&AdapterInfo->hw, I40E_RESOURCE_WRITE);
  while(i40e_clean_arq_element(&AdapterInfo->hw, &e, &pending) != I40E_ERR_ADMIN_QUEUE_NO_WORK);

  I40eStatus = __i40e_write_nvm_buffer(&AdapterInfo->hw, ModulePointer, Offset, Words, Data);
  if (I40eStatus != I40E_SUCCESS) {
    enum i40e_admin_queue_err LastStatus;
    
    DEBUGPRINT (CRITICAL, ("__i40e_write_nvm_buffer (%d, %d) returned: %d, %d\n",
                            Offset, Words, I40eStatus, AdapterInfo->hw.aq.asq_last_status));
    LastStatus = AdapterInfo->hw.aq.asq_last_status;
    i40e_release_nvm(&AdapterInfo->hw);
    if (LastStatus == I40E_AQ_RC_EPERM) {
      //
      // Need to detect attempts to write RO area
      //
      DEBUGPRINT (IMAGE, ("__i40e_write_nvm_buffer returned EPERM\n"));
      return EFI_ACCESS_DENIED;
    } else {
      return EFI_DEVICE_ERROR;
    }
  }

  WaitCnt = 0;
  do {
    I40eStatus = i40e_clean_arq_element(&AdapterInfo->hw, &e, &pending);
    if (I40eStatus == I40E_SUCCESS) {
      if (e.desc.opcode != i40e_aqc_opc_nvm_update) {
        I40eStatus = I40E_ERR_ADMIN_QUEUE_NO_WORK;
      } else {
        break;
      }
    }
    
    DelayInMicroseconds(AdapterInfo, 1000);
    WaitCnt++;
    if (WaitCnt > 1000) {
      i40e_release_nvm(&AdapterInfo->hw);
      DEBUGPRINT (CRITICAL, ("Timeout waiting for ARQ response\n"));
      return EFI_DEVICE_ERROR;
    }
    //
    // Wait until we get response to i40e_aqc_opc_nvm_update
    //
  } while(I40eStatus != I40E_SUCCESS);
  i40e_release_nvm(&AdapterInfo->hw);

  return EFI_SUCCESS;
}

EFI_STATUS 
I40eWriteNvmBufferExt(
  I40E_DRIVER_DATA   *AdapterInfo,
  UINT32             Offset,
  UINT16              Words, 
  VOID               *Data
)
/*++

  Routine Description:
    Writes data buffer to nvm using __i40e_write_nvm_buffer shared code function.
    Function works around the situation when the buffer spreads over two sectors.
    The entire buffer must be located inside the Shared RAM.
    
  Arguments:
    AdapterInfo  - Points to the driver information
    Offset       - buffer offset from the start of NVM
    
    
  Returns:
    EFI_STATUS      
--*/
{
  UINT16     SectorStart;
  UINT16     Margin;
  UINT16     Words1, Words2;
  EFI_STATUS Status;

	DEBUGFUNC("__i40e_write_nvm_buffer");

  //
  // Check if the buffer spreads over two sectors. Then we need to split 
  // the buffer into two adjacent buffers, one for each sector and write them separatelly.
  //
  SectorStart = (Offset / I40E_SR_SECTOR_SIZE_IN_WORDS) * I40E_SR_SECTOR_SIZE_IN_WORDS;
  Margin = (SectorStart + I40E_SR_SECTOR_SIZE_IN_WORDS) - Offset;
  if (Words > Margin) {
    Words1 = Margin;
    Words2 = Words - Margin;
  } else {
    Words1 = Words;
    Words2 = 0;
  }

	Status = I40eWriteNvmBuffer(AdapterInfo, 0, Offset, Words1, Data);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("I40eWriteNvmBuffer returned %r\n", Status));
    return EFI_DEVICE_ERROR;
  }
  if (Words2 > 0) {
    //
    // Write the remaining part of the input data to the second sector
    //
    Status = I40eWriteNvmBuffer(AdapterInfo, 0, SectorStart + I40E_SR_SECTOR_SIZE_IN_WORDS, Words2, (UINT16*)Data + Words1);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("I40eWriteNvmBuffer returned %r\n", Status));
      return EFI_DEVICE_ERROR;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
ReadDataFromNvmModule(
  I40E_DRIVER_DATA   *AdapterInfo,
  UINT8               ModulePtr,
  UINT16              Offset,
  UINT16              DataSizeInWords,
  UINT16             *DataPointer
)
{
  enum i40e_status_code I40eStatus = I40E_SUCCESS;
  UINT16     PtrValue = 0;
  UINT32     FlatOffset;

  if (ModulePtr !=0) {
    I40eStatus = i40e_read_nvm_word(&AdapterInfo->hw, ModulePtr, &PtrValue);
    if (I40eStatus != I40E_SUCCESS) {
      DEBUGPRINT (CRITICAL, ("i40e_read_nvm_word returned error\n"));
      return EFI_DEVICE_ERROR;
    }
  } else {
    //
    // Reading from the flat memory
    //
    PtrValue = 0;
  }

  //
  // Pointer not initialized?
  //
  if (PtrValue == 0xFFFF || PtrValue == 0x7FFF) {
    return EFI_NOT_FOUND;
  }

  //
  // Check wether the module is in SR mapped area or outside
  //
  if ((PtrValue & 0x8000) == 0x8000) {
    //
    // Pointer points outside of the Shared RAM mapped area
    //
    PtrValue &= ~0x8000;
    //
    // PtrValue in 4kB units, need to convert to words
    //
    PtrValue /= 2;
    FlatOffset = ((UINT32)PtrValue * 0x1000) + (UINT32)Offset;
    I40eStatus = i40e_acquire_nvm (&AdapterInfo->hw, I40E_RESOURCE_READ);
    if (I40eStatus == I40E_SUCCESS) {
      I40eStatus = i40e_aq_read_nvm (
                     &AdapterInfo->hw,
                     0,
                     2 * FlatOffset,
                     2 * DataSizeInWords,
                     DataPointer,
                     TRUE,
                     NULL
                     );
      i40e_release_nvm (&AdapterInfo->hw);
    }
    if (I40eStatus != I40E_SUCCESS) {
      DEBUGPRINT (CRITICAL, ("i40e_aq_read_nvm (%d, %d) returned: %d, %d\n", 
                              FlatOffset, DataSizeInWords, I40eStatus, AdapterInfo->hw.aq.asq_last_status));
      DEBUGPRINT (CRITICAL, ("i40e_read_nvm_aq returned %d\n", I40eStatus));
      return EFI_DEVICE_ERROR;
    }
  } else {
    //
    // Read from the Shadow RAM
    //
    I40eStatus = i40e_read_nvm_buffer (&AdapterInfo->hw, PtrValue + Offset, &DataSizeInWords, DataPointer);
    if (I40eStatus != I40E_SUCCESS) {
      DEBUGPRINT (CRITICAL, ("i40e_read_nvm_buffer returned %d\n", I40eStatus));
    }
  }
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;  
  
}

EFI_STATUS
WriteDataToNvmModule(
  I40E_DRIVER_DATA   *AdapterInfo,
  UINT8               ModulePtr,
  UINT16              Offset,
  UINT16              DataSizeInWords,
  UINT16             *DataPointer
)
{
  enum i40e_status_code      I40eStatus;
  UINT16                     PtrValue = 0;

  if (ModulePtr !=0) {
    I40eStatus = i40e_read_nvm_word(&AdapterInfo->hw, ModulePtr, &PtrValue);
    if (I40eStatus != I40E_SUCCESS) {
      return EFI_DEVICE_ERROR;
    }
  } else {
    //
    // Reading from the flat memory
    //
    PtrValue = 0;
  }

  //
  // Pointer not initialized?
  //
  if (PtrValue == 0xFFFF || PtrValue == 0x7FFF) {
    return EFI_NOT_FOUND;
  }

  return I40eWriteNvmBuffer (AdapterInfo, 0, PtrValue + Offset, DataSizeInWords, DataPointer);
}


#define I40E_AUTOGEN_PTR_PFGEN_PORTNUM_SECTION         0x15
#define I40E_AUTOGEN_PTR_PFGEN_PORTNUM_OFFSET          0x16
#define I40E_AUTOGEN_PTR_GLPCI_CAPSUP_SECTION          0xF
#define I40E_AUTOGEN_PTR_GLPCI_CAPSUP_OFFSET           0x10
#define I40E_AUTOGEN_PTR_PFPCI_FUNC2_SECTION           0x17
#define I40E_AUTOGEN_PTR_PFPCI_FUNC2_OFFSET            0x18
#define I40E_AUTOGEN_PTR_PF_VT_PFALLOC_PCIE_SECTION    0x1B
#define I40E_AUTOGEN_PTR_PF_VT_PFALLOC_PCIE_OFFSET     0x1C
#define I40E_AUTOGEN_PTR_PF_VT_PFALLOC_SECTION         0x1D
#define I40E_AUTOGEN_PTR_PF_VT_PFALLOC_OFFSET          0x1E

UINT16
GetRegisterInitializationDataOffset (
  I40E_DRIVER_DATA  *AdapterInfo,
  UINT16            AutoGeneratedSection,
  UINT16            AutoGeneratedOffset
)
/*++

  Routine Description:
    Returns the offset in the NVM of the CSR register initialization data 
    in the module pointed by the ModulePointer. Function looks for the register 
    based on its address in CSR register space.   

  Arguments:
    UndiPrivateData  - Points to the driver instance private data
    AutoGeneratedSection - 
    AutoGeneratedOffset - 
    
  Returns:
    offset of register data in NVM,
    0 when register is not found
      
  --*/
{
  UINT16  AutoGeneratedSectionPointer;
  UINT16  ModuleOffset;
  UINT16  OffsetWithinModule;
  enum i40e_status_code I40eStatus;

  // 
  // Read pointer to the auto generated pointers section
  //
  I40eStatus = i40e_read_nvm_word (
                 &AdapterInfo->hw, 
                 I40E_SR_AUTO_GENERATED_POINTERS_PTR,
                 &AutoGeneratedSectionPointer
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return 0;
  }

  
  // 
  // Read Nvm Module offset in SR
  //
  
  I40eStatus = i40e_read_nvm_word (
                 &AdapterInfo->hw, 
                 AutoGeneratedSectionPointer + AutoGeneratedSection,
                 &ModuleOffset
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return 0;
  }

  //
  // Read the length of the module
  //
  I40eStatus = i40e_read_nvm_word (
                 &AdapterInfo->hw, 
                 AutoGeneratedSectionPointer + AutoGeneratedOffset,
                 &OffsetWithinModule
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return 0;
  }

  if ((ModuleOffset == 0x7FFF) || (ModuleOffset == 0xFFFF)) {
    return 0;
  }

  if (OffsetWithinModule == 0x0000) {
    return 0;
  }

  return ModuleOffset + OffsetWithinModule;
}

#ifndef INTEL_KDNET

UINTN
EepromGetLanSpeedStatus(
  IN UNDI_PRIVATE_DATA *UndiPrivateData
)
{
  //
  //  Speed settings are currently not supported for 10 Gig driver. It's always set to autoneg to
  //  allow operation with the highest possible speed
  //
  return LINK_SPEED_AUTO_NEG;
}

#endif

EFI_STATUS
EepromSetLanSpeed(
  UNDI_PRIVATE_DATA     *UndiPrivateData,
  UINT8	LanSpeed
)
{
  return EFI_SUCCESS;
}


EFI_STATUS
EepromFirstPortMacAddressGet(
  IN UNDI_PRIVATE_DATA  *UndiPrivateData,
  OUT UINT16            *AlternateMacAddress,
  OUT UINT16            *FactoryMacAddress
)
{
  EFI_STATUS            Status;
  enum i40e_status_code I40eStatus;
  UINT16                PFMacAddressesPointer;
  UINT32                AltRamBuffer[2];

  //
  // Since NVM3.1 Factory MAC addresses are located in the EMP SR Settings module
  //
  Status = ReadDataFromNvmModule (
             &UndiPrivateData->NicInfo,
             NVM_EMP_SR_SETTINGS_MODULE_PTR,
             NVM_EMP_SR_PF_MAC_PTR,
             1,
             &PFMacAddressesPointer
             );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("ReadDataFromNvmModule error\n"));
    return Status;
  }

  //
  // Read from the PF MAC Address subsection of the EMP SR Settings module
  //
  Status = ReadDataFromNvmModule (
             &UndiPrivateData->NicInfo,
             NVM_EMP_SR_SETTINGS_MODULE_PTR,
             PFMacAddressesPointer + NVM_EMP_SR_PF_MAC_PTR +
             NVM_EMP_SR_PF_MAC_SAL0(0),
             3,
             FactoryMacAddress
             );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("ReadDataFromNvmModule error\n"));
    return Status;
  }

  //
  // Now that we have Factory MAC Address read, look for the Alternate MAC Address
  // In Fortville it is stored in Alternate RAM
  //
  I40eStatus = i40e_aq_alternate_read_indirect (
                 &UndiPrivateData->NicInfo.hw,
                 I40E_ALT_RAM_LAN_MAC_ADDRESS_LOW(0),
                 2,
                 AltRamBuffer
                 );
  if (I40eStatus != I40E_SUCCESS) {
    DEBUGPRINT (CRITICAL, ("i40e_aq_alternate_read_indirect error\n"));
    return EFI_DEVICE_ERROR;
  }
  //
  // Check if this Alternate RAM entry is valid and then use it,
  // otherwise return zeros
  //
  if ((AltRamBuffer[1] & ALT_RAM_VALID_PARAM_BIT_MASK) != 0) {
    AlternateMacAddress[0] = SwapBytes16((UINT16)(AltRamBuffer[1] & 0x0000FFFF));
    AlternateMacAddress[1] = SwapBytes16((UINT16)((AltRamBuffer[0] & 0xFFFF0000) >> 16));
    AlternateMacAddress[2] = SwapBytes16((UINT16)(AltRamBuffer[0] & 0x0000FFFF));
  } else {
    AlternateMacAddress[0] = 0;
    AlternateMacAddress[1] = 0;
    AlternateMacAddress[2] = 0;
  }

  return Status;
}

EFI_STATUS
EepromPartitionMacAddressGet(
  IN UNDI_PRIVATE_DATA  *UndiPrivateData,
  OUT UINT16            *FactoryMacAddress,
  OUT UINT16            *AlternateMacAddress,
  IN  UINT8              Partition  
)
{
  EFI_STATUS            Status;
  enum i40e_status_code I40eStatus;
  UINT16                PFMacAddressesPointer;
  UINT32                AltRamBuffer[2];

  //
  // Check if the partition wih this number exsist
  // Partition numbers are 0 - based internally
  // 
  if (Partition >= UndiPrivateData->NicInfo.PfPerPortMaxNumber) {
    return EFI_NOT_FOUND;
  }
  
  //
  // Since NVM3.1 Factory MAC addresses are located in the EMP SR Settings module
  //
  Status = ReadDataFromNvmModule (
             &UndiPrivateData->NicInfo,
             NVM_EMP_SR_SETTINGS_MODULE_PTR,
             NVM_EMP_SR_PF_MAC_PTR,
             1,
             &PFMacAddressesPointer
             );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("ReadDataFromNvmModule error\n"));
    return Status;
  }

  //
  // Read from the PF MAC Address subsection of the EMP SR Settings module
  //
  Status = ReadDataFromNvmModule (
             &UndiPrivateData->NicInfo,
             NVM_EMP_SR_SETTINGS_MODULE_PTR,
             PFMacAddressesPointer + NVM_EMP_SR_PF_MAC_PTR +
             NVM_EMP_SR_PF_MAC_SAL0(UndiPrivateData->NicInfo.PartitionPfNumber[Partition]),
             3,
             FactoryMacAddress
             );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("ReadDataFromNvmModule error\n"));
    return Status;
  }

  //
  // Now that we have Factory MAC Address read, look for the Alternate MAC Address
  // In Fortville it is stored in Alternate RAM
  //
  I40eStatus = i40e_aq_alternate_read_indirect (
                 &UndiPrivateData->NicInfo.hw,
                 I40E_ALT_RAM_LAN_MAC_ADDRESS_LOW(UndiPrivateData->NicInfo.PartitionPfNumber[Partition]),
                 2,
                 AltRamBuffer
                 );
  if (I40eStatus != I40E_SUCCESS) {
    DEBUGPRINT (CRITICAL, ("i40e_aq_alternate_read_indirect error\n"));
    return EFI_DEVICE_ERROR;
  }
  //
  // Check if this Alternate RAM entry is valid and then use it,
  // otherwise return zeros
  //
  if ((AltRamBuffer[1] & ALT_RAM_VALID_PARAM_BIT_MASK) != 0) {
    AlternateMacAddress[0] = SwapBytes16((UINT16)(AltRamBuffer[1] & 0x0000FFFF));
    AlternateMacAddress[1] = SwapBytes16((UINT16)((AltRamBuffer[0] & 0xFFFF0000) >> 16));
    AlternateMacAddress[2] = SwapBytes16((UINT16)(AltRamBuffer[0] & 0x0000FFFF));
  } else {
    AlternateMacAddress[0] = 0;
    AlternateMacAddress[1] = 0;
    AlternateMacAddress[2] = 0;
  }

  return Status;
}

EFI_STATUS
EepromPartitionMacAddressSet (
  IN UNDI_PRIVATE_DATA *UndiPrivateData,
  IN UINT16            *NewMacAddress,
  IN  UINT8             Partition  
  )
/*++

Routine Description:
  Programs the partition with an alternate MAC address, and backs up the factory default
  MAC address.

Arguments:
  XgbeAdapter - Pointer to adapter structure
  MacAddress - Value to set the MAC address to.

Returns:
  VOID

--*/
{
  enum i40e_status_code I40eStatus;  
  UINT32      AltRamBuffer[2];
  //
  // Now that we have Factory MAC Address read, look for the Alternate MAC Address
  // In Fortville it is stored in Alternate RAM
  // Prepare Alternate RAM entry structure
  //

  AltRamBuffer[0] = SwapBytes16(NewMacAddress[2]) + ((u32)SwapBytes16(NewMacAddress[1]) << 16);
  AltRamBuffer[1] = SwapBytes16(NewMacAddress[0]);

  AltRamBuffer[1] |= ALT_RAM_VALID_PARAM_BIT_MASK;

  I40eStatus = i40e_aq_alternate_write_indirect (
                 &UndiPrivateData->NicInfo.hw,
                 I40E_ALT_RAM_LAN_MAC_ADDRESS_LOW(UndiPrivateData->NicInfo.PartitionPfNumber[Partition]),
                 2,
                 AltRamBuffer
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EepromPartitionMacAddressDefault (
  IN UNDI_PRIVATE_DATA *UndiPrivateData,
  IN  UINT8             Partition  
  )
/*++

Routine Description:
  Restores the factory default MAC address for partition.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  PXE_STATCODE

--*/
{
  enum i40e_status_code I40eStatus;  
  UINT32      AltRamBuffer[2];

  //
  // Invalidate Alternate RAM entry by writing zeros
  //
  AltRamBuffer[0] = 0;
  AltRamBuffer[1] = 0;

  I40eStatus = i40e_aq_alternate_write_indirect (
                    &UndiPrivateData->NicInfo.hw,
                    I40E_ALT_RAM_LAN_MAC_ADDRESS_LOW(UndiPrivateData->NicInfo.PartitionPfNumber[Partition]),
                    2,
                    AltRamBuffer
                    );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;

}

EFI_STATUS
EepromMacAddressGet (
  IN UNDI_PRIVATE_DATA *UndiPrivateData,
  OUT UINT16           *FactoryMacAddress,
  OUT UINT16           *AlternateMacAddress
  )
/*++

Routine Description:
  Reads the currently assigned MAC address and factory default MAC address for port.

Arguments:
  XgbeAdapter - Pointer to adapter structure
  DefaultMacAddress - Factory default MAC address of the adapter
  AssignedMacAddress - CLP Assigned MAC address of the adapter, or the factory MAC address
                       if an alternate MAC address has not been assigned.

Returns:
  VOID

--*/
{
  EFI_STATUS  Status;

  //
  // Call EepromPartitionMacAddressGet with partition number 0
  //
  Status = EepromPartitionMacAddressGet(UndiPrivateData, FactoryMacAddress, AlternateMacAddress, 0);
  
  return Status;
}

EFI_STATUS
FixFwsm31Bit(
  IN UNDI_PRIVATE_DATA *UndiPrivateData
  )
/*++

Routine Description:
  Sets FWSM.31 bit like management FW does. This is Dell SIK-KR adapter
  specific. Other Niantic based adapters have FW on board and FW is responsible
  for FWSM.31 bit manipulations. This routine shall be called when MAC address
  is changed using HII interface.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  PXE_STATCODE

--*/
{

  return EFI_SUCCESS;
}

EFI_STATUS
EepromMacAddressSet (
  IN UNDI_PRIVATE_DATA *UndiPrivateData,
  IN UINT16            *NewMacAddress
  )
/*++

Routine Description:
  Programs the port with an alternate MAC address, and backs up the factory default
  MAC address.

Arguments:
  XgbeAdapter - Pointer to adapter structure
  MacAddress - Value to set the MAC address to.

Returns:
  VOID

--*/
{
  EFI_STATUS  Status;

  //
  // Call EepromPartitionMacAddressSet with partition number 0
  //
  Status = EepromPartitionMacAddressSet(UndiPrivateData, NewMacAddress, 0);

  return Status;
}

EFI_STATUS
EepromMacAddressDefault (
  IN UNDI_PRIVATE_DATA *UndiPrivateData
  )
/*++

Routine Description:
  Restores the factory default MAC address.

Arguments:
  XgbeAdapter - Pointer to adapter structure

Returns:
  PXE_STATCODE

--*/
{
  EFI_STATUS  Status;
  
  //
  // Call EepromPartitionMacAddressDefault with partition number 0
  //
  Status = EepromPartitionMacAddressDefault(UndiPrivateData, 0);

  return Status;
}

EFI_STATUS
EepromPartitionFactorySanMacAddressGet(
  IN UNDI_PRIVATE_DATA *UndiPrivateData,
  OUT UINT16           *FactorySanMacAddress,
  IN  UINT8             Partition  
)
{
  EFI_STATUS  Status;

  //
  // Check if the partition wih this number exsist
  // Partition numbers are 0 - based internally
  // 
  if (Partition >= UndiPrivateData->NicInfo.PfPerPortMaxNumber) {
    return EFI_NOT_FOUND;
  }
  
  //
  // Read the data from the Partition's offfset
  //
  Status = ReadDataFromNvmModule (
             &UndiPrivateData->NicInfo,
             NVM_SAN_MAC_ADDRESS_MODULE_PTR, 
             NVM_SAN_MAC_ADDRESS_OFFSET (UndiPrivateData->NicInfo.PartitionPfNumber[Partition]),
             3,
             FactorySanMacAddress
             );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  return Status;
}

EFI_STATUS
EepromGetCapabilitiesWord (
  IN  UNDI_PRIVATE_DATA *UndiPrivateData,
  OUT UINT16            *CapabilitiesWord
)
/*++

  Routine Description:
    Returns EEPROM capabilities word (0x33) for current adapter

  Arguments:
    UndiPrivateData  - Points to the driver instance private data
    CapabilitiesWord     - EEPROM capabilities word (0x33) for current adapter
    
  Returns:
    EFI_SUCCESS if function completed succesfully, 
    otherwise specific EEPROM read error code is returned.
    
--*/
{
  enum i40e_status_code I40eStatus;
  UINT16      Word;
  
#define EEPROM_CAPABILITIES_WORD 0x33
#define EEPROM_CAPABILITIES_SIG  0x4000

  I40eStatus = i40e_read_nvm_word(
                 &UndiPrivateData->NicInfo.hw,
                 EEPROM_CAPABILITIES_WORD,
                 &Word
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

  Word &= ~EEPROM_CAPABILITIES_SIG;
  *CapabilitiesWord = Word;

  return EFI_SUCCESS;
}


EFI_STATUS
EepromGetNvmVersion (
  IN  UNDI_PRIVATE_DATA *UndiPrivateData,
  OUT UINT16            *MajorVer,
  OUT UINT16            *MinorVer
)
/*++

  Routine Description:
    Returns NVM version

  Arguments:
    UndiPrivateData  - Points to the driver instance private data
    Major - NVM major version
    Minor - NVM minor version

  Returns:
    EFI_SUCCESS if function completed succesfully,
    otherwise specific EEPROM read error code is returned.

--*/
{
  enum i40e_status_code I40eStatus;
  UINT16      Word;

#define EEPROM_VERSION_WORD 0x18

  I40eStatus = i40e_read_nvm_word(
                 &UndiPrivateData->NicInfo.hw,
                 EEPROM_VERSION_WORD,
                 &Word
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

  *MajorVer = (Word >> 12);
  *MinorVer = (Word & 0xFF);

  return EFI_SUCCESS;
}



EFI_STATUS
EepromGetMaxPfPerPortNumber (
  I40E_DRIVER_DATA  *AdapterInfo,
  OUT UINT8         *PfPerPortNumber
)
{
  EFI_STATUS  Status;
  UINT16      DataWord;

  //
  // This parameter is stored in EMP Settings module in Shadow RAM
  //
  Status = ReadDataFromNvmModule (
             AdapterInfo,
             NVM_EMP_SR_SETTINGS_MODULE_PTR, 
             NVM_EMP_SR_MAX_PF_VF_PER_PORT_WORD_OFFSET,
             1,
             &DataWord
             );
  if (EFI_ERROR(Status)) {
    DEBUGPRINT (CRITICAL, ("ReadDataFromNvmModule returned %r\n", Status));    
    return Status;
  }

  DataWord &= NVM_EMP_SR_MAX_PF_PER_PORT_MASK;
  DataWord <<= NVM_EMP_SR_MAX_PF_PER_PORT_OFFSET;

  //
  // Max number of pfs per port is not written directly, need to do conversion
  //
  switch (DataWord) {
    case NVM_1_PF_PER_PORT:
      *PfPerPortNumber = 1;
      break;
    case NVM_2_PF_PER_PORT:
      *PfPerPortNumber = 2;
      break;
    case NVM_4_PF_PER_PORT:
      *PfPerPortNumber = 4;
      break;
    case NVM_8_PF_PER_PORT:
      *PfPerPortNumber = 8;
      break;
    case NVM_16_PF_PER_PORT:
      *PfPerPortNumber = 16;
      break;
    default:
      //
      // Any other values are reserved so return error status
      //
      return EFI_DEVICE_ERROR;      
  }

  return EFI_SUCCESS;
}

BOOLEAN
EepromIsLomDevice(
  IN  UNDI_PRIVATE_DATA *UndiPrivateData  
)
{
  return FALSE;  
}

EFI_STATUS
EepromUpdateChecksum(
  IN  UNDI_PRIVATE_DATA *UndiPrivateData  
)
{
  enum i40e_status_code  I40eStatus;
  struct i40e_arq_event_info e;
  u16 pending;

  i40e_acquire_nvm(&UndiPrivateData->NicInfo.hw, I40E_RESOURCE_WRITE);
  while(i40e_clean_arq_element(&UndiPrivateData->NicInfo.hw, &e, &pending) != I40E_ERR_ADMIN_QUEUE_NO_WORK);

  if (i40e_update_nvm_checksum(&UndiPrivateData->NicInfo.hw) != I40E_SUCCESS) {
    i40e_release_nvm(&UndiPrivateData->NicInfo.hw);   
    return EFI_DEVICE_ERROR;
  }

  do {
    I40eStatus = i40e_clean_arq_element(&UndiPrivateData->NicInfo.hw, &e, &pending);
    if (I40eStatus == I40E_SUCCESS) {
      if (e.desc.opcode != i40e_aqc_opc_nvm_update) {
        I40eStatus = I40E_ERR_ADMIN_QUEUE_NO_WORK;
      } 
    }
    //
    // Wait until we get response to i40e_aqc_opc_nvm_update
    //
  } while(I40eStatus != I40E_SUCCESS);

  i40e_release_nvm(&UndiPrivateData->NicInfo.hw);
  return EFI_SUCCESS;
  
}

EFI_STATUS
ReadPbaString(
  I40E_DRIVER_DATA  *AdapterInfo,
  IN OUT UINT8      *PbaNumber, 
  IN     UINT32     PbaNumberSize
)
{
  UINT16  PbaFlags;
  UINT16  Data;
  UINT16  PbaPtr;
	UINT16  Offset;
	UINT16  Length;
  enum i40e_status_code I40eStatus;

	if (PbaNumber == NULL) {
		return EFI_INVALID_PARAMETER;
	}

  I40eStatus = i40e_read_nvm_word (
                 &AdapterInfo->hw, 
                 NVM_PBA_FLAGS_OFFSET, 
                 &PbaFlags
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

  I40eStatus = i40e_read_nvm_word(
                 &AdapterInfo->hw,
                 NVM_PBA_BLOCK_MODULE_POINTER,
                 &PbaPtr
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

	//
	// If PbaFlags is not 0xFAFA the PBA must be in legacy format which
	// means PbaPtr is actually our second data word for the PBA number
	// and we can decode it into an ascii string
	//
	if (PbaFlags != NVM_PBA_FLAG_STRING_MODE) {
    //
    // We will need 11 characters to store the PBA
    //
		if (PbaNumberSize < 11) {
			return EFI_INVALID_PARAMETER;
		}

    //
    // Extract hex string from data and pba_ptr
    //
		PbaNumber[0] = (PbaFlags >> 12) & 0xF;
		PbaNumber[1] = (PbaFlags >> 8) & 0xF;
		PbaNumber[2] = (PbaFlags >> 4) & 0xF;
		PbaNumber[3] = PbaFlags & 0xF;
		PbaNumber[4] = (PbaPtr >> 12) & 0xF;
		PbaNumber[5] = (PbaPtr >> 8) & 0xF;
		PbaNumber[6] = '-';
		PbaNumber[7] = 0;
		PbaNumber[8] = (PbaPtr >> 4) & 0xF;
		PbaNumber[9] = PbaPtr & 0xF;

    //
    // Put a null character on the end of our string
    //
		PbaNumber[10] = '\0';
    //
		// switch all the data but the '-' to hex char
		//
		for (Offset = 0; Offset < 10; Offset++) {
			if (PbaNumber[Offset] < 0xA)
				PbaNumber[Offset] += '0';
			else if (PbaNumber[Offset] < 0x10)
				PbaNumber[Offset] += 'A' - 0xA;
		}

		return EFI_SUCCESS;
	}

  I40eStatus = i40e_read_nvm_word (
                 &AdapterInfo->hw, 
                 PbaPtr, 
                 &Length
                 );
  if (I40eStatus != I40E_SUCCESS) {
    return EFI_DEVICE_ERROR;
  }

	if (Length == 0xFFFF || Length == 0) {
		return EFI_DEVICE_ERROR;
	}

  //
	// check if PbaNumber buffer is big enough
	//
	if (PbaNumberSize  < (((u32)Length * 2) - 1)) {
		return EFI_INVALID_PARAMETER;
	}

  //
	// trim pba length from start of string
	//
	PbaPtr++;
	Length--;

	for (Offset = 0; Offset < Length; Offset++) {
    I40eStatus = i40e_read_nvm_word (
                   &AdapterInfo->hw, 
                   PbaPtr + Offset, 
                   &Data
                   );
    if (I40eStatus != I40E_SUCCESS) {
      return EFI_DEVICE_ERROR;
    }
		PbaNumber[Offset * 2] = (u8)(Data >> 8);
		PbaNumber[(Offset * 2) + 1] = (u8)(Data & 0xFF);
	}
	PbaNumber[Offset * 2] = '\0';
  
  return EFI_SUCCESS;
}

EFI_STATUS
EepromReadPortnumValues(
  I40E_DRIVER_DATA  *AdapterInfo,
  UINT16            *PortNumbers,
  UINT16             ArraySize
)
{
  UINT16  RegAddress;
  UINTN   i;
  enum i40e_status_code I40eStatus;

  RegAddress = GetRegisterInitializationDataOffset (
                 AdapterInfo,
                 I40E_AUTOGEN_PTR_PFGEN_PORTNUM_SECTION,
                 I40E_AUTOGEN_PTR_PFGEN_PORTNUM_OFFSET
                 );
  if (RegAddress == 0) {
    return EFI_DEVICE_ERROR;
  }

  for (i = 0; i < ArraySize; i++) {
    I40eStatus = i40e_read_nvm_word (
                   &AdapterInfo->hw,
                   RegAddress + i * 2,
                   &PortNumbers[i]
                   );
    if (I40eStatus != I40E_SUCCESS) {
      return EFI_DEVICE_ERROR;
    }
  }
  return EFI_SUCCESS;
}


#pragma warning (pop)


