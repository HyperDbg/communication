/**************************************************************************

Copyright (c) 2001-2016, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

#ifndef INTEL_KDNET

#include <Protocol/HiiString.h>
#include <Library/HiiLib.h>

#endif

#include "xgbe.h"
#include "DeviceSupport.h"

//
// Global Variables
//
#ifdef INTEL_KDNET

//
// Remap the 10GBit ActiveInterface count so it doesn't collide with the 1GBit count.
//

#define ActiveInterfaces Active10GbitInterfaces

__declspec(align(16)) PXE_SW_UNDI             ixgbe_pxe_31_struct;  // 3.1 entry
#endif
VOID                                    *ixgbe_pxe_memptr = NULL;
PXE_SW_UNDI                             *ixgbe_pxe_31     = NULL; // 3.1 entry
UNDI_PRIVATE_DATA                       *XgbeDeviceList[MAX_NIC_INTERFACES];
#ifndef INTEL_KDNET
NII_TABLE                               ixgbe_UndiData;
#endif
UINT8                                   ActiveInterfaces = 0;
#ifndef INTEL_KDNET
EFI_EVENT                               EventNotifyExitBs;
EFI_EVENT                               EventNotifyVirtual;

EFI_HANDLE                              gImageHandle;
EFI_SYSTEM_TABLE                        *gSystemTable;
#endif

#ifdef INTEL_KDNET
UNDI_PRIVATE_DATA         gXgbeUndiPrivateData;
UNDI_PRIVATE_DATA         *XgbePrivate = &gXgbeUndiPrivateData;
#endif

//
// external Global Variables
//
extern UNDI_CALL_TABLE                     ixgbe_api_table[];
#ifndef INTEL_KDNET
extern EFI_COMPONENT_NAME_PROTOCOL         gUndiComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL        gUndiComponentName2;
extern EFI_DRIVER_SUPPORTED_EFI_VERSION_PROTOCOL gUndiSupportedEFIVersion;
extern EFI_DRIVER_DIAGNOSTICS_PROTOCOL     gXgbeUndiDriverDiagnostics;
extern EFI_DRIVER_DIAGNOSTICS2_PROTOCOL    gXgbeUndiDriverDiagnostics2;
extern EFI_DRIVER_HEALTH_PROTOCOL          gUndiDriverHealthProtocol;
#endif


#ifndef INTEL_KDNET
extern EFI_GUID                   gEfiStartStopProtocolGuid;

#define EFI_NII_POINTER_PROTOCOL_GUID \
  { \
    0xE3161450, 0xAD0F, 0x11D9, \
    { \
      0x96, 0x69, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 \
    } \
  }

EFI_GUID  gEfiNIIPointerGuid = EFI_NII_POINTER_PROTOCOL_GUID;
#endif

//
// function prototypes
//

#ifndef INTEL_KDNET
EFI_STATUS
EFIAPI
InitializeXgbeUNDIDriver (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );
#else
EFI_STATUS
InitializeXGigUNDIDriver (
  IN VOID
  );
#endif

EFI_STATUS
InitInstallConfigTable (
  IN VOID
  );

VOID
EFIAPI
InitUndiNotifyExitBs (
  IN EFI_EVENT Event,
  IN VOID      *Context
  );

EFI_STATUS
InitializePxeStruct (
  VOID
  );

#ifndef INTEL_KDNET

EFI_STATUS
EFIAPI
InitUndiDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
InitUndiDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
InitUndiDriverStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Controller,
  IN  UINTN                        NumberOfChildren,
  IN  EFI_HANDLE                   *ChildHandleBuffer
  );

#else // kdnet

EFI_STATUS
XgbeIsVendorDeviceSupported (
  UINT16    VendorId,
  UINT16    DeviceId
  );

EFI_STATUS
InitUndiDriverSupported (
  VOID
  );

EFI_STATUS
InitUndiDriverStart (
  VOID
  );

EFI_STATUS
EFIAPI
InitUndiDriverStop (
  VOID
  );

#endif

#ifndef INTEL_KDNET
EFI_STATUS
InitAppendMac2DevPath (
  IN OUT  EFI_DEVICE_PATH_PROTOCOL   **DevPtr,
  IN      EFI_DEVICE_PATH_PROTOCOL   *BaseDevPtr,
  IN      XGBE_DRIVER_DATA           *XgbeAdapter
  );
#endif

VOID
InitUndiPxeStructInit (
  PXE_SW_UNDI *PxePtr,
  UINTN       VersionFlag
  );

VOID
InitUndiPxeUpdate (
  IN XGBE_DRIVER_DATA *NicPtr,
  IN PXE_SW_UNDI      *PxePtr
  );

EFI_STATUS
EFIAPI
UnloadXGigUndiDriver (
  IN EFI_HANDLE ImageHandle
  );

UINT8
CheckSum (
  IN  VOID   *Buffer,
  IN  UINT16 Len
  );

EFI_STATUS
EFIAPI
HiiInit (
  IN UNDI_PRIVATE_DATA *XgbePrivate
  );

EFI_STATUS
EFIAPI
HiiUnload (
  IN UNDI_PRIVATE_DATA     *XgbePrivate
  );

EFI_STATUS
InitFirmwareManagementProtocol (
  IN UNDI_PRIVATE_DATA     *XgbePrivate
  );

EFI_STATUS
InitAdapterInformationProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
UninstallAdapterInformationProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
UninstallFirmwareManagementProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
InitPrivateDataAccessProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
UninstallPrivateDataAccessProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
InitFcoeConfigurationProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
UninstallFcoeConfigurationProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);


//
// end function prototypes
//

#ifndef INTEL_KDNET
//
// UNDI Class Driver Global Variables
//
EFI_DRIVER_BINDING_PROTOCOL gUndiDriverBinding = {
  InitUndiDriverSupported,  // Supported
  InitUndiDriverStart,      // Start
  InitUndiDriverStop,       // Stop
  VERSION_TO_HEX,           // Driver Version
  NULL,                     // ImageHandle
  NULL                      // Driver Binding Handle
};
#endif


EFI_STATUS
#ifndef INTEL_KDNET
EFIAPI
#endif
InitializeXGigUndiDriver(
#ifndef INTEL_KDNET
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
#else
  VOID
#endif
  )
/*++

Routine Description:
  Register Driver Binding protocol for this driver.

Arguments:
  ImageHandle - Handle to this driver image
  SystemTable - Pointer to UEFI system table

Returns:
  EFI_SUCCESS - Driver loaded.
  other       - Driver not loaded.

--*/
{
#ifndef INTEL_KDNET
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImageInterface;
#endif
  EFI_STATUS                Status;

#ifndef INTEL_KDNET
  gImageHandle  = ImageHandle;
  gSystemTable  = SystemTable;


  Status = EfiLibInstallDriverBinding (ImageHandle, SystemTable, &gUndiDriverBinding, ImageHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gUndiDriverBinding.DriverBindingHandle,
                  &gEfiComponentNameProtocolGuid,
                  &gUndiComponentName,
                  &gEfiComponentName2ProtocolGuid,
                  &gUndiComponentName2,
                  &gEfiDriverDiagnosticsProtocolGuid,
                  &gXgbeUndiDriverDiagnostics,
                  &gEfiDriverDiagnostics2ProtocolGuid,
                  &gXgbeUndiDriverDiagnostics2,
                  &gEfiDriverHealthProtocolGuid,
                  &gUndiDriverHealthProtocol,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("InstallMultipleProtocolInterfaces returns %x\n", Status));
    return Status;
  }

  if (SystemTable->Hdr.Revision >= EFI_2_10_SYSTEM_TABLE_REVISION) {
    DEBUGPRINT (INIT, ("Installing UEFI 2.1 Supported EFI Version Protocol.\n"));
    Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiDriverSupportedEfiVersionProtocolGuid,
                  &gUndiSupportedEFIVersion,
                  NULL
                  );
  }
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("InstallMultipleProtocolInterfaces returns %x\n", Status));
    return Status;
  }

  //
  // This protocol does not need to be closed because it uses the GET_PROTOCOL attribute
  //
  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID *) &LoadedImageInterface,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("%X: OpenProtocol returns %r\n", __LINE__, Status));
    return Status;
  }

  LoadedImageInterface->Unload = UnloadXGigUndiDriver;

  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_NOTIFY,
                  InitUndiNotifyExitBs,
                  NULL,
                  &EventNotifyExitBs
                  );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("%X: CreateEvent returns %r\n", __LINE__, Status));
    return Status;
  }
