/**************************************************************************

Copyright (c) 2001-2019, Intel Corporation
All rights reserved.

Source code in this module is released to Microsoft per agreement INTC093053_DA
solely for the purpose of supporting Intel Ethernet hardware in the Ethernet
transport layer of Microsoft's Kernel Debugger.

***************************************************************************/

/** @file
  Driver initialization and deinitialization functions.

  Implementation of Driver Binding protocol and UNDI initialization functions.

  Copyright (C) 2015 Intel Corporation. <BR>
  All rights reserved. This software and associated documentation
  (if any) is furnished undr a license and may only be used or
  copied in accordance with the terms of the license. Except as
  permitted by such license, no part of this software or
  documentation may be reproduced, stored in a retrieval system, or
  transmitted in any form or by any means without the express
  written consent of Intel Corporation.

**/
#ifndef INTEL_KDNET
#include <Protocol/HiiString.h>
#include <Library/HiiLib.h>
#endif

#include "I40e.h"
#include "DeviceSupport.h"
#include "Decode.h"

#ifdef INTEL_KDNET
// Remap the 40GBit ActiveInterface count so it doesn't collide with the 1GBit count.
#define ActiveInterfaces Active40GbitInterfaces
__declspec(align(16)) PXE_SW_UNDI            i40e_pxe_31_struct;  // 3.1 entry
#endif

#ifndef INTEL_KDNET
// External Variables
extern EFI_DRIVER_DIAGNOSTICS_PROTOCOL       gUndiDriverDiagnostics;
extern EFI_DRIVER_DIAGNOSTICS2_PROTOCOL      gUndiDriverDiagnostics2;
extern EFI_DRIVER_STOP_PROTOCOL              gUndiDriverStop;
extern EFI_DRIVER_START_PROTOCOL             gUndiDriverStart
#endif

//
// Global Variables
//

VOID                    *i40e_pxe_memptr = NULL;
PXE_SW_UNDI             *i40e_pxe_31 = NULL; // 3.1 entry
UNDI_PRIVATE_DATA       *Undi32DeviceList[MAX_NIC_INTERFACES];
EFI_EVENT               EventNotifyExitBs;
#ifdef INTEL_KDNET
UNDI_PRIVATE_DATA       gI40eUndiPrivateData;
UNDI_PRIVATE_DATA       *I40ePrivate = &gI40eUndiPrivateData;
#endif
UINT8                   ActiveInterfaces = 0;

EFI_STATUS
EFIAPI
HiiInit(
    UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
EFIAPI
HiiUnload(
    UNDI_PRIVATE_DATA     *UndiPrivateData
);

#ifndef INTEL_KDNET

EFI_STATUS
EFIAPI
i40eUndiDriverSupported(
    IN EFI_DRIVER_BINDING_PROTOCOL  *This,
    IN EFI_HANDLE                   Controller,
    IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
);

EFI_STATUS
EFIAPI
i40eUndiDriverStart(
    IN EFI_DRIVER_BINDING_PROTOCOL  *This,
    IN EFI_HANDLE                   Controller,
    IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
);

EFI_STATUS
EFIAPI
i40eUndiDriverStop(
    IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
    IN  EFI_HANDLE                   Controller,
    IN  UINTN                        NumberOfChildren,
    IN  EFI_HANDLE                   *ChildHandleBuffer
);

#else

EFI_STATUS
i40eIsVendorDeviceSupported(
    UINT16    VendorId,
    UINT16    DeviceId
);

EFI_STATUS
i40eUndiDriverSupported(
    VOID
);

EFI_STATUS
i40eUndiDriverStart(
    VOID
);

EFI_STATUS
i40eUndiDriverStop(
    VOID
);

EFI_STATUS
i40eInitializeUNDIDriver(
    IN VOID
);

#endif

EFI_STATUS
InitFirmwareManagementProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
UninstallFirmwareManagementProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
InitAdapterInformationProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
UninstallAdapterInformationProtocol(
    IN UNDI_PRIVATE_DATA     *UndiPrivateData
);

EFI_STATUS
InitializePxeStruct(
    VOID
);

VOID
InitPxeStructInit(
    PXE_SW_UNDI *PxePtr,
    UINTN       VersionFlag
);

VOID
UndiPxeUpdate(
    IN I40E_DRIVER_DATA *NicPtr,
    IN PXE_SW_UNDI      *PxePtr
);

#ifndef INTEL_KDNET
EFI_STATUS
AppendMac2DevPath(
    IN OUT EFI_DEVICE_PATH_PROTOCOL **DevPtr,
    IN EFI_DEVICE_PATH_PROTOCOL     *BaseDevPtr,
    IN I40E_DRIVER_DATA             *AdapterInfo
);
#endif


UINT8
ChkSum(
    IN VOID   *Buffer,
    IN UINT16 Len
);


//
// UNDI Class Driver Global Variables
//
#ifndef INTEL_KDNET
EFI_DRIVER_BINDING_PROTOCOL  gUndiDriverBinding = {
  i40eUndiDriverSupported,
  i40eUndiDriverStart,
  i40eUndiDriverStop,
  VERSION_TO_HEX,
  NULL,
  NULL
};
#endif

extern EFI_GUID                   gEfiStartStopProtocolGuid;

#define EFI_NII_POINTER_PROTOCOL_GUID \
  { \
    0xE3161450, 0xAD0F, 0x11D9, \
    { \
      0x96, 0x69, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 \
    } \
  }

EFI_GUID gEfiNIIPointerGuid = EFI_NII_POINTER_PROTOCOL_GUID;


VOID
EFIAPI
UndiNotifyExitBs(
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
        if (Undi32DeviceList[i]->NicInfo.hw.device_id != 0) {            
            //I40eReceiveStop(&Undi32DeviceList[i]->NicInfo);
            Undi32DeviceList[i]->NicInfo.hw.numExitBS++;
#ifndef INTEL_KDNET
            gBS->Stall(10000);
#else
            KeStallExecutionProcessor(10000);
#endif
        }
    }
}


