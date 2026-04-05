#pragma once

#include <windows.h>

// Filters lifetime/state
void Filters_ResetDefaults(void);
BOOL Filters_AreVisible(void);

// Filters UI (top bar)
void CreateTopFilters(HWND hWndParent);
void LayoutTopFilters(HWND hWndParent, int clientW);
void UI_ShowTopFilters(HWND hWnd, BOOL show);

// Pull current UI state into the active filter state
void UI_SaveFiltersFromTopControls(void);

// new
int UI_ServerPassesFilters(const LameServer* s);
void Filters_OnMainWindowResized(HWND hWndParent);