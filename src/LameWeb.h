#pragma once

#include <Windows.h>

#define IDT_HOME_LOAD_TIMEOUT  0x4A12
#define IDT_WEB_REFIT_RESIZE   0x4A13

#ifdef __cplusplus
extern "C" {
#endif

	void Web_Init(HWND hwndMain, HWND hWebHost);
	void Web_Shutdown(void);

	void Web_SetVisible(BOOL show);
	BOOL Web_IsVisible(void);

	void Web_ShowHomePage(void);

	// If `openLinksInApp` is TRUE, links that request a new window (target=_blank, window.open)
	// will be navigated in the existing WebView. If FALSE, they will be allowed to open externally.
	void Web_ShowPage(const wchar_t* title, const wchar_t* url, BOOL openLinksInApp);

	// Call from WM_SIZE after `g_ui.hWebHost` is moved/resized.
	void Web_OnHostResized(void);

	// Call from WM_TIMER when IDT_HOME_LOAD_TIMEOUT fires.
	void Web_OnHomeLoadTimeout(void);

	BOOL Web_OnTimer(HWND hWnd, WPARAM wParam);

	void Web_PostJson(const wchar_t* json);

#ifdef __cplusplus
}
#endif

// Back-compat convenience: default to in-app navigation.
// (Macro avoids C-linkage overloading issues.)
#define Web_ShowPageDefault(title, url) Web_ShowPage((title), (url), TRUE)