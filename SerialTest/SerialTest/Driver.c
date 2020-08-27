#include <ntddk.h>
#include"Serial.h"

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    status = STATUS_SUCCESS;

    DbgBreakPoint();
    DbgPrint("DriverObject : %x\n", DriverObject);

    DbgBreakPoint();
    DbgPrint("RegisteryPath : %x\n", RegistryPath);
    DbgBreakPoint();

    DbgPrint("Test\n");
    DbgBreakPoint();
    //SerialPortInitialize(SERIAL_PORT_0, 4800);
    SerialPortInitialize(SERIAL_PORT_1, 4800);
    SerialPortInitialize(SERIAL_PORT_2, 4800);
    //SerialPortInitialize(SERIAL_PORT_3, 4800);
    DbgBreakPoint();

    //SerialPortWrite(SERIAL_PORT_0, 'H');
    //SerialPortWrite(SERIAL_PORT_0, 'H');
    //SerialPortWrite(SERIAL_PORT_0, 'H');
    //SerialPortWrite(SERIAL_PORT_0, 'H');
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_1, 'H');
    SerialPortWrite(SERIAL_PORT_1, 'H');
    SerialPortWrite(SERIAL_PORT_1, 'H');
    SerialPortWrite(SERIAL_PORT_1, 'H');
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_2, 'H');
    SerialPortWrite(SERIAL_PORT_2, 'H');
    SerialPortWrite(SERIAL_PORT_2, 'H');
    SerialPortWrite(SERIAL_PORT_2, 'H');
    SerialPortWrite(SERIAL_PORT_2, 'H');
    DbgBreakPoint();
    DbgPrint("Test\n");
    DbgBreakPoint();
    DbgPrint("Test\n");
    DbgBreakPoint();
    DbgPrint("Test\n");
    DbgBreakPoint();
    DbgPrint("Test\n");
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_3, 'H');
    SerialPortWrite(SERIAL_PORT_3, 'H');
    SerialPortWrite(SERIAL_PORT_3, 'H');
    SerialPortWrite(SERIAL_PORT_3, 'H');
    SerialPortWrite(SERIAL_PORT_3, 'H');
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_1, 'i');
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_1, '!');
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_1, '!');
    DbgBreakPoint();
    SerialPortWrite(SERIAL_PORT_1, '!');
    DbgBreakPoint();

    return status;
}