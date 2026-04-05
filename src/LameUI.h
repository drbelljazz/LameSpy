// LameUI.h - Main UI header for LameUI.cpp

#pragma once

#include "LameWin.h"

typedef struct LameUI
{
    HINSTANCE hInst;
    HWND      hwndMain;

    HWND      hToolBar;

    HWND      hTreeMenu;
    HWND      hServerList;
    HWND      hPlayerList;
    HWND      hRulesList;

    HWND      hStatusBar;
    HWND      hStatusProgress;

    HWND      hFilterSearch;
    HWND      hFilterField;
    HWND      hFilterShowFull;
    HWND      hFilterShowEmpty;
    HWND      hFilterShowPassword;    

} LameUI;

extern LameUI g_ui;

typedef enum TreeNodeKind
{
    TREE_NODE_NONE = 0,
    TREE_NODE_GAME = 1,
    TREE_NODE_MASTER = 2,
    TREE_NODE_FAVORITES = 3,      // per-game favorites
    TREE_NODE_HOME = 4,

    TREE_NODE_INTERNET = 5,
    TREE_NODE_LAN = 6,

    TREE_NODE_HOME_FAVORITES = 7, // combined favorites from all games
    TREE_NODE_HOME_GAMEDATE = 8,
    TREE_NODE_HOME_TUTORIALS = 9,
    TREE_NODE_HOME_ARCHIVES = 10,
    TREE_NODE_HOME_MUSIC = 11

} TreeNodeKind;

enum GameIcons
{
    ICON_Q3 = 0,
    ICON_Q2 = 1,
    ICON_QW = 2,
    ICON_UT99 = 3,
    ICON_UG = 4,
    ICON_DX = 5,
    ICON_GS = 6,

    ICON_GAME_MAX
};

// WndProc handlers
void CreateLameWindow(HWND hWnd);
void ResizeLameWindow(HWND hWnd, LPARAM lParam);
LRESULT NotifyLameWindow(LPARAM lParam);
void LameMenus(HWND hWnd, WPARAM wParam, LPARAM lParam);
void KillLameWindow();

// Resizing column between tree and list
BOOL LameUI_OnSetCursor(HWND hWnd);
BOOL LameUI_OnLButtonDown(HWND hWnd, LPARAM lParam);
void LameUI_OnMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam);
void LameUI_OnLButtonUp(HWND hWnd);
BOOL LameUI_IsSplitterDragging(void);

// UI elements
void DrawTopLine(HWND hWnd, HDC hdc);

// Exported for LameDialogs.cpp
void UI_BuildTree(void);
void UI_RefreshCurrentView(void);
void SetDialogIcon(int resourceId, HWND hDlg);  // Now located in LameDialogs.cpp

// For UI messages (e.g. from worker threads)
void LameUI_OnAppMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Sounds
void PlayLameSound(int resourceId, int allow);

// Misc
void SetMainIcon(WNDCLASSEXW* wc, HINSTANCE hInst);
void UI_DumpActiveMaster(void);
void FetchOnStartup();

BOOL UI_GetSuppressTreeSelChanged(void);
void UI_SetSuppressTreeSelChanged(BOOL suppress);

void LameUI_OnGetMinMaxInfo(HWND hWnd, MINMAXINFO* mmi);

// Context menus
void ShowTreeContextMenu(HWND hWnd, HWND hTree);
void ShowListViewColumnMenu(HWND hWnd, HWND hList);
void ShowServerContextMenu(HWND hWnd, HWND hList);

// Timers (used for smooth batched UI updates during mass queries)
void LameUI_OnTimer(HWND hWnd, WPARAM wParam);

BOOL Web_OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam);

void UI_RefreshActiveTreeSelection(void);

void UI_RequestManualVersionCheck(void);

// Control IDs for filter controls
#define IDC_FILTER_SEARCH        5101
#define IDC_FILTER_FIELD         5102
#define IDC_FILTER_SHOW_FULL     5103
#define IDC_FILTER_SHOW_EMPTY    5104
#define IDC_FILTER_SHOW_PASSWORD 5105