#endif

#ifdef INTEL_KDNET

    //
    // Zero out all globals.  This is required so that state is set to its
    // initial load state after resume from hibernate or a Bugcheck when
    // KdInitSystem is called and initialization is forced.
    //

    ActiveInterfaces = 0;
    ZeroMem(&ixgbe_pxe_31_struct, sizeof(ixgbe_pxe_31_struct));
    ZeroMem(&gXgbeUndiPrivateData, sizeof(gXgbeUndiPrivateData));

#endif

  Status = InitializePxeStruct ();

  return Status;
}

EFI_STATUS
InitializePxeStruct (
  VOID
  )
/*++

Routine Description:
  Allocate and initialize both (old and new) the !pxe structures here,
  there should only be one copy of each of these structure for any number
  of NICs this undi supports. Also, these structures need to be on a
  paragraph boundary as per the spec. so, while allocating space for these,
  make sure that there is space for 2 !pxe structures (old and new) and a
  32 bytes padding for alignment adjustment (in case)

Arguments:
  VOID

Returns:
  None

--*/
{
  EFI_STATUS  Status;

#ifdef INTEL_KDNET
  Status = EFI_SUCCESS;
#endif

#ifndef INTEL_KDNET

  Status = gBS->AllocatePool (
                  EfiBootServicesData,  // EfiRuntimeServicesData,
                  (sizeof (PXE_SW_UNDI) + sizeof (PXE_SW_UNDI) + 32),
                  &ixgbe_pxe_memptr
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("%X: AllocatePool returns %r\n", __LINE__, Status));
    return Status;
  }

#else

  ixgbe_pxe_memptr = &ixgbe_pxe_31_struct;

#endif


#ifndef INTEL_KDNET

  ZeroMem (
    ixgbe_pxe_memptr,
    sizeof (PXE_SW_UNDI) + sizeof (PXE_SW_UNDI) + 32
    );

#else

  ZeroMem (
    ixgbe_pxe_memptr,
    sizeof(PXE_SW_UNDI)
    );

#endif

#ifndef INTEL_KDNET

  //
  // check for paragraph alignment here, assuming that the pointer is
  // already 8 byte aligned.
  //
  if (((UINTN) ixgbe_pxe_memptr & 0x0F) != 0) {
    ixgbe_pxe_31 = (PXE_SW_UNDI *) ((UINTN) ((((UINTN) ixgbe_pxe_memptr) & (0xFFFFFFFFFFFFFFF0)) + 0x10));
  } else {
    ixgbe_pxe_31 = (PXE_SW_UNDI *) ixgbe_pxe_memptr;
  }

#else

  //
  // check for paragraph alignment here, assuming that the pointer is
  // already 8 byte aligned.
  //
  if (((UINTN) ixgbe_pxe_memptr & 0x0F) != 0) {
    Status = EFI_INVALID_PARAMETER;
  }

  ixgbe_pxe_31 = (PXE_SW_UNDI *) ixgbe_pxe_memptr;

#endif

  InitUndiPxeStructInit (ixgbe_pxe_31, 0x31); // 3.1 entry
  return Status;
}

VOID
EFIAPI
InitUndiNotifyExitBs (
  IN EFI_EVENT Event,
  IN VOID      *Context
)
/*++

Routine Description:
  When EFI is shuting down the boot services, we need to install a
  configuration table for UNDI to work at runtime!

Arguments:
  (Standard Event handler)

Returns:
  None

--*/
{
  UINT32  i;

  //
  // Divide Active interfaces by two because it tracks both the controller and
  // child handle, then shutdown the receive unit in case it did not get done
  // by the SNP
  //
  for (i = 0; i < ActiveInterfaces; i++) {
    if (XgbeDeviceList[i]->NicInfo.hw.device_id != 0) {
      IXGBE_WRITE_REG (&XgbeDeviceList[i]->NicInfo.hw, IXGBE_RXDCTL (0), 0);
      if (XgbeDeviceList[i]->NicInfo.hw.mac.type == ixgbe_mac_X550EM_a) {
        XgbeClearRegBits (&XgbeDeviceList[i]->NicInfo, IXGBE_CTRL_EXT, IXGBE_CTRL_EXT_DRV_LOAD);
      }
      XgbePciFlush (&XgbeDeviceList[i]->NicInfo);
    }
  }
}

EFI_STATUS
EFIAPI
UnloadXGigUndiDriver (
  IN EFI_HANDLE ImageHandle
  )
