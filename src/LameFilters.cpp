#include <windows.h>
#include <wctype.h>
#include <CommCtrl.h>
#include "LameCore.h"
#include "LameUI.h"
#include "LameFonts.h"
#include "LameFilters.h"

// Filter controls
static BOOL g_filtersVisible = TRUE;

typedef enum FilterField
{
    FILTERFIELD_HOSTNAME = 0,
    FILTERFIELD_MAP = 1,
    FILTERFIELD_PLAYER = 2,
    FILTERFIELD_GAMETYPE = 3,
    FILTERFIELD_IP = 4,
    FILTERFIELD_MAX
} FilterField;

typedef struct LameFilters
{
    wchar_t     search[256];   // user text
    FilterField field;         // combo selection
    BOOL        showEmpty;
    BOOL        showFull;
    BOOL        showPassword;
} LameFilters;

static LameFilters g_filters = { 0 };

static int UI_MeasureTextWidthPx(HWND hRefWnd, HFONT hFont, const wchar_t* text)
{
    HDC hdc;
    HFONT hOld;
    SIZE sz = {};
    int w;

    if (!text || !text[0])
        return 0;

    hdc = GetDC(hRefWnd ? hRefWnd : NULL);
    if (!hdc)
        return 0;

    if (!hFont)
        hFont = UI_GetDefaultGuiFont();

    hOld = (HFONT)SelectObject(hdc, hFont);
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    SelectObject(hdc, hOld);
    ReleaseDC(hRefWnd ? hRefWnd : NULL, hdc);

    w = sz.cx;
    return (w > 0) ? w : 0;
}

static int UI_CalcCheckboxWidth(HWND hCheck, int minWidth)
{
    wchar_t txt[128] = {};
    HFONT hFont;
    int textW;
    int glyphW;
    int w;

    if (!hCheck)
        return minWidth;

    GetWindowTextW(hCheck, txt, (int)_countof(txt));
    hFont = (HFONT)SendMessageW(hCheck, WM_GETFONT, 0, 0);

    textW = UI_MeasureTextWidthPx(hCheck, hFont, txt);
    glyphW = GetSystemMetrics(SM_CXMENUCHECK);

    // glyph + spacing + text + right padding
    w = glyphW + 10 + textW + 10;
    if (w < minWidth)
        w = minWidth;

    return w;
}

static int UI_CalcComboWidth(HWND hCombo, int minWidth)
{
    HFONT hFont;
    int count;
    int maxTextW = 0;
    int i;

    if (!hCombo)
        return minWidth;

    hFont = (HFONT)SendMessageW(hCombo, WM_GETFONT, 0, 0);
    count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);

    for (i = 0; i < count; i++)
    {
        int len = (int)SendMessageW(hCombo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (len <= 0 || len > 255)
            continue;

        wchar_t buf[256] = {};
        SendMessageW(hCombo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);

        int w = UI_MeasureTextWidthPx(hCombo, hFont, buf);
        if (w > maxTextW)
            maxTextW = w;
    }

    // tighter than before: less extra right-side padding
    int comboW = maxTextW + 12 + GetSystemMetrics(SM_CXVSCROLL);
    if (comboW < minWidth)
        comboW = minWidth;

    return comboW;
}

BOOL Filters_AreVisible(void)
{
    return g_filtersVisible;
}

void Filters_ResetDefaults(void)
{
    g_filters.search[0] = 0;
    g_filters.field = FILTERFIELD_HOSTNAME;
    g_filters.showEmpty = TRUE;
    g_filters.showFull = TRUE;
    g_filters.showPassword = TRUE;

    g_filtersVisible = TRUE;
}

