#include <windows.h>
#include <vector>
#include <string>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

#include "internal.h"
#include "FuvallatProvider.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define IDC_MODE                        1001
#define IDC_TARGET_PORT                 1002
#define IDC_MODE_LABEL                  1003
#define IDC_TARGET_LABEL                1004
#define IDC_FORWARD_TARGET              1005
#define IDC_FORWARD_TARGET_LABEL        1006
#define IDC_TARGET_ADDRESS              1008
#define IDC_TARGET_FILE                 1009
#define IDC_GROUP_PROPERTIES            1010
#define IDC_SETTINGS_LIST	            1011
#define IDC_OPERATION		            1012
#define IDC_OPERATION_LABEL		        1013
#define IDC_ENABLE_CHECK				1014
#define IDC_DEVICE_LABEL				1015
#define IDC_DEVICE						1016

#define IDOK 1

INT_PTR CALLBACK FuvallatDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void InitDialog(HWND hDlg, const std::wstring& currentPort);
void UpdateVisibility(HWND hDlg);
void LoadDeviceList(HWND hDeviceCombo, const std::wstring& currentPort);
void SaveSettings(HWND hDlg, const std::wstring& currentPort);
std::vector<std::wstring> EnumerateSerialPorts();
//void RefreshSettingsList(HWND hDlg);
BOOL GetComPortSettings(const char* port, char* outText, size_t outSize);

void InitControls() {
	INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX) };
	icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_USEREX_CLASSES;
	InitCommonControlsEx(&icex);
}

INT_PTR CALLBACK FuvallatDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG:
		{
			InitControls();

			const PROPSHEETPAGEW* psp = (const PROPSHEETPAGEW*)lParam;
			PAGE_CONTEXT* ctx = (PAGE_CONTEXT*)psp->lParam;
			SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);

			char port[32];
			WideCharToMultiByte(CP_UTF8, 0, ctx->PortName, -1, port, sizeof(port), NULL, NULL);
			CreateDefaultSettings(port);

			InitDialog(hDlg, ctx->PortName);

			char settingsText[128];
			if (GetComPortSettings(port, settingsText, sizeof(settingsText))) {
				SetDlgItemTextA(hDlg, IDC_GROUP_PROPERTIES, settingsText);
			} else {
				SetDlgItemTextA(hDlg, IDC_GROUP_PROPERTIES, port);
			}

//			SetTimer(hDlg, 1, 4000, NULL);
			return TRUE;
		}
		case WM_DESTROY: {
			PAGE_CONTEXT* ctx = (PAGE_CONTEXT*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
			if (ctx) {
				HeapFree(GetProcessHeap(), 0, ctx);
				SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
			}

			break;
		}
		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case IDC_MODE:
					if (HIWORD(wParam) == CBN_SELCHANGE) {
						UpdateVisibility(hDlg);
					}
					break;
				case IDC_FORWARD_TARGET:
					if (HIWORD(wParam) == CBN_SELCHANGE) {
						UpdateVisibility(hDlg);
					}
					break;
				case IDOK: {
					PAGE_CONTEXT* ctx = (PAGE_CONTEXT*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
					if (ctx) {
						SaveSettings(hDlg, ctx->PortName);
					}
					return TRUE;
				}
			}
			break;
		}
		case WM_TIMER:
			if (wParam == 1) {
//				RefreshSettingsList(hDlg);
			}
			break;
	}

	return FALSE;
}

