#pragma once

#include <Windows.h>

#include "LameCore.h"

void HomeView_Init(HWND hwndMain, HINSTANCE hInst, HWND hwndParent);
void HomeView_Shutdown(void);

void HomeView_SetVisible(BOOL show);
BOOL HomeView_IsVisible(void);

void HomeView_ShowHomePage(void);
void HomeView_ShowPage(const wchar_t* title, const wchar_t* url);

// Layout: call from WM_SIZE after computing the right-pane rectangle for Home.
void HomeView_MoveHost(int x, int y, int w, int h);

// Owns switching the UI between Home(Web) mode and List mode.
// When `showHome` is TRUE: show Web + hide list panes + hide filters.
// When `showHome` is FALSE: hide Web + show list panes + show filters if enabled.
void HomeView_SetMode(BOOL showHome);

// "All Favorites" combined master (owned by HomeView module)
void HomeView_BuildHomeFavoritesMaster(void);
LameMaster* HomeView_GetHomeFavoritesMaster(void);

void HomeView_KillTimers(void);

BOOL HomeView_OnTimer(HWND hWnd, WPARAM wParam);