#ifdef INTEL_KDNET
UINT32
CalcI40eContextSize()
{
    // Calculate the number of bytes required by all structures needed for
    // KDNET driver since we do not have runtime alloc routines.

    UINT32 ContextSize = (2 * I40E_AQ_RING_DESCRIPTOR_SIZE *
                            sizeof(struct i40e_aq_desc)) + // ASQ/ARQ descriptor
                        (2 * I40E_AQ_RING_DESCRIPTOR_SIZE *
                            I40E_AQ_INDIRECT_BUFFER_SIZE) + // ASQ/ARQ indirect buffers
                        (I40E_FPM_SD_SIZE)+ // SD backing page
                        (I40E_NUM_TX_RX_DESCRIPTORS *
                            sizeof(struct i40e_tx_desc)) + // TX ring descriptor
                        (I40E_NUM_TX_RX_DESCRIPTORS *
                            sizeof(union i40e_32byte_rx_desc)) + // RX ring descriptor
                        ((I40E_RXBUFFER_2048 + RECEIVE_BUFFER_ALIGN_SIZE) * I40E_NUM_TX_RX_DESCRIPTORS) + // RX receive buffer
                        (I40E_FPM_PD_SIZE)+ // PD page
                        (2 * EFI_PAGE_SIZE); // Page alignment buffer for ASQ and ARQ 
    return ContextSize;
}
#endif


#ifndef INTEL_KDNET
EFI_STATUS
EFIAPI
InitializeUNDIDriver(
    IN EFI_HANDLE           ImageHandle,
    IN EFI_SYSTEM_TABLE     *SystemTable
)
#else
EFI_STATUS
i40eInitializeUNDIDriver(
    IN VOID
)
#endif
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
    EFI_STATUS    Status = EFI_SUCCESS;

    do {

#ifndef INTEL_KDNET

        // 
        // Install all the required driver protocols
        //
        Status = EfiLibInstallAllDriverProtocols2(
            ImageHandle,
            SystemTable,
            &gUndiDriverBinding,
            ImageHandle,
            &gUndiComponentName,
            &gUndiComponentName2,
            NULL,
            NULL,
            &gUndiDriverDiagnostics,
            &gUndiDriverDiagnostics2
        );
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("EfiLibInstallAllDriverProtocols2 - %r\n", Status));
            break;
        }
        //
        // Install UEFI 2.1 Supported EFI Version Protocol
        // 
        if (SystemTable->Hdr.Revision >= EFI_2_10_SYSTEM_TABLE_REVISION) {
            DEBUGPRINT(INIT, ("Installing UEFI 2.1 Supported EFI Version Protocol.\n"));
            Status = gBS->InstallMultipleProtocolInterfaces(
                &ImageHandle,
                &gEfiDriverSupportedEfiVersionProtocolGuid,
                &gUndiSupportedEFIVersion,
                NULL
            );
        }
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("Install UEFI 2.1 Supported EFI Version Protocol - %r\n", Status));
            break;
        }
        //
        // Install Driver Health Protocol for driver
        //
        Status = gBS->InstallMultipleProtocolInterfaces(
            &ImageHandle,
            &gEfiDriverHealthProtocolGuid,
            &gUndiDriverHealthProtocol,
            NULL
        );
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("Installing UEFI 2.2 Driver Health Protocol - %r\n", Status));
            return Status;
        }

        Status = gBS->CreateEvent(
            EVT_SIGNAL_EXIT_BOOT_SERVICES,
            TPL_NOTIFY,
            UndiNotifyExitBs,
            NULL,
            &EventNotifyExitBs
        );
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("%X: CreateEvent returns %r\n", __LINE__, Status));
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
        ZeroMem(&i40e_pxe_31_struct, sizeof(i40e_pxe_31_struct));
        ZeroMem(&gI40eUndiPrivateData, sizeof(gI40eUndiPrivateData));

#endif

        Status = InitializePxeStruct();
    } while (0);

    return Status;
}


EFI_STATUS
EFIAPI
i40eGigUndiUnload(
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
    EFI_STATUS  Status = EFI_SUCCESS;

    DEBUGPRINT(INIT, ("GigUndiUnload pxe->IFcnt = %d\n", i40e_pxe_31->IFcnt));
    DEBUGWAIT(INIT);

#ifndef INTEL_KDNET
    do {
        //
        // Get the list of all the handles in the handle database.
        // If there is an error getting the list, then the unload operation fails.
        //
        Status = gBS->LocateHandleBuffer(
            AllHandles,
            NULL,
            NULL,
            &DeviceHandleCount,
            &DeviceHandleBuffer
        );
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("LocateHandleBuffer returns %r\n", Status));
            break;
        }

        //
        // Disconnect the driver specified by ImageHandle from all the devices in the
        // handle database.
        //
        DEBUGPRINT(INIT, ("Active interfaces = %d\n", ActiveInterfaces));
        for (Index = 0; Index < DeviceHandleCount; Index++) {
            Status = gBS->DisconnectController(
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
            gBS->FreePool(DeviceHandleBuffer);
        }

        if (ActiveInterfaces == 0) {
            //
            // Free PXE structures since they will no longer be needed
            //
            Status = gBS->FreePool(pxe_memptr);
            if (EFI_ERROR(Status)) {
                DEBUGPRINT(CRITICAL, ("FreePool returns %r\n", Status));
                break;
            }

            //
            // Close both events before unloading
            //
            Status = gBS->CloseEvent(EventNotifyExitBs);
            if (EFI_ERROR(Status)) {
                DEBUGPRINT(CRITICAL, ("CloseEvent returns %r\n", Status));
                break;
            }
            DEBUGPRINT(INIT, ("Uninstalling UEFI 1.10/2.10 Driver Diags and Component Name protocols.\n"));
            Status = gBS->UninstallMultipleProtocolInterfaces(
                ImageHandle,
                &gEfiDriverBindingProtocolGuid,
                &gUndiDriverBinding,
                &gEfiComponentNameProtocolGuid,
                &gUndiComponentName,
                &gEfiDriverDiagnosticsProtocolGuid,
                &gUndiDriverDiagnostics,
                &gEfiComponentName2ProtocolGuid,
                &gUndiComponentName2,
                &gEfiDriverHealthProtocolGuid,
                &gUndiDriverHealthProtocol,
                &gEfiDriverDiagnostics2ProtocolGuid,
                &gUndiDriverDiagnostics2,
                NULL
            );

            if (EFI_ERROR(Status)) {
                DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %x\n", Status));
                break;
            }

            if (gST->Hdr.Revision >= EFI_2_10_SYSTEM_TABLE_REVISION) {
                DEBUGPRINT(INIT, ("Uninstalling UEFI 2.1 Supported EFI Version Protocol.\n"));
                Status = gBS->UninstallMultipleProtocolInterfaces(
                    ImageHandle,
                    &gEfiDriverSupportedEfiVersionProtocolGuid,
                    &gUndiSupportedEFIVersion,
                    NULL
                );
            }
            if (EFI_ERROR(Status)) {
                DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %x\n", Status));
                break;
            }

        }
        else {
            DEBUGPRINT(INIT, ("Returning EFI_INVALID_PARAMETER\n"));
            DEBUGWAIT(INIT);
            Status = EFI_INVALID_PARAMETER;
            break;
        }
    } while (0);
