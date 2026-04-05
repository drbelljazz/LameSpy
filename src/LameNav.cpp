#include "LameNav.h"
#include <CommCtrl.h>
#include <stdio.h>
#include "LameData.h"
#include "LameFilters.h"
#include "LameGame.h"
#include "LameNet.h"
#include "LameServerListView.h"
#include "LameUiSession.h"
#include "LameStatusBar.h"
#include "LameTree.h"
#include "LameTreeTag.h"
#include "LameHomeView.h"
#include "resource.h"

static const NavUi* Nav_GetUi(const NavContext* ctx)
{
    return (ctx && ctx->ui) ? ctx->ui : NULL;
}

static void UI_SetHomeVisible(BOOL show)
{
    HomeView_SetMode(show);
}

static void UI_ShowWebPage(const wchar_t* title, const wchar_t* url)
{
    HomeView_ShowPage(title, url);
}

static void UI_ShowHomePage(void)
{
    HomeView_ShowHomePage();
}

static void ResetCompleteSoundForBatch(int gen, int* soundPlayedForBatch, int* soundBatchGen)
{
    if (soundPlayedForBatch) *soundPlayedForBatch = 0;
    if (soundBatchGen) *soundBatchGen = gen;
}

static BOOL Nav_IsUnrealFamilyGame(GameId game)
{
    return (game == GAME_UT99 ||
        game == GAME_UG ||
        game == GAME_DX ||
        game == GAME_UE) ? TRUE : FALSE;
}

static int Nav_CountCachedInternetServersForGame(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return 0;

    int totalCached = 0;
    const int masterCount = Data_GetMasterCountForGame(game);

    for (int mi = 0; mi < masterCount; mi++)
    {
        LameMaster* m = Data_GetMasterInternet(game, mi);
        if (m && m->count > 0)
            totalCached += m->count;
    }

    return totalCached;
}

static BOOL Nav_IsShiftDown(void)
{
    return (GetKeyState(VK_SHIFT) & 0x8000) ? TRUE : FALSE;
}

void Nav_OnCommandFetchNewList(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled)
{
    HTREEITEM hSel = TreeView_GetSelection(Nav_GetUi(ctx)->hTreeMenu);
    if (!hSel)
        return;

    TVITEMW item = {};
    item.mask = TVIF_PARAM;
    item.hItem = hSel;

    if (!TreeView_GetItem(Nav_GetUi(ctx)->hTreeMenu, &item))
        return;

    const BOOL forceFetch = Nav_IsShiftDown();

    GameId game = TreeTag_Game(item.lParam);
    TreeNodeKind kind = TreeTag_Kind(item.lParam);
    int masterIndex = TreeTag_MasterIndex(item.lParam);

    if (kind == TREE_NODE_HOME)
    {
        if (activeGame) *activeGame = GAME_NONE;
        if (activeMaster) *activeMaster = NULL;
        if (selectedServer) *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, activeMaster ? *activeMaster : NULL);
        UI_ShowHomePage();

        return;
    }

    if (kind == TREE_NODE_HOME_FAVORITES ||
        kind == TREE_NODE_HOME_GAMEDATE ||
        kind == TREE_NODE_HOME_TUTORIALS ||
        kind == TREE_NODE_HOME_ARCHIVES ||
        kind == TREE_NODE_HOME_MUSIC)
    {
        StatusTextPriority(STATUS_INFO, L"This item does not use game master servers.");
        return;
    }

    if (kind == TREE_NODE_GAME ||
        kind == TREE_NODE_FAVORITES ||
        kind == TREE_NODE_INTERNET ||
        kind == TREE_NODE_LAN)
    {
        if (game <= GAME_NONE || game >= GAME_MAX)
            return;

        if (Nav_IsUnrealFamilyGame(game) && !forceFetch)
        {
            const int totalCached = Nav_CountCachedInternetServersForGame(game);
            if (totalCached > 0)
            {
                StatusReset();
                StatusTextFmtPriority(STATUS_INFO,
                    L"%s already has %d servers in memory. Hold Shift while clicking to force fetch.",
                    Game_PrefixW(game),
                    totalCached);
                return;
            }
        }

        int masterCount = Data_GetMasterCountForGame(game);
        if (masterCount <= 0)
        {
            StatusTextFmtPriority(STATUS_INFO,
                L"No master servers configured for %s.",
                Game_PrefixW(game));
            return;
        }

        UI_SetHomeVisible(FALSE);

        StatusReset();
        StatusTextFmtPriority(STATUS_OPERATION,
            L"Fetching server lists from %d masters for %s...",
            masterCount,
            Game_PrefixW(game));
        StatusProgress_Begin();

        if (masterFetchGame) *masterFetchGame = game;
        if (masterFetchRemaining) *masterFetchRemaining = masterCount;
        if (queryBatchCanceled) *queryBatchCanceled = FALSE;

        for (int mi = 0; mi < masterCount; mi++)
            Query_StartMaster(game, mi);

        return;
    }

    if (kind == TREE_NODE_MASTER)
    {
        if (Nav_IsUnrealFamilyGame(game) && !forceFetch)
        {
            const int totalCached = Nav_CountCachedInternetServersForGame(game);
            if (totalCached > 0)
            {
                StatusReset();
                StatusTextFmtPriority(STATUS_INFO,
                    L"%s already has %d servers in memory. Hold Shift while clicking to force fetch.",
                    Game_PrefixW(game),
                    totalCached);
                return;
            }
        }

        UI_SetHomeVisible(FALSE);

        StatusReset();
        StatusTextFmtPriority(STATUS_OPERATION, L"Fetching new server list from master...");
        StatusProgress_Begin();

        if (masterFetchGame) *masterFetchGame = game;
        if (masterFetchRemaining) *masterFetchRemaining = 1;
        if (queryBatchCanceled) *queryBatchCanceled = FALSE;

        Query_StartMaster(game, masterIndex);
        return;
    }

    (void)combinedQueryGame;
    (void)combinedQueryGen;
}


