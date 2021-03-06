#include "stdafx.h"

void CShake::Load(void)
{
	Shaking = false;
	ShakingChat = false;
	nScaleClist = g_plugin.getDword("ScaleClist", 5);
	nScaleChat = g_plugin.getDword("ScaleChat", 2);
	nMoveClist = g_plugin.getDword("MoveClist", 15);
	nMoveChat = g_plugin.getDword("MoveChat", 15);
}
void CShake::Save(void)
{
	g_plugin.setDword("ScaleClist", this->nScaleClist);
	g_plugin.setDword("ScaleChat", this->nScaleChat);
	g_plugin.setDword("MoveClist", this->nMoveClist);
	g_plugin.setDword("MoveChat", this->nMoveChat);
}

void __cdecl ShakeChatWindow(void *Param)
{
	HWND hWnd = (HWND)Param;
	shake.ShakeChat(hWnd);
}

void __cdecl ShakeClistWindow(void *Param)
{
	HWND hWnd = (HWND)Param;
	shake.ShakeClist(hWnd);
}

INT_PTR ShakeClist(WPARAM, LPARAM)
{
	mir_forkthread(ShakeClistWindow, (void*)g_clistApi.hwndContactList);
	return 0;
}

INT_PTR ShakeChat(WPARAM wParam, LPARAM)
{
	if (((HANDLE)wParam) == nullptr) return -1;

	MessageWindowData mwd;
	Srmm_GetWindowData(db_mc_tryMeta(wParam), mwd);

	HWND parent;
	HWND hWnd = mwd.hwndWindow;
	while ((parent = GetParent(hWnd)) != nullptr) hWnd = parent; // ensure we have the top level window (need parent window for scriver & tabsrmm)

	mir_forkthread(ShakeChatWindow, (void*)hWnd);
	return 0;
}

int CShake::ShakeChat(HWND hWnd)
{
	if (!ShakingChat) {
		ShakingChat = true;
		RECT rect;
		GetWindowRect(hWnd, &rect);
		for (int i = 0; i < nMoveChat; i++) {
			SetWindowPos(hWnd, nullptr, rect.left - nScaleChat, rect.top, 0, 0, SWP_NOSIZE);
			Sleep(10);
			SetWindowPos(hWnd, nullptr, rect.left, rect.top - nScaleChat, 0, 0, SWP_NOSIZE);
			Sleep(10);
			SetWindowPos(hWnd, nullptr, rect.left + nScaleChat, rect.top, 0, 0, SWP_NOSIZE);
			Sleep(10);
			SetWindowPos(hWnd, nullptr, rect.left, rect.top + nScaleChat, 0, 0, SWP_NOSIZE);
			Sleep(10);
		}
		SetWindowPos(hWnd, nullptr, rect.left, rect.top, 0, 0, SWP_NOSIZE); //SWP_DRAWFRAME
		ShakingChat = false;
	}
	return 0;
}

int CShake::ShakeClist(HWND hWnd)
{
	if (!Shaking) {
		Shaking = true;
		RECT rect;
		GetWindowRect(hWnd, &rect);
		for (int i = 0; i < nMoveClist; i++) {
			SetWindowPos(hWnd, nullptr, rect.left - nScaleClist, rect.top, 0, 0, SWP_NOSIZE);
			Sleep(10);
			SetWindowPos(hWnd, nullptr, rect.left, rect.top - nScaleClist, 0, 0, SWP_NOSIZE);
			Sleep(10);
			SetWindowPos(hWnd, nullptr, rect.left + nScaleClist, rect.top, 0, 0, SWP_NOSIZE);
			Sleep(10);
			SetWindowPos(hWnd, nullptr, rect.left, rect.top + nScaleClist, 0, 0, SWP_NOSIZE);
			Sleep(10);
		}
		SetWindowPos(hWnd, nullptr, rect.left, rect.top, 0, 0, SWP_NOSIZE);
		Shaking = false;
	}
	return 0;
}
