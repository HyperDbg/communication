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

//
// Global Variables
//
#ifdef INTEL_KDNET
__declspec(align(16)) PXE_SW_UNDI             e1000_pxe_31_struct;  // 3.1 entry
#endif
VOID                                        *e1000_pxe_memptr = NULL;
PXE_SW_UNDI                                 *e1000_pxe_31 = NULL;  // 3.1 entry
UNDI_PRIVATE_DATA                           *e1000_UNDI32DeviceList[MAX_NIC_INTERFACES];
#ifndef INTEL_KDNET
NII_TABLE                                   e1000_UndiData;
#endif
UINT8                                       ActiveInterfaces = 0;
#ifndef INTEL_KDNET
EFI_EVENT                                   EventNotifyExitBs;
EFI_EVENT                                   EventNotifyVirtual;
EFI_SYSTEM_TABLE                            *gSystemTable;
#else
UNDI_PRIVATE_DATA         gUndiPrivateData;
UNDI_PRIVATE_DATA         *UndiPrivateData = &gUndiPrivateData;
#endif

//
// external Global Variables
//
extern UNDI_CALL_TABLE                      e1000_api_table[];
#ifndef INTEL_KDNET
extern EFI_COMPONENT_NAME_PROTOCOL          gUndiComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL         gUndiComponentName2;
extern EFI_DRIVER_SUPPORTED_EFI_VERSION_PROTOCOL gUndiSupportedEFIVersion;
extern EFI_DRIVER_CONFIGURATION_PROTOCOL    gGigUndiDriverConfiguration;
extern EFI_DRIVER_DIAGNOSTICS_PROTOCOL      gGigUndiDriverDiagnostics;
extern EFI_DRIVER_DIAGNOSTICS2_PROTOCOL     gGigUndiDriverDiagnostics2;
extern EFI_DRIVER_HEALTH_PROTOCOL           gUndiDriverHealthProtocol;

extern EFI_GUID                             gEfiStartStopProtocolGuid;

#define EFI_NII_POINTER_PROTOCOL_GUID \
  { \
    0xE3161450, 0xAD0F, 0x11D9, \
    { \
      0x96, 0x69, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 \
    } \
  }


EFI_GUID gEfiNIIPointerGuid = EFI_NII_POINTER_PROTOCOL_GUID;
#endif

//
// function prototypes
//
EFI_STATUS
#ifndef INTEL_KDNET
EFIAPI
#endif
InitializeGigUNDIDriver (
#ifndef INTEL_KDNET
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
#else
  VOID
#endif
  );

VOID
EFIAPI
GigUndiNotifyExitBs (
  EFI_EVENT Event,
  VOID      *Context
  );

EFI_STATUS
InitializePxeStruct(VOID);

#ifndef INTEL_KDNET

EFI_STATUS
EFIAPI
GigUndiDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
GigUndiDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
GigUndiDriverStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Controller,
  IN  UINTN                        NumberOfChildren,
  IN  EFI_HANDLE                   *ChildHandleBuffer
  );

#else

EFI_STATUS
GigUndiDriverSupported (
  VOID
  );

EFI_STATUS
GigUndiDriverStart (
  VOID
  );

EFI_STATUS
EFIAPI
GigUndiDriverStop (
  VOID
  );

#endif

#ifndef INTEL_KDNET
EFI_STATUS
GigAppendMac2DevPath (
  IN OUT  EFI_DEVICE_PATH_PROTOCOL  **DevPtr,
  IN      EFI_DEVICE_PATH_PROTOCOL  *BaseDevPtr,
  IN      GIG_DRIVER_DATA           *GigAdapterInfo
  );
#endif

VOID
GigUndiPxeStructInit (
  PXE_SW_UNDI *PxePtr,
  UINTN       VersionFlag
  );

VOID
GigUndiPxeUpdate (
  IN GIG_DRIVER_DATA *NicPtr,
  IN PXE_SW_UNDI     *PxePtr
  );

EFI_STATUS
EFIAPI
GigUndiUnload (
  IN EFI_HANDLE ImageHandle
  );

UINT8
e1000_ChkSum (
  IN  VOID   *Buffer,
  IN  UINT16 Len
  );

EFI_STATUS
EFIAPI
HiiInit (
  IN UNDI_PRIVATE_DATA *UndiPrivateData
  );

