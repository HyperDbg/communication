#include <pshpack1.h>

//------------------------------------------------------------------------------
//
// Version of the registers (this will map to the version of the Databook)
//

#define REGISTERS_VERSION               "2.03a"

//
// Offset of CSR (Control and Status) registers.
//

#define SYNOPSYS_CSR_REGISTERS_OFFSET   0xC100

//
// Number of event buffers and seperate interrupts that Synopsys supports
//

#define DWC_USB3_DEVICE_NUM_INT         32

//
// Size of Register Groups
//

#define GLOBAL_REGISTERS_SIZE           0x0600
#define DEVICE_REGISTERS_SIZE           0x0500
#define OTG_REGISTERS_SIZE              0x0020

//
// Macro to generate mask for the request "bit" position.
//

#define MASK(bit)                       (1 << bit)

//
// Magic QC Stuff
//

#define QCOM_SCRATCH_REGISTERS_BASE_OFFSET      0x000F8800
#define QCOM_SCRATCH_V1_HW_FIX_REGISTER_OFFSET  (QCOM_SCRATCH_REGISTERS_BASE_OFFSET + 0x30)
#define QCOM_PHY_CONTROL_LANE0_PWR_PRESENT_BMSK 0x01000000

#pragma warning(push)
#pragma warning(disable:4214)


//
// 6.2 Global Registers [0xC100 -0xC6FC]
//

//
// 6.2.1 Global Common Registers [0xC100 - 0xC134]
//

typedef union _GLOBAL_SOC_BUS_CONFIGURATION_REGISTER0 {

    ULONG AsUlong32;

    struct {
        ULONG INCRBrstEna:1;
        ULONG INCR4BrstEna:1;
        ULONG INCR8BrstEna:1;
        ULONG INCR16BrstEna:1;
        ULONG INCR32BrstEna:1;
        ULONG INCR64BrstEna:1;
        ULONG INCR128BrstEna:1;
        ULONG INCR256BrstEna:1;
        ULONG Reserved0:2;
        ULONG DescBigEnd:1;
        ULONG DataBigEnd:1;
        ULONG Reserved1:4;
        ULONG DesWrReqInfo:4;
        ULONG DatWrReqInfo:4;
        ULONG DesRdReqInfo:4;
        ULONG DatRdReqInfo:4;
    };
} GLOBAL_SOC_BUS_CONFIGURATION_REGISTER0, *PGLOBAL_SOC_BUS_CONFIGURATION_REGISTER0;

C_ASSERT(sizeof(GLOBAL_SOC_BUS_CONFIGURATION_REGISTER0) == sizeof(ULONG));

typedef union _GLOBAL_SOC_BUS_CONFIGURATION_REGISTER1 {

    ULONG AsUlong32;

    struct {
        ULONG Reserved0:8;
        ULONG PipeTransLimit:4;
        ULONG Enable1kPages:1;
        ULONG Reserved1:19;
    };
} GLOBAL_SOC_BUS_CONFIGURATION_REGISTER1, *PGLOBAL_SOC_BUS_CONFIGURATION_REGISTER1;

#define RAM_SELECT_BUS_CLOCK                    0
#define RAM_SELECT_PIPE_CLOCK                   1
#define RAM_SELECT_PIPE_CLOCK_BY_2              2

#define PORT_CAPABILITY_HOST_CONFIGURATION      1
#define PORT_CAPABILITY_DEVICE_CONFIGURATION    2
#define PORT_CAPABILITY_OTG_CONFIGURATION       3

typedef union _GLOBAL_CORE_CONTROL_REGISTER {

    ULONG AsUlong32;

    struct {
        ULONG DisableClockGating:1;
        ULONG EnableHibernation:1;
        ULONG U2ExitLFPS:1;
        ULONG DisableScrambling:1;
        ULONG ScaleDownMode:2;
        ULONG RAMClockSelect:2;
        ULONG DebugAttach:1;
        ULONG DisableU1U2TimerScaledown:1;
        ULONG SOFITPSync:1;
        ULONG CoreSoftReset:1;
        ULONG PortCapabilityDirection:2;
        ULONG FrmScaleDown:2;
        ULONG RetrySSConnection:1;
        ULONG BypassSetAddress:1;
        ULONG BypassAllFilters:1;
        ULONG PowerDownScale:13;
    };
} GLOBAL_CORE_CONTROL_REGISTER, *PGLOBAL_CORE_CONTROL_REGISTER;

C_ASSERT(sizeof(GLOBAL_CORE_CONTROL_REGISTER) == sizeof(ULONG));

typedef union _GLOBAL_STATUS_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG CurrentOperationMode:2;
        ULONG Reserved0:2;
        ULONG BusErrorAddressValid:1;
        ULONG CSRRegisterAccessTimeout:1;
        ULONG DeviceInterruptPending:1;
        ULONG HostInterruptPending:1;
        ULONG ADPInterruptPending:1;
        ULONG BatteryChargerInterruptPending:1;
        ULONG OTGInterruptPending:1;
        ULONG CurrentBELTValue:12;
    };
} GLOBAL_STATUS_REGISTER, *PGLOBAL_STATUS_REGISTER;