void Nav_OnCommandRefreshList(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled,
    int* soundPlayedForBatch,
    int* soundBatchGen)
 {
        HTREEITEM hSel = TreeView_GetSelection(Nav_GetUi(ctx)->hTreeMenu);
        if (!hSel)
            return;

        TVITEMW item = { 0 };
        item.mask = TVIF_PARAM;
        item.hItem = hSel;

        if (!TreeView_GetItem(Nav_GetUi(ctx)->hTreeMenu, &item))
            return;

        GameId game = TreeTag_Game(item.lParam);
        TreeNodeKind kind = TreeTag_Kind(item.lParam);
        int masterIndex = TreeTag_MasterIndex(item.lParam);

        if (kind == TREE_NODE_HOME)
        {
            (*activeGame) = GAME_NONE;
            (*activeMaster) = NULL;
            (*selectedServer) = NULL;

            ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, (*activeMaster));
            
            if (ctx && ctx->RebuildPlayersRulesFromServer)
                ctx->RebuildPlayersRulesFromServer(NULL);

            UI_ShowHomePage();

            //StatusTextPriority(STATUS_INFO, L"Home");
            return;
        }

        UI_SetHomeVisible(FALSE);

        if (kind == TREE_NODE_MASTER)
        {
            LameMaster* master = Data_GetMasterInternet(game, masterIndex);

            if (master && master->count > 0)
            {
                StatusTextFmtPriority(STATUS_OPERATION,
                    L"Refreshing %d cached servers...", master->count);
                StatusProgress_Begin();

                (*queryBatchCanceled) = FALSE;
                Query_StartAllServers(game, masterIndex);
            }
            else
            {
                StatusReset();
                StatusTextFmtPriority(STATUS_OPERATION,
                    L"No cached servers. Fetching new server list from master...");
                StatusProgress_Begin();

                (*masterFetchGame) = game;
                (*masterFetchRemaining) = 1;

                (*queryBatchCanceled) = FALSE;
                Query_StartMaster(game, masterIndex);
            }
        }
        else if (kind == TREE_NODE_GAME || kind == TREE_NODE_INTERNET)
        {
            int masterCount = Data_GetMasterCountForGame(game);
            int totalCached = 0;

            if (game <= GAME_NONE || game >= GAME_MAX)
                return;

            for (int mi = 0; mi < masterCount; mi++)
            {
                LameMaster* m = Data_GetMasterInternet(game, mi);
                if (m)
                    totalCached += m->count;
            }

            if (kind == TREE_NODE_GAME)
            {
                // Parent-only node: do NOT switch away from the home/web page.
                (*activeGame) = game;
                (*activeMaster) = NULL;
                (*selectedServer) = NULL;

                ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, (*activeMaster));
                
                if (ctx && ctx->RebuildPlayersRulesFromServer)
                    ctx->RebuildPlayersRulesFromServer(NULL);

                UI_SetHomeVisible(TRUE);

                if (totalCached > 0)
                {
                    StatusReset();
                    StatusTextFmtPriority(STATUS_OPERATION,
                        L"Refreshing %d cached servers for %s...",
                        totalCached,
                        Game_PrefixW(game));
                    StatusProgress_Begin();

                    (*queryBatchCanceled) = FALSE;

                    int gen = Query_BeginBatch();

                    (*soundPlayedForBatch) = 0;
                    (*soundBatchGen) = gen;

                    for (int mi = 0; mi < masterCount; mi++)
                    {
                        LameMaster* m = Data_GetMasterInternet(game, mi);
                        if (m && m->count > 0)
                            Query_StartAllServersWithGen(game, mi, gen);
                    }
                }
                else
                {
                    StatusReset();
                    StatusTextFmtPriority(STATUS_OPERATION,
                        L"No cached servers for %s. Fetching server lists from all masters...",
                        Game_PrefixW(game));
                    StatusProgress_Begin();

                    (*masterFetchGame) = game;
                    (*masterFetchRemaining) = masterCount;
                    (*queryBatchCanceled) = FALSE;

                    for (int mi = 0; mi < masterCount; mi++)
                        Query_StartMaster(game, mi);
                }

                return;
            }

            // TREE_NODE_INTERNET: show and refresh the combined list as usual
            UI_SetHomeVisible(FALSE);

            if (totalCached > 0)
            {
                Master_BuildCombinedForGame(game);
                (*activeGame) = game;
                (*activeMaster) = Data_GetMasterCombined();
                (*selectedServer) = NULL;

                ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, (*activeMaster));

                if (ctx && ctx->RebuildPlayersRulesFromServer)
                    ctx->RebuildPlayersRulesFromServer(NULL);

                StatusTextFmtPriority(STATUS_OPERATION,
                    L"Refreshing %d cached servers...", totalCached);
                StatusProgress_Begin();

                (*queryBatchCanceled) = FALSE;

                int gen = Query_BeginBatch();

                (*soundPlayedForBatch) = 0;
                (*soundBatchGen) = gen;

                for (int mi = 0; mi < masterCount; mi++)
                {
                    LameMaster* m = Data_GetMasterInternet(game, mi);
                    if (m && m->count > 0)
                        Query_StartAllServersWithGen(game, mi, gen);
                }
            }
            else
            {
                StatusReset();
                StatusTextFmtPriority(STATUS_OPERATION,
                    L"No cached servers for %s. Fetching server lists from all masters...",
                    Game_PrefixW(game));
                StatusProgress_Begin();

                (*masterFetchGame) = game;
                (*masterFetchRemaining) = masterCount;
                (*queryBatchCanceled) = FALSE;

                for (int mi = 0; mi < masterCount; mi++)
                    Query_StartMaster(game, mi);
            }
        }
        else if (kind == TREE_NODE_LAN)
        {
            StatusTextPriority(STATUS_INFO, L"LAN browser not implemented yet.");
        }
        else if (kind == TREE_NODE_FAVORITES)
        {
            (*queryBatchCanceled) = FALSE;
            Query_StartFavorites(game);
        }
        else if (kind == TREE_NODE_HOME_FAVORITES)
        {
            (*combinedQueryGame) = GAME_NONE;
            (*combinedQueryGen) = 0;

            (*activeGame) = GAME_NONE;
            (*selectedServer) = NULL;

            UI_SetHomeVisible(FALSE);
            if (ctx && ctx->BuildHomeFavoritesMaster)
                ctx->BuildHomeFavoritesMaster();

            (*activeMaster) = (ctx && ctx->homeFavoritesMaster) ? ctx->homeFavoritesMaster : NULL;

            if ((*activeMaster) && (*activeMaster)->count > 0)
            {
                int gen = Query_BeginBatch();

                (*soundPlayedForBatch) = 0;
                (*soundBatchGen) = gen;

                StatusTextFmtPriority(STATUS_OPERATION,
                    L"Refreshing %d favorite servers from all games...",
                    (*activeMaster)->count);
                StatusProgress_Begin();

                (*queryBatchCanceled) = FALSE;

                for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
                {
                    LameMaster* fav = Data_GetMasterFavorites((GameId)gi);
                    if (fav && fav->count > 0)
                        Query_StartFavoritesWithGen((GameId)gi, gen);
                }
            }
            else
            {
                ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, (*activeMaster));

                if (ctx && ctx->RebuildPlayersRulesFromServer)
                    ctx->RebuildPlayersRulesFromServer(NULL);

                StatusTextPriority(STATUS_INFO, L"No favorites saved.");
            }
        }
}


