// LameSplitter.h - Splitter (tree/list) resizing logic extracted from LameUI.cpp

#pragma once

#include "LameWin.h"

void Splitter_Init(int initialTreeWidth);

int Splitter_GetTreeWidth(void);

BOOL Splitter_OnSetCursor(HWND hWnd);
BOOL Splitter_OnLButtonDown(HWND hWnd, LPARAM lParam);
void Splitter_OnMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam);
void Splitter_OnLButtonUp(HWND hWnd);
BOOL Splitter_IsDragging(void);

// Used by layout + min-size calculations
int Splitter_GetMinLeftWidth(void);
int Splitter_GetBarWidth(void);
int Splitter_GetRightMinMargin(void);