EFI_STATUS
EFIAPI
HiiUnload (
  UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
InitFirmwareManagementProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
InitAdapterInformationProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
UninstallFirmwareManagementProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

EFI_STATUS
UninstallAdapterInformationProtocol (
  IN UNDI_PRIVATE_DATA     *UndiPrivateData
  );

//
// end function prototypes
//
//
// This is a macro to convert the preprocessor defined version number into a hex value
// that can be registered with EFI.
//
#ifndef INTEL_KDNET
//
// The version number ends with additional .1 suffix for open source version of driver
//
#define VERSION_TO_HEX  ((MAJORVERSION << 24) + (MINORVERSION << 16) + (BUILDNUMBER / 10 << 12) + (BUILDNUMBER % 10 << 8) + 1)
//
// UNDI Class Driver Global Variables
//
EFI_DRIVER_BINDING_PROTOCOL gUndiDriverBinding = {
  GigUndiDriverSupported, // Supported
  GigUndiDriverStart,     // Start
  GigUndiDriverStop,      // Stop
  VERSION_TO_HEX,         // Driver Version
  NULL,                   // ImageHandle
  NULL                    // Driver Binding Handle
};
#endif


EFI_STATUS
#ifndef INTEL_KDNET
EFIAPI
#endif
InitializeGigUNDIDriver (
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
  (Standard EFI Image entry - EFI_IMAGE_ENTRY_POINT)

Returns:
  EFI_SUCCESS - Driver loaded.
  other       - Driver not loaded.

--*/
// GC_TODO:    ImageHandle - add argument and description to function comment
// GC_TODO:    SystemTable - add argument and description to function comment
{
#ifndef INTEL_KDNET
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImageInterface;
  EFI_STATUS                Status;

  gSystemTable = SystemTable;

  Status = EfiLibInstallDriverBinding (ImageHandle, SystemTable, &gUndiDriverBinding, ImageHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  DEBUGPRINT (INIT, ("SystemTable->Hdr.Revision = %x\n", SystemTable->Hdr.Revision));

  DEBUGPRINT (INIT, ("Installing UEFI 1.10/2.10 Driver Diags and Component Name protocols.\n"));
  Status = gBS->InstallMultipleProtocolInterfaces (
                    &gUndiDriverBinding.DriverBindingHandle,
                    &gEfiComponentNameProtocolGuid,
                    &gUndiComponentName,
                    &gEfiDriverDiagnosticsProtocolGuid,
                    &gGigUndiDriverDiagnostics,
                    &gEfiComponentName2ProtocolGuid,
                    &gUndiComponentName2,
                    &gEfiDriverDiagnostics2ProtocolGuid,
                    &gGigUndiDriverDiagnostics2,
                    &gEfiDriverConfigurationProtocolGuid,
                    &gGigUndiDriverConfiguration,
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

  if (EFI_ERROR(Status)) {
    DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
    return Status;
  }

  LoadedImageInterface->Unload = GigUndiUnload;

  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_NOTIFY,
                  GigUndiNotifyExitBs,
                  NULL,
                  &EventNotifyExitBs
                  );
  if (EFI_ERROR(Status)) {
    DEBUGPRINT(CRITICAL, ("CreateEvent returns %r\n", Status));
    return Status;
  }

#endif

#ifdef INTEL_KDNET

    EFI_STATUS                Status;

    //
    // Zero out all globals.  This is required so that state is set to its
    // initial load state after resume from hibernate or a Bugcheck when
    // KdInitSystem is called and initialization is forced.
    //

    ActiveInterfaces = 0;
    ZeroMem(&e1000_pxe_31_struct, sizeof(e1000_pxe_31_struct));
    ZeroMem(&gUndiPrivateData, sizeof(gUndiPrivateData));

#endif

  Status = InitializePxeStruct();

  return Status;
}


EFI_STATUS
InitializePxeStruct(VOID)
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
  EFI_STATUS Status;

#ifndef INTEL_KDNET

  Status = gBS->AllocatePool (
                  EfiBootServicesData, //EfiRuntimeServicesData,
                  (sizeof (PXE_SW_UNDI) + sizeof (PXE_SW_UNDI) + 32),
                  &e1000_pxe_memptr
                  );

  if (EFI_ERROR(Status)) {
    DEBUGPRINT(CRITICAL, ("AllocatePool returns %r\n", Status));
    return Status;
  }

#else

  Status = EFI_SUCCESS;
  e1000_pxe_memptr = &e1000_pxe_31_struct;

#endif

#ifndef INTEL_KDNET

  ZeroMem (
    e1000_pxe_memptr,
    sizeof (PXE_SW_UNDI) + sizeof (PXE_SW_UNDI) + 32
    );

#else

  ZeroMem (
    e1000_pxe_memptr,
    sizeof(PXE_SW_UNDI)
    );

#endif

#ifndef INTEL_KDNET

  //
  // check for paragraph alignment here, assuming that the pointer is
  // already 8 byte aligned.
  //
  if (((UINTN) e1000_pxe_memptr & 0x0F) != 0) {
    e1000_pxe_31 = (PXE_SW_UNDI *) ((UINTN)( (((UINTN)e1000_pxe_memptr) & (0xFFFFFFFFFFFFFFF0)) + 0x10 ));
  } else {
    e1000_pxe_31 = (PXE_SW_UNDI *) e1000_pxe_memptr;
  }

#else

  //
  // check for paragraph alignment here, assuming that the pointer is
  // already 8 byte aligned.
  //
  if (((UINTN) e1000_pxe_memptr & 0x0F) != 0) {
    Status = EFI_INVALID_PARAMETER;
  }

  e1000_pxe_31 = (PXE_SW_UNDI *) e1000_pxe_memptr;

#endif

  //
  // assuming that the sizeof pxe_31 is a 16 byte multiple
  //
  //e1000_pxe = (PXE_SW_UNDI*)((UINT8 *)(e1000_pxe_31) + sizeof (PXE_SW_UNDI));

 // GigUndiPxeStructInit (e1000_pxe, 0x30);     // 3.0 entry point
  GigUndiPxeStructInit (e1000_pxe_31, 0x31);  // 3.1 entry

  return Status;
}

VOID
EFIAPI
GigUndiNotifyExitBs (
  EFI_EVENT Event,
  VOID      *Context
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
// GC_TODO:    Context - add argument and description to function comment
{
  UINT32  i;

  //
  // Divide Active interfaces by two because it tracks both the controller and
  // child handle, then shutdown the receive unit in case it did not get done
  // by the SNP, and release the software semaphore aquired by the shared code
  //
  for (i = 0; i < ActiveInterfaces; i++) {
    if (e1000_UNDI32DeviceList[i]->NicInfo.hw.device_id != 0) {
      E1000_WRITE_REG (&(e1000_UNDI32DeviceList[i]->NicInfo.hw), E1000_RCTL, 0);
      E1000_WRITE_REG (&(e1000_UNDI32DeviceList[i]->NicInfo.hw), E1000_SWSM, 0);
      e1000_PciFlush (&e1000_UNDI32DeviceList[i]->NicInfo.hw);
      //
      // Delay for 10ms to allow in progress DMA to complete
      //
#ifndef INTEL_KDNET
      gBS->Stall(10000);
#else
      KeStallExecutionProcessor(10000);
#endif
    }
  }
}


EFI_STATUS
EFIAPI
GigUndiUnload (
  IN EFI_HANDLE ImageHandle
  )
/*++

Routine Description:
  Callback to unload the GigUndi from memory.

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

  DEBUGPRINT(INIT, ("GigUndiUnload e1000_pxe->IFcnt = %d\n", e1000_pxe_31->IFcnt));
  DEBUGWAIT (INIT);

#ifdef INTEL_KDNET
  Status = EFI_SUCCESS;
#endif

#ifndef INTEL_KDNET

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
    DEBUGPRINT(CRITICAL, ("LocateHandleBuffer returns %r\n", Status));
    return Status;
  }

  //
  // Disconnect the driver specified by ImageHandle from all the devices in the
  // handle database.
  //
  DEBUGPRINT(INIT, ("Active interfaces = %d\n", ActiveInterfaces));
  for (Index = 0; Index < DeviceHandleCount; Index++) {
    Status = gBS->DisconnectController (
                    DeviceHandleBuffer[Index],
                    ImageHandle,
                    NULL
                    );
  }
  DEBUGPRINT(INIT, ("Active interfaces = %d\n", ActiveInterfaces));

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
    Status = gBS->FreePool (e1000_pxe_memptr);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT(CRITICAL, ("FreePool returns %r\n", Status));
      return Status;
    }

    //
    // Close both events before unloading
    //
    Status = gBS->CloseEvent (EventNotifyExitBs);
    if (EFI_ERROR (Status)) {
      DEBUGPRINT(CRITICAL, ("CloseEvent returns %r\n", Status));
      return Status;
    }

    DEBUGPRINT (INIT, ("Uninstalling UEFI 1.10/2.10 Driver Diags and Component Name protocols.\n"));
    Status = gBS->UninstallMultipleProtocolInterfaces (
                    ImageHandle,
                    &gEfiDriverBindingProtocolGuid,
                    &gUndiDriverBinding,
                    &gEfiComponentNameProtocolGuid,
                    &gUndiComponentName,
                    &gEfiDriverDiagnosticsProtocolGuid,
                    &gGigUndiDriverDiagnostics,
                    &gEfiComponentName2ProtocolGuid,
                    &gUndiComponentName2,
                    &gEfiDriverDiagnostics2ProtocolGuid,
                    &gGigUndiDriverDiagnostics2,
                    &gEfiDriverConfigurationProtocolGuid,
                    &gGigUndiDriverConfiguration,
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
    DEBUGPRINT(INIT, ("Returning EFI_INVALID_PARAMETER\n"));
    DEBUGWAIT (INIT);
    return EFI_INVALID_PARAMETER;
  }

#endif

  return Status;
}

#ifndef INTEL_KDNET

EFI_STATUS
EFIAPI
GigUndiDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )

#else

#undef GigUndiDriverSupported

EFI_STATUS
GigUndiDriverSupported (
  VOID
  )

#endif

/*++

Routine Description:
  Test to see if this driver supports ControllerHandle. Any ControllerHandle
  than contains a  DevicePath, PciIo protocol, Class code of 2, Vendor ID of 0x8086,
  and DeviceId matching an Intel PRO/1000 adapter can be supported.

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
    return Status;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        0,
                        sizeof (PCI_CONFIG_HEADER),
                        &Pci
                        );

#else

  Status = EFI_SUCCESS;
  ZeroMem(&Pci, sizeof(Pci));
  GetDevicePciInfo(&Pci);

#endif

  if (!EFI_ERROR (Status)) {
    Status = EFI_UNSUPPORTED;

#ifndef INTEL_KDNET_1G_SERVER_MERGE_MARKER
    if (IsDeviceIdSupported (Pci.Hdr.VendorId, Pci.Hdr.DeviceId)) {
        Status = EFI_SUCCESS;
    }
#endif

#ifndef INTEL_KDNET_BOOTROM_SUPPORTED_DEVICES_ONLY
    else
    if (IsLegacy1GServerDeviceIdSupported (Pci.Hdr.VendorId, Pci.Hdr.DeviceId)) {
        Status = EFI_SUCCESS;
    }
#endif

#ifndef INTEL_KDNET_1G_CLIENT_MERGE_MARKER
    else
    if (Pci.Hdr.VendorId == INTEL_VENDOR_ID) {
      if (
#ifndef NO_82542_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82542 ||
#endif

#ifndef NO_82543_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82543GC_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82543GC_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82544EI_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82544EI_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82544GC_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82544GC_LOM ||
#endif

#ifndef NO_82540_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82540EM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82540EM_LOM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82540EP ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82540EP_LOM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82540EP_LP ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82545EM_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82545EM_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82545GM_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82545GM_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82545GM_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546EB_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546EB_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546EB_QUAD_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546GB_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546GB_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546GB_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546GB_PCIE ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546GB_QUAD_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3 ||
#endif

#ifndef NO_82541_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541EI ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541EI_MOBILE ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541ER_LOM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541ER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541GI ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541GI_LF ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82541GI_MOBILE ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82547EI ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82547EI_MOBILE ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82547GI ||
#endif

#ifndef NO_82571_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_SERDES_DUAL ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_SERDES_QUAD ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_QUAD_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571PT_QUAD_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_QUAD_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82571EB_QUAD_COPPER_LP ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82572EI ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82572EI_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82572EI_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82572EI_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82572EI ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82573E ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82573E_IAMT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82573L ||
#endif /* NO_82571_SUPPORT */

#ifndef NO_82574_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82574L ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82574LA ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82583V ||
#endif /* NO_82574_SUPPORT */

#ifndef NO_80003ES2LAN_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_80003ES2LAN_COPPER_DPT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_80003ES2LAN_SERDES_DPT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_80003ES2LAN_COPPER_SPT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_80003ES2LAN_SERDES_SPT ||
#endif /* NO_80003ES2LAN_SUPPORT */

#ifndef NO_ICH8LAN_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH8_IFE ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH8_IGP_M ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH8_IGP_M_AMT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH8_IGP_AMT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH8_IGP_C ||
#ifndef INTEL_KDNET_BOOTROM_SUPPORTED_DEVICES_ONLY
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH8_82567V_3 ||
#endif
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH9_IGP_M ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH9_IGP_M_AMT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH9_IGP_M_V ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH9_IGP_AMT ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH9_BM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH9_IGP_C ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH10_R_BM_LM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH10_R_BM_LF ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH10_R_BM_V ||
#ifndef INTEL_KDNET_BOOTROM_SUPPORTED_DEVICES_ONLY
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH10_D_BM_LM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH10_D_BM_LF ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_ICH10_D_BM_V ||
#endif
          Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_M_HV_LM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_M_HV_LC ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_D_HV_DM ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_D_HV_DC ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH2_LV_LM ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH2_LV_V ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_LPT_I217_LM||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_LPT_I217_V||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_LPTLP_I218_LM||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_LPTLP_I218_V||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_I218_LM2||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_I218_V2||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_I218_LM3 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_I218_V3 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_LM ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_V ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_LM2 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_V2 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_LBG_I219_LM3 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_LM4 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_V4 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_LM5 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_SPT_I219_V5 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_CNP_I219_LM6 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_CNP_I219_V6 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_CNP_I219_LM7 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_CNP_I219_V7 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_ICP_I219_LM8 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_ICP_I219_V8 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_ICP_I219_LM9 ||
         Pci.Hdr.DeviceId == E1000_DEV_ID_PCH_ICP_I219_V9 ||
#endif /* NO_ICH8LAN_SUPPORT */
#ifndef NO_82575_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82575EB_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82575EB_FIBER_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82575GB_QUAD_COPPER ||
#ifndef NO_82576_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576 ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_QUAD_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_QUAD_COPPER_ET2 ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_NS ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_NS_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82576_SERDES_QUAD ||
#endif /* NO_82576_SUPPORT */
#endif /* NO_82575_SUPPORT */

#ifndef NO_82580_SUPPORT
          Pci.Hdr.DeviceId == E1000_DEV_ID_82580_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82580_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82580_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82580_SGMII ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82580_COPPER_DUAL ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_82580_QUAD_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_DH89XXCC_SGMII ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_DH89XXCC_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_DH89XXCC_BACKPLANE ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_DH89XXCC_SFP ||
#endif /* NO_82580_SUPPORT */
          Pci.Hdr.DeviceId == E1000_DEV_ID_I350_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I350_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I350_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I350_SGMII ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I354_BACKPLANE_1GBPS ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I354_SGMII ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I354_BACKPLANE_2_5GBPS ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I210_COPPER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I210_FIBER ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I210_SERDES ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I210_SGMII ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I210_COPPER_FLASHLESS ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I210_SERDES_FLASHLESS ||
          Pci.Hdr.DeviceId == E1000_DEV_ID_I211_COPPER ||
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
GigUndiDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )

#else 

EFI_STATUS
GigUndiDriverStart (
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
  UNDI_PRIVATE_DATA     *UndiPrivateData;
  EFI_PCI_IO_PROTOCOL       *PciIoFncs;
  UINT64                    Result = 0;

#else

/*
  USHORT Command;
*/

#endif

  EFI_STATUS                Status;

  DEBUGPRINT(INIT, ("GigUndiDriverStart\n"));
  DEBUGWAIT(INIT);

#ifndef INTEL_KDNET

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (UNDI_PRIVATE_DATA),
                  (VOID **) &UndiPrivateData
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("AllocatePool returns %r\n", Status));
    DEBUGWAIT(CRITICAL);
    goto UndiError;
  }

  ZeroMem ((UINT8 *) UndiPrivateData, sizeof (UNDI_PRIVATE_DATA));

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &UndiPrivateData->NicInfo.PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
    DEBUGWAIT(CRITICAL);
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
    DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
    DEBUGWAIT(CRITICAL);
    return Status;
  }

  UndiPrivateData->NIIProtocol_31.Id = (UINT64) (e1000_pxe_31);

  //
  // Get the PCI Command options that are supported by this controller.
  //
  Status = UndiPrivateData->NicInfo.PciIo->Attributes (
                        UndiPrivateData->NicInfo.PciIo,
                        EfiPciIoAttributeOperationSupported,
                        0,
                        &Result
                        );

  DEBUGPRINT(INIT, ("Attributes supported %x\n", Result));
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("Attributes returns %r\n", Status));
    DEBUGWAIT(CRITICAL);
    goto UndiError;
  }

  //
  // Set the PCI Command options to enable device memory mapped IO,
  // port IO, and bus mastering.
  //
  Status = UndiPrivateData->NicInfo.PciIo->Attributes (
                        UndiPrivateData->NicInfo.PciIo,
                        EfiPciIoAttributeOperationEnable,
                        Result & (EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE),
                        NULL
                        );

  DEBUGPRINT(INIT, ("Attributes enabled %x\n", Result));
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("Attributes returns %r\n", Status));
    DEBUGWAIT(CRITICAL);
    goto UndiError;
  }

