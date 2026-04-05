// LameUI.cpp : UI elements and main window management

#include "LameCore.h"
#include "LameWin.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameUI.h"
#include "LameNet.h"
#include "LameStatusBar.h"
#include "LameTreeTag.h"
#include "LameFonts.h"
#include "LameFilters.h"
#include "LameTree.h"
#include "LameNav.h"
#include "LameServerListView.h"
#include "LameListTrickle.h"
#include "LameListViews.h"
#include "LameUiMessages.h"
#include "LameHomeView.h"
#include "LameSplitter.h"
#include "LameUiSession.h"
#include "LameToolbar.h"
#include "resource.h"
#include <vector>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <wil/com.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

LameUI g_ui;

static UiSession g_session =
{
    GAME_Q3,
    NULL,
    NULL,
    FALSE,
    GAME_NONE,
    0,
    GAME_NONE,
    0,
    FALSE,
    0,
    0
};

static NavUi g_navUi = { NULL, NULL, NULL };

static BOOL g_manualVersionCheckRequested = FALSE;

void UI_RequestManualVersionCheck(void)
{
    g_manualVersionCheckRequested = TRUE;
    StartVersionCheckAsync();
}

static void UI_OnMasterDone(GameId game, int masterIndex, int serverCount, void* user)
{
    HWND hwnd = (HWND)user;
    if (hwnd && IsWindow(hwnd))
        PostMessageW(hwnd, WM_APP_MASTER_DONE, (WPARAM)game, (LPARAM)masterIndex);
    (void)serverCount;
}

static void UI_OnAllDone(void* user)
{
    HWND hwnd = (HWND)user;
    if (hwnd && IsWindow(hwnd))
        PostMessageW(hwnd, WM_APP_QUERY_DONE, 0, 0);
}

// ------------------------------------------------------------
// Misc / small helpers
// ------------------------------------------------------------

void PlayLameSound(int resourceId, int allow)
{
    /*if (g_config.sounds < allow)
        return;*/

    int playFlags = SND_ASYNC | SND_RESOURCE;

    PlaySoundW(MAKEINTRESOURCEW(resourceId), NULL, playFlags);
}

void SetMainIcon(WNDCLASSEXW* wc, HINSTANCE hInst)
{
    wc->hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_LAMESPY),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR);

    wc->hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_LAMESPY),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR);
}

void DrawTopLine(HWND hWnd, HDC hdc)
{
    RECT rc;
    RECT rLine;
    int y;

    GetClientRect(hWnd, &rc);

    rLine = rc;
    y = 0;
    rLine.top = y;
    rLine.bottom = y + 2;

    DrawEdge(hdc, &rLine, EDGE_ETCHED, BF_BOTTOM);
}

static void InitCommonCtrl()
{
    INITCOMMONCONTROLSEX icc;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES |
        ICC_TREEVIEW_CLASSES |
        ICC_BAR_CLASSES;

    InitCommonControlsEx(&icc);
}

//
// Version checking
//

static void UI_OnVersionCheckResult(VersionCheckResult result, void* user)
{
    HWND hwnd = (HWND)user;
    if (hwnd && IsWindow(hwnd))
        PostMessageW(hwnd, WM_APP_VERSION_CHECK_RESULT, (WPARAM)result, 0);
}

static HRESULT CALLBACK UI_UpdateDialogCallback(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR refData)
{
    (void)wParam;
    (void)refData;

    if (msg == TDN_HYPERLINK_CLICKED)
    {
        const wchar_t* url = (const wchar_t*)lParam;
        ShellExecuteW(hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    }

    return S_OK;
}

static void UI_ShowVersionOutdatedDialog(HWND hwnd)
{
    TASKDIALOGCONFIG tdc = { 0 };
    tdc.cbSize = sizeof(tdc);
    tdc.hwndParent = hwnd;
    tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    tdc.dwCommonButtons = TDCBF_OK_BUTTON;
    tdc.pszWindowTitle = L"LameSpy - Update Available";
    tdc.pszMainIcon = TD_INFORMATION_ICON;
    tdc.pszMainInstruction = L"New version available!";
    tdc.pszContent =
        L"Please visit: "
        L"<a href=\"https://lamespy.org?page=lamespy\">https://lamespy.org?page=lamespy</a>";
    tdc.pfCallback = UI_UpdateDialogCallback;

    if (FAILED(TaskDialogIndirect(&tdc, NULL, NULL, NULL)))
    {
        MessageBoxW(hwnd,
            L"New version available!. Please visit: https://lamespy.org?page=lamespy",
            L"LameSpy - Update Available",
            MB_OK | MB_ICONINFORMATION);
    }
}


// ------------------------------------------------------------
// Query / network notification
// ------------------------------------------------------------

// Call this once after hwndMain exists (right after window creation / init):
void UI_InitNetNotify(HWND hwndMain)
{
    LameNetNotify n = { 0 };
    n.OnMasterDone = UI_OnMasterDone;
    n.OnAllDone = UI_OnAllDone;
    n.user = hwndMain;

    Query_SetNotify(&n);
    VersionCheck_SetNotify(UI_OnVersionCheckResult, hwndMain);
}

void UI_CancelAllQueries(HWND hWnd)
{
    if (!Query_HasActiveQueries())
    {
        StatusTextFmtPriority(STATUS_INFO, L"No active queries.");
        return;
    }

    g_session.queryBatchCanceled = TRUE;

    // First ask workers to stop.
    Query_Stop();

    KillTimer(hWnd, IDT_QUERY_FLUSH);

    ServerListTrickle_FlushNow(hWnd, 4096, g_session.activeMaster, &g_session.selectedServer);

    // Now invalidate the old batch so any later completions are ignored.
    Query_IncrementGeneration();

    // Drop anything still left queued after the flush.
    ServerListTrickle_ClearPending();

    StatusReset();
    StatusProgress_End();
    StatusTextFmtPriority(STATUS_IMPORTANT, L"Canceled. Showing partial results.");

    SetCursor(LoadCursor(NULL, IDC_ARROW));

    g_session.masterFetchGame = GAME_NONE;
    g_session.masterFetchRemaining = 0;
    g_session.combinedQueryGame = GAME_NONE;
    g_session.combinedQueryGen = 0;

    if (g_config.soundFlags & LSOUND_UPDATE_ABORT)
        PlayLameSound(IDR_SOUND_ABORT, SOUND_ALL);
}

// ------------------------------------------------------------
// Session / state accessors
// ------------------------------------------------------------

BOOL UI_GetSuppressTreeSelChanged(void)
{
    return g_session.suppressTreeSelChanged;
}

void UI_SetSuppressTreeSelChanged(BOOL suppress)
{
    g_session.suppressTreeSelChanged = suppress ? TRUE : FALSE;
}

// ------------------------------------------------------------
// Server details panes (players/rules)
// ------------------------------------------------------------

static BOOL UI_IsUnrealPlayerRuleKey(const wchar_t* key)
{
    if (!key || !key[0])
        return FALSE;

    static const wchar_t* const prefixes[] =
    {
        L"player_",
        L"frags_",
        L"ping_",
        L"team_",
        L"mesh_",
        L"skin_",
        L"face_",
        L"ngsecret_",
    };

    for (size_t i = 0; i < _countof(prefixes); i++)
    {
        const wchar_t* p = prefixes[i];
        const size_t n = wcslen(p);

        if (_wcsnicmp(key, p, n) == 0)
            return TRUE;
    }

    return FALSE;
}

static void UI_RebuildPlayersRulesFromServer(const LameServer* s)
{
    int i;
    LVITEMW lvi;
    wchar_t score[32];
    wchar_t ping[32];

    ListView_DeleteAllItems(g_ui.hPlayerList);
    ListView_DeleteAllItems(g_ui.hRulesList);

    if (!s)
        return;

    // Fill player list
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;

    for (i = 0; i < s->playerCount; i++)
    {
        lvi.iItem = i;

        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)s->playerList[i].name;
        ListView_InsertItem(g_ui.hPlayerList, &lvi);

        swprintf_s(score, _countof(score), L"%d", s->playerList[i].score);
        lvi.iSubItem = 1;
        lvi.pszText = score;
        ListView_SetItem(g_ui.hPlayerList, &lvi);

        swprintf_s(ping, _countof(ping), L"%d", s->playerList[i].ping);
        lvi.iSubItem = 2;
        lvi.pszText = ping;
        ListView_SetItem(g_ui.hPlayerList, &lvi);
    }

    // re - zero lvi before the rules list loop
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;

    // Fill rules list (skip UE per-player fields that also show up in player list)
    int outRuleRow = 0;
    for (i = 0; i < s->ruleCount; i++)
    {
        const wchar_t* key = s->ruleList[i].key;
        if (UI_IsUnrealPlayerRuleKey(key))
            continue;

        lvi.iItem = outRuleRow++;

        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)key;
        ListView_InsertItem(g_ui.hRulesList, &lvi);

        lvi.iSubItem = 1;
        lvi.pszText = (LPWSTR)s->ruleList[i].value;
        ListView_SetItem(g_ui.hRulesList, &lvi);
    }

    // Append IP:Port at the end of the rules list
    {
        wchar_t addr[96] = {};
        swprintf_s(addr, _countof(addr), L"%s:%d", s->ip, s->port);

        lvi.iItem = outRuleRow;   // end of list
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)L"IP:Port";
        ListView_InsertItem(g_ui.hRulesList, &lvi);

        lvi.iSubItem = 1;
        lvi.pszText = addr;
        ListView_SetItem(g_ui.hRulesList, &lvi);
    }
}

