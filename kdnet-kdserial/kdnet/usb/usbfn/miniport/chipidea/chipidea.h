#include <pshpack1.h>

//------------------------------------------------------------------------------

typedef struct __CHIPIDEA_USB_CTRL {

    UINT32 ID;                      // 0000
    UINT32 HWGENERAL;               // 0004
    UINT32 HWHOST;                  // 0008 Host Hardware Parameters
    UINT32 HWDEVICE;                // 000C Device Hardware Parameters
    UINT32 HWTXBUF;                 // 0010 TX Buffer Hardware Parameters
    UINT32 HWRXBUF;                 // 0014 RX Buffer Hardware Parameters
    UINT32 RESERVED_0018[26];
    UINT32 GPTIMER0LD;              // 0080 General Purpose Timer #0 Load
    UINT32 GPTIMER0CTRL;            // 0084 General Purpose Timer #0 Control
    UINT32 GPTIMER1LD;              // 0088 General Purpose Timer #1 Load
    UINT32 GPTIMER1CTRL;            // 008C General Purpose Timer #1 Control
    UINT32 SBUSCFG;                 // 0090 Control for the System Bus Interface
    UINT32 RESERVED_0094[27];
    UINT8  CAPLENGTH;               // 0100 Capability Register Length
    UINT8  RESERVED_0101;
    UINT16 HCIVERSION;              // 0102 Host Interface Version Number
    UINT32 HCSPARAMS;               // 0104 Host Control Structural Parameters
    UINT32 HCCPARAMS;               // 0108 Host Control Capability Parameters
    UINT32 RESERVED_10C[5];
    UINT16 DCIVERSION;              // 0120 Device Interface Version Number
    UINT16 RESERVED_0122;
    UINT32 DCCPARAMS;               // 0124 Device Control CapabilityParameters
    UINT32 RESERVED_0128[6];
    UINT32 USBCMD;                  // 0140 USB Command
    UINT32 USBSTS;                  // 0144 USB Status
    UINT32 USBINTR;                 // 0148 USB Interrupt Enable
    UINT32 FRINDEX;                 // 014C USB Frame Index
    UINT32 CTRLDSSENGMENT;          // 0150 Not used in this implementation
    union {
        UINT32 PERIODICLISTBASE;    // 0154 Frame List Base Address
        UINT32 USBADR;              // 0154 USB Device Address
        };
    union {
        UINT32 ASYNCLISTADDR;       // 0158 Next Asynchronous List Address
        UINT32 ENDPOINTLISTADDR;    // 0158 Address at Endpoint list in memory
        };
    UINT32 ASYNCTTSTS;              // 015C Asynchronous Buffer Status For Embedded TT
    UINT32 BURSTSIZE;               // 0160 Programmable Burst Size
    UINT32 TXFILLTUNING;            // 0164 Host Transmit Pre-Buffer Packet Tuning
    UINT32 TXTTFILLTUNING;          // 0168 Host TT Transmit Pre-Buffer Packet Tuning
    UINT32 RESERVED_016C;
    UINT32 ULPI_VIEWPORT;           // 0170 ULPI Viewport
    UINT32 RESERVED_0174;
    UINT32 ENDPTNAK;                // 0178 Endpoint NAK
    UINT32 ENDPTNAKEN;              // 017c Endpoint NAK Enable
    UINT32 CONFIGFLAG;              // 0180
    UINT32 PORTSC[8];               // 0184
    UINT32 OTGSC;                   // 01A4 On-The-Go(OTG) Status and Control
    UINT32 USBMODE;                 // 01A8 USB Device Mode
    UINT32 ENDPTSETUPSTAT;          // 01AC Endpoint Setup Status
    UINT32 ENDPTPRIME;              // 01B0 Endpoint Initialization
    UINT32 ENDPTFLUSH;              // 01B4 Endpoint De-Initialize
    UINT32 ENDPTSTATUS;             // 01B8 Endpoint Status
    UINT32 ENDPTCOMPLETE;           // 01BC Endpoint Complete
    UINT32 ENDPTCTRL[16];           // 01C0 Endpoint Control
} CHIPIDEA_USB_CTRL, *PCHIPIDEA_USB_CTRL;

