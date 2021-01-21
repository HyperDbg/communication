/*++
Copyright (c) Microsoft Corporation

Module Name:

    kdnetusbfn.c

Abstract:

    Network Kernel Debugger Extension Base for UsbFn DLL.

--*/

#include "pch.h"
#include <logger.h>
#include <stdlib.h>

//------------------------------------------------------------------------------

#define VIRTETH_OPTION                  "VIRTETH"
#define EEM_OPTION                      "EEM"

#define EVENT_COUNTER_MAX               64              // Max events processed
#define MAX_PACKET_SIZE                 1514            // Max packet size

#define DISCOVERY_DELAY                 500000          // 500 msec
#define DISCOVERY_RESET_DELAY           20000           // 20 msec

#define TX_SEND_RETRY                   100
#define TX_SEND_STALL                   10

#define TRANSFER_TIMEOUT                1000000         // 1 sec
#define TIMEOUT_STALL                   100             // 100 usec

#define SEND_LOOP_TIMEOUT               500000          // 500 msec

//
//  Timeouts used by InitializeController to ensure 'attach' and 'connected' status
//
#define ATTACH_TIMEOUT                   3000000        // 3 seconds
#define CONNECT_TIMEOUT                  3000000  
#define EXTENDED_ATTACH_TIMEOUT          10000000       // 10 seconds
#define EXTENDED_CONNECT_TIMEOUT         10000000
C_ASSERT((ATTACH_TIMEOUT % TIMEOUT_STALL) == 0);
C_ASSERT((EXTENDED_ATTACH_TIMEOUT % TIMEOUT_STALL) == 0);

//------------------------------------------------------------------------------

#define INVALID_INDEX                   (ULONG)-1

#define IN_ENDPT                        2               // IN endpoints
#define OUT_ENDPT                       2               // OUT endpoints

#define BULKOUT_ENDPT                   1
#define BULKIN_ENDPT                    1

#define MAX_TRANSFERS                   28
#define MIN_TRANSFERS                   28

#define REF_PER_TRANSFER                16

#define CONTROL_TRANSFERS               1
#define BULKOUT_TRANSFERS               14
#define BULKIN_TRANSFERS                13

#define RX_ENDPT                        OUT_ENDPT
#define TX_ENDPT                        IN_ENDPT

#define RX_BUFFER_HANDLE                MAX_TRANSFERS

//------------------------------------------------------------------------------

#define EEM_HEADER_CMD_FLAG             (1 << 15)
#define EEM_HEADER_CRC_FLAG             (1 << 14)
#define EEM_HEADER_CMD_MASK             (7 << 11)
#define EEM_HEADER_LENGTH_MASK          0x07FF

#define EEM_HEADER_CMD_ECHO             (0 << 11)
#define EEM_HEADER_CMD_ECHO_REPLY       (1 << 11)
#define EEM_HEADER_CMD_SUSPEND_HINT     (2 << 11)
#define EEM_HEADER_CMD_RESPONSE_HINT    (3 << 11)
#define EEM_HEADER_CMD_COMPLETE_HINT    (4 << 11)
#define EEM_HEADER_CMD_TICKLE           (5 << 11)

#define EEM_TX_QUEUE_LIMIT              1

#define EEM_HEADER_LEN                  sizeof(UINT16)
#define EEM_CRC_LEN                     sizeof(UINT32)

#define EEM_MAX_PACKET_SIZE             (EEM_HEADER_LEN +               \
                                            MAX_PACKET_SIZE +           \
                                            EEM_CRC_LEN)

#define PCI_VID_INTEL                   0x8086

//
//  Helpers for getting the SS & HS descriptor pointers.
//

#define IS_SS_BUS(_BusSpeed) ((_BusSpeed == UsbBusSpeedSuper) ? (TRUE): (FALSE))


#define GET_PTR_TO_EP_IN(_BusSpeed, _UsbConfigDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                        ((PUSB_ENDPOINT_DESCRIPTOR)&_UsbConfigDesc[0x1F]) : \
                        ((PUSB_ENDPOINT_DESCRIPTOR)&_UsbConfigDesc[0x19]))

#define GET_PTR_TO_EP_COMPANION_IN(_BusSpeed, _UsbConfigDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                                  ((PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)&_UsbConfigDesc[0x26]) : \
                                  (NULL))

#define GET_PTR_TO_EP_OUT(_BusSpeed, _UsbConfigDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                         ((PUSB_ENDPOINT_DESCRIPTOR)&_UsbConfigDesc[0x12]) : \
                         ((PUSB_ENDPOINT_DESCRIPTOR)&_UsbConfigDesc[0x12]))

#define GET_PTR_TO_EP_COMPANION_OUT(_BusSpeed, _UsbConfigDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                                   ((PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)&_UsbConfigDesc[0x19]) : \
                                   (NULL))

#define GET_PTR_TO_INTERFACE(_BusSpeed, _UsbConfigDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                            ((PUSB_INTERFACE_DESCRIPTOR)&_UsbConfigDesc[0x09]) : \
                            ((PUSB_INTERFACE_DESCRIPTOR)&_UsbConfigDesc[0x09]))

#define GET_PTR_TO_CONFIGURATION(_BusSpeed, _UsbConfigDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                            ((PUSB_CONFIGURATION_DESCRIPTOR)&_UsbConfigDesc[0x00]) : \
                            ((PUSB_CONFIGURATION_DESCRIPTOR)&_UsbConfigDesc[0x00]))

#define GET_PTR_TO_DEVICE(_BusSpeed, _UsbDeviceDesc) ((IS_SS_BUS(_BusSpeed)) ? \
                         ((PUSB_DEVICE_DESCRIPTOR)&_UsbDeviceDesc[0x00]) : \
                         ((PUSB_DEVICE_DESCRIPTOR)&_UsbDeviceDesc[0x00]))

//
//  Maximum transfer to hold when ASYNC flag and SS are enabled
//

#define EEM_TX_SS_ASYNC_QUEUE_LIMIT     2

//
//  Set the Bursting transfer mode (from device to host) only for SS link & EEM
//

#define SET_IF_BURST_TRANSFER_MODE(AsyncHandle, BusSpeed, EemMode) ((AsyncHandle != FALSE) && (IS_SS_BUS(BusSpeed) != FALSE) && (EemMode != FALSE))

//
//  Set the asynchronous mode 
//

#define SET_ASYNC_FLAG(Handle) (((Handle & TRANSMIT_ASYNC) != 0))

//
//  Check if the current transfer can be added to the transfer DMA buffer
//  This macro is used only for SS in burst mode when the device can send sequence of transfers without USB ack.
//

#define IS_ALLOWED_NEW_PACKET_IN_TRANSFER(RefPos, RefCount, TransferFree, QueuedTx)  \
                                         ((RefPos < REF_PER_TRANSFER) && \
                                         (RefCount > 1) && (TransferFree > EEM_MAX_PACKET_SIZE) && \
                                         (QueuedTx < EEM_TX_SS_ASYNC_QUEUE_LIMIT))

//
//  Check if there is a room for another transfer when burst mode is enabled
//

#define CHECK_TRANSFER_SPACE_FOR_TX(RefCount, TransferFree, RefPos)  \
                                   ((RefCount > 1) || (TransferFree <= EEM_MAX_PACKET_SIZE) || \
                                   ((RefPos) >= REF_PER_TRANSFER))

//------------------------------------------------------------------------------

#pragma warning(disable : 4200)
typedef struct {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    WCHAR   pwcbString[];
} USB_STRING, *PUSB_STRING;
#pragma warning(default : 4200)

typedef struct {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    WCHAR   pwcbString[12];
} USB_STRING_MAC;

#pragma pack(push, 1)
typedef struct {
    UINT32 dwLength;
    UINT16 bcdVersion;
    UINT16 wIndex;
    UINT8  bCount;
    UINT8  bReserved1[7];
    UINT8  bInterfaceNumber;
    UINT8  bReserved2;
    UINT8  bCompatibleID[8];
    UINT8  bSubCompatibleID[8];
    UINT8  bReserved[6];
} USB_COMP_ID_WINUSB;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    UINT32 dwLength;
    UINT16 bcdVersion;
    UINT16 wIndex;
    UINT16 wCount;
    UINT32 dwSize;
    UINT32 dwPropertyDataType;
    UINT16 wPropertyNameLength;
    WCHAR  wPropertyName[20];
    UINT32 dwPropertyDataLength;
    WCHAR  wProperyData[39];
} USB_EXT_PROPERTY_WINUSB;
#pragma pack(pop)

//
//  This structure indicates the parameters used to configure the USB working modes.
//

typedef struct {
    BOOLEAN EemMode;
    BOOLEAN NoDhcp;
    BOOLEAN DiscoveryEnabled;
    ULONG DiscoveryDelay;
    struct {
        int IsZeroLengthPacketConfig : 1;  // Set if zero-length packet is required to be sent
        int Reserved : 31;
    } Config;
} USBFN_MODE, *PUSBFN_MODE;

//------------------------------------------------------------------------------

static
UINT8
UsbVeDeviceDesc[] = {
    18,                                 // 00 - bLength
    USB_DEVICE_DESCRIPTOR_TYPE,         // 01 - bDescriptorType = DEVICE
    0x00, 0x02,                         // 02 - bcdUSB
    0x00,                               // 04 - bDeviceClass
    0x00,                               // 05 - bDeviceSubClass
    0x00,                               // 06 - bDeviceProtocol
    0x40,                               // 07 - bMaxPacketSize0
    0x5E, 0x04,                         // 08 - idVendor
    0x44, 0x06,                         // 0A - idProduct
    0x00, 0x01,                         // 0C - bcdDevice
    0x01,                               // 0E - iManufacturer
    0x02,                               // 0F - iProduct
    0x03,                               // 10 - iSerialNumber
    0x01                                // 11 - bNumConfigs
};

static
UINT8
UsbVeConfigDesc[] = {
    9,                                  // 00 - bLength
    USB_CONFIGURATION_DESCRIPTOR_TYPE,  // 01 - bDescriptorType
    32, 0,                              // 02 - wTotalLength
    1,                                  // 04 - bNumInterfaces
    1,                                  // 05 - bConfigurationValue
    0,                                  // 06 - iConfiguration
    0xC0,                               // 07 - bmAttributes
    0x32,                               // 08 - MaxPower (100mA)

    // Interface descriptor
    9,                                  // 09 - bLength
    USB_INTERFACE_DESCRIPTOR_TYPE,      // 0A - bDescriptorType
    0,                                  // 0B - bInterfaceNumber
    0,                                  // 0C - bAlternateSetting
    2,                                  // 0D - bNumEndpoints
    0xFF,                               // 0E - bInterfaceClass
    0xFF,                               // 0F - bInterfaceSubClass
    0xFF,                               // 10 - bInterfaceProtocol
    0,                                  // 11 - ilInterface

    // EP1 Bulk Out
    7,                                  // 12 - bLength
    USB_ENDPOINT_DESCRIPTOR_TYPE,       // 13 - bDescriptorType
    0x01,                               // 14 - bEndpointAddress (EP 1, OUT)
    2,                                  // 15 - bmAttributes (0010 = Bulk)
    0x00, 0x02,                         // 16 - wMaxPacketSize
    0,                                  // 18 - bInterval (ignored for bulk)

    // EP2 Bulk In
    7,                                  // 19 - bLength
    USB_ENDPOINT_DESCRIPTOR_TYPE,       // 1A - bDescriptorType
    0x81,                               // 1B - bEndpointAddress
    2,                                  // 1C - bmAttributes (0010 = Bulk)
    0x00, 0x02,                         // 1D - wMaxPacketSize
    0,                                  // 1F - bInterval (ignored for bulk)
};

static
UINT8
UsbEemDeviceDesc[] = {
    18,                                 // 00 - bLength
    USB_DEVICE_DESCRIPTOR_TYPE,         // 01 - bDescriptorType = DEVICE
    0x00, 0x02,                         // 02 - bcdUSB
    0x00,                               // 04 - bDeviceClass
    0x00,                               // 05 - bDeviceSubClass
    0x00,                               // 06 - bDeviceProtocol
    0x40,                               // 07 - bMaxPacketSize0
    0x5E, 0x04,                         // 08 - idVendor
    0x4D, 0x06,                         // 0A - idProduct
    0x00, 0x01,                         // 0C - bcdDevice
    0x01,                               // 0E - iManufacturer
    0x02,                               // 0F - iProduct
    0x03,                               // 10 - iSerialNumber
    0x01                                // 11 - bNumConfigs
};

static
UINT8
UsbEemConfigDesc[] = {
    9,                                  // 00 - bLength
    USB_CONFIGURATION_DESCRIPTOR_TYPE,  // 01 - bDescriptorType
    32, 0,                              // 02 - wTotalLength
    1,                                  // 04 - bNumInterfaces
    1,                                  // 05 - bConfigurationValue
    0,                                  // 06 - iConfiguration
    0xC0,                               // 07 - bmAttributes
    0x32,                               // 08 - MaxPower (100mA)

    // Interface descriptor
    9,                                  // 09 - bLength
    USB_INTERFACE_DESCRIPTOR_TYPE,      // 0A - bDescriptorType
    0,                                  // 0B - bInterfaceNumber
    0,                                  // 0C - bAlternateSetting
    2,                                  // 0D - bNumEndpoints
    0x02,                               // 0E - bInterfaceClass
    0x0C,                               // 0F - bInterfaceSubClass
    0x07,                               // 10 - bInterfaceProtocol
    0,                                  // 11 - ilInterface

    // EP1 Bulk Out
    7,                                  // 12 - bLength
    USB_ENDPOINT_DESCRIPTOR_TYPE,       // 13 - bDescriptorType
    0x01,                               // 14 - bEndpointAddress (EP 1, OUT)
    2,                                  // 15 - bmAttributes (0010 = Bulk)
    0x00, 0x02,                         // 16 - wMaxPacketSize
    0,                                  // 18 - bInterval (ignored for bulk)

    // EP2 Bulk In
    7,                                  // 19 - bLength
    USB_ENDPOINT_DESCRIPTOR_TYPE,       // 1A - bDescriptorType
    0x81,                               // 1B - bEndpointAddress
    2,                                  // 1C - bmAttributes (0010 = Bulk)
    0x00, 0x02,                         // 1D - wMaxPacketSize
    0,                                  // 1F - bInterval (ignored for bulk)
};

//--------------------------------------------------------------------------------
//
//  USB 3.0 (Super Speed) Descriptors
//
//--------------------------------------------------------------------------------

//
// Standard Device SuperSpeed Descriptor (One Device)
//

static UINT8 UsbSuperSpeedEemDeviceDesc[] = {
    18,                                 // 00 - bLength
    USB_DEVICE_DESCRIPTOR_TYPE,         // 01 - bDescriptorType = DEVICE
    0x00, 0x03,                         // 02 - bcdUSB 3.00
    0x00,                               // 04 - bDeviceClass
    0x00,                               // 05 - bDeviceSubClass
    0x00,                               // 06 - bDeviceProtocol
    0x40,                               // 07 - bMaxPacketSize0
    0x5E, 0x04,                         // 08 - idVendor
    0x5F, 0x06,                         // 0A - idProduct
    0x00, 0x01,                         // 0C - bcdDevice
    0x01,                               // 0E - iManufacturer
    0x02,                               // 0F - iProduct
    0x03,                               // 10 - iSerialNumber
    0x01                                // 11 - bNumConfigs
};