// ------------------------------------------------------------
// Navigation / view refresh
// ------------------------------------------------------------

static const NavContext g_navCtx =
{
    &g_navUi,
    HomeView_GetHomeFavoritesMaster(),
    &HomeView_BuildHomeFavoritesMaster,
    &UI_RebuildPlayersRulesFromServer
};

void UI_RefreshCurrentView(void)
{
    if (HomeView_IsVisible())
        return;

    // Do NOT rebuild combined list here; it changes insertion order and looks like a "re-sort".
    // Tree selection changes already rebuild the correct master.
    ServerListView_Rebuild(g_ui.hServerList, g_ui.hTreeMenu, g_session.activeMaster);

    g_session.selectedServer = NULL;
    UI_RebuildPlayersRulesFromServer(NULL);
}

void UI_RefreshActiveTreeSelection(void)
{
    if (!g_ui.hwndMain || !IsWindow(g_ui.hwndMain))
        return;

    Nav_OnTreeSelectionChangedS(&g_navCtx, g_ui.hwndMain, &g_session);
}

#if 0
static int UI_CountVisibleServers(const LameMaster* m)
{
    if (!m)
        return 0;

    int isFavoritesView = ServerListView_IsFavoritesView(g_ui.hTreeMenu);

    int vis = 0;
    for (int i = 0; i < m->count; i++)
    {
        const LameServer* s = m->servers[i];
        if (!s)
            continue;

        if (!isFavoritesView)
        {
            if (s->state == QUERY_FAILED || s->state == QUERY_CANCELED)
                continue;
        }

        vis++;
    }
    return vis;
}
#endif
// ------------------------------------------------------------
// Main window construction
// ------------------------------------------------------------

static void BuildMainWindow(HWND hwnd)
{
#if LAMESPY_USE_NEW_FONTS
    // Create custom fonts (9pt for lists, 9pt for tree)
    UI_SetListFont(CreateListFont(g_config.rightPaneFontPt, FALSE));
    UI_SetTreeFont(CreateTreeFont(g_config.leftPaneFontPt, FALSE));
#endif
    g_ui.hTreeMenu = Tree_Create(hwnd, g_ui.hInst);
    g_ui.hServerList = ListViews_CreateListView(hwnd, g_ui.hInst, IDC_LIST_SERVERS);
    g_ui.hPlayerList = ListViews_CreateListView(hwnd, g_ui.hInst, IDC_LIST_PLAYERS);
    g_ui.hRulesList = ListViews_CreateListView(hwnd, g_ui.hInst, IDC_LIST_RULES);

#if LAMESPY_USE_NEW_FONTS
    // Apply fonts to controls
    if (UI_GetTreeFont())
        SendMessageW(g_ui.hTreeMenu, WM_SETFONT, (WPARAM)UI_GetTreeFont(), TRUE);

    if (UI_GetListFont())
    {
        SendMessageW(g_ui.hServerList, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);
        SendMessageW(g_ui.hPlayerList, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);
        SendMessageW(g_ui.hRulesList, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);

        HWND hHdr;

        hHdr = ListView_GetHeader(g_ui.hServerList);
        if (hHdr)
            SendMessageW(hHdr, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);

        hHdr = ListView_GetHeader(g_ui.hPlayerList);
        if (hHdr)
            SendMessageW(hHdr, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);

        hHdr = ListView_GetHeader(g_ui.hRulesList);
        if (hHdr)
            SendMessageW(hHdr, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);
    }
#endif

    if (!g_ui.hTreeMenu || !g_ui.hServerList || !g_ui.hPlayerList || !g_ui.hRulesList)
    {
        MessageBoxW(hwnd, L"Failed to create one or more controls.", L"Error", MB_OK);
        return;
    }

    g_navUi.hwndMain = g_ui.hwndMain;
    g_navUi.hTreeMenu = g_ui.hTreeMenu;
    g_navUi.hServerList = g_ui.hServerList;

    HomeView_Init(g_ui.hwndMain, g_ui.hInst, hwnd);

    // Create image list for server list
    ServerListView_Init(g_ui.hServerList, g_ui.hInst);

    ListViews_InitServerColumns(g_ui.hServerList);
    ListViews_InitPlayerColumns(g_ui.hPlayerList);
    ListViews_InitRulesColumns(g_ui.hRulesList);

    g_ui.hFilterSearch = NULL;
    g_ui.hFilterField = NULL;
    g_ui.hFilterShowFull = NULL;
    g_ui.hFilterShowEmpty = NULL;
    g_ui.hFilterShowPassword = NULL;

    g_ui.hToolBar = NULL;
    Toolbar_Create(hwnd);
    CreateTopFilters(hwnd);
    CreateStatusBar(hwnd);
}

// ------------------------------------------------------------
// Main window lifecycle (create/destroy/startup)
// ------------------------------------------------------------