/*++

Routine Description:
  Callback to unload the XgbeUndi from memory.

Arguments:
  ImageHandle to driver.

Returns:
  EFI_SUCCESS            - This driver was unloaded successfully.
  EFI_INVALID_PARAMETER  - This driver was not unloaded.

--*/
{
#ifndef INTEL_KDNET

  EFI_HANDLE  *DeviceHandleBuffer;
  UINTN       DeviceHandleCount;
  UINTN       Index;

#endif

  EFI_STATUS  Status;

  DEBUGPRINT (INIT, ("XgbeUndiUnload ixgbe_pxe_31->IFcnt = %d\n", ixgbe_pxe_31->IFcnt));
  DEBUGWAIT (INIT);

#ifdef INTEL_KDNET

  Status = EFI_SUCCESS;

#else

  //
  // Get the list of all the handles in the handle database.
  // If there is an error getting the list, then the unload operation fails.
  //
  Status = gBS->LocateHandleBuffer (
                  AllHandles,
                  NULL,
                  NULL,
                  &DeviceHandleCount,
                  &DeviceHandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Disconnect the driver specified by ImageHandle from all the devices in the
  // handle database.
  //
  DEBUGPRINT (INIT, ("Active interfaces = %d\n", ActiveInterfaces));
  for (Index = 0; Index < DeviceHandleCount; Index++) {
    Status = gBS->DisconnectController (
                    DeviceHandleBuffer[Index],
                    ImageHandle,
                    NULL
                    );
  }

  DEBUGPRINT (INIT, ("Active interfaces = %d\n", ActiveInterfaces));

  //
  // Free the buffer containing the list of handles from the handle database
  //
  if (DeviceHandleBuffer != NULL) {
    gBS->FreePool (DeviceHandleBuffer);
  }

  if (ActiveInterfaces == 0) {
    //
    // Free PXE structures since they will no longer be needed
    //
    Status = gBS->FreePool (ixgbe_pxe_memptr);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("FreePool returns %x\n", Status));
      return Status;
    }

    //
    // Close both events before unloading
    //
    Status = gBS->CloseEvent (EventNotifyExitBs);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("CloseEvent EventNotifyExitBs returns %x\n", Status));
      return Status;
    }

    Status = gBS->UninstallMultipleProtocolInterfaces (
                    ImageHandle,
                    &gEfiDriverBindingProtocolGuid,
                    &gUndiDriverBinding,
                    &gEfiComponentNameProtocolGuid,
                    &gUndiComponentName,
                    &gEfiComponentName2ProtocolGuid,
                    &gUndiComponentName2,
                    &gEfiDriverDiagnosticsProtocolGuid,
                    &gXgbeUndiDriverDiagnostics,
                    &gEfiDriverDiagnostics2ProtocolGuid,
                    &gXgbeUndiDriverDiagnostics2,
                    &gEfiDriverHealthProtocolGuid,
                    &gUndiDriverHealthProtocol,
                    NULL
                    );

    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("UninstallMultipleProtocolInterfaces returns %x\n", Status));
      return Status;
    }

    if (gST->Hdr.Revision >= EFI_2_10_SYSTEM_TABLE_REVISION) {
      DEBUGPRINT (INIT, ("Uninstalling UEFI 2.1 Supported EFI Version Protocol.\n"));
      Status = gBS->UninstallMultipleProtocolInterfaces (
                  ImageHandle,
                  &gEfiDriverSupportedEfiVersionProtocolGuid,
                  &gUndiSupportedEFIVersion,
                  NULL
                  );
    }
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (CRITICAL, ("UninstallMultipleProtocolInterfaces returns %x\n", Status));
      return Status;
    }

  } else {
    DEBUGPRINT (INIT, ("Returning EFI_INVALID_PARAMETER\n"));
    DEBUGWAIT (INIT);
    return EFI_INVALID_PARAMETER;
  }

#endif

  return Status;
}

#ifndef INTEL_KDNET

EFI_STATUS
EFIAPI
InitUndiDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )

#else

#undef InitUndiDriverSupported

EFI_STATUS
InitUndiDriverSupported (
  VOID
  )
{
  PCI_TYPE00 Pci;
  ZeroMem(&Pci, sizeof(Pci));
  GetDevicePciInfo(&Pci);
  return XgbeIsVendorDeviceSupported(Pci.Hdr.VendorId, Pci.Hdr.DeviceId);
}

EFI_STATUS
XgbeIsVendorDeviceSupported (
  UINT16    VendorId,
  UINT16    DeviceId
  )

#endif

/*++

Routine Description:
  Test to see if this driver supports ControllerHandle. Any ControllerHandle
  than contains a  DevicePath, PciIo protocol, Class code of 2, Vendor ID of 0x8086,
  and DeviceId matching an Intel 10 Gig adapter can be supported.

Arguments:
  This                - Protocol instance pointer.
  Controller          - Handle of device to test.
  RemainingDevicePath - Not used.

Returns:
  EFI_SUCCESS         - This driver supports this device.
  other               - This driver does not support this device.

--*/
{
  EFI_STATUS          Status;
#ifndef INTEL_KDNET
  EFI_PCI_IO_PROTOCOL *PciIo;
#endif
  PCI_TYPE00          Pci;

#ifndef INTEL_KDNET

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (SUPPORTED, ("OpenProtocol1 %r, ", Status));
    return Status;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        0,
                        sizeof (PCI_CONFIG_HEADER),
                        &Pci
                        );
  DEBUGPRINT (SUPPORTED, ("Check devID %X, ", Pci.Hdr.DeviceId));

#else

  Status = EFI_SUCCESS;
  ZeroMem(&Pci, sizeof(Pci));

  Pci.Hdr.VendorId = VendorId;
  Pci.Hdr.DeviceId = DeviceId;

#endif

  if (!EFI_ERROR (Status)) {
    Status = EFI_UNSUPPORTED;
    if (IsDeviceIdSupported (Pci.Hdr.VendorId, Pci.Hdr.DeviceId)) {
        Status = EFI_SUCCESS;
    }

#ifndef INTEL_KDNET_BOOTROM_SUPPORTED_DEVICES_ONLY
    else
    if (Pci.Hdr.VendorId == IXGBE_INTEL_VENDOR_ID) {
      if (

          /* mac.type == ixgbe_mac_82598EB */
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598AF_SINGLE_PORT ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598AF_DUAL_PORT ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598AT ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598AT2 ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598EB_CX4 ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598_CX4_DUAL_PORT ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598_DA_DUAL_PORT ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598EB_XF_LR ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82598EB_SFP_LOM ||

          /* mac.type == ixgbe_mac_82599EB */
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_KX4 ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_KX4_MEZZ ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_XAUI_LOM ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_COMBO_BACKPLANE ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_KR ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_SFP ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_BACKPLANE_FCOE ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_SFP_FCOE ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_SFP_EM ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_SFP_SF2 ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_SFP_SF_QP ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_QSFP_SF_QP ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599EN_SFP ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_CX4 ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_82599_T3_LOM ||
            
          /* hw->mac.type = ixgbe_mac_X540 */
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X540T ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X540T1 ||

          /* hw->mac.type = ixgbe_mac_X550 */
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550T ||

          /* hw->mac.type = ixgbe_mac_X550EM_X */
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550EM_X_KX4 ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550EM_X_KR ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550EM_X_10G_T ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550EM_X_1G_T ||
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550EM_X_SFP ||

          /* hw->mac.type = ixgbe_mac_X550EM_a */
          Pci.Hdr.DeviceId == IXGBE_DEV_ID_X550EM_A_KR ||

          Pci.Hdr.DeviceId == 0xDEAD
          ) {
        Status = EFI_SUCCESS;
      }
    }

#endif
  }


#ifndef INTEL_KDNET

  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

#endif

  return Status;
}

#ifndef INTEL_KDNET

EFI_STATUS
EFIAPI
InitUndiDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )

#else

EFI_STATUS
InitUndiDriverStart (
  VOID
  )

#endif