void LoadDeviceList(HWND hDeviceCombo, const std::wstring& currentPort) {
	SendMessage(hDeviceCombo, CB_RESETCONTENT, 0, 0);
	OutputDebugStringW(L"LoadDeviceList: Started.");

	wchar_t baseKey[256];
	swprintf(baseKey, 256, L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\%ws", currentPort.c_str());

	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return;

	DWORD index = 0;
	wchar_t subKeyName[256];
	DWORD subKeyLen;

	while (true) {
		subKeyLen = 256;
		LONG res = RegEnumKeyExW(hKey, index, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL);
		if (res != ERROR_SUCCESS) break;

		if (iswdigit(subKeyName[0])) {
			int deviceID = _wtoi(subKeyName);

			wchar_t fullPath[256];
			swprintf(fullPath, 256, L"%ws\\%ws", baseKey, subKeyName);

			wchar_t deviceName[256];
			DWORD size = sizeof(deviceName);
			HKEY hSub;
			if (RegOpenKeyExW(hKey, subKeyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
				if (RegQueryValueExW(hSub, NULL, NULL, NULL, (LPBYTE)deviceName, &size) == ERROR_SUCCESS) {
					int idx = SendMessage(hDeviceCombo, CB_ADDSTRING, 0, (LPARAM)deviceName);
					SendMessage(hDeviceCombo, CB_SETITEMDATA, idx, deviceID);
				}
				RegCloseKey(hSub);
			}
		}

		index++;
	}

	RegCloseKey(hKey);
	OutputDebugStringW(L"LoadDeviceList: Finished.");
}

void InitDialog(HWND hDlg, const std::wstring& currentPort) {
	HWND hModeCombo = GetDlgItem(hDlg, IDC_MODE);
	HWND hForwardCombo = GetDlgItem(hDlg, IDC_FORWARD_TARGET);
	HWND hPortCombo = GetDlgItem(hDlg, IDC_TARGET_PORT);
	HWND hNetText = GetDlgItem(hDlg, IDC_TARGET_ADDRESS);
	HWND hFileText = GetDlgItem(hDlg, IDC_TARGET_FILE);
	HWND hOperCombo = GetDlgItem(hDlg, IDC_OPERATION);
	HWND hOperText = GetDlgItem(hDlg, IDC_OPERATION_LABEL);
	HWND hEnable = GetDlgItem(hDlg, IDC_ENABLE_CHECK);
	HWND hDeviceCombo = GetDlgItem(hDlg, IDC_DEVICE);
	HWND hDeviceText = GetDlgItem(hDlg, IDC_DEVICE_LABEL);

	SendMessage(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"Emulator");
	SendMessage(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"Forwarder");
	SendMessage(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"Echo");
	SendMessage(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"Fake");
	SendMessage(hModeCombo, CB_SETCURSEL, 0, 0);

	SendMessage(hOperCombo, CB_ADDSTRING, 0, (LPARAM)L"Automatic answer");
	SendMessage(hOperCombo, CB_ADDSTRING, 0, (LPARAM)L"Ask for respond");
	SendMessage(hOperCombo, CB_SETCURSEL, 0, 0);

	LoadDeviceList(hDeviceCombo, currentPort);

	SendMessage(hForwardCombo, CB_ADDSTRING, 0, (LPARAM)L"Serial Port");
	SendMessage(hForwardCombo, CB_ADDSTRING, 0, (LPARAM)L"Network Address");
	SendMessage(hForwardCombo, CB_ADDSTRING, 0, (LPARAM)L"Local File");
	SendMessage(hForwardCombo, CB_SETCURSEL, 0, 0);

	std::vector<std::wstring> ports = EnumerateSerialPorts();
	for (const auto& port : ports) {
		if (_wcsicmp(port.c_str(), currentPort.c_str()) != 0) {
			SendMessage(hPortCombo, CB_ADDSTRING, 0, (LPARAM)port.c_str());
		}
	}

	if (ports.size() <= 1) {
		ShowWindow(hPortCombo, SW_HIDE);
	}

	std::wstring keyPath = L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\" + currentPort;
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		DWORD modeValue = 0;
		DWORD operValue = 0;
		DWORD forwardTypeValue = 0;
		DWORD enableValue = 0;
		DWORD deviceValue = 0;
		DWORD size = sizeof(DWORD);
		DWORD type = 0;

		if (RegQueryValueExW(hKey, L"Mode", NULL, &type, (LPBYTE)&modeValue, &size) == ERROR_SUCCESS && type == REG_DWORD) {
			SendMessage(hModeCombo, CB_SETCURSEL, modeValue, 0);
		} else {
			SendMessage(hModeCombo, CB_SETCURSEL, 0, 0);
		}

		if (RegQueryValueExW(hKey, L"Operation", NULL, &type, (LPBYTE)&operValue, &size) == ERROR_SUCCESS && type == REG_DWORD) {
			SendMessage(hOperCombo, CB_SETCURSEL, operValue, 0);
		} else {
			SendMessage(hOperCombo, CB_SETCURSEL, 0, 0);
		}

		if (RegQueryValueExW(hKey, L"Device", NULL, &type, (LPBYTE)&deviceValue, &size) == ERROR_SUCCESS) {
			int count = SendMessage(hDeviceCombo, CB_GETCOUNT, 0, 0);
			for (int i = 0; i < count; i++) {
				if ((int)SendMessage(hDeviceCombo, CB_GETITEMDATA, i, 0) == deviceValue) {
					SendMessage(hDeviceCombo, CB_SETCURSEL, i, 0);
					break;
				}
			}
		}

		if (RegQueryValueExW(hKey, L"ForwardType", NULL, &type, (LPBYTE)&forwardTypeValue, &size) == ERROR_SUCCESS && type == REG_DWORD) {
			SendMessage(hForwardCombo, CB_SETCURSEL, forwardTypeValue, 0);
		} else {
			SendMessage(hForwardCombo, CB_SETCURSEL, 0, 0);
		}

		if (RegQueryValueExW(hKey, L"Enable", NULL, &type, (LPBYTE)&enableValue, &size) == ERROR_SUCCESS && type == REG_DWORD) {
			CheckDlgButton(hDlg, IDC_ENABLE_CHECK, BST_CHECKED);
		} else {
			CheckDlgButton(hDlg, IDC_ENABLE_CHECK, BST_UNCHECKED);
		}

		if (modeValue == 1 || modeValue == 2) {
			wchar_t targetPort[32] = L"";
			DWORD portSize = sizeof(targetPort);
			if (RegQueryValueExW(hKey, L"TargetPort", NULL, NULL, (LPBYTE)targetPort, &portSize) == ERROR_SUCCESS) {
				int count = SendMessage(hPortCombo, CB_GETCOUNT, 0, 0);
				for (int i = 0; i < count; ++i) {
					wchar_t itemText[32];
					SendMessage(hPortCombo, CB_GETLBTEXT, i, (LPARAM)itemText);
					if (_wcsicmp(itemText, targetPort) == 0) {
						SendMessage(hPortCombo, CB_SETCURSEL, i, 0);
						break;
					}
				}
			}
			wchar_t targetAddress[256] = L"";
			DWORD addressSize = sizeof(targetAddress);
			if (RegQueryValueExW(hKey, L"TargetNetAddress", NULL, NULL, (LPBYTE)targetAddress, &addressSize) == ERROR_SUCCESS) {
				SetWindowTextW(hNetText, targetAddress);
			}
			wchar_t targetFile[260] = L"";
			DWORD fileSize = sizeof(targetFile);
			if (RegQueryValueExW(hKey, L"TargetPath", NULL, NULL, (LPBYTE)targetFile, &fileSize) == ERROR_SUCCESS) {
				SetWindowTextW(hFileText, targetFile);
			}
		}

		UpdateVisibility(hDlg);

		RegCloseKey(hKey);
	}
}

void UpdateVisibility(HWND hDlg) {
	HWND hModeCombo = GetDlgItem(hDlg, IDC_MODE);
	HWND hForwardLabel = GetDlgItem(hDlg, IDC_FORWARD_TARGET_LABEL);
	HWND hForwardCombo = GetDlgItem(hDlg, IDC_FORWARD_TARGET);
	HWND hTargetLabel = GetDlgItem(hDlg, IDC_TARGET_LABEL);
	HWND hPortCombo = GetDlgItem(hDlg, IDC_TARGET_PORT);
	HWND hNetText = GetDlgItem(hDlg, IDC_TARGET_ADDRESS);
	HWND hFileText = GetDlgItem(hDlg, IDC_TARGET_FILE);
	HWND hOperCombo = GetDlgItem(hDlg, IDC_OPERATION);
	HWND hOperText = GetDlgItem(hDlg, IDC_OPERATION_LABEL);
	HWND hDeviceCombo = GetDlgItem(hDlg, IDC_DEVICE);
	HWND hDeviceText = GetDlgItem(hDlg, IDC_DEVICE_LABEL);

	ShowWindow(hForwardLabel, SW_HIDE);
	ShowWindow(hForwardCombo, SW_HIDE);
	ShowWindow(hTargetLabel, SW_HIDE);
	ShowWindow(hPortCombo, SW_HIDE);
	ShowWindow(hNetText, SW_HIDE);
	ShowWindow(hFileText, SW_HIDE);
	ShowWindow(hOperCombo, SW_HIDE);
	ShowWindow(hOperText, SW_HIDE);
	ShowWindow(hDeviceCombo, SW_HIDE);
	ShowWindow(hDeviceText, SW_HIDE);

	int sel = SendMessage(hModeCombo, CB_GETCURSEL, 0, 0);
	switch (sel) {
		case 0:
			ShowWindow(hOperCombo, SW_SHOW);
			ShowWindow(hOperText, SW_SHOW);
			ShowWindow(hDeviceCombo, SW_SHOW);
			ShowWindow(hDeviceText, SW_SHOW);
			break;
		case 1: {
			ShowWindow(hForwardLabel, SW_SHOW);
			ShowWindow(hForwardCombo, SW_SHOW);
			ShowWindow(hTargetLabel, SW_SHOW);
			ShowWindow(hPortCombo, SW_HIDE);
			ShowWindow(hNetText, SW_HIDE);
			ShowWindow(hFileText, SW_HIDE);
			int selTarget = SendMessage(hForwardCombo, CB_GETCURSEL, 0, 0);
			if (selTarget == 2) {
				ShowWindow(hFileText, SW_SHOW);
			} else if (selTarget == 1) {
				ShowWindow(hNetText, SW_SHOW);
			} else {
				ShowWindow(hPortCombo, SW_SHOW);
			}
			break;
		}
		case 2:
		case 3:
			break;
		default:
			break;
	}
}

void SaveSettings(HWND hDlg, const std::wstring& currentPort) {
	HWND hModeCombo = GetDlgItem(hDlg, IDC_MODE);
	HWND hForwardCombo = GetDlgItem(hDlg, IDC_FORWARD_TARGET);
	HWND hPortCombo = GetDlgItem(hDlg, IDC_TARGET_PORT);
	HWND hNetText = GetDlgItem(hDlg, IDC_TARGET_ADDRESS);
	HWND hFileText = GetDlgItem(hDlg, IDC_TARGET_FILE);
	HWND hOperCombo = GetDlgItem(hDlg, IDC_OPERATION);
	HWND hDeviceCombo = GetDlgItem(hDlg, IDC_DEVICE);

	int modeSel = SendMessage(hModeCombo, CB_GETCURSEL, 0, 0);
	int operSel = SendMessage(hOperCombo, CB_GETCURSEL, 0, 0);
	int deviceSel = SendMessage(hDeviceCombo, CB_GETCURSEL, 0, 0);
	int forwardSel = SendMessage(hForwardCombo, CB_GETCURSEL, 0, 0);

	wchar_t targetPort[32] = L"";
	wchar_t targetAddress[256] = L"";
	wchar_t targetFile[260] = L"";
	if (modeSel == 1 || modeSel == 2) {
		int portSel = SendMessage(hPortCombo, CB_GETCURSEL, 0, 0);
		if (portSel != CB_ERR) {
			SendMessage(hPortCombo, CB_GETLBTEXT, portSel, (LPARAM)targetPort);
		}
		GetWindowTextW(hNetText, targetAddress, _countof(targetAddress));
		GetWindowTextW(hFileText, targetFile, _countof(targetFile));
	}

	std::wstring keyPath = L"SYSTEM\\CurrentControlSet\\Services\\FuvallatVirtualSerial\\Parameters\\" + currentPort;

	HKEY hKey;
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		DWORD modeVal = (DWORD)modeSel;
		RegSetValueExW(hKey, L"Mode", 0, REG_DWORD, (BYTE*)&modeVal, sizeof(modeVal));

		DWORD operVal = (DWORD)operSel;
		RegSetValueExW(hKey, L"Operation", 0, REG_DWORD, (BYTE*)&operVal, sizeof(operVal));

		DWORD deviceVal = (DWORD)SendMessage(hDeviceCombo, CB_GETITEMDATA, deviceSel, 0);
		RegSetValueExW(hKey, L"Device", 0, REG_DWORD, (BYTE*)&deviceVal, sizeof(deviceVal));

		DWORD forwardVal = (DWORD)forwardSel;
		RegSetValueExW(hKey, L"ForwardType", 0, REG_DWORD, (BYTE*)&forwardVal, sizeof(forwardVal));

		if (modeSel == 1 || modeSel == 2) {
			RegSetValueExW(hKey, L"TargetPort", 0, REG_SZ, (BYTE*)targetPort, (DWORD)((wcslen(targetPort) + 1) * sizeof(wchar_t)));
			RegSetValueExW(hKey, L"TargetNetAddress", 0, REG_SZ, (BYTE*)targetAddress, (DWORD)((wcslen(targetAddress) + 1) * sizeof(wchar_t)));
			RegSetValueExW(hKey, L"TargetPath", 0, REG_SZ, (BYTE*)targetFile, (DWORD)((wcslen(targetFile) + 1) * sizeof(wchar_t)));
		}

		DWORD enable = (IsDlgButtonChecked(hDlg, IDC_ENABLE_CHECK) == BST_CHECKED) ? 1 : 0;
		RegSetValueExW(hKey, L"Enable", 0, REG_DWORD, (const BYTE*)&enable, sizeof(enable));

		RegCloseKey(hKey);
	}
}