void CreateLameWindow(HWND hWnd)
{
    wchar_t favPath[MAX_PATH];

    InitCommonCtrl();
    g_ui.hwndMain = hWnd;

    Config_Load();

    // Set default filters
    Filters_ResetDefaults();

    BuildMainWindow(hWnd);

    Splitter_Init(217);

    // Register all game modules
    QW_RegisterGame();
    Q2_RegisterGame();
    Q3_RegisterGame();
    UT99_RegisterGame();
    UG_RegisterGame();
    DX_RegisterGame();
    UE_RegisterGame();

    ServerListTrickle_Init(g_ui.hwndMain, g_ui.hServerList);
    Query_SetFinishedCallback(ServerListTrickle_OnServerQueryFinished);

    Masters_InitData();

    Path_BuildFavoritesCfg(favPath, _countof(favPath));
    Favorites_EnsureFileExists(favPath);
    Favorites_LoadFile(favPath);

    Master_BuildCombinedForGame(g_session.activeGame);
    g_session.activeMaster = Data_GetMasterCombined();

    UI_RefreshCurrentView();

    UI_BuildTree();
    Tree_ExpandAllGameNodesIfEnabled(g_ui.hTreeMenu);

    if (_stricmp(g_config.startupItem, "HOME") == 0)
    {
        g_session.activeGame = GAME_NONE;
        g_session.activeMaster = NULL;
        g_session.selectedServer = NULL;

        ServerListView_Rebuild(g_ui.hServerList, g_ui.hTreeMenu, g_session.activeMaster);
        UI_RebuildPlayersRulesFromServer(NULL);

        HomeView_SetVisible(TRUE);
    }
    else if (_stricmp(g_config.startupItem, "FAVORITES") == 0)
    {
        g_session.activeGame = GAME_NONE;
        g_session.selectedServer = NULL;

        HomeView_SetVisible(FALSE);
        UI_RebuildPlayersRulesFromServer(NULL);
    }
    else
    {
        HomeView_SetVisible(FALSE);

        if (g_session.activeMaster && g_session.activeMaster->count > 0)
            UI_RebuildPlayersRulesFromServer(g_session.activeMaster->servers[0]);
        else
            UI_RebuildPlayersRulesFromServer(NULL);
    }

    RECT rc;
    GetClientRect(hWnd, &rc);

    SendMessageW(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));

    InvalidateRect(hWnd, NULL, TRUE);
    UpdateWindow(hWnd);

    UI_ShowTopFilters(hWnd, Filters_AreVisible());

    UI_InitNetNotify(hWnd);
}

void KillLameWindow()
{
    HomeView_Shutdown();

#if LAMESPY_USE_NEW_FONTS
    if (UI_GetListFont())
    {
        DeleteObject(UI_GetListFont());
        UI_SetListFont(NULL);
    }

    if (UI_GetTreeFont())
    {
        DeleteObject(UI_GetTreeFont());
        UI_SetTreeFont(NULL);
    }
#endif

    ServerListTrickle_Shutdown();
    ServerListView_Shutdown();

    //if (g_hToolBarImages)
    //{
    //    ImageList_Destroy(g_hToolBarImages); g_hToolBarImages = NULL;
    //}
}

void FetchOnStartup()
{
    if (!g_ui.hTreeMenu || !IsWindow(g_ui.hTreeMenu))
        return;

    if (_stricmp(g_config.startupItem, "HOME") == 0 ||
        _stricmp(g_config.startupItem, "FAVORITES") == 0)
    {
        PostMessageW(g_ui.hwndMain, WM_APP_SHOW_HOME_STARTUP, 0, 0);
        return;
    }

    if (1 /*g_config.autoRefreshStartup*/)
    {
        HTREEITEM hSel = TreeView_GetSelection(g_ui.hTreeMenu);
        if (!hSel)
            return;

        TVITEMW item = {};
        item.mask = TVIF_PARAM;
        item.hItem = hSel;

        if (!TreeView_GetItem(g_ui.hTreeMenu, &item))
            return;

        GameId game = TreeTag_Game(item.lParam);
        TreeNodeKind kind = TreeTag_Kind(item.lParam);

        if (kind == TREE_NODE_GAME || kind == TREE_NODE_INTERNET)
        {
            int masterCount = Data_GetMasterCountForGame(game);
            if (masterCount > 0)
            {
                StatusReset();
                StatusTextFmtPriority(STATUS_OPERATION,
                    L"Fetching server lists for %s...",
                    Game_GetDescriptor(game)->name);
                StatusProgress_Begin();

                g_session.masterFetchGame = game;
                g_session.masterFetchRemaining = masterCount;
                g_session.queryBatchCanceled = FALSE;

                for (int mi = 0; mi < masterCount; mi++)
                    Query_StartMaster(game, mi);
            }
        }
    }
}

// ------------------------------------------------------------
// Game launching
// ------------------------------------------------------------

static GameId g_gameConfigInitialGame = GAME_NONE;

static const wchar_t* UI_GetGameExePath(GameId game)
{
    return Config_GetExePath(game);
}

static void UI_OpenGameConfigForGame(HWND hOwner, GameId game)
{
    g_gameConfigInitialGame = game;
    DialogBox(GetModuleHandleW(NULL),
        MAKEINTRESOURCE(IDD_GAMECONFIG),
        hOwner,
        GameConfigDlg);
    g_gameConfigInitialGame = GAME_NONE;
}

static BOOL UI_EnsureGameConfigured(HWND hOwner, GameId game)
{
    const wchar_t* exePath;
    DWORD attr;

    exePath = Config_GetExePath(game);
    if (exePath && exePath[0])
    {
        attr = GetFileAttributesW(exePath);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            return TRUE;
    }

    MessageBoxW(hOwner,
        L"This game is not set up yet.\n\nPlease choose the game executable first.",
        L"Game Not Configured",
        MB_OK | MB_ICONWARNING);

    // Switch the UI to the game's node so the user can open its config page.
    if (g_ui.hTreeMenu && IsWindow(g_ui.hTreeMenu))
    {
        HTREEITEM hGame = Tree_FindGameNode(g_ui.hTreeMenu, game);
        if (hGame)
        {
            g_session.suppressTreeSelChanged = TRUE;
            TreeView_SelectItem(g_ui.hTreeMenu, hGame);
            g_session.suppressTreeSelChanged = FALSE;

            Nav_OnTreeSelectionChangedS(&g_navCtx, g_ui.hwndMain, &g_session);
        }
    }

    exePath = Config_GetExePath(game);
    if (!exePath || !exePath[0])
        return FALSE;

    attr = GetFileAttributesW(exePath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;

    return TRUE;
}

static void UI_ConnectToServer(GameId game, LameServer* s)
{
    if (!s)
        return;

    // Quake 3: warn on password-protected servers (rule: g_needpass == 1)
    if (game == GAME_Q3)
    {
        const wchar_t* needpass = Server_FindRuleValue(s, L"g_needpass");
        if (needpass && _wtoi(needpass) == 1)
        {
            int r = MessageBoxW(
                g_ui.hwndMain,
                L"This server is password protected. Are you sure you want to join?",
                L"Password Protected Server",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

            if (r != IDYES)
            {
                StatusTextPriority(STATUS_INFO, L"Join canceled.");
                return;
            }
        }
    }

    if (!UI_EnsureGameConfigured(g_ui.hwndMain, game))
    {
        StatusText(L"Game launch canceled.");
        return;
    }

    const wchar_t* exePath = UI_GetGameExePath(game);
    if (!exePath)
    {
        StatusText(L"No EXE path set for this game.");
        return;
    }

    DWORD attr = GetFileAttributesW(exePath);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        StatusText(L"EXE path is set but the file does not exist.");
        return;
    }

    wchar_t args[512];
    wchar_t workingDir[MAX_PATH];

    // Extract working directory from exe path
    wcsncpy_s(workingDir, _countof(workingDir), exePath, _TRUNCATE);
    wchar_t* lastSlash = wcsrchr(workingDir, L'\\');
    if (lastSlash)
        *lastSlash = L'\0';

    if (game == GAME_Q3 || game == GAME_QW || game == GAME_Q2)
        swprintf_s(args, _countof(args), L"+connect %s:%d", s->ip, s->port);
    else if (game == GAME_UT99 || game == GAME_UG || game == GAME_UE)
        swprintf_s(args, _countof(args), L"unreal://%s:%d", s->ip, s->port);
    else if (game == GAME_DX)
        swprintf_s(args, _countof(args), L"deusex://%s:%d", s->ip, s->port);
    else
        args[0] = 0;

    if (!Game_LaunchProcess(game, exePath, args, workingDir))
        StatusText(L"Failed to launch game process.");
    else
    {
        StatusTextFmt(L"Launching: %s (%s:%d)", exePath, s->ip, s->port);

        if (g_config.soundFlags & LSOUND_LAUNCH)
            PlayLameSound(IDR_SOUND_LAUNCH, SOUND_LAUNCH_ONLY);
    }

    const char* tag = "";
    switch (game) {
    case GAME_QW:   tag = "QW"; break;
    case GAME_Q2:   tag = "Q2"; break;
    case GAME_Q3:   tag = "Q3"; break;
    case GAME_UT99: tag = "UT99"; break;
    case GAME_UG:   tag = "UG"; break;
    case GAME_DX:   tag = "DX"; break;
    case GAME_UE:   tag = "UE"; break;
    default:        tag = "???"; break;
    }

    SendSessionEvent("join", g_config.playerName, s->ip, s->port, tag);
}

static HRESULT UI_GetDesktopDir(wchar_t* outDir, int outCap)
{
    if (!outDir || outCap <= 0)
        return E_INVALIDARG;

    outDir[0] = 0;
    return SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, outDir);
}