/*++

Routine Description:
  Start this driver on Controller by opening PciIo and DevicePath protocol.
  Initialize PXE structures, create a copy of the Controller Device Path with the
  NIC's MAC address appended to it, install the NetworkInterfaceIdentifier protocol
  on the newly created Device Path.

Arguments:
  This                - Protocol instance pointer.
  Controller          - Handle of device to work with.
  RemainingDevicePath - Not used, always produce all possible children.

Returns:
  EFI_SUCCESS         - This driver is added to Controller.
  other               - This driver does not support this device.

--*/
{
#ifndef INTEL_KDNET

  EFI_DEVICE_PATH_PROTOCOL  *UndiDevicePath;
  EFI_PCI_IO_PROTOCOL       *PciIoFncs;
  UNDI_PRIVATE_DATA         *XgbePrivate;

#endif

  EFI_STATUS                Status;

  DEBUGPRINT (INIT, ("XgbeUndiDriverStart\n"));
  DEBUGWAIT (INIT);

#ifndef INTEL_KDNET

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (UNDI_PRIVATE_DATA),
                  (VOID **) &XgbePrivate
                  );

  if (EFI_ERROR (Status)) {
    goto UndiError;
  }

  ZeroMem ((UINT8 *) XgbePrivate, sizeof (UNDI_PRIVATE_DATA));

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &XgbePrivate->NicInfo.PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **) &UndiDevicePath,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  XgbePrivate->NIIProtocol_31.Id = (UINT64) (ixgbe_pxe_31);

#endif

  //
  // the IfNum index for the current interface will be the total number
  // of interfaces initialized so far
  //
  InitUndiPxeUpdate (&XgbePrivate->NicInfo, ixgbe_pxe_31);

  //
  // IFcnt should be equal to the total number of physical ports - 1
  //
  DEBUGWAIT (INIT);
#ifndef INTEL_KDNET
  XgbePrivate->NIIProtocol_31.IfNum             = ixgbe_pxe_31->IFcnt;
  XgbeDeviceList[XgbePrivate->NIIProtocol_31.IfNum] = XgbePrivate;
#else
  XgbeDeviceList[ixgbe_pxe_31->IFcnt] = XgbePrivate;
#endif

  ActiveInterfaces++;

#ifndef INTEL_KDNET

  XgbePrivate->Undi32BaseDevPath = UndiDevicePath;

#endif

  //
  // Initialize the UNDI callback functions.
  //
  XgbePrivate->NicInfo.Delay        = (VOID *) 0;
  XgbePrivate->NicInfo.Virt2Phys    = (VOID *) 0;
  XgbePrivate->NicInfo.Block        = (VOID *) 0;
  XgbePrivate->NicInfo.Map_Mem      = (void *) 0;
  XgbePrivate->NicInfo.UnMap_Mem    = (void *) 0;
  XgbePrivate->NicInfo.Sync_Mem     = (void *) 0;
  XgbePrivate->NicInfo.Unique_ID    = (UINT64) &XgbePrivate->NicInfo;
  XgbePrivate->NicInfo.VersionFlag  = 0x31;

#ifndef INTEL_KDNET

  XgbePrivate->DeviceHandle                  = NULL;

#endif

  //
  // Initialize PCI-E Bus and read PCI related information.
  //
  Status = XgbePciInit (&XgbePrivate->NicInfo);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("XgbePciInit fails: %r", Status));
    goto UndiErrorDeleteDevicePath;
  }

  Status = XgbeFirstTimeInit (XgbePrivate);

/*  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("XgbeFirstTimeInit fails: %r", Status));
    goto UndiErrorDeleteDevicePath;
  }
 */
  if (EFI_ERROR (Status) && (Status != EFI_ACCESS_DENIED)) {
    DEBUGPRINT (CRITICAL, ("XgbeFirstTimeInit fails: %r", Status));
    goto UndiErrorDeleteDevicePath;
  }

  XgbePrivate->NicInfo.UNDIEnabled = TRUE;

  if (Status == EFI_ACCESS_DENIED)
  {
    XgbePrivate->NicInfo.UNDIEnabled = FALSE;
  }

#ifndef INTEL_KDNET

  Status = InitAppendMac2DevPath (
              &XgbePrivate->Undi32DevPath,
              XgbePrivate->Undi32BaseDevPath,
              &XgbePrivate->NicInfo
              );
  
  if (EFI_ERROR (Status)) {
      DEBUGPRINT (INIT, ("Could not append mac address to the device path.  Error = %r\n", Status));
      goto UndiErrorDeleteDevicePath;
  }
  
  //
  // Figure out the controllers name for the Component Naming protocol
  // This must be performed after XgbeAppendMac2DevPath because we need the MAC
  // address of the controller to name it properly
  //
  ComponentNameInitializeControllerName (XgbePrivate);

#endif
  
  XgbePrivate->Signature                    = XGBE_UNDI_DEV_SIGNATURE;

#ifndef INTEL_KDNET

  XgbePrivate->NIIProtocol_31.Revision      = EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_REVISION;
  XgbePrivate->NIIProtocol_31.Type          = EfiNetworkInterfaceUndi;
  XgbePrivate->NIIProtocol_31.MajorVer      = PXE_ROMID_MAJORVER;
  XgbePrivate->NIIProtocol_31.MinorVer      = PXE_ROMID_MINORVER_31;
  XgbePrivate->NIIProtocol_31.ImageSize     = 0;
  XgbePrivate->NIIProtocol_31.ImageAddr     = 0;
  XgbePrivate->NIIProtocol_31.Ipv6Supported = TRUE;

  XgbePrivate->NIIProtocol_31.StringId[0]   = 'U';
  XgbePrivate->NIIProtocol_31.StringId[1]   = 'N';
  XgbePrivate->NIIProtocol_31.StringId[2]   = 'D';
  XgbePrivate->NIIProtocol_31.StringId[3]   = 'I';

#endif

  XgbePrivate->NicInfo.HwInitialized        = FALSE;