void Nav_OnTreeReclicked(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled,
    int* soundPlayedForBatch,
    int* soundBatchGen)
{
    HTREEITEM hSel;
    TVITEMW item = {};

    (void)hWnd;
    (void)masterFetchGame;
    (void)masterFetchRemaining;

    if (!activeGame || !activeMaster || !selectedServer ||
        !combinedQueryGame || !combinedQueryGen ||
        !queryBatchCanceled || !soundPlayedForBatch || !soundBatchGen)
        return;

    hSel = TreeView_GetSelection(Nav_GetUi(ctx)->hTreeMenu);
    if (!hSel)
        return;

    item.mask = TVIF_PARAM;
    item.hItem = hSel;

    if (!TreeView_GetItem(Nav_GetUi(ctx)->hTreeMenu, &item))
        return;

    const GameId game = TreeTag_Game(item.lParam);
    const TreeNodeKind kind = UI_NormalizeTreeKind(item.lParam);
    const int masterIndex = TreeTag_MasterIndex(item.lParam);

    // Re-click behavior:
    // - Home: re-show home page
    // - Game: re-navigate setup page
    // - Master: refresh that specific master (query cached servers)
    // - Others: no-op (keep current view stable)
    if (kind == TREE_NODE_HOME)
    {
        UI_ShowHomePage();
        //StatusTextPriority(STATUS_INFO, L"Home");
        return;
    }

    if (kind == TREE_NODE_GAME)
    {
        const wchar_t* prefix = Game_PrefixW(game);

        if (prefix && prefix[0])
        {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(NULL, exePath, _countof(exePath));

            // Remove the executable filename to get the directory
            wchar_t* lastSlash = wcsrchr(exePath, L'\\');
            if (lastSlash)
                *(lastSlash + 1) = L'\0';

            // Build the full path to gamesetup.html
            wchar_t url[MAX_PATH + 128] = {};
            _snwprintf_s(
                url,
                _countof(url),
                _TRUNCATE,
                L"file:///%sui\\gamesetup.html?gameId=%s",
                exePath,
                prefix);

            UI_ShowWebPage(L"Game Setup", url);
        }
        else
        {
            UI_ShowHomePage();
        }

        return;
    }

    if (kind == TREE_NODE_MASTER)
    {
        LameMaster* master = Data_GetMasterInternet(game, masterIndex);
        if (!master)
            return;

        // Ensure we're in list mode for master refresh.
        UI_SetHomeVisible(FALSE);

        *activeGame = game;
        *activeMaster = master;
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        if (master->count > 0)
        {
            const int gen = Query_BeginBatch();
            ResetCompleteSoundForBatch(gen, soundPlayedForBatch, soundBatchGen);

            StatusTextFmtPriority(STATUS_OPERATION, L"Refreshing %d servers from %s...", master->count, master->name);
            StatusProgress_Begin();

            *queryBatchCanceled = FALSE;

            Query_StartAllServersWithGen(game, masterIndex, gen);
        }
        else
        {
            StatusTextFmtPriority(STATUS_INFO,
                L"%s - 0 servers cached. Use 'Fetch New List' to download from master.",
                master->name);
        }

        return;
    }

    if (kind == TREE_NODE_HOME_TUTORIALS)
    {
        // Always reload the tutorials page on re-click
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exePath, _countof(exePath));
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash)
            *(lastSlash + 1) = L'\0';
        wcscat_s(exePath, _countof(exePath), L"ui\\index_tutorials.html");
        // Convert backslashes to forward slashes for URI
        for (wchar_t* p = exePath; *p; ++p) {
            if (*p == L'\\') *p = L'/';
        }
        wchar_t url[MAX_PATH + 32] = {};
        _snwprintf_s(url, _countof(url), _TRUNCATE, L"file:///%s", exePath);

        UI_ShowWebPage(L"Game Setup Tutorials", url);
        StatusTextPriority(STATUS_INFO, L"Game Setup Tutorials");
        return;
    }

    // Default: do nothing; keep view stable.
}