static HRESULT UI_CreateShortcutOnDesktop(const wchar_t* linkName, const wchar_t* targetExe, const wchar_t* args)
{
    if (!linkName || !linkName[0] || !targetExe || !targetExe[0])
        return E_INVALIDARG;

    wchar_t desktopDir[MAX_PATH] = {};
    HRESULT hr = UI_GetDesktopDir(desktopDir, _countof(desktopDir));
    if (FAILED(hr))
        return hr;

    wchar_t linkPath[MAX_PATH] = {};
    _snwprintf_s(linkPath, _countof(linkPath), _TRUNCATE, L"%s\\%s.lnk", desktopDir, linkName);

    wil::com_ptr<IShellLinkW> link;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr) || !link)
        return FAILED(hr) ? hr : E_FAIL;

    hr = link->SetPath(targetExe);
    if (FAILED(hr))
        return hr;

    if (args && args[0])
    {
        hr = link->SetArguments(args);
        if (FAILED(hr))
            return hr;
    }

    wchar_t workingDir[MAX_PATH] = {};
    wcsncpy_s(workingDir, _countof(workingDir), targetExe, _TRUNCATE);
    wchar_t* lastSlash = wcsrchr(workingDir, L'\\');
    if (lastSlash)
    {
        *lastSlash = 0;
        link->SetWorkingDirectory(workingDir);
    }

    link->SetIconLocation(targetExe, 0);

    wil::com_ptr<IPersistFile> pf;
    hr = link->QueryInterface(IID_PPV_ARGS(&pf));
    if (FAILED(hr) || !pf)
        return FAILED(hr) ? hr : E_FAIL;

    return pf->Save(linkPath, TRUE);
}

static BOOL UI_BuildConnectArgs(GameId game, const LameServer* s, wchar_t* outArgs, int outCap)
{
    if (!outArgs || outCap <= 0 || !s)
        return FALSE;

    outArgs[0] = 0;

    if (game == GAME_Q3 || game == GAME_QW || game == GAME_Q2)
        _snwprintf_s(outArgs, outCap, _TRUNCATE, L"+connect %s:%d", s->ip, s->port);
    else if (game == GAME_UT99 || game == GAME_UG || game == GAME_UE)
        _snwprintf_s(outArgs, outCap, _TRUNCATE, L"unreal://%s:%d", s->ip, s->port);
    else if (game == GAME_DX)
        _snwprintf_s(outArgs, outCap, _TRUNCATE, L"deusex://%s:%d", s->ip, s->port);

    return outArgs[0] ? TRUE : FALSE;
}

static void TrimWhitespace(wchar_t* s)
{
    if (!s) return;

    // Trim leading
    wchar_t* start = s;
    while (*start && iswspace(*start)) start++;

    // If all spaces, just set to empty
    if (start != s)
        memmove(s, start, (wcslen(start) + 1) * sizeof(wchar_t));

    // Trim trailing
    size_t len = wcslen(s);
    while (len > 0 && iswspace(s[len - 1]))
        s[--len] = 0;
}

static void UI_CreateDesktopShortcutForServer(GameId game, const LameServer* s)
{
    if (!s)
        return;

    if (!UI_EnsureGameConfigured(g_ui.hwndMain, game))
    {
        StatusTextPriority(STATUS_INFO, L"Configure the game first.");
        return;
    }

    const wchar_t* exePath = Config_GetExePath(game);
    if (!exePath || !exePath[0])
    {
        StatusTextPriority(STATUS_INFO, L"No EXE path set for this game.");
        return;
    }

    wchar_t args[512] = {};
    if (!UI_BuildConnectArgs(game, s, args, _countof(args)))
    {
        StatusTextPriority(STATUS_INFO, L"Shortcut not supported for this game.");
        return;
    }

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE)
    {
        StatusTextPriority(STATUS_IMPORTANT, L"Shortcut creation failed (COM init).");
        return;
    }

    const wchar_t* gameName = Game_GetDescriptor(game)->name;
    wchar_t linkName[256] = {};
    wchar_t cleanName[128] = {};

    // Prefer hostname if available, else use IP:Port
    if (s->name && s->name[0])
    {
        wcsncpy_s(cleanName, _countof(cleanName), s->name, _TRUNCATE);
        TrimWhitespace(cleanName);
        if (cleanName[0])
            _snwprintf_s(linkName, _countof(linkName), _TRUNCATE, L"Play %s on %s", gameName ? gameName : L"Game", cleanName);
        else
            _snwprintf_s(linkName, _countof(linkName), _TRUNCATE, L"Play %s on %s:%d", gameName ? gameName : L"Game", s->ip, s->port);
    }
    else
    {
        _snwprintf_s(linkName, _countof(linkName), _TRUNCATE, L"Play %s on %s:%d", gameName ? gameName : L"Game", s->ip, s->port);
    }

    HRESULT hr = UI_CreateShortcutOnDesktop(linkName, exePath, args);

    if (SUCCEEDED(hr))
        StatusTextPriority(STATUS_IMPORTANT, L"Desktop shortcut created.");
    else
        StatusTextPriority(STATUS_IMPORTANT, L"Failed to create desktop shortcut.");
}


// ------------------------------------------------------------
// Menu/command handling
// ------------------------------------------------------------