//
// Standard Configuration SuperSpeed Descriptor (One Configuration)
// USB 3.0 doc (9.6 Standard USB Descriptor Definitions)
//

static UINT8 UsbSuperSpeedEemConfigDesc[] = {
    9,                                  // 00 - bLength
    USB_CONFIGURATION_DESCRIPTOR_TYPE,  // 01 - bDescriptorType
    44, 0,                              // 02 - wTotalLength
    1,                                  // 04 - bNumInterfaces
    1,                                  // 05 - bConfigurationValue
    0,                                  // 06 - iConfiguration
    0xC0,                               // 07 - bmAttributes
    0x64,                               // 08 - MaxPower (100mA, counted in 8mA units, so max <=900mA))

    // Interface descriptor
    9,                                  // 09 - bLength
    USB_INTERFACE_DESCRIPTOR_TYPE,      // 0A - bDescriptorType
    0,                                  // 0B - bInterfaceNumber
    0,                                  // 0C - bAlternateSetting
    2,                                  // 0D - bNumEndpoints
    0x02,                               // 0E - bInterfaceClass
    0x0C,                               // 0F - bInterfaceSubClass
    0x07,                               // 10 - bInterfaceProtocol
    0,                                  // 11 - ilInterface

    // Standard Endpoint Descriptor (EP1 - BULK OUT)
    7,                                  // 12 - bLength
    USB_ENDPOINT_DESCRIPTOR_TYPE,       // 13 - bDescriptorType
    0x01,                               // 14 - bEndpointAddress (EP 1, OUT)
    2,                                  // 15 - bmAttributes (0010 = Bulk)
    0x00, 0x04,                         // 16 - wMaxPacketSize
    0,                                  // 18 - bInterval (ignored for bulk)

    // Standard Endpoint Companion Descriptor (EP1 - BULK OUT)
    6,                                  // 19 - bLength
    USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE, // 1A - bDescriptorType (SUPERSPEED_USB_ENDPOINT_COMPANION)
    0x0f,                               // 1B - bMaxBurst 
    0x00,                               // 1C - bmAttributes (No streams)
    0x00, 0x00,                         // 1D - wBytesPerInterval (0 for control and bulk endpoints)

    // Standard Endpoint Descriptor (EP2 - BULK IN)
    7,                                  // 1F - bLength
    USB_ENDPOINT_DESCRIPTOR_TYPE,       // 20 - bDescriptorType
    0x81,                               // 21 - bEndpointAddress (EP 2, IN)
    2,                                  // 22 - bmAttributes (0010 = Bulk)
    0x00, 0x04,                         // 23 - wMaxPacketSize
    0,                                  // 25 - bInterval (ignored for bulk)

    // Standard Endpoint Companion Descriptor (EP2 - BULK IN)
    0x06,                               // 26 - bLength
    USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE, // 27 - bDescriptorType (SUPERSPEED_USB_ENDPOINT_COMPANION)
    0x04,                               // 28 - bMaxBurst (0x0 maximum allowed burst packets for now)
    0x00,                               // 29 - bmAttributes 0 (No streams)
    0x00, 0x00,                         // 2A - wBytesPerInterval (0 for control and bulk endpoints)
};

//
// Binaries Device Object Store Descriptor (USB 3.0 doc. 9.6.2.2 SuperSpeed USB Device Capability )
// This BOS descritor includes USB HS and SS extension capability descriptors
// Device Capability descriptors are always returned as part of the BOS information 
// returned by a GetDescriptor(BOS) request.  
// A Device Capability cannot be directly accessed with a GetDescriptor() or SetDescriptor() request. 
// A SuperSpeed device shall include the USB 2.0 Extension descriptor and shall 
// support LPM when operating in USB 2.0 High-Speed mode.
//  

const static UCHAR UsbDeviceBOSDescriptor[] =
{
    0x05,                                   // 00 - Descriptor Size
    USB_BOS_DESCRIPTOR_TYPE,                // 01 - Device Descriptor Type
    0x16, 0x00,                             // 02 - Length of this descriptor and all sub descriptors
    0x02,                                   // 03 - Number of device capability descriptors
    0x07,                                   // 04 - Descriptor Size
    USB_DEVICE_CAPABILITY_DESCRIPTOR_TYPE,  // 05 - Device Capability Type descriptor
    0x02,                                   // 06 - USB 2.0 Extension Capability Type (Capability type: USB 2.0 EXTENSION = 2)
    0x02, 0x00, 0x00, 0x00,                 // 07 - Supported device level features  (Bitmap encoding of supported device level features)
    0x0A,                                   // 08 - Descriptor Size
    USB_DEVICE_CAPABILITY_DESCRIPTOR_TYPE,  // 09 - Device Capability Type descriptor
    0x03,                                   // 0A - SuperSpeed Device Capability Type (Capability type: SUPERSPEED_USB=3)
    0x00,                                   // 0B - Supported device level features  (Bitmap encoding of supported device level features)
    0x0C, 0x00,                             // 0C - Speeds Supported by the device : SS, HS (Bitmap encoding of the speed supported by this device)
    0x02,                                   // 0D - Functionality support (The lowest speed at which all the functionality supported by the device is available to the user.)
    0x0A,                                   // 0E - U1 Device Exit Latency Less than 10 microseconds
    0xFF, 0x07                              // 0F - U2 Device Exit Latency Less than 2047 microseconds
};

//------------------------------------------------------------------------------

static
const
USB_STRING_DESCRIPTOR
SupportedLang = {
    0x04,                               // 00 - bLength
    USB_STRING_DESCRIPTOR_TYPE,         // 01 - bDescriptorType
    0x0409                              // 02 - US English
};

#define MANUFACTURER            L"Microsoft"

static
const
USB_STRING
Manufacturer = {
    sizeof(MANUFACTURER),               // 00 - bLength
    USB_STRING_DESCRIPTOR_TYPE,         // 01 - bDescriptorType
    MANUFACTURER
};

#define PRODUCT_VE              L"KdNet VirtEth"

static
USB_STRING
ProductVe = {
    sizeof(PRODUCT_VE),                 // 00 - bLength
    USB_STRING_DESCRIPTOR_TYPE,         // 01 - bDescriptorType
    PRODUCT_VE
};

#define PRODUCT_EEM             L"KdNet EEM"

static
USB_STRING
ProductEem = {
    sizeof(PRODUCT_EEM),                // 00 - bLength
    USB_STRING_DESCRIPTOR_TYPE,         // 01 - bDescriptorType
    PRODUCT_EEM
};

#define OSDESCRIPTOR            L"MSFT100!"

static
const
USB_STRING
OsDescriptor = {
    sizeof(OSDESCRIPTOR),               // 00 - bLength
    USB_STRING_DESCRIPTOR_TYPE,         // 01 - bDescriptor
    OSDESCRIPTOR
};

//------------------------------------------------------------------------------

#define MS_EXT_CONFIG_DESC_INDEX    4
#define MS_EXT_PROP_DESC_INDEX      5

#define MS_EXT_PROP_DESC_NAME       L"DeviceInterfaceGUID"
#define MS_EXT_PROP_DESC_DATA       L"{627fab51-6def-46cc-9331-c430ba8d9905}"

//------------------------------------------------------------------------------

static
USB_COMP_ID_WINUSB
UsbCompIdWinUsb = {
    sizeof(USB_COMP_ID_WINUSB),             // 00 - dwLength
    0x0100,                                 // 04 - bcdVersion
    MS_EXT_CONFIG_DESC_INDEX,               // 06 - wIndex
    0x01,                                   // 07 - bCount
    {
        0x00, 0x00, 0x00,                   // 09 - Reserved
        0x00, 0x00, 0x00, 0x00
    },
    0x00,                                   // 16 - bFirstInterfaceNumber
    0x01,                                   // 17 - Reserved
    "WINUSB\0",                             // 18 - compatibleId
    {
        0x00, 0x00, 0x00, 0x00,             // 26 - subCompatibleId
        0x00, 0x00, 0x00, 0x00
    }, {
        0x00, 0x00, 0x00, 0x00,             // 34 - Reserved
        0x00, 0x00
    }
};

static
USB_EXT_PROPERTY_WINUSB
UsbPropertyWinUsb = {
    sizeof(USB_EXT_PROPERTY_WINUSB),        // 00 - dwLength
    0x0100,                                 // 04 - bcdVersion
    MS_EXT_PROP_DESC_INDEX,                 // 06 - wIndex
    1,                                      // 08 - wCount
    sizeof(USB_EXT_PROPERTY_WINUSB) - 10,   // 0A - dwSize
    1,                                      // 0E - dwPropertyDataType
    sizeof(MS_EXT_PROP_DESC_NAME),
    MS_EXT_PROP_DESC_NAME,
    sizeof(MS_EXT_PROP_DESC_DATA),
    MS_EXT_PROP_DESC_DATA
};

//------------------------------------------------------------------------------

static
PUINT8
UsbDeviceDesc;

static
PUINT8
UsbConfigDesc;

static
PUSB_STRING
Product;

//------------------------------------------------------------------------------

//
// This structure indicates an instance that represents a specific entry in the 
// DMA miniport buffer (maximum 28 transfer)
//

typedef struct USBFNDBG_TRANSFER {
    
    //
    //  The Buffer indicates a pointer to an entry of the Miniport DMA buffer.
    //

    PUCHAR Buffer;

    //
    //  The length of the current buffer pointed by Buffer field, which would be the length
    //  of one DMA buffer entry (16Kb).
    //

    ULONG Length;
    ULONG Next;
    
    //
    //  This indicates if the current transfer has been transmitted by the miniport layer.
    //  This flag is cleared in the event handler function when there is
    //  an event notification of transmitted/processed transfer.
    //

    BOOLEAN Active;

    //
    // A reference entry represents a specifix Tx/Rx mapped packet inside of the current transfer DMA buffer.
    // there can be up to 16 number different packet references by the current transfer. 
    //

    struct {
        PUCHAR Packet;
        ULONG Length;
    } Ref[REF_PER_TRANSFER];

    //
    // This indicates the number of references within current transfer instance.
    // this can be pointing to a TX/RX packet withing of the current transfer buffer.    
    //

    ULONG RefCount;

    //
    // RefPos indicates the index that points to a specific packet reference in the current Transfer.
    //

    ULONG RefPos;

} USBFNDBG_TRANSFER, *PUSBFNDBG_TRANSFER;

typedef struct USBFNDBG_ENDPOINT {
    ULONG FirstDone;
    ULONG LastDone;
    ULONG FirstActive;
    ULONG LastActive;
} USBFNDBG_ENDPOINT, *PUSBFNDBG_ENDPOINT;

typedef struct _USBFNDBG_CONTEXT {
    PUINT8 DmaBuffer;
    ULONG  DmaLength;
    PVOID  Miniport;
    ULONG  MiniportLength;

    ULONG  MaxTransferSize;
    ULONG  Transfers;

    BOOLEAN Initialized;
    BOOLEAN Running;
    BOOLEAN Attached;
    BOOLEAN Configured;
    BOOLEAN Connected;

    BOOLEAN EemMode;

    BOOLEAN DiscoveryEnabled;
    BOOLEAN DiscoveryInReset;
    ULONG DiscoveryDelay;
    ULONG DiscoveryTimeBase;

    UCHAR Mac[6];
    ULONG LinkSpeed;
    ULONG FullDuplex;

    USBFNMP_BUS_SPEED BusSpeed;
    UINT8  DeviceConfig;
    UINT16 DeviceStatus;

    struct {
        UINT32 Rate;
        UINT8 StopBits;
        UINT8 Parity;
        UINT8 DataBits;
    } LineCoding;

    BOOLEAN PendingUdr;
    USB_DEFAULT_PIPE_SETUP_PACKET PendingUdrPacket;

    ULONG Ep0Transfer;
    USBFNDBG_TRANSFER Transfer[MAX_TRANSFERS];
    USBFNDBG_ENDPOINT Rx[OUT_ENDPT];
    USBFNDBG_ENDPOINT Tx[IN_ENDPT];

    BOOLEAN TxPacketFailed;
    ULONG64 TxTimeStamp;
    ULONG64 Frequency;

    ULONG QueuedTx;
    ULONG EemTx;

    ULONG EemRx;
    ULONG EemRxPos;

    BOOLEAN RxBufferBusy;
    ULONG RxBufferLength;
    UCHAR RxBuffer[EEM_MAX_PACKET_SIZE];
    PKDNET_SHARED_DATA KdNetData;
    USBFN_MODE InitialMode;
    BOOLEAN IsRestartDone;
    ULONG AttachTimeout;    
    ULONG ConnectTimeout;
    USB_DEVICE_SPEED DeviceSpeed;
    BOOLEAN UsbLinkDown;
    BOOLEAN UsbLinkUp;
    BOOLEAN IsBurstTransferMode;
    BOOLEAN NoDhcp;
} USBFNDBG_CONTEXT, *PUSBFNDBG_CONTEXT;

//------------------------------------------------------------------------------

static
USBFNDBG_CONTEXT
UsbFnDbgContext;

ULONG UsbFnHibernateDectected;