C_ASSERT(sizeof(GLOBAL_STATUS_REGISTER) == sizeof(ULONG));

typedef struct _GLOBAL_COMMON_REGISTERS {
    GLOBAL_SOC_BUS_CONFIGURATION_REGISTER0 SocBusConfig0;
    GLOBAL_SOC_BUS_CONFIGURATION_REGISTER1 SocBusConfig1;
    ULONG TxThresholdControl;
    ULONG RxThresholdControl;
    GLOBAL_CORE_CONTROL_REGISTER CoreControl;
    ULONG Reserved0;
    GLOBAL_STATUS_REGISTER Status;
    ULONG Reserved1;
    ULONG SynopsysID;
    ULONG GPIO;
    ULONG UserID;
    ULONG UserControl;
    ULONG BusErrorAddressLow;
    ULONG BusErrorAddressHigh;
} GLOBAL_COMMON_REGISTERS, *PGLOBAL_COMMON_REGISTERS;

#define GLOBAL_COMMON_REGISTER_SIZE     0x38
C_ASSERT(sizeof(GLOBAL_COMMON_REGISTERS) == GLOBAL_COMMON_REGISTER_SIZE);

//
// 6.2.2 Global Port to USB Instance Mapping Registers
// (Not contiguous, hence will be found in the Global Registers)
//

//
// 6.2.3 Global Hardware Parameter Registers
// (Not contiguous, hence will be found in the Global Registers)
//

//
// 6.2.4 Global Debug Registers [0xC160-0xC17C]
//

typedef struct _GLOBAL_DEBUG_REGISTERS {
    ULONG DebugRegisters[8];
} GLOBAL_DEBUG_REGISTERS, *PGLOBAL_DEBUG_REGISTERS;

#define GLOBAL_DEBUG_REGISTERS_SIZE     0x20
C_ASSERT(sizeof(GLOBAL_DEBUG_REGISTERS) == GLOBAL_DEBUG_REGISTERS_SIZE);

#define USB3_PIPECONTROL_SUSPEND_SS_PHY_BIT     17
#define USB2_PHY_CONFIG_SUSPEND_PHY_BIT         6

//
// 6.2.5 Global PHY Registers [0xC200-0xC2FC]
//

typedef struct _GLOBAL_PHY_REGISTERS {
    ULONG USB2PhyConfig[16];
    ULONG USB2I2CAccess[16];
    ULONG USB2PhyVendorControl[16];
    ULONG USB3PIPEControl[16];
} GLOBAL_PHY_REGISTERS, *PGLOBAL_PHY_REGISTERS;

#define GLOBAL_PHY_REGISTERS_SIZE       0x100
C_ASSERT(sizeof(GLOBAL_PHY_REGISTERS) == GLOBAL_PHY_REGISTERS_SIZE);

//
// 6.2.6 Global FIFO Size Registers [0xC300 - 0xC3FC]
//

typedef union _GLOBAL_FIFO_SIZE_REGISTER {

    ULONG AsUlong32;

    struct {
        ULONG FIFODepth:16;
        ULONG FIFOStartAddress:16;
    };
} GLOBAL_FIFO_SIZE_REGISTER, *PGLOBAL_FIFO_SIZE_REGISTER;

C_ASSERT(sizeof(GLOBAL_FIFO_SIZE_REGISTER) == sizeof(ULONG));

typedef struct _GLOBAL_FIFO_SIZE_REGISTERS {
    GLOBAL_FIFO_SIZE_REGISTER TxFIFOSize[32];
    GLOBAL_FIFO_SIZE_REGISTER RxFIFOSize[32];
} GLOBAL_FIFO_SIZE_REGISTERS, *PGLOBAL_FIFO_SIZE_REGISTERS;

#define GLOBAL_FIFO_SIZE_REGISTERS_SIZE     0x100
C_ASSERT(sizeof(GLOBAL_FIFO_SIZE_REGISTERS) == GLOBAL_FIFO_SIZE_REGISTERS_SIZE);

//
// 6.2.7 Global Event buffer registers [0xC400 - 0xC5FC]
//

typedef struct _GLOBAL_EVENT_BUFFER_ADDRESS_REGISTER {
    ULONG Lo;
    ULONG Hi;
} GLOBAL_EVENT_BUFFER_ADDRESS_REGISTER, *PGLOBAL_EVENT_BUFFER_ADDRESS_REGISTER;

typedef union _GLOBAL_EVENT_BUFFER_SIZE_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG SizeInBytes:16;
        ULONG Reserved0:15;
        ULONG InterruptMask:1;
    };
} GLOBAL_EVENT_BUFFER_SIZE_REGISTER, *PGLOBAL_EVENT_BUFFER_SIZE_REGISTER;

C_ASSERT(sizeof(GLOBAL_EVENT_BUFFER_SIZE_REGISTER) == sizeof(ULONG));

typedef union _GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG Count:16;
        ULONG Reserved0:16;
    };
} GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER, *PGLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER;

C_ASSERT(sizeof(GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER) == sizeof(ULONG));