//------------------------------------------------------------------------------

#define USB_ID_MASK                 (0xFFFF << 0)
#define USB_ID_VALUE                0xFA05

#define USB_HWDEVICE_DC             (1 << 0)
#define USB_HWDEVICE_EP_MASK        (0x1F << 1)
#define USB_HWDEVICE_EP_SHIFT       1

#define USB_UTMI_CTRL_USBEN         (1 << 24)

#define USB_GPT_RUN                 (1UL << 31)
#define USB_GPT_RST                 (1UL << 30)
#define USB_GPT_REPEAT              (1UL << 24)
#define USB_GPT_MASK                (0x00FFFFFF)

#define USB_USBMODE_CM              (3 << 0)
#define USB_USBMODE_CM_DEVICE       (2 << 0)
#define USB_USBMODE_CM_HOST         (3 << 0)
#define USB_USBMODE_SLOM            (1 << 3)
#define USB_USBMODE_SDIS            (1 << 4)

#define USB_USBCMD_RS               (1 << 0)
#define USB_USBCMD_RST              (1 << 1)
#define USB_USBCMD_SUTW             (1 << 13)
#define USB_USBCMD_ATDTW            (1 << 14)

#define USB_PORTSC_PTS              (3 << 30)
#define USB_PORTSC_PTS_UTMI         (0 << 30)
#define USB_PORTSC_PTS_ULPI         (2 << 30)
#define USB_PORTSC_PTS_SERIAL       (3 << 30)
#define USB_PORTSC_PTW              (1 << 28)
#define USB_PORTSC_PSPD             (3 << 26)
#define USB_PORTSC_PSPD_FULL        (0 << 26)
#define USB_PORTSC_PSPD_LOW         (1 << 26)
#define USB_PORTSC_PSPD_HIGH        (2 << 26)
#define USB_PORTSC_WKOC             (1 << 22)
#define USB_PORTSC_WKDS             (1 << 21)
#define USB_PORTSC_WKCN             (1 << 20)
#define USB_PORTSC_PP               (1 << 12)
#define USB_PORTSC_PR               (1 << 8)
#define USB_PORTSC_SUSP             (1 << 7)
#define USB_PORTSC_CSC              (1 << 1)
#define USB_PORTSC_CCS              (1 << 0)

#define USB_USBINTR_SLE             (1 << 8)
#define USB_USBINTR_SRE             (1 << 7)
#define USB_USBINTR_URE             (1 << 6)
#define USB_USBINTR_AAE             (1 << 5)
#define USB_USBINTR_SEE             (1 << 4)
#define USB_USBINTR_FRE             (1 << 3)
#define USB_USBINTR_PCE             (1 << 2)
#define USB_USBINTR_UEE             (1 << 1)
#define USB_USBINTR_UE              (1 << 0)

#define USB_USBADR_ADRA             (1 << 24)

#define USB_OTGSC_BSEIE             (1 << 28)
#define USB_OTGSC_BSVIE             (1 << 27)
#define USB_OTGSC_ASVIE             (1 << 26)
#define USB_OTGSC_AVVIE             (1 << 25)
#define USB_OTGSC_IDIE              (1 << 24)

#define USB_OTGSC_BSEIS             (1 << 20)
#define USB_OTGSC_BSVIS             (1 << 19)
#define USB_OTGSC_ASVIS             (1 << 18)
#define USB_OTGSC_AVVIS             (1 << 17)
#define USB_OTGSC_IDIS              (1 << 16)

#define USB_OTGSC_OT                (1 << 3)

