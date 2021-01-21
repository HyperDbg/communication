/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    logger.c

Abstract:

    Simple Debug Output Library

Author:

    Karel Danihelka (kareld)

--*/

#include <ntddk.h>
#include <stdarg.h>
#include <limits.h>
#include <sal.h>
#include <logger.h>

//------------------------------------------------------------------------------

#define DEBUG_MAX_CHARS     160

//------------------------------------------------------------------------------

static 
ULONG   
LoggingZones;

//------------------------------------------------------------------------------
//
// helper functions for uartlogger_wvsprintf to work properly
//

static 
VOID 
Reverse(
    __in CHAR *pFirst, 
    __in CHAR *pLast
    ) 
{
    UINT_PTR swaps;
    CHAR ch;

    swaps = (INT)(pLast - pFirst + 1) >> 1;
    while (swaps--) {
        ch = *pFirst;
        *pFirst++ = *pLast;
        *pLast-- = ch;
    }
}

//------------------------------------------------------------------------------

static
VOID 
GetFormatValue(
    LPCSTR *pszFormat, 
    LONG *pWidth, 
    va_list *ppArgList
    )
{
    LONG width = 0;
    
    if (**pszFormat == '*') {
        *pWidth = va_arg(*ppArgList, int);
        (*pszFormat)++;
    } else {
        while (**pszFormat >= '0' && **pszFormat <= '9') {
            width = width * 10 + (**pszFormat - '0');
            (*pszFormat)++;
        }
        *pWidth = width;
    }
}

//------------------------------------------------------------------------------

INT_PTR
_vsprintf_p(
    _Out_writes_(maxChars) PSTR szBuffer, 
    int maxChars,
    _In_z_ PCSTR szFormat, 
    va_list pArgList
    )