#endif

    return Status;
}


#ifndef INTEL_KDNET
EFI_STATUS
EFIAPI
i40eUndiDriverSupported(
    IN EFI_DRIVER_BINDING_PROTOCOL  *This,
    IN EFI_HANDLE                   Controller,
    IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
)

#else
#undef i40eUndiDriverSupported
EFI_STATUS
i40eUndiDriverSupported(
    VOID
)
{
    PCI_TYPE00 Pci;
    ZeroMem(&Pci, sizeof(Pci));
    GetDevicePciInfo(&Pci);
    return i40eIsVendorDeviceSupported(Pci.Hdr.VendorId, Pci.Hdr.DeviceId);
}


EFI_STATUS
i40eIsVendorDeviceSupported(
    UINT16    VendorId,
    UINT16    DeviceId
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

    do {

#ifndef INTEL_KDNET

        Status = gBS->OpenProtocol(
            Controller,
            &gEfiPciIoProtocolGuid,
            (VOID **)&PciIo,
            This->DriverBindingHandle,
            Controller,
            EFI_OPEN_PROTOCOL_BY_DRIVER
        );
        if (EFI_ERROR(Status)) {
            break;
        }

        Status = PciIo->Pci.Read(
            PciIo,
            EfiPciIoWidthUint8,
            0,
            sizeof(PCI_CONFIG_HEADER),
            &Pci
        );
#else

        Status = EFI_SUCCESS;
        ZeroMem(&Pci, sizeof(Pci));

        Pci.Hdr.VendorId = VendorId;
        Pci.Hdr.DeviceId = DeviceId;

#endif


        if (!EFI_ERROR(Status)) {
            Status = EFI_UNSUPPORTED;
            if (IsDeviceIdSupported(Pci.Hdr.VendorId, Pci.Hdr.DeviceId)) {
                Status = EFI_SUCCESS;

            }
        }

#ifndef INTEL_KDNET
        gBS->CloseProtocol(
            Controller,
            &gEfiPciIoProtocolGuid,
            This->DriverBindingHandle,
            Controller
        );
#endif
    } while (0);

    return Status;

}


/**
  Perform partial initialization required when FW version is not supported and we don't want
  to initialize HW. Still we need to perform some steps to be able to report out health status
  correctly

  @param  This                    Protocol instance pointer
  @param  UndiPrivateData         Pointer to the driver data
  @param  Controller              Handle of device to work with.
**/
#ifndef INTEL_KDNET
EFI_STATUS
I40ePartialInitialization(
    IN EFI_DRIVER_BINDING_PROTOCOL  *This,
    IN  UNDI_PRIVATE_DATA           *UndiPrivateData,
    IN EFI_HANDLE                   Controller
)
{
    EFI_GUID            mHiiFormGuid = I40E_HII_FORM_GUID;
    extern UINT8        InventoryBin[];
    EFI_STATUS          Status;
    EFI_PCI_IO_PROTOCOL *PciIoFncs;

    DEBUGPRINT(INIT, ("I40ePartialInitialization\n"));

    UndiPrivateData->Signature = I40E_UNDI_DEV_SIGNATURE;
    UndiPrivateData->ControllerHandle = Controller;
    UndiPrivateData->NIIPointerProtocol.NIIProtocol_31 = &UndiPrivateData->NIIProtocol_31;
    Status = gBS->InstallMultipleProtocolInterfaces(
        &Controller,
        &gEfiNIIPointerGuid, &UndiPrivateData->NIIPointerProtocol,
        NULL
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("InstallMultipleProtocolInterfaces returns Error = %d %r\n", Status, Status));
        return Status;
    }

    Status = gBS->InstallMultipleProtocolInterfaces(
        &UndiPrivateData->DeviceHandle,
        &gEfiNIIPointerGuid,
        &UndiPrivateData->NIIPointerProtocol,
        NULL
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
        return Status;
    }

    UndiPrivateData->HiiHandle = HiiAddPackages(
        &mHiiFormGuid,
        UndiPrivateData->DeviceHandle,
        I40eUndiDxeStrings,
        NULL
    );
    if (UndiPrivateData->HiiHandle == NULL) {
        DEBUGPRINT(CRITICAL, ("PreparePackageList, out of resource.\n"));
        DEBUGWAIT(CRITICAL);
        return EFI_OUT_OF_RESOURCES;
    }

    //
    // Open For Child Device
    //
    Status = gBS->OpenProtocol(
        Controller,
        &gEfiPciIoProtocolGuid,
        (VOID **)&PciIoFncs,
        This->DriverBindingHandle,
        UndiPrivateData->DeviceHandle,
        EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
    }

    return Status;
}
#endif


EFI_STATUS
#ifndef INTEL_KDNET
EFIAPI
#endif
i40eUndiDriverStart(
#ifndef INTEL_KDNET
    IN EFI_DRIVER_BINDING_PROTOCOL  *This,
    IN EFI_HANDLE                   Controller,
    IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
#else
    VOID
#endif
)
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
    UNDI_PRIVATE_DATA         *I40ePrivate;
    EFI_PCI_IO_PROTOCOL       *PciIoFncs;
#endif
    EFI_STATUS                Status;

    DEBUGPRINT(INIT, ("i40eUndiDriverStart\n"));
    DEBUGWAIT(INIT);

