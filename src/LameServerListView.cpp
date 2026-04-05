#include <stdio.h>
#include "LameServerListView.h"
#include "LameData.h"
#include "LameFilters.h"
#include "LameTreeTag.h"
#include "LameUI.h"
#include "resource.h"

static HIMAGELIST g_hImageList = NULL;

static int ServerListView_GameToIconIndex(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return ICON_Q3;
    case GAME_Q2:   return ICON_Q2;
    case GAME_QW:   return ICON_QW;
    case GAME_UT99: return ICON_UT99;
    case GAME_UG:   return ICON_UG;
    case GAME_DX:   return ICON_DX;
    default:        return ICON_GS;
    }
}

static void ServerListView_CreateImageList(HWND listView, HINSTANCE hInst)
{
    // Create 16x16 + 6px padding
    g_hImageList = ImageList_Create(22, 20, ILC_COLOR32 | ILC_MASK, 3, 1);
    if (!g_hImageList)
        return;

    HICON hIconQ3 = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_GAME_Q3));
    HICON hIconQ2 = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_GAME_Q2));
    HICON hIconQW = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_GAME_QW));
    HICON hIconUT99 = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_GAME_UT99));
    HICON hIconUG = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_GAME_UG));
    HICON hIconDX = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_GAME_DX));
    HICON hIconDefault = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_LAMESPY));

    if (hIconQ3) ImageList_AddIcon(g_hImageList, hIconQ3);
    if (hIconQ2) ImageList_AddIcon(g_hImageList, hIconQ2);
    if (hIconQW) ImageList_AddIcon(g_hImageList, hIconQW);
    if (hIconUT99) ImageList_AddIcon(g_hImageList, hIconUT99);
    if (hIconUG) ImageList_AddIcon(g_hImageList, hIconUG);
    if (hIconDX) ImageList_AddIcon(g_hImageList, hIconDX);
    if (hIconDefault) ImageList_AddIcon(g_hImageList, hIconDefault);

    ListView_SetImageList(listView, g_hImageList, LVSIL_SMALL);

    if (hIconQ3) DestroyIcon(hIconQ3);
    if (hIconQ2) DestroyIcon(hIconQ2);
    if (hIconQW) DestroyIcon(hIconQW);
    if (hIconUT99) DestroyIcon(hIconUT99);
    if (hIconUG) DestroyIcon(hIconUG);
    if (hIconDX) DestroyIcon(hIconDX);
    if (hIconDefault) DestroyIcon(hIconDefault);
}

void ServerListView_Init(HWND listView, HINSTANCE hInst)
{
    if (!IsWindow(listView))
        return;

    ServerListView_CreateImageList(listView, hInst);
}

void ServerListView_Shutdown(void)
{
    if (g_hImageList)
    {
        ImageList_Destroy(g_hImageList);
        g_hImageList = NULL;
    }
}

int ServerListView_IsFavoritesView(HWND treeMenu)
{
    if (!treeMenu || !IsWindow(treeMenu))
        return 0;

    HTREEITEM hSel = TreeView_GetSelection(treeMenu);
    if (!hSel)
        return 0;

    TVITEMW item = {};
    item.mask = TVIF_PARAM;
    item.hItem = hSel;

    if (!TreeView_GetItem(treeMenu, &item))
        return 0;

    TreeNodeKind kind = TreeTag_Kind(item.lParam);
    return (kind == TREE_NODE_FAVORITES || kind == TREE_NODE_HOME_FAVORITES);
}

int ServerListView_FindServerRow(HWND lv, const LameServer* server)
{
    if (!lv || !server)
        return -1;

    LVFINDINFOW fi = {};
    fi.flags = LVFI_PARAM;
    fi.lParam = (LPARAM)server;

    return ListView_FindItem(lv, -1, &fi);
}