void Nav_OnTreeSelectionChanged(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled,
    int* soundPlayedForBatch,
    int* soundBatchGen)
{
    NMTREEVIEWW* tv;
    TVITEMW item;

    if (!activeGame || !activeMaster || !selectedServer ||
        !combinedQueryGame || !combinedQueryGen ||
        !masterFetchGame || !masterFetchRemaining ||
        !queryBatchCanceled || !soundPlayedForBatch || !soundBatchGen)
        return;

    tv = (NMTREEVIEWW*)GetWindowLongPtrW(Nav_GetUi(ctx)->hwndMain, GWLP_USERDATA);
    (void)tv;

    StatusReset();
    StatusText(L"");

    // Get the currently selected tree item's lParam.
    HTREEITEM hSel = TreeView_GetSelection(Nav_GetUi(ctx)->hTreeMenu);
    if (!hSel)
        return;

    ZeroMemory(&item, sizeof(item));
    item.mask = TVIF_PARAM;
    item.hItem = hSel;

    if (!TreeView_GetItem(Nav_GetUi(ctx)->hTreeMenu, &item))
        return;

    GameId game = TreeTag_Game(item.lParam);
    TreeNodeKind kind = UI_NormalizeTreeKind(item.lParam);
    int masterIndex = TreeTag_MasterIndex(item.lParam);

    // Game nodes -> local setup pages
    if (kind == TREE_NODE_GAME)
    {
        const wchar_t* prefix = Game_PrefixW(game);

        if (prefix && prefix[0])
        {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(NULL, exePath, _countof(exePath));

            // Remove the executable filename to get the directory
            wchar_t* lastSlash = wcsrchr(exePath, L'\\');
            if (lastSlash)
                *(lastSlash + 1) = L'\0';

            // Build the full path to gamesetup.html
            wchar_t url[MAX_PATH + 128] = {};
            _snwprintf_s(
                url,
                _countof(url),
                _TRUNCATE,
                L"file:///%sui\\gamesetup.html?gameId=%s",
                exePath,
                prefix);

            UI_ShowWebPage(L"Game Setup", url);
        }
        else
        {
            UI_ShowHomePage();
        }

        return;
    }

    if (kind == TREE_NODE_HOME)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;

        *activeGame = GAME_NONE;
        *activeMaster = NULL;
        *selectedServer = NULL;

        StatusReset();
        StatusProgress_End();

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        UI_ShowHomePage();
        //StatusTextPriority(STATUS_INFO, L"Home");
        return;
    }

    if (kind == TREE_NODE_HOME_FAVORITES)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;

        *activeGame = GAME_NONE;
        *selectedServer = NULL;

        UI_SetHomeVisible(FALSE);

        if (ctx && ctx->BuildHomeFavoritesMaster)
            ctx->BuildHomeFavoritesMaster();

        *activeMaster = (ctx && ctx->homeFavoritesMaster) ? ctx->homeFavoritesMaster : NULL;

        if (*activeMaster && (*activeMaster)->count > 0)
        {
            int gen = Query_BeginBatch();
            ResetCompleteSoundForBatch(gen, soundPlayedForBatch, soundBatchGen);

            StatusTextFmtPriority(STATUS_OPERATION,
                L"Refreshing %d favorite servers from all games...",
                (*activeMaster)->count);

            for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
            {
                LameMaster* fav = Data_GetMasterFavorites((GameId)gi);
                if (fav && fav->count > 0)
                    Query_StartFavoritesWithGen((GameId)gi, gen);
            }
        }
        else
        {
            StatusTextPriority(STATUS_INFO, L"No favorites saved.");
        }

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        StatusTextFmtPriority(STATUS_INFO, L"Browsing %d favorite servers",
            *activeMaster ? (*activeMaster)->count : 0);
        return;
    }

    if (kind == TREE_NODE_HOME_GAMEDATE)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;

        *activeGame = GAME_NONE;
        *activeMaster = NULL;
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        UI_ShowWebPage(L"GameDate", L"https://gamedate.org/");
        StatusTextPriority(STATUS_INFO, L"GameDate");
        return;
    }

    if (kind == TREE_NODE_HOME_TUTORIALS)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;

        *activeGame = GAME_NONE;
        *activeMaster = NULL;
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        // Build local file:/// URL to ui\tutorials.html
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exePath, _countof(exePath));
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash)
            *(lastSlash + 1) = L'\0';
        wcscat_s(exePath, _countof(exePath), L"ui\\index_tutorials.html");
        // Convert backslashes to forward slashes for URI
        for (wchar_t* p = exePath; *p; ++p) {
            if (*p == L'\\') *p = L'/';
        }
        wchar_t url[MAX_PATH + 32] = {};
        _snwprintf_s(url, _countof(url), _TRUNCATE, L"file:///%s", exePath);

        UI_ShowWebPage(L"Game Setup Tutorials", url);
        StatusTextPriority(STATUS_INFO, L"Game Setup Tutorials");
        return;
    }

    if (kind == TREE_NODE_HOME_ARCHIVES)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;

        *activeGame = GAME_NONE;
        *activeMaster = NULL;
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        UI_ShowWebPage(L"GameSpy Archives", L"https://lamespy.org/archives.html");
        StatusTextPriority(STATUS_INFO, L"GameSpy Archives");
        return;
    }

    if (kind == TREE_NODE_HOME_MUSIC)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;

        *activeGame = GAME_NONE;
        *activeMaster = NULL;
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        UI_ShowWebPage(L"Music", L"https://nanotechstudios.com/");
        StatusTextPriority(STATUS_INFO, L"Music");
        return;
    }

    // Any non-game selection should show list panes
    UI_SetHomeVisible(FALSE);

    if (kind != TREE_NODE_GAME)
    {
        *combinedQueryGame = GAME_NONE;
        *combinedQueryGen = 0;
    }

    if (game > GAME_NONE && game < GAME_MAX)
        *activeGame = game;

    if (kind == TREE_NODE_MASTER)
    {
        LameMaster* master = Data_GetMasterInternet(game, masterIndex);
        if (master)
        {
            *activeMaster = master;

            if (master->count > 0)
            {
                int gen = Query_BeginBatch();
                ResetCompleteSoundForBatch(gen, soundPlayedForBatch, soundBatchGen);

                StatusTextFmt(L"Refreshing %d servers from %s...", master->count, master->name);
                Query_StartAllServersWithGen(game, masterIndex, gen);
                StatusProgress_Begin();
            }
            else
            {
                StatusTextFmt(L"%s - 0 servers cached. Use 'Fetch New List' to download from master.", master->name);
            }
        }
    }
    else if (kind == TREE_NODE_FAVORITES)
    {
        LameMaster* master = Data_GetMasterFavorites(game);
        if (master)
        {
            *activeMaster = master;

            if (master->count > 0)
            {
                int gen = Query_BeginBatch();
                ResetCompleteSoundForBatch(gen, soundPlayedForBatch, soundBatchGen);

                StatusTextFmtPriority(STATUS_OPERATION, L"Refreshing %d %s favorites...",
                    master->count, Game_PrefixW(game));

                Query_StartFavoritesWithGen(game, gen);
            }
            else
            {
                StatusTextFmtPriority(STATUS_INFO, L"No %s favorites (right-click a server to add)",
                    Game_PrefixW(game));
            }
        }
    }
    else if (kind == TREE_NODE_INTERNET)
    {
        *activeGame = game;
        Master_BuildCombinedForGame(game);
        *activeMaster = Data_GetMasterCombined();
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        int masterCount = Data_GetMasterCountForGame(game);
        int totalServers = 0;
        for (int mi = 0; mi < masterCount; mi++)
        {
            LameMaster* m = Data_GetMasterInternet(game, mi);
            if (m)
                totalServers += m->count;
        }

        if (totalServers == 0)
        {
            StatusTextFmtPriority(STATUS_OPERATION,
                L"Fetching server lists from all masters for %s...",
                Game_PrefixW(game));
            StatusProgress_Begin();

            *masterFetchGame = game;
            *masterFetchRemaining = masterCount;

            for (int mi = 0; mi < masterCount; mi++)
                Query_StartMaster(game, mi);

            return;
        }

        if (*activeMaster && (*activeMaster)->count > 0)
        {
            if (*combinedQueryGame != game || *combinedQueryGen == 0)
            {
                *combinedQueryGame = game;
                *combinedQueryGen = Query_BeginBatch();
                ResetCompleteSoundForBatch(*combinedQueryGen, soundPlayedForBatch, soundBatchGen);
            }

            StatusTextFmtPriority(STATUS_OPERATION,
                L"Refreshing %d cached servers...", (*activeMaster)->count);
            StatusProgress_Begin(); // <- missing line

            for (int mi = 0; mi < masterCount; mi++)
            {
                LameMaster* m = Data_GetMasterInternet(game, mi);
                if (m && m->count > 0)
                {
                    Query_StartAllServersWithGen(game, mi, *combinedQueryGen);
                }
            }
        }

        return;
    }
    else if (kind == TREE_NODE_LAN)
    {
        *activeGame = game;
        *activeMaster = NULL;
        *selectedServer = NULL;

        ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);

        if (ctx && ctx->RebuildPlayersRulesFromServer)
            ctx->RebuildPlayersRulesFromServer(NULL);

        StatusTextPriority(STATUS_INFO, L"LAN browser not implemented yet.");
        return;
    }

    // Always rebuild list immediately on selection switch
    ServerListView_Rebuild(Nav_GetUi(ctx)->hServerList, Nav_GetUi(ctx)->hTreeMenu, *activeMaster);
    *selectedServer = NULL;

    if (ctx && ctx->RebuildPlayersRulesFromServer)
        ctx->RebuildPlayersRulesFromServer(NULL);

    (void)hWnd;
}

