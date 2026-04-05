#include "LameUiMessages.h"
#include "LameHomeView.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameListTrickle.h"
#include "LameNet.h"
#include "LameServerListView.h"
#include "LameStatusBar.h"
#include "LameTree.h"
#include "LameTreeTag.h"
#include "resource.h"

static void UiMessages_RestoreExpandedGames(HWND treeMenu, const BOOL expandedGames[GAME_MAX])
{
    if (!treeMenu || !IsWindow(treeMenu) || !expandedGames)
        return;

    HTREEITEM homeNode = TreeView_GetRoot(treeMenu);
    HTREEITEM gamesRoot = homeNode ? TreeView_GetNextSibling(treeMenu, homeNode) : NULL;
    if (!gamesRoot)
        return;

    for (HTREEITEM gameNode = TreeView_GetChild(treeMenu, gamesRoot);
        gameNode;
        gameNode = TreeView_GetNextSibling(treeMenu, gameNode))
    {
        TVITEMW it = {};
        it.mask = TVIF_PARAM;
        it.hItem = gameNode;

        if (!TreeView_GetItem(treeMenu, &it))
            continue;

        if (TreeTag_Kind(it.lParam) != TREE_NODE_GAME)
            continue;

        GameId g = TreeTag_Game(it.lParam);
        if (g > GAME_NONE && g < GAME_MAX && expandedGames[g])
            TreeView_Expand(treeMenu, gameNode, TVE_EXPAND);
    }
}

static int UiMessages_CountVisibleServers(HWND treeMenu, const LameMaster* m)
{
    if (!m)
        return 0;

    int isFavoritesView = ServerListView_IsFavoritesView(treeMenu);

    int vis = 0;
    for (int i = 0; i < m->count; i++)
    {
        const LameServer* s = m->servers[i];
        if (!s)
            continue;

        if (isFavoritesView)
        {
            if (g_config.hideDeadFavorites && s->state != QUERY_DONE)
                continue;
        }
        else
        {
            if (g_config.hideDeadInternets)
            {
                if (s->state == QUERY_FAILED || s->state == QUERY_CANCELED)
                    continue;
            }
        }

        vis++;
    }

    return vis;
}