void ServerListView_ApplyServerResultToRow(HWND lv, int row, const LameServer* server)
{
    if (!lv || row < 0 || !server)
        return;

    if (server->state == QUERY_FAILED)
    {
        ListView_DeleteItem(lv, row);
        return;
    }

    if (server->state != QUERY_DONE)
        return;

    wchar_t buf[96];

    ListView_SetItemText(lv, row, 0, (LPWSTR)server->name);
    ListView_SetItemText(lv, row, 1, (LPWSTR)server->map);

    swprintf_s(buf, _countof(buf), L"%d/%d", server->players, server->maxPlayers);
    ListView_SetItemText(lv, row, 2, buf);

    swprintf_s(buf, _countof(buf), L"%d", server->ping);
    ListView_SetItemText(lv, row, 3, buf);

    ListView_SetItemText(lv, row, 4, (LPWSTR)server->gametype);
    ListView_SetItemText(lv, row, 5, (LPWSTR)server->label);
    ListView_SetItemText(lv, row, 6, (LPWSTR)server->gamename);

    swprintf_s(buf, _countof(buf), L"%s:%d", server->ip, server->port);
    ListView_SetItemText(lv, row, 7, buf);
}

int ServerListView_InsertServerRowFromServer(HWND lv, const LameServer* s)
{
    if (!lv || !s)
        return -1;

    LVITEMW lvi = {};
    wchar_t buf[96];

    lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
    lvi.iItem = ListView_GetItemCount(lv);
    lvi.pszText = (LPWSTR)(s->name ? s->name : L"");
    lvi.lParam = (LPARAM)s;
    lvi.iImage = ServerListView_GameToIconIndex(s->game);

    int row = ListView_InsertItem(lv, &lvi);
    if (row < 0)
        return -1;

    ListView_SetItemText(lv, row, 1, (LPWSTR)(s->map ? s->map : L""));

    swprintf_s(buf, _countof(buf), L"%d/%d", s->players, s->maxPlayers);
    ListView_SetItemText(lv, row, 2, buf);

    swprintf_s(buf, _countof(buf), L"%d", s->ping);
    ListView_SetItemText(lv, row, 3, buf);

    ListView_SetItemText(lv, row, 4, (LPWSTR)(s->gametype ? s->gametype : L""));
    ListView_SetItemText(lv, row, 5, (LPWSTR)(s->label ? s->label : L""));
    ListView_SetItemText(lv, row, 6, (LPWSTR)(s->gamename ? s->gamename : L""));

    swprintf_s(buf, _countof(buf), L"%s:%d", s->ip, s->port);
    ListView_SetItemText(lv, row, 7, buf);

    return row;
}