/*++
    
    Routine Description:
    
        This function exists since we can't link to printf-style functions
        that are not available when KDNET loads very early in the boot
        process. Unfortunately this duplication is needed for now.
    
    Arguments:
    
        szBuffer - buffer that will contain the final string
        szFormat - format that will be used
        pArgList - the arguments
        maxChars - max chars supported by the buffer szBuffer        
        
    Return Value:
    
        None.
    
--*/      
{
    static CHAR upch[]  = "0123456789ABCDEF";
    static CHAR lowch[] = "0123456789abcdef";
    enum { typeNone = 0, typeNumber, typeCh, typeString } type;
    enum { modeNone = 0, modeH, modeL, modeX } mode;
    BOOLEAN padLeft, prefix, sign, upper;
    INT32 width, precision, radix = 0, chars;
    CHAR ch, fillCh;
    LPSTR szPos, szC;
    LPWSTR szW;
    UINT64 value;


    // First check input params
    if (szBuffer == NULL || szFormat == NULL || maxChars < 1) return 0;

    // Set actual position
    szPos = szBuffer;
    
    // While there are format strings
    while (*szFormat != '\0' && maxChars > 0) {

        // If it is other than format prefix, print it and move to next one
        if (*szFormat != '%') {
            if (--maxChars <= 0) goto cleanUp;
            *szPos++ = *szFormat++;
            continue;
        }

        // Set flags to default values        
        padLeft = FALSE;
        prefix = FALSE;
        sign = FALSE;
        upper = FALSE;
        fillCh = ' ';
        width = 0;
        precision = -1;
        type = typeNone;
        mode = modeNone;

        // read pad left and prefix flags
        while (*++szFormat != '\0') {
            if (*szFormat == '-') {
                padLeft = TRUE;
            } else if (*szFormat == '#') {
                prefix = TRUE;
            } else {
                break;
            }
        }

        // find fill character
        if (*szFormat == '0') {
            fillCh = '0';
            szFormat++;
        }
            
        // read the width specification 
        GetFormatValue(&szFormat, (LONG *)&width, &pArgList);

        // read the precision
        if (*szFormat == '.') {
            szFormat++;
            GetFormatValue(&szFormat, (LONG *)&precision, &pArgList);
        }                

        // get the operand size
        if (*szFormat == 'l') {
            mode = modeL;
            szFormat++;
        } else if (*szFormat == 'w') {
            mode = modeL;
            szFormat++;
        } else if (
            szFormat[0] == 'I' && szFormat[1] == '3' && szFormat[2] == '2'
        ) {                
            mode = modeL;
            szFormat += 3;
        } else if (*szFormat == 'h') {
            mode = modeH;
            szFormat++;
        } else if (
            szFormat[0] == 'I' && szFormat[1] == '6' && szFormat[2] == '4'
        ) {
            mode = modeX;
            szFormat += 3;
        }

        // break if there is unexpected format string end
        if (*szFormat == '\0') break;
            
        switch (*szFormat) {

        case 'i':
        case 'd':
            sign = TRUE;
        case 'u':
            radix = 10;
            type = typeNumber;
            if (mode == modeNone) mode = modeL;
            break;

        case 'X':
            if (width == 0) {
                width = 8;
                fillCh = '0';
            } else {
                upper = TRUE;
            }                
        case 'p':
        case 'x':
            radix = 16;
            type = typeNumber;
            if (mode == modeNone) mode = modeL;
            break;

        case 'B':
            upper = TRUE;
            radix = 16;
            width = 2;
            fillCh = '0';
            type = typeNumber;
            if (mode == modeNone) mode = modeL;
            break;            

        case 'H':
            upper = TRUE;
            radix = 16;
            width = 4;
            fillCh = '0';
            type = typeNumber;
            if (mode == modeNone) mode = modeL;
            break;            

        case 'c':
            if (mode == modeNone) mode = modeL;
            type = typeCh;
            break;
                
        case 'C':
            if (mode == modeNone) mode = modeH;
            type = typeCh;
            break;

        case 'a':
            mode = modeH;
            type = typeString;
            break;
            
        case 'S':
            if (mode == modeNone) mode = modeL;
            type = typeString;
            break;

        case 's':
            if (mode == modeNone) mode = modeH;
            type = typeString;
            break;

        default:
            if (--maxChars <= 0) goto cleanUp;
            *szPos++ = *szFormat;
        }

        // Move to next format character
        szFormat++;

        switch (type) {
        case typeNumber:
            // Special cases to act like MSC v5.10
            if (padLeft || precision >= 0) fillCh = ' ';
            // Fix possible prefix
            if (radix != 16) prefix = FALSE;
            // Depending on mode obtain value
            if (mode == modeH) {
                if (sign) {
                    value = (INT64)va_arg(pArgList, INT16);
                } else {
                    value = (UINT64)va_arg(pArgList, UINT16);
                }                    
            } else if (mode == modeL) {
                if (sign) {
                    value = (INT64)va_arg(pArgList, INT32);
                } else {
                    value = (UINT64)va_arg(pArgList, UINT32);
                }                    
            } else if (mode == modeX) {
                value = va_arg(pArgList, UINT64);
            } else {
                goto cleanUp;
            }
            // Should sign be printed?
            if (sign && (INT64)value < 0) {
                value = -(INT64)value;
            } else {
                sign = FALSE;
            }
            // Start with reverse string
            szC = szPos;
            chars = 0;
            do {
                if (--maxChars <= 0) goto cleanUp;
                *szC++ = upper ? upch[value%radix] : lowch[value%radix];
                chars++;
            } while ((value /= radix) != 0 && maxChars > 0);
            // Fix sizes
            width -= chars;
            precision -= chars;
            if (precision > 0) width -= precision;
            // Fill to the field precision
            while (precision-- > 0) {
                if (--maxChars <= 0) goto cleanUp;
                *szC++ = '0';
            }
            if (width > 0 && !padLeft) {
                // If we're filling with spaces, put sign first
                if (fillCh != '0') {
                    if (sign) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szC++ = '-';
                        width--;
                        sign = FALSE;
                    }
                    if (prefix && radix == 16) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szC++ = upper ? 'X' : 'x';
                        if (--maxChars <= 0) goto cleanUp;
                        *szC++ = '0';
                        prefix = FALSE;
                    }
                }
                // Leave place for sign
                if (sign) width--;
                // Fill to the field width
                while (width-- > 0) {
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = fillCh;
                }
                // Still have sign?
                if (sign) {
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = '-';
                    sign = FALSE;
                }
                // Or prefix?
                if (prefix) {
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = upper ? 'X' : 'x';
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = '0';
                    prefix = FALSE;
                }
                // Now reverse the string in place
                Reverse(szPos, szC - 1);
                szPos = szC;
            } else {
                // Add the sign character
                if (sign) {
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = '-';
                    sign = FALSE;
                }
                if (prefix) {
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = upper ? 'X' : 'x';
                    if (--maxChars <= 0) goto cleanUp;
                    *szC++ = '0';
                    prefix = FALSE;
                }
                // Reverse the string in place
                Reverse(szPos, szC - 1);
                szPos = szC;
                // Pad to the right of the string in case left aligned
                while (width-- > 0) {
                    if (--maxChars <= 0) goto cleanUp;
                    *szPos++ = fillCh;
                }                    
            }
            break;

        case typeCh:
            // Depending on size obtain value
            if (mode == modeH) {
                ch = va_arg(pArgList, CHAR);
            } else if (mode == modeL) {
                ch = va_arg(pArgList, CHAR);
            } else {
                goto cleanUp;
            }
            if (--maxChars <= 0) goto cleanUp;
            *szPos++ = ch;
            break;

        case typeString:
            if (mode == modeH) {
                // It is ascii string
                szC = va_arg(pArgList, LPSTR);
                if (szC == NULL) szC = "(NULL)";
                // Get string size
                chars = 0;
                while (chars < maxChars && szC[chars] != '\0') chars++;
                // Fix string size
                if (precision >= 0 && chars > precision) chars = precision;
                width -= chars;
                if (padLeft) {
                    while (chars-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = *szC++;
                    }                        
                    while (width-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = fillCh;
                    }                        
                } else {
                    while (width-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = fillCh;
                    }                        
                    while (chars-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = *szC++;
                    }                        
                }
            } else if (mode == modeL) {
                // It is unicode string
                szW = va_arg(pArgList, LPWSTR);
                if (szW == NULL) szW = L"(NULL)";
                // Get string size
                chars = 0;
                while (chars < maxChars && szW[chars] != L'\0') chars++;
                // Fix string size
                if (precision >= 0 && chars > precision) chars = precision;
                width -= chars;
                if (padLeft) {
                    while (chars-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = (CHAR)*szW++;
                    }                        
                    while (width-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = fillCh;
                    }                        
                } else {
                    while (width-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = fillCh;
                    }                        
                    while (chars-- > 0) {
                        if (--maxChars <= 0) goto cleanUp;
                        *szPos++ = (CHAR)*szW++;
                    }                        
                }
            } else {
                goto cleanUp;
            }
            break;
        }
            
    }

