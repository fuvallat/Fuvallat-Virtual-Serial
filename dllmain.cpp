#include <Windows.h>

HINSTANCE g_hInstance = NULL;

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH) g_hInstance = hinstDLL;

	return TRUE;
}