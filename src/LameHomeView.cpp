#include "LameHomeView.h"

#include "LameData.h"
#include "LameFilters.h"
#include "LameUI.h"
#include "LameWeb.h"

static HWND g_hwndMain = NULL;
static HWND g_hWebHost = NULL;

static LameServer* g_homeFavoritesList[4096];
static LameMaster g_homeFavoritesMaster;

void HomeView_Init(HWND hwndMain, HINSTANCE hInst, HWND hwndParent)
{
    g_hwndMain = hwndMain;

    g_hWebHost = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        NULL,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 100, 100,
        hwndParent,
        NULL,
        hInst,
        NULL);

    if (g_hWebHost)
        ShowWindow(g_hWebHost, SW_HIDE);

    if (hwndMain && g_hWebHost)
        Web_Init(hwndMain, g_hWebHost);
}

void HomeView_KillTimers(void)
{
    if (!g_hwndMain)
        return;

    KillTimer(g_hwndMain, IDT_HOME_LOAD_TIMEOUT);
    KillTimer(g_hwndMain, IDT_WEB_REFIT_RESIZE);
}

void HomeView_Shutdown(void)
{
    HomeView_KillTimers();

    Web_Shutdown();

    if (g_hWebHost && IsWindow(g_hWebHost))
        DestroyWindow(g_hWebHost);

    g_hWebHost = NULL;
    g_hwndMain = NULL;
}

void HomeView_SetVisible(BOOL show)
{
    Web_SetVisible(show);
}

BOOL HomeView_IsVisible(void)
{
    return Web_IsVisible();
}

void HomeView_SetMode(BOOL showHome)
{
    HomeView_SetVisible(showHome);

    // List panes
    if (g_ui.hServerList) ShowWindow(g_ui.hServerList, showHome ? SW_HIDE : SW_SHOW);
    if (g_ui.hPlayerList) ShowWindow(g_ui.hPlayerList, showHome ? SW_HIDE : SW_SHOW);
    if (g_ui.hRulesList)  ShowWindow(g_ui.hRulesList, showHome ? SW_HIDE : SW_SHOW);

    // Top filters: only show when NOT home, and only if filters enabled.
    const BOOL showFilters = (!showHome && Filters_AreVisible());

    if (g_ui.hFilterSearch)       ShowWindow(g_ui.hFilterSearch, showFilters ? SW_SHOW : SW_HIDE);
    if (g_ui.hFilterField)        ShowWindow(g_ui.hFilterField, showFilters ? SW_SHOW : SW_HIDE);
    if (g_ui.hFilterShowFull)     ShowWindow(g_ui.hFilterShowFull, showFilters ? SW_SHOW : SW_HIDE);
    if (g_ui.hFilterShowEmpty)    ShowWindow(g_ui.hFilterShowEmpty, showFilters ? SW_SHOW : SW_HIDE);
    if (g_ui.hFilterShowPassword) ShowWindow(g_ui.hFilterShowPassword, showFilters ? SW_SHOW : SW_HIDE);
}

void HomeView_ShowPage(const wchar_t* title, const wchar_t* url)
{
    HomeView_SetMode(TRUE);
    Web_ShowPage(title, url, TRUE);
}

void HomeView_ShowHomePage(void)
{
    HomeView_SetMode(TRUE);
    Web_ShowHomePage();
}

void HomeView_MoveHost(int x, int y, int w, int h)
{
    if (!g_hWebHost || !IsWindow(g_hWebHost))
        return;

    MoveWindow(g_hWebHost, x, y, w, h, TRUE);
    Web_OnHostResized();
}

void HomeView_BuildHomeFavoritesMaster(void)
{
    g_homeFavoritesMaster.servers = g_homeFavoritesList;
    g_homeFavoritesMaster.cap = (int)_countof(g_homeFavoritesList);
    g_homeFavoritesMaster.count = 0;
    wcscpy_s(g_homeFavoritesMaster.name, _countof(g_homeFavoritesMaster.name), L"All Favorites");

    Data_Lock();

    for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
    {
        LameMaster* fav = Data_GetMasterFavorites((GameId)gi);
        if (!fav)
            continue;

        for (int i = 0; i < fav->count; i++)
        {
            LameServer* s = fav->servers[i];
            if (!s)
                continue;

            if (g_homeFavoritesMaster.count >= g_homeFavoritesMaster.cap)
                break;

            g_homeFavoritesMaster.servers[g_homeFavoritesMaster.count++] = s;
        }
    }

    Data_Unlock();
}

LameMaster* HomeView_GetHomeFavoritesMaster(void)
{
    return &g_homeFavoritesMaster;
}

BOOL HomeView_OnTimer(HWND hWnd, WPARAM wParam)
{
    return Web_OnTimer(hWnd, wParam);
}