#define USB_CTRL_OWIR               (1 << 31)
#define USB_CTRL_OUIE               (1 << 28)
#define USB_CTRL_OWIE               (1 << 27)
#define USB_CTRL_OPM                (1 << 24)
#define USB_CTRL_HSIC               (3 << 21)
#define USB_CTRL_HUIE               (1 << 20)
#define USB_CTRL_HWIE               (1 << 19)
#define USB_CTRL_PP_HST             (1 << 18)
#define USB_CTRL_HPM                (1 << 16)
#define USB_CTRL_OKEN               (1 << 13)
#define USB_CTRL_HKLEN              (1 << 12)
#define USB_CTRL_PP_OTG             (1 << 11)
#define USB_CTRL_XCSO               (1 << 10)
#define USB_CTRL_XCSH               (1 << 9)
#define USB_CTRL_IP_PUIDP           (1 << 8)
#define USB_CTRL_IP_PUE_UP          (1 << 7)
#define USB_CTRL_IP_PUE_DWN         (1 << 6)
#define USB_CTRL_HSTD               (1 << 5)
#define USB_CTRL_USBTE              (1 << 4)
#define USB_CTRL_OCPOL_OTG          (1 << 3)
#define USB_CTRL_OCPOL_HST          (1 << 2)
#define USB_CTRL_HOCS               (1 << 1)
#define USB_CTRL_OOCS               (1 << 0)

#define USB_USBSTS_HCH              (1 << 12)
#define USB_USBSTS_SLI              (1 << 8)
#define USB_USBSTS_SRI              (1 << 7)
#define USB_USBSTS_URI              (1 << 6)
#define USB_USBSTS_AAI              (1 << 5)
#define USB_USBSTS_SEI              (1 << 4)
#define USB_USBSTS_FRI              (1 << 3)
#define USB_USBSTS_PCI              (1 << 2)
#define USB_USBSTS_UEI              (1 << 1)
#define USB_USBSTS_UI               (1 << 0)

#define USB_ENDPTCTRL_TXE           (1 << 23)
#define USB_ENDPTCTRL_TXR           (1 << 22)
#define USB_ENDPTCTRL_TXT_BULK      (2 << 18)
#define USB_ENDPTCTRL_TXS           (1 << 16)

#define USB_ENDPTCTRL_RXE           (1 << 7)
#define USB_ENDPTCTRL_RXR           (1 << 6)
#define USB_ENDPTCTRL_RXT_BULK      (2 << 2)
#define USB_ENDPTCTRL_RXS           (1 << 0)

//------------------------------------------------------------------------------

typedef volatile union _CHIPIDEA_USB_dTD {
    UINT32 DATA[16];
    struct {
        UINT32 T:1;                 // 00.00 Terminate (T)
        UINT32 RESERVED_0004:4;     // 00.01 Reserved
        UINT32 NEXT_dTD:27;         // 00.05 Next Transfer Element Pointer
        UINT32 STATUS:8;            // 01.00 Status of the last transaction
        UINT32 RESERVED_0028:2;     // 01.08 Reserved
        UINT32 MULTO:2;             // 01.0A Multiplier Override (MultO)
        UINT32 RESERVED_002C:3;     // 01.0C Reserved
        UINT32 IOC:1;               // 01.0F Interrupt On Complete (IOC)
        UINT32 TB:15;               // 01.10 Total Bytes
        UINT32 RESERVED_003F:1;     // 01.1F Reserved
        UINT32 CURR_OFF:12;         // 02.00 Current Offset
        UINT32 BP0:20;              // 02.0C Buffer Pointer 0
        UINT32 FN:11;               // 03.00 Frame Number
        UINT32 RESERVED_006B:1;     // 03.0B Reserved
        UINT32 BP1:20;              // 03.0C Buffer Pointer 1
        UINT32 RESERVED_0080:12;    // 04.00 Reserved
        UINT32 BP2:20;              // 04.0C Buffer Pointer 2
        UINT32 RESERVED_00A0:12;    // 05.00 Reserved
        UINT32 BP3:20;              // 05.0C Buffer Pointer 3
        UINT32 RESERVED_00C0:12;    // 06.00 Reserved
        UINT32 BP4:20;              // 06.0C Buffer Pointer 4
        };
    struct {
        UINT32 NextTD;
        UINT32 Control;
        UINT32 BP[5];
        UINT32 UserData;
        };
} CHIPIDEA_USB_dTD, *PCHIPIDEA_USB_dTD;