#ifndef INTEL_KDNET


    Status = gBS->AllocatePool(
        EfiBootServicesData,
        sizeof(UNDI_PRIVATE_DATA),
        (VOID **)&I40ePrivate
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("AllocatePool returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
        goto UndiError;
    }

    ZeroMem((UINT8 *)I40ePrivate, sizeof(UNDI_PRIVATE_DATA));

    Status = gBS->OpenProtocol(
        Controller,
        &gEfiPciIoProtocolGuid,
        (VOID **)&I40ePrivate->NicInfo.PciIo,
        This->DriverBindingHandle,
        Controller,
        EFI_OPEN_PROTOCOL_BY_DRIVER
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
        return Status;
    }

    Status = gBS->OpenProtocol(
        Controller,
        &gEfiDevicePathProtocolGuid,
        (VOID **)&UndiDevicePath,
        This->DriverBindingHandle,
        Controller,
        EFI_OPEN_PROTOCOL_BY_DRIVER
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
        return Status;
    }

    I40ePrivate->NIIProtocol_31.Id = (UINT64)(i40e_pxe_31);

#endif
    //
    // the IfNum index for the current interface will be the total number
    // of interfaces initialized so far
    //
    UndiPxeUpdate(&I40ePrivate->NicInfo, i40e_pxe_31);
    //
    // IFcnt should be equal to the total number of physical ports - 1
    //
    DEBUGWAIT(INIT);
#ifndef INTEL_KDNET
    I40ePrivate->NIIProtocol_31.IfNum = i40e_pxe_31->IFcnt;
    Undi32DeviceList[I40ePrivate->NIIProtocol_31.IfNum] = I40ePrivate;
#else
    Undi32DeviceList[i40e_pxe_31->IFcnt] = I40ePrivate;
#endif

    ActiveInterfaces++;
#ifndef INTEL_KDNET
    I40ePrivate->Undi32BaseDevPath = UndiDevicePath;

#endif
    //
    // Initialize the UNDI callback functions to 0 so that the default boot services
    // callback is used instead of the SNP callback.
    //
    I40ePrivate->NicInfo.Delay = (VOID *)0;
    I40ePrivate->NicInfo.Virt2Phys = (VOID *)0;
    I40ePrivate->NicInfo.Block = (VOID *)0;
    I40ePrivate->NicInfo.Map_Mem = (VOID *)0;
    I40ePrivate->NicInfo.UnMap_Mem = (VOID *)0;
    I40ePrivate->NicInfo.Sync_Mem = (VOID *)0;
    I40ePrivate->NicInfo.Unique_ID = (UINT64)&I40ePrivate->NicInfo;
    I40ePrivate->NicInfo.VersionFlag = 0x31;

#ifndef INTEL_KDNET
    I40ePrivate->DeviceHandle = NULL;

#endif
    //
    // Initialize PCI-E Bus and read PCI related information.
    //
    I40ePrivate->NicInfo.hw.numDriverStart++;
    Status = i40ePciInit(&I40ePrivate->NicInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("i40ePciInit fails: %r", Status));
        goto UndiErrorDeleteDevicePath;
    }

    //
    // NVM is not acquired on init. This field is specific for NUL flash operations.
    //
    I40ePrivate->NicInfo.NvmAcquired = FALSE;
#ifndef INTEL_KDNET
    //
    // Alternate MAC address always supported 
    //
    I40ePrivate->AltMacAddrSupported = TRUE;
#endif
    //
    // Do all the stuff that is needed when we initialize hw for the first time
    //
    Status = i40eFirstTimeInit(&I40ePrivate->NicInfo);
#ifndef INTEL_KDNET
    if (Status == EFI_UNSUPPORTED) {
        if (!I40ePrivate->NicInfo.FwVersionSupported) {
            //
            // Current FW version is not supported. Perform only part of initialization needed
            // to report our health status correctly thru the DriverHealthProtocol
            //
            Status = I40ePartialInitialization(
                gUndiDriverBinding,
                I40ePrivate,
                Controller
            );
            if (EFI_ERROR(Status)) {
                DEBUGPRINT(CRITICAL, ("I40ePartialInitialization returned %r\n", Status));
                goto UndiErrorDeleteDevicePath;
            }
            return Status;
        }
    }
#endif
    if (EFI_ERROR(Status) && (Status != EFI_ACCESS_DENIED)) {
        DEBUGPRINT(CRITICAL, ("i40eFirstTimeInit fails: %r", Status));
        goto UndiErrorDeleteDevicePath;
    }

    //
    // Read MAC address. Do it now when Clp processing is done.
    //
    Status = I40eReadMacAddress(&I40ePrivate->NicInfo);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("I40eReadMacAddress failed with %r\n", Status));
        goto UndiErrorDeleteDevicePath;
    }

    //
    // Initialize hardware
    //
    if (I40ePrivate->NicInfo.UNDIEnabled) {
        //
        // Only required when UNDI is being initialized
        //
        Status = I40eInitHw(&I40ePrivate->NicInfo);
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("I40eInitHw failed with %r", Status));
            goto UndiErrorDeleteDevicePath;
        }
    }