#ifndef INTEL_KDNET

  //
  // The PRO1000 COM protocol is used only by this driver.  It is done so that
  // we can get the NII protocol from either the parent or the child handle.  This is convenient
  // in the Diagnostic protocol because it allows the test to be run when called from either the
  // parent or child handle which makes it more user friendly.
  //
  XgbePrivate->NIIPointerProtocol.NIIProtocol_31 = &XgbePrivate->NIIProtocol_31;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Controller,
                  &gEfiNIIPointerGuid,
                  &XgbePrivate->NIIPointerProtocol,
                  NULL
                  );

  DEBUGPRINT (INIT, ("InstallMultipleProtocolInterfaces returns = %d %r\n", Status, Status));
  DEBUGWAIT (INIT);
  
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("Could not install protocol interfaces.  Error = %d %r\n", Status, Status));
    goto UndiErrorDeleteDevicePath;
  }
  
  XgbePrivate->DriverStop.StartDriver = StartDriver;
  XgbePrivate->DriverStop.StopDriver = StopDriver;
  

  if (TRUE == XgbePrivate->NicInfo.UNDIEnabled) {
      //
      // Install both the 3.0 and 3.1 NII protocols.
      //
      Status = gBS->InstallMultipleProtocolInterfaces (
                    &XgbePrivate->DeviceHandle,
                    &gEfiDevicePathProtocolGuid,
                    XgbePrivate->Undi32DevPath,
                    &gEfiNIIPointerGuid,
                    &XgbePrivate->NIIPointerProtocol,
                    &gEfiNetworkInterfaceIdentifierProtocolGuid_31,
                    &XgbePrivate->NIIProtocol_31,
                    &gEfiStartStopProtocolGuid,
                    &XgbePrivate->DriverStop,
                    NULL
                    );
  } else {
      //
      // Install both the 3.0 and 3.1 NII protocols.
      //
      Status = gBS->InstallMultipleProtocolInterfaces (
                    &XgbePrivate->DeviceHandle,
                    &gEfiDevicePathProtocolGuid,
                    XgbePrivate->Undi32DevPath,
                    &gEfiNIIPointerGuid,
                    &XgbePrivate->NIIPointerProtocol,
                    &gEfiStartStopProtocolGuid,
                    &XgbePrivate->DriverStop,
                    NULL
                    );
  }
  DEBUGPRINT (INIT, ("InstallMultipleProtocolInterfaces returns = %d %r\n", Status, Status));
  DEBUGWAIT (INIT);
  
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("Could not install protocol interfaces.  Error = %d %r\n", Status, Status));
    goto UndiErrorDeleteDevicePath;
  }


  Status = InitAdapterInformationProtocol (XgbePrivate);
  if (EFI_ERROR (Status) && (Status != EFI_UNSUPPORTED)) {
    DEBUGPRINT (CRITICAL, ("Could not install Firmware Managment protocol interfaces.  Error = %r\n", Status));
    goto UndiErrorDeleteDevicePath;
  }


  //
  // Open For Child Device
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIoFncs,
                  This->DriverBindingHandle,
                  XgbePrivate->DeviceHandle,
                  EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  );


  XgbePrivate->AltMacAddrSupported = IsAltMacAddrSupported(XgbePrivate);
  //
  // Save off the controller handle so we can disconnect the driver later
  //
  XgbePrivate->ControllerHandle = Controller;
  DEBUGPRINT(INIT, ("ControllerHandle = %X, DeviceHandle = %X\n",
   XgbePrivate->ControllerHandle,
   XgbePrivate->DeviceHandle
   ));
  //
  // HII may not install, so we do not want to return any errors
  //
  Status = HiiInit (XgbePrivate);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("HiiInit returns %r\n", Status));
  }
#endif

  DEBUGPRINT (INIT, ("XgbeUndiDriverStart - success\n"));
  
  return EFI_SUCCESS;

UndiErrorDeleteDevicePath:
#ifndef INTEL_KDNET
  XgbeDeviceList[XgbePrivate->NIIProtocol_31.IfNum] = NULL;
  gBS->FreePool (XgbePrivate->Undi32DevPath);
#else
  XgbeDeviceList[ixgbe_pxe_31->IFcnt] = NULL;
#endif
  InitUndiPxeUpdate (NULL, ixgbe_pxe_31);
  ActiveInterfaces--;

#ifndef INTEL_KDNET
UndiError:

  gBS->CloseProtocol (
        Controller,
        &gEfiDevicePathProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  gBS->FreePool ((VOID *) XgbePrivate);

  DEBUGPRINT (INIT, ("XgbeUndiDriverStart - error %x\n", Status));
#endif
  
  return Status;
}

EFI_STATUS
EFIAPI
InitUndiDriverStop (
#ifndef INTEL_KDNET
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN UINTN                          NumberOfChildren,
  IN EFI_HANDLE                     *ChildHandleBuffer
#else
  VOID
#endif
  )
/*++

Routine Description:
  Stop this driver on Controller by removing NetworkInterfaceIdentifier protocol and
  closing the DevicePath and PciIo protocols on Controller.

Arguments:
  This              - Protocol instance pointer.
  Controller        - Handle of device to stop driver on.
  NumberOfChildren  - How many children need to be stopped.
  ChildHandleBuffer - Child handle buffer to uninstall.

Returns:
  EFI_SUCCESS       - This driver is removed Controller.
  EFI_DEVICE_ERROR  - The driver could not be successfully stopped.
  other             - This driver was not removed from this device.

--*/
{
  EFI_STATUS                Status;
#ifndef INTEL_KDNET
  UNDI_PRIVATE_DATA         *XgbePrivate;
  EFI_NII_POINTER_PROTOCOL  *NIIPointerProtocol;
#endif

#ifdef INTEL_KDNET
  ActiveInterfaces--;
  Status = EFI_SUCCESS;
#endif

#ifndef INTEL_KDNET

  DEBUGPRINT (INIT, ("Number of children %d\n", NumberOfChildren));

  //
  // If we are called with less than one child handle it means that we already sucessfully
  // uninstalled
  //
  if (NumberOfChildren == 0) {
    //
    // Decrement the number of interfaces this driver is supporting
    //
    DEBUGPRINT (INIT, ("XgbeUndiPxeUpdate"));
    ActiveInterfaces--;

    //
    // The below is commmented out because it causes a crash when SNP, MNP, and ARP drivers are loaded
    // This has not been root caused but it is probably because some driver expects IFcnt not to change
    // This should be okay because when ifCnt is set when the driver is started it is based on ActiveInterfaces
    //  InitUndiPxeUpdate (NULL, ixgbe_pxe);
    //  InitUndiPxeUpdate (NULL, ixgbe_pxe_31);
    //
    DEBUGPRINT (INIT, ("removing gEfiDevicePathProtocolGuid\n"));
    Status = gBS->CloseProtocol (
                    Controller,
                    &gEfiDevicePathProtocolGuid,
                    This->DriverBindingHandle,
                    Controller
                    );
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (INIT, ("Close of gEfiDevicePathProtocolGuid failed with %r\n", Status));
      DEBUGWAIT (INIT);
      return Status;
    }

    Status = gBS->CloseProtocol (
                    Controller,
                    &gEfiPciIoProtocolGuid,
                    This->DriverBindingHandle,
                    Controller
                    );
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (INIT, ("Close of gEfiPciIoProtocolGuid failed with %r\n", Status));
      DEBUGWAIT (INIT);
      return Status;
    }

    return Status;
  }

  if (NumberOfChildren > 1) {
    DEBUGPRINT(INIT, ("Unexpected number of child handles.\n"));
    return EFI_INVALID_PARAMETER;
  }

  //
  //  Open an instance for the Network Interface Identifier Protocol so we can check to see
  // if the interface has been shutdown.  Does not need to be closed because we use the
  // GET_PROTOCOL attribute to open it.
  //
  Status = gBS->OpenProtocol (
                  ChildHandleBuffer[0],
                  &gEfiNIIPointerGuid,
                  (VOID **) &NIIPointerProtocol,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("%d: OpenProtocol returns %r\n", __LINE__, Status));
    return Status;
  }

  XgbePrivate = UNDI_PRIVATE_DATA_FROM_THIS (NIIPointerProtocol->NIIProtocol_31);