void LameMenus(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    int wmId = LOWORD(wParam);

    auto favoritesContains = [](GameId game, const wchar_t* ip, int port) -> BOOL
        {
            if (game <= GAME_NONE || game >= GAME_MAX || !ip || !ip[0] || port <= 0)
                return FALSE;

            LameMaster* fm = Data_GetMasterFavorites(game);
            if (!fm)
                return FALSE;

            for (int i = 0; i < fm->count; i++)
            {
                if (Server_MatchIPPort(fm->servers[i], ip, port))
                    return TRUE;
            }

            return FALSE;
        };

    auto removeFavoriteIfPresent = [&](const wchar_t* ip, int port) -> BOOL
        {
            if (!ip || !ip[0] || port <= 0)
                return FALSE;

            for (int gi = 0; gi < GAME_MAX; gi++)
            {
                if (gi == GAME_NONE)
                    continue;

                LameMaster* fm = Data_GetMasterFavorites((GameId)gi);
                if (!fm || fm->count <= 0)
                    continue;

                for (int i = 0; i < fm->count; i++)
                {
                    if (!Server_MatchIPPort(fm->servers[i], ip, port))
                        continue;

                    if (i < fm->count - 1)
                    {
                        memmove(
                            fm->servers + i,
                            fm->servers + i + 1,
                            sizeof(LameServer*) * (size_t)(fm->count - i - 1));
                    }

                    fm->count--;
                    fm->servers[fm->count] = NULL;
                    return TRUE;
                }
            }

            return FALSE;
        };

    switch (wmId)
    {
    case ID_LAMESPY_SETTINGS:
        DialogBox(g_ui.hInst, MAKEINTRESOURCEW(IDD_SETTINGS), hWnd, SettingsDlg);
        break;

    case ID_LAMESPY_GAMESETUP:
        DialogBox(g_ui.hInst, MAKEINTRESOURCEW(IDD_GAMETOGGLE), g_ui.hwndMain, GameToggleDlg);
        break;

    case ID_LAMESPY_PROFILE:
        DialogBox(g_ui.hInst, MAKEINTRESOURCEW(IDD_PROFILE), hWnd, ProfileDlg);
        break;

    case ID_LAMESPY_ABOUT:
        DialogBox(g_ui.hInst, MAKEINTRESOURCEW(IDD_ABOUT), hWnd, AboutDlg);
        break;

    case ID_HELP_DISCORD:
    {
        HINSTANCE rc = ShellExecuteW(
            hWnd,
            L"open",
            L"https://discord.gg/HthtxF43kC",
            NULL,
            NULL,
            SW_SHOWNORMAL);

        if ((INT_PTR)rc <= 32)
            StatusTextPriority(STATUS_INFO, L"Failed to open Discord in default browser.");

        break;
    }

    case ID_HELP_TUTORIALS:
    {
        // Find the Tutorials node in the tree
        HTREEITEM hTutorials = Tree_FindTutorialsNode(g_ui.hTreeMenu);
        if (hTutorials)
        {
            g_session.suppressTreeSelChanged = TRUE;
            TreeView_SelectItem(g_ui.hTreeMenu, hTutorials);
            g_session.suppressTreeSelChanged = FALSE;

            // Trigger the selection changed logic to update the view
            Nav_OnTreeSelectionChangedS(&g_navCtx, hWnd, &g_session);
        }
        else
        {
            StatusTextPriority(STATUS_INFO, L"Tutorials node not found.");
        }
        break;
    }

    case ID_LAMESPY_CHAT:
        //LameChat_OpenBuddyList(hWnd);
        break;

    case ID_SERVERS_FILTERS:
        UI_ShowTopFilters(hWnd, !Filters_AreVisible());
        break;

    case IDC_FILTER_SEARCH:
        if (HIWORD(wParam) == EN_CHANGE)
        {
            UI_SaveFiltersFromTopControls();
            UI_RefreshCurrentView();
        }
        break;

    case IDC_FILTER_FIELD:
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
            UI_SaveFiltersFromTopControls();
            UI_RefreshCurrentView();
        }
        break;

    case IDC_FILTER_SHOW_EMPTY:
    case IDC_FILTER_SHOW_FULL:
    case IDC_FILTER_SHOW_PASSWORD:
        if (HIWORD(wParam) == BN_CLICKED)
        {
            UI_SaveFiltersFromTopControls();
            UI_RefreshCurrentView();
        }
        break;

    case ID_SERVERS_CANCELQUERIES:
        UI_CancelAllQueries(hWnd);
        break;

        case ID_SERVER_CREATE_SHORTCUT:
        if (g_session.selectedServer)
            UI_CreateDesktopShortcutForServer(g_session.selectedServer->game, g_session.selectedServer);
        else
            StatusTextPriority(STATUS_INFO, L"No server selected.");
        break;

    case ID_FAVORITES_ADD:
    {
        wchar_t favPath[MAX_PATH];
        const BOOL isFavoritesView = ServerListView_IsFavoritesView(g_ui.hTreeMenu);

        // Remember current tree selection; dialog/UI focus can disturb tree state.
        HTREEITEM hTreeSel = TreeView_GetSelection(g_ui.hTreeMenu);

        // When viewing Favorites, Add should always open the manual dialog regardless of selection.
        if (isFavoritesView)
        {
            DialogBox(g_ui.hInst, MAKEINTRESOURCEW(IDD_ADDFAVORITE), hWnd, AddFavoriteDlg);

            // Restore tree selection and rebuild view deterministically.
            if (hTreeSel)
            {
                g_session.suppressTreeSelChanged = TRUE;
                TreeView_SelectItem(g_ui.hTreeMenu, hTreeSel);
                g_session.suppressTreeSelChanged = FALSE;
            }

            Nav_OnTreeSelectionChangedS(&g_navCtx, hWnd, &g_session);
            break;
        }

        if (!g_session.selectedServer)
        {
            DialogBox(g_ui.hInst, MAKEINTRESOURCEW(IDD_ADDFAVORITE), hWnd, AddFavoriteDlg);

            if (hTreeSel)
            {
                g_session.suppressTreeSelChanged = TRUE;
                TreeView_SelectItem(g_ui.hTreeMenu, hTreeSel);
                g_session.suppressTreeSelChanged = FALSE;
            }

            Nav_OnTreeSelectionChangedS(&g_navCtx, hWnd, &g_session);
            break;
        }

        if (g_session.selectedServer->isFavorite)
        {
            StatusTextPriority(STATUS_INFO, L"That server is already a favorite.");
            break;
        }

        const GameId game = g_session.selectedServer->game;
        const wchar_t* ip = g_session.selectedServer->ip;
        const int port = g_session.selectedServer->port;

        if (favoritesContains(game, ip, port))
        {
            StatusTextPriority(STATUS_INFO, L"That server is already a favorite.");
            break;
        }

        Favorites_AddInternal(game, ip, port);

        // Mark the currently selected server row as a favorite (so the context menu updates).
        g_session.selectedServer->isFavorite = 1;

        Path_BuildFavoritesCfg(favPath, _countof(favPath));
        Favorites_SaveFile(favPath);

        StatusTextFmtPriority(STATUS_IMPORTANT, L"Added favorite to %s.", Game_PrefixW(game));

        // Restore tree selection and rebuild view deterministically (prevents Home/Favorites "losing" games).
        if (hTreeSel)
        {
            g_session.suppressTreeSelChanged = TRUE;
            TreeView_SelectItem(g_ui.hTreeMenu, hTreeSel);
            g_session.suppressTreeSelChanged = FALSE;
        }

        Nav_OnTreeSelectionChangedS(&g_navCtx, hWnd, &g_session);

        // Ensures it appears even if "hide dead favorites" is enabled.
        Query_StartFavorites(game);
        break;
    }

    case ID_FAVORITES_REMOVE:
    {
        wchar_t favPath[MAX_PATH];

        if (!g_session.selectedServer || !g_session.selectedServer->isFavorite)
        {
            StatusTextPriority(STATUS_INFO, L"Select a favorite server to remove.");
            break;
        }

        const wchar_t* ip = g_session.selectedServer->ip;
        const int port = g_session.selectedServer->port;

        if (!removeFavoriteIfPresent(ip, port))
        {
            StatusTextPriority(STATUS_INFO, L"Selected server is not in favorites.");
            break;
        }

        g_session.selectedServer = NULL;
        UI_RebuildPlayersRulesFromServer(NULL);

        Path_BuildFavoritesCfg(favPath, _countof(favPath));
        Favorites_SaveFile(favPath);

        StatusTextPriority(STATUS_IMPORTANT, L"Removed favorite.");
        // Refresh based on the *current tree selection* so Favorites/Home Favorites rebuild correctly.
        Nav_OnTreeSelectionChangedS(&g_navCtx, hWnd, &g_session);
        break;
    }

    case ID_SERVER_FETCHNEWLIST:
        Nav_OnCommandFetchNewListS(&g_navCtx, hWnd, &g_session);
        break;

    case ID_SERVER_DUMPMASTER:
        UI_DumpActiveMaster();
        break;

    case ID_SERVER_REFRESH:
        Nav_OnCommandRefreshListS(&g_navCtx, hWnd, &g_session);
        break;

    case ID_SERVER_REFRESHSERVER:
    case ID_SERVER_REFRESH_SINGLE:
    {
        if (g_session.selectedServer)
        {
            const LameGameDescriptor* desc = Game_GetDescriptor(g_session.activeGame);
            if (desc && desc->QueryGameServer)
            {
                // Reset state before query
                g_session.selectedServer->state = QUERY_IN_PROGRESS;

                int success = desc->QueryGameServer(
                    g_session.selectedServer->ip,
                    g_session.selectedServer->port,
                    g_session.selectedServer
                );

                if (success)
                {
                    g_session.selectedServer->state = QUERY_DONE;
                    ServerListView_UpdateSelectedRow(g_ui.hServerList, g_session.selectedServer);
                    UI_RebuildPlayersRulesFromServer(g_session.selectedServer);
                    StatusTextPriority(STATUS_IMPORTANT, L"Server refreshed successfully.");
                }
                else
                {
                    g_session.selectedServer->state = QUERY_FAILED;
                    StatusTextPriority(STATUS_IMPORTANT, L"Query failed (timeout or no response)");
                }
            }
            else
            {
                StatusTextPriority(STATUS_INFO, L"No query function registered for this game");
            }
        }
        else
        {
            StatusTextPriority(STATUS_INFO, L"No server selected.");
        }
    }
    break;

    case ID_SERVER_CONNECT:
        if (g_session.selectedServer)
            UI_ConnectToServer(g_session.selectedServer->game, g_session.selectedServer);
        else
            StatusTextPriority(STATUS_INFO, L"No server selected.");
        break;

    case ID_SERVER_COPYIP:
    {
        if (g_session.selectedServer)
        {
            wchar_t ipPort[128];
            swprintf_s(ipPort, _countof(ipPort), L"%s:%d",
                g_session.selectedServer->ip, g_session.selectedServer->port);

            if (OpenClipboard(hWnd))
            {
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE,
                    (wcslen(ipPort) + 1) * sizeof(wchar_t));
                if (hMem)
                {
                    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                    if (pMem)
                    {
                        wcscpy_s(pMem, wcslen(ipPort) + 1, ipPort);
                        GlobalUnlock(hMem);
                        EmptyClipboard();
                        SetClipboardData(CF_UNICODETEXT, hMem);
                        StatusTextFmt(L"Copied: %s", ipPort);
                    }
                    else
                    {
                        GlobalFree(hMem);
                    }
                }
                CloseClipboard();
            }
        }
    }
    break;

    case ID_SERVER_SERVERINFO:
    case ID_SERVER_INFO:
    {
        if (g_session.selectedServer)
        {
            if (g_session.selectedServer->ping == 999)
                break;

            wchar_t info[512];
            swprintf_s(info, _countof(info),
                L"=== Server Identity ===\n"
                L"Name:       %s\n"
                L"IP:           %s:%d\n"
                L"Game:      %s\n\n"
                L"=== Current Match ===\n"
                L"Map:            %s\n"
                L"Gametype:  %s\n"
                L"Players:     %d / %d\n\n"
                L"=== Network ===\n"
                L"Ping:         %d ms\n"
                L"Rules:       %d available",
                g_session.selectedServer->name,
                g_session.selectedServer->ip,
                g_session.selectedServer->port,
                (g_session.selectedServer->label[0]) ? g_session.selectedServer->label : Game_PrefixW(g_session.selectedServer->game),
                g_session.selectedServer->map,
                g_session.selectedServer->gametype,
                g_session.selectedServer->players,
                g_session.selectedServer->maxPlayers,
                g_session.selectedServer->ping,
                g_session.selectedServer->ruleCount);

            MessageBoxW(hWnd, info, L"Server Information", MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            StatusTextPriority(STATUS_INFO, L"No server selected.");
        }
    }
    break;

    case IDM_EXIT:
        DestroyWindow(hWnd);
        break;
    }
}

