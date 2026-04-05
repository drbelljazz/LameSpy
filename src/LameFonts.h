#pragma once

#include <Windows.h>

HFONT UI_GetListFont(void);
HFONT UI_GetTreeFont(void);
HFONT UI_GetDefaultGuiFont(void);

HFONT CreateListFont(int pointSize, BOOL bold);
HFONT CreateTreeFont(int pointSize, BOOL bold);

void UI_SetListFont(HFONT hFont);
void UI_SetTreeFont(HFONT hFont);