#ifndef INTEL_KDNET
    //
    // If needed re-read the MAC address after running CLP.  This will also set the RAR0 address
    // if the alternate MAC address is in effect.
    //
    Status = AppendMac2DevPath(
        &I40ePrivate->Undi32DevPath,
        I40ePrivate->Undi32BaseDevPath,
        &I40ePrivate->NicInfo
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("GigAppendMac2DevPath returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
        goto UndiErrorDeleteDevicePath;
    }

    //
    // Figure out the controllers name for the Component Naming protocol
    // This must be performed after GigAppendMac2DevPath because we need the MAC
    // address of the controller to name it properly
    //
    ComponentNameInitializeControllerName(I40ePrivate);

#endif
    I40ePrivate->Signature = I40E_UNDI_DEV_SIGNATURE;

#ifndef INTEL_KDNET
    I40ePrivate->NIIProtocol_31.Revision = EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_REVISION_31;
    I40ePrivate->NIIProtocol_31.Type = EfiNetworkInterfaceUndi;
    I40ePrivate->NIIProtocol_31.MajorVer = PXE_ROMID_MAJORVER;
    I40ePrivate->NIIProtocol_31.MinorVer = PXE_ROMID_MINORVER_31;
    I40ePrivate->NIIProtocol_31.ImageSize = 0;
    I40ePrivate->NIIProtocol_31.ImageAddr = 0;
    I40ePrivate->NIIProtocol_31.Ipv6Supported = TRUE;

    I40ePrivate->NIIProtocol_31.StringId[0] = 'U';
    I40ePrivate->NIIProtocol_31.StringId[1] = 'N';
    I40ePrivate->NIIProtocol_31.StringId[2] = 'D';
    I40ePrivate->NIIProtocol_31.StringId[3] = 'I';

#endif
    // do not set it to false to avoid HW reinit. Alternative : reset the 
    // AdapterInfo->MemoryPtr
    // I40ePrivate->NicInfo.HwInitialized = FALSE;

#ifndef INTEL_KDNET
    //
    // The EFI_NII_POINTER_PROTOCOL protocol is used only by this driver.  It is done so that
    // we can get the NII protocol from either the parent or the child handle.  This is convenient
    // in the Diagnostic protocol because it allows the test to be run when called from either the
    // parent or child handle which makes it more user friendly.
    //
    I40ePrivate->NIIPointerProtocol.NIIProtocol_31 = &I40ePrivate->NIIProtocol_31;

    Status = gBS->InstallMultipleProtocolInterfaces(
        &Controller,
        &gEfiNIIPointerGuid, &UndiPrivateData->NIIPointerProtocol,
        NULL
    );

    DEBUGPRINT(INIT, ("InstallMultipleProtocolInterfaces returned %r\n", Status));
    DEBUGWAIT(INIT);

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("InstallMultipleProtocolInterfaces returned %r\n", Status));
        DEBUGWAIT(CRITICAL);
        goto UndiErrorDeleteDevicePath;
    }

    I40ePrivate->StartDriver.StartDriver = gUndiDriverStart;
    I40ePrivate->DriverStop.StopDriver = gUndiDriverStop;

    if (I40ePrivate->NicInfo.UNDIEnabled == TRUE) {
        Status = gBS->InstallMultipleProtocolInterfaces(
            &I40ePrivate->DeviceHandle,
            &gEfiDevicePathProtocolGuid,
            I40ePrivate->Undi32DevPath,
            &gEfiNIIPointerGuid,
            &I40ePrivate->NIIPointerProtocol,
            &gEfiNetworkInterfaceIdentifierProtocolGuid_31,
            &I40ePrivate->NIIProtocol_31,
            &gEfiStartStopProtocolGuid,
            &I40ePrivate->DriverStop,
            NULL
        );
    }
    else {
        Status = gBS->InstallMultipleProtocolInterfaces(
            &I40ePrivate->DeviceHandle,
            &gEfiDevicePathProtocolGuid,
            I40ePrivate->Undi32DevPath,
            &gEfiNIIPointerGuid,
            &I40ePrivate->NIIPointerProtocol,
            &gEfiStartStopProtocolGuid,
            &I40ePrivate->DriverStop,
            NULL
        );
    }
    DEBUGPRINT(INIT, ("InstallMultipleProtocolInterfaces returns = %d %r\n", Status, Status));
    DEBUGWAIT(INIT);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("InstallMultipleProtocolInterfaces returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
        goto UndiErrorDeleteDevicePath;
    }


    Status = InitAdapterInformationProtocol(I40ePrivate);
    if (EFI_ERROR(Status) && (Status != EFI_UNSUPPORTED)) {
        DEBUGPRINT(CRITICAL, ("InitAdapterInformationProtocol returned %r\n", Status));
        goto UndiErrorDeleteDevicePath;
    }
    //
    // Open For Child Device
    //
    Status = gBS->OpenProtocol(
        Controller,
        &gEfiPciIoProtocolGuid,
        (VOID **)&PciIoFncs,
        This->DriverBindingHandle,
        UndiPrivateData->DeviceHandle,
        EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("OpenProtocol returned %r\n", Status));
    }

    //
    // Save off the controller handle so we can disconnect the driver later
    //
    I40ePrivate->ControllerHandle = Controller;
    DEBUGPRINT(INIT, ("ControllerHandle = %x, DeviceHandle = %x\n",
        I40ePrivate->ControllerHandle,
        I40ePrivate->DeviceHandle
        ));

    //
    // Initialize HII Protocols
    //
    Status = HiiInit(I40ePrivate);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(INIT, ("HiiInit returns %r\n", Status));
    }

#endif

    DEBUGPRINT(INIT, ("XgbeUndiDriverStart - success\n"));
    return EFI_SUCCESS;

UndiErrorDeleteDevicePath:
#ifndef INTEL_KDNET
    Undi32DeviceList[I40ePrivate->NIIProtocol_31.IfNum] = NULL;
    gBS->FreePool(I40ePrivate->Undi32DevPath);
#else
    Undi32DeviceList[i40e_pxe_31->IFcnt] = NULL;
#endif
    UndiPxeUpdate(NULL, i40e_pxe_31);
    ActiveInterfaces--;

    if (I40ePrivate->NicInfo.UNDIEnabled) {
        I40eReleaseControllerHw(&I40ePrivate->NicInfo);
    }
#ifndef INTEL_KDNET
    UndiError :
        gBS->CloseProtocol(
                Controller,
                &gEfiDevicePathProtocolGuid,
                This->DriverBindingHandle,
                Controller
            );

        gBS->CloseProtocol(
          Controller,
          &gEfiPciIoProtocolGuid,
          This->DriverBindingHandle,
          Controller
        );
        gBS->FreePool((VOID *)I40ePrivate);
        DEBUGPRINT(INIT, ("i40eUndiDriverStart - error %x\n", Status));
#endif

    return Status;
}