cleanUp:
    *szPos = '\0';
    return (int)(szPos - szBuffer);
}

//------------------------------------------------------------------------------

INT_PTR
_sprintf_p(
    _Out_writes_(maxChars) PSTR szBuffer, 
    int maxChars,
    _In_z_ PCSTR szFormat, 
    ...
    )
{
    INT_PTR chars;
    va_list arglist;
            
    va_start(arglist, szFormat);
    chars = _vsprintf_p(szBuffer, maxChars, szFormat, arglist);
    va_end(arglist);
    return chars;
}

//------------------------------------------------------------------------------

C_ASSERT(DEBUG_MAX_CHARS <= ULONG_MAX);

static
void
_DbgPrintf(
    _In_z_ char* formatString,
    va_list arglist
    )
/*++
    
    Routine Description:
    
        This is an internal function that logs the given string such
        that it fits withing DEBUG_MAX_CHARS characters. Longer strings
        are simply truncated.
    
    Arguments:
    
        FormatString - Format-string 
        arglist      - Arguments to be logged.
        
    Return Value:
    
        None.
    
--*/  

{
    CHAR buffer[DEBUG_MAX_CHARS];
    PCHAR pos;
    ULONG_PTR size;

    size = _vsprintf_p(buffer, DEBUG_MAX_CHARS - 1, formatString, arglist);

    pos = buffer;
    while (*pos != '\0')
        {
        DebugOutputByte(*pos++);
        }
}

//------------------------------------------------------------------------------

void
DbgPrintf(
    _Printf_format_string_ char* formatString,
    ...
    )
/*++

Routine Description:

    This is the top level function that does logging to UART. It calls
    internal functions to do the actual logging.

Arguments:

    FormatString - Format-string using which the UART logging is to be done

    ...          - Variadic arguments for logging.
    
Return Value:

    None.

--*/    
{
    va_list arglist;

        
    va_start(arglist, formatString);
    _DbgPrintf(formatString, arglist);
    va_end(arglist);
}

//------------------------------------------------------------------------------

void
DbgPrintfWithZones(
    ULONG zones,
    _In_z_ char* FormatString,
    ...
    )
/*++

Routine Description:

    This is the top level function that does logging to UART. It calls
    internal functions to do the actual logging.

Arguments:

    FormatString - Format-string using which the UART logging is to be done

    ...          - Variadic arguments for logging.
    
Return Value:

    None.

--*/    
{
    va_list arglist;


    if (zones & LoggingZones)
        {        
        va_start(arglist, FormatString);
        _DbgPrintf(FormatString, arglist);
        va_end(arglist);
        }
}

//------------------------------------------------------------------------------

VOID
DbgSetZones(
    ULONG zones
    )
{
    LoggingZones = zones;
}

//------------------------------------------------------------------------------