std::vector<std::wstring> EnumerateSerialPorts() {
	std::vector<std::wstring> ports;
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		DWORD index = 0;
		wchar_t valueName[256];
		BYTE data[1024];
		DWORD valueSize, dataSize, type;

		while (true) {
			valueSize = sizeof(valueName);
			dataSize = sizeof(data);
			if (RegEnumValueW(hKey, index++, valueName, &valueSize, NULL, &type, data, &dataSize) != ERROR_SUCCESS) break;

			if (type == REG_SZ) {
				ports.push_back(std::wstring((wchar_t*)data));
			}
		}
		RegCloseKey(hKey);
	}
	return ports;
}

BOOL GetComPortSettings(const char* port, char* outText, size_t outSize) {
	char path[64];
	snprintf(path, sizeof(path), "\\\\.\\%hs", port);

	HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

	if (h == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	DCB dcb = { 0 };
	dcb.DCBlength = sizeof(DCB);

	if (!GetCommState(h, &dcb)) {
		CloseHandle(h);
		return FALSE;
	}

	CloseHandle(h);

	char parityChar = 'N';
	switch (dcb.Parity) {
		case EVENPARITY: parityChar = 'E'; break;
		case ODDPARITY:  parityChar = 'O'; break;
		case MARKPARITY: parityChar = 'M'; break;
		case SPACEPARITY:parityChar = 'S'; break;
		default:         parityChar = 'N'; break;
	}

	char stopChar = '1';
	if (dcb.StopBits == ONE5STOPBITS) stopChar = '5';
	else if (dcb.StopBits == TWOSTOPBITS) stopChar = '2';

	if (dcb.BaudRate == 0) {
		snprintf(outText, outSize, "%hs", port);
	} else {
		snprintf(outText, outSize, "%hs � %lu %d%c%c", port, dcb.BaudRate, dcb.ByteSize, parityChar, stopChar);
	}

	return TRUE;
}