static void UI_LoadFiltersIntoTopControls(void)
{
    int count;
    int idx;

    if (g_ui.hFilterSearch)
        SetWindowTextW(g_ui.hFilterSearch, g_filters.search);

    if (g_ui.hFilterShowEmpty)
        SendMessageW(g_ui.hFilterShowEmpty, BM_SETCHECK,
            g_filters.showEmpty ? BST_CHECKED : BST_UNCHECKED, 0);

    if (g_ui.hFilterShowFull)
        SendMessageW(g_ui.hFilterShowFull, BM_SETCHECK,
            g_filters.showFull ? BST_CHECKED : BST_UNCHECKED, 0);

    if (g_ui.hFilterShowPassword)
        SendMessageW(g_ui.hFilterShowPassword, BM_SETCHECK,
            g_filters.showPassword ? BST_CHECKED : BST_UNCHECKED, 0);

    if (!g_ui.hFilterField)
        return;

    count = (int)SendMessageW(g_ui.hFilterField, CB_GETCOUNT, 0, 0);
    for (idx = 0; idx < count; idx++)
    {
        LRESULT data = SendMessageW(g_ui.hFilterField, CB_GETITEMDATA, (WPARAM)idx, 0);
        if ((FilterField)(int)data == g_filters.field)
        {
            SendMessageW(g_ui.hFilterField, CB_SETCURSEL, (WPARAM)idx, 0);
            return;
        }
    }

    SendMessageW(g_ui.hFilterField, CB_SETCURSEL, 0, 0);
}