#else
/*
    ulResult = KdGetPciDataByOffset(KdNetData->Device->Bus,
                                    KdNetData->Device->Slot,
                                    (PVOID)&Command,
                                    FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                                    sizeof(USHORT));

    if(ulResult == sizeof(USHORT)) {
        Command |= 0x40;
        KdSetPciDataByOffset(Adapter->Device->Bus,
                             Adapter->Device->Slot,
                             (PVOID)&Command,
                             FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                             sizeof(USHORT));
    }

*/

#endif

  //
  // the IfNum index for the current interface will be the total number
  // of interfaces initialized so far
  //
  GigUndiPxeUpdate (&UndiPrivateData->NicInfo, e1000_pxe_31);
  //
  // IFcnt should be equal to the total number of physical ports - 1
  //
  DEBUGWAIT(INIT);
#ifndef INTEL_KDNET
  UndiPrivateData->NIIProtocol_31.IfNum  = e1000_pxe_31->IFcnt;
  e1000_UNDI32DeviceList[UndiPrivateData->NIIProtocol_31.IfNum] = UndiPrivateData;
#else
  e1000_UNDI32DeviceList[e1000_pxe_31->IFcnt] = UndiPrivateData;
#endif

  ActiveInterfaces++;
#ifndef INTEL_KDNET
  UndiPrivateData->Undi32BaseDevPath   = UndiDevicePath;
