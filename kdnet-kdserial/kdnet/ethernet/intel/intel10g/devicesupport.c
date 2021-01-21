/**************************************************************************

Copyright (c) 2001-2016, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

#include "DeviceSupport.h"
#include "Brand.h"


/*++

  Routine Description:
    Seeks for current device's entry in branding table

  Arguments:
    UndiPrivateData -
    ExactMatch      - Indicator whether full 4-part device ID match is expected.
                      If FALSE, function returns best matching device's info.

  Returns:
    Device info structure pointer when match was found, NULL otherwise.

--*/
#ifdef INTEL_KDNET
static
#endif
BRAND_STRUCT*
FindDeviceInTableByIds(
  UINT16              VendorId,
  UINT16              DeviceId,
  UINT16              SubvendorId,
  UINT16              SubdeviceId,
  BOOLEAN             ExactMatch
)
{
  UINTN             i;
  BRAND_STRUCT      *Device = NULL;

  if (ExactMatch) {
    for (i = 0; i < BRANDTABLE_SIZE; i++) {
      if ((VendorId == branding_table[i].vendor_id) &&
          (DeviceId == branding_table[i].device_id) &&
          (SubvendorId == branding_table[i].subvendor_id) &&
          (SubdeviceId == branding_table[i].subsystem_id))
      {
        Device = &branding_table[i];
        break;
      }
    }
  }
  else {
    INTN    SubsystemMatch  = -1;
    INTN    SubvendorMatch  = -1;
    INTN    DeviceMatch = -1;
    INTN    VendorMatch = -1;

    for(i = 0; i < BRANDTABLE_SIZE; i++) {
      if (VendorId == branding_table[i].vendor_id) {
        if (DeviceId == branding_table[i].device_id) {
          if (SubvendorId == branding_table[i].subvendor_id) {
            if (SubdeviceId == branding_table[i].subsystem_id) {
              SubsystemMatch = i;
              break;
            }
            else if (branding_table[i].subsystem_id == WILD_CARD) {
              SubvendorMatch = i;
            }
          }
          else if (branding_table[i].subvendor_id == WILD_CARD) {
            DeviceMatch = i;
          }
        }
        else if (branding_table[i].device_id == WILD_CARD) {
          VendorMatch = i;
        }
      }
    }
    do {
      if (SubsystemMatch != -1) {
        Device = &branding_table[SubsystemMatch];
        break;
      }
      if (SubvendorMatch != -1) {
        Device = &branding_table[SubvendorMatch];
        break;
      }
      if (DeviceMatch != -1) {
        Device = &branding_table[DeviceMatch];
        break;
      }
      if (VendorMatch != -1) {
        Device = &branding_table[VendorMatch];
        break;
      }
    } while (0);

  }
  return Device;
}

/*++

  Routine Description:
    Seeks for current device's entry in branding table

  Arguments:
    UndiPrivateData -
    ExactMatch      - Indicator whether full 4-part device ID match is expected.
                      If FALSE, function returns best matching device's info.

  Returns:
    Device info structure pointer when match was found, NULL otherwise.

--*/
#ifdef INTEL_KDNET
static
#endif
BRAND_STRUCT*
FindDeviceInTable(
  UNDI_PRIVATE_DATA   *UndiPrivateData,
  BOOLEAN             ExactMatch
)
{
  struct ixgbe_hw   *Nic = &UndiPrivateData->NicInfo.hw;

  return FindDeviceInTableByIds (
           Nic->vendor_id,
           Nic->device_id,
           Nic->subsystem_vendor_id,
           Nic->subsystem_device_id,
           ExactMatch
           );
}

/*++

  Routine Description:
    Returns pointer to current device's branding string (looks for best match)

  Arguments:
    UndiPrivateData - Points to the driver instance private data

  Returns:
    Pointer to current device's branding string

--*/
CHAR16*
GetDeviceBrandingString(
  UNDI_PRIVATE_DATA     *UndiPrivateData
)
{
  BRAND_STRUCT *Device = NULL;
  CHAR16       *BrandingString = NULL;


  // It will return last, INVALID entry at least
  Device = FindDeviceInTable(UndiPrivateData, FALSE);
  if (Device != NULL) {
    BrandingString = Device->brand_string;
  }

  return BrandingString;
}

/*++

  Routine Description:
    Returns information whether given device ID is supported basing on branding
    table.

  Arguments:
    Vendor ID, Device ID.

  Returns:
    TRUE if device ID is supported, FALSE if device ID is not supported.

--*/
BOOLEAN
IsDeviceIdSupported (
  UINT16  VendorId,
  UINT16  DeviceId
)
{
  UINTN         i;
  
  if (VendorId == INTEL_VENDOR_ID) {
    for (i = 0; i < BRANDTABLE_SIZE; i++) {
      if (DeviceId == branding_table[i].device_id) {
        return TRUE;
      }
    }
  }
  return FALSE;
}