void ServerListView_Rebuild(HWND serverList, HWND treeMenu, const LameMaster* activeMaster)
{
    if (!IsWindow(serverList))
        return;

    SendMessageW(serverList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(serverList);

    if (!activeMaster)
    {
        SendMessageW(serverList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(serverList, NULL, TRUE);
        return;
    }

    const int isFavoritesView = ServerListView_IsFavoritesView(treeMenu);

    Data_Lock();

    for (int i = 0; i < activeMaster->count; i++)
    {
        LameServer* s = activeMaster->servers[i];
        if (!s)
            continue;

        if (isFavoritesView)
        {
            if (g_config.hideDeadFavorites)
            {
                if (s->state != QUERY_DONE)
                    continue;
            }
        }
        else
        {
            // Tighter behavior:
            // - keep QUERY_IDLE / QUERY_IN_PROGRESS visible for real-time list population
            // - hide only truly dead states when enabled
            if (g_config.hideDeadInternets)
            {
                if (s->state == QUERY_FAILED || s->state == QUERY_CANCELED)
                    continue;
            }
        }

        if (!UI_ServerPassesFilters(s))
            continue;

        LVITEMW lvi = {};
        wchar_t buf[96];

        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = ListView_GetItemCount(serverList);
        lvi.pszText = s->name;
        lvi.lParam = (LPARAM)s;
        lvi.iImage = ServerListView_GameToIconIndex(s->game);

        ListView_InsertItem(serverList, &lvi);

        lvi.mask = LVIF_TEXT;

        lvi.iSubItem = 1;
        lvi.pszText = s->map;
        ListView_SetItem(serverList, &lvi);

        swprintf_s(buf, _countof(buf), L"%d/%d", s->players, s->maxPlayers);
        lvi.iSubItem = 2;
        lvi.pszText = buf;
        ListView_SetItem(serverList, &lvi);

        swprintf_s(buf, _countof(buf), L"%d", s->ping);
        lvi.iSubItem = 3;
        lvi.pszText = buf;
        ListView_SetItem(serverList, &lvi);

        lvi.iSubItem = 4;
        lvi.pszText = s->gametype;
        ListView_SetItem(serverList, &lvi);

        lvi.iSubItem = 5;
        lvi.pszText = s->label;
        ListView_SetItem(serverList, &lvi);

        lvi.iSubItem = 6;
        lvi.pszText = s->gamename;
        ListView_SetItem(serverList, &lvi);

        swprintf_s(buf, _countof(buf), L"%s:%d", s->ip, s->port);
        lvi.iSubItem = 7;
        lvi.pszText = buf;
        ListView_SetItem(serverList, &lvi);
    }

    Data_Unlock();

    SendMessageW(serverList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(serverList, NULL, TRUE);
}

void ServerListView_UpdateSelectedRow(HWND serverList, const LameServer* selectedServer)
{
    if (!IsWindow(serverList) || !selectedServer)
        return;

    int selectedIndex = ListView_GetNextItem(serverList, -1, LVNI_SELECTED);
    if (selectedIndex < 0)
        return;

    LVITEMW lvi = {};
    wchar_t buf[96];

    lvi.mask = LVIF_TEXT;
    lvi.iItem = selectedIndex;

    lvi.iSubItem = 0;
    lvi.pszText = (LPWSTR)selectedServer->name;
    ListView_SetItem(serverList, &lvi);

    lvi.iSubItem = 1;
    lvi.pszText = (LPWSTR)selectedServer->map;
    ListView_SetItem(serverList, &lvi);

    swprintf_s(buf, _countof(buf), L"%d/%d", selectedServer->players, selectedServer->maxPlayers);
    lvi.iSubItem = 2;
    lvi.pszText = buf;
    ListView_SetItem(serverList, &lvi);

    swprintf_s(buf, _countof(buf), L"%d", selectedServer->ping);
    lvi.iSubItem = 3;
    lvi.pszText = buf;
    ListView_SetItem(serverList, &lvi);

    lvi.iSubItem = 4;
    lvi.pszText = (LPWSTR)selectedServer->gametype;
    ListView_SetItem(serverList, &lvi);

    lvi.iSubItem = 5;
    lvi.pszText = (LPWSTR)selectedServer->label;
    ListView_SetItem(serverList, &lvi);

    lvi.iSubItem = 6;
    lvi.pszText = (LPWSTR)selectedServer->gamename;
    ListView_SetItem(serverList, &lvi);

    swprintf_s(buf, _countof(buf), L"%s:%d", selectedServer->ip, selectedServer->port);
    lvi.iSubItem = 7;
    lvi.pszText = buf;
    ListView_SetItem(serverList, &lvi);
}

LRESULT ServerListView_OnCustomDraw(LPNMLVCUSTOMDRAW cd)
{
    switch (cd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
    {
        LameServer* s = (LameServer*)cd->nmcd.lItemlParam;
        if (!s)
            return CDRF_DODEFAULT;

        if (s->players > 0)
        {
            if (s->maxPlayers > 0 && s->players >= s->maxPlayers)
            {
                cd->clrText = RGB(120, 60, 60);
                cd->clrTextBk = RGB(255, 232, 232);
            }
            else
            {
                cd->clrTextBk = RGB(248, 248, 248);
            }
        }

        return CDRF_DODEFAULT;
    }
    }

    return CDRF_DODEFAULT;
}