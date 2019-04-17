/*
UserinfoEx plugin for Miranda IM

Copyright:
� 2006-2010 DeathAxe, Yasnovidyashii, Merlin, K. Romanov, Kreol

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "stdafx.h"

static HGENMENU ghMenuItem = nullptr;
static HANDLE ghExtraIconDef = INVALID_HANDLE_VALUE;
static HANDLE ghExtraIconSvc = INVALID_HANDLE_VALUE;

static HANDLE hChangedHook = nullptr;
static HANDLE hApplyIconHook = nullptr;

bool g_eiEmail = false;

/**
* This function reads the email address of the contact.
*
* @param	hContact		- handle to contact to read email from
*
* @retval	email address
* @retval	NULL, if contact does not provide any email address
**/

static LPSTR Get(MCONTACT hContact)
{
	// ignore owner
	if (hContact != NULL) {
		LPCSTR pszProto = Proto_GetBaseAccountName(hContact);
		
		if (pszProto != nullptr) {
			LPCSTR e[2][4] = {
				{ SET_CONTACT_EMAIL,			SET_CONTACT_EMAIL0,			SET_CONTACT_EMAIL1,			"Mye-mail0"},
				{ SET_CONTACT_COMPANY_EMAIL,	SET_CONTACT_COMPANY_EMAIL0,	SET_CONTACT_COMPANY_EMAIL1,	"MyCompanye-mail0"}
			};

			for (int i = 0; i < 2; i++) {
				for (int j = 0; j < 4; j++) {
					LPSTR pszEMail = DB::Setting::GetAStringEx(hContact, USERINFO, pszProto, e[i][j]);
					if (pszEMail) {
						if (strchr(pszEMail, '@'))
							return pszEMail;

						mir_free(pszEMail);
					}
				}
			}
		}
	}
	return nullptr;
}

/**
* Service function that sends emails
*
* @param	wParam			- handle to contact to send an email to
* @param	lParam			- not used
*
* @retval	0 if email was sent
* @retval	1 if no email can be sent
**/

static INT_PTR MenuCommand(WPARAM wParam,LPARAM lParam)
{
	int result = 0;
	LPSTR val = nullptr;

	__try 
	{
		val = Get(wParam);
		if (val) {
			LPSTR szUrl;
			INT_PTR len;

			len = mir_strlen(val) + mir_strlen("mailto:");

			szUrl = (LPSTR)_alloca(len + 1);

			mir_snprintf(szUrl, len + 1, "mailto:%s", val);
			mir_free(val);

			Utils_OpenUrl(szUrl);
		}
		else {
			result = 1;
			MsgBox((HWND)lParam, MB_OK, LPGENW("Send e-mail"), nullptr, LPGENW("Memory allocation error!"));
		}
	}
	__except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? 
		EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) 
	{
		mir_free(val);
		result = 1;
		MsgErr((HWND)lParam, LPGENW("Memory allocation error!"));
	}
	return result;
}

/***********************************************************************************************************
 * Event Handler functions
 ***********************************************************************************************************/

/**
* Notification handler for clist extra icons to be applied for a contact.
*
* @param	wParam			- handle to the contact whose extra icon is to apply
* @param	lParam			- not used
**/

static int OnCListApplyIcons(WPARAM wParam, LPARAM)
{
	LPSTR val = Get(wParam);
	ExtraIcon_SetIcon(ghExtraIconSvc, wParam, (val) ? g_plugin.getIconHandle(IDI_BTN_EMAIL) : nullptr);
	mir_free(val);
	return 0;
}

/**
* Notification handler for changed contact settings
*
* @param	wParam			- (HANDLE)hContact
* @param	lParam			- (DBCONTACTWRITESETTING*)pdbcws
**/

static int OnContactSettingChanged(MCONTACT hContact, DBCONTACTWRITESETTING* pdbcws)
{
	if (hContact && pdbcws && pdbcws->szSetting && 
			((pdbcws->value.type & DBVTF_VARIABLELENGTH) || (pdbcws->value.type == DBVT_DELETED)) &&
			(!strncmp(pdbcws->szSetting, SET_CONTACT_EMAIL, 6) ||
			 !strncmp(pdbcws->szSetting, SET_CONTACT_COMPANY_EMAIL, 13) ||
			 !strncmp(pdbcws->szSetting, "mye-mail0", 9)))
	{
		OnCListApplyIcons(hContact, 0);
	}
	return 0;
}

