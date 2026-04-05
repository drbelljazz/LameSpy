#include <windows.h>
#include <stdio.h>
#include <CommCtrl.h>
#include "LameTree.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameTreeTag.h"
#include "LameUI.h"
#include "resource.h"

static HTREEITEM Tree_AddItem(HWND tv, HTREEITEM parent, const wchar_t* text, LPARAM tag)
{
    TVINSERTSTRUCTW ti = {};
    ti.hParent = parent;
    ti.hInsertAfter = TVI_LAST;
    ti.item.mask = TVIF_TEXT | TVIF_PARAM;
    ti.item.pszText = (LPWSTR)text;
    ti.item.lParam = tag;

    return (HTREEITEM)SendMessageW(tv, TVM_INSERTITEMW, 0, (LPARAM)&ti);
}

HWND Tree_Create(HWND parent, HINSTANCE hInst)
{
    HWND h = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_TREEVIEWW,
        NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS |
        TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
        0, 0, 200, 100,
        parent,
        (HMENU)IDC_TREE_GAMES,
        hInst,
        NULL);

    if (!h)
    {
        wchar_t buf[256];
        swprintf_s(buf, _countof(buf), L"CreateTree failed (err=%lu)", GetLastError());
        MessageBoxW(parent, buf, L"UI Error", MB_OK | MB_ICONERROR);
        return NULL;
    }

    TreeView_SetBkColor(h, GetSysColor(COLOR_WINDOW));
    TreeView_SetTextColor(h, GetSysColor(COLOR_WINDOWTEXT));

    return h;
}

/*static*/ void UI_BuildTree(void)
{
    HTREEITEM homeNode;
    HTREEITEM root;
    HTREEITEM gameNode;
    int gi, mi;
    const LameGameDescriptor* desc;
    int masterCount;
    const LameMasterAddress* addr;
    wchar_t nodeName[256];

    if (!g_ui.hTreeMenu)
        return;

    TreeView_DeleteAllItems(g_ui.hTreeMenu);

    homeNode = Tree_AddItem(g_ui.hTreeMenu, TVI_ROOT, L"Home",
        TreeTag_Make(GAME_NONE, TREE_NODE_HOME, 0));

    Tree_AddItem(g_ui.hTreeMenu, homeNode, L"Favorites",
        TreeTag_Make(GAME_NONE, TREE_NODE_HOME_FAVORITES, 0));

    Tree_AddItem(g_ui.hTreeMenu, homeNode, L"Tutorials",
        TreeTag_Make(GAME_NONE, TREE_NODE_HOME_TUTORIALS, 0));

    /*Tree_AddItem(g_ui.hTreeMenu, homeNode, L"Archives",
        TreeTag_Make(GAME_NONE, TREE_NODE_HOME_ARCHIVES, 0));*/

    /*Tree_AddItem(g_ui.hTreeMenu, homeNode, L"GameDate",
        TreeTag_Make(GAME_NONE, TREE_NODE_HOME_GAMEDATE, 0));*/

    root = Tree_AddItem(g_ui.hTreeMenu, TVI_ROOT, L"Games", 0);

    for (gi = GAME_Q3; gi < GAME_MAX; gi++)
    {
        HTREEITEM internetNode;

        desc = Game_GetDescriptor((GameId)gi);
        if (!desc || desc->id == GAME_NONE)
            continue;

        // Leave out "Other" tree since we don't have a way to launch all those other games
        if (desc->id == GAME_UE)
            continue;

        if ((g_config.enabledGameMask & (1u << desc->id)) == 0)
            continue;

        gameNode = Tree_AddItem(g_ui.hTreeMenu, root, desc->name,
            TreeTag_Make((GameId)gi, TREE_NODE_GAME, 0));

        Tree_AddItem(g_ui.hTreeMenu, gameNode, L"Favorites",
            TreeTag_Make((GameId)gi, TREE_NODE_FAVORITES, 0));

        internetNode = Tree_AddItem(g_ui.hTreeMenu, gameNode, L"Internet",
            TreeTag_Make((GameId)gi, TREE_NODE_INTERNET, 0));

        Tree_AddItem(g_ui.hTreeMenu, gameNode, L"LAN",
            TreeTag_Make((GameId)gi, TREE_NODE_LAN, 0));

        if (g_config.showMasters)
        {
            masterCount = Data_GetMasterCountForGame((GameId)gi);
            for (mi = 0; mi < masterCount; mi++)
            {
                addr = Data_GetMasterAddress((GameId)gi, mi);

                if (addr && addr->address[0])
                    _snwprintf_s(nodeName, _countof(nodeName), _TRUNCATE, L"%s", addr->address);
                else
                    _snwprintf_s(nodeName, _countof(nodeName), _TRUNCATE, L"Master %d", mi + 1);

                Tree_AddItem(g_ui.hTreeMenu, internetNode, nodeName,
                    TreeTag_Make((GameId)gi, TREE_NODE_MASTER, mi));
            }
        }
    }

    TreeView_Expand(g_ui.hTreeMenu, root, TVE_EXPAND);
    TreeView_Expand(g_ui.hTreeMenu, homeNode, TVE_EXPAND);

    HTREEITEM selectItem = homeNode;
    HTREEITEM gamesRoot = root;

    const char* startup = g_config.startupItem;

    if (startup && startup[0])
    {
        if (_stricmp(startup, "HOME") == 0)
        {
            selectItem = homeNode;
        }
        else if (_stricmp(startup, "FAVORITES") == 0)
        {
            HTREEITEM child = TreeView_GetChild(g_ui.hTreeMenu, homeNode);
            while (child)
            {
                TVITEMW tvi = {};
                tvi.mask = TVIF_PARAM;
                tvi.hItem = child;

                if (TreeView_GetItem(g_ui.hTreeMenu, &tvi) &&
                    TreeTag_Kind(tvi.lParam) == TREE_NODE_HOME_FAVORITES)
                {
                    selectItem = child;
                    break;
                }

                child = TreeView_GetNextSibling(g_ui.hTreeMenu, child);
            }
        }
        else
        {
            GameId startupGame = Game_FromPrefixA(startup);
            if (startupGame != GAME_NONE && startupGame != GAME_UE && gamesRoot)
            {
                HTREEITEM gameNode2 = TreeView_GetChild(g_ui.hTreeMenu, gamesRoot);
                while (gameNode2)
                {
                    TVITEMW tvi = {};
                    tvi.mask = TVIF_PARAM;
                    tvi.hItem = gameNode2;

                    if (TreeView_GetItem(g_ui.hTreeMenu, &tvi))
                    {
                        if (TreeTag_Kind(tvi.lParam) == TREE_NODE_GAME &&
                            TreeTag_Game(tvi.lParam) == startupGame)
                        {
                            selectItem = gameNode2;
                            break;
                        }
                    }

                    gameNode2 = TreeView_GetNextSibling(g_ui.hTreeMenu, gameNode2);
                }
            }
        }
    }

    if (!UI_GetSuppressTreeSelChanged() && selectItem)
        TreeView_SelectItem(g_ui.hTreeMenu, selectItem);
}

