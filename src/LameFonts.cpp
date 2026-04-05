#include "LameFonts.h"

static HFONT g_hListFont = NULL;
static HFONT g_hTreeFont = NULL;

HFONT UI_GetListFont(void)
{
    return g_hListFont;
}

HFONT UI_GetTreeFont(void)
{
    return g_hTreeFont;
}

HFONT UI_GetDefaultGuiFont(void)
{
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

void UI_SetListFont(HFONT hFont)
{
    g_hListFont = hFont;
}

void UI_SetTreeFont(HFONT hFont)
{
    g_hTreeFont = hFont;
}

HFONT CreateListFont(int pointSize, BOOL bold)
{
    HDC hdc = GetDC(NULL);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    int height = -MulDiv(pointSize, logPixelsY, 72);

    return CreateFontW(
        height,
        0,
        0,
        0,
        bold ? FW_BOLD : FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"MS Sans Serif"
    );
}

HFONT CreateTreeFont(int pointSize, BOOL bold)
{
    HDC hdc = GetDC(NULL);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    int height = -MulDiv(pointSize, logPixelsY, 72);

    return CreateFontW(
        height,
        0,
        0,
        0,
        bold ? FW_BOLD : FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Tahoma"
    );
}