// ------------------------------------------------------------
// Sorting / list view column click
// ------------------------------------------------------------

#if LAMESPY_USE_COMCTL6
static void ListView_SetSortArrow(HWND lv, int sortSubItem, int ascending)
{
    HWND hHdr;
    int colCount;
    int i;
    int order[64];
    int headerIndex;

    HDITEMW hi;

    hHdr = ListView_GetHeader(lv);
    if (!hHdr)
        return;

    colCount = Header_GetItemCount(hHdr);
    if (colCount <= 0)
        return;

    if (colCount > (int)_countof(order))
        colCount = (int)_countof(order);

    ZeroMemory(order, sizeof(order));
    if (!ListView_GetColumnOrderArray(lv, colCount, order))
    {
        order[0] = 0;
    }

    ZeroMemory(&hi, sizeof(hi));
    hi.mask = HDI_FORMAT;

    for (i = 0; i < colCount; i++)
    {
        if (!Header_GetItem(hHdr, i, &hi))
            continue;

        //hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);  // DISABLES SORT ARROW; TODO: option for sort arrow
        Header_SetItem(hHdr, i, &hi);
    }

    headerIndex = -1;
    for (i = 0; i < colCount; i++)
    {
        if (order[i] == sortSubItem)
        {
            headerIndex = i;
            break;
        }
    }

    if (headerIndex < 0 || headerIndex >= colCount)
        return;

    if (!Header_GetItem(hHdr, headerIndex, &hi))
        return;

    /*hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);  // DISABLES SORT ARROW; TODO: option for sort arrow
    if (ascending)
        hi.fmt |= HDF_SORTUP;
    else
        hi.fmt |= HDF_SORTDOWN;*/

    Header_SetItem(hHdr, headerIndex, &hi);
}
#endif

static void HandleListColumnClick(NMHDR* hdr, LPARAM lParam)
{
    NMLISTVIEW* nmlv;
    int sortColumn;
    int sortAscending;

    if (hdr->idFrom != IDC_LIST_SERVERS)
        return;

    if (hdr->code != LVN_COLUMNCLICK)
        return;

    nmlv = (NMLISTVIEW*)lParam;

    Data_GetSortState(&sortColumn, &sortAscending);

    if (sortColumn == nmlv->iSubItem)
        sortAscending = !sortAscending;
    else
    {
        sortColumn = nmlv->iSubItem;
        sortAscending = 1;
    }

    Data_SetSortState(sortColumn, sortAscending);

#if LAMESPY_USE_COMCTL6
    ListView_SetSortArrow(g_ui.hServerList, sortColumn, sortAscending);
#endif

    if (!g_session.activeMaster || !g_session.activeMaster->servers || g_session.activeMaster->count <= 1)
        return;

    Data_Lock();

    qsort(g_session.activeMaster->servers,
        g_session.activeMaster->count,
        sizeof(LameServer*),
        LameServerPtrCompare);

    Data_Unlock();

    ServerListView_Rebuild(g_ui.hServerList, g_ui.hTreeMenu, g_session.activeMaster);

    UI_RebuildPlayersRulesFromServer(g_session.selectedServer);
}