#endif

  //
  // Initialize the UNDI callback functions to 0 so that the default boot services
  // callback is used instead of the SNP callback.
  //
  UndiPrivateData->NicInfo.Delay       = (VOID *) 0;
  UndiPrivateData->NicInfo.Virt2Phys   = (VOID *) 0;
  UndiPrivateData->NicInfo.Block       = (VOID *) 0;
  UndiPrivateData->NicInfo.MapMem      = (void *) 0;
  UndiPrivateData->NicInfo.UnMapMem    = (void *) 0;
  UndiPrivateData->NicInfo.SyncMem     = (void *) 0;
  UndiPrivateData->NicInfo.Unique_ID   = (UINT64) &UndiPrivateData->NicInfo;
  UndiPrivateData->NicInfo.VersionFlag = 0x31;

  //
  // Allocate memory for transmit and receive resources.
  //

#ifndef INTEL_KDNET

  Status = UndiPrivateData->NicInfo.PciIo->AllocateBuffer (
                          UndiPrivateData->NicInfo.PciIo,
                          AllocateAnyPages,
                          EfiBootServicesData,
                          UNDI_MEM_PAGES (MEMORY_NEEDED),
                          (VOID **) &UndiPrivateData->NicInfo.MemoryPtr,
                          0
                          );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("AllocateBuffer returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
    return Status;
  }

