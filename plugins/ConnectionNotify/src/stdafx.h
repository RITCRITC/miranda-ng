#pragma once

#include <windows.h>
#include <Commctrl.h>
#include <assert.h>
#include <iphlpapi.h>
#include <Tlhelp32.h>

#include <newpluginapi.h>
#include <m_core.h>
#include <m_clistint.h>
#include <m_skin.h>
#include <m_langpack.h>
#include <m_options.h>
#include <m_popup.h>
#include <m_utils.h>
#include <m_protosvc.h>
#include <m_system.h>

#ifdef _DEBUG
#include "debug.h"
#endif
#include "resource.h"
#include "netstat.h"
#include "filter.h"
#include "version.h"
#include "pid2name.h"

#define MAX_SETTING_STR 512
#define MAX_LENGTH 512
#define STATUS_COUNT 9

// Note: could also use malloc() and free()
#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

#define PLUGINNAME "ConnectionNotify"
#define PLUGINNAME_NEWSOUND PLUGINNAME "_new_sound"

struct CMPlugin : public PLUGIN<CMPlugin>
{
	CMPlugin();

	int Load() override;
	int Unload() override;
};

void showMsg(wchar_t *pName,DWORD pid,wchar_t *intIp,wchar_t *extIp,int intPort,int extPort,int state);
static unsigned __stdcall checkthread(void *dummy);
struct CONNECTION * LoadSettingsConnections();
void saveSettingsConnections(struct CONNECTION *connHead);