#endif

  DEBUGPRINT (INIT, ("State = %X\n", XgbePrivate->NicInfo.State));
  DEBUGWAIT (INIT);

#ifndef INTEL_KDNET

  //
  // Close the bus driver
  //
  DEBUGPRINT (INIT, ("removing gEfiPciIoProtocolGuid\n"));
  Status = gBS->CloseProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  This->DriverBindingHandle,
                  ChildHandleBuffer[0]
                  );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("Close of gEfiPciIoProtocolGuid failed with %r\n", Status));
    DEBUGWAIT (INIT);
    return Status;
  }

    Status = UninstallAdapterInformationProtocol(XgbePrivate);

  DEBUGPRINT (INIT, ("%d: UninstallMultipleProtocolInterfaces\n", __LINE__));
  if (TRUE == XgbePrivate->NicInfo.UNDIEnabled) {
      Status = gBS->UninstallMultipleProtocolInterfaces (
                      XgbePrivate->DeviceHandle,
                      &gEfiStartStopProtocolGuid,
                      &XgbePrivate->DriverStop,
                      &gEfiNetworkInterfaceIdentifierProtocolGuid_31,
                      &XgbePrivate->NIIProtocol_31,
                      &gEfiNIIPointerGuid,
                      &XgbePrivate->NIIPointerProtocol,
                      &gEfiDevicePathProtocolGuid,
                      XgbePrivate->Undi32DevPath,
                      NULL
                      );
  } else {
      Status = gBS->UninstallMultipleProtocolInterfaces (
                      XgbePrivate->DeviceHandle,
                      &gEfiStartStopProtocolGuid,
                      &XgbePrivate->DriverStop,
                      &gEfiNIIPointerGuid,
                      &XgbePrivate->NIIPointerProtocol,
                      &gEfiDevicePathProtocolGuid,
                      XgbePrivate->Undi32DevPath,
                      NULL
                      );
  }
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("1. UninstallMultipleProtocolInterfaces returns %r\n", Status));
    DEBUGWAIT (INIT);
  }

  //
  // If we get the ACCESS_DENIED status code usually calling UninstallMultipleProtocolInterfaces a second
  // time will uninstall the protocols successfully.
  //
  if (Status == EFI_ACCESS_DENIED) {
    DEBUGPRINT (INIT, ("%d: UninstallMultipleProtocolInterfaces\n", __LINE__));
    if (TRUE == XgbePrivate->NicInfo.UNDIEnabled) {
        Status = gBS->UninstallMultipleProtocolInterfaces (
                        XgbePrivate->DeviceHandle,
                        &gEfiStartStopProtocolGuid,
                        &XgbePrivate->DriverStop,
                        &gEfiNetworkInterfaceIdentifierProtocolGuid_31,
                        &XgbePrivate->NIIProtocol_31,
                        &gEfiNIIPointerGuid,
                        &XgbePrivate->NIIPointerProtocol,
                        &gEfiDevicePathProtocolGuid,
                        XgbePrivate->Undi32DevPath,
                        NULL
                        );
    } else {
        Status = gBS->UninstallMultipleProtocolInterfaces (
                        XgbePrivate->DeviceHandle,
                        &gEfiStartStopProtocolGuid,
                        &XgbePrivate->DriverStop,
                        &gEfiNIIPointerGuid,
                        &XgbePrivate->NIIPointerProtocol,
                        &gEfiDevicePathProtocolGuid,
                        XgbePrivate->Undi32DevPath,
                        NULL
                        );    
    }
    if (EFI_ERROR (Status)) {
      DEBUGPRINT (INIT, ("1. UninstallMultipleProtocolInterfaces returns %r\n", Status));
      DEBUGWAIT (INIT);
    }
  }

#endif

  if (TRUE == XgbePrivate->NicInfo.UNDIEnabled) {
      XgbeShutdown (&XgbePrivate->NicInfo);

      XgbeClearRegBits (&XgbePrivate->NicInfo, IXGBE_CTRL_EXT, IXGBE_CTRL_EXT_DRV_LOAD);
  }


#ifndef INTEL_KDNET

  Status = HiiUnload (XgbePrivate);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("HiiUnload returns %r\n", Status));
  }

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  Controller,
                  &gEfiNIIPointerGuid,
                  &XgbePrivate->NIIPointerProtocol,
                  NULL
                  );

#endif

#ifndef INTEL_KDNET

  DEBUGPRINT (INIT, ("EfiLibFreeUnicodeStringTable"));
  FreeUnicodeStringTable (XgbePrivate->ControllerNameTable);

  XgbeDeviceList[XgbePrivate->NIIProtocol_31.IfNum] = NULL;
  DEBUGPRINT (INIT, ("FreePool(XgbePrivate->Undi32DevPath)"));
  Status = gBS->FreePool (XgbePrivate->Undi32DevPath);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("FreePool(XgbePrivate->Undi32DevPath) returns %r\n", Status));
  }

  Status = XgbePrivate->NicInfo.PciIo->FreeBuffer (
                                        XgbePrivate->NicInfo.PciIo,
                                        UNDI_MEM_PAGES (MEMORY_NEEDED),
                                        (VOID *) (UINTN) XgbePrivate->NicInfo.MemoryPtr
                                        );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("PCI IO FreeBuffer returns %X\n", Status));
    DEBUGWAIT (INIT);
    return Status;
  }

  DEBUGPRINT (INIT, ("Attributes"));
  Status = XgbePrivate->NicInfo.PciIo->Attributes (
                                        XgbePrivate->NicInfo.PciIo,
                                        EfiPciIoAttributeOperationDisable,
                                        EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE,
                                        NULL
                                        );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (INIT, ("2. PCI IO Attributes returns %X\n", Status));
    DEBUGWAIT (INIT);
    return Status;
  }

  Status = gBS->FreePool (XgbePrivate);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(INIT, ("FreePool(XgbePrivate) returns %r\n", Status));
    DEBUGWAIT (INIT);
  }

#endif

  return Status;
}

#ifndef INTEL_KDNET

VOID
WaitForEnter (
  VOID
  )
/*++

Routine Description:
  This is only for debugging, it will pause and wait for the user to press <ENTER>
  Results AFTER this call are unpredicable. You can only be assured the code up to
  this call is working.

Arguments:
  VOID

Returns:
  none

--*/
{
  EFI_INPUT_KEY Key;

  AsciiPrint ("\nPress <Enter> to continue...\n");

  do {
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  } while (Key.UnicodeChar != 0xD);
}

EFI_STATUS
InitAppendMac2DevPath (
  IN OUT EFI_DEVICE_PATH_PROTOCOL  **DevPtr,
  IN EFI_DEVICE_PATH_PROTOCOL      *BaseDevPtr,
  IN XGBE_DRIVER_DATA              *XgbeAdapter
  )