#else

  UndiPrivateData->NicInfo.MemoryPtr = (UINT64)(UINTN)GetDeviceMemory();

#endif


  //
  // Perform the first time initialization of the hardware
  //
  Status = e1000_FirstTimeInit (&UndiPrivateData->NicInfo);
#ifndef INTEL_KDNET
  if (EFI_ERROR (Status) && (Status != EFI_ACCESS_DENIED)) {
      DEBUGPRINT(CRITICAL, ("e1000_FirstTimeInit returns %r\n", Status));
      DEBUGWAIT(CRITICAL);
    goto UndiErrorDeleteDevicePath;
  }
#else
  if (EFI_ERROR(Status)) {
    DEBUGPRINT (CRITICAL, ("e1000_FirstTimeInit fails: %r\n", Status));
    goto UndiErrorDeleteDevicePath;
  }
#endif

#ifndef INTEL_KDNET

  UndiPrivateData->NicInfo.UNDIEnabled = TRUE;

  if (Status == EFI_ACCESS_DENIED)
  {
    UndiPrivateData->NicInfo.UNDIEnabled = FALSE;
  }

UndiPrivateData->DeviceHandle = NULL;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &UndiPrivateData->DeviceHandle,
                  NULL
                  );


  Status = GigAppendMac2DevPath (
             &UndiPrivateData->Undi32DevPath,
             UndiPrivateData->Undi32BaseDevPath,
             &UndiPrivateData->NicInfo
             );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("GigAppendMac2DevPath returns %r\n", Status));
    DEBUGWAIT(CRITICAL);
    goto UndiErrorDeleteDevicePath;
  }

  //
  // Figure out the controllers name for the Component Naming protocol
  // This must be performed after GigAppendMac2DevPath because we need the MAC
  // address of the controller to name it properly
  //
  ComponentNameInitializeControllerName(UndiPrivateData);

#endif

  UndiPrivateData->Signature                     = GIG_UNDI_DEV_SIGNATURE;