EFI_STATUS
#ifndef INTEL_KDNET
EFIAPI
#endif
i40eUndiDriverStop(
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
    UNDI_PRIVATE_DATA                         *UndiPrivateData;
    EFI_NII_POINTER_PROTOCOL                  *NIIPointerProtocol;
#endif
    DEBUGPRINT(INIT, ("DriverStop\n"));
    DEBUGWAIT(INIT);

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
        Status = gBS->CloseProtocol(
            Controller,
            &gEfiDevicePathProtocolGuid,
            This->DriverBindingHandle,
            Controller
        );
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("Close of gEfiDevicePathProtocolGuid failed with %r\n", Status));
            DEBUGWAIT(CRITICAL);
            return Status;
        }

        Status = gBS->CloseProtocol(
            Controller,
            &gEfiPciIoProtocolGuid,
            This->DriverBindingHandle,
            Controller
        );
        if (EFI_ERROR(Status)) {
            DEBUGPRINT(CRITICAL, ("Close of gEfiPciIoProtocolGuid failed with %r\n", Status));
            DEBUGWAIT(CRITICAL);
            return Status;
        }

        return Status;
    }

    if (NumberOfChildren > 1) {
        DEBUGPRINT(INIT, ("Unexpected number of child handles.\n"));
        return EFI_INVALID_PARAMETER;
    }

    //
    // Open an instance for the Network Interface Identifier Protocol so we can check to see
    // if the interface has been shutdown.  Does not need to be closed because we use the
    // GET_PROTOCOL attribute to open it.
    //
    Status = gBS->OpenProtocol(
        ChildHandleBuffer[0],
        &gEfiNIIPointerGuid,
        (VOID **)&NIIPointerProtocol,
        This->DriverBindingHandle,
        Controller,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("OpenProtocol returns %r\n", Status));
        return Status;
    }
    UndiPrivateData = UNDI_PRIVATE_DATA_FROM_THIS(NIIPointerProtocol->NIIProtocol_31);
    DEBUGPRINT(INIT, ("State = %X\n", UndiPrivateData->NicInfo.State));
    DEBUGWAIT(INIT);

    UndiPrivateData->NicInfo.hw.numDriverStop++;

    //
    // Try uninstall HII protocols and remove data from HII database
    //
    Status = HiiUnload(UndiPrivateData);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("HiiUnload returns %r\n", Status));
    }

    //
    // Close the bus driver
    //
    DEBUGPRINT(INIT, ("Removing gEfiPciIoProtocolGuid\n"));
    Status = gBS->CloseProtocol(
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        ChildHandleBuffer[0]
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("Close of gEfiPciIoProtocolGuid failed with %r\n", Status));
        DEBUGWAIT(CRITICAL);
        return Status;
    }
    Status = UninstallAdapterInformationProtocol(UndiPrivateData);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("UninstallAdapterInformationProtocol returned %r\n", Status));
    }

    DEBUGPRINT(INIT, ("Unistall protocols installed on the DeveiceHandle\n"));
    if (UndiPrivateData->NicInfo.FwVersionSupported) {
        if (UndiPrivateData->NicInfo.UNDIEnabled) {
            Status = gBS->UninstallMultipleProtocolInterfaces(
                UndiPrivateData->DeviceHandle,
                &gEfiStartStopProtocolGuid, &UndiPrivateData->DriverStop,
                &gEfiNetworkInterfaceIdentifierProtocolGuid_31, &UndiPrivateData->NIIProtocol_31,
                &gEfiNIIPointerGuid, &UndiPrivateData->NIIPointerProtocol,
                &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                NULL
            );
        }
        else {
            Status = gBS->UninstallMultipleProtocolInterfaces(
                UndiPrivateData->DeviceHandle,
                &gEfiStartStopProtocolGuid, &UndiPrivateData->DriverStop,
                &gEfiNIIPointerGuid, &UndiPrivateData->NIIPointerProtocol,
                &gEfiDevicePathProtocolGuid, UndiPrivateData->Undi32DevPath,
                NULL
            );
        }
    }
    else {
        Status = gBS->UninstallMultipleProtocolInterfaces(
            UndiPrivateData->DeviceHandle,
            &gEfiNIIPointerGuid, &UndiPrivateData->NIIPointerProtocol,
            NULL
        );
    }
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
    }

    DEBUGPRINT(INIT, ("UninstallMultipleProtocolInterfaces: NIIPointerProtocol\n"));
    Status = gBS->UninstallMultipleProtocolInterfaces(
        Controller,
        &gEfiNIIPointerGuid, &UndiPrivateData->NIIPointerProtocol,
        NULL
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("UninstallMultipleProtocolInterfaces returns %r\n", Status));
        //
        // This one should be always installed so there is real issue if we cannot uninstall
        //
        return Status;
    }

    DEBUGPRINT(INIT, ("FreeUnicodeStringTable\n"));
    FreeUnicodeStringTable(UndiPrivateData->ControllerNameTable);

    DEBUGPRINT(INIT, ("Undi32DeviceList\n"));
    Undi32DeviceList[UndiPrivateData->NIIProtocol_31.IfNum] = NULL;
    Status = gBS->FreePool(UndiPrivateData->Undi32DevPath);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("FreePool(UndiPrivateData->Undi32DevPath) returns %r\n", Status));
    }

    //
    // The shtdown should have already been done the the stack
    // In any case do it again. Will not be executed if HwInitialized flag is FALSE.
    //
    if (UndiPrivateData->NicInfo.UNDIEnabled) {
        if (UndiPrivateData->NicInfo.FwVersionSupported) {
            I40eShutdown(&UndiPrivateData->NicInfo);
        }
        I40eReleaseControllerHw(&UndiPrivateData->NicInfo);
    }

    DEBUGPRINT(INIT, ("Shutting down AdminQ\n"));
    //
    // This is the right moment for AQ shutdown as we no longer need it after 
    // shutting down UNDI interface
    //
    i40e_shutdown_adminq(&UndiPrivateData->NicInfo.hw);

    //
    // Restore original PCI attributes
    //  
    Status = UndiPrivateData->NicInfo.PciIo->Attributes(
        UndiPrivateData->NicInfo.PciIo,
        EfiPciIoAttributeOperationSet,
        UndiPrivateData->NicInfo.OriginalPciAttributes,
        NULL
    );
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("PCI IO Attributes returns %r\n", Status));
        DEBUGWAIT(CRITICAL);
        return Status;
    }

    Status = gBS->FreePool(UndiPrivateData);
    if (EFI_ERROR(Status)) {
        DEBUGPRINT(CRITICAL, ("FreePool(GigUndiPrivateData) returns %r\n", Status));
    }
#else
    Status = EFI_SUCCESS;
#endif

    return Status;
}


VOID
UndiPxeUpdate(
    IN I40E_DRIVER_DATA *NicPtr,
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

        PxePtr->Fudge = (UINT8)(PxePtr->Fudge - ChkSum((VOID *)PxePtr, PxePtr->Len));
        DEBUGPRINT(INIT, ("PxeUpdate: ActiveInterfaces = %d\n", ActiveInterfaces));
        DEBUGPRINT(INIT, ("PxeUpdate: PxePtr->IFcnt = %d\n", PxePtr->IFcnt));
        return;
    }

    //
    // number of NICs this undi supports
    //
    PxePtr->IFcnt = ActiveInterfaces;
    PxePtr->Fudge = (UINT8)(PxePtr->Fudge - ChkSum((VOID *)PxePtr, PxePtr->Len));
    DEBUGPRINT(INIT, ("PxeUpdate: ActiveInterfaces = %d\n", ActiveInterfaces));
    DEBUGPRINT(INIT, ("PxeUpdate: PxePtr->IFcnt = %d\n", PxePtr->IFcnt));

    return;
}