void UI_SaveFiltersFromTopControls(void)
{
    int sel;
    LRESULT data;

    if (g_ui.hFilterSearch)
        GetWindowTextW(g_ui.hFilterSearch, g_filters.search, (int)_countof(g_filters.search));

    if (g_ui.hFilterShowEmpty)
        g_filters.showEmpty =
        (SendMessageW(g_ui.hFilterShowEmpty, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (g_ui.hFilterShowFull)
        g_filters.showFull =
        (SendMessageW(g_ui.hFilterShowFull, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (g_ui.hFilterShowPassword)
        g_filters.showPassword =
        (SendMessageW(g_ui.hFilterShowPassword, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (!g_ui.hFilterField)
        return;

    sel = (int)SendMessageW(g_ui.hFilterField, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR)
    {
        data = SendMessageW(g_ui.hFilterField, CB_GETITEMDATA, (WPARAM)sel, 0);
        if (data != CB_ERR)
            g_filters.field = (FilterField)(int)data;
    }
}


static void UI_BringTopFiltersToFront(void)
{
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;

    if (g_ui.hFilterSearch)
        SetWindowPos(g_ui.hFilterSearch, HWND_TOP, 0, 0, 0, 0, flags);

    if (g_ui.hFilterField)
        SetWindowPos(g_ui.hFilterField, HWND_TOP, 0, 0, 0, 0, flags);

    if (g_ui.hFilterShowFull)
        SetWindowPos(g_ui.hFilterShowFull, HWND_TOP, 0, 0, 0, 0, flags);

    if (g_ui.hFilterShowEmpty)
        SetWindowPos(g_ui.hFilterShowEmpty, HWND_TOP, 0, 0, 0, 0, flags);

    if (g_ui.hFilterShowPassword)
        SetWindowPos(g_ui.hFilterShowPassword, HWND_TOP, 0, 0, 0, 0, flags);
}

void CreateTopFilters(HWND hWndParent)
{
    HFONT hFont;

    g_ui.hFilterSearch = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        hWndParent,
        (HMENU)IDC_FILTER_SEARCH,
        g_ui.hInst,
        NULL
    );

    g_ui.hFilterField = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"COMBOBOX",
        NULL,
        WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 0, 200,
        hWndParent,
        (HMENU)IDC_FILTER_FIELD,
        g_ui.hInst,
        NULL
    );

    g_ui.hFilterShowFull = CreateWindowExW(
        0, L"BUTTON", L"Show Full",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        hWndParent,
        (HMENU)IDC_FILTER_SHOW_FULL,
        g_ui.hInst,
        NULL
    );

    g_ui.hFilterShowEmpty = CreateWindowExW(
        0, L"BUTTON", L"Show Empty",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        hWndParent,
        (HMENU)IDC_FILTER_SHOW_EMPTY,
        g_ui.hInst,
        NULL
    );

    g_ui.hFilterShowPassword = CreateWindowExW(
        0, L"BUTTON", L"Show Locked",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        hWndParent,
        (HMENU)IDC_FILTER_SHOW_PASSWORD,
        g_ui.hInst,
        NULL
    );

    if (g_ui.hFilterField)
    {
        int i;

        SendMessageW(g_ui.hFilterField, CB_RESETCONTENT, 0, 0);

        i = (int)SendMessageW(g_ui.hFilterField, CB_ADDSTRING, 0, (LPARAM)L"Hostname");
        if (i >= 0) SendMessageW(g_ui.hFilterField, CB_SETITEMDATA, (WPARAM)i, (LPARAM)FILTERFIELD_HOSTNAME);

        i = (int)SendMessageW(g_ui.hFilterField, CB_ADDSTRING, 0, (LPARAM)L"Map");
        if (i >= 0) SendMessageW(g_ui.hFilterField, CB_SETITEMDATA, (WPARAM)i, (LPARAM)FILTERFIELD_MAP);

        i = (int)SendMessageW(g_ui.hFilterField, CB_ADDSTRING, 0, (LPARAM)L"Player");
        if (i >= 0) SendMessageW(g_ui.hFilterField, CB_SETITEMDATA, (WPARAM)i, (LPARAM)FILTERFIELD_PLAYER);

        i = (int)SendMessageW(g_ui.hFilterField, CB_ADDSTRING, 0, (LPARAM)L"Gametype");
        if (i >= 0) SendMessageW(g_ui.hFilterField, CB_SETITEMDATA, (WPARAM)i, (LPARAM)FILTERFIELD_GAMETYPE);

        i = (int)SendMessageW(g_ui.hFilterField, CB_ADDSTRING, 0, (LPARAM)L"IP");
        if (i >= 0) SendMessageW(g_ui.hFilterField, CB_SETITEMDATA, (WPARAM)i, (LPARAM)FILTERFIELD_IP);
    }

    hFont = UI_GetListFont() ? UI_GetListFont() : UI_GetDefaultGuiFont();

    if (g_ui.hFilterSearch)       SendMessageW(g_ui.hFilterSearch, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (g_ui.hFilterField)        SendMessageW(g_ui.hFilterField, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (g_ui.hFilterShowFull)     SendMessageW(g_ui.hFilterShowFull, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (g_ui.hFilterShowEmpty)    SendMessageW(g_ui.hFilterShowEmpty, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (g_ui.hFilterShowPassword) SendMessageW(g_ui.hFilterShowPassword, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (g_ui.hFilterShowFull)     SendMessageW(g_ui.hFilterShowFull, BM_SETCHECK, g_filters.showFull ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_ui.hFilterShowEmpty)    SendMessageW(g_ui.hFilterShowEmpty, BM_SETCHECK, g_filters.showEmpty ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_ui.hFilterShowPassword) SendMessageW(g_ui.hFilterShowPassword, BM_SETCHECK, g_filters.showPassword ? BST_CHECKED : BST_UNCHECKED, 0);

    UI_BringTopFiltersToFront();
    UI_LoadFiltersIntoTopControls();
    UI_ShowTopFilters(hWndParent, TRUE);
}


static void UI_RedrawTopFilters(void)
{
    UINT flags = RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW;

    if (g_ui.hFilterSearch)
        RedrawWindow(g_ui.hFilterSearch, NULL, NULL, flags);

    if (g_ui.hFilterField)
        RedrawWindow(g_ui.hFilterField, NULL, NULL, flags);

    if (g_ui.hFilterShowFull)
        RedrawWindow(g_ui.hFilterShowFull, NULL, NULL, flags);

    if (g_ui.hFilterShowEmpty)
        RedrawWindow(g_ui.hFilterShowEmpty, NULL, NULL, flags);

    if (g_ui.hFilterShowPassword)
        RedrawWindow(g_ui.hFilterShowPassword, NULL, NULL, flags);
}

static void UI_RefreshTopFilterFrames(void)
{
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;

    if (g_ui.hFilterSearch)
        SetWindowPos(g_ui.hFilterSearch, NULL, 0, 0, 0, 0, flags);

    if (g_ui.hFilterField)
        SetWindowPos(g_ui.hFilterField, NULL, 0, 0, 0, 0, flags);

    if (g_ui.hFilterShowFull)
        SetWindowPos(g_ui.hFilterShowFull, NULL, 0, 0, 0, 0, flags);

    if (g_ui.hFilterShowEmpty)
        SetWindowPos(g_ui.hFilterShowEmpty, NULL, 0, 0, 0, 0, flags);

    if (g_ui.hFilterShowPassword)
        SetWindowPos(g_ui.hFilterShowPassword, NULL, 0, 0, 0, 0, flags);
}

#define FILTER_COMBO_WIDTH 88

void LayoutTopFilters(HWND hWndParent, int clientW)
{
    RECT rcTB;
    RECT rcBtn;
    int y, h;
    int x;
    int minX;
    int hSearch;
    int total;
    int available;
    int over;

    const int padRight = 10;

    const int gap = 4;
    const int gapFieldToChecks = 24; // bigger spacing between combo and checkboxes

    const int desiredSearch = 160;
    const int minSearch = 72;

    const int desiredField = 78;
    const int minField = 64;

    int wSearch = desiredSearch;
    int wField;
    int wFull;
    int wEmpty;
    int wPass;

    if (!g_filtersVisible || !g_ui.hToolBar)
        return;

    GetWindowRect(g_ui.hToolBar, &rcTB);
    MapWindowPoints(NULL, hWndParent, (LPPOINT)&rcTB, 2);

    y = rcTB.top + 2;
    h = (rcTB.bottom - rcTB.top) - 4;
    if (h < 20) h = 20;
    if (h > 28) h = 28;

    hSearch = (h > 2) ? (h - 4) : h;

    minX = 4;
    if (SendMessageW(g_ui.hToolBar, TB_BUTTONCOUNT, 0, 0) > 0)
    {
        int lastBtn = (int)SendMessageW(g_ui.hToolBar, TB_BUTTONCOUNT, 0, 0) - 1;
        if (SendMessageW(g_ui.hToolBar, TB_GETITEMRECT, lastBtn, (LPARAM)&rcBtn))
        {
            MapWindowPoints(g_ui.hToolBar, hWndParent, (LPPOINT)&rcBtn, 2);
            minX = rcBtn.right + 12;
        }
    }

    wField = UI_CalcComboWidth(g_ui.hFilterField, desiredField);
    if (wField > FILTER_COMBO_WIDTH)
        wField = FILTER_COMBO_WIDTH; // keep if you want hard cap; otherwise remove this block

    wFull = UI_CalcCheckboxWidth(g_ui.hFilterShowFull, 84);
    wEmpty = UI_CalcCheckboxWidth(g_ui.hFilterShowEmpty, 94);
    wPass = UI_CalcCheckboxWidth(g_ui.hFilterShowPassword, 108);

    total = wSearch + gap + wField + gapFieldToChecks + wFull + gap + wEmpty + gap + wPass;

    available = clientW - padRight - minX;
    if (available < 0)
        available = 0;

    if (total > available)
    {
        over = total - available;

        if (wSearch > minSearch)
        {
            int canShrink = wSearch - minSearch;
            int shrink = (over < canShrink) ? over : canShrink;
            wSearch -= shrink;
            over -= shrink;
        }

        if (over > 0 && wField > minField)
        {
            int canShrink = wField - minField;
            int shrink = (over < canShrink) ? over : canShrink;
            wField -= shrink;
            over -= shrink;
        }

        total = wSearch + gap + wField + gapFieldToChecks + wFull + gap + wEmpty + gap + wPass;
    }

    x = clientW - padRight - total;
    if (x < minX)
        x = minX;

    if (g_ui.hFilterSearch)
    {
        MoveWindow(g_ui.hFilterSearch, x, y + 4, wSearch, hSearch, TRUE);
        x += wSearch + gap;
    }

    if (g_ui.hFilterField)
    {
        MoveWindow(g_ui.hFilterField, x, y + 4, wField, h + 160, TRUE);
        x += wField + gapFieldToChecks; // <-- bigger separation before checkboxes
    }

    if (g_ui.hFilterShowFull)
    {
        MoveWindow(g_ui.hFilterShowFull, x, y + 2, wFull, h, TRUE);
        x += wFull + gap;
    }

    if (g_ui.hFilterShowEmpty)
    {
        MoveWindow(g_ui.hFilterShowEmpty, x, y + 2, wEmpty, h, TRUE);
        x += wEmpty + gap;
    }

    if (g_ui.hFilterShowPassword)
    {
        MoveWindow(g_ui.hFilterShowPassword, x, y + 2, wPass, h, TRUE);
    }

    UI_BringTopFiltersToFront();
    UI_RefreshTopFilterFrames();
    UI_RedrawTopFilters();
    RedrawWindow(hWndParent, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

void UI_ShowTopFilters(HWND hWnd, BOOL show)
{
    g_filtersVisible = show ? TRUE : FALSE;

    if (g_filtersVisible)
        UI_LoadFiltersIntoTopControls();

    if (g_ui.hFilterSearch)
        ShowWindow(g_ui.hFilterSearch, g_filtersVisible ? SW_SHOWNA : SW_HIDE);

    if (g_ui.hFilterField)
        ShowWindow(g_ui.hFilterField, g_filtersVisible ? SW_SHOWNA : SW_HIDE);

    if (g_ui.hFilterShowFull)
        ShowWindow(g_ui.hFilterShowFull, g_filtersVisible ? SW_SHOWNA : SW_HIDE);

    if (g_ui.hFilterShowEmpty)
        ShowWindow(g_ui.hFilterShowEmpty, g_filtersVisible ? SW_SHOWNA : SW_HIDE);

    if (g_ui.hFilterShowPassword)
        ShowWindow(g_ui.hFilterShowPassword, g_filtersVisible ? SW_SHOWNA : SW_HIDE);

    if (IsWindow(hWnd))
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        ResizeLameWindow(hWnd, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
    }

    if (g_filtersVisible)
    {
        UI_BringTopFiltersToFront();
        UI_RefreshTopFilterFrames();
        UI_RedrawTopFilters();
    }

    InvalidateRect(hWnd, NULL, TRUE);
    UpdateWindow(hWnd);
}


static int StrContainsI_W(const wchar_t* hay, const wchar_t* needle)
{
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;

    // simple case-insensitive substring
    for (const wchar_t* h = hay; *h; ++h)
    {
        const wchar_t* a = h;
        const wchar_t* b = needle;

        while (*a && *b && towlower(*a) == towlower(*b))
        {
            ++a; ++b;
        }
        if (!*b)
            return 1;
    }
    return 0;
}

static int Server_IsPassworded(const LameServer* s)
{
    if (!s) return 0;

    // Heuristic using rules list (works across a bunch of protocols)
    // Looks for: password/needpass/private (value "1", "true", etc)
    for (int i = 0; i < s->ruleCount; i++)
    {
        const wchar_t* k = s->ruleList[i].key;
        const wchar_t* v = s->ruleList[i].value;
        if (!k) continue;

        // key match
        if (_wcsicmp(k, L"password") == 0 ||
            _wcsicmp(k, L"needpass") == 0 ||
            _wcsicmp(k, L"private") == 0 ||
            _wcsicmp(k, L"requirespassword") == 0)
        {
            if (!v) return 1;
            if (_wcsicmp(v, L"1") == 0 || _wcsicmp(v, L"true") == 0 || _wcsicmp(v, L"yes") == 0)
                return 1;
        }
    }

    return 0;
}

int UI_ServerPassesFilters(const LameServer* s)
{
    if (!s)
        return 0;

    // Show empty / full / password are "INCLUDE" toggles.
    // If a toggle is unchecked, those servers are excluded.
    if (!g_filters.showEmpty)
    {
        if (s->players <= 0)
            return 0;
    }

    if (!g_filters.showFull)
    {
        if (s->maxPlayers > 0 && s->players >= s->maxPlayers)
            return 0;
    }

    if (!g_filters.showPassword)
    {
        if (Server_IsPassworded(s))
            return 0;
    }

    // Search text
    if (!g_filters.search[0])
        return 1;

    switch (g_filters.field)
    {
    default:
    case FILTERFIELD_HOSTNAME:
        return StrContainsI_W(s->name, g_filters.search);

    case FILTERFIELD_MAP:
        return StrContainsI_W(s->map, g_filters.search);

    case FILTERFIELD_GAMETYPE:
        return StrContainsI_W(s->gametype, g_filters.search);

    case FILTERFIELD_IP:
        return StrContainsI_W(s->ip, g_filters.search);

    case FILTERFIELD_PLAYER:
    {
        for (int i = 0; i < s->playerCount; i++)
        {
            const wchar_t* pn = s->playerList[i].name;
            if (StrContainsI_W(pn, g_filters.search))
                return 1;
        }
        return 0;
    }
    }
}

void Filters_OnMainWindowResized(HWND hWndParent)
{
    if (!g_filtersVisible)
        return;

    UI_BringTopFiltersToFront();
    UI_RedrawTopFilters();

    if (IsWindow(hWndParent))
        RedrawWindow(hWndParent, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}