#ifndef INTEL_KDNET
  UndiPrivateData->NIIProtocol_31.Revision       = EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_REVISION_31;
  UndiPrivateData->NIIProtocol_31.Type           = EfiNetworkInterfaceUndi;
  UndiPrivateData->NIIProtocol_31.MajorVer       = PXE_ROMID_MAJORVER;
  UndiPrivateData->NIIProtocol_31.MinorVer       = PXE_ROMID_MINORVER_31;
  UndiPrivateData->NIIProtocol_31.ImageSize      = 0;
  UndiPrivateData->NIIProtocol_31.ImageAddr      = 0;
  UndiPrivateData->NIIProtocol_31.Ipv6Supported  = TRUE;

  UndiPrivateData->NIIProtocol_31.StringId[0]    = 'U';
  UndiPrivateData->NIIProtocol_31.StringId[1]    = 'N';
  UndiPrivateData->NIIProtocol_31.StringId[2]    = 'D';
  UndiPrivateData->NIIProtocol_31.StringId[3]    = 'I';

//  UndiPrivateData->DeviceHandle                  = NULL;
#endif

  UndiPrivateData->NicInfo.HwInitialized         = FALSE;

#ifndef INTEL_KDNET

  //
  // The PRO1000 COM protocol is used only by this driver.  It is done so that
  // we can get the NII protocol from either the parent or the child handle.  This is convenient
  // in the Diagnostic protocol because it allows the test to be run when called from either the
  // parent or child handle which makes it more user friendly.
  //
  UndiPrivateData->NiiPointerProtocol.NIIProtocol_31  = &UndiPrivateData->NIIProtocol_31;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Controller,
                  &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                  NULL
                  );

  DEBUGPRINT(INIT, ("InstallMultipleProtocolInterfaces returns = %d %r\n", Status, Status));
  DEBUGWAIT(INIT);

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("InstallMultipleProtocolInterfaces returns Error = %d %r\n", Status, Status));
    DEBUGWAIT(CRITICAL);
    goto UndiErrorDeleteDevicePath;
  }

  UndiPrivateData->DriverStop.StartDriver = StartDriver;
  UndiPrivateData->DriverStop.StopDriver = StopDriver;


  if (TRUE == UndiPrivateData->NicInfo.UNDIEnabled) {
      Status = gBS->InstallMultipleProtocolInterfaces (
                      &UndiPrivateData->DeviceHandle,
                      &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                      &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                      &gEfiNetworkInterfaceIdentifierProtocolGuid_31, &UndiPrivateData->NIIProtocol_31,
                      &gEfiVlanProtocolGuid, &gGigUndiVlanData,
                      &gEfiStartStopProtocolGuid, &UndiPrivateData->DriverStop,
                      NULL
                      );
  } else {
      Status = gBS->InstallMultipleProtocolInterfaces (
                      &UndiPrivateData->DeviceHandle,
                      &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                      &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                      &gEfiVlanProtocolGuid, &gGigUndiVlanData,
                      &gEfiStartStopProtocolGuid, &UndiPrivateData->DriverStop,
                      NULL
                      );
  }
  DEBUGPRINT(INIT, ("InstallMultipleProtocolInterfaces returns = %d %r\n", Status, Status));
  DEBUGWAIT(INIT);

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("InstallMultipleProtocolInterfaces returns Error = %d %r\n", Status, Status));
    DEBUGWAIT(CRITICAL);
    goto UndiErrorDeleteDevicePath;
  }

  Status = InitAdapterInformationProtocol (UndiPrivateData);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT (CRITICAL, ("Could not install Adapter Information Protocol interface.  Error = %d %r\n", Status, Status));
    DEBUGWAIT(CRITICAL);
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
                  UndiPrivateData->DeviceHandle,
                  EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
  }

  //
  // Save off the controller handle so we can disconnect the driver later
  //
  UndiPrivateData->ControllerHandle = Controller;
  DEBUGPRINT(INIT, ("ControllerHandle = %x, DeviceHandle = %x\n",
   UndiPrivateData->ControllerHandle,
   UndiPrivateData->DeviceHandle
   ));

  UndiPrivateData->AltMacAddrSupported = IsAltMacAddrSupported(UndiPrivateData);

  //
  // HII may not install, so we do not want to return any errors
  //
  Status = HiiInit(UndiPrivateData);
  if (EFI_ERROR(Status)) {
    DEBUGPRINT(CRITICAL, ("HiiInit returns %r\n", Status));
  }

#endif

  return EFI_SUCCESS;

UndiErrorDeleteDevicePath:
#ifndef INTEL_KDNET

  e1000_UNDI32DeviceList[UndiPrivateData->NIIProtocol_31.IfNum] = NULL;
  gBS->FreePool (UndiPrivateData->Undi32DevPath);

#else

  e1000_UNDI32DeviceList[e1000_pxe_31->IFcnt] = NULL;

#endif

  GigUndiPxeUpdate (NULL, e1000_pxe_31);
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

#endif

  return Status;
}