//------------------------------------------------------------------------------
//
//  Table of 256 full pre-computed constants is used to implement a table lookup 
//  method for CRC-32 calculation. 
//
static
ULONG32 const
crcTable[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

//------------------------------------------------------------------------------

NTSTATUS
EventHandler(
    _In_ PUSBFNDBG_CONTEXT Context,
    BOOLEAN InConfigTransaction,
    _Out_opt_ PBOOLEAN Idle);

//------------------------------------------------------------------------------

//
//  Table lookup CRC algorithm
//

static
ULONG32
ComputeCrc32(
    ULONG32 PartialCrc,
    _In_bytecount_(DataSize) PUCHAR Data,
    ULONG DataSize)
{
    ULONG32 crc = ~PartialCrc;
    ULONG i;

    for (i = 0; i < DataSize; i++) {
        crc = crcTable[Data[i] ^ (crc & 0xff)] ^ (crc >> 8);
    }
    return ~crc;
}

//------------------------------------------------------------------------------

VOID GetConfigDescriptorByBusSpeed(
    _In_ USBFNMP_BUS_SPEED BusSpeed,
    _In_ BOOLEAN EemMode,
    _Out_ PUINT8 *UsbDeviceDesc,
    _Out_ PUINT8 *UsbConfigDesc
    )
{
    switch (BusSpeed) {
    case UsbBusSpeedUnknown:
        LOG_TRACE("Unknown Device Speed HS");
        break;

    case UsbBusSpeedLow:
    case UsbBusSpeedFull:
    case UsbBusSpeedHigh:
        LOG_TRACE("Device Speed HS");
        if (EemMode != FALSE) {
            *UsbDeviceDesc = (PUINT8)UsbEemDeviceDesc;
            *UsbConfigDesc = (PUINT8)UsbEemConfigDesc;
        } else {
            *UsbDeviceDesc = (PUINT8)UsbVeDeviceDesc;
            *UsbConfigDesc = (PUINT8)UsbVeConfigDesc;
        }
        break;

    case UsbBusSpeedSuper:
        LOG_TRACE("Device Speed SS");
        *UsbDeviceDesc = (PUINT8)UsbSuperSpeedEemDeviceDesc;
        *UsbConfigDesc = (PUINT8)UsbSuperSpeedEemConfigDesc;
        break;

    default:

        NT_ASSERT(FALSE);

        break;
    }
}

//------------------------------------------------------------------------------

USB_DEVICE_SPEED GetDeviceSpeedByBusSpeed(
    _In_ USBFNMP_BUS_SPEED BusSpeed
    )
{
    USB_DEVICE_SPEED DeviceSpeed;

    switch (BusSpeed) {
        case UsbBusSpeedUnknown:
        case UsbBusSpeedLow:
            DeviceSpeed = UsbLowSpeed;
            break;

        case UsbBusSpeedFull:
            DeviceSpeed = UsbFullSpeed;
            break;

        case UsbBusSpeedHigh:
            DeviceSpeed = UsbHighSpeed;
            break;

        case UsbBusSpeedSuper:
            DeviceSpeed = UsbSuperSpeed;
            break;

        default:
            DeviceSpeed = UsbLowSpeed;
            break;
    }

    return DeviceSpeed;
}

//------------------------------------------------------------------------------

VOID SetAttachConnectTimeouts(_In_ PDEBUG_DEVICE_DESCRIPTOR Device)
{
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;

    //
    //  Check PCI device type
    //
    
    if ((Device->NameSpace == KdNameSpacePCI) &&
        (Device->BaseClass == PCI_CLASS_SERIAL_BUS_CTLR) &&
        (Device->VendorID == PCI_VID_INTEL)) {

        Context->AttachTimeout = EXTENDED_ATTACH_TIMEOUT;    
        Context->ConnectTimeout = EXTENDED_CONNECT_TIMEOUT;

    } else {        
        Context->AttachTimeout = ATTACH_TIMEOUT;    
        Context->ConnectTimeout = CONNECT_TIMEOUT;
    }
}

//------------------------------------------------------------------------------

VOID CheckRestartUsbController(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ BOOLEAN InitialConnectStatus)
{

    //
    //  The statement (InitialConnectStatus == FALSE) && (Context->Connected != FALSE)
    //  will be asserted only for Chipidea USB controller. The function SetupPacket() 
    //  will be invoked as a result of the USB Reset event initiated by the host, and 
    //  followed by host requesting to start again the USB setup phase.
    //  If the setup phase succeed, then the Context->Connected will be TRUE. 
    //  The statement (Context->UsbLinkDown != FALSE) && (Context->UsbLinkUp != FALSE)
    //  will be asserted only for Synopsys controller that does not report the attach-deAttach
    //  USB events, so the statement just tracks the USB link status change from down to Up.
    //

    if (((InitialConnectStatus == FALSE) && (Context->Connected != FALSE)) ||
        ((Context->UsbLinkDown != FALSE) && (Context->UsbLinkUp != FALSE))) {
        Context->KdNetData->RestartKdnet = TRUE;
        Context->IsRestartDone = TRUE;
        Context->UsbLinkDown = FALSE;
        Context->UsbLinkUp = FALSE;
        LOG_TRACE("Restart KdnetData:%d", Context->KdNetData->RestartKdnet);
        LOG_TRACE("LinkDown:%d LinkUp:%d", Context->UsbLinkDown, Context->UsbLinkUp);
    }
}

//------------------------------------------------------------------------------
/*++

Routine Description:

    This function determines if the Zero-length packet needs to be transmitted.
    The current functionality will be enabled only for specific bcdedit.exe LoadOption
    as Aries is the only host that requires zero-length packet to indicate EOT.
    Basically, the device will indicate a transfer completion to the host by
    sending a zero-length packet when the total transfer packet length is a multiple
    of maximum packet size that BULK IN pipe has been configured.

Arguments: 
    Context      Supplies a pointer to the current transport block.
    Transfer     Supplies a pointer to the transfer block containing the current transfer buffer and buffer index.
    EndPointType Supplies the USB endpoint type.

Return Value: 
    TRUE    If the current transfer needs to be concatenated with a Zero length packet.
    FALSE   Otherwise.

Notes.
    The USB protocol states that Zero-length packet is required to notify 
    the host the end of the current transfer, so it will stop waiting for more 
    packets from the device.

    Aries waits for the Zero-length packet to mark the end of the data transfer
    and pass the buffer to the ETH code that sends the packet over the network,
    otherwise the host (Aries) will timeout waiting for EOT as it relays on
    this condition to mark the end of the transfer and forward the packet.

    The windows USB PC driver seems to have a better way to determine 
    the EOT packet rather than waiting for receiving a Zero-length packet.

    But the Zero-length packet is part of the USB protocol, so it would 
    be implemented when the host does NOT handle the EOT by other mechanism
    (like packet timeouts, etc.).

    The Synopsys USB controller spec. mentions about the Zero-length packet 
    on 8.2.3.3.

--*/
BOOLEAN IsRequiredZeroLengthPacket(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSBFNDBG_TRANSFER Transfer,
    _In_ UINT8 EndPointType
    )
{
    BOOLEAN IsNeededZeroLength;
    UINT16 MaxPacketSize;
    BOOLEAN IsHWZLPSupported;

    IsNeededZeroLength = FALSE;
    
    //
    //  There are USB HW controllers (e.g. Chipidea) that support sending the ZLP 
    //  by setting a facility bit that allows to configure the BULK OUT endpoint
    //  to send ZLP on EOT.
    //  But Synopsys controller does not have such functionality, so the ZLP has 
    //  to be sent by the high level transport protocol.
    //

    if (!NT_SUCCESS(UsbFnMpIsZeroLengthPacketSupported(Context, &IsHWZLPSupported)))
    {
        goto IsRequiredZeroLengthPacketEnd;
    }

    if ((IsHWZLPSupported == FALSE) &&
        (Transfer->Length != 0) &&
        (Context->BusSpeed != UsbBusSpeedUnknown)) {

        MaxPacketSize = 0;
        UsbFnMpGetEndpointMaxPacketSize(Context->Miniport, 
                                        EndPointType,  
                                        Context->DeviceSpeed,
                                        &MaxPacketSize);

        //
        //  Is the transfer length a multiple of the current USB max. packet size?
        //

        if ((MaxPacketSize != 0) &&
            ((Transfer->Length & (MaxPacketSize - 1)) == 0)) {
            
            IsNeededZeroLength = TRUE;
        }
    }

IsRequiredZeroLengthPacketEnd:
    return IsNeededZeroLength;
}

//------------------------------------------------------------------------------

NTSTATUS
KdUsbFnWaitForConnection(
    _In_ PUSBFNDBG_CONTEXT Context
    )
{
    
    NTSTATUS Status;
    ULONG TimeLimit;
    ULONG WaitForConnect;

    Status = STATUS_SUCCESS;

    //
    //  Do we have a 'connected' link status?
    //  Is the USB link attached, and configured for EEM/VirtEth mode?
    //

    if (Context->Connected == FALSE) {
        TimeLimit = Context->AttachTimeout;

        //
        //  No, then wait for the USB 'connected' link status.
        //  We bail out if we don't get the attached status before trying to look for 'connected' status.
        //

        for (WaitForConnect = 0; WaitForConnect < TimeLimit; WaitForConnect += TIMEOUT_STALL) {
            Status = EventHandler(Context, FALSE, NULL);
            if (!NT_SUCCESS(Status) || (Context->Connected != FALSE)) {
                LOG_TRACE("Connect Status: %x, Connect:%d", Status, Context->Connected);
                break;
            }

            if ((TimeLimit == Context->AttachTimeout) && (Context->Attached != FALSE)) {
                TimeLimit += Context->ConnectTimeout;
            }
            KeStallExecutionProcessor(TIMEOUT_STALL);
        }
    }

    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
TransferTx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG EndpointIndex,
    ULONG TransferIndex,
    ULONG TransferLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUSBFNDBG_TRANSFER Transfer = &Context->Transfer[TransferIndex];
    PUSBFNDBG_ENDPOINT Endpoint = &Context->Tx[EndpointIndex];

    // Start transfer in miniport (if we are connected or it is control ep)
    if ((EndpointIndex == 0) || Context->Connected) {

        Status = UsbFnMpTransfer(Context->Miniport,
                                 (UINT8)EndpointIndex,
                                 UsbEndpointDirectionDeviceTx,
                                 &TransferLength,
                                 Transfer->Buffer);

        if (NT_SUCCESS(Status)) {

            // Add transfer to endpoint list
            Transfer->Active = TRUE;
            Transfer->Length = TransferLength;

            Transfer->Next = INVALID_INDEX;
            if (Endpoint->LastActive == INVALID_INDEX) {
                Endpoint->FirstActive = TransferIndex;
            } else {
                Context->Transfer[Endpoint->LastActive].Next = TransferIndex;
            }
            Endpoint->LastActive = TransferIndex;

            Context->QueuedTx++;
            goto TransferTxEnd;
        }
    }

    // Return transfer to end point list if start failed or we aren't connected
    Transfer->Active = FALSE;
    Transfer->Next = INVALID_INDEX;
    if (Endpoint->LastDone == INVALID_INDEX) {
        Endpoint->FirstDone = TransferIndex;
        Endpoint->LastDone = TransferIndex;
    } else {
        Context->Transfer[Endpoint->LastDone].Next = TransferIndex;
        Endpoint->LastDone = TransferIndex;
    }

TransferTxEnd:
    Context->IsBurstTransferMode = FALSE;
    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
AbortTransferTx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG EndpointIndex)
{
    NTSTATUS Status;
    PUSBFNDBG_ENDPOINT Endpoint = &Context->Tx[EndpointIndex];
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;

    // Start transfer in miniport
    Status = UsbFnMpAbortTransfer(Context->Miniport,
                                  (UINT8)EndpointIndex,
                                  UsbEndpointDirectionDeviceTx);

    // Mark all transfers as non-active
    while (Endpoint->FirstActive != INVALID_INDEX) {

        TransferIndex = Endpoint->FirstActive;
        Transfer = &Context->Transfer[TransferIndex];

        Endpoint->FirstActive = Transfer->Next;
        if (Endpoint->FirstActive == INVALID_INDEX) {
            Endpoint->LastActive = INVALID_INDEX;
        }
        Transfer->Next = INVALID_INDEX;

        Transfer->Active = FALSE;
        Transfer->Length = 0;

        if (Endpoint->LastDone == INVALID_INDEX) {
            Endpoint->FirstDone = TransferIndex;
            Endpoint->LastDone = TransferIndex;
        } else {
            Context->Transfer[Endpoint->LastDone].Next = TransferIndex;
            Endpoint->LastDone = TransferIndex;
        }

        Context->QueuedTx--;
    }

    return Status;
}

//------------------------------------------------------------------------------

static
ULONG
DoneTransferTx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG EndpointIndex)
{
    PUSBFNDBG_ENDPOINT Endpoint = &Context->Tx[EndpointIndex];
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;

    TransferIndex = Endpoint->FirstDone;
    if (TransferIndex == INVALID_INDEX) goto DoneTransferTxEnd;

    Transfer = &Context->Transfer[TransferIndex];

    Endpoint->FirstDone = Transfer->Next;
    if (Endpoint->FirstDone == INVALID_INDEX) {
        Endpoint->LastDone = INVALID_INDEX;
    }
    Transfer->Next = INVALID_INDEX;

DoneTransferTxEnd:
    return TransferIndex;
}

//------------------------------------------------------------------------------

static
BOOLEAN
IsTransferDoneTx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG TransferIndex)
{
   PUSBFNDBG_TRANSFER Transfer = &Context->Transfer[TransferIndex];
   return !Transfer->Active;
}

//------------------------------------------------------------------------------

static
NTSTATUS
TransferRx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG EndpointIndex,
    ULONG TransferIndex,
    ULONG TransferLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUSBFNDBG_TRANSFER Transfer = &Context->Transfer[TransferIndex];
    PUSBFNDBG_ENDPOINT Endpoint = &Context->Rx[EndpointIndex];

    // Start transfer in miniport
    if ((EndpointIndex == 0) || Context->Connected) {
        Status = UsbFnMpTransfer(Context->Miniport,
                                 (UINT8)EndpointIndex,
                                 UsbEndpointDirectionDeviceRx,
                                 &TransferLength,
                                 Transfer->Buffer);
    }

    // Add transfer to endpoint list
    Transfer->Active = TRUE;
    Transfer->Next = INVALID_INDEX;
    if (Endpoint->LastActive == INVALID_INDEX) {
        Endpoint->LastActive = TransferIndex;
        Endpoint->FirstActive = TransferIndex;
    } else {
        Context->Transfer[Endpoint->LastActive].Next = TransferIndex;
        Endpoint->LastActive = TransferIndex;
    }

    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
AbortTransferRx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG EndpointIndex)
{
    NTSTATUS Status;
    PUSBFNDBG_ENDPOINT Endpoint = &Context->Rx[EndpointIndex];
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;

    // Start transfer in miniport
    Status = UsbFnMpAbortTransfer(Context->Miniport,
                                  (UINT8)EndpointIndex,
                                  UsbEndpointDirectionDeviceRx);

    // Mark all transfers as non-active
    while (Endpoint->FirstActive != INVALID_INDEX) {

        TransferIndex = Endpoint->FirstActive;
        Transfer = &Context->Transfer[TransferIndex];

        Endpoint->FirstActive = Transfer->Next;
        if (Endpoint->FirstActive == INVALID_INDEX) {
            Endpoint->LastActive = INVALID_INDEX;
        }
        Transfer->Next = INVALID_INDEX;

        Transfer->Active = FALSE;
        Transfer->Length = 0;

        if (Endpoint->LastDone == INVALID_INDEX) {
            Endpoint->FirstDone = TransferIndex;
            Endpoint->LastDone = TransferIndex;
        } else {
            Context->Transfer[Endpoint->LastDone].Next = TransferIndex;
            Endpoint->LastDone = TransferIndex;
        }

    }

    return Status;
}

//------------------------------------------------------------------------------

static
ULONG
DoneTransferRx(
    _In_ PUSBFNDBG_CONTEXT Context,
    ULONG EndpointIndex)
{
    PUSBFNDBG_ENDPOINT Endpoint = &Context->Rx[EndpointIndex];
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;

    TransferIndex = Endpoint->FirstDone;
    if (TransferIndex == INVALID_INDEX) goto DoneTransferRxEnd;

    Transfer = &Context->Transfer[TransferIndex];

    Endpoint->FirstDone = Transfer->Next;
    if (Endpoint->FirstDone == INVALID_INDEX) {
        Endpoint->LastDone = INVALID_INDEX;
    }
    Transfer->Next = INVALID_INDEX;

DoneTransferRxEnd:
    return TransferIndex;
}

//------------------------------------------------------------------------------