void Nav_OnTreeSelectionChangedS(const NavContext* ctx, HWND hWnd, UiSession* s)
{
    if (!s)
        return;

    const NavUi* ui = Nav_GetUi(ctx);
    if (!ui)
        return;

    Nav_OnTreeSelectionChanged(
        ctx,
        hWnd,
        &s->activeGame,
        &s->activeMaster,
        &s->selectedServer,
        &s->combinedQueryGame,
        &s->combinedQueryGen,
        &s->masterFetchGame,
        &s->masterFetchRemaining,
        &s->queryBatchCanceled,
        &s->soundPlayedForBatch,
        &s->soundBatchGen);
}

void Nav_OnTreeReclickedS(const NavContext* ctx, HWND hWnd, UiSession* s)
{
    if (!s)
        return;

    const NavUi* ui = Nav_GetUi(ctx);
    if (!ui)
        return;

    Nav_OnTreeReclicked(
        ctx,
        hWnd,
        &s->activeGame,
        &s->activeMaster,
        &s->selectedServer,
        &s->combinedQueryGame,
        &s->combinedQueryGen,
        &s->masterFetchGame,
        &s->masterFetchRemaining,
        &s->queryBatchCanceled,
        &s->soundPlayedForBatch,
        &s->soundBatchGen);
}

void Nav_OnCommandFetchNewListS(const NavContext* ctx, HWND hWnd, UiSession* s)
{
    if (!s)
        return;

    const NavUi* ui = Nav_GetUi(ctx);
    if (!ui)
        return;

    Nav_OnCommandFetchNewList(
        ctx,
        hWnd,
        &s->activeGame,
        &s->activeMaster,
        &s->selectedServer,
        &s->combinedQueryGame,
        &s->combinedQueryGen,
        &s->masterFetchGame,
        &s->masterFetchRemaining,
        &s->queryBatchCanceled);
}

void Nav_OnCommandRefreshListS(const NavContext* ctx, HWND hWnd, UiSession* s)
{
    if (!s)
        return;

    const NavUi* ui = Nav_GetUi(ctx);
    if (!ui)
        return;

    Nav_OnCommandRefreshList(
        ctx,
        hWnd,
        &s->activeGame,
        &s->activeMaster,
        &s->selectedServer,
        &s->combinedQueryGame,
        &s->combinedQueryGen,
        &s->masterFetchGame,
        &s->masterFetchRemaining,
        &s->queryBatchCanceled,
        &s->soundPlayedForBatch,
        &s->soundBatchGen);
}