typedef struct _GLOBAL_EVENT_BUFFER_REGISTERS {
    GLOBAL_EVENT_BUFFER_ADDRESS_REGISTER EventBufferAddress;
    GLOBAL_EVENT_BUFFER_SIZE_REGISTER EventSize;
    GLOBAL_EVENT_BUFFER_EVENT_COUNT_REGISTER EventCount;
} GLOBAL_EVENT_BUFFER_REGISTERS, *PGLOBAL_EVENT_BUFFER_REGISTERS;

#define GLOBAL_EVENT_BUFFER_REGISTERS_SIZE 0X10
C_ASSERT(sizeof(GLOBAL_EVENT_BUFFER_REGISTERS) == GLOBAL_EVENT_BUFFER_REGISTERS_SIZE);


//
// 6.2.9 Global DMA Priority Registers [0XC610 - 0XC624]
//

typedef struct _GLOBAL_DMA_PRIORITY_REGISTERS {
    ULONG DeviceTxFIFOPriority;
    ULONG DeviceRxFIFOPriority;
    ULONG HostTxFIFOPriority;
    ULONG HostRxFIFOPriority;
    ULONG HostDebugCapabilityPriority;
    ULONG HostFIFOPriorityRatio;
} GLOBAL_DMA_PRIORITY_REGISTERS, *PGLOBAL_DMA_PRIORITY_REGISTERS;

#define GLOBAL_DMA_PRIORITY_REGISTERS_SIZE      0x18
C_ASSERT(sizeof(GLOBAL_DMA_PRIORITY_REGISTERS) == GLOBAL_DMA_PRIORITY_REGISTERS_SIZE);

typedef struct _GLOBAL_REGISTERS {
    GLOBAL_COMMON_REGISTERS Common;         // [0xC100-0xC134]
    ULONG SSPortToBusInstanceMapping[2];    // [0xC138-0xC13C]
    ULONG HWParameters0_7[8];               // [0xC140-0xC15C]
    GLOBAL_DEBUG_REGISTERS Debug;           // [0xC160-0xC17C]
    ULONG HSPortToBusInstanceMapping[2];    // [0xC180-0xC184]
    ULONG FSPortToBusInstanceMapping[2];    // [0xC188-0xC18C]
    ULONG Reserved0[28];                    // [0xC190-0xC1FC]
    GLOBAL_PHY_REGISTERS Phy;               // [0xC200-0xC2FC]
    GLOBAL_FIFO_SIZE_REGISTERS FIFOSize;    // [0xC300-0xC3FC]
    GLOBAL_EVENT_BUFFER_REGISTERS EventBuffers[DWC_USB3_DEVICE_NUM_INT]; // [0xC400-0xC5FC]
    ULONG HWParameters8;                    // 0xC600
    ULONG Reserved1[3];                     // [0xC604-0xC60C]
    GLOBAL_DMA_PRIORITY_REGISTERS Priority; // [0xC610-0xC624]
    ULONG Reserved2[54];                    // [0xC628-0xC6FC]
} GLOBAL_REGISTERS, *PGLOBAL_REGISTERS;

C_ASSERT(sizeof(GLOBAL_REGISTERS) == GLOBAL_REGISTERS_SIZE);

//
// 6.3 Device Registers [0xC700 - 0xCBFC]
//

//
// 6.3.1 Device Common Registers [0xC700 - 0xC714]
//

//
// Device Speed Constants
//

#define DEVICE_SPEED_HIGH_SPEED         0
#define DEVICE_SPEED_FULL_SPEED         1
#define DEVICE_SPEED_LOW_SPEED          2
#define DEVICE_SPEED_FULL_SPEED_OTHER   3
#define DEVICE_SPEED_SUPER_SPEED        4

//
// 6.3.1.1 Device Configuration Register [0xC700]
//

typedef union _DEVICE_CONFIGURATION_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG MaxDeviceSpeed:3;
        ULONG DeviceAddress:7;
        ULONG Reserved0:2;
        ULONG EventBufferIndex:5;
        ULONG NumP:5;
        ULONG LPMCapable:1;
        ULONG IgnoreStreamPacketPending:1;
        ULONG Reserved1:8;
    };
} DEVICE_CONFIGURATION_REGISTER, *PDEVICE_CONFIGURATION_REGISTER;

C_ASSERT(sizeof(DEVICE_CONFIGURATION_REGISTER) == sizeof(ULONG));

//
// 6.3.1.2 Device Control Register [0xC704]
//

typedef union _DEVICE_CONTROL_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG Reserved0:1;
        ULONG TestControl:4;
        ULONG LinkStateChangeRequest:4;
        ULONG AcceptU1Enable:1;
        ULONG InitiateU1Enable:1;
        ULONG AcceptU2Enable:1;
        ULONG InitiateU2Enable:1;
        ULONG Reserved1:3;
        ULONG ControllerSaveState:1;
        ULONG ControllerRestoreState:1;
        ULONG L1HibernationEnable:1;
        ULONG KeepConnect:1;
        ULONG LPMResponse:4;
        ULONG HIRDThreshold:5;
        ULONG Reserved2:1;
        ULONG CoreSoftReset:1;
        ULONG RunStop:1;
    };
} DEVICE_CONTROL_REGISTER, *PDEVICE_CONTROL_REGISTER;