static
NTSTATUS
WriteEp0(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_opt_ const VOID * const Data,
    ULONG Size)
{
    NTSTATUS Status;
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG Time;

    // Get transfer
    TransferIndex = Context->Ep0Transfer;
    if (TransferIndex == INVALID_INDEX) {
        Status = STATUS_UNSUCCESSFUL;
        goto WriteEp0End;
        }
    Context->Ep0Transfer = INVALID_INDEX;
    Transfer = &Context->Transfer[TransferIndex];

    //
    // Transfer
    //

    if (Size > 0) {

        // Copy date to transfer buffer
        if (Size > Context->MaxTransferSize) Size = Context->MaxTransferSize;
        RtlCopyMemory(Transfer->Buffer, Data, Size);

        // Start transfer
        Status = TransferTx(Context, 0, TransferIndex, Size);
        if (!NT_SUCCESS(Status)) {
            Context->Ep0Transfer = TransferIndex;
            goto WriteEp0End;
        }

        // Wait for transfer
        for (Time = 0; Time < TRANSFER_TIMEOUT; Time += TIMEOUT_STALL) {
            Status = EventHandler(Context, TRUE, NULL);
            if (!NT_SUCCESS(Status) || !Context->Attached) {
                AbortTransferTx(Context, 0);
                Context->Ep0Transfer = DoneTransferTx(Context, 0);
                Status = STATUS_UNSUCCESSFUL;
                goto WriteEp0End;
                }
            if (!Transfer->Active) break;
            KeStallExecutionProcessor(TIMEOUT_STALL);
        }

        // If it isn't done in time limit abort it
        if (Transfer->Active) {
            AbortTransferTx(Context, 0);
            Context->Ep0Transfer = DoneTransferTx(Context, 0);
            Status = STATUS_UNSUCCESSFUL;
            goto WriteEp0End;
        }

    }

    //
    // Handshake
    //

    Status = TransferRx(Context, 0, TransferIndex, 0);
    if (!NT_SUCCESS(Status)) {
        Context->Ep0Transfer = TransferIndex;
        goto WriteEp0End;
    }

    // Wait for transfer
    for (Time = 0; Time < TRANSFER_TIMEOUT; Time += TIMEOUT_STALL) {
        Status = EventHandler(Context, TRUE, NULL);
        if (!NT_SUCCESS(Status) || !Context->Attached) {
            AbortTransferRx(Context, 0);
            Context->Ep0Transfer = DoneTransferRx(Context, 0);
            Status = STATUS_UNSUCCESSFUL;
            goto WriteEp0End;
            }
        if (!Transfer->Active) break;
        KeStallExecutionProcessor(TIMEOUT_STALL);
    }

    // If it isn't done in time limit abort it
    if (Transfer->Active) {
        AbortTransferRx(Context, 0);
        Context->Ep0Transfer = DoneTransferRx(Context, 0);
        Status = STATUS_UNSUCCESSFUL;
        goto WriteEp0End;
    }

    // Return transfer
    Context->Ep0Transfer = DoneTransferRx(Context, 0);

    // Done
    Status = STATUS_SUCCESS;

WriteEp0End:
    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
ReadEp0(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PVOID Data,
    _Inout_ PULONG Size)
{
    NTSTATUS Status;
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG Time;

    // Get transfer
    TransferIndex = Context->Ep0Transfer;
    if (TransferIndex == INVALID_INDEX) {
        Status = STATUS_UNSUCCESSFUL;
        goto ReadEp0End;
        }
    Context->Ep0Transfer = INVALID_INDEX;
    Transfer = &Context->Transfer[TransferIndex];

    //
    // Transfer
    //

    if ((Size != NULL) && (*Size > 0)) {

        // Limit transfer size
        if (*Size > Context->MaxTransferSize) *Size = Context->MaxTransferSize;

        // Start transfer
        Status = TransferRx(Context, 0, TransferIndex, *Size);
        if (!NT_SUCCESS(Status)) {
            Context->Ep0Transfer = TransferIndex;
            goto ReadEp0End;
        }

        // Wait for transfer
        for (Time = 0; Time < TRANSFER_TIMEOUT; Time += TIMEOUT_STALL) {
            Status = EventHandler(Context, TRUE, NULL);
            if (!NT_SUCCESS(Status) || !Context->Attached) {
                AbortTransferRx(Context, 0);
                Context->Ep0Transfer = DoneTransferRx(Context, 0);
                Status = STATUS_UNSUCCESSFUL;
                goto ReadEp0End;
            }
            if (!Transfer->Active) break;
            KeStallExecutionProcessor(TIMEOUT_STALL);
        }

        // If it isn't done in time limit abort it
        if (Transfer->Active) {
            AbortTransferRx(Context, 0);
            Context->Ep0Transfer = DoneTransferRx(Context, 0);
            Status = STATUS_UNSUCCESSFUL;
            goto ReadEp0End;
        }

        // Copy received data and update transfer length
        RtlCopyMemory(Data, Transfer->Buffer, Transfer->Length);
        *Size = Transfer->Length;
    }

    //
    // Handshake
    //

    Status = TransferTx(Context, 0, TransferIndex, 0);
    if (!NT_SUCCESS(Status)) {
        Context->Ep0Transfer = TransferIndex;
        goto ReadEp0End;
    }

    // Wait for transfer
    for (Time = 0; Time < TRANSFER_TIMEOUT; Time += TIMEOUT_STALL) {
        Status = EventHandler(Context, TRUE, NULL);
        if (!NT_SUCCESS(Status) || !Context->Attached) {
            AbortTransferTx(Context, 0);
            Context->Ep0Transfer = DoneTransferTx(Context, 0);
            Status = STATUS_UNSUCCESSFUL;
            goto ReadEp0End;
        }
        if (!Transfer->Active) break;
        KeStallExecutionProcessor(TIMEOUT_STALL);
    }

    // If it isn't done in time limit abort it
    if (Transfer->Active) {
        AbortTransferTx(Context, 0);
        Context->Ep0Transfer = DoneTransferTx(Context, 0);
        Status = STATUS_UNSUCCESSFUL;
        goto ReadEp0End;
    }

    // Return transfer
    Context->Ep0Transfer = DoneTransferTx(Context, 0);

    // Done
    Status = STATUS_SUCCESS;

ReadEp0End:
    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
StallEp0(
    _In_ PUSBFNDBG_CONTEXT Context,
    UCHAR Dir)
{
    return UsbFnMpSetEndpointStallState(
        Context->Miniport, 0,
        Dir ? UsbEndpointDirectionDeviceRx : UsbEndpointDirectionDeviceTx,
        TRUE);
}

//------------------------------------------------------------------------------

static
NTSTATUS
StartController(
    _In_ PUSBFNDBG_CONTEXT Context,
    _Inout_opt_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _In_opt_ PVOID DmaBuffer)
{
    NTSTATUS Status;
    ULONG Index;
    ULONG TransferIndex;
    PUSBFNDBG_ENDPOINT Endpoint;
    PUSBFNDBG_TRANSFER Transfer;

    // Initialize internal structures (note buffer allocation must be done
    // after controller is started).

    for (Index = 0; Index < TX_ENDPT;  Index++) {
        Endpoint = &Context->Tx[Index];
        Endpoint->FirstDone = INVALID_INDEX;
        Endpoint->LastDone = INVALID_INDEX;
        Endpoint->FirstActive = INVALID_INDEX;
        Endpoint->LastActive = INVALID_INDEX;
    }

    for (Index = 0; Index < RX_ENDPT;  Index++) {
        Endpoint = &Context->Rx[Index];
        Endpoint->FirstDone = INVALID_INDEX;
        Endpoint->LastDone = INVALID_INDEX;
        Endpoint->FirstActive = INVALID_INDEX;
        Endpoint->LastActive = INVALID_INDEX;
    }

    Context->BusSpeed = UsbBusSpeedUnknown;

    // Call miniport to start controller
    Status = UsbFnMpStartController(Context->Miniport, Device, DmaBuffer);
    if (!NT_SUCCESS(Status)) goto StartControllerEnd;

    // Controller is now running
    Context->Running = TRUE;

    // Get maximal transfer size
    Status = UsbFnMpGetMaxTransferSize(Context->Miniport,
                                       &Context->MaxTransferSize);

    if (!NT_SUCCESS(Status)) goto StartControllerEnd;

    // Pre-allocate transfers
    for (Index = 0; Index < MAX_TRANSFERS; Index++) {
        Transfer = &Context->Transfer[Index];
        Status = UsbFnMpAllocateTransferBuffer(Context->Miniport,
                                               Context->MaxTransferSize,
                                               &Transfer->Buffer);
        if (!NT_SUCCESS(Status)) break;
        Transfer->Next = INVALID_INDEX;
    }

    // We need at least one transfer per EP
    if (Index < MIN_TRANSFERS) {
        Status = STATUS_UNSUCCESSFUL;
        UsbFnMpStopController(Context->Miniport);
        Context->Running = FALSE;
        goto StartControllerEnd;
    }

    // Save number of transfers to context
    Context->Transfers = Index;

    // Pre-allocate transfer for EP0/Control transfers
    Context->Ep0Transfer = 0;
    TransferIndex = 1;

    // Attach BULKIN transfers to endpoint
    for (Index = 0; Index < BULKIN_TRANSFERS; Index++) {
        Endpoint = &Context->Tx[BULKIN_ENDPT];
        Transfer = &Context->Transfer[TransferIndex];

        Transfer->Active = FALSE;
        Transfer->Length = Context->MaxTransferSize;
        Transfer->Next = INVALID_INDEX;

        if (Endpoint->LastDone == INVALID_INDEX) {
            Endpoint->FirstDone = TransferIndex;
        } else {
            Context->Transfer[Endpoint->LastDone].Next = TransferIndex;
        }
        Endpoint->LastDone = TransferIndex;

        if (++TransferIndex >= Context->Transfers) break;
    }

    if (TransferIndex >= Context->Transfers) {
        Status = STATUS_UNSUCCESSFUL;
        UsbFnMpStopController(Context->Miniport);
        Context->Running = FALSE;
        goto StartControllerEnd;
    }

    // Attach BULKOUT transfers to endpoint
    for (Index = 0; Index < BULKOUT_TRANSFERS; Index++) {
        Endpoint = &Context->Rx[BULKOUT_ENDPT];
        Transfer = &Context->Transfer[TransferIndex];

        Transfer->Active = TRUE;
        Transfer->Length = Context->MaxTransferSize;
        Transfer->Next = INVALID_INDEX;

        if (Endpoint->LastActive == INVALID_INDEX) {
            Endpoint->FirstActive = TransferIndex;
        } else {
            Context->Transfer[Endpoint->LastActive].Next = TransferIndex;
        }
        Endpoint->LastActive = TransferIndex;

        if (++TransferIndex >= Context->Transfers) break;
    }

    // There isn't any pending send transfer
    Context->QueuedTx = 0;

StartControllerEnd:
    return Status;
}

//------------------------------------------------------------------------------

static
VOID
StopController(
    _In_ PUSBFNDBG_CONTEXT Context)
{
    UsbFnMpStopController(Context->Miniport);
    Context->Running = FALSE;
}

//------------------------------------------------------------------------------

static
VOID
UpdateUsbDescriptors(
    __in PUSBFNDBG_CONTEXT Context,
    USBFNMP_BUS_SPEED ubs)
{
    UNREFERENCED_PARAMETER(Context);

    switch (ubs) {
    case UsbBusSpeedLow:
    case UsbBusSpeedFull:
    default:
        UsbConfigDesc[0x16] = 64;       // EP1 OUT
        UsbConfigDesc[0x17] = 0;
        UsbConfigDesc[0x1D] = 64;       // EP1 IN
        UsbConfigDesc[0x1E] = 0;
        break;

    case UsbBusSpeedHigh:
        UsbConfigDesc[0x16] = 0;       // EP1 OUT
        UsbConfigDesc[0x17] = 2;
        UsbConfigDesc[0x1D] = 0;       // EP1 IN
        UsbConfigDesc[0x1E] = 2;
        break;

    case UsbBusSpeedSuper:
        UsbConfigDesc[0x16] = 0;       // EP1 OUT
        UsbConfigDesc[0x17] = 4;
        UsbConfigDesc[0x23] = 0;       // EP1 IN
        UsbConfigDesc[0x24] = 4;
        break;
    }

    return;
}

//------------------------------------------------------------------------------

NTSTATUS
ConfigureEnableEndpoints(
    _In_ PUSBFNDBG_CONTEXT Context)
{
    PUSB_ENDPOINT_DESCRIPTOR EpDesc0[2];
    USBFNMP_INTERFACE_INFO IfcInfo[1];
    PUSBFNMP_INTERFACE_INFO IfcInfoTable[1];
    USBFNMP_CONFIGURATION_INFO ConfInfo[1];
    PUSBFNMP_CONFIGURATION_INFO ConfInfoTable[1];
    USBFNMP_DEVICE_INFO DeviceInfo;
    PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR EpCompanionDesc[2];

    //
    // Set the Bulk OUT EP Descriptor
    //

    EpDesc0[0] = GET_PTR_TO_EP_OUT(Context->BusSpeed, UsbConfigDesc);

    //
    // Set the associated Bulk OUT Companion Descriptor
    //

    EpCompanionDesc[0] = GET_PTR_TO_EP_COMPANION_OUT(Context->BusSpeed, UsbConfigDesc);

    //
    // Set the Bulk IN EP Descriptor
    //

    EpDesc0[1] = GET_PTR_TO_EP_IN(Context->BusSpeed, UsbConfigDesc);

    //
    // Set the associated Bulk IN Companion Descriptor
    //

    EpCompanionDesc[1] = GET_PTR_TO_EP_COMPANION_IN(Context->BusSpeed, UsbConfigDesc);
    IfcInfo[0].InterfaceDescriptor = GET_PTR_TO_INTERFACE(Context->BusSpeed, UsbConfigDesc);
    IfcInfo[0].EndpointDescriptorTable = EpDesc0;
    IfcInfo[0].EndpointCompanionDescriptor = EpCompanionDesc;
    IfcInfoTable[0] = &IfcInfo[0];
    ConfInfo[0].ConfigDescriptor = GET_PTR_TO_CONFIGURATION(Context->BusSpeed, UsbConfigDesc);
    ConfInfo[0].InterfaceInfoTable = IfcInfoTable;
    ConfInfoTable[0] = &ConfInfo[0];

    //
    // Set the Device Descriptor
    //

    DeviceInfo.DeviceDescriptor = GET_PTR_TO_DEVICE(Context->BusSpeed, UsbDeviceDesc);
    DeviceInfo.ConfigurationInfoTable = ConfInfoTable;
    return UsbFnMpConfigureEnableEndpoints(Context->Miniport, &DeviceInfo);
}

//------------------------------------------------------------------------------

NTSTATUS
Configure(
    _In_ PUSBFNDBG_CONTEXT Context)
{
    NTSTATUS Status;
    ULONG EndpointIndex;
    PUSBFNDBG_ENDPOINT Endpoint;
    ULONG TransferIndex;

    // First clear context
    Context->TxPacketFailed = FALSE;
    Context->EemTx = INVALID_INDEX;
    Context->EemRx = INVALID_INDEX;
    Context->EemRxPos = 0;
    Context->RxBufferBusy = FALSE;
    Context->RxBufferLength = 0;

    // Then configure endpoints
    Status = ConfigureEnableEndpoints(Context);
    if (!NT_SUCCESS(Status)) goto ConfigureEnd;

    // Restart transactions if there are some...
    for (EndpointIndex = 1; EndpointIndex < TX_ENDPT; EndpointIndex++) {

        Endpoint = &Context->Tx[EndpointIndex];

        TransferIndex = Endpoint->FirstActive;
        while (TransferIndex != INVALID_INDEX) {
            PUSBFNDBG_TRANSFER Transfer = &Context->Transfer[TransferIndex];

            Status = UsbFnMpTransfer(Context->Miniport,
                                     (UINT8)EndpointIndex,
                                     UsbEndpointDirectionDeviceTx,
                                     &Transfer->Length,
                                     Transfer->Buffer);

            if (!NT_SUCCESS(Status)) goto ConfigureEnd;

            Context->QueuedTx++;
            TransferIndex = Transfer->Next;
        }
    }

    for (EndpointIndex = 1; EndpointIndex < RX_ENDPT; EndpointIndex++) {

        Endpoint = &Context->Rx[EndpointIndex];

        TransferIndex = Endpoint->FirstActive;
        while (TransferIndex != INVALID_INDEX) {
            PUSBFNDBG_TRANSFER Transfer = &Context->Transfer[TransferIndex];

            Transfer->Length = Context->MaxTransferSize;

            Status = UsbFnMpTransfer(Context->Miniport,
                                     (UINT8)EndpointIndex,
                                     UsbEndpointDirectionDeviceRx,
                                     &Transfer->Length,
                                     Transfer->Buffer);

            if (!NT_SUCCESS(Status)) goto ConfigureEnd;

            TransferIndex = Transfer->Next;
        }
    }

ConfigureEnd:
    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
RequestGetDescriptor(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{
    NTSTATUS Status;
    ULONG Length;
    USB_STRING_MAC Mac;
    ULONG Idx;

    switch (Request->wValue.HiByte)
        {
        case USB_DEVICE_DESCRIPTOR_TYPE:
            LOG_TRACE("%s mode\r\n", Context->EemMode ? "EEM" : "VirtEth");
            Length = UsbDeviceDesc[0];
            if (Length > Request->wLength) Length = Request->wLength;
            Status = WriteEp0(Context, UsbDeviceDesc, Length);
            break;
        case USB_CONFIGURATION_DESCRIPTOR_TYPE:
            Length = UsbConfigDesc[2] | (UsbConfigDesc[3] << 8);
            if (Length > Request->wLength) Length = Request->wLength;
            Status = WriteEp0(Context, UsbConfigDesc, Length);
            break;
        case USB_STRING_DESCRIPTOR_TYPE:
            switch (Request->wValue.LowByte) {
            case 0x00:
                Length = SupportedLang.bLength;
                if (Length > Request->wLength) Length = Request->wLength;
                Status = WriteEp0(Context, &SupportedLang, Length);
                break;
            case 0x01:
                Length = Manufacturer.bLength;
                if (Length > Request->wLength) Length = Request->wLength;
                Status = WriteEp0(Context, &Manufacturer, Length);
                break;
            case 0x02:
                Length = Product->bLength;
                if (Length > Request->wLength) Length = Request->wLength;
                Status = WriteEp0(Context, Product, Length);
                break;
            case 0x03:
                Mac.bLength = sizeof(Mac);
                Mac.bDescriptorType = USB_STRING_DESCRIPTOR_TYPE;
                for (Idx = 0; Idx < sizeof(Context->Mac); Idx++) {
                    WCHAR Ch = (Context->Mac[Idx] >> 4);
                    Ch = (Ch < 10) ? Ch + L'0' : Ch + L'A' - 10;
                    Mac.pwcbString[Idx << 1] = Ch;
                    Ch = Context->Mac[Idx] & 0x0F;
                    Ch = (Ch < 10) ? Ch + L'0' : Ch + L'A' - 10;
                    Mac.pwcbString[(Idx << 1) + 1] = Ch;
                }
                Length = Mac.bLength;
                if (Length > Request->wLength) Length = Request->wLength;
                Status = WriteEp0(Context, &Mac, Length);
                break;
            case 0xEE:
                Length = OsDescriptor.bLength;
                if (Length > Request->wLength) Length = Request->wLength;
                Status = WriteEp0(Context, &OsDescriptor, Length);
                break;
            default:
                Status = StallEp0(Context, Request->bmRequestType.Dir);
                break;
            }
            break;

        case USB_BOS_DESCRIPTOR_TYPE:
            
            //
            // BOS descriptor handling (see Table 9-11).  
            // This descriptor is used by the host to read individual device capability descriptors. 
            //

            LOG_TRACE("Get BOS SS descriptor: %d\r\n", Request->wLength);
            
            Length = UsbDeviceBOSDescriptor[2] | (UsbDeviceBOSDescriptor[3] << 8);
            if (Length > Request->wLength) {
                Length = Request->wLength;
            }
            
            Status = WriteEp0(Context, UsbDeviceBOSDescriptor, Length);
            break;

        default:
            Status = StallEp0(Context, Request->bmRequestType.Dir);
            break;
        }

    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
StandardRequestForDevice(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{

    NTSTATUS Status;

    switch (Request->bRequest) {
    case USB_REQUEST_GET_STATUS:
        Status = WriteEp0(Context, &Context->DeviceStatus, 2);
        break;

    case USB_REQUEST_CLEAR_FEATURE:
    case USB_REQUEST_SET_FEATURE:
        Status = ReadEp0(Context, NULL, NULL);

        //
        //  The Set Feature setup is invalid if the device is not
        //  in the configuration state.
        //

        if (Context->Configured == FALSE) {
            StallEp0(Context, Request->bmRequestType.Dir);
        }
        break;

    case USB_REQUEST_SET_ADDRESS:
        Status = ReadEp0(Context, NULL, NULL);
        break;

    case USB_REQUEST_GET_DESCRIPTOR:
        Status = RequestGetDescriptor(Context, Request);
        break;

    case USB_REQUEST_GET_CONFIGURATION:
        Status = WriteEp0(Context,
                          &Context->DeviceConfig,
                          sizeof(Context->DeviceConfig));
        break;

    case USB_REQUEST_SET_CONFIGURATION:
        Context->DeviceConfig = (UINT8)Request->wValue.W;
        Status = ConfigureEnableEndpoints(Context);
        if (!NT_SUCCESS(Status)) {
            StallEp0(Context, Request->bmRequestType.Dir);
            break;
        }

        Status = ReadEp0(Context, NULL, NULL);
        if (!NT_SUCCESS(Status)) {
            break;
        }

        Context->Configured = TRUE;
        UsbFnMpTimeDelay(Context->Miniport, NULL, &Context->DiscoveryTimeBase);
        if (Context->EemMode) {
            Status = Configure(Context);
        }
        break;

    case USB_REQUEST_ISOCH_DELAY:
        Status = ReadEp0(Context, NULL, NULL);
        break;

    case USB_REQUEST_SET_SEL:
        Status = ReadEp0(Context, NULL, NULL);
        break;

    default:
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        break;
    }

   return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
StandardRequestForEndpoint(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{
    NTSTATUS Status;
    UINT8 EndpointIndex;
    USBFNMP_ENDPOINT_DIRECTION EndpointDirection;
    BOOLEAN Stalled;


    EndpointIndex = Request->wIndex.LowByte & USB_ENDPOINT_ADDRESS_MASK;
    EndpointDirection = USB_ENDPOINT_DIRECTION_IN(Request->wIndex.LowByte) ?
            UsbEndpointDirectionHostIn : UsbEndpointDirectionHostOut;

    switch (Request->bRequest)
        {
        case USB_REQUEST_GET_STATUS:
            Status = UsbFnMpGetEndpointStallState(
                Context->Miniport, EndpointIndex, EndpointDirection, &Stalled
                );
            if (NT_SUCCESS(Status)) {
                UINT16 Data = Stalled ? 0x0001 : 0x0000;
                Status = WriteEp0(Context, &Data, sizeof(Data));
            } else {
                Status = StallEp0(Context, Request->bmRequestType.Dir);
            }
            break;
        case USB_REQUEST_CLEAR_FEATURE:
            switch (Request->wValue.W)
                {
                case 0x0000:
                    Status = UsbFnMpSetEndpointStallState(
                        Context->Miniport, EndpointIndex, EndpointDirection,
                        FALSE
                        );
                    if (NT_SUCCESS(Status)) {
                        Status = ReadEp0(Context, NULL, NULL);
                    }
                    break;
                default:
                    Status = StallEp0(Context, Request->bmRequestType.Dir);
                    break;
                }
            break;
        case USB_REQUEST_SET_FEATURE:
            switch (Request->wValue.W)
                {
                case 0x0000:
                    Status = UsbFnMpSetEndpointStallState(
                        Context->Miniport, EndpointIndex, EndpointDirection,
                        TRUE
                        );
                    if (NT_SUCCESS(Status)) {
                        Status = ReadEp0(Context, NULL, NULL);
                    }
                    break;
                default:
                    Status = StallEp0(Context, Request->bmRequestType.Dir);
                    break;
                }
            break;
        default:
            Status = StallEp0(Context, Request->bmRequestType.Dir);
            break;
        }

    return Status;
}

//------------------------------------------------------------------------------


static
NTSTATUS
ClassRequestForInterface(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{
    NTSTATUS Status;
    ULONG Length;

    switch (Request->bRequest) {
    case 0x20: // SET_LINE_CODING
        Length = Request->wLength;
        if (Length > sizeof(Context->LineCoding)) {
            Length = sizeof(Context->LineCoding);
        }
        Status = ReadEp0(Context, &Context->LineCoding, &Length);
        break;
    
    case 0x21: // GET_LINE_CODING
        Length = Request->wLength;
        if (Length > sizeof(Context->LineCoding)) {
            Length = sizeof(Context->LineCoding);
        }
        Status = WriteEp0(Context, &Context->LineCoding, Length);
        break;
    
    case 0x22: // SET_CONTROL_LINE_STATE
        Status = ReadEp0(Context, NULL, NULL);
        if (!NT_SUCCESS(Status)) {
            break;
        }
        
        Context->Connected = (Request->wValue.W != 0);
        if (!Context->EemMode) {
            if (Context->Connected) {
                Status = Configure(Context);
            }

            if (Request->wValue.W == 0xEEEE) {
                Context->EemMode = TRUE;
            }

        } else {
            if (Context->Connected != FALSE) {
                LOG_TRACE("EEM-MODE SET_CONTROL_LINE_STATE");

            } else {
                LOG_TRACE("EEM-MODE WAS NOT SET IN SET_CONTROL_LINE_STATE");
                Status = STATUS_UNSUCCESSFUL;
            }
        }
        break;

    default:
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        break;
    }

    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
VendorRequestForDevice(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{
    NTSTATUS Status;
    ULONG Length;

    if (Context->EemMode) {
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        goto VendorRequestForDeviceEnd;
    }

    if ((Request->bRequest != '!') || (Request->bmRequestType.Dir != 1)) {
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        goto VendorRequestForDeviceEnd;
    }

    switch (Request->wIndex.W) {
    case MS_EXT_CONFIG_DESC_INDEX:
        Length = Request->wLength;
        if (Length > sizeof(UsbCompIdWinUsb)) {
            Length = sizeof(UsbCompIdWinUsb);
        }
        Status = WriteEp0(Context, &UsbCompIdWinUsb, Length);
        break;
    case MS_EXT_PROP_DESC_INDEX:
        Length = Request->wLength;
        if (Length > sizeof(UsbPropertyWinUsb)) {
            Length = sizeof(UsbPropertyWinUsb);
        }
        Status = WriteEp0(Context, &UsbPropertyWinUsb, Length);
        break;
    default:
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        break;
        }

VendorRequestForDeviceEnd:
    return Status;
}

//------------------------------------------------------------------------------

static
NTSTATUS
VendorRequestForInterface(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{
    NTSTATUS Status;
    ULONG Length;

    if (Context->EemMode) {
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        goto VendorRequestForInterfaceEnd;
    }

    if ((Request->bRequest != '!') || (Request->bmRequestType.Dir != 1)) {
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        goto VendorRequestForInterfaceEnd;
    }

    switch (Request->wIndex.W) {
    case MS_EXT_PROP_DESC_INDEX:
        Length = Request->wLength;
        if (Length > sizeof(UsbPropertyWinUsb)) {
            Length = sizeof(UsbPropertyWinUsb);
        }
        Status = WriteEp0(Context, &UsbPropertyWinUsb, Length);
        break;
    default:
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        break;
        }

VendorRequestForInterfaceEnd:
    return Status;
}

//------------------------------------------------------------------------------

NTSTATUS
SetupPacket(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Request)
{
    NTSTATUS Status;

    LOG_TRACE("%02x.%02x.%04x.%04x.%04x",
        Request->bmRequestType.B,
        Request->bRequest,
        Request->wValue.W,
        Request->wIndex.W,
        Request->wLength);

    switch (Request->bmRequestType.Type) {
    case BMREQUEST_STANDARD:
        switch (Request->bmRequestType.Recipient) {
        case BMREQUEST_TO_DEVICE:
            Status = StandardRequestForDevice(Context, Request);
            break;
        case BMREQUEST_TO_ENDPOINT:
            Status = StandardRequestForEndpoint(Context, Request);
            break;
        case BMREQUEST_TO_INTERFACE:
        case BMREQUEST_TO_OTHER:
        default:
            Status = StallEp0(Context, Request->bmRequestType.Dir);
            break;
        }
        break;
    case BMREQUEST_CLASS:
        switch (Request->bmRequestType.Recipient) {
        case BMREQUEST_TO_INTERFACE:
            Status = ClassRequestForInterface(Context, Request);
            break;
        case BMREQUEST_TO_DEVICE:
        case BMREQUEST_TO_ENDPOINT:
        case BMREQUEST_TO_OTHER:
        default:
            Status = StallEp0(Context, Request->bmRequestType.Dir);
            break;
        }
        break;
    case BMREQUEST_VENDOR:
        switch (Request->bmRequestType.Recipient) {
        case BMREQUEST_TO_DEVICE:
            Status = VendorRequestForDevice(Context, Request);
            break;
        case BMREQUEST_TO_INTERFACE:
            Status = VendorRequestForInterface(Context, Request);
            break;
        case BMREQUEST_TO_ENDPOINT:
        case BMREQUEST_TO_OTHER:
        default:
            Status = StallEp0(Context, Request->bmRequestType.Dir);
            break;
        }
        break;
    default:
        Status = StallEp0(Context, Request->bmRequestType.Dir);
        break;
    }

    return Status;
}

//------------------------------------------------------------------------------

ULONG
EemLengthFromHeader(
    USHORT Header)
{
    ULONG Length;

    if ((Header & EEM_HEADER_CMD_FLAG) != 0) {
        switch (Header & EEM_HEADER_CMD_MASK) {
            case EEM_HEADER_CMD_ECHO:
            case EEM_HEADER_CMD_ECHO_REPLY:
                Length = Header & EEM_HEADER_LENGTH_MASK;
                break;
            case EEM_HEADER_CMD_SUSPEND_HINT:
            case EEM_HEADER_CMD_RESPONSE_HINT:
            case EEM_HEADER_CMD_COMPLETE_HINT:
            case EEM_HEADER_CMD_TICKLE:
                Length = 0;
                break;
            default:
                Length = Header & EEM_HEADER_LENGTH_MASK;
                break;
        }
    } else {
        Length = Header & EEM_HEADER_LENGTH_MASK;
    }

    return sizeof(Header) + Length;
}

//------------------------------------------------------------------------------

BOOLEAN
EemValidPacket(
    _In_reads_(Length) PUCHAR Data,
    ULONG Length)
{
    BOOLEAN rc = FALSE;
    USHORT header;
    ULONG32 dataCrc;
    ULONG32 crc;

    // It isn't valid frame if we didn't get header
    if (Length < EEM_HEADER_LEN) {
        goto EemValidPacketEnd;
    }

    // Extract header
    header = (Data[0] << 8) | Data[1];
    Data += EEM_HEADER_LEN;
    Length -= EEM_HEADER_LEN;

    // We will ignore command frames
    if ((header & EEM_HEADER_CMD_FLAG) != 0) {
        goto EemValidPacketEnd;
    }

    // And special zero length frames
    if (header == 0) {
        goto EemValidPacketEnd;
    }

    RtlCopyMemory(&dataCrc, &Data[Length - EEM_CRC_LEN], sizeof(ULONG32));
    
    //
    //  Does the header contain a calculated CRC?
    //  
    //  EEM Data Packet Header is a 2 bytes field.
    //  1bit    1bit    14 bits
    //     0    bmCRC   Length of the Ethernet frame
    //
    //  The bmCrC bit field is set to 0 if the Ethernet frame CRC is set to 0xdeadbeef
    //  The bmCrC bit field is set to 1 if the Ethernet frame CRC has been calculated
    //
    if ((header & EEM_HEADER_CRC_FLAG) != 0) {
        // Yes, it contains a calculated CRC, so validate it by using a table lookup CRC method. 
        crc = ComputeCrc32(0, Data, Length - EEM_CRC_LEN);
    } else {
        // No, it has a sentinel value.
        crc = 0xEFBEADDE;
    }
    
    if (dataCrc != crc) {
        goto EemValidPacketEnd;
    }

    // It is valid EEM frame
    rc = TRUE;

EemValidPacketEnd:
    return rc;
}

//------------------------------------------------------------------------------

NTSTATUS
EventHandler(
    _In_ PUSBFNDBG_CONTEXT Context,
    BOOLEAN InConfigTransaction,
    _Out_opt_ PBOOLEAN Idle)
{
    NTSTATUS Status = STATUS_SUCCESS;
    USBFNMP_MESSAGE Message;
    USBFNMP_MESSAGE_PAYLOAD Payload;
    ULONG PayloadSize;
    PUSBFNDBG_ENDPOINT Endpoint;
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG Delta;
    BOOLEAN InitialConnectStatus;

    //
    // If there is pending setup packet it should be processed
    //

    InitialConnectStatus = Context->Connected;
    if (!InConfigTransaction && Context->PendingUdr) {
        USB_DEFAULT_PIPE_SETUP_PACKET PendingUdrPacket;

        RtlCopyMemory(&PendingUdrPacket,
                      &Context->PendingUdrPacket,
                      sizeof(PendingUdrPacket));
        Context->PendingUdr = FALSE;
        SetupPacket(Context, &PendingUdrPacket);
        if (Idle != NULL) *Idle = FALSE;
        goto EventHandlerEnd;
    }

    //
    // In discovery mode check for connection timeout
    //

    if (Context->DiscoveryEnabled) {

        UsbFnMpTimeDelay(Context->Miniport,
                         &Delta,
                         &Context->DiscoveryTimeBase);

        if (Context->DiscoveryInReset) {

            if (Delta > DISCOVERY_RESET_DELAY) {

                // Restart controller after reset delay
                StartController(Context, NULL, NULL);
                Context->DiscoveryInReset = FALSE;

                // Switch configuration
                if (!Context->EemMode) {
                    Context->EemMode = TRUE;
                    GetConfigDescriptorByBusSpeed(Context->BusSpeed, Context->EemMode, &UsbDeviceDesc, &UsbConfigDesc);
                    Product = &ProductEem;
                    Context->DiscoveryEnabled = FALSE;
                }
            }

            if (Idle != NULL) *Idle = TRUE;
            goto EventHandlerEnd;
        }

        if (Context->Configured && !Context->Connected &&
            (Delta >= Context->DiscoveryDelay)) {

            StopController(Context);
            Context->DiscoveryInReset = TRUE;
            UsbFnMpTimeDelay(Context->Miniport,
                             NULL,
                             &Context->DiscoveryTimeBase);

            if (Idle != NULL) *Idle = TRUE;
            goto EventHandlerEnd;
         }
    }

    //
    // Call miniport to get message
    //

    PayloadSize = sizeof(Payload);
    Status = UsbFnMpEventHandler(Context->Miniport,
                                 &Message,
                                 &PayloadSize,
                                 &Payload);

    if (!NT_SUCCESS(Status)) goto EventHandlerEnd;

    switch (Message) {

        //
        // A packet received on an endpoint
        //
        case UsbMsgEndpointStatusChangedRx:
            Endpoint = &Context->Rx[Payload.utr.EndpointIndex];

            TransferIndex = Endpoint->FirstActive;
            if (TransferIndex == INVALID_INDEX) break;

            Transfer = &Context->Transfer[TransferIndex];

            Endpoint->FirstActive = Transfer->Next;
            if (Endpoint->FirstActive == INVALID_INDEX) {
                Endpoint->LastActive = INVALID_INDEX;
            }
            Transfer->Next = INVALID_INDEX;

            //_ASSERT(Transfer->Buffer == Payload.utr.Buffer)
            Transfer->Active = FALSE;
            if (Payload.utr.TransferStatus == UsbTransferStatusComplete) {
                Transfer->Length = Payload.utr.BytesTransferred;
            } else {
                Transfer->Length = 0;
            }

            Transfer->Next = INVALID_INDEX;
            if (Endpoint->LastDone == INVALID_INDEX) {
                Endpoint->FirstDone = TransferIndex;
            } else {
                Context->Transfer[Endpoint->LastDone].Next = TransferIndex;
            }
            Endpoint->LastDone = TransferIndex;

            break;

        //
        // A packet sent on an endpoint.
        //
        case UsbMsgEndpointStatusChangedTx:
            Endpoint = &Context->Tx[Payload.utr.EndpointIndex];

            TransferIndex = Endpoint->FirstActive;
            if (TransferIndex == INVALID_INDEX) break;

            Transfer = &Context->Transfer[TransferIndex];

            Endpoint->FirstActive = Transfer->Next;
            if (Endpoint->FirstActive == INVALID_INDEX) {
                Endpoint->LastActive = INVALID_INDEX;
            }
            Transfer->Next = INVALID_INDEX;

            //_ASSERT(Transfer->Buffer == Payload.utr.Buffer)
            Transfer->Active = FALSE;
            if (Payload.utr.TransferStatus == UsbTransferStatusComplete) {
                Transfer->Length = Payload.utr.BytesTransferred;
            } else {
                Transfer->Length = 0;
            }

            Transfer->Next = INVALID_INDEX;
            if (Endpoint->LastDone == INVALID_INDEX) {
                Endpoint->FirstDone = TransferIndex;
            } else {
                Context->Transfer[Endpoint->LastDone].Next = TransferIndex;
            }
            Endpoint->LastDone = TransferIndex;

            if (Context->QueuedTx >= Payload.utr.NumberOfTransfers) {
                Context->QueuedTx -= Payload.utr.NumberOfTransfers;

            } else {
                Context->QueuedTx--;
            }
            break;

        //
        // Set up packet received.
        //
        case UsbMsgSetupPacket:
            // If we are in config transaction save new setup packet
            if (InConfigTransaction) {
                RtlCopyMemory(&Context->PendingUdrPacket,
                              &Payload.udr,
                              sizeof(Context->PendingUdrPacket));
                Context->PendingUdr = TRUE;
                break;
            }

            LOG_TRACE("Setup Packet received");
            SetupPacket(Context, &Payload.udr);
            break;

        case UsbMsgBusEventAttach:
            LOG_TRACE("Attach");
            Context->Attached = TRUE;
            Context->Configured = FALSE;
            Context->Connected = FALSE;
            break;

        case UsbMsgBusEventDetach:
            LOG_TRACE("Detach");
            Context->Attached = FALSE;
            Context->Configured = FALSE;
            Context->Connected = FALSE;
            break;

        case UsbMsgBusEventReset:
            LOG_TRACE("Reset");
            Context->Attached = TRUE;
            Context->Configured = FALSE;
            Context->Connected = FALSE;
            Context->UsbLinkDown = FALSE;
            Context->UsbLinkUp = FALSE;
            break;

        case UsbMsgBusEventSpeed:

            LOG_TRACE("Speed:%d", Payload.ubs);
            GetConfigDescriptorByBusSpeed(Payload.ubs, Context->EemMode, &UsbDeviceDesc, &UsbConfigDesc);
            UpdateUsbDescriptors(Context, Payload.ubs);
            Context->BusSpeed = Payload.ubs;
            Context->DeviceSpeed = GetDeviceSpeedByBusSpeed(Context->BusSpeed);
            break;

        case UsbMsgBusEventLinkDown:
            LOG_TRACE("USB-Link-Status Cable Disconnect");
            Context->UsbLinkDown = TRUE;
            Context->UsbLinkUp = FALSE;
            break;

        case UsbMsgBusEventLinkUp:
            LOG_TRACE("USB-Link-Status Cable On");
            Context->UsbLinkUp = TRUE;
            break;

        case UsbMsgNone:
            break;
    }

    // Tell back if there wasn't any event....
    if (Idle != NULL) *Idle = (Message == UsbMsgNone);

    if (((Message != UsbMsgNone) && (Context->KdNetData != NULL))) {
        CheckRestartUsbController(Context, InitialConnectStatus);
    }

EventHandlerEnd:
    return Status;
}

//------------------------------------------------------------------------------

NTSTATUS
ProcessEvents(
    _In_ PUSBFNDBG_CONTEXT Context)
{
    NTSTATUS Status;
    ULONG Counter;
    BOOLEAN Idle;

    // Process events
    for (Counter = 0; Counter < EVENT_COUNTER_MAX; Counter++) {
        Status = EventHandler(Context, FALSE, &Idle);
        if (Idle || !NT_SUCCESS(Status)) break;
    }

    return Status;
}

//------------------------------------------------------------------------------

void HandleZeroLengthPacket(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSBFNDBG_TRANSFER Transfer,
    _In_ ULONG TransferTxIndex,
    _In_ UINT8 EndPointType,
    _In_ ULONG EndPoint
    )
{

    if (IsRequiredZeroLengthPacket(Context, Transfer, EndPointType) != FALSE) {
        TransferTx(Context, EndPoint, TransferTxIndex, 0);
    }

    return;
}

//------------------------------------------------------------------------------

VOID HandleTransferForSSBurstMode(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSBFNDBG_TRANSFER Transfer,
    _In_ ULONG TransferFree,
    _In_ ULONG TransferIndex)
{

    NT_ASSERT(Context->IsBurstTransferMode != FALSE);

    if (IS_ALLOWED_NEW_PACKET_IN_TRANSFER(Transfer->RefPos, Transfer->RefCount, TransferFree, Context->QueuedTx)) {
        Transfer->RefCount--;

    } else {
         
        //
        //  The packet cannot be added to the current buffer entry,
        //  so post the transfer with the current buffer.
        //

        if (Transfer->RefCount > 0) {
            Transfer->RefCount -= Transfer->RefCount;
        }
         
        TransferTx(Context,
                   BULKIN_ENDPT,
                   TransferIndex,
                   Transfer->Length);
        Context->EemTx = INVALID_INDEX;

        //
        //  Check if we need to send a Zero-length packet
        //

        HandleZeroLengthPacket(Context, Transfer, TransferIndex, USB_ENDPOINT_TYPE_BULK, BULKIN_ENDPT);
    }
    
    return;
}

//------------------------------------------------------------------------------

VOID AddTransportProtocolHeader(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSBFNDBG_TRANSFER Transfer,
    _In_ ULONG TransferPos,
    _Inout_ PULONG Length)
{

    PUCHAR Data;

    Data = Transfer->Ref[TransferPos].Packet;
    if (Context->EemMode) {
        Data -= EEM_HEADER_LEN;
        Data[0] = (UCHAR)((*Length + EEM_CRC_LEN) >> 8);
        Data[1] = (UCHAR)((*Length + EEM_CRC_LEN) >> 0);
        Data[2 + *Length] = 0xDE;
        Data[3 + *Length] = 0xAD;
        Data[4 + *Length] = 0xBE;
        Data[5 + *Length] = 0xEF;
        *Length += EEM_HEADER_LEN + EEM_CRC_LEN;
    }

    return;
}

//------------------------------------------------------------------------------

VOID TransferTxIfNoFreeSpace(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSBFNDBG_TRANSFER Transfer,
    _In_ ULONG TransferFree,
    _Inout_ PULONG TransferIndex)
{

    if (Context->IsBurstTransferMode == FALSE) {

        //
        //  Check if there is space for one more Tx packet when legacy HS session
        //

        if (!Context->EemMode || (Context->QueuedTx < EEM_TX_QUEUE_LIMIT) ||
            CHECK_TRANSFER_SPACE_FOR_TX(Transfer->RefCount, TransferFree, Transfer->RefPos)) {    
            if (--Transfer->RefCount == 0) {
                TransferTx(Context,
                           BULKIN_ENDPT,
                           *TransferIndex,
                           Transfer->Length);
            }
            *TransferIndex = Context->EemTx = INVALID_INDEX;
        }

    } else {

        //
        // Check if there is space for one more Tx packet on SS-EEM-Async session,
        // if yes, then mark this USB Transfer to be sent next before using
        // the next Tx allocated packet.
        // if no, then let the current Tx packet to be used.
        //

        if (CHECK_TRANSFER_SPACE_FOR_TX(Transfer->RefCount, TransferFree, Transfer->RefPos + 1)) {
            if (Transfer->RefCount > 0) {
                Transfer->RefCount -= Transfer->RefCount;
                TransferTx(Context,
                           BULKIN_ENDPT,
                           *TransferIndex,
                           Transfer->Length);
            }
            *TransferIndex = Context->EemTx = INVALID_INDEX;
        }
    }

    return;
}

//------------------------------------------------------------------------------

VOID InitializeTransferHandle(
    _Inout_ PUSBFNDBG_CONTEXT Context,
    _Inout_ PULONG Handle,
    _Out_ PULONG TransferIndex,
    _Out_ PBOOLEAN AsyncHandle,
    _Out_ PULONG TransferPos)
{

    //
    //  Extract the Async flag, the encoded transfer buffer index, and buffer position
    //

    *AsyncHandle = SET_ASYNC_FLAG(*Handle);
    Context->IsBurstTransferMode = SET_IF_BURST_TRANSFER_MODE(*AsyncHandle, Context->BusSpeed, Context->EemMode) &&
                                   ((*Handle & TRANSMIT_LAST) == 0);
    *Handle &= ~(HANDLE_FLAGS);
    *TransferPos = *Handle >> 16;
    *TransferIndex = *Handle & 0xFFFF;
    return;
}

//------------------------------------------------------------------------------

NTSTATUS WaitForTxPacketToBeSent(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ ULONG TransferIndex)
{

    NTSTATUS Status;
    ULONG Retry;

    Status = STATUS_SUCCESS;

    //
    // For kernel transport packets we have to wait for send happen.
    // It is needed because kernel debugger depends on ACK packet in
    // some situations (like reboot) and hangs when it isn't received.
    // The Asynchronous flag was always FALSE since it was cleared by the Proxy
    // layer (in the legacy code), but the performance changes for the Proxy layer no longer clears
    // this flag, so by checking for IsBurstTransferMode, then we ensure to eliminate
    // any regression when there is EEM-HS ongoing connection, and enable only
    // asynchronous mode for EEM-SS connections.
    //

    if ((Context->Configured != FALSE) && (Context->IsBurstTransferMode == FALSE)) {

        for (Retry = 0; Retry < TX_SEND_RETRY; Retry++) {
            Status = ProcessEvents(Context);
            if (!NT_SUCCESS(Status)) {
                break;
            }

            if (IsTransferDoneTx(Context, TransferIndex)) {
                break;
            }

            KeStallExecutionProcessor(TX_SEND_STALL);
        }
    }

    return Status;
}

//------------------------------------------------------------------------------

NTSTATUS SendTxPacket(
    _In_ PUSBFNDBG_CONTEXT Context,
    _In_ PUSBFNDBG_TRANSFER Transfer,
    _In_ ULONG TransferIndex,
    _In_ BOOLEAN AsyncHandle)
{

    NTSTATUS Status;
    ULONG TransferFree;

    Status = STATUS_SUCCESS;

    //
    //  Is this an EEM Tx packet?
    //

    if (Context->EemTx == TransferIndex) {
        TransferFree = Context->MaxTransferSize - Transfer->Length;

        //
        // Is there an ongoing Asynchronous-Burst Transfer?
        //

        if (Context->IsBurstTransferMode != FALSE) {

            //
            //  Handle the transfer by adding the current packet to the DMA buffer, 
            //  or post the entire buffer entry, if the current packet cannot be 
            //  appended to the current buffer.
            //

            HandleTransferForSSBurstMode(Context, Transfer, TransferFree, TransferIndex);
            goto SendTxPacketEnd;
        }

        if (!Context->EemMode || !AsyncHandle ||
                (Context->QueuedTx < EEM_TX_QUEUE_LIMIT) ||
                (Transfer->RefPos >= REF_PER_TRANSFER) ||
                (TransferFree < EEM_MAX_PACKET_SIZE)) {

            Transfer->RefCount--;
            Context->EemTx = INVALID_INDEX;
         }
    }

    if (--Transfer->RefCount == 0) {

        //
        //  It's not allowed to hold transfers when ASYNC is not enabled.
        //  so the RefCount condition would be always equal 0 for kernel 
        //  debugger packets without ASYNC flag.
        //

        TransferTx(Context,
                   BULKIN_ENDPT,
                   TransferIndex,
                   Transfer->Length);

        //
        //  Check if we need to send a Zero-length packet
        //

        HandleZeroLengthPacket(Context, Transfer, TransferIndex, USB_ENDPOINT_TYPE_BULK, BULKIN_ENDPT);
    }

    //
    // For kernel transport packets (without ASYNC flag),
    // There is a need to wait for the TX packet to be transmitted
    // and wait for the completion notification from the miniport layer.
    //

    Status = WaitForTxPacketToBeSent(Context, TransferIndex);

SendTxPacketEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
KdUsbFnInitializeLibrary (
    PKDNET_EXTENSIBILITY_IMPORTS ImportTable,
    PCHAR LoaderOptions,
    PDEBUG_DEVICE_DESCRIPTOR Device)
{
    NTSTATUS Status;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    ULONG MiniportContextLength;
    ULONG DmaLength;
    ULONG DmaAlignment;
    PCHAR Option;

    // Initialize miniport
    Status = UsbFnMpInitializeLibrary(ImportTable,
                                      LoaderOptions,
                                      Device,
                                      &MiniportContextLength,
                                      &DmaLength,
                                      &DmaAlignment);
    if (!NT_SUCCESS(Status)) goto KdUsbFnInitializeLibraryEnd;

    // Initialize memory requirements
    Device->Memory.Cached = FALSE;
    Device->Memory.Aligned = TRUE;
    Device->Memory.Length = DmaLength + MiniportContextLength;

    //
    // Initialize timeouts
    //

    SetAttachConnectTimeouts(Device);

    // Find protocol option
    while (LoaderOptions != NULL) {

        //
        // Default behavior is to run EEM with DHCP allowed which means use of
        // DHCP will be controlled by the dhcp BCD setting in dbgsettings.
        //

        Context->DiscoveryEnabled = FALSE;
        Context->EemMode = TRUE;
        Context->NoDhcp = FALSE;
        Context->DiscoveryDelay = DISCOVERY_DELAY;

        //
        // If loadoptions VIRTETH set, then force virteth and allow DHCP.
        // Intentionally allow both comma and space separated options, since
        // winload will replace commas with spaces for kernel, but that
        // replacement may not happen for boot debugging.
        //

        Option = strstr(LoaderOptions, VIRTETH_OPTION);
        if ((Option != NULL) && (
                (Option[sizeof(VIRTETH_OPTION) - 1] == '\0') ||
                (Option[sizeof(VIRTETH_OPTION) - 1] == ',') ||
                (Option[sizeof(VIRTETH_OPTION) - 1] == ' '))) {

            Context->DiscoveryEnabled = FALSE;
            Context->EemMode = FALSE;
            Context->NoDhcp = FALSE;
            break;
        }

        //
        // If loadoptions EEM set, then force EEM and force DHCP off.
        // Intentionally allow both comma and space separated options, since
        // winload will replace commas with spaces for kernel, but that
        // replacement may not happen for boot debugging.
        //

        Option = strstr(LoaderOptions, EEM_OPTION);
        if ((Option != NULL) && (
                (Option[sizeof(EEM_OPTION) - 1] == '\0') ||
                (Option[sizeof(EEM_OPTION) - 1] == ',') ||
                (Option[sizeof(EEM_OPTION) - 1] == ' '))) {

            Context->DiscoveryEnabled = FALSE;
            Context->EemMode = TRUE;
            Context->NoDhcp = TRUE;
            break;
        }

        break;
    }

    Context->InitialMode.DiscoveryEnabled = Context->DiscoveryEnabled;
    Context->InitialMode.EemMode = Context->EemMode;
    Context->InitialMode.NoDhcp = Context->NoDhcp;
    Context->InitialMode.DiscoveryDelay = Context->DiscoveryDelay;
    Status = STATUS_SUCCESS;

KdUsbFnInitializeLibraryEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
ULONG
KdUsbFnGetHardwareContextSize (
    PDEBUG_DEVICE_DESCRIPTOR Device)
{
    return UsbFnMpGetHardwareContextSize(Device);
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
KdUsbFnInitializeController(
    PKDNET_SHARED_DATA KdNet)
{
    NTSTATUS Status;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    PUINT8 Buffer;
    ULONG MiniportContextLength, DmaLength, DmaAlignment;
    BOOLEAN Idle;
#ifdef SOFIA_LTE
    ULONG PageCount;
    PVOID VirtualAddress;

    //
    // SoFIA LTE Workaround: 
    // 1.) Map Scratch Buffer in Uncached Memory:
    // Initial Scratch Buffer is located in Cached memory.  SoFIA requires this 
    // Scratch Buffer to be in Non-Cached memory.  Create new Scratch Buffer mapping
    // using Non-Cached API and update Memory structure.
    //
    // Note: Assumption coming in is that initial Scratch Buffer allocation succeeded
    // Note: Scratch Buffer Length is 135 Pages.
    //
    LOG_TRACE("Sofia Memory Mapping");
    PageCount = BYTES_TO_PAGES(KdNet->Device->Memory.Length);

    VirtualAddress = KdMapPhysicalMemory64(
                        KdNet->Device->Memory.Start, 
                        PageCount,
                        TRUE);
    KdNet->Device->Memory.VirtualAddress = VirtualAddress;
    KdNet->Hardware = VirtualAddress;
#endif

    //
    // On resume from hibernate and bugchecks, properly re-initialize the
    // context.
    //

    if ((Context->Initialized != FALSE) && (Context->Running != FALSE) &&
        (Context->Miniport != NULL) && (*(PVOID *)Context->Miniport == NULL)) {

        Context->Initialized = FALSE;
        Context->DiscoveryEnabled = Context->InitialMode.DiscoveryEnabled;
        Context->EemMode = Context->InitialMode.EemMode;
        Context->NoDhcp = Context->InitialMode.NoDhcp;
        Context->DiscoveryDelay = Context->InitialMode.DiscoveryDelay;
        UsbFnHibernateDectected += 1;
    }

    Context->KdNetData = KdNet;
    // This function is called by KdNet multiple times, there are some
    // actions need to be done only on first call (like remember MAC address
    // and miniport call functions).
    if (Context->Initialized) {
        RtlCopyMemory(KdNet->TargetMacAddress, Context->Mac, 6);
    } else {

        // Call initialize library to obtain DmaLenght
        Status = UsbFnMpInitializeLibrary(NULL,
                                          NULL,
                                          KdNet->Device,
                                          &MiniportContextLength,
                                          &DmaLength,
                                          &DmaAlignment);
        Context->Initialized = TRUE;
        Context->Running = FALSE;
        Context->Attached = FALSE;
        Context->Configured = FALSE;
        Context->Connected = FALSE;
        Context->IsRestartDone = FALSE;
        Context->UsbLinkDown = FALSE;
        Context->UsbLinkUp = FALSE;
        Context->IsBurstTransferMode = FALSE;

        Context->DiscoveryInReset = FALSE;

        if (Context->EemMode) {
            GetConfigDescriptorByBusSpeed(Context->BusSpeed, Context->EemMode, &UsbDeviceDesc, &UsbConfigDesc);
            Product = &ProductEem;

        } else {
            UsbDeviceDesc = UsbVeDeviceDesc;
            UsbConfigDesc = UsbVeConfigDesc;
            Product = &ProductVe;
        }

        Context->DmaLength = DmaLength;
        Context->MiniportLength = MiniportContextLength;

        // Calculate and remember miniport context
        Buffer = KdNet->Hardware;
        Context->Miniport = &Buffer[Context->DmaLength];

        // Use very specific MAC addresses
        KdNet->TargetMacAddress[0] = 0x00;
        KdNet->TargetMacAddress[1] = 0x11;
        RtlCopyMemory(Context->Mac, KdNet->TargetMacAddress, 6);
    }

    // Set default line coding (it is obsolete now, but just in case)
    Context->LineCoding.Rate = 0x9600;
    Context->LineCoding.DataBits = 8;
    Context->LineCoding.StopBits = 0;
    Context->LineCoding.Parity = 0;
    Context->DeviceStatus = 0x0001;

    // KdNet can call this function second time without
    // KdUsbFnShutdownController call from KdD0Transition. We don't want
    // reinitialize controller which will result broken KDNet connection...
    if (Context->Running) {
        Status = STATUS_SUCCESS;
        goto KdUsbFnInitializeControllerEnd;
    }

    // Start controller
    Status = StartController(Context, KdNet->Device, KdNet->Hardware);
    if (!NT_SUCCESS(Status)) goto KdUsbFnInitializeControllerEnd;

    // Process pending events
    for (;;) {
        Status = EventHandler(Context, FALSE, &Idle);
        if (!NT_SUCCESS(Status)) {
            UsbFnMpStopController(Context->Miniport);
            goto KdUsbFnInitializeControllerEnd;
        }
        if (Idle) break;
    }

    // Connection is done...
    switch (Context->BusSpeed) {
        case UsbBusSpeedUnknown:
        case UsbBusSpeedLow:
        case UsbBusSpeedFull:
        default:
            Context->LinkSpeed = KdNet->LinkSpeed = 10;
            Context->FullDuplex = KdNet->LinkDuplex = FALSE;
            break;
        case UsbBusSpeedHigh:
        case UsbBusSpeedSuper:
            Context->LinkSpeed = KdNet->LinkSpeed = 100;
            Context->FullDuplex = KdNet->LinkDuplex = TRUE;
            break;
    }

    if (KdNet->LinkState != NULL) *(KdNet->LinkState) = TRUE;

    //
    //  Wait for the USB link 'Connected' status before returning.
    //

    Status = KdUsbFnWaitForConnection(Context);
    if (NT_SUCCESS(Status)) {
        if (Context->EemMode != FALSE) {
            KdNet->Flags |= KDX_EXTENDED_INITIAL_CONNECT;

        } else {
            KdNet->Flags &= ~KDX_EXTENDED_INITIAL_CONNECT;
        }

        if (Context->NoDhcp != FALSE) {
            KdNet->Flags |= KDX_FORCE_DHCP_OFF;

        } else {
            KdNet->Flags &= ~KDX_FORCE_DHCP_OFF;
        }
    }

    //
    //  Ensure to clear the restart flag, otherwise we can enter into
    //  a loop trying to do the sequence ShutdownController-InitializeController 
    //  between kdnet and kdnet extensibility module.
    //

    KdNet->RestartKdnet = FALSE;

KdUsbFnInitializeControllerEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
VOID
KdUsbFnShutdownController(
    PVOID Buffer)
{
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    NTSTATUS Status;
    ULONG Base, Delta;
    BOOLEAN Idle;


    UNREFERENCED_PARAMETER(Buffer);

    // If there is active pending send wait until they are done. This
    // helps avoid kernel debugger hang in situations when device is
    // powered off.

    UsbFnMpTimeDelay(Context->Miniport, NULL, &Base);
    while (Context->QueuedTx > 0) {
        Status = EventHandler(Context, FALSE, &Idle);
        UsbFnMpTimeDelay(Context->Miniport, &Delta, &Base);
        if (!NT_SUCCESS(Status) || (Delta > SEND_LOOP_TIMEOUT)) break;
    }

    StopController(Context);

    if (Context->IsRestartDone != FALSE) {
        LOG_TRACE("USB Restart done");
        Context->IsRestartDone = FALSE;
        Context->Initialized = FALSE;
        Context->DiscoveryEnabled = Context->InitialMode.DiscoveryEnabled;
        Context->EemMode = Context->InitialMode.EemMode;
        Context->DiscoveryDelay = Context->InitialMode.DiscoveryDelay;
        Context->IsBurstTransferMode = FALSE;
    }
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
KdUsbFnGetTxPacket(
    PVOID Buffer,
    PULONG Handle)
{
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    ULONG RetryCount;
    NTSTATUS Status;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG TransferIndex;
    ULONG TransferFree;
    ULONG TransferPos;

    UNREFERENCED_PARAMETER(Buffer);

    for (RetryCount = 0; RetryCount < 1; RetryCount++) {

        // Process events
        Status = ProcessEvents(Context);
        if (!NT_SUCCESS(Status)) goto KdUsbFnGetTxPacketEnd;

        TransferIndex = Context->EemTx;
        if (TransferIndex != INVALID_INDEX) {
            Transfer = &Context->Transfer[TransferIndex];
            TransferFree = Context->MaxTransferSize - Transfer->Length;

            //
            //  Check if there is space in the current Transfer buffer 
            //  for the next Tx packet.
            //

            TransferTxIfNoFreeSpace(Context, Transfer, TransferFree, &TransferIndex);

        }

        // If there isn't active transfer allocate one
        if (TransferIndex == INVALID_INDEX) {

            // Try get free Tx buffer. If there isn't any we will wait
            // and try again until we run out of retry attempts
            TransferIndex = DoneTransferTx(Context, BULKIN_ENDPT);
            if (TransferIndex == INVALID_INDEX) {
                Status = STATUS_IO_TIMEOUT;
                KeStallExecutionProcessor(10);
                continue;
            }

            Transfer = &Context->Transfer[TransferIndex];
            Transfer->RefCount = 1;
            Transfer->RefPos = 0;
            Transfer->Length = 0;
            Context->EemTx = TransferIndex;
        } else {
            Transfer = &Context->Transfer[TransferIndex];
        }

        TransferPos = Transfer->RefPos;
        Transfer->Ref[TransferPos].Packet = &Transfer->Buffer[Transfer->Length];
        Transfer->Ref[TransferPos].Length = MAX_PACKET_SIZE;
        if (Context->EemMode) {
            Transfer->Ref[TransferPos].Packet += EEM_HEADER_LEN;
        }

        if (++Transfer->RefPos >= REF_PER_TRANSFER) {
            Transfer->RefPos = 0;
        }

        Transfer->RefCount++;
        if ((TransferIndex >= MAX_TRANSFERS) || 
                (TransferPos >= REF_PER_TRANSFER)) {

            KeBugCheckEx(THREAD_STUCK_IN_DEVICE_DRIVER, 1, 1, 0, 0);
            *Handle = 0;
            goto KdUsbFnGetTxPacketEnd;
        }

        Context->TxPacketFailed = FALSE;
        *Handle = (TransferPos << 16) | TransferIndex;
        Status = STATUS_SUCCESS;
        break;
    }

KdUsbFnGetTxPacketEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
KdUsbFnSendTxPacket(
    PVOID Buffer,
    ULONG Handle,
    ULONG Length)
{
    NTSTATUS Status;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    BOOLEAN AsyncHandle;
    ULONG TransferIndex;
    ULONG TransferPos;
    PUSBFNDBG_TRANSFER Transfer;

    UNREFERENCED_PARAMETER(Buffer);

    //
    //  Process any pending events
    //

    Status = ProcessEvents(Context);
    if (!NT_SUCCESS(Status)) {
        goto KdSendTxPacketEnd;
    }

    //
    //  Extract the buffer index and buffer position from the passed-in Handle
    //  and initialize ASYNC flags.
    //

    InitializeTransferHandle(Context, &Handle, &TransferIndex, &AsyncHandle, &TransferPos);

    //
    //  Validate that the buffer index is not going beyond the maximum number of allowed entries.
    //

    if ((TransferIndex >= MAX_TRANSFERS) || (TransferPos >= REF_PER_TRANSFER)) {
        KeBugCheckEx(THREAD_STUCK_IN_DEVICE_DRIVER, 1, 2, 0, 0);
        goto KdSendTxPacketEnd;
    }

    //
    //  Add the corresponding Transport Header. 
    //  For EEM: Length + <payload> + EEM checksum (0xdeadbeef)
    //  For virteth: there is no header
    //

    Transfer = &Context->Transfer[TransferIndex];
    AddTransportProtocolHeader(Context, Transfer, TransferPos, &Length);

    //
    //  Update the Transfer length with the updated protocol header length
    //

    Transfer->Length += Length;

    //
    //  Post the Transfer if there is a ready packet to be transferred.
    //  In the ASYNC case (send the packet without waiting for the
    //  transmission to be completed), the packet won't be sent unless
    //  there is no room in the current transfer buffer entry, or 
    //  transfer buffer position reached the maximum allowed packet 
    //  to be hold in the out pipe. 
    //

    Status = SendTxPacket(Context, Transfer, TransferIndex, AsyncHandle);

KdSendTxPacketEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
NTSTATUS
KdUsbFnGetRxPacket(
    PVOID Buffer,
    PULONG Handle,
    PVOID *Packet,
    PULONG Length)
{
    NTSTATUS Status;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    ULONG TransferIndex;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG TransferLength;
    ULONG BufferLength;
    UINT16 EemHeader;
    ULONG EemLength;


    UNREFERENCED_PARAMETER(Buffer);

    // Process events
    Status = ProcessEvents(Context);
    if (!NT_SUCCESS(Status)) {
        goto KdUsbFnGetRxPacketEnd;
    }

    // When disconnected there be no packet
    if (!Context->Connected) {
        Status = STATUS_IO_TIMEOUT;
        goto KdUsbFnGetRxPacketEnd;
    }

    //
    //  Ensure any packets still pending in the transmit queue are sent. 
    //  Packets sent with the TRANSMIT_ASYNC handle flag, may still be pending as 
    //  they are not guaranteed to be immediately transmitted in KdUsbFnSendTxPacket
    //

    if ((Context->EemMode && (Context->QueuedTx < EEM_TX_QUEUE_LIMIT)) ||
        (Context->IsBurstTransferMode != FALSE)) {
        TransferIndex = Context->EemTx;
        if (TransferIndex != INVALID_INDEX) {
            Transfer = &Context->Transfer[TransferIndex];
            TransferTx(Context, BULKIN_ENDPT, TransferIndex, Transfer->Length);
            Context->EemTx = INVALID_INDEX;
        }
    }

    // Virtual Ethernet mode is simple
    if (!Context->EemMode) {

        TransferIndex = DoneTransferRx(Context, BULKOUT_ENDPT);
        if (TransferIndex == INVALID_INDEX) {
            Status = STATUS_IO_TIMEOUT;
            goto KdUsbFnGetRxPacketEnd;
        }

        Transfer = &Context->Transfer[TransferIndex];

        *Handle = TransferIndex;
        *Packet = Transfer->Buffer;
        *Length = Transfer->Length;

        Transfer->RefPos = 0;
        Transfer->RefCount = 1;
        Transfer->Ref[Transfer->RefPos].Packet = *Packet;
        Transfer->Ref[Transfer->RefPos].Length = *Length;

        Status = STATUS_SUCCESS;
        goto KdUsbFnGetRxPacketEnd;
    }

    // Get active transfer to work with
    TransferIndex = Context->EemRx;
    if (TransferIndex == INVALID_INDEX) {

        TransferIndex = DoneTransferRx(Context, BULKOUT_ENDPT);
        if (TransferIndex == INVALID_INDEX) {
            Status = STATUS_IO_TIMEOUT;
            goto KdUsbFnGetRxPacketEnd;
        }

        // If this is zero length transfer drop it
        Transfer = &Context->Transfer[TransferIndex];
        if (Transfer->Length == 0) {
            TransferRx(Context,
                       BULKOUT_ENDPT,
                       TransferIndex,
                       Context->MaxTransferSize);

            Status = STATUS_IO_TIMEOUT;
            goto KdUsbFnGetRxPacketEnd;
        }

        // We have new tranfer
        Context->EemRx = TransferIndex;
        Context->EemRxPos = 0;
        // We are holding reference in EemRx
        Transfer->RefCount = 1;
    }
    else {
        Transfer = &Context->Transfer[TransferIndex];
    }

    BufferLength = Context->RxBufferLength;
    TransferLength = Transfer->Length - Context->EemRxPos;

    if (!Context->RxBufferBusy && (BufferLength > 0)) {
        // Header is in RxBuffer or in RxBuffer and new transfer
        EemHeader = Context->RxBuffer[1];
        if (BufferLength > 1) {
            EemHeader |= Context->RxBuffer[0] << 8;
        }
        else {
            EemHeader |= Transfer->Buffer[0] << 8;
        }
    }
    else if (TransferLength < EEM_HEADER_LEN) {

        // This is error if transfer was shorter than maximal size
        if (Transfer->Length >= Context->MaxTransferSize) {

            // We must wait for RxBuffer
            if (Context->RxBufferBusy) {
                Status = STATUS_IO_TIMEOUT;
                goto KdUsbFnGetRxPacketEnd;
            }

            // Copy transfer to buffer
            RtlCopyMemory(&Context->RxBuffer[Context->RxBufferLength],
                          &Transfer->Buffer[Context->EemRxPos],
                          TransferLength);

            Context->RxBufferLength += TransferLength;
        }

        Context->EemRx = INVALID_INDEX;
        Context->EemRxPos = 0;

        if (--Transfer->RefCount == 0) {
            Transfer->RefPos = 0;
            TransferRx(Context,
                       BULKOUT_ENDPT,
                       TransferIndex,
                       Context->MaxTransferSize);
        }

        Status = STATUS_IO_TIMEOUT;
        goto KdUsbFnGetRxPacketEnd;
    }
    else {
        EemHeader = Transfer->Buffer[Context->EemRxPos + 1];
        EemHeader |= Transfer->Buffer[Context->EemRxPos] << 8;
    }

    // Get total EEM packet length
    EemLength = EemLengthFromHeader(EemHeader);

    // We have to use RxBuffer if we don't have full packet
    if (EemLength > (BufferLength + TransferLength)) {

        // This is error if transfer was shorter than maximal size
        if (Transfer->Length >= Context->MaxTransferSize) {

            // We must wait for RxBuffer when we don't
            if (Context->RxBufferBusy) {
                Status = STATUS_IO_TIMEOUT;
                goto KdUsbFnGetRxPacketEnd;
            }

            // Copy transfer data to buffer and release it
            RtlCopyMemory(&Context->RxBuffer[BufferLength],
                          &Transfer->Buffer[Context->EemRxPos],
                          TransferLength);

            Context->RxBufferLength += TransferLength;
        }

        Context->EemRx = INVALID_INDEX;
        Context->EemRxPos = 0;

        if (--Transfer->RefCount == 0) {
            Transfer->RefPos = 0;
            TransferRx(Context,
                       BULKOUT_ENDPT,
                       TransferIndex,
                       Context->MaxTransferSize);
        }

        Status = STATUS_IO_TIMEOUT;
        goto KdUsbFnGetRxPacketEnd;
    }

    // If packet is partialy in buffer we have use it for packet
    if (BufferLength > 0) {

        RtlCopyMemory(&Context->RxBuffer[BufferLength],
                      &Transfer->Buffer[Context->EemRxPos],
                      EemLength - BufferLength);

        Context->EemRxPos += EemLength - BufferLength;

        // Check if we get valid packet
        if (!EemValidPacket(Context->RxBuffer, EemLength)) {
            Context->RxBufferLength = 0;

            Status = STATUS_IO_TIMEOUT;
            goto KdUsbFnGetRxPacketEnd;
        }

        Context->RxBufferLength = EemLength;
        Context->RxBufferBusy = TRUE;

        *Handle = RX_BUFFER_HANDLE;
        *Packet = &Context->RxBuffer[EEM_HEADER_LEN];
        *Length = Context->RxBufferLength - EEM_HEADER_LEN - EEM_CRC_LEN;

        Status = STATUS_SUCCESS;
        goto KdUsbFnGetRxPacketEnd;
    }

    // We need empty reference position in transfer
    if (Transfer->RefCount >= REF_PER_TRANSFER) {
        Status = STATUS_IO_TIMEOUT;
        goto KdUsbFnGetRxPacketEnd;
    }

    // Drop packet when it isn't valid
    if (!EemValidPacket(&Transfer->Buffer[Context->EemRxPos], EemLength)) {

        Context->EemRxPos += EemLength;
        if (Context->EemRxPos >= Transfer->Length) {
            Context->EemRx = INVALID_INDEX;
            Context->EemRxPos = 0;

            Transfer->RefCount--;
            if (Transfer->RefCount == 0) {
                Transfer->RefPos = 0;
                TransferRx(Context,
                           BULKOUT_ENDPT,
                           TransferIndex,
                           Context->MaxTransferSize);
            }
        }

        Status = STATUS_IO_TIMEOUT;
        goto KdUsbFnGetRxPacketEnd;
    }

    *Handle = TransferIndex | (Transfer->RefPos << 16);
    *Packet = &Transfer->Buffer[Context->EemRxPos + EEM_HEADER_LEN];
    *Length = EemLength - EEM_HEADER_LEN - EEM_CRC_LEN;

    Transfer->Ref[Transfer->RefPos].Packet = *Packet;
    Transfer->Ref[Transfer->RefPos].Length = *Length;

    Transfer->RefCount++;
    if (++Transfer->RefPos >= REF_PER_TRANSFER) {
        Transfer->RefPos = 0;
    }

    Context->EemRxPos += EemLength;
    if (Context->EemRxPos >= Transfer->Length) {
        Context->EemRx = INVALID_INDEX;
        Context->EemRxPos = 0;
        --Transfer->RefCount;
    }

    Status = STATUS_SUCCESS;

KdUsbFnGetRxPacketEnd:
    return Status;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
VOID
KdUsbFnReleaseRxPacket(
    PVOID Buffer,
    ULONG Handle)
{
    NTSTATUS Status;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    PUSBFNDBG_TRANSFER Transfer;

    UNREFERENCED_PARAMETER(Buffer);

    // Process events
    Status = ProcessEvents(Context);
    if (!NT_SUCCESS(Status)) {
        goto KdUsbFnReleaseRxPacketEnd;
    }

    if (Handle == RX_BUFFER_HANDLE) {
        Context->RxBufferLength = 0;
        Context->RxBufferBusy = FALSE;
    } else {

        Handle &= 0xFFFF;
        if (Handle >= MAX_TRANSFERS) {
            KeBugCheckEx(THREAD_STUCK_IN_DEVICE_DRIVER, 1, 3, 0, 0);
            goto KdUsbFnReleaseRxPacketEnd;
        }

        Transfer = &Context->Transfer[Handle];
        if (--Transfer->RefCount == 0) {
            TransferRx(Context,
                       BULKOUT_ENDPT,
                       Handle,
                       Context->MaxTransferSize);
        }
    }

KdUsbFnReleaseRxPacketEnd:
    return;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
PVOID
KdUsbFnGetPacketAddress(
    PVOID Buffer,
    ULONG Handle)
{
    PVOID Address = NULL;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG RefPos;

    UNREFERENCED_PARAMETER(Buffer);
    Handle &= ~(HANDLE_FLAGS);
    if (Handle == RX_BUFFER_HANDLE) {
        Address = &Context->RxBuffer[EEM_HEADER_LEN];

    } else {
        RefPos = Handle >> 16;
        Handle &= 0xFFFF;
        if ((Handle >= MAX_TRANSFERS) || (RefPos >= REF_PER_TRANSFER)) {
            KeBugCheckEx(THREAD_STUCK_IN_DEVICE_DRIVER, 1, 4, 0, 0);
            goto KdUsbFnGetPacketAddressEnd;
        }

        Transfer = &Context->Transfer[Handle];
        Address = Transfer->Ref[RefPos].Packet;
    }

KdUsbFnGetPacketAddressEnd:
    return Address;
}

//------------------------------------------------------------------------------

_Use_decl_annotations_
ULONG
KdUsbFnGetPacketLength(
    PVOID Buffer,
    ULONG Handle)
{
    ULONG Length = 0;
    PUSBFNDBG_CONTEXT Context = &UsbFnDbgContext;
    PUSBFNDBG_TRANSFER Transfer;
    ULONG RefPos;

    UNREFERENCED_PARAMETER(Buffer);
    Handle &= ~(HANDLE_FLAGS);
    if (Handle == RX_BUFFER_HANDLE) {
        Length = Context->RxBufferLength - EEM_HEADER_LEN - EEM_CRC_LEN;

    } else {
        RefPos = Handle >> 16;
        Handle &= 0xFFFF;
        if ((Handle >= MAX_TRANSFERS) || (RefPos >= REF_PER_TRANSFER)) {
            KeBugCheckEx(THREAD_STUCK_IN_DEVICE_DRIVER, 1, 5, 0, 0);
            goto KdUsbFnGetPacketLengthEnd;
        }

        Transfer = &Context->Transfer[Handle];
        Length = Transfer->Ref[RefPos].Length;
    }

KdUsbFnGetPacketLengthEnd:
    return Length;
}

//------------------------------------------------------------------------------