void Tree_ExpandAllGameNodesIfEnabled(HWND treeMenu)
{
    if (!g_config.expandTreeOnStartup)
        return;

    if (!treeMenu || !IsWindow(treeMenu))
        return;

    HTREEITEM homeNode = TreeView_GetRoot(treeMenu);
    if (!homeNode)
        return;

    HTREEITEM gamesRoot = TreeView_GetNextSibling(treeMenu, homeNode);
    if (!gamesRoot)
        return;

    HTREEITEM gameNode = TreeView_GetChild(treeMenu, gamesRoot);
    while (gameNode)
    {
        TreeView_Expand(treeMenu, gameNode, TVE_EXPAND);
        gameNode = TreeView_GetNextSibling(treeMenu, gameNode);
    }
}

void Tree_UpdateMasterNodeBold(HWND treeMenu, GameId game, int masterIndex, BOOL bold)
{
    if (!treeMenu || !IsWindow(treeMenu))
        return;

    HTREEITEM root = TreeView_GetRoot(treeMenu);
    if (!root)
        return;

    root = TreeView_GetNextSibling(treeMenu, root); // skip Home
    if (!root)
        return;

    HTREEITEM gameNode = TreeView_GetChild(treeMenu, root);

    while (gameNode)
    {
        TVITEMW item = {};
        item.mask = TVIF_PARAM;
        item.hItem = gameNode;

        if (TreeView_GetItem(treeMenu, &item))
        {
            GameId nodeGame = TreeTag_Game(item.lParam);
            if (nodeGame == game)
            {
                HTREEITEM masterNode = TreeView_GetChild(treeMenu, gameNode);
                while (masterNode)
                {
                    TVITEMW masterItem = {};
                    masterItem.mask = TVIF_PARAM;
                    masterItem.hItem = masterNode;

                    if (TreeView_GetItem(treeMenu, &masterItem))
                    {
                        TreeNodeKind kind = TreeTag_Kind(masterItem.lParam);
                        int nodeMasterIndex = TreeTag_MasterIndex(masterItem.lParam);

                        if (kind == TREE_NODE_MASTER && nodeMasterIndex == masterIndex)
                        {
                            TVITEMW updateItem = {};
                            updateItem.mask = TVIF_STATE;
                            updateItem.hItem = masterNode;
                            updateItem.stateMask = TVIS_BOLD;
                            updateItem.state = bold ? TVIS_BOLD : 0;
                            TreeView_SetItem(treeMenu, &updateItem);
                            return;
                        }
                    }

                    masterNode = TreeView_GetNextSibling(treeMenu, masterNode);
                }
                return;
            }
        }

        gameNode = TreeView_GetNextSibling(treeMenu, gameNode);
    }
}