EFI_STATUS
InitializePxeStruct(
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
    Status = gBS->AllocatePool(
        EfiBootServicesData,  // EfiRuntimeServicesData,
        (sizeof(PXE_SW_UNDI) + sizeof(PXE_SW_UNDI) + 32),
        &i40e_pxe_memptr
    );

    if (EFI_ERROR(Status)) {
        DEBUGPRINT(INIT, ("%X: AllocatePool returns %r\n", __LINE__, Status));
        return Status;
    }

#else
    i40e_pxe_memptr = &i40e_pxe_31_struct;

#endif


#ifndef INTEL_KDNET
    ZeroMem(
        i40e_pxe_memptr,
        sizeof(PXE_SW_UNDI) + sizeof(PXE_SW_UNDI) + 32
    );
#else
    ZeroMem(
        i40e_pxe_memptr,
        sizeof(PXE_SW_UNDI)
    );
#endif

#ifndef INTEL_KDNET
    //
    // check for paragraph alignment here, assuming that the pointer is
    // already 8 byte aligned.
    //
    if (((UINTN)i40e_pxe_memptr & 0x0F) != 0) {
        i40e_pxe_31 = (PXE_SW_UNDI *)((UINTN)((((UINTN)i40e_pxe_memptr) & (0xFFFFFFFFFFFFFFF0)) + 0x10));
    }
    else {
        i40e_pxe_31 = (PXE_SW_UNDI *)i40e_pxe_memptr;
    }

#else
    //
    // check for paragraph alignment here, assuming that the pointer is
    // already 8 byte aligned.
    //
    if (((UINTN)i40e_pxe_memptr & 0x0F) != 0) {
        Status = EFI_INVALID_PARAMETER;
    }

    i40e_pxe_31 = (PXE_SW_UNDI *)i40e_pxe_memptr;
#endif

    InitUndiPxeStructInit(i40e_pxe_31, 0x31); // 3.1 entry
    return Status;
}


VOID
InitPxeStructInit(
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
    PxePtr->Len = sizeof(PXE_SW_UNDI);
    PxePtr->Fudge = 0;  // cksum
    PxePtr->IFcnt = 0;  // number of NICs this undi supports
    PxePtr->Rev = PXE_ROMID_REV;
    PxePtr->MajorVer = PXE_ROMID_MAJORVER;
    PxePtr->MinorVer = PXE_ROMID_MINORVER_31;
    PxePtr->reserved1 = 0;

    PxePtr->Implementation = PXE_ROMID_IMP_SW_VIRT_ADDR |
        PXE_ROMID_IMP_FRAG_SUPPORTED |
        PXE_ROMID_IMP_CMD_LINK_SUPPORTED |
        PXE_ROMID_IMP_NVDATA_NOT_AVAILABLE |
        PXE_ROMID_IMP_STATION_ADDR_SETTABLE |
        PXE_ROMID_IMP_PROMISCUOUS_MULTICAST_RX_SUPPORTED |
        PXE_ROMID_IMP_PROMISCUOUS_RX_SUPPORTED |
        PXE_ROMID_IMP_BROADCAST_RX_SUPPORTED |
        PXE_ROMID_IMP_FILTERED_MULTICAST_RX_SUPPORTED |
        PXE_ROMID_IMP_SOFTWARE_INT_SUPPORTED |
        PXE_ROMID_IMP_PACKET_RX_INT_SUPPORTED;

#ifndef INTEL_KDNET
    PxePtr->EntryPoint = (UINT64)UndiApiEntry;
#else
    PxePtr->EntryPoint = (UINT64)i40eUndiApiEntry;
#endif
    PxePtr->MinorVer = PXE_ROMID_MINORVER_31;

    PxePtr->reserved2[0] = 0;
    PxePtr->reserved2[1] = 0;
    PxePtr->reserved2[2] = 0;
    PxePtr->BusCnt = 1;
    PxePtr->BusType[0] = PXE_BUSTYPE_PCI;

    PxePtr->Fudge = (UINT8)(PxePtr->Fudge - ChkSum((VOID *)PxePtr, PxePtr->Len));

    //
    // return the !PXE structure
    //
}


#ifndef INTEL_KDNET
EFI_STATUS
AppendMac2DevPath(
    IN OUT EFI_DEVICE_PATH_PROTOCOL **DevPtr,
    IN EFI_DEVICE_PATH_PROTOCOL     *BaseDevPtr,
    IN I40E_DRIVER_DATA             *AdapterInfo
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

    ZeroMem(
        (char *)&MacAddrNode,
        sizeof(MacAddrNode)
    );

    CopyMem(
        (char *)&MacAddrNode.MacAddress,
        (char *)AdapterInfo->hw.mac.perm_addr,
        I40E_ETH_LENGTH_OF_ADDRESS
    );


    DEBUGPRINT(INIT, ("\n"));
    for (i = 0; i < 6; i++) {
        DEBUGPRINT(INIT, ("%2x ", MacAddrNode.MacAddress.Addr[i]));
    }

    DEBUGPRINT(INIT, ("\n"));
    for (i = 0; i < 6; i++) {
        DEBUGPRINT(INIT, ("%2x ", AdapterInfo->hw.mac.perm_addr[i]));
    }

    DEBUGPRINT(INIT, ("\n"));
    DEBUGWAIT(INIT);

    MacAddrNode.Header.Type = MESSAGING_DEVICE_PATH;
    MacAddrNode.Header.SubType = MSG_MAC_ADDR_DP;
    MacAddrNode.Header.Length[0] = sizeof(MacAddrNode);
    MacAddrNode.Header.Length[1] = 0;
    MacAddrNode.IfType = PXE_IFTYPE_ETHERNET;

    //
    // find the size of the base dev path.
    //
    EndNode = BaseDevPtr;
    while (!IsDevicePathEnd(EndNode)) {
        EndNode = NextDevicePathNode(EndNode);
    }

    BasePathLen = (UINT16)((UINTN)(EndNode)-(UINTN)(BaseDevPtr));

    //
    // create space for full dev path
    //
    TotalPathLen = (UINT16)(BasePathLen + sizeof(MacAddrNode) + sizeof(EFI_DEVICE_PATH_PROTOCOL));

    Status = gBS->AllocatePool(
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
    *DevPtr = (EFI_DEVICE_PATH_PROTOCOL *)DevicePtr;
    CopyMem(DevicePtr, (char *)BaseDevPtr, BasePathLen);
    DevicePtr += BasePathLen;
    CopyMem(DevicePtr, (char *)&MacAddrNode, sizeof(MacAddrNode));
    DevicePtr += sizeof(MacAddrNode);
    CopyMem(DevicePtr, (char *)EndNode, sizeof(EFI_DEVICE_PATH_PROTOCOL));

    return EFI_SUCCESS;
}
#endif


UINT8
ChkSum(
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
            Chksum = (UINT8)(Chksum + *Bp++);
        }
    }

    return Chksum;
}


#ifndef INTEL_KDNET
VOID
WaitForEnter(
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

    DEBUGPRINT(INIT, ("\nPress <Enter> to continue...\n"));

    do {
        gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    } while (Key.UnicodeChar != 0xD);
}
#endif


