// LameToolbar.h - Toolbar creation + tooltip text

#pragma once

#include "LameWin.h"

void Toolbar_Create(HWND hWndParent);

// Returns non-zero if handled (i.e. hdr->code == TTN_GETDISPINFO* and id matches).
LRESULT Toolbar_OnNotifyTooltip(const NMHDR* hdr, LPARAM lParam);

// Call during shutdown to release imagelists, etc.
void Toolbar_Shutdown(void);