/*++

Routine Description:
  Using the NIC data structure information, read the EEPROM to get the MAC address and then allocate space
  for a new devicepath (**DevPtr) which will contain the original device path the NIC was found on (*BaseDevPtr)
  and an added MAC node.

Arguments:
  DevPtr            - Pointer which will point to the newly created device path with the MAC node attached.
  BaseDevPtr        - Pointer to the device path which the UNDI device driver is latching on to.
  XgbeAdapter   - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  EFI_SUCCESS       - A MAC address was successfully appended to the Base Device Path.
  other             - Not enough resources available to create new Device Path node.

--*/
{
  MAC_ADDR_DEVICE_PATH      MacAddrNode;
  EFI_DEVICE_PATH_PROTOCOL  *EndNode;
  UINT16                    i;
  UINT16                    TotalPathLen;
  UINT16                    BasePathLen;
  EFI_STATUS                Status;
  UINT8                     *DevicePtr;

  DEBUGPRINT (INIT, ("XgbeAppendMac2DevPath\n"));

  ZeroMem ((char *) &MacAddrNode, sizeof (MacAddrNode));
  CopyMem (
    (char *) &MacAddrNode.MacAddress,
    (char *) XgbeAdapter->hw.mac.perm_addr,
    IXGBE_ETH_LENGTH_OF_ADDRESS
    );

  DEBUGPRINT (INIT, ("\n"));
  for (i = 0; i < 6; i++) {
    DEBUGPRINT (INIT, ("%2x ", MacAddrNode.MacAddress.Addr[i]));
  }

  DEBUGPRINT (INIT, ("\n"));
  for (i = 0; i < 6; i++) {
    DEBUGPRINT (INIT, ("%2x ", XgbeAdapter->hw.mac.perm_addr[i]));
  }

  DEBUGPRINT (INIT, ("\n"));
  DEBUGWAIT (INIT);

  MacAddrNode.Header.Type       = MESSAGING_DEVICE_PATH;
  MacAddrNode.Header.SubType    = MSG_MAC_ADDR_DP;
  MacAddrNode.Header.Length[0]  = sizeof (MacAddrNode);
  MacAddrNode.Header.Length[1]  = 0;
  MacAddrNode.IfType            = PXE_IFTYPE_ETHERNET;

  //
  // find the size of the base dev path.
  //
  EndNode = BaseDevPtr;
  while (!IsDevicePathEnd (EndNode)) {
    EndNode = NextDevicePathNode (EndNode);
  }

  BasePathLen = (UINT16) ((UINTN) (EndNode) - (UINTN) (BaseDevPtr));

  //
  // create space for full dev path
  //
  TotalPathLen = (UINT16) (BasePathLen + sizeof (MacAddrNode) + sizeof (EFI_DEVICE_PATH_PROTOCOL));

  Status = gBS->AllocatePool (
                  EfiBootServicesData,  // EfiRuntimeServicesData,
                  TotalPathLen,
                  &DevicePtr
                  );

  if (Status != EFI_SUCCESS) {
    return Status;
  }

  //
  // copy the base path, mac addr and end_dev_path nodes
  //
  *DevPtr = (EFI_DEVICE_PATH_PROTOCOL *) DevicePtr;
  CopyMem (DevicePtr, (char *) BaseDevPtr, BasePathLen);
  DevicePtr += BasePathLen;
  CopyMem (DevicePtr, (char *) &MacAddrNode, sizeof (MacAddrNode));
  DevicePtr += sizeof (MacAddrNode);
  CopyMem (DevicePtr, (char *) EndNode, sizeof (EFI_DEVICE_PATH_PROTOCOL));

  return EFI_SUCCESS;
}

#endif

VOID
InitUndiPxeUpdate (
  IN XGBE_DRIVER_DATA *NicPtr,
  IN PXE_SW_UNDI      *PxePtr
  )
/*++

Routine Description:
  When called with a null NicPtr, this routine decrements the number of NICs
  this UNDI is supporting and removes the NIC_DATA_POINTER from the array.
  Otherwise, it increments the number of NICs this UNDI is supported and
  updates the pxe.Fudge to ensure a proper check sum results.

Arguments:
    NicPtr = contains bus, dev, function etc.
    PxePtr = Pointer to the PXE structure

Returns:
  None

--*/
{
  if (NicPtr == NULL) {
    //
    // IFcnt is equal to the number of NICs this undi supports - 1
    //
    if (PxePtr->IFcnt > 0) {
      PxePtr->IFcnt--;
    }

    PxePtr->Fudge = (UINT8) (PxePtr->Fudge - CheckSum ((VOID *) PxePtr, PxePtr->Len));
    DEBUGPRINT (INIT, ("XgbeUndiPxeUpdate: ActiveInterfaces = %d\n", ActiveInterfaces));
    DEBUGPRINT (INIT, ("XgbeUndiPxeUpdate: PxePtr->IFcnt = %d\n", PxePtr->IFcnt));
    return ;
  }

  //
  // number of NICs this undi supports
  //
  PxePtr->IFcnt = ActiveInterfaces;
  PxePtr->Fudge = (UINT8) (PxePtr->Fudge - CheckSum ((VOID *) PxePtr, PxePtr->Len));
  DEBUGPRINT (INIT, ("XgbeUndiPxeUpdate: ActiveInterfaces = %d\n", ActiveInterfaces));
  DEBUGPRINT (INIT, ("XgbeUndiPxeUpdate: PxePtr->IFcnt = %d\n", PxePtr->IFcnt));

  return ;
}

VOID
InitUndiPxeStructInit (
  PXE_SW_UNDI *PxePtr,
  UINTN       VersionFlag
  )
/*++

Routine Description:
  Initialize the !PXE structure

Arguments:
    PxePtr = Pointer to the PXE structure to initialize
    VersionFlag = Indicates PXE version 3.0 or 3.1

Returns:
  EFI_SUCCESS         - This driver is added to Controller.
  other               - This driver does not support this device.

--*/
{
  //
  // initialize the !PXE structure
  //
  PxePtr->Signature = PXE_ROMID_SIGNATURE;
  PxePtr->Len       = sizeof (PXE_SW_UNDI);
  PxePtr->Fudge     = 0;  // cksum
  PxePtr->IFcnt     = 0;  // number of NICs this undi supports
  PxePtr->Rev       = PXE_ROMID_REV;
  PxePtr->MajorVer  = PXE_ROMID_MAJORVER;
  PxePtr->MinorVer  = PXE_ROMID_MINORVER_31;
  PxePtr->reserved1 = 0;

  PxePtr->Implementation = PXE_ROMID_IMP_SW_VIRT_ADDR |
    PXE_ROMID_IMP_FRAG_SUPPORTED |
    PXE_ROMID_IMP_CMD_LINK_SUPPORTED |
    PXE_ROMID_IMP_NVDATA_READ_ONLY |
    PXE_ROMID_IMP_STATION_ADDR_SETTABLE |
    PXE_ROMID_IMP_PROMISCUOUS_MULTICAST_RX_SUPPORTED |
    PXE_ROMID_IMP_PROMISCUOUS_RX_SUPPORTED |
    PXE_ROMID_IMP_BROADCAST_RX_SUPPORTED |
    PXE_ROMID_IMP_FILTERED_MULTICAST_RX_SUPPORTED |
    PXE_ROMID_IMP_SOFTWARE_INT_SUPPORTED |
    PXE_ROMID_IMP_PACKET_RX_INT_SUPPORTED;

#ifndef INTEL_KDNET

  PxePtr->EntryPoint    = (UINT64) UndiApiEntry;

#else

  PxePtr->EntryPoint    = (UINT64) XgbeUndiApiEntry;

#endif

  PxePtr->MinorVer      = PXE_ROMID_MINORVER_31;

  PxePtr->reserved2[0]  = 0;
  PxePtr->reserved2[1]  = 0;
  PxePtr->reserved2[2]  = 0;
  PxePtr->BusCnt        = 1;
  PxePtr->BusType[0]    = PXE_BUSTYPE_PCI;

  PxePtr->Fudge         = (UINT8) (PxePtr->Fudge - CheckSum ((VOID *) PxePtr, PxePtr->Len));

  //
  // return the !PXE structure
  //
}

