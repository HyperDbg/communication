/*++

Copyright (c) Microsoft Corporation

Module Name:

    uart.c

Abstract:

    Simple trivial UART debug output for QC8960

Author:

    Karel Danihelka (kareld)

--*/

#include <pch.h>

//------------------------------------------------------------------------------
//
// Register addresses
//
#define UART_DM_SR_ADDR                             0x00000008
#define UART_DM_CSR_ADDR                            0x00000008
#define UART_DM_CR_ADDR                             0x00000010
#define UART_DM_ISR_ADDR                            0x00000014
#define UART_DM_IMR_ADDR                            0x00000014
#define UART_DM_DMRX_ADDR                           0x00000034
#define UART_DM_RX_TOTAL_SNAP_ADDR                  0x00000038
#define UART_DM_NO_CHARS_FOR_TX_ADDR                0x00000040
#define UART_DM_RXFS_ADDR                           0x00000050
#define UART_DM_TF_ADDR                             0x00000070
#define UART_DM_RF_ADDR                             0x00000070

//
// Register Masks
//
#define UART_DM_SR_UART_OVERRUN_BMSK                0x10
#define UART_DM_SR_PAR_FRAME_ERR_BMSK               0x20
#define UART_DM_SR_RX_BREAK_BMSK                    0x40
#define UART_DM_SR_HUNT_CHAR_BMSK                   0x80


#define UART_DM_SR_RXRDY_BMSK                       0x01
#define UART_DM_SR_TXRDY_BMSK                       0x04
#define UART_DM_SR_TXEMT_BMSK                       0x08
#define UART_DM_SR_UART_OVERRUN_BMSK                0x10


#define UART_DM_ISR_RXSTALE_BMSK                    0x08
#define UART_DM_ISR_TX_READY_BMSK                   0x80

//
// Channel Command
//
#define UART_DM_CH_CMD_RESET_RECEIVER               0x01
#define UART_DM_CH_CMD_RESET_TRANSMITTER            0x02
#define UART_DM_CH_CMD_RESET_ERROR_STATUS           0x03
#define UART_DM_CH_CMD_RESET_BREAK_CHANGE_IRQ       0x04
#define UART_DM_CH_CMD_START_BREAK                  0x05
#define UART_DM_CH_CMD_STOP_BREAK                   0x06
#define UART_DM_CH_CMD_RESET_CTS_N                  0x07
#define UART_DM_CH_CMD_RESET_STALE_IRQ              0x08
#define UART_DM_CH_CMD_PACKET_MODE                  0x09
#define UART_DM_CH_CMD_TEST_PARITY_ON               0x0A
#define UART_DM_CH_CMD_TEST_FRAME_ON                0x0B
#define UART_DM_CH_CMD_MODE_RESET                   0x0C
#define UART_DM_CH_CMD_SET_RFR_N                    0x0D
#define UART_DM_CH_CMD_RESET_RFR_N                  0x0E
#define UART_DM_CH_CMD_UART_RESET_INT               0x0F
#define UART_DM_CH_CMD_RESET_TX_ERROR               0x10
#define UART_DM_CH_CMD_CLEAR_TX_DONE                0x11
#define UART_DM_CH_CMD_RESET_BRK_START_IRQ          0x12
#define UART_DM_CH_CMD_RESET_BRK_END_IRQ            0x13
#define UART_DM_CH_CMD_RESET_PAR_FRAME_ERR_IRQ      0x14

//
// General Command
//
#define UART_DM_GENERAL_CMD_CR_PROTECTION_ENABLE    0x01
#define UART_DM_GENERAL_CMD_CR_PROTECTION_DISABLE   0x02
#define UART_DM_GENERAL_CMD_RESET_TX_READY_IRQ      0x03
#define UART_DM_GENERAL_CMD_SW_FORCE_STALE          0x04
#define UART_DM_GENERAL_CMD_ENABLE_STALE_EVENT      0x05
#define UART_DM_GENERAL_CMD_DISABLE_STALE_EVENT     0x06

//------------------------------------------------------------------------------

#define INREG8(x)               READ_REGISTER_UCHAR((PUCHAR)(x))
#define OUTREG8(x, y)           WRITE_REGISTER_UCHAR((PUCHAR)(x), (y))
#define SETREG8(x, y)           OUTREG8(x, INREG8(x)|(y))
#define CLRREG8(x, y)           OUTREG8(x, INREG8(x)&~(y))
#define MASKREG8(x, y, z)       OUTREG8(x, (INREG8(x)&~(y))|(z))

#define INREG32(x)              READ_REGISTER_ULONG((PULONG)(x))
#define OUTREG32(x, y)          WRITE_REGISTER_ULONG((PULONG)(x), (y))
#define SETREG32(x, y)          OUTREG32(x, INREG32(x)|(y))
#define CLRREG32(x, y)          OUTREG32(x, INREG32(x)&~(y))
#define MASKREG32(x, y, z)      OUTREG32(x, (INREG32(x)&~(y))|(z))

//------------------------------------------------------------------------------