C_ASSERT(sizeof(DEVICE_CONTROL_REGISTER) == sizeof(ULONG));

//
// 6.3.1.3 Device Event Enable Register [0xC708]
//

typedef union _DEVICE_EVENT_ENABLE_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG Disconnect:1;
        ULONG USBReset:1;
        ULONG ConnectionDone:1;
        ULONG USBLinkStateChange:1;
        ULONG WakeUp:1;
        ULONG HibernateRequest:1;
        ULONG Suspend:1;
        ULONG SOF:1;
        ULONG Reserved0:1;
        ULONG ErraticError:1;
        ULONG Reserved1:2;
        ULONG VendorDeviceTestLMP:1;
        ULONG Reserved2:19;
    };
} DEVICE_EVENT_ENABLE_REGISTER, *PDEVICE_EVENT_ENABLE_REGISTER;

C_ASSERT(sizeof(DEVICE_EVENT_ENABLE_REGISTER) == sizeof(ULONG));


//
// 6.3.1.4 Device Status Register [0xC70C]
//

typedef union _DEVICE_STATUS_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG ConnectedSpeed:3;
        ULONG uFrameNumber:14;
        ULONG RxFIFOEmpty:1;
        ULONG USBLinkState:4;
        ULONG DeviceControllerHalted:1;
        ULONG CoreIdle:1;
        ULONG SaveStateStatus:1;
        ULONG RestoreStateStatus:1;
        ULONG Reserved0:2;
        ULONG SaveRestoreError:1;
        ULONG DeviceControllerNotReady:1;
        ULONG Reserved1:2;
    };
} DEVICE_STATUS_REGISTER, *PDEVICE_STATUS_REGISTER;

C_ASSERT(sizeof(DEVICE_STATUS_REGISTER) == sizeof(ULONG));

//
// USBLinkState fields values in SS and non-SS respectively
//

#define USB_LINK_STATE_U0               0
#define USB_LINK_STATE_U1               1
#define USB_LINK_STATE_U2               2
#define USB_LINK_STATE_U3               3
#define USB_LINK_STATE_SS_DISABLED      4
#define USB_LINK_STATE_RX_DETECT        5
#define USB_LINK_STATE_SS_INACTIVE      6
#define USB_LINK_STATE_POLLING          7
#define USB_LINK_STATE_RECOVERY         8
#define USB_LINK_STATE_HOT_RESET        9
#define USB_LINK_STATE_COMPLIANCE_MODE  10
#define USB_LINK_STATE_LOOPBACK         11
#define USB_LINK_STATE_RESUME_RESET     15

#define USB_LINK_STATE_ON               0
#define USB_LINK_STATE_SLEEP            2
#define USB_LINK_STATE_SUSPEND          3
#define USB_LINK_STATE_DISCONNECTED     4
#define USB_LINK_STATE_EARLY_SUSPEND    5
#define USB_LINK_STATE_RESET            14
#define USB_LINK_STATE_RESUME           15

//
// 6.3.1.6 Device Generic Command Register [0xC714]
//

#define DEVICE_COMMAND_SET_PERIODIC_PARAMETERS          2
#define DEVICE_COMMAND_SET_SCRATCHPAD_BUFFER_ARRAY_LO   4
#define DEVICE_COMMAND_SET_SCRATCHPAD_BUFFER_ARRAY_HI   5
#define DEVICE_COMMAND_TRANSMIT_DEVICE_NOTIFICATION     7
#define DEVICE_COMMAND_FLUSH_FIFO                       9
#define DEVICE_COMMAND_FLUSH_ALL_FIFO                   0xA
#define DEVICE_COMMAND_SET_ENDPOINT_NRDY                0xC
#define DEVICE_COMMAND_RUN_SOC_LOOPBACK_TEST            0x10

typedef union _DEVICE_COMMAND_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG CommandType:8;
        ULONG InterruptOnCompletion:1;
        ULONG Reserved0:1;
        ULONG CommandActive:1;
        ULONG Reserved1:4;
        ULONG CommandStatus:1;
        ULONG Reserved2:16;
    };
} DEVICE_COMMAND_REGISTER, *PDEVICE_COMMAND_REGISTER;

C_ASSERT(sizeof(DEVICE_COMMAND_REGISTER) == sizeof(ULONG));

typedef struct _DEVICE_COMMAND_REGISTERS {
    ULONG Parameter;                                    // [0xC710]
    DEVICE_COMMAND_REGISTER Command;
} DEVICE_COMMAND_REGISTERS, *PDEVICE_COMMAND_REGISTERS;

typedef struct _DEVICE_COMMON_REGISTERS {
    DEVICE_CONFIGURATION_REGISTER Configuration;
    DEVICE_CONTROL_REGISTER Control;
    DEVICE_EVENT_ENABLE_REGISTER EnabledEvents;
    DEVICE_STATUS_REGISTER Status;
    DEVICE_COMMAND_REGISTERS DeviceCommand;
    ULONG Reserved0[2];                                 // [0xC718 - 0xC71C]
} DEVICE_COMMON_REGISTERS, *PDEVICE_COMMON_REGISTERS;

#define DEVICE_COMMON_REGISTERS_SIZE        0x20
C_ASSERT(sizeof(DEVICE_COMMON_REGISTERS) == DEVICE_COMMON_REGISTERS_SIZE);

//
// 6.3.2 Device Endpoint Registers [0xC720 - 0xCBFC]
//

//
// 6.3.2.1 Device Active USB Endpoint Enable Register [0xC720]
//

typedef ULONG DEVICE_ENDPOINT_ENABLE_REGISTER;

typedef union _DEVICE_ENDPOINT_COMMAND_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG CommandType:4;
        ULONG Reserved0:4;
        ULONG InterruptOnCompletion:1;
        ULONG Reserved1:1;
        ULONG CommandActive:1;
        ULONG HighPriority_ForceRM:1;
        ULONG CommandStatus:4;
        ULONG Parameter:16;
    };
} DEVICE_ENDPOINT_COMMAND_REGISTER, *PDEVICE_ENDPOINT_COMMAND_REGISTER;

C_ASSERT(sizeof(DEVICE_ENDPOINT_COMMAND_REGISTER) == sizeof(ULONG));


typedef struct _DEVICE_ENDPOINT_COMMAND_REGISTERS {
    ULONG Parameter2;
    ULONG Parameter1;
    ULONG Parameter0;
    DEVICE_ENDPOINT_COMMAND_REGISTER EndpointCommand;
} DEVICE_ENDPOINT_COMMAND_REGISTERS, *PDEVICE_ENDPOINT_COMMAND_REGISTERS;

typedef struct _DEVICE_ENDPOINT_REGISTERS {
    DEVICE_ENDPOINT_ENABLE_REGISTER EndpointEnable; // [0xC720]
    ULONG Reserved0[55];                            // [0xC720] - [0xC7FC]
    DEVICE_ENDPOINT_COMMAND_REGISTERS EndpointCommand[32]; // [0xC800] - [0xC9FC]
    ULONG Reserved1[128];                           // [0xCA00] - [0xCBFC]
} DEVICE_ENDPOINT_REGISTERS, *PDEVICE_ENDPOINT_REGISTERS;

#define DEVICE_ENDPOINT_REGISTERS_SIZE 0X4E0
C_ASSERT(sizeof(DEVICE_ENDPOINT_REGISTERS) == DEVICE_ENDPOINT_REGISTERS_SIZE);

typedef struct _DEVICE_REGISTERS {
    DEVICE_COMMON_REGISTERS Common;
    DEVICE_ENDPOINT_REGISTERS Endpoints;
} DEVICE_REGISTERS, *PDEVICE_REGISTERS;

C_ASSERT(sizeof(DEVICE_REGISTERS) == DEVICE_REGISTERS_SIZE);


//
// 6.4 OTG and Battery registers [0xCC00 - 0xCCFC]
//

//
// 6.4 OTG registers [0xCC00 - 0xCC10]
//

//
// 6.4.1.1 OTG Configuration Register
//

typedef union _OTG_CONFIGURATION_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG SRPCapablility:1;
        ULONG HNPCapability:1;
        ULONG OTGVersion:1;
        ULONG OTGSoftResetMask:1;
        ULONG OTGHibernationDisableMask:1;
        ULONG OTGDisablePortCutOff:1;
        ULONG Reserved0:26;
    };
} OTG_CONFIGURATION_REGISTER, *POTG_CONFIGURATION_REGISTER;

C_ASSERT(sizeof(OTG_CONFIGURATION_REGISTER) == sizeof(ULONG));

//
// 6.4.1.2 OTG Control Register
//

typedef union _OTG_CONTROL_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG HostHNPEnable:1;
        ULONG DeviceHNPEnable:1;
        ULONG DLinePulsingSelection:1;
        ULONG SessionRequest:1;
        ULONG HNPRequest:1;
        ULONG PortPowerControl:1;
        ULONG OTG3GoToErrorState:1;
        ULONG Reserved0:24;
    };
} OTG_CONTROL_REGISTER, *POTG_CONTROL_REGISTER;

C_ASSERT(sizeof(OTG_CONTROL_REGISTER) == sizeof(ULONG));

//
// 6.4.1.3 OTG Events Register
//

typedef union _OTG_EVENTS_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG OTGEventError:1;
        ULONG Reserved0:1;
        ULONG HNSStatus:1;
        ULONG BSessionValid:1;
        ULONG Reserved1:4;

        ULONG VBusChangeEvent:1;
        ULONG BSessionStartedEvent:1;
        ULONG HNPChangeEvent:1;
        ULONG HostRoleEnd:1;

        ULONG Reserved2:4;
        ULONG Ignored0:8;

        ULONG ConnectorIDStatusChangeEvent:1;
        ULONG HiberntationEntryEvent:1;
        ULONG DeviceRunStopSetEvent:1;
        ULONG HostRunStopSetEvent:1;

        ULONG Reserved3:3;

        ULONG InBDeviceMode:1;
    };
} OTG_EVENTS_REGISTER, *POTG_EVENTS_REGISTER;

C_ASSERT(sizeof(OTG_EVENTS_REGISTER) == sizeof(ULONG));

//
// 6.4.1.4 OTG Events Enable Register
//

typedef union _OTG_EVENTS_ENABLE_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG Reserved0:8;

        ULONG VBusChangeEventEnable:1;
        ULONG BSessionStartedEventEnable:1;
        ULONG HNPChangeEventEnable:1;
        ULONG HostRoleEndEnable:1;

        ULONG Reserved1:4;
        ULONG Ignored0:8;

        ULONG ConnectorIDStatusChangeEventEnable:1;
        ULONG HiberntationEntryEventEnable:1;
        ULONG DeviceRunStopSetEventEnable:1;
        ULONG HostRunStopSetEventEnable:1;

        ULONG Reserved2:4;
    };
} OTG_EVENTS_ENABLE_REGISTER, *POTG_EVENTS_ENABLE_REGISTER;

C_ASSERT(sizeof(OTG_EVENTS_ENABLE_REGISTER) == sizeof(ULONG));

//
// 6.4.1.5 OTG Status Register
//

typedef union _OTG_STATUS_REGISTER {
    ULONG AsUlong32;
    struct {
        ULONG ConnectorIDStatus:1;
        ULONG HostModeVBUSStatus:1;
        ULONG BSessionValid:1;
        ULONG Ignored0:1;
        ULONG PeripheralState:1;

        ULONG Reserved0:3;

        ULONG OTG2State:4;
        ULONG HostRunStop:1;
        ULONG DeviceRunStop:1;

        ULONG Reserved2:18;
    };
} OTG_STATUS_REGISTER, *POTG_STATUS_REGISTER;

C_ASSERT(sizeof(OTG_STATUS_REGISTER) == sizeof(ULONG));


typedef struct _OTG_REGISTERS {
    OTG_CONFIGURATION_REGISTER Configuration;
    OTG_CONTROL_REGISTER Control;
    OTG_EVENTS_REGISTER Events;
    OTG_EVENTS_ENABLE_REGISTER EventsEnable;
    OTG_STATUS_REGISTER Status;
    ULONG Reserved0[3];
} OTG_REGISTERS, *POTG_REGISTERS;

C_ASSERT(sizeof(OTG_REGISTERS) == OTG_REGISTERS_SIZE);

//
// 6. Control Status registers
//

typedef struct _CSR_REGISTERS {
    UCHAR XHciRegisters[0x8000];
    UCHAR Reserved0[0x4100];
    GLOBAL_REGISTERS GlobalRegisters;
    DEVICE_REGISTERS DeviceRegisters;
    OTG_REGISTERS OTGRegisters;
} CSR_REGISTERS, *PCSR_REGISTERS;

//------------------------------------------------------------------------------

#define ENDPOINT_CMD_SET_CONFIGURATION          1
#define ENDPOINT_CMD_TRANSFER_RESOURCE_CONFIG   2
#define ENDPOINT_CMD_GET_STATE                  3
#define ENDPOINT_CMD_SET_STALL                  4
#define ENDPOINT_CMD_CLEAR_STALL                5
#define ENDPOINT_CMD_START_TRANSFER             6
#define ENDPOINT_CMD_UPDATE_TRANSFER            7
#define ENDPOINT_CMD_END_TRANSFER               8
#define ENDPOINT_CMD_START_CONFIGURATION        9

#define ENDPOINT_CONFIGURE_INITIALIZE           0
#define ENDPOINT_CONFIGURE_RESTORE              1
#define ENDPOINT_CONFIGURE_MODIFY               2


typedef union _ENDPOINT_SET_CONGIFURATION_PARAMETER_0 {
    ULONG AsUlong32;
    struct {
        ULONG Reserved1:1;
        ULONG EndpointType:2;
        ULONG MaxPacketSize:11;
        ULONG Reserved2:3;
        ULONG FifoNumber:5;
        ULONG BurstSize:4;
        ULONG Reserved3:4;
        ULONG ConfigAction:2;
    };
} ENDPOINT_SET_CONFIGURATION_PARAMETER_0, *PENDPOINT_SET_CONFIGURATION_PARAMETER_0;

C_ASSERT(sizeof(ENDPOINT_SET_CONFIGURATION_PARAMETER_0) == sizeof(ULONG));

typedef union _ENDPOINT_SET_CONGIFURATION_PARAMETER_1 {
    ULONG AsUlong32;
    struct {
        ULONG InterruptNumber:5;
        ULONG Reserved1:3;
        ULONG XferCompleteEnable:1;
        ULONG XferInProgressEnable:1;
        ULONG XferNotReadyEnable:1;
        ULONG Reserved2:2;
        ULONG StreamEventEnable:1;
        ULONG Reserved3:1;
        ULONG ExternalBufferControl:1;
        ULONG IntervalM1:8;
        ULONG StreamCapable:1;
        ULONG UsbEndpointDirection:1;
        ULONG UsbEndpointNumber:4;
        ULONG BulkBased:1;
        ULONG FifoBased:1;
    };
} ENDPOINT_SET_CONFIGURATION_PARAMETER_1, *PENDPOINT_SET_CONFIGURATION_PARAMETER_1;

C_ASSERT(sizeof(ENDPOINT_SET_CONFIGURATION_PARAMETER_1) == sizeof(ULONG));

//------------------------------------------------------------------------------

//
// 6.2.8 Content of Event Buffer
//

//
// 6.2.8.1 Content of Event Buffer for Endpoint specific event (ENDPOINT_EVENT)
//

typedef struct _ENDPOINT_TRANSFER_COMPLETE_EVENT {
    ULONG Reserved0:1;
    ULONG PhysicalEndpointNumber:5;
    ULONG Reserved1:7;
    ULONG IsShortPacket:1;
    ULONG IsInterruptOnComplete:1;
    ULONG IsLastTrb:1;
    ULONG StreamID:16;
} ENDPOINT_TRANSFER_COMPLETE_EVENT, *PENDPOINT_TRANSFER_COMPLETE_EVENT;

C_ASSERT(sizeof(ENDPOINT_TRANSFER_COMPLETE_EVENT) == sizeof(ULONG));

typedef struct _ENDPOINT_TRANSFER_IN_PROGRESS_EVENT {
    ULONG Reserved0:1;
    ULONG PhysicalEndpointNumber:5;
    ULONG Reserved1:7;
    ULONG IsShortPakcet:1;
    ULONG IOCBitOfCompletedTRB:1;
    ULONG MissedIsoc:1;
    ULONG IsocMicroFrameNumber:16;
} ENDPOINT_TRANSFER_IN_PROGRESS_EVENT, *PENDPOINT_TRANSFER_IN_PROGRESS_EVENT;

C_ASSERT(sizeof(ENDPOINT_TRANSFER_IN_PROGRESS_EVENT) == sizeof(ULONG));

typedef struct _ENDPOINT_TRANSFER_NOT_READY_EVENT {
    ULONG Reserved0:1;
    ULONG PhysicalEndpointNumber:5;
    ULONG Reserved1:6;
    ULONG ControlEndpointStage:2;
    ULONG Reserved2:1;
    ULONG IsTransferActive:1;
    ULONG StreamID:16;
} ENDPOINT_TRANSFER_NOT_READY_EVENT, *PENDPOINT_TRANSFER_NOT_READY_EVENT;

C_ASSERT(sizeof(ENDPOINT_TRANSFER_NOT_READY_EVENT) == sizeof(ULONG));

typedef struct _ENDPOINT_STREAM_EVENT {
    ULONG Reserved0:1;
    ULONG PhysicalEndpointNumber:5;
    ULONG Reserved1:6;
    ULONG StreamStatus:4;
    ULONG StreamID:16;
} ENDPOINT_STREAM_EVENT, *PENDPOINT_STREAM_EVENT;

C_ASSERT(sizeof(ENDPOINT_STREAM_EVENT) == sizeof(ULONG));

//
// Refer to ENDPOINT_COMMAND_* values defined in registers.h for 'CommandType'
//

typedef struct _ENDPOINT_COMMAND_COMPLETE_EVENT {
    ULONG Reserved0:1;
    ULONG PhysicalEndpointNumber:5;
    ULONG Reserved1:6;
    ULONG CommandStatus:4;
    ULONG XferRscIdx:7;
    ULONG Reserved2:1;
    ULONG CommandType:4;
    ULONG Reserved3:4;
} ENDPOINT_COMMAND_COMPLETE_EVENT, *PENDPOINT_COMMAND_COMPLETE_EVENT;

C_ASSERT(sizeof(ENDPOINT_COMMAND_COMPLETE_EVENT) == sizeof(ULONG));

typedef union _ENDPOINT_EVENT {
    struct {
        ULONG Reserved0:1;
        ULONG PhysicalEndpointNumber:5;
        ULONG EndpointEventType:4;
        ULONG Reserved1:2;
        ULONG Status:4;
        ULONG EventParameters:16;
    };
    ENDPOINT_TRANSFER_COMPLETE_EVENT TransferComplete;
    ENDPOINT_TRANSFER_IN_PROGRESS_EVENT TransferInProgress;
    ENDPOINT_TRANSFER_NOT_READY_EVENT TransferNotReady;
    ENDPOINT_STREAM_EVENT Stream;
    ENDPOINT_COMMAND_COMPLETE_EVENT Command;
    ULONG AsUlong32;
} ENDPOINT_EVENT, *PENDPOINT_EVENT;

C_ASSERT(sizeof(ENDPOINT_EVENT) == sizeof(ULONG));

typedef enum _ENDPOINT_EVENT_TYPE {
    EndpointEventReserved0 = 0,
    EndpointEventXferComplete,
    EndpointEventXferInProgress,
    EndpointEventXferNotReady,
    EndpointEventReserved1,
    EndpointEventReserved2,
    EndpointEventStreamEvent,
    EndpointEventCommandComplete
} ENDPOINT_EVENT_TYPE, *PENDPOINT_EVENT_TYPE;

//
// 6.2.8.2 Content of Event Buffer for device event (DEVICE_EVENT)
//

//
// Refer to ENDPOINT_COMMAND_* values defined in registers.h for 'CommandType'
//

typedef struct _DEVICE_COMMAND_COMPLETE_EVENT {
    ULONG Reserved0:1;
    ULONG PhysicalEndpointNumber:5;
    ULONG Reserved1:6;
    ULONG CommandStatus:4;
    ULONG Reserved2:8;
    ULONG CommandType:4;
    ULONG Reserved3:4;
} DEVICE_COMMAND_COMPLETE_EVENT, *PDEVICE_COMMAND_COMPLETE_EVENT;

C_ASSERT(sizeof(DEVICE_COMMAND_COMPLETE_EVENT) == sizeof(ULONG));

typedef struct _DEVICE_USB_LINK_STATE_CHANGE_EVENT {
    ULONG Reserved0:16;
    ULONG LinkState:4;
    ULONG Reserved1:12;
} DEVICE_USB_LINK_STATE_CHANGE_EVENT, *PDEVICE_USB_LINK_STATE_CHANGE_EVENT;

C_ASSERT(sizeof(DEVICE_USB_LINK_STATE_CHANGE_EVENT) == sizeof(ULONG));


typedef struct _DEVICE_HIBERNATION_REQUEST_EVENT {
    ULONG Reserved0:16;
    ULONG LinkState:4;
    ULONG SuperSpeedEvent:1;
    ULONG HIRD:4;
    ULONG Reserved1:7;
} DEVICE_HIBERNATION_REQUEST_EVENT, *PDEVICE_HIBERNATION_REQUEST_EVENT;

C_ASSERT(sizeof(DEVICE_HIBERNATION_REQUEST_EVENT) == sizeof(ULONG));

typedef enum _DEVICE_EVENT_TYPE {
    DeviceEventDisconnectDetected = 0,
    DeviceEventUSBReset = 1,
    DeviceEventConnectionDone = 2,
    DeviceEventUSBLinkStateChange = 3,
    DeviceEventWakeUpEvent = 4,
    DeviceEventHibernationRequest = 5,
    DeviceEventSOF = 7,
    DeviceEventErraticError = 9,
    DeviceEventCommandComplete = 10,
    DeviceEventEventBufferOverflow = 11,
    DeviceEventVendorDeviceTestLMP = 12,
} DEVICE_EVENT_TYPE, *PDEVICE_EVENT_TYPE;

typedef union _DEVICE_EVENT {
    ULONG AsUlong32;
    struct {
        ULONG Reserved0:8;
        ULONG DeviceEventType:4;
        ULONG Reserved1:4;
        ULONG EventInformation:9;
        ULONG Reserved2:7;
    };
    DEVICE_USB_LINK_STATE_CHANGE_EVENT LinkStateEvent;
    DEVICE_HIBERNATION_REQUEST_EVENT HibernationRequest;
} DEVICE_EVENT, *PDEVICE_EVENT;

C_ASSERT(sizeof(DEVICE_EVENT) == sizeof(ULONG));

typedef union _GLOBAL_EVENT {
    ULONG AsUlong32;
    struct {
        ULONG IsDeviceEvent:1;
        ULONG Reserved0:31;
    };
    ENDPOINT_EVENT EndpointEvent;
    DEVICE_EVENT DeviceEvent;
} GLOBAL_EVENT, *PGLOBAL_EVENT;

C_ASSERT(sizeof(GLOBAL_EVENT) == sizeof(ULONG));

//------------------------------------------------------------------------------

#define TRB_CONTROL_NORMAL                  1
#define TRB_CONTROL_SETUP                   2
#define TRB_CONTROL_STATUS_NO_DATA          3
#define TRB_CONTROL_STATUS_DATA             4
#define TRB_CONTROL_DATA                    5
#define TRB_CONTROL_LINK                    8

#define TRB_STATUS_SETUP_PACKET             2

#define TRB_ALIGNMENT                       16
#define DATA_ALIGNMENT                      64

#define EVENT_NOT_READY_CONTROL_DATA        1
#define EVENT_NOT_READY_CONTROL_STATUS      2

typedef struct _TRB {
    ULONG BufferPointerLow;
    LONG BufferPointerHigh;
    struct {
        ULONG BufferSize:24;
        ULONG PacketCountM1:2;
        ULONG Reserved1:2;
        ULONG TrbStatus:4;
    };
    struct {
        ULONG HardwareOwned:1;
        ULONG LastTrb:1;
        ULONG ChainBuffers:1;
        ULONG ContinueOnShortPacket:1;
        ULONG TrbControl:6;
        ULONG InterruptOnShortPacket:1;
        ULONG InterruptOnComplete:1;
        ULONG Reserved2:2;
        ULONG StreamId:16;
        ULONG Reserved3:2;
    };
} TRB, *PTRB;

C_ASSERT(sizeof(TRB) == sizeof(ULONG) * 4);

//------------------------------------------------------------------------------

#include <poppack.h>