UINT8
CheckSum (
  IN VOID   *Buffer,
  IN UINT16 Len
  )
/*++

Routine Description:
  This does an 8 bit check sum of the passed in buffer for Len bytes.
  This is primarily used to update the check sum in the SW UNDI header.

Arguments:
  Buffer         - Pointer to the passed in buffer to check sum
  Len            - Length of buffer to be check summed in bytes.

Returns:
  The 8-bit checksum of the array pointed to by buf.

--*/
{
  UINT8 Chksum;
  INT8  *Bp;

  Chksum = 0;

  if ((Bp = Buffer) != NULL) {
    while (Len--) {
      Chksum = (UINT8) (Chksum +*Bp++);

    }

  }

  return Chksum;
}

#ifndef INTEL_KDNET

EFI_STATUS
UndiGetControllerHealthStatus (
  IN  UNDI_PRIVATE_DATA              *UndiPrivateData,
  OUT EFI_DRIVER_HEALTH_STATUS       *DriverHealthStatus,
  OUT EFI_DRIVER_HEALTH_HII_MESSAGE  **MessageList
)
/*++

Routine Description:
  Return the health status of the controller. If there is a message status that
  is related to the current health status it is also prepared and returned
  by this function

Arguments:
  UndiPrivateData      - controller data
  DriverHealthStatus   - controller status to be returned
  MessageList          - pointer to the message list to be returned

Returns:
  EFI_STATUS

--*/
{
  EFI_STATUS                     Status;
  EFI_DRIVER_HEALTH_HII_MESSAGE  *ErrorMessage;


  BOOLEAN                        SfpModuleQualified;
  UINT32                         ErrorCount;

  DEBUGPRINT(HEALTH, ("\n"));

  if ((UndiPrivateData == NULL) || (DriverHealthStatus == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (MessageList != NULL) {
    *MessageList = NULL;
  }

  *DriverHealthStatus = EfiDriverHealthStatusHealthy;
  SfpModuleQualified  = TRUE;
  ErrorCount = 0;
  //
  // Check if module is supported/qualified for media types fiber and unknown.
  //
  if (UndiPrivateData->NicInfo.hw.phy.media_type == ixgbe_media_type_fiber ||
      UndiPrivateData->NicInfo.hw.phy.media_type == ixgbe_media_type_unknown) {
    if (!IsQualifiedSfpModule(&UndiPrivateData->NicInfo)) {
      DEBUGPRINT(HEALTH, ("*DriverHealthStatus = EfiDriverHealthStatusFailed\n"));
      *DriverHealthStatus = EfiDriverHealthStatusFailed;
      SfpModuleQualified  = FALSE;
      ErrorCount++;
    }
  }

  if (*DriverHealthStatus == EfiDriverHealthStatusHealthy) {
    return EFI_SUCCESS;
  }

  //
  // Create error message string
  //
  if ((MessageList == NULL) || (UndiPrivateData->HiiHandle == NULL)) {
    //
    // Text message are not requested or HII is not supported on this port
    //
    return EFI_SUCCESS;
  }

  //
  // Need to allocate space for two message entries:
  // the one for the message we need to pass to UEFI BIOS and
  // one for NULL entry indicating the end of list
  //
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  (ErrorCount + 1) * sizeof (EFI_DRIVER_HEALTH_HII_MESSAGE),
                  (VOID **) MessageList
                  );
  if (EFI_ERROR (Status)) {
    *MessageList = NULL;
    return EFI_OUT_OF_RESOURCES;
  }

  ErrorCount = 0;
  ErrorMessage = *MessageList;

  if (!SfpModuleQualified) {
    Status = HiiSetString (
               UndiPrivateData->HiiHandle,
               STRING_TOKEN(STR_DRIVER_HEALTH_MESSAGE),
               L"The driver failed to load because an unsupported module type was detected.",
               NULL // All languages will be updated
               );
    if (EFI_ERROR (Status)) {
      *MessageList = NULL;
      return EFI_OUT_OF_RESOURCES;
    }
    ErrorMessage[ErrorCount].HiiHandle = UndiPrivateData->HiiHandle;
    ErrorMessage[ErrorCount].StringId = STRING_TOKEN(STR_DRIVER_HEALTH_MESSAGE);
    ErrorCount++;
  }

  //
  // Indicate the end of list by setting HiiHandle to NULL
  //
  ErrorMessage[ErrorCount].HiiHandle = NULL;
  ErrorMessage[ErrorCount].StringId = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
UndiGetDriverHealthStatus (
  OUT EFI_DRIVER_HEALTH_STATUS       *DriverHealthStatus
)
/*++

Routine Description:
  Return the cumulative health status of all controllers managed by the driver

Arguments:
  DriverHealthStatus - controller status to be returned

Returns:
  EFI_STATUS

--*/
{
  EFI_STATUS                 Status;
  EFI_DRIVER_HEALTH_STATUS   HealthStatus;
  UINTN                      i;

  DEBUGPRINT(HEALTH, ("\n"));

  if (DriverHealthStatus == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  HealthStatus = EfiDriverHealthStatusHealthy;
  *DriverHealthStatus = EfiDriverHealthStatusHealthy;

  //
  // Iterate throug all controllers managed by this instance of driver and
  // ask them about their health status
  //
  for (i = 0; i < ActiveInterfaces; i++) {
    if (XgbeDeviceList[i]->NicInfo.hw.device_id != 0) {
      Status = UndiGetControllerHealthStatus(XgbeDeviceList[i], &HealthStatus, NULL);
      if (EFI_ERROR (Status)) {
        DEBUGPRINT(CRITICAL, ("UndiGetHealthStatus - %r\n"));
        return EFI_DEVICE_ERROR;
      }
      if (HealthStatus != EfiDriverHealthStatusHealthy) {
        *DriverHealthStatus = EfiDriverHealthStatusFailed;
      }
    }
  }

  return EFI_SUCCESS;
}

#endif