#if 1 //TODO This function is having trouble getting accepted to SC, keep it here for now
/**
 * i40e_get_supported_aq_api_version
 * @hw: pointer to hardware structure
 * @sw_api_major: aq api version major supported by shared code
 * @sw_api_minor: aq api version minor supported by shared code
 *
 * Get the firmware aq api version number which shared
 * code is compliant with.
 **/
enum i40e_status_code i40e_get_supported_aq_api_version(struct i40e_hw *hw,
    u16 *aq_api_major,
    u16 *aq_api_minor)
{
    enum i40e_status_code status = I40E_SUCCESS;
    *aq_api_major = I40E_FW_API_VERSION_MAJOR;

    switch (hw->mac.type) {
    case I40E_MAC_X710:
    case I40E_MAC_XL710:
    case I40E_MAC_GENERIC:
        *aq_api_minor = I40E_FW_API_VERSION_MINOR_X710;
        break;
    default:
        status = I40E_ERR_DEVICE_NOT_SUPPORTED;
        break;
    }

    return status;
}
#endif


#define MAX_FIRMWARE_COMPATIBILITY_STRING 150
BOOLEAN
IsFirmwareCompatible(
    IN   I40E_DRIVER_DATA     *AdapterInfo,
    IN   UINT32               StringBufferSize,
    OUT  CHAR16               *FirmwareCompatibilityString
)
{
    UINT16 ApiMajor;
    UINT16 ApiMinor;

    DEBUGPRINT(HEALTH, ("\n"));

    i40e_get_supported_aq_api_version(&AdapterInfo->hw, &ApiMajor, &ApiMinor);

    if ((AdapterInfo->hw.aq.api_maj_ver > ApiMajor) &&
        (AdapterInfo->hw.aq.api_min_ver == ApiMinor)) {
#ifndef INTEL_KDNET
        StrCpy(FirmwareCompatibilityString,
            L"The UEFI driver for the device stopped because the NVM image is newer than expected. \
        You must install the most recent version of the UEFI driver."
        );
#endif
        return FALSE;
    }
    if ((AdapterInfo->hw.aq.api_min_ver > ApiMinor) &&
        (AdapterInfo->hw.aq.api_maj_ver == ApiMajor)) {
#ifndef INTEL_KDNET
        StrCpy(FirmwareCompatibilityString,
            L"The UEFI driver for the device detected a newer version of the NVM image than expected. \
        Please install the most recent version of the UEFI driver."
        );
#endif
        return FALSE;
    }
    if ((AdapterInfo->hw.aq.api_maj_ver < ApiMajor) ||
        (AdapterInfo->hw.aq.api_min_ver < ApiMinor)) {
#ifndef INTEL_KDNET
        StrCpy(FirmwareCompatibilityString,
            L"The UEFI driver for the device detected an older version of the NVM image than expected. \
        Please update the NVM image."
        );
#endif
        return FALSE;
    }
    FirmwareCompatibilityString = NULL;
    return TRUE;
}


#ifndef INTEL_KDNET
EFI_STATUS
UndiGetControllerHealthStatus(
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
    CHAR16                         FirmwareCompatibilityString[MAX_FIRMWARE_COMPATIBILITY_STRING];
    EFI_DRIVER_HEALTH_HII_MESSAGE  *ErrorMessage;


    BOOLEAN                        FirmwareCompatible;
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
    FirmwareCompatible = TRUE;
    SfpModuleQualified = TRUE;
    ErrorCount = 0;

    if (!UndiPrivateData->NicInfo.QualifiedSfpModule) {
        DEBUGPRINT(HEALTH, ("*DriverHealthStatus = EfiDriverHealthStatusFailed\n"));
        *DriverHealthStatus = EfiDriverHealthStatusFailed;
        SfpModuleQualified = FALSE;
        ErrorCount++;
    }
    if (!IsFirmwareCompatible(&UndiPrivateData->NicInfo, sizeof(FirmwareCompatibilityString), FirmwareCompatibilityString)) {
        DEBUGPRINT(HEALTH, ("*DriverHealthStatus = EfiDriverHealthStatusFailed\n"));
        *DriverHealthStatus = EfiDriverHealthStatusFailed;
        FirmwareCompatible = FALSE;
        ErrorCount++;
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
    Status = gBS->AllocatePool(
        EfiBootServicesData,
        (ErrorCount + 1) * sizeof(EFI_DRIVER_HEALTH_HII_MESSAGE),
        (VOID **)MessageList
    );
    if (EFI_ERROR(Status)) {
        *MessageList = NULL;
        return EFI_OUT_OF_RESOURCES;
    }

    ErrorCount = 0;
    ErrorMessage = *MessageList;

    if (!SfpModuleQualified) {
        Status = HiiSetString(
            UndiPrivateData->HiiHandle,
            STRING_TOKEN(STR_DRIVER_HEALTH_MESSAGE),
            L"The driver failed to load because an unsupported module type was detected.",
            NULL // All languages will be updated
        );
        if (EFI_ERROR(Status)) {
            gBS->FreePool(*MessageList);
            *MessageList = NULL;
            return EFI_OUT_OF_RESOURCES;
        }
        ErrorMessage[ErrorCount].HiiHandle = UndiPrivateData->HiiHandle;
        ErrorMessage[ErrorCount].StringId = STRING_TOKEN(STR_DRIVER_HEALTH_MESSAGE);
        ErrorCount++;
    }

    if (!FirmwareCompatible) {
        Status = HiiSetString(
            UndiPrivateData->HiiHandle,
            STRING_TOKEN(STR_FIRMWARE_HEALTH_MESSAGE),
            FirmwareCompatibilityString,
            NULL // All languages will be updated
        );
        if (EFI_ERROR(Status)) {
            gBS->FreePool(*MessageList);
            *MessageList = NULL;
            return EFI_OUT_OF_RESOURCES;
        }
        ErrorMessage[ErrorCount].HiiHandle = UndiPrivateData->HiiHandle;
        ErrorMessage[ErrorCount].StringId = STRING_TOKEN(STR_FIRMWARE_HEALTH_MESSAGE);
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
UndiGetDriverHealthStatus(
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
        if (Undi32DeviceList[i]->NicInfo.hw.device_id != 0) {
            Status = UndiGetControllerHealthStatus(Undi32DeviceList[i], &HealthStatus, NULL);
            if (EFI_ERROR(Status)) {
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

