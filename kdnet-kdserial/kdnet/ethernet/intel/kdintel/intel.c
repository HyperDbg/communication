/*++

Copyright (c) Microsoft Corporation

Module Name:

    intel.c

Abstract:

    Intel Network Kernel Debug Support.

Author:

    Joe Ballantyne (joeball)

--*/

#include "efibind.h"
#include "efitypes.h"
#include "efipxe.h"
#include "undi.h"

//
// Main entry points into the Intel 1GBit EFI driver code.
//

EFI_STATUS
InitializeGigUNDIDriver (
  VOID
  );

EFI_STATUS
GigUndiDriverSupported (
  VOID
  );

EFI_STATUS
GigUndiDriverStart (
  VOID
  );

VOID
e1000_UNDI_APIEntry (
  IN  UINT64 cdb
  );

//
// Main entry points into the Intel 10Gbit EFI driver code.
//

EFI_STATUS
InitializeXGigUndiDriver (
  VOID
  );

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

VOID
XgbeUndiApiEntry (
  IN  UINT64 cdb
  );

//
// Main entry points into the Intel 40Gbit EFI driver code.
//
EFI_STATUS
i40eInitializeUNDIDriver (
  IN VOID
  );

EFI_STATUS
i40eIsVendorDeviceSupported (
  UINT16    VendorId,
  UINT16    DeviceId
  );

EFI_STATUS
i40eUndiDriverSupported (
  VOID
  );

EFI_STATUS
i40eUndiDriverStart (
  VOID
  );

VOID
i40eUndiApiEntry (
  IN  UINT64 cdb
  );


UINT32
CalcI40eContextSize(
    VOID
    );

ULONG
IntelGetHardwareContextSize (
    __in PDEBUG_DEVICE_DESCRIPTOR Device
    )
{
    ULONG Length;
    EFI_STATUS Status;

    Length = UNDI_DEFAULT_HARDWARE_CONTEXT_SIZE;
    Status = XgbeIsVendorDeviceSupported(Device->VendorID, Device->DeviceID);
    if (Status == STATUS_SUCCESS) {
        Length += 2048*2048;
    }

    Status = i40eIsVendorDeviceSupported(Device->VendorID, Device->DeviceID);
    if (Status == STATUS_SUCCESS) {
        Length += CalcI40eContextSize();
    }

    return UndiGetHardwareContextSize(Length);
}

NTSTATUS
IntelInitializeLibrary (
    __in_opt PCHAR LoaderOptions,
    __inout PDEBUG_DEVICE_DESCRIPTOR Device
    )
{
    UNREFERENCED_PARAMETER(LoaderOptions);

    if (Device == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Device->Memory.Length = IntelGetHardwareContextSize(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
IntelInitializeController(
    __in PKDNET_SHARED_DATA Adapter
    )

/*++

Routine Description:

    This function initializes the Network controller.  The controller is setup
    to send and recieve packets at the fastest rate supported by the hardware
    link.  Packet send and receive will be functional on successful exit of
    this routine.  The controller will be initialized with Interrupts masked
    since all debug devices must operate without interrupt support.

Arguments:

    Adapter - Supplies a pointer to the debug adapter object.

Return Value:

    STATUS_SUCCESS on successful initialization.  Appropriate failure code if
    initialization fails.

--*/

{

#if defined(NO_INTEL_SUPPORT)

    UNREFERENCED_PARAMETER(Adapter);
    return STATUS_NO_SUCH_DEVICE;

#else

    NTSTATUS Status;

    //
    // Select the function pointers only the first time this routine is called.
    //

    if (UndiInitializeDriver != NULL) {
        goto FunctionPointersInitialized;
    }

    //
    // Immediately fail initialization if the device does not have an Intel
    // vendor ID.
    //

    if ((Adapter->Device->BaseClass != PCI_CLASS_NETWORK_CTLR) ||
        (Adapter->Device->VendorID != PCI_VID_INTEL)) {

        Status = STATUS_NO_SUCH_DEVICE;
        KdNetErrorString = L"Not an Intel network controller.";
        goto IntelInitializeControllerEnd;
    }

    //
    // Immediately fail initialization if insufficient memory is available for
    // the device.
    //

    if (Adapter->Device->Memory.Length <
        IntelGetHardwareContextSize(Adapter->Device)) {

        Status = STATUS_INSUFFICIENT_RESOURCES;
        KdNetErrorString = L"Allocated memory passed to KdInitializeController is less than minimum requested in KdInitializeLibrary.";
        goto IntelInitializeControllerEnd;
    }

    //
    // Set the UNDI driver function pointers for the Intel 1GBit UNDI driver.
    //

    UndiInitializeDriver = InitializeGigUNDIDriver;
    UndiHardwareSupported = GigUndiDriverSupported;
    UndiDriverStart = GigUndiDriverStart;
    UndiApiEntry = e1000_UNDI_APIEntry;

    //
    // Initialize the kdnet UNDI driver.
    //

    Status = UndiInitializeController(Adapter);
    if (Status != STATUS_NO_SUCH_DEVICE) {
        goto IntelInitializeControllerEnd;
    }

    //
    // Set the UNDI driver function pointers for the Intel 10GBit UNDI driver.
    //

    UndiInitializeDriver = InitializeXGigUndiDriver;
    UndiHardwareSupported = InitUndiDriverSupported;
    UndiDriverStart = InitUndiDriverStart;
    UndiApiEntry = XgbeUndiApiEntry;

    //
    // Initialize the kdnet UNDI driver.
    //

    Status = UndiInitializeController(Adapter);
    if (Status != STATUS_NO_SUCH_DEVICE) {
        goto IntelInitializeControllerEnd;
    }

    //
    // Set the UNDI driver function pointers for the Intel 40GBit UNDI driver.
    //

    UndiInitializeDriver = i40eInitializeUNDIDriver ;
    UndiHardwareSupported = i40eUndiDriverSupported ;
    UndiDriverStart = i40eUndiDriverStart ;
    UndiApiEntry = i40eUndiApiEntry;

FunctionPointersInitialized:

    //
    // Initialize the kdnet UNDI driver.
    //

    Status = UndiInitializeController(Adapter);

    //
    // On success, erase any KdNetErrorString set during the 1GBit
    // initialization.
    //

    if (NT_SUCCESS(Status)) {
        KdNetErrorString = NULL;
    }

IntelInitializeControllerEnd:
    return Status;

#endif

}
