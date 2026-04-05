#pragma once

#include <windows.h>
#include "LameCore.h"

// Creates the Win32 status bar and its child progress control.
// Writes handles into `g_ui.hStatusBar` and `g_ui.hStatusProgress`.
void CreateStatusBar(HWND hWndParent);

// Lays out the status bar parts and positions the progress control.
// Call after resize / when the status bar width changes.
void StatusBar_LayoutPartsAndProgress(void);

// Status text API
void StatusText(const wchar_t* text);
void StatusTextFmt(const wchar_t* fmt, ...);
void StatusTextPriority(StatusPriority priority, const wchar_t* text);
void StatusTextFmtPriority(StatusPriority priority, const wchar_t* fmt, ...);
void StatusReset(void);

// Marquee progress API (shown during long operations)
void StatusProgress_Begin(void);
void StatusProgress_End(void);

// Keep marquee correct even if other UI code calls End()
void StatusProgress_SyncToBackgroundActivity(void);