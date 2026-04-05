// LameWin.h

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

// Messages for communicating query results from worker threads back to the UI thread
#define WM_APP_QUERY_RESULT   (WM_APP + 1)
#define WM_APP_QUERY_DONE     (WM_APP + 2)
#define WM_APP_MASTER_DONE    (WM_APP + 3) 
#define WM_APP_STARTUP_QUERY  (WM_APP + 4)
#define WM_APP_QUERY_FLUSH    (WM_APP + 5)
#define WM_APP_VERSION_CHECK_RESULT (WM_APP + 6)
#define WM_APP_SHOW_HOME_STARTUP   (WM_APP + 41)  // does the number matter, as long as they're different?
#define WM_APP_WEB_BROWSE_EXE (WM_APP + 220)

// UI dialog procedures
INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AddFavoriteDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ChatWindowDlg(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK BuddyListDlg(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK GameConfigDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProfileDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK GameToggleDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);