void UiMessages_OnAppMessage(
    const NavContext* navCtx,
    const UiSession* session,
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    (void)hWnd;

    if (!navCtx || !navCtx->ui || !session)
        return;

    UiSession* s = (UiSession*)session;
    const NavUi* ui = navCtx->ui;

    if (msg == WM_APP_SHOW_HOME_STARTUP)
    {
        HTREEITEM hSel = TreeView_GetSelection(ui->hTreeMenu);
        if (hSel)
        {
            TVITEMW item = {};
            item.mask = TVIF_PARAM;
            item.hItem = hSel;

            if (TreeView_GetItem(ui->hTreeMenu, &item))
            {
                TreeNodeKind kind = TreeTag_Kind(item.lParam);

                if (kind == TREE_NODE_HOME)
                {
                    HomeView_ShowHomePage();
                    //StatusTextPriority(STATUS_INFO, L"Home");
                }
                else if (kind == TREE_NODE_HOME_FAVORITES)
                {
                    s->combinedQueryGame = GAME_NONE;
                    s->combinedQueryGen = 0;

                    s->activeGame = GAME_NONE;
                    s->selectedServer = NULL;

                    HomeView_SetVisible(FALSE);
                    HomeView_BuildHomeFavoritesMaster();
                    s->activeMaster = HomeView_GetHomeFavoritesMaster();

                    if (s->activeMaster && s->activeMaster->count > 0)
                    {
                        int gen = Query_BeginBatch();

                        s->soundPlayedForBatch = 0;
                        s->soundBatchGen = gen;

                        StatusTextFmtPriority(STATUS_OPERATION,
                            L"Refreshing %d favorite servers from all games...",
                            s->activeMaster->count);

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

                    ServerListView_Rebuild(ui->hServerList, ui->hTreeMenu, s->activeMaster);
                    if (navCtx->RebuildPlayersRulesFromServer)
                        navCtx->RebuildPlayersRulesFromServer(NULL);
                }
            }
        }
        return;
    }
    else if (msg == WM_APP_QUERY_FLUSH)
    {
        ServerListTrickle_OnAppFlush(hWnd, s->activeMaster, &s->selectedServer);

        // Auto-sort internet/master views to populated servers as results arrive.
        // Keep Favorites behavior unchanged.
        if (s->activeMaster && !ServerListView_IsFavoritesView(ui->hTreeMenu) && s->activeMaster->count > 1)
        {
            Data_Lock();

            qsort(s->activeMaster->servers,
                s->activeMaster->count,
                sizeof(LameServer*),
                LameServerPtrCompareAutoPopulated);

            Data_Unlock();

            ServerListView_Rebuild(ui->hServerList, ui->hTreeMenu, s->activeMaster);
        }

        if (navCtx->RebuildPlayersRulesFromServer)
            navCtx->RebuildPlayersRulesFromServer(s->selectedServer);

        return;
    }
    else if (msg == WM_APP_QUERY_RESULT)
    {
        PostMessageW(hWnd, WM_APP_QUERY_FLUSH, 0, 0);
        return;
    }
    else if (msg == WM_APP_QUERY_DONE)
    {
        if (s->queryBatchCanceled)
        {
            s->queryBatchCanceled = FALSE;
            StatusReset();
            StatusProgress_End();
            return;
        }

        if (g_config.soundFlags & LSOUND_SCAN_COMPLETE)
        {
            HTREEITEM hSel = TreeView_GetSelection(ui->hTreeMenu);
            if (hSel)
            {
                TVITEMW item = { 0 };
                item.mask = TVIF_PARAM;
                item.hItem = hSel;

                if (TreeView_GetItem(ui->hTreeMenu, &item))
                {
                    TreeNodeKind kind = TreeTag_Kind(item.lParam);

                    if (kind == TREE_NODE_INTERNET || kind == TREE_NODE_MASTER)
                    {
                        if (!s->soundPlayedForBatch &&
                            s->soundBatchGen != 0 &&
                            s->soundBatchGen == Query_GetGeneration())
                        {
                            s->soundPlayedForBatch = 1;
                            PlayLameSound(IDR_SOUND_COMPLETE, SOUND_ALL);
                        }
                    }
                }
            }
        }

        StatusReset();
        StatusProgress_End();

        ServerListTrickle_FlushNow(hWnd, 4096, s->activeMaster, &s->selectedServer);

        if (navCtx->RebuildPlayersRulesFromServer)
            navCtx->RebuildPlayersRulesFromServer(s->selectedServer);

        InvalidateRect(ui->hServerList, NULL, FALSE);

        HTREEITEM hSel = TreeView_GetSelection(ui->hTreeMenu);
        if (!hSel)
        {
            if (s->activeMaster)
            {
                ServerListView_Rebuild(ui->hServerList, ui->hTreeMenu, s->activeMaster);
                StatusTextFmtPriority(STATUS_IMPORTANT,
                    L"Browsing %d servers", UiMessages_CountVisibleServers(ui->hTreeMenu, s->activeMaster));

                StatusProgress_End();
            }
            return;
        }

        TVITEMW item = { 0 };
        item.mask = TVIF_PARAM;
        item.hItem = hSel;

        if (!TreeView_GetItem(ui->hTreeMenu, &item))
            return;

        GameId game = TreeTag_Game(item.lParam);
        TreeNodeKind kind = TreeTag_Kind(item.lParam);
        int masterIndex = TreeTag_MasterIndex(item.lParam);

        if (kind == TREE_NODE_INTERNET)
        {
            int masterCount = Data_GetMasterCountForGame(game);
            for (int mi = 0; mi < masterCount; mi++)
            {
                LameMaster* m = Data_GetMasterInternet(game, mi);
                if (m)
                    Tree_UpdateMasterNodeBold(ui->hTreeMenu, game, mi, UiMessages_CountVisibleServers(ui->hTreeMenu, m) > 0);
            }

            Master_BuildCombinedForGame(game);
            s->activeMaster = Data_GetMasterCombined();

            StatusTextFmtPriority(STATUS_IMPORTANT,
                L"Browsing %d servers",
                s->activeMaster ? UiMessages_CountVisibleServers(ui->hTreeMenu, s->activeMaster) : 0);
            return;
        }

        if (kind == TREE_NODE_HOME_FAVORITES)
        {
            ServerListView_Rebuild(ui->hServerList, ui->hTreeMenu, s->activeMaster);

            StatusTextFmtPriority(STATUS_IMPORTANT,
                L"Browsing %d favorite servers",
                s->activeMaster ? UiMessages_CountVisibleServers(ui->hTreeMenu, s->activeMaster) : 0);
            return;
        }

        if (kind == TREE_NODE_LAN)
        {
            StatusTextPriority(STATUS_INFO, L"LAN browser not implemented yet.");
            return;
        }

        if (kind == TREE_NODE_MASTER)
        {
            LameMaster* master = Data_GetMasterInternet(game, masterIndex);
            if (master)
            {
                Tree_UpdateMasterNodeBold(ui->hTreeMenu, game, masterIndex, UiMessages_CountVisibleServers(ui->hTreeMenu, master) > 0);
                Tree_UpdateGameNodeBold(ui->hTreeMenu, game);

                if (master == s->activeMaster)
                {
                    StatusTextFmtPriority(STATUS_IMPORTANT,
                        L"Browsing %d servers",
                        UiMessages_CountVisibleServers(ui->hTreeMenu, master));
                }
            }
            return;
        }

        if (kind == TREE_NODE_FAVORITES)
        {
            LameMaster* favMaster = Data_GetMasterFavorites(game);
            if (favMaster)
            {
                if (favMaster == s->activeMaster)
                {
                    StatusTextFmtPriority(STATUS_IMPORTANT,
                        L"Browsing %d favorites",
                        UiMessages_CountVisibleServers(ui->hTreeMenu, favMaster));
                }
            }
            return;
        }

        return;
    }
    else if (msg == WM_APP_MASTER_DONE)
    {
        if (s->queryBatchCanceled)
            return;

        if (InterlockedCompareExchange((volatile LONG*)&s->masterFetchRemaining, 0, 0) <= 0 &&
            s->masterFetchGame == GAME_NONE)
            return;

        GameId game = (GameId)wParam;
        int masterIndex = (int)lParam;

        int totalMasters = Data_GetMasterCountForGame(game);

        LameMaster* master = Data_GetMasterInternet(game, masterIndex);
        if (!master)
            return;

        HTREEITEM hOldSel = TreeView_GetSelection(ui->hTreeMenu);
        GameId oldGame = GAME_NONE;
        TreeNodeKind oldKind = TREE_NODE_NONE;
        int oldMasterIndex = 0;

        if (hOldSel)
        {
            TVITEMW oldItem = { 0 };
            oldItem.mask = TVIF_PARAM;
            oldItem.hItem = hOldSel;
            if (TreeView_GetItem(ui->hTreeMenu, &oldItem))
            {
                oldGame = TreeTag_Game(oldItem.lParam);
                oldKind = TreeTag_Kind(oldItem.lParam);
                oldMasterIndex = TreeTag_MasterIndex(oldItem.lParam);
            }
        }

        BOOL expandedGames[GAME_MAX] = { 0 };
        {
            HTREEITEM root = TreeView_GetRoot(ui->hTreeMenu);
            if (root)
            {
                root = TreeView_GetNextSibling(ui->hTreeMenu, root);
                if (!root)
                    return;

                HTREEITEM gn = TreeView_GetChild(ui->hTreeMenu, root);
                while (gn)
                {
                    TVITEMW gi = { 0 };
                    gi.mask = TVIF_PARAM | TVIF_STATE;
                    gi.stateMask = TVIS_EXPANDED;
                    gi.hItem = gn;
                    if (TreeView_GetItem(ui->hTreeMenu, &gi))
                    {
                        GameId g = TreeTag_Game(gi.lParam);
                        if (g > GAME_NONE && g < GAME_MAX)
                            expandedGames[g] = (gi.state & TVIS_EXPANDED) ? TRUE : FALSE;
                    }
                    gn = TreeView_GetNextSibling(ui->hTreeMenu, gn);
                }
            }
        }

        s->suppressTreeSelChanged = TRUE;
        SendMessageW(ui->hTreeMenu, WM_SETREDRAW, FALSE, 0);
        UI_BuildTree();

        UiMessages_RestoreExpandedGames(ui->hTreeMenu, expandedGames);

        for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
        {
            const LameGameDescriptor* gDesc = Game_GetDescriptor((GameId)gi);
            if (!gDesc || gDesc->id == GAME_NONE)
                continue;

            int mc = Data_GetMasterCountForGame((GameId)gi);
            for (int mi = 0; mi < mc; mi++)
            {
                LameMaster* m = Data_GetMasterInternet((GameId)gi, mi);
                if (m && m->count > 0)
                    Tree_UpdateMasterNodeBold(ui->hTreeMenu, (GameId)gi, mi, TRUE);
            }
        }

        {
            HTREEITEM homeNode = TreeView_GetRoot(ui->hTreeMenu);
            HTREEITEM gamesRoot = homeNode ? TreeView_GetNextSibling(ui->hTreeMenu, homeNode) : NULL;
            HTREEITEM restoreItem = NULL;

            if (gamesRoot && oldKind != TREE_NODE_NONE)
            {
                HTREEITEM gameNode = TreeView_GetChild(ui->hTreeMenu, gamesRoot);
                while (gameNode)
                {
                    TVITEMW it = { 0 };
                    it.mask = TVIF_PARAM;
                    it.hItem = gameNode;

                    if (TreeView_GetItem(ui->hTreeMenu, &it))
                    {
                        if (TreeTag_Game(it.lParam) == oldGame &&
                            TreeTag_Kind(it.lParam) == TREE_NODE_GAME)
                        {
                            TreeView_Expand(ui->hTreeMenu, gameNode, TVE_EXPAND);

                            if (oldKind == TREE_NODE_GAME)
                            {
                                restoreItem = gameNode;
                            }
                            else
                            {
                                HTREEITEM child = TreeView_GetChild(ui->hTreeMenu, gameNode);
                                while (child)
                                {
                                    TVITEMW ci = { 0 };
                                    ci.mask = TVIF_PARAM;
                                    ci.hItem = child;

                                    if (TreeView_GetItem(ui->hTreeMenu, &ci))
                                    {
                                        if (TreeTag_Kind(ci.lParam) == oldKind &&
                                            TreeTag_Game(ci.lParam) == oldGame &&
                                            TreeTag_MasterIndex(ci.lParam) == oldMasterIndex)
                                        {
                                            restoreItem = child;
                                            break;
                                        }
                                    }

                                    child = TreeView_GetNextSibling(ui->hTreeMenu, child);
                                }

                                if (!restoreItem)
                                    restoreItem = gameNode;
                            }

                            break;
                        }
                    }

                    gameNode = TreeView_GetNextSibling(ui->hTreeMenu, gameNode);
                }
            }

            if (!restoreItem && gamesRoot && oldGame > GAME_NONE && oldGame < GAME_MAX)
            {
                HTREEITEM gameNode = TreeView_GetChild(ui->hTreeMenu, gamesRoot);
                while (gameNode)
                {
                    TVITEMW it = { 0 };
                    it.mask = TVIF_PARAM;
                    it.hItem = gameNode;

                    if (TreeView_GetItem(ui->hTreeMenu, &it) &&
                        TreeTag_Game(it.lParam) == oldGame &&
                        TreeTag_Kind(it.lParam) == TREE_NODE_GAME)
                    {
                        restoreItem = gameNode;
                        break;
                    }

                    gameNode = TreeView_GetNextSibling(ui->hTreeMenu, gameNode);
                }
            }

            if (restoreItem)
                TreeView_SelectItem(ui->hTreeMenu, restoreItem);
        }

        SendMessageW(ui->hTreeMenu, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(ui->hTreeMenu, NULL, TRUE);
        s->suppressTreeSelChanged = FALSE;

        if (game == s->masterFetchGame && s->masterFetchRemaining > 0)
        {
            s->masterFetchRemaining--;

            if (s->masterFetchRemaining == 0)
            {
                Master_BuildCombinedForGame(game);
                LameMaster* combined = Data_GetMasterCombined();
                int totalServers = combined ? combined->count : 0;

                if (totalServers <= 0)
                {
                    StatusProgress_End();
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                }

                s->masterFetchGame = GAME_NONE;
            }
        }

        StatusReset();

        if (master->count > 0)
        {
            StatusTextFmtPriority(STATUS_OPERATION,
                L"Master %d/%d complete (%d servers). Now querying servers...",
                masterIndex + 1,
                totalMasters,
                master->count);
        }
        else
        {
            StatusTextFmtPriority(STATUS_IMPORTANT,
                L"Master %d/%d complete: 0 servers found",
                masterIndex + 1,
                totalMasters);
        }

        HTREEITEM hSel = TreeView_GetSelection(ui->hTreeMenu);
        if (hSel)
        {
            TVITEMW item = { 0 };
            item.mask = TVIF_PARAM;
            item.hItem = hSel;

            if (TreeView_GetItem(ui->hTreeMenu, &item))
            {
                GameId selectedGame = TreeTag_Game(item.lParam);
                TreeNodeKind selectedKind = TreeTag_Kind(item.lParam);
                int selectedMasterIndex = TreeTag_MasterIndex(item.lParam);

                if (selectedGame == game &&
                    selectedKind == TREE_NODE_MASTER &&
                    selectedMasterIndex == masterIndex)
                {
                    s->activeMaster = master;

                    ServerListView_Rebuild(ui->hServerList, ui->hTreeMenu, s->activeMaster);

                    if (master->count > 0)
                    {
                        int gen = Query_BeginBatch();

                        s->soundPlayedForBatch = 0;
                        s->soundBatchGen = gen;

                        StatusTextFmtPriority(STATUS_OPERATION,
                            L"Querying %d servers...", master->count);

                        Query_StartAllServersWithGen(game, masterIndex, gen);
                    }
                    else
                    {
                        StatusTextFmtPriority(STATUS_INFO,
                            L"Browsing 0 servers");

                        StatusProgress_End();
                    }
                    return;
                }

                if (selectedGame == game && selectedKind == TREE_NODE_INTERNET)
                {
                    Master_BuildCombinedForGame(game);
                    s->activeMaster = Data_GetMasterCombined();

                    ServerListView_Rebuild(ui->hServerList, ui->hTreeMenu, s->activeMaster);

                    if (s->combinedQueryGame != game || s->combinedQueryGen == 0)
                    {
                        s->combinedQueryGame = game;
                        s->combinedQueryGen = Query_BeginBatch();

                        s->soundPlayedForBatch = 0;
                        s->soundBatchGen = s->combinedQueryGen;
                    }

                    StatusTextFmtPriority(STATUS_OPERATION,
                        L"Refreshing %d cached servers...", s->activeMaster->count);

                    int masterCount = Data_GetMasterCountForGame(game);
                    for (int mi = 0; mi < masterCount; mi++)
                    {
                        LameMaster* m = Data_GetMasterInternet(game, mi);
                        if (m && m->count > 0)
                            Query_StartAllServersWithGen(game, mi, s->combinedQueryGen);
                    }
                }
            }
        }

        return;
    }
}