#include <ntddk.h>


UINT64 KdSinaTest(UINT16 Byte);


NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{


    for (size_t i = 0; i < 100; i++)
    {
        KdSinaTest((UINT16)i);

    }

    NTSTATUS status = STATUS_SUCCESS;
    return status;
}
