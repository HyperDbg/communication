/*++

Copyright (c) Microsoft Corporation

Module Name:

    tracing.c

Abstract:

    Debug tracing.

Author:

    Joe Ballantyne (joeball)

Revision History:

    Initial revision 4/29/2011.

--*/


#if TRACE_DBG_PRINT

//
// Change the following variable to TRUE to force logging of the debug
// transport to be sent to the kernel DbgPrint log.  This can be done
// either dynamically at runtime from the debugger, or at compile time.
// Be careful to disable logging BEFORE running !dbgprint if you are
// running that command over the kd transport itself, or the log buffer
// will change while it is being dumped... which makes for confusing logs.
//

ULONG KdDbgLog = TRUE;

//
// These variables are used to caculate the time in microseconds between
// calls to the logging routine.
//

ULONG LastLogTime;
ULONG CyclesPerMicrosecond;
ULONG LoggingInitialized;

VOID
InitializeLogTiming (
    VOID
    )

/*++

Routine Description:

    Initialize the logging subsystem that enables logs to the circular debug
    print buffer.  Measure the frequency of ReadTimeStampCounter so that timing
    information can be automatically included in those logs.  Note that these
    logs are intentionally only sent to the in memory circular print buffer and
    are NOT sent through the transport to the host machine, since the point is
    to log transport activity itself.

    !dbgprint can be used to view the contents of the logged information.

    Note that KdDbgLog should be set to FALSE before running !dbgprint
    so that the transport operations used to view the log don't overwrite
    the log itself.

Arguments:

    None

Return Value:

    None

--*/

{
    ULONG StartTime;

    StartTime = (ULONG)ReadTimeStampCounter();
    KeStallExecutionProcessor(20000);
    CyclesPerMicrosecond = ((ULONG)ReadTimeStampCounter() - StartTime);
    CyclesPerMicrosecond += 10000;
    CyclesPerMicrosecond /= 20000;
    LoggingInitialized = TRUE;
    TRACE_ULONG(CyclesPerMicrosecond);
}

#if defined(_KDNET_INTERNAL_)

VOID
KdPrintf(
    __in __nullterminated PCHAR f,
    ...
    )

/*++

Routine Description:

    Printf routine for logging KD activity.  Output is written to the
    system circular debug print buffer.  It is NOT sent through the transport
    to the host machine, since the point is to log transport activity itself.
    !dbgprint can be used to view the contents of the logged information.
    Note that KdDbgLog should be set to FALSE before running !dbgprint
    so that the transport operations used to view the log don't overwrite
    the log itself.

Arguments:

    f - Supplies printf format

Return Value:

    None

--*/

{
    char Buffer[128];
    STRING Output;
    va_list mark;

    if (KdPrint != FALSE) {
        va_start(mark, f);
        RtlStringCbVPrintfA(Buffer, sizeof(Buffer), f, mark);
        va_end(mark);
        Output.Buffer = Buffer;
        Output.Length = (USHORT)strlen(Output.Buffer);
        if (Output.Length < sizeof(Buffer)) {
            Output.Length += 1;

        } else {
            Output.Length = sizeof(Buffer);
            Output.Buffer[Output.Length - 1] = 0;
        }

        KdLogDbgPrint(&Output);
    }

    return;
}

#endif

#endif
