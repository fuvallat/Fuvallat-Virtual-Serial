#pragma once

#include <windows.h>
#include <wdf.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include "serial.h"
#include "ringbuffer.h"
#include "settings.h"
#include "driver.h"
#include "device.h"
#include "queue.h"

#define Trace(level, _fmt_, ...) DbgPrintEx(DPFLTR_DEFAULT_ID, level, _fmt_ "\n", __VA_ARGS__)

#define TRACE_LEVEL_ERROR DPFLTR_ERROR_LEVEL
#define TRACE_LEVEL_INFO DPFLTR_INFO_LEVEL

#ifndef ASSERT
#define ASSERT(exp) { if (!(exp)) { RtlAssert(#exp, __FILE__, __LINE__, NULL); }}
#endif

static void DebugLog(PDEVICE_CONTEXT ctx, const wchar_t* fmt, ...) {
    wchar_t finalBuf[512];

    wchar_t prefix[64];
    swprintf(prefix, 64, L"[Port %S] ", ctx->port);

    wchar_t msg[448];
    va_list args;
    va_start(args, fmt);
    vswprintf(msg, 448, fmt, args);
    va_end(args);

    swprintf(finalBuf, 512, L"%ws%ws", prefix, msg);

    OutputDebugStringW(finalBuf);
}

static void DebugLogVisible(PDEVICE_CONTEXT ctx, const UCHAR* data, size_t len) {
    wchar_t out[512];
    size_t pos = 0;

    int written = _snwprintf_s(out, _countof(out), _TRUNCATE, L"[Port %S] ", ctx->port);
    if (written < 0) return;
    pos = (size_t)written;

    for (size_t i = 0; i < len && pos < _countof(out) - 1; i++) {
        if (pos >= _countof(out) - 1) break;

        const wchar_t* fmt = NULL;
        wchar_t tmp[16];

        UCHAR c = data[i];
        if (c >= 32 && c <= 126) {
            fmt = L"%c";
            written = _snwprintf_s(out + pos, _countof(out) - pos, _TRUNCATE, fmt, c);
        } else {
            switch (c) {
            case '\r': fmt = L"[CR]"; break;
            case '\n': fmt = L"[LF]"; break;
            case '\t': fmt = L"[TAB]"; break;
            case 0x02: fmt = L"[STX]"; break;
            case 0x03: fmt = L"[ETX]"; break;
            case 0x1B: fmt = L"[ESC]"; break;
            default:
                _snwprintf_s(tmp, _countof(tmp), _TRUNCATE, L"[0x%02X]", c);
                fmt = tmp;
                break;
            }

            written = _snwprintf_s(out + pos, _countof(out) - pos, _TRUNCATE, L"%ws", fmt);
        }

        if (written <= 0) break;
        pos += (size_t)written;
        if (pos >= _countof(out) - 1) break;
    }

    out[pos] = L'\0';
    OutputDebugStringW(out);
}