// ------------------------------------------------------------
// WM_APP_* messages
// ------------------------------------------------------------

// Posted by background threads when they finish a batch of queries (e.g., master list, favorites list).
static void UI_OnQueryBatchFinished(HWND hWnd)
{
    // Clear "operation" status (e.g., "Master 2/3...") and return to normal.
    StatusReset();
    StatusText(L"Ready.");

    // Ensure the marquee matches actual background activity.
    // (If some code ended it early, this will re-show it while active queries exist.)
    StatusProgress_SyncToBackgroundActivity();

    SetCursor(LoadCursor(NULL, IDC_ARROW));

    // Reset session bookkeeping
    g_session.masterFetchGame = GAME_NONE;
    g_session.masterFetchRemaining = 0;
    g_session.combinedQueryGame = GAME_NONE;
    g_session.combinedQueryGen = 0;
    g_session.queryBatchCanceled = FALSE;

    (void)hWnd;
}

void LameUI_OnAppMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_APP_QUERY_DONE)
    {
        UiMessages_OnAppMessage(&g_navCtx, &g_session, hWnd, msg, wParam, lParam);
        UI_OnQueryBatchFinished(hWnd);
        return;
    }

    if (msg == WM_APP_VERSION_CHECK_RESULT)
    {
        VersionCheckResult r = (VersionCheckResult)wParam;

        if (r == VERSION_CHECK_OK)
        {
            if (g_manualVersionCheckRequested)
            {
                MessageBoxW(
                    hWnd,
                    L"LameSpy is up to date.",
                    L"Version Check",
                    MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                StatusText(L"Welcome to LameSpy!");
            }
        }
        else if (r == VERSION_CHECK_OUTDATED)
        {
            UI_ShowVersionOutdatedDialog(hWnd);
        }
        else if (r == VERSION_CHECK_UNAVAILABLE && g_manualVersionCheckRequested)
        {
            MessageBoxW(
                hWnd,
                L"Update server not avaiable. Try again later.",
                L"Version Check",
                MB_OK | MB_ICONWARNING);
        }

        g_manualVersionCheckRequested = FALSE;
        return;
    }

    UiMessages_OnAppMessage(&g_navCtx, &g_session, hWnd, msg, wParam, lParam);
}

// ------------------------------------------------------------
// WM_NOTIFY handler
// ------------------------------------------------------------