//------------------------------------------------------------------------------

#define USB_dTD_T                   (1 << 0)
#define USB_dTD_IOC                 (1 << 15)
#define USB_dTD_ACTIVE              (1 << 7)
#define USB_dTD_HALTED              (1 << 6)
#define USB_dTD_DATA_ERROR          (1 << 5)
#define USB_dTD_TRANS_ERROR         (1 << 3)
#define USB_dTD_STATUS_MASK         (0x00FF)
#define USB_dTD_OFFSET_MASK         (0x0FFF)
#define USB_dTD_FRAME_MASK          (0x0FFF)

//------------------------------------------------------------------------------

typedef volatile union CHIPIDEA_USB_dQH {
    UINT32 DATA[16];
    struct {
        UINT32 RESERVED_0000:15;    // 00.00 Reserved
        UINT32 IOS:1;               // 00.0F Interrupt On Setup
        UINT32 MPL:11;              // 00.10 Maximum Packet Length
        UINT32 RESERVED_001B:2;     // 00.1B Reserved
        UINT32 ZLT:1;               // 00.1D Zero Length Termination Select
        UINT32 MULT:2;              // 00.1E Packets executed per transaction
        UINT32 RESERVED_0020:5;     // 01.00 Reserved
        UINT32 CURR_dTD:27;         // 01.05 Current dTD
        UINT32 T:1;                 // 02.00 Terminate (T)
        UINT32 RESERVED_0024:4;     // 02.01 Reserved
        UINT32 NEXT_dTD:27;         // 02.05 Next Transfer Element Pointer
        UINT32 STATUS:8;            // 03.00 Status of the last transaction
        UINT32 RESERVED_0048:2;     // 03.08 Reserved
        UINT32 MULTO:2;             // 03.0A Multiplier Override (MultO)
        UINT32 RESERVED_004C:3;     // 03.0C Reserved
        UINT32 IOC:1;               // 03.0F Interrupt On Complete (IOC)
        UINT32 TB:15;               // 03.10 Total Bytes
        UINT32 RESERVED_005F:1;     // 03.1F Reserved
        UINT32 CURR_OFF:12;         // 04.00 Current Offset
        UINT32 BP0:20;              // 04.0C Buffer Pointer 0
        UINT32 FN:11;               // 05.00 Frame Number
        UINT32 RESERVED_008B:1;     // 05.0B Reserved
        UINT32 BP1:20;              // 05.0C Buffer Pointer 1
        UINT32 RESERVED_00A0:12;    // 06.00 Reserved
        UINT32 BP2:20;              // 06.0C Buffer Pointer 2
        UINT32 RESERVED_00C0:12;    // 07.00 Reserved
        UINT32 BP3:20;              // 07.0C Buffer Pointer 3
        UINT32 RESERVED_00E0:12;    // 08.00 Reserved
        UINT32 BP4:20;              // 08.0C Buffer Pointer 4
        UINT32 RESERVED_0100;       // 09.00 Reserved
        UINT32 SB0;                 // 0A.00 Setup Buffer 0
        UINT32 SB1;                 // 0B.00 Setup Buffer 1
        UINT32 RESERVED_0160[4];    // 0C.00 Reserved
        };
    struct {
        UINT32 Config;
        UINT32 CurrentTD;
        UINT32 NextTD;
        UINT32 Control;
        UINT32 BP[5];
        UINT32 Reserved;
        UINT32 SB[2];
    };
} CHIPIDEA_USB_dQH, *PCHIPIDEA_USB_dQH;

//------------------------------------------------------------------------------

#define USB_dQH_ZLT                 (1 << 29)
#define USB_dQH_IOS                 (1 << 15)
#define USB_dQH_T                   (1 << 0)

#define USB_dQH_STATUS              (0xFF << 0)
#define USB_dQH_ACTIVE              (1 << 7)
#define USB_dQH_HALTED              (1 << 6)
#define USB_dQH_DATA_ERROR          (1 << 5)
#define USB_dQH_TRANS_ERROR         (1 << 3)

//------------------------------------------------------------------------------

#include <poppack.h>