EFI_STATUS
EFIAPI
GigUndiDriverStop (
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
  EFI_STATUS                                Status;
#ifndef INTEL_KDNET
  UNDI_PRIVATE_DATA                     *UndiPrivateData;
  EFI_NII_POINTER_PROTOCOL                    *NiiPointerProtocol;
#endif

#ifdef INTEL_KDNET
    ActiveInterfaces--;
    Status = EFI_SUCCESS;
#endif

#ifndef INTEL_KDNET

  DEBUGPRINT(INIT, ("Number of children %d\n", NumberOfChildren));
  //
  // If we are called with less than one child handle it means that we already sucessfully
  // uninstalled
  //
  if (NumberOfChildren == 0) {
    //
    // Decrement the number of interfaces this driver is supporting
    //
    DEBUGPRINT(INIT, ("GigUndiPxeUpdate"));
    ActiveInterfaces--;
    // The below is commmented out because it causes a crash when SNP, MNP, and ARP drivers are loaded
    // This has not been root caused but it is probably because some driver expects IFcnt not to change
    // This should be okay because when ifCnt is set when the driver is started it is based on ActiveInterfaces
    //  GigUndiPxeUpdate (NULL, e1000_pxe);
    //  GigUndiPxeUpdate (NULL, e1000_pxe_31);

    DEBUGPRINT(INIT, ("Removing gEfiDevicePathProtocolGuid\n"));
    Status = gBS->CloseProtocol (
                    Controller,
                    &gEfiDevicePathProtocolGuid,
                    This->DriverBindingHandle,
                    Controller
                    );
    if (EFI_ERROR (Status)) {
      DEBUGPRINT(CRITICAL, ("Close of gEfiDevicePathProtocolGuid failed with %r\n", Status));
      DEBUGWAIT (CRITICAL);
      return Status;
    }

    Status = gBS->CloseProtocol (
                    Controller,
                    &gEfiPciIoProtocolGuid,
                    This->DriverBindingHandle,
                    Controller
                    );
    if (EFI_ERROR (Status)) {
      DEBUGPRINT(CRITICAL, ("Close of gEfiPciIoProtocolGuid failed with %r\n", Status));
      DEBUGWAIT (CRITICAL);
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
                  (VOID **) &NiiPointerProtocol,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
      DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
      return Status;
  }

  UndiPrivateData = UNDI_PRIVATE_DATA_FROM_THIS (NiiPointerProtocol->NIIProtocol_31);

#endif

  DEBUGPRINT(INIT, ("State = %X\n", UndiPrivateData->NicInfo.State));
  DEBUGWAIT (INIT);

#ifndef INTEL_KDNET

  //
  // Close the bus driver
  //
  DEBUGPRINT(INIT, ("Removing gEfiPciIoProtocolGuid\n"));
  Status = gBS->CloseProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  This->DriverBindingHandle,
                  ChildHandleBuffer[0]
                  );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("Close of gEfiPciIoProtocolGuid failed with %r\n", Status));
    DEBUGWAIT (CRITICAL);
    return Status;
  }


  Status = UninstallAdapterInformationProtocol (UndiPrivateData);
  if ((EFI_ERROR (Status)) && (Status != EFI_UNSUPPORTED)) {
    DEBUGPRINT(CRITICAL, ("UninstallAdapterInformationProtocol returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
  }

  DEBUGPRINT(INIT, ("UninstallMultipleProtocolInterfaces\n"));

  if (TRUE == UndiPrivateData->NicInfo.UNDIEnabled) {
      Status = gBS->UninstallMultipleProtocolInterfaces (
                      UndiPrivateData->DeviceHandle,
                      &gEfiStartStopProtocolGuid, &UndiPrivateData->DriverStop,
                      &gEfiVlanProtocolGuid, &gGigUndiVlanData,
                      &gEfiNetworkInterfaceIdentifierProtocolGuid_31, &UndiPrivateData->NIIProtocol_31,
                      &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                      &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                      NULL
                      );
  } else {
      Status = gBS->UninstallMultipleProtocolInterfaces (
                      UndiPrivateData->DeviceHandle,
                      &gEfiStartStopProtocolGuid, &UndiPrivateData->DriverStop,
                      &gEfiVlanProtocolGuid, &gGigUndiVlanData,
                      &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                      &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                      NULL
                      );
  }
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
  }

  //
  // If we get the ACCESS_DENIED status code usually calling UninstallMultipleProtocolInterfaces a second
  // time will uninstall the protocols successfully.
  //
  if (Status == EFI_ACCESS_DENIED)
  {
    DEBUGPRINT(INIT, ("UninstallMultipleProtocolInterfaces\n"));
    Status = gBS->UninstallMultipleProtocolInterfaces (
                    UndiPrivateData->DeviceHandle,
                    &gEfiNetworkInterfaceIdentifierProtocolGuid_31, &UndiPrivateData->NIIProtocol_31,
                    &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                    &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %r\n", Status));
      DEBUGWAIT (CRITICAL);
    }
  }

#endif

  if (TRUE == UndiPrivateData->NicInfo.UNDIEnabled) {
      //
      // Call shutdown to clear DRV_LOAD bit and stop tx and rx
      //
      e1000_Shutdown (&UndiPrivateData->NicInfo);

      e1000_ClearRegBits(&UndiPrivateData->NicInfo, E1000_CTRL_EXT, E1000_CTRL_EXT_DRV_LOAD);
  }

#ifndef INTEL_KDNET

  Status = HiiUnload(UndiPrivateData);
  if (EFI_ERROR(Status)) {
    DEBUGPRINT(CRITICAL, ("HiiUnload returns %r\n", Status));
    return Status;
  }

    Status = gBS->UninstallMultipleProtocolInterfaces (
                    Controller,
                    &gEfiNIIPointerGuid, &UndiPrivateData->NiiPointerProtocol,
                    NULL
                    );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
    return Status;
  }

  DEBUGPRINT(INIT, ("EfiLibFreeUnicodeStringTable"));
  FreeUnicodeStringTable (UndiPrivateData->ControllerNameTable);

  e1000_UNDI32DeviceList[UndiPrivateData->NIIProtocol_31.IfNum] = NULL;
  DEBUGPRINT(INIT, ("FreePool(UndiPrivateData->Undi32DevPath)"));
  Status = gBS->FreePool (UndiPrivateData->Undi32DevPath);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("FreePool(UndiPrivateData->Undi32DevPath) returns %r\n", Status));
  }

  Status = UndiPrivateData->NicInfo.PciIo->FreeBuffer (
    UndiPrivateData->NicInfo.PciIo,
    UNDI_MEM_PAGES (MEMORY_NEEDED),
    (VOID*) (UINTN) UndiPrivateData->NicInfo.MemoryPtr
    );

  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("PCI IO FreeBuffer returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
    return Status;
  }


  DEBUGPRINT(INIT, ("Attributes"));
  Status = UndiPrivateData->NicInfo.PciIo->Attributes (
                                                      UndiPrivateData->NicInfo.PciIo,
                                                      EfiPciIoAttributeOperationDisable,
                                                      EFI_PCI_DEVICE_ENABLE,
                                                      NULL
                                                      );
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("PCI IO Attributes returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
    return Status;
  }

  Status = gBS->FreePool (UndiPrivateData);
  if (EFI_ERROR (Status)) {
    DEBUGPRINT(CRITICAL, ("FreePool(UndiPrivateData) returns %r\n", Status));
    DEBUGWAIT (CRITICAL);
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
// GC_TODO:    VOID - add argument and description to function comment
{
  EFI_INPUT_KEY Key;

  DEBUGPRINT(0xFFFF, ("\nPress <Enter> to continue...\n"));

  do {
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  } while (Key.UnicodeChar != 0xD);
}

EFI_STATUS
GigAppendMac2DevPath (
  IN OUT EFI_DEVICE_PATH_PROTOCOL **DevPtr,
  IN EFI_DEVICE_PATH_PROTOCOL     *BaseDevPtr,
  IN GIG_DRIVER_DATA              *GigAdapterInfo
  )
/*++

Routine Description:
  Using the NIC data structure information, read the EEPROM to get the MAC address and then allocate space
  for a new devicepath (**DevPtr) which will contain the original device path the NIC was found on (*BaseDevPtr)
  and an added MAC node.

Arguments:
  DevPtr            - Pointer which will point to the newly created device path with the MAC node attached.
  BaseDevPtr        - Pointer to the device path which the UNDI device driver is latching on to.
  AdapterInfo       - Pointer to the NIC data structure information which the UNDI driver is layering on..

Returns:
  EFI_SUCCESS       - A MAC address was successfully appended to the Base Device Path.
  other             - Not enough resources available to create new Device Path node.

--*/
// GC_TODO:    GigAdapterInfo - add argument and description to function comment
{
  MAC_ADDR_DEVICE_PATH      MacAddrNode;
  EFI_DEVICE_PATH_PROTOCOL  *EndNode;
  UINT16                    i;
  UINT16                    TotalPathLen;
  UINT16                    BasePathLen;
  EFI_STATUS                Status;
  UINT8                     *DevicePtr;

  DEBUGPRINT(INIT, ("GigAppendMac2DevPath\n"));

  ZeroMem (
    (char *) &MacAddrNode,
    sizeof (MacAddrNode)
    );

  E1000_COPY_MAC (MacAddrNode.MacAddress.Addr, GigAdapterInfo->hw.mac.perm_addr);

  DEBUGPRINT(INIT, ("\n"));
  for (i = 0; i < 6; i++) {
    DEBUGPRINT(INIT, ("%2x ", MacAddrNode.MacAddress.Addr[i]));
  }

  DEBUGPRINT(INIT, ("\n"));
  for (i = 0; i < 6; i++) {
    DEBUGPRINT(INIT, ("%2x ", GigAdapterInfo->hw.mac.perm_addr[i]));
  }

  DEBUGPRINT(INIT, ("\n"));
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
                  EfiBootServicesData, //EfiRuntimeServicesData,
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
GigUndiPxeUpdate (
  IN GIG_DRIVER_DATA *NicPtr,
  IN PXE_SW_UNDI     *PxePtr
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

    PxePtr->Fudge = (UINT8) (PxePtr->Fudge - e1000_ChkSum ((VOID *) PxePtr, PxePtr->Len));
    DEBUGPRINT(INIT, ("GigUndiPxeUpdate: ActiveInterfaces = %d\n", ActiveInterfaces));
    DEBUGPRINT(INIT, ("GigUndiPxeUpdate: PxePtr->IFcnt = %d\n", PxePtr->IFcnt));
    return ;
  }

  //
  // number of NICs this undi supports
  //
  PxePtr->IFcnt = ActiveInterfaces;
  PxePtr->Fudge = (UINT8) (PxePtr->Fudge - e1000_ChkSum ((VOID *) PxePtr, PxePtr->Len));
  DEBUGPRINT(INIT, ("GigUndiPxeUpdate: ActiveInterfaces = %d\n", ActiveInterfaces));
  DEBUGPRINT(INIT, ("GigUndiPxeUpdate: PxePtr->IFcnt = %d\n", PxePtr->IFcnt));

  return ;
}

VOID
GigUndiPxeStructInit (
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

  PxePtr->EntryPoint    = (UINT64) e1000_UNDI_APIEntry;
  PxePtr->MinorVer      = PXE_ROMID_MINORVER_31;
  PxePtr->reserved2[0]  = 0;
  PxePtr->reserved2[1]  = 0;
  PxePtr->reserved2[2]  = 0;
  PxePtr->BusCnt        = 1;
  PxePtr->BusType[0]    = PXE_BUSTYPE_PCI;

  // TODO: Check if this is correct.
  PxePtr->Fudge         = (UINT8) (PxePtr->Fudge - e1000_ChkSum ((VOID *) PxePtr, PxePtr->Len));

  //
  // return the !PXE structure
  //
}

UINT8
e1000_ChkSum (
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
      Chksum = (UINT8) (Chksum + *Bp++);
    }
  }

  return Chksum;
}
