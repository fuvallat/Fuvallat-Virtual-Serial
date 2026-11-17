#pragma once

#include <SetupAPI.h>

typedef struct _PAGE_CONTEXT {
	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA devInfo;
	WCHAR PortName[64];
	WCHAR InstanceId[256];
} PAGE_CONTEXT;