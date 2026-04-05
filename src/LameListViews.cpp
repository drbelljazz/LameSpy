#include "LameListViews.h"
#include <stdio.h>
#include <commctrl.h>
#include "LameCore.h"

#if LAMESPY_USE_COMCTL6
  #include <uxtheme.h>
  #pragma comment(lib, "uxtheme.lib")
#endif


HWND ListViews_CreateListView(HWND parent, HINSTANCE hInst, int id)
{
    HWND h;
    DWORD ex;

    h = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 100, 100,
        parent,
        (HMENU)(UINT_PTR)id,
        hInst,
        NULL
    );

    if (!h)
    {
        wchar_t buf[256];
        swprintf_s(buf, _countof(buf), L"CreateListView failed (id=%d err=%lu)", id, GetLastError());
        MessageBoxW(parent, buf, L"UI Error", MB_OK | MB_ICONERROR);
        return NULL;
    }

    ex = ListView_GetExtendedListViewStyle(h);
    ex |= LVS_EX_FULLROWSELECT;
    ex |= LVS_EX_GRIDLINES;
    ex |= LVS_EX_DOUBLEBUFFER;
    ex |= LVS_EX_HEADERDRAGDROP;
    ListView_SetExtendedListViewStyle(h, ex);

    ListView_SetUnicodeFormat(h, TRUE);

#if LAMESPY_USE_COMCTL6
    {
        HWND hHdr;

        hHdr = ListView_GetHeader(h);
        if (hHdr)
            SetWindowTheme(hHdr, L"", L"");
    }
#endif

    return h;
}

void ListViews_AddColumn(HWND lv, int i, int w, const wchar_t* txt, int hiddenByDefault)
{
    LVCOLUMNW c = { 0 };
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.cx = hiddenByDefault ? 0 : w;
    c.pszText = (LPWSTR)txt;

    ListView_InsertColumn(lv, i, &c);
}

void ListViews_InitServerColumns(HWND hServerList)
{
    ListViews_AddColumn(hServerList, 0, 300, L"Name", 0);
    ListViews_AddColumn(hServerList, 1, 130, L"Map", 0);
    ListViews_AddColumn(hServerList, 2, 70, L"Players", 0);
    ListViews_AddColumn(hServerList, 3, 50, L"Ping", 0);
    ListViews_AddColumn(hServerList, 4, 120, L"Gametype", 0);
    ListViews_AddColumn(hServerList, 5, 150, L"Label", 1);
    ListViews_AddColumn(hServerList, 6, 120, L"Gamename", 1);
    ListViews_AddColumn(hServerList, 7, 130, L"IP Address", 1);
}

void ListViews_InitPlayerColumns(HWND hPlayerList)
{
    ListViews_AddColumn(hPlayerList, 0, 300, L"Player", 0);
    ListViews_AddColumn(hPlayerList, 1, 50, L"Score", 0);
    ListViews_AddColumn(hPlayerList, 2, 55, L"Ping", 0);
}

void ListViews_InitRulesColumns(HWND hRulesList)
{
    ListViews_AddColumn(hRulesList, 0, 130, L"Rule", 0);
    ListViews_AddColumn(hRulesList, 1, 260, L"Value", 0);
}