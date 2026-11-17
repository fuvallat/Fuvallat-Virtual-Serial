#include <windows.h>

#include "resource.h"
#include "FuvallatProvider.h"

#ifndef _countof
#define _countof(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

extern HINSTANCE g_hInstance;

INT_PTR CALLBACK FuvallatDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

static BOOL GetPortName(HDEVINFO hSet, PSP_DEVINFO_DATA pDev, LPWSTR buf, DWORD cchBuf) {
	HKEY hKey = SetupDiOpenDevRegKey(hSet, pDev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
	if (hKey == INVALID_HANDLE_VALUE) return FALSE;
	DWORD type = 0, cb = cchBuf * sizeof(WCHAR);
	LONG rc = RegQueryValueExW(hKey, L"PortName", NULL, &type, (LPBYTE)buf, &cb);
	RegCloseKey(hKey);
	return (rc == ERROR_SUCCESS && type == REG_SZ && buf[0] != L'\0');
}

extern "C" __declspec(dllexport) BOOL CALLBACK FuvallatPropPageProvider( PSP_PROPSHEETPAGE_REQUEST pRequest, LPFNADDPROPSHEETPAGE AddPageFunc, LPARAM lParam) {
	if (!pRequest || !pRequest->DeviceInfoData || pRequest->DeviceInfoData->cbSize != sizeof(SP_DEVINFO_DATA)) {
		return FALSE;
	}

	PAGE_CONTEXT* ctx = (PAGE_CONTEXT*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PAGE_CONTEXT));
	if (!ctx) return FALSE;

	ctx->hDevInfo = pRequest->DeviceInfoSet;
	ctx->devInfo = *pRequest->DeviceInfoData;

	SetupDiGetDeviceInstanceIdW(ctx->hDevInfo, &ctx->devInfo, ctx->InstanceId, _countof(ctx->InstanceId), NULL);
	if (!GetPortName(ctx->hDevInfo, &ctx->devInfo, ctx->PortName, _countof(ctx->PortName))) {
		ctx->PortName[0] = L'\0';
	}

	PROPSHEETPAGEW psp = { 0 };
	psp.dwSize = sizeof(psp);
	psp.dwFlags = PSP_USETITLE;
	psp.hInstance = g_hInstance;
	psp.pszTemplate = MAKEINTRESOURCEW(IDD_FUVALLAT_DIALOG);
	psp.pfnDlgProc = FuvallatDlgProc;
	psp.pszTitle = L"Settings";
	psp.lParam = (LPARAM)ctx;

	HPROPSHEETPAGE hPage = CreatePropertySheetPageW(&psp);
	if (!hPage) {
		HeapFree(GetProcessHeap(), 0, ctx);
		return FALSE;
	}

	if (!AddPageFunc(hPage, lParam)) {
		DestroyPropertySheetPage(hPage);
		HeapFree(GetProcessHeap(), 0, ctx);
		return FALSE;
	}

	return TRUE;
}