/**
* This function decides whether to show menuitem for sending emails or not.
*
* @param	wParam			- handle to contact to send an email to
* @param	lParam			- not used
*
* @return	always 0
**/

static int OnPreBuildMenu(WPARAM wParam, LPARAM)
{
	LPSTR val = Get(wParam);
	Menu_ShowItem(ghMenuItem, val != nullptr);
	mir_free(val);
	return 0;
}

/***********************************************************************************************************
 * public Module Interface functions
 ***********************************************************************************************************/

void SvcEMailRebuildMenu()
{
	static HANDLE hPrebuildMenuHook = nullptr;

	if (g_plugin.getByte(SET_EXTENDED_EMAILSERVICE, TRUE)) {
		if (!hPrebuildMenuHook) 
			hPrebuildMenuHook = HookEvent(ME_CLIST_PREBUILDCONTACTMENU, OnPreBuildMenu);

		if (!ghMenuItem) {
			// insert contact menuitem
			CMenuItem mi(&g_plugin);
			SET_UID(mi, 0x61d8e25a, 0x92e, 0x4470, 0x84, 0x57, 0x5e, 0x52, 0x17, 0x7f, 0xfa, 0x3);
			mi.position = -2000010000;
			mi.hIcolibItem = g_plugin.getIconHandle(IDI_DLG_EMAIL);
			mi.name.a = "&E-mail";
			mi.pszService = MS_EMAIL_SENDEMAIL;
			ghMenuItem = Menu_AddContactMenuItem(&mi);
		}
	}
	else {
		if (hPrebuildMenuHook)
			UnhookEvent(ME_CLIST_PREBUILDCONTACTMENU), hPrebuildMenuHook = NULL;

		if (ghMenuItem) {
			Menu_RemoveItem(ghMenuItem);
			ghMenuItem = nullptr;
		}
	}
}

/**
* Enable or disable the replacement of clist extra icons.
*
* @param	bEnable			- determines whether icons are enabled or not
* @param	bUpdateDB		- if true the database setting is updated, too.
**/

bool SvcEMailEnableExtraIcons(bool bEnable, bool bUpdateDB)
{
	bool bChanged;

	if (bUpdateDB) {
		bChanged = g_eiEmail != bEnable;
		g_plugin.setByte(SET_CLIST_EXTRAICON_EMAIL, g_eiEmail = bEnable);
	}
	else bChanged = g_eiEmail = g_plugin.getByte(SET_CLIST_EXTRAICON_EMAIL, DEFVAL_CLIST_EXTRAICON_EMAIL) != 0;

	if (g_eiEmail) { // E-mail checked
		// hook events
		if (hChangedHook == nullptr) 
			hChangedHook = HookEvent(ME_DB_CONTACT_SETTINGCHANGED, (MIRANDAHOOK)OnContactSettingChanged);

		if (hApplyIconHook == nullptr) 
			hApplyIconHook = HookEvent(ME_CLIST_EXTRA_IMAGE_APPLY, OnCListApplyIcons);

		if (ghExtraIconSvc == INVALID_HANDLE_VALUE)
			ghExtraIconSvc = ExtraIcon_RegisterIcolib("email", LPGEN("E-mail (UInfoEx)"), g_plugin.getIconHandle(IDI_BTN_EMAIL));
	}
	else { // E-mail uncheckt
		if (hChangedHook) {
			UnhookEvent(hChangedHook); 
			hChangedHook = nullptr;
		}			
		if (hApplyIconHook) {
			UnhookEvent(hApplyIconHook); 
			hApplyIconHook = nullptr;
		}			
	}

	return bChanged;
}

/**
* This function initially loads the module upon startup.
**/

void SvcEMailLoadModule()
{
	SvcEMailEnableExtraIcons();
	if (g_plugin.getByte(SET_EXTENDED_EMAILSERVICE, TRUE)) {
		// create own email send command
		DestroyServiceFunction(MS_EMAIL_SENDEMAIL);
		CreateServiceFunction(MS_EMAIL_SENDEMAIL, MenuCommand);
	}
}

/**
* This function unloads the Email module.
*
* @param	none
*
* @return	nothing
**/

void SvcEMailUnloadModule()
{	
	// unhook event handlers
	UnhookEvent(hChangedHook); hChangedHook = nullptr;
	UnhookEvent(hApplyIconHook); hApplyIconHook = nullptr;
}