void Tree_UpdateGameNodeBold(HWND treeMenu, GameId game)
{
    if (!treeMenu || !IsWindow(treeMenu))
        return;

    HTREEITEM root = TreeView_GetRoot(treeMenu);
    if (!root)
        return;

    root = TreeView_GetNextSibling(treeMenu, root); // skip Home
    if (!root)
        return;

    BOOL hasServers = FALSE;
    int mc = Data_GetMasterCountForGame(game);
    for (int mi = 0; mi < mc; mi++)
    {
        LameMaster* m = Data_GetMasterInternet(game, mi);
        if (m && m->count > 0)
        {
            hasServers = TRUE;
            break;
        }
    }

    HTREEITEM gameNode = TreeView_GetChild(treeMenu, root);
    while (gameNode)
    {
        TVITEMW item = {};
        item.mask = TVIF_PARAM;
        item.hItem = gameNode;

        if (TreeView_GetItem(treeMenu, &item))
        {
            if (TreeTag_Game(item.lParam) == game &&
                TreeTag_Kind(item.lParam) == TREE_NODE_GAME)
            {
                TVITEMW updateItem = {};
                updateItem.mask = TVIF_STATE;
                updateItem.hItem = gameNode;
                updateItem.stateMask = TVIS_BOLD;
                updateItem.state = hasServers ? TVIS_BOLD : 0;
                TreeView_SetItem(treeMenu, &updateItem);
                return;
            }
        }

        gameNode = TreeView_GetNextSibling(treeMenu, gameNode);
    }
}

HTREEITEM Tree_FindGameNode(HWND hTree, GameId game)
{
    if (!hTree || !IsWindow(hTree))
        return NULL;

    // Tree layout from `UI_BuildTree`:
    // root: "Home" (first root), "Games" (next sibling)
    HTREEITEM homeNode = TreeView_GetRoot(hTree);
    if (!homeNode)
        return NULL;

    HTREEITEM gamesRoot = TreeView_GetNextSibling(hTree, homeNode);
    if (!gamesRoot)
        return NULL;

    // Children of "Games" are per-game nodes tagged with TREE_NODE_GAME.
    for (HTREEITEM hItem = TreeView_GetChild(hTree, gamesRoot);
        hItem;
        hItem = TreeView_GetNextSibling(hTree, hItem))
    {
        TVITEMW tvi = {};
        tvi.mask = TVIF_PARAM;
        tvi.hItem = hItem;

        if (!TreeView_GetItem(hTree, &tvi))
            continue;

        if (TreeTag_Kind(tvi.lParam) == TREE_NODE_GAME &&
            TreeTag_Game(tvi.lParam) == game)
        {
            return hItem;
        }
    }

    return NULL;
}

#include "LameTree.h"

// Helper: Recursively search for a node with the text "Tutorials"
static HTREEITEM Tree_FindTutorialsNodeRecurse(HWND hTree, HTREEITEM hItem)
{
    TVITEMW tvi = { 0 };
    wchar_t text[128] = {};

    while (hItem)
    {
        tvi.mask = TVIF_TEXT;
        tvi.hItem = hItem;
        tvi.pszText = text;
        tvi.cchTextMax = _countof(text);

        if (TreeView_GetItem(hTree, &tvi))
        {
            if (wcscmp(text, L"Tutorials") == 0)
                return hItem;
        }

        // Search children
        HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
        if (hChild)
        {
            HTREEITEM found = Tree_FindTutorialsNodeRecurse(hTree, hChild);
            if (found)
                return found;
        }

        hItem = TreeView_GetNextSibling(hTree, hItem);
    }
    return NULL;
}

HTREEITEM Tree_FindTutorialsNode(HWND hTree)
{
    if (!hTree)
        return NULL;
    HTREEITEM hRoot = TreeView_GetRoot(hTree);
    return Tree_FindTutorialsNodeRecurse(hTree, hRoot);
}