LRESULT NotifyLameWindow(LPARAM lParam)
{
    NMHDR* hdr;

    hdr = (NMHDR*)lParam;
    if (!hdr)
        return 0;

    if (hdr->hwndFrom == g_ui.hServerList &&
        hdr->code == NM_CUSTOMDRAW)
    {
        return ServerListView_OnCustomDraw((LPNMLVCUSTOMDRAW)lParam);
    }

    // Handle toolbar tooltips
    if (Toolbar_OnNotifyTooltip(hdr, lParam))
        return 0;

    // Handle right-click on tree
    if (hdr->idFrom == IDC_TREE_GAMES && hdr->code == NM_RCLICK)
    {
        ShowTreeContextMenu(g_ui.hwndMain, g_ui.hTreeMenu);
    }

    // Handle right-click on server list header (toggle columns on/off)
    if (hdr->code == NM_RCLICK)
    {
        HWND hHdr;
        hHdr = ListView_GetHeader(g_ui.hServerList);
        if (hHdr && hdr->hwndFrom == hHdr)
        {
            ShowListViewColumnMenu(g_ui.hwndMain, g_ui.hServerList);
        }
    }

    // Handle right-click on server list
    if (hdr->idFrom == IDC_LIST_SERVERS && hdr->code == NM_RCLICK)
    {
        ShowServerContextMenu(g_ui.hwndMain, g_ui.hServerList);
    }

    // Double-click on server list = connect
    if (hdr->idFrom == IDC_LIST_SERVERS && hdr->code == NM_DBLCLK)
    {
        if (g_session.selectedServer)
            UI_ConnectToServer(g_session.selectedServer->game, g_session.selectedServer);
    }

    // Click on empty space in server list = clear selection
    if (hdr->idFrom == IDC_LIST_SERVERS && hdr->code == NM_CLICK)
    {
        NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lParam;
        if (ia && ia->iItem < 0)
        {
            ListView_SetItemState(g_ui.hServerList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            g_session.selectedServer = NULL;
            UI_RebuildPlayersRulesFromServer(NULL);
        }
    }

    // Clicked a column header in Players list
    if (hdr->idFrom == IDC_LIST_PLAYERS && hdr->code == LVN_COLUMNCLICK)
    {
        NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;
        int playerSortColumn;
        int playerSortAscending;

        if (g_session.selectedServer)
        {
            Data_GetPlayerSortState(&playerSortColumn, &playerSortAscending);

            if (playerSortColumn == nmlv->iSubItem)
                playerSortAscending = !playerSortAscending;
            else
            {
                playerSortColumn = nmlv->iSubItem;
                playerSortAscending = 1;
            }

            Data_SetPlayerSortState(playerSortColumn, playerSortAscending);

            if (g_session.selectedServer->playerCount > 1)
            {
                qsort(g_session.selectedServer->playerList,
                    g_session.selectedServer->playerCount,
                    sizeof(LamePlayer),
                    LamePlayerCompare);
            }

            UI_RebuildPlayersRulesFromServer(g_session.selectedServer);
        }
    }

    // Clicked a column header in Rules list
    if (hdr->idFrom == IDC_LIST_RULES && hdr->code == LVN_COLUMNCLICK)
    {
        NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;
        int ruleSortColumn;
        int ruleSortAscending;

        if (g_session.selectedServer)
        {
            Data_GetRuleSortState(&ruleSortColumn, &ruleSortAscending);

            if (ruleSortColumn == nmlv->iSubItem)
                ruleSortAscending = !ruleSortAscending;
            else
            {
                ruleSortColumn = nmlv->iSubItem;
                ruleSortAscending = 1;
            }

            Data_SetRuleSortState(ruleSortColumn, ruleSortAscending);

            if (g_session.selectedServer->ruleCount > 1)
            {
                qsort(g_session.selectedServer->ruleList,
                    g_session.selectedServer->ruleCount,
                    sizeof(LameRule),
                    LameRuleCompare);
            }

            UI_RebuildPlayersRulesFromServer(g_session.selectedServer);
        }
    }

    // Clicked on server list
    if (hdr->idFrom == IDC_LIST_SERVERS && hdr->code == LVN_ITEMCHANGED)
    {
        NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;

        if (nmlv->uChanged & LVIF_STATE)
        {
            int wasSel = ((nmlv->uOldState & LVIS_SELECTED) != 0);
            int isSel = ((nmlv->uNewState & LVIS_SELECTED) != 0);

            if (isSel)
            {
                LVITEMW it;
                ZeroMemory(&it, sizeof(it));
                it.mask = LVIF_PARAM;
                it.iItem = nmlv->iItem;
                it.iSubItem = 0;

                LameServer* s = NULL;
                if (ListView_GetItem(g_ui.hServerList, &it))
                    s = (LameServer*)it.lParam;

                if (s)
                {
                    g_session.selectedServer = s;
                    UI_RebuildPlayersRulesFromServer(s);
                }
                else
                {
                    g_session.selectedServer = NULL;
                    UI_RebuildPlayersRulesFromServer(NULL);
                }
            }
            else if (wasSel && !isSel)
            {
                int anySel = ListView_GetNextItem(g_ui.hServerList, -1, LVNI_SELECTED);
                if (anySel < 0)
                {
                    g_session.selectedServer = NULL;
                    UI_RebuildPlayersRulesFromServer(NULL);
                }
            }
        }
    }

    // Tree click on already-selected item (TVN_SELCHANGEDW won't fire in this case)
    if (hdr->idFrom == IDC_TREE_GAMES && hdr->code == NM_CLICK)
    {
        TVHITTESTINFO ht = { 0 };
        HTREEITEM hCurrent;
        HTREEITEM hHit;

        GetCursorPos(&ht.pt);
        ScreenToClient(g_ui.hTreeMenu, &ht.pt);

        hCurrent = TreeView_GetSelection(g_ui.hTreeMenu);
        hHit = TreeView_HitTest(g_ui.hTreeMenu, &ht);

        // If the user clicked the +/- button, let the TreeView handle expand/collapse.
        if ((ht.flags & TVHT_ONITEMBUTTON) != 0)
            return 0;

        // Only handle re-click on the current selection. New selections are handled by TVN_SELCHANGEDW.
        if (hHit && hHit == hCurrent && !g_session.suppressTreeSelChanged)
        {
            Nav_OnTreeReclickedS(&g_navCtx, g_ui.hwndMain, &g_session);

            return 0;
        }
    }

    // Tree selection change
    if (hdr->idFrom == IDC_TREE_GAMES && hdr->code == TVN_SELCHANGEDW)
    {
        if (!g_session.suppressTreeSelChanged)
        {
            Nav_OnTreeSelectionChangedS(&g_navCtx, g_ui.hwndMain, &g_session);
        }
    }

#if LAMESPY_USE_COMCTL6
    if (hdr->idFrom == IDC_LIST_SERVERS && hdr->code == HDN_ENDDRAG)
    {
        int sortColumn;
        int sortAscending;

        Data_GetSortState(&sortColumn, &sortAscending);
        ListView_SetSortArrow(g_ui.hServerList, sortColumn, sortAscending);
    }
#endif

    HandleListColumnClick(hdr, lParam);

    return 0;
}

// ------------------------------------------------------------
// WM_SIZE / layout
// ------------------------------------------------------------

void ResizeLameWindow(HWND hWnd, LPARAM lParam)
{
    int wW;
    int wH;

    RECT rcSB;
    int sbH;
    int clientH;

    int top;
    int bottom;
    int pad;

    RECT rcTB;
    int tbH;

    (void)hWnd;

    wW = LOWORD(lParam);
    wH = HIWORD(lParam);

    sbH = 0;
    if (g_ui.hStatusBar)
    {
        SendMessageW(g_ui.hStatusBar, WM_SIZE, 0, 0);
        GetWindowRect(g_ui.hStatusBar, &rcSB);
        sbH = rcSB.bottom - rcSB.top;
    }

    StatusBar_LayoutPartsAndProgress();

    tbH = 0;
    if (g_ui.hToolBar)
    {
        SendMessageW(g_ui.hToolBar, TB_AUTOSIZE, 0, 0);
        GetWindowRect(g_ui.hToolBar, &rcTB);
        tbH = rcTB.bottom - rcTB.top - 13;  // Added -13 for easy Y axis adjustment
    }

    clientH = wH - sbH;
    if (clientH < 0)
        clientH = 0;

    top = 4 + tbH;
    bottom = clientH / 3;
    pad = 10;

    int treeWidth = Splitter_GetTreeWidth();
    const int barW = Splitter_GetBarWidth();
    const int rightMargin = Splitter_GetRightMinMargin();

    if (treeWidth < Splitter_GetMinLeftWidth())
        treeWidth = Splitter_GetMinLeftWidth();

    if (treeWidth > wW - rightMargin - barW - 3)
        treeWidth = wW - rightMargin - barW - 3;

    int treeX = 6;
    int splitterX = treeX + treeWidth;
    int rightX = splitterX + barW;
    int rightW = wW - rightX - 10;

    if (rightW < 0)
        rightW = 0;

    MoveWindow(g_ui.hTreeMenu,
        treeX, top + pad,
        treeWidth,
        clientH - (top + 20),
        TRUE);

    MoveWindow(g_ui.hServerList,
        rightX - 4, top + pad,
        rightW,
        clientH - bottom - (top + 12),
        TRUE);

    MoveWindow(g_ui.hPlayerList,
        rightX - 4, clientH - bottom,
        rightW / 2 - 10,
        bottom - 10,
        TRUE);

    MoveWindow(g_ui.hRulesList,
        rightX + (rightW / 2) - 11,
        clientH - bottom,
        rightW / 2 + 8,
        bottom - 10,
        TRUE);

    HomeView_MoveHost(
        rightX, top + pad,
        rightW,
        clientH - (top + 20));

    if (g_ui.hStatusBar)
        SendMessageW(g_ui.hStatusBar, WM_SIZE, 0, 0);

    LayoutTopFilters(hWnd, wW);
    Filters_OnMainWindowResized(hWnd);

    if (g_ui.hStatusBar)
        SendMessageW(g_ui.hStatusBar, WM_SIZE, 0, 0);
}

// Resizing column between tree and list (delegates to Splitter_*)
BOOL LameUI_OnSetCursor(HWND hWnd)
{
    return Splitter_OnSetCursor(hWnd);
}

BOOL LameUI_OnLButtonDown(HWND hWnd, LPARAM lParam)
{
    return Splitter_OnLButtonDown(hWnd, lParam);
}

void LameUI_OnMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    Splitter_OnMouseMove(hWnd, wParam, lParam);
}

void LameUI_OnLButtonUp(HWND hWnd)
{
    Splitter_OnLButtonUp(hWnd);
}

BOOL LameUI_IsSplitterDragging(void)
{
    return Splitter_IsDragging();
}

void LameUI_OnGetMinMaxInfo(HWND hWnd, MINMAXINFO* mmi)
{
    if (!mmi)
        return;

    // Must be >=: left pane lock + splitter + right side min + margins used in ResizeLameWindow.
    const int minClientW = 6 + Splitter_GetMinLeftWidth() + Splitter_GetBarWidth() + Splitter_GetRightMinMargin() + 100;  // was 10

    const int minClientH = 360;

    RECT rc = { 0, 0, minClientW, minClientH };
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    mmi->ptMinTrackSize.x = rc.right - rc.left;
    mmi->ptMinTrackSize.y = rc.bottom - rc.top;

    (void)hWnd;
}

// ------------------------------------------------------------
// Timers / debug helpers
// ------------------------------------------------------------

void UI_DumpActiveMaster(void)
{
    if (!g_session.activeMaster)
    {
        StatusTextFmtPriority(STATUS_INFO, L"No active master to dump");
        return;
    }

    wchar_t dir[MAX_PATH] = {};
    Path_GetExeDir(dir, _countof(dir));

    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t path[MAX_PATH] = {};
    swprintf_s(path, _countof(path),
        L"%s\\master_dump_%04d%02d%02d_%02d%02d%02d.txt",
        dir,
        (int)st.wYear, (int)st.wMonth, (int)st.wDay,
        (int)st.wHour, (int)st.wMinute, (int)st.wSecond);

    Data_DumpMasterToFile(path, g_session.activeMaster);

    StatusTextFmtPriority(STATUS_IMPORTANT, L"Dumped master to %s", path);
}

void LameUI_OnTimer(HWND hWnd, WPARAM wParam)
{
    if (wParam == IDT_QUERY_FLUSH)
    {
        ServerListTrickle_OnTimer(hWnd, wParam, g_session.activeMaster, &g_session.selectedServer);

        if (g_session.selectedServer)
            UI_RebuildPlayersRulesFromServer(g_session.selectedServer);

        return;
    }

    if (HomeView_OnTimer(hWnd, wParam))
        return;
}