#define UART_DM_CH_CMD(addr, val)        OUTREG32(                          \
        (addr) + UART_DM_CR_ADDR,                                           \
        ((((val) >> 4) & 0x1) << 11) | (((val) & 0xF) << 4)                 \
    )

#define UART_DM_GENERAL_CMD(addr, val)   OUTREG32(                          \
        (addr) + UART_DM_CR_ADDR,                                           \
        ((val) & 0x7) << 8                                                  \
    )

//------------------------------------------------------------------------------

static
PUCHAR
s_uartBase;

//------------------------------------------------------------------------------


//------------------------------------------------------------------------------

VOID
DebugSerialOutputByte(
    _In_ const UCHAR byte
    )
{
            
    //
    // Skip if UART isn't initialized
    //

    if (s_uartBase == NULL) {
        goto Exit;
    }

    //
    // Wait for prior packet to be sent    
    //

    if (0 == (INREG32(s_uartBase + UART_DM_SR_ADDR) & UART_DM_SR_TXEMT_BMSK)) {
        while (0 == (INREG32(s_uartBase + UART_DM_ISR_ADDR) & UART_DM_ISR_TX_READY_BMSK)) { 
        }
    }

    //
    // Tx 1 byte
    //

    OUTREG32(s_uartBase + UART_DM_NO_CHARS_FOR_TX_ADDR, 1);
    UART_DM_GENERAL_CMD(s_uartBase, UART_DM_GENERAL_CMD_RESET_TX_READY_IRQ);

    //
    // Wait for space in TX FIFO
    //

    while (0 == (INREG32(s_uartBase + UART_DM_SR_ADDR) & UART_DM_SR_TXRDY_BMSK)) {
    }

    //
    // Write to TX fifo
    //

    OUTREG32(s_uartBase + UART_DM_TF_ADDR, (UINT32)byte);
    while (0 == (INREG32(s_uartBase + UART_DM_SR_ADDR) & UART_DM_SR_TXEMT_BMSK)) {
    }

Exit:
    return;
}

VOID
DebugOutputByte(
    const UCHAR byte
    )
{
    DebugSerialOutputByte(byte);
}

NTSTATUS
DebugOutputInitInternal(
    _In_opt_ PDEBUG_DEVICE_DESCRIPTOR pDevice,
    _In_ ULONG index,
    _Out_opt_ PPHYSICAL_ADDRESS PAddress
    )
{

    NTSTATUS Status;

    Status = STATUS_UNSUCCESSFUL;
    if ((s_uartBase == NULL) && (pDevice != NULL)) {
       s_uartBase = pDevice->BaseAddress[index].TranslatedAddress;

    }
    else if ((s_uartBase == NULL) && (PAddress != NULL)) {
       s_uartBase = KdMapPhysicalMemory64(*PAddress, 1, FALSE);
    }

    if (s_uartBase == NULL) {
        goto Exit;
    }

    UART_DM_CH_CMD(s_uartBase, UART_DM_CH_CMD_RESET_ERROR_STATUS);
    UART_DM_GENERAL_CMD(s_uartBase, UART_DM_GENERAL_CMD_ENABLE_STALE_EVENT);
    UART_DM_CH_CMD(s_uartBase, UART_DM_CH_CMD_RESET_STALE_IRQ);
    Status = STATUS_SUCCESS;

Exit:
    return Status;
}

NTSTATUS
DebugSerialOutputInit(
    _In_opt_ PDEBUG_DEVICE_DESCRIPTOR Device,
    _Out_opt_ PPHYSICAL_ADDRESS PAddress
    )
/*++

Routine Description:

    Serial UART initialization routine.

Arguments:

    pDevice - Pointer to Device descriptor structure.
    PAddress - Pointer to the MM physical address.

Return Value:

    Nothing.

Note.
    This function will try to use the current set port address that is present in the descriptor array,
    if the address is not available, then it'll check for the address that can be manually set by using the command:
    "bcdedit.exe -set LoadOptions KDNET_EXTENSIBILITY_SERIAL_LOGGING,KDNET_EXTENSIBILITY_SERIAL_PORT_ADDRESS=<physical address value>"

    This is the current list of the known physical addresses available for QC HW.

    Board Type      MM physical address Value
    8660            0x19C40000
    8960 (Phi)      0x16440000
    8974            0xF991E000
    8x26 (Qrd)      0xF991F000
    8996            0x075b0000

    The board type can be found in the OEMInput.xml file:
    Ex. 
    The board type is 8960 (tag:<SOC>QC8960_Test</SOC>) for the Phi image type.
    The file OEMInput.xml is located in the image directory together with flash.ffu file.


--*/
{
    
    UNREFERENCED_PARAMETER(Device);
    NTSTATUS Status;

    Status = DebugOutputInitInternal(NULL, 0, PAddress);
    return Status;
}

VOID
DebugOutputInit(
    PDEBUG_DEVICE_DESCRIPTOR pDevice,
    ULONG index
    )
{

    DebugOutputInitInternal(pDevice, index, NULL);
    return;
}

//------------------------------------------------------------------------------

