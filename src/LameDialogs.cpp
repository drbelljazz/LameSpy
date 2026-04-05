#include <stdio.h>
#include <stdlib.h>
#include <ShObjIdl.h>
#include "LameCore.h"
#include "LameUI.h"
#include "LameWin.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameFonts.h"
#include "LameStatusBar.h"
#include "resource.h"
#include "version.h"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

static GameId g_gameConfigInitialGame = GAME_NONE;

//
// Dialog procedures
//

INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        SetDialogIcon(IDI_LAMESPY, hDlg);
        SetDlgItemTextW(hDlg, IDC_ABOUTTEXT2, L"Version " WIDEN(VER_PRODUCTVERSION_STR));
        return (INT_PTR)TRUE;

    case WM_NOTIFY:
    {
        NMHDR* pnm = (NMHDR*)lParam;
        if (pnm && pnm->idFrom == IDC_LAMELINK &&
            (pnm->code == NM_CLICK || pnm->code == NM_RETURN))
        {
            ShellExecuteW(hDlg, L"open", L"https://lamespy.org", NULL, NULL, SW_SHOWNORMAL);
            return (INT_PTR)TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_UPDATE)
        {
            UI_RequestManualVersionCheck();
            return TRUE;
        }

        if (LOWORD(wParam) == IDOK)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

//
// Add Favorite dialog
//

static void FavDlg_EnableAddrControls(HWND hDlg)
{
    const BOOL useDns = (IsDlgButtonChecked(hDlg, IDC_RADIO_DNS) == BST_CHECKED);

    EnableWindow(GetDlgItem(hDlg, IDC_DNS_INPUT), useDns);
    EnableWindow(GetDlgItem(hDlg, IDC_IP_INPUT), !useDns);

    if (useDns)
        SetFocus(GetDlgItem(hDlg, IDC_DNS_INPUT));
    else
        SetFocus(GetDlgItem(hDlg, IDC_IP_INPUT));
}

static void FavDlg_TrimInPlace(wchar_t* s)
{
    if (!s) return;

    // left trim
    wchar_t* p = s;
    while (*p && iswspace(*p)) p++;
    if (p != s)
        memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));

    // right trim
    size_t n = wcslen(s);
    while (n > 0 && iswspace(s[n - 1]))
        s[--n] = 0;
}

static int FavDlg_GetSelectedGame(HWND hDlg, GameId* outGame)
{
    if (!outGame) return 0;
    *outGame = GAME_NONE;

    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITE_SELECTGAME);
    if (!hCombo) return 0;

    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return 0;

    LRESULT data = SendMessageW(hCombo, CB_GETITEMDATA, sel, 0);
    if (data == CB_ERR) return 0;

    GameId g = (GameId)(int)data;
    if (g <= GAME_NONE || g >= GAME_MAX) return 0;

    *outGame = g;
    return 1;
}

static int FavDlg_GetAddress(HWND hDlg, wchar_t* out, int outCap)
{
    if (!out || outCap <= 0) return 0;
    out[0] = 0;

    const BOOL useDns = (IsDlgButtonChecked(hDlg, IDC_RADIO_DNS) == BST_CHECKED);

    if (useDns)
    {
        GetDlgItemTextW(hDlg, IDC_DNS_INPUT, out, outCap);
        FavDlg_TrimInPlace(out);
        if (out[0] == 0) return 0;
        return 1;
    }

    // IP Address control
    DWORD ip = 0;
    if (!SendDlgItemMessageW(hDlg, IDC_IP_INPUT, IPM_GETADDRESS, 0, (LPARAM)&ip))
        return 0;

    int b1 = FIRST_IPADDRESS(ip);
    int b2 = SECOND_IPADDRESS(ip);
    int b3 = THIRD_IPADDRESS(ip);
    int b4 = FOURTH_IPADDRESS(ip);

    // treat 0.0.0.0 as "not entered"
    if (b1 == 0 && b2 == 0 && b3 == 0 && b4 == 0)
        return 0;

    _snwprintf_s(out, outCap, _TRUNCATE, L"%d.%d.%d.%d", b1, b2, b3, b4);
    return 1;
}

static int FavDlg_GetPort(HWND hDlg, int* outPort)
{
    if (!outPort) return 0;
    *outPort = 0;

    BOOL ok = FALSE;
    UINT port = GetDlgItemInt(hDlg, IDC_EDIT_PORT, &ok, FALSE);
    if (!ok) return 0;
    if (port == 0 || port > 65535) return 0;

    *outPort = (int)port;
    return 1;
}

static void FavDlg_ShowMissingFields(HWND hDlg)
{
    MessageBoxW(
        hDlg,
        L"To add a favorite:\n\n"
        L" Select a game\n"
        L" Enter a DNS name or an IPv4 address\n"
        L" Enter a port (165535)\n",
        L"Add Favorite",
        MB_OK | MB_ICONINFORMATION
    );
}

INT_PTR CALLBACK AddFavoriteDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDialogIcon(IDI_LAMESPY, hDlg);

        // Game combo: hard-code UT + Q3 for now
        HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITE_SELECTGAME);
        if (hCombo)
        {
            SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

            int i = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Unreal Tournament");
            if (i != CB_ERR && i != CB_ERRSPACE)
                SendMessageW(hCombo, CB_SETITEMDATA, (WPARAM)i, (LPARAM)GAME_UT99);

            i = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Quake 3 Arena");
            if (i != CB_ERR && i != CB_ERRSPACE)
                SendMessageW(hCombo, CB_SETITEMDATA, (WPARAM)i, (LPARAM)GAME_Q3);

            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
        }

        // Port: keep it tight (max "65535")
        SendDlgItemMessageW(hDlg, IDC_EDIT_PORT, EM_SETLIMITTEXT, 5, 0);

        // Default to DNS mode
        CheckDlgButton(hDlg, IDC_RADIO_DNS, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_RADIO_IP, BST_UNCHECKED);
        FavDlg_EnableAddrControls(hDlg);

        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_RADIO_DNS:
        case IDC_RADIO_IP:
            // If you only want this when the radio is actually clicked:
            if (HIWORD(wParam) == BN_CLICKED)
                FavDlg_EnableAddrControls(hDlg);
            return (INT_PTR)TRUE;

        case IDOK:
        {
            GameId game = GAME_NONE;
            wchar_t addr[256];
            int port = 0;

            if (!FavDlg_GetSelectedGame(hDlg, &game) ||
                !FavDlg_GetAddress(hDlg, addr, _countof(addr)) ||
                !FavDlg_GetPort(hDlg, &port))
            {
                FavDlg_ShowMissingFields(hDlg);
                return (INT_PTR)TRUE; // keep dialog open
            }

            // Avoid duplicates in favorites for this game
            {
                LameMaster* fm = Data_GetMasterFavorites(game);
                if (fm)
                {
                    for (int i = 0; i < fm->count; i++)
                    {
                        if (Server_MatchIPPort(fm->servers[i], addr, port))
                        {
                            MessageBoxW(
                                hDlg,
                                L"That server is already in your favorites.",
                                L"Add Favorite",
                                MB_OK | MB_ICONINFORMATION
                            );
                            return (INT_PTR)TRUE; // keep dialog open
                        }
                    }
                }
            }

            Favorites_AddInternal(game, addr, port);

            // Persist favorites.cfg
            {
                wchar_t favPath[MAX_PATH];
                Path_BuildFavoritesCfg(favPath, _countof(favPath));
                Favorites_SaveFile(favPath);
            }

            // Update UI (same behavior as the menu-path add)
            StatusTextFmtPriority(STATUS_IMPORTANT, L"Added favorite to %s.", Game_PrefixW(game));

            UI_RefreshActiveTreeSelection();
            Query_StartFavorites(game);

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    }

    return (INT_PTR)FALSE;
}


//
// Settings dialog
//

static int UI_LeftTextSizeSelToPt(int sel)
{
    // Left pane:
    // Small = old Medium = 10pt
    // Large = old Large  = 11pt
    if (sel == 0) return 10;
    if (sel == 1) return 11;
    return 10;
}

static int UI_RightTextSizeSelToPt(int sel)
{
    // Right pane:
    // Small = old Small  = 9pt
    // Large = old Medium = 10pt
    if (sel == 0) return 9;
    if (sel == 1) return 10;
    return 9;
}

static int UI_LeftTextSizePtToSel(int pt)
{
    if (pt >= 11)
        return 1;   // Large

    return 0;       // Small
}

static int UI_RightTextSizePtToSel(int pt)
{
    if (pt >= 10)
        return 1;   // Large

    return 0;       // Small
}

static void UI_FillLeftTextSizeCombo(HWND hCombo, int pt)
{
    if (!hCombo)
        return;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Small");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Large");
    SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)UI_LeftTextSizePtToSel(pt), 0);
}

static void UI_FillRightTextSizeCombo(HWND hCombo, int pt)
{
    if (!hCombo)
        return;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Small");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Large");
    SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)UI_RightTextSizePtToSel(pt), 0);
}

static void UI_RecreateMainFontsFromConfig(void)
{
    HFONT hNewListFont;
    HFONT hNewTreeFont;
    HFONT hOldListFont;
    HFONT hOldTreeFont;

    hNewListFont = CreateListFont(g_config.rightPaneFontPt, FALSE);
    hNewTreeFont = CreateTreeFont(g_config.leftPaneFontPt, FALSE);

    if (!hNewListFont || !hNewTreeFont)
    {
        if (hNewListFont)
            DeleteObject(hNewListFont);
        if (hNewTreeFont)
            DeleteObject(hNewTreeFont);
        return;
    }

    hOldListFont = UI_GetListFont();
    hOldTreeFont = UI_GetTreeFont();

    UI_SetListFont(hNewListFont);
    UI_SetTreeFont(hNewTreeFont);

    if (g_ui.hTreeMenu && IsWindow(g_ui.hTreeMenu))
        SendMessageW(g_ui.hTreeMenu, WM_SETFONT, (WPARAM)UI_GetTreeFont(), TRUE);

    if (g_ui.hServerList && IsWindow(g_ui.hServerList))
        SendMessageW(g_ui.hServerList, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);

    if (g_ui.hPlayerList && IsWindow(g_ui.hPlayerList))
        SendMessageW(g_ui.hPlayerList, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);

    if (g_ui.hRulesList && IsWindow(g_ui.hRulesList))
        SendMessageW(g_ui.hRulesList, WM_SETFONT, (WPARAM)UI_GetListFont(), TRUE);

    {
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

    if (g_ui.hTreeMenu && IsWindow(g_ui.hTreeMenu))
        InvalidateRect(g_ui.hTreeMenu, NULL, TRUE);

    if (g_ui.hServerList && IsWindow(g_ui.hServerList))
        InvalidateRect(g_ui.hServerList, NULL, TRUE);

    if (g_ui.hPlayerList && IsWindow(g_ui.hPlayerList))
        InvalidateRect(g_ui.hPlayerList, NULL, TRUE);

    if (g_ui.hRulesList && IsWindow(g_ui.hRulesList))
        InvalidateRect(g_ui.hRulesList, NULL, TRUE);

    if (hOldListFont)
        DeleteObject(hOldListFont);

    if (hOldTreeFont)
        DeleteObject(hOldTreeFont);
}

INT_PTR CALLBACK SettingsDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDialogIcon(IDI_LAMESPY, hDlg);

        // Left/right pane text size combos
        UI_FillLeftTextSizeCombo(GetDlgItem(hDlg, IDC_TEXTSIZE_LEFT), g_config.leftPaneFontPt);
        UI_FillRightTextSizeCombo(GetDlgItem(hDlg, IDC_TEXTSIZE_RIGHT), g_config.rightPaneFontPt);

        // Existing global options
        CheckDlgButton(hDlg, IDC_CHECK_SHOWMASTERS,
            g_config.showMasters ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_CHECK_DEDUPE,
            g_config.dedupeLists ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_CHECK_FAVESFILTER,
            g_config.hideDeadFavorites ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_HIDEDEADINTERNETS,
            g_config.hideDeadInternets ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_EXPANDONSTART,
            g_config.expandTreeOnStartup ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_SHOWQTV,
            g_config.showQtvQwfwd ? BST_CHECKED : BST_UNCHECKED);

        // Per-sound toggles
        CheckDlgButton(hDlg, IDC_SOUND_WELCOME,
            (g_config.soundFlags & LSOUND_WELCOME) ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_SOUND_COMPLETE,
            (g_config.soundFlags & LSOUND_SCAN_COMPLETE) ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_SOUND_ABORTED,
            (g_config.soundFlags & LSOUND_UPDATE_ABORT) ? BST_CHECKED : BST_UNCHECKED);

        CheckDlgButton(hDlg, IDC_SOUND_KILLING,
            (g_config.soundFlags & LSOUND_LAUNCH) ? BST_CHECKED : BST_UNCHECKED);

        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            HWND hLeftCombo;
            HWND hRightCombo;
            int leftSel;
            int rightSel;

            hLeftCombo = GetDlgItem(hDlg, IDC_TEXTSIZE_LEFT);
            hRightCombo = GetDlgItem(hDlg, IDC_TEXTSIZE_RIGHT);

            leftSel = (int)SendMessageW(hLeftCombo, CB_GETCURSEL, 0, 0);
            rightSel = (int)SendMessageW(hRightCombo, CB_GETCURSEL, 0, 0);

            if (leftSel == CB_ERR)
                leftSel = UI_LeftTextSizePtToSel(g_config.leftPaneFontPt);

            if (rightSel == CB_ERR)
                rightSel = UI_RightTextSizePtToSel(g_config.rightPaneFontPt);

            g_config.leftPaneFontPt = UI_LeftTextSizeSelToPt(leftSel);
            g_config.rightPaneFontPt = UI_RightTextSizeSelToPt(rightSel);

            g_config.showMasters =
                (IsDlgButtonChecked(hDlg, IDC_CHECK_SHOWMASTERS) == BST_CHECKED) ? 1 : 0;

            g_config.dedupeLists =
                (IsDlgButtonChecked(hDlg, IDC_CHECK_DEDUPE) == BST_CHECKED) ? 1 : 0;

            g_config.hideDeadFavorites =
                (IsDlgButtonChecked(hDlg, IDC_CHECK_FAVESFILTER) == BST_CHECKED) ? 1 : 0;

            g_config.hideDeadInternets =
                (IsDlgButtonChecked(hDlg, IDC_HIDEDEADINTERNETS) == BST_CHECKED) ? 1 : 0;

            g_config.expandTreeOnStartup =
                (IsDlgButtonChecked(hDlg, IDC_EXPANDONSTART) == BST_CHECKED) ? 1 : 0;

            g_config.showQtvQwfwd =
                (IsDlgButtonChecked(hDlg, IDC_SHOWQTV) == BST_CHECKED) ? 1 : 0;

            g_config.soundFlags = 0;

            if (IsDlgButtonChecked(hDlg, IDC_SOUND_WELCOME) == BST_CHECKED)
                g_config.soundFlags |= LSOUND_WELCOME;

            if (IsDlgButtonChecked(hDlg, IDC_SOUND_COMPLETE) == BST_CHECKED)
                g_config.soundFlags |= LSOUND_SCAN_COMPLETE;

            if (IsDlgButtonChecked(hDlg, IDC_SOUND_ABORTED) == BST_CHECKED)
                g_config.soundFlags |= LSOUND_UPDATE_ABORT;

            if (IsDlgButtonChecked(hDlg, IDC_SOUND_KILLING) == BST_CHECKED)
                g_config.soundFlags |= LSOUND_LAUNCH;

            Config_Save();

            // Apply live UI changes now
            UI_RecreateMainFontsFromConfig();
            //UI_BuildTree();   // EXPERIMENT:  CAN WE DITCH THIS???????
            UI_RefreshCurrentView();

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}


//
// Game config dialog
//

static void GC_TrimInPlace(wchar_t* s)
{
    if (!s) return;

    wchar_t* t = WTrim(s);
    if (t != s)
        memmove(s, t, (wcslen(t) + 1) * sizeof(wchar_t));

    size_t n = wcslen(s);
    while (n > 0 && iswspace((unsigned short)s[n - 1]))
        s[--n] = 0;
}

static void GC_PopulateGameCombo(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_GAMECONFIG_SELECT);
    if (!hCombo) return;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

    for (int gi = 0; gi < GAME_MAX; gi++)
    {
        if (gi == GAME_NONE)
            continue;

        // Will need to implement Rune, etc later
        if (gi == GAME_UE)
            continue;

        const LameGameDescriptor* desc = Game_GetDescriptor((GameId)gi);
        if (!desc || !desc->name)
            continue;

        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)desc->name);
        if (idx >= 0)
            SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)gi);
    }

    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

static GameId GC_GetSelectedGame(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_GAMECONFIG_SELECT);
    if (!hCombo) return GAME_NONE;

    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return GAME_NONE;

    LRESULT data = SendMessageW(hCombo, CB_GETITEMDATA, sel, 0);
    if (data <= GAME_NONE || data >= GAME_MAX)
        return GAME_NONE;

    return (GameId)data;
}

static void GC_LoadControlsFromConfig(HWND hDlg, GameId game)
{
    const wchar_t* exe = Config_GetExePath(game);
    const wchar_t* cmd = Config_GetCmdArgs(game);

    SetDlgItemTextW(hDlg, IDC_GAMECONFIG_PATH, exe ? exe : L"");
    SetDlgItemTextW(hDlg, IDC_GAMECONFIG_CMD, cmd ? cmd : L"");
}

static void GC_SaveControlsToConfig(HWND hDlg, GameId game)
{
    wchar_t exe[CFG_EXEPATH_LEN] = { 0 };
    wchar_t cmd[CFG_CMD_LEN] = { 0 };

    GetDlgItemTextW(hDlg, IDC_GAMECONFIG_PATH, exe, (int)_countof(exe));
    GetDlgItemTextW(hDlg, IDC_GAMECONFIG_CMD, cmd, (int)_countof(cmd));

    GC_TrimInPlace(exe);
    GC_TrimInPlace(cmd);

    Config_SetExePath(game, exe);
    Config_SetCmdArgs(game, cmd);
}


// File selection code
static BOOL GC_BrowseForExe(HWND hDlg)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return FALSE;

    IFileDialog* pfd = NULL;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (FAILED(hr))
        return FALSE;

    COMDLG_FILTERSPEC filters[] =
    {
        { L"Executable Files (*.exe)", L"*.exe" },
        { L"All Files (*.*)", L"*.*" }
    };

    pfd->SetFileTypes(_countof(filters), filters);
    pfd->SetFileTypeIndex(1);
    pfd->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

    hr = pfd->Show(hDlg);
    if (SUCCEEDED(hr))
    {
        IShellItem* psi = NULL;
        hr = pfd->GetResult(&psi);
        if (SUCCEEDED(hr))
        {
            PWSTR path = NULL;
            hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr))
            {
                SetDlgItemTextW(hDlg, IDC_GAMECONFIG_PATH, path);
                CoTaskMemFree(path);
                psi->Release();
                pfd->Release();
                return TRUE;
            }
            psi->Release();
        }
    }

    pfd->Release();
    return FALSE;
}

static void GC_SelectGameInCombo(HWND hDlg, GameId game)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_GAMECONFIG_SELECT);
    int count, i;

    if (!hCombo || game == GAME_NONE)
        return;

    count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++)
    {
        GameId itemGame = (GameId)SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)i, 0);
        if (itemGame == game)
        {
            SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
}

INT_PTR CALLBACK GameConfigDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static GameId s_curGame = GAME_NONE;

    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDialogIcon(IDI_LAMESPY, hDlg);

        // limits
        SendDlgItemMessageW(hDlg, IDC_GAMECONFIG_PATH, EM_SETLIMITTEXT, CFG_EXEPATH_LEN - 1, 0);
        SendDlgItemMessageW(hDlg, IDC_GAMECONFIG_CMD, EM_SETLIMITTEXT, CFG_CMD_LEN - 1, 0);

        // make sure config is loaded before showing values (harmless if already loaded)
        Config_Load();

        GC_PopulateGameCombo(hDlg);

        if (g_gameConfigInitialGame != GAME_NONE)
            GC_SelectGameInCombo(hDlg, g_gameConfigInitialGame);

        s_curGame = GC_GetSelectedGame(hDlg);
        if (s_curGame != GAME_NONE)
            GC_LoadControlsFromConfig(hDlg, s_curGame);

        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_GAMECONFIG_SELECT:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                // commit edits for previous selection before switching
                if (s_curGame != GAME_NONE)
                    GC_SaveControlsToConfig(hDlg, s_curGame);

                s_curGame = GC_GetSelectedGame(hDlg);
                if (s_curGame != GAME_NONE)
                    GC_LoadControlsFromConfig(hDlg, s_curGame);

                return (INT_PTR)TRUE;
            }
            break;

        case IDC_GAMECONFIG_FILE:
        {
            if (HIWORD(wParam) == BN_CLICKED)
            {
                GC_BrowseForExe(hDlg);
                return (INT_PTR)TRUE;
            }
        }
        break;

        case IDC_GAMECONFIG_SAVE:
        {
            GameId g = GC_GetSelectedGame(hDlg);
            if (g != GAME_NONE)
                GC_SaveControlsToConfig(hDlg, g);

            Config_Save();
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    }
    return (INT_PTR)FALSE;
}



//
// Profile dialog
//

static void Profile_PopulateFavoriteGameCombo(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITEGAME);
    if (!hCombo)
        return;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

    for (int gi = 0; gi < GAME_MAX; gi++)
    {
        if (gi == GAME_NONE)
            continue;

        if (gi == GAME_UE)
            continue;

        const LameGameDescriptor* desc = Game_GetDescriptor((GameId)gi);
        if (!desc || !desc->name)
            continue;

        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)desc->name);
        if (idx >= 0)
            SendMessageW(hCombo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)gi);
    }

    // Default selection
    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

static void Profile_SelectFavoriteGame(HWND hDlg, GameId game)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITEGAME);
    if (!hCombo || game == GAME_NONE)
        return;

    int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++)
    {
        GameId itemGame = (GameId)SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)i, 0);
        if (itemGame == game)
        {
            SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
}

static GameId Profile_GetSelectedFavoriteGame(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITEGAME);
    if (!hCombo)
        return GAME_NONE;

    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
        return GAME_NONE;

    return (GameId)SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
}

static void Profile_PopulateStartupItemCombo(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITEGAME);
    if (!hCombo)
        return;

    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

    {
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Home");
        if (idx >= 0)
            SendMessageA(hCombo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)0);
    }

    {
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Favorites");
        if (idx >= 0)
            SendMessageA(hCombo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)1);
    }

    for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
    {
        const LameGameDescriptor* desc = Game_GetDescriptor((GameId)gi);
        if (!desc || !desc->name)
            continue;

        if (gi == GAME_UE)
            continue;

        // NEW: honor per-game enable/disable
        if ((g_config.enabledGameMask & (1u << desc->id)) == 0)
            continue;

        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)desc->name);
        if (idx >= 0)
            SendMessageW(hCombo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)gi);
    }

    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

static void Profile_SelectStartupItem(HWND hDlg, const char* startupItem)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITEGAME);
    if (!hCombo)
        return;

    if (!startupItem || !startupItem[0])
    {
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0); // Home
        return;
    }

    int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);

    if (_stricmp(startupItem, "HOME") == 0)
    {
        for (int i = 0; i < count; i++)
        {
            LPARAM data = SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)i, 0);
            if (data == 0)
            {
                SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)i, 0);
                return;
            }
        }
    }

    if (_stricmp(startupItem, "FAVORITES") == 0)
    {
        for (int i = 0; i < count; i++)
        {
            LPARAM data = SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)i, 0);
            if (data == 1)
            {
                SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)i, 0);
                return;
            }
        }
    }

    {
        GameId game = Game_FromPrefixA(startupItem);
        if (game != GAME_NONE && game != GAME_UE)
        {
            for (int i = 0; i < count; i++)
            {
                LPARAM data = SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)i, 0);
                if ((GameId)data == game)
                {
                    SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)i, 0);
                    return;
                }
            }
        }
    }

    SendMessageW(hCombo, CB_SETCURSEL, 0, 0); // fallback = Home
}

static void Profile_SaveSelectedStartupItem(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FAVORITEGAME);
    if (!hCombo)
    {
        g_config.startupItem[0] = 0;
        return;
    }

    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
    {
        strcpy_s(g_config.startupItem, _countof(g_config.startupItem), "HOME");
        return;
    }

    LPARAM data = SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);

    if (data == 0)
    {
        strcpy_s(g_config.startupItem, _countof(g_config.startupItem), "HOME");
        return;
    }

    if (data == 1)
    {
        strcpy_s(g_config.startupItem, _countof(g_config.startupItem), "FAVORITES");
        return;
    }

    {
        GameId game = (GameId)data;
        const wchar_t* prefix = Game_PrefixW(game);

        if (game != GAME_NONE && game != GAME_UE && prefix && prefix[0] && prefix[1])
        {
            char a[16] = {};
            a[0] = (char)prefix[0];
            a[1] = (char)prefix[1];
            a[2] = 0;
            strcpy_s(g_config.startupItem, _countof(g_config.startupItem), a);
        }
        else
        {
            strcpy_s(g_config.startupItem, _countof(g_config.startupItem), "HOME");
        }
    }
}

INT_PTR CALLBACK ProfileDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDialogIcon(IDI_LAMESPY, hDlg);

        SendDlgItemMessageW(hDlg, IDC_NAMEINPUT, EM_SETLIMITTEXT, 63, 0);

        if (g_config.playerName[0])
            SetDlgItemTextW(hDlg, IDC_NAMEINPUT, g_config.playerName);

        Profile_PopulateStartupItemCombo(hDlg);
        Profile_SelectStartupItem(hDlg, g_config.startupItem);

        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            GetDlgItemTextW(hDlg,
                IDC_NAMEINPUT,
                g_config.playerName,
                _countof(g_config.playerName));

            WTrim(g_config.playerName);

            if (!g_config.playerName[0])
                wcscpy_s(g_config.playerName, _countof(g_config.playerName), L"Player");

            Profile_SaveSelectedStartupItem(hDlg);

            Config_Save();
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    }

    return (INT_PTR)FALSE;
}


//
// Game toggle dialog
//

static const int IDC_GAMETOGGLE_LIST = 6001;

static int g_gameTogglePopulating = 0;

static void GameToggleDlg_Populate(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_GAMETOGGLE_LIST);
    if (!hList)
        return;

    g_gameTogglePopulating = 1;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
    {
        const LameGameDescriptor* desc = Game_GetDescriptor((GameId)gi);
        if (!desc || desc->id == GAME_NONE || !desc->name)
            continue;

        // Keep UE hidden from toggles if the app doesn't support it in the tree.
        if (desc->id == GAME_UE)
            continue;

        int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)desc->name);
        if (idx >= 0)
        {
            SendMessageW(hList, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)desc->id);

            const BOOL enabled = (g_config.enabledGameMask & (1u << desc->id)) != 0;
            SendMessageW(hList, LB_SETSEL, (WPARAM)enabled, (LPARAM)idx);
        }
    }

    // Put keyboard focus/cursor on first item without toggling anything.
    SendMessageW(hList, LB_SETCARETINDEX, 0, FALSE);

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, TRUE);

    g_gameTogglePopulating = 0;
}

static void GameToggleDlg_Save(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_GAMETOGGLE_LIST);
    if (!hList)
        return;

    unsigned int mask = 0;

    const int count = (int)SendMessageW(hList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++)
    {
        const int sel = (int)SendMessageW(hList, LB_GETSEL, (WPARAM)i, 0);
        const LRESULT data = SendMessageW(hList, LB_GETITEMDATA, (WPARAM)i, 0);
        const GameId id = (GameId)(int)data;

        if (sel > 0 && id > GAME_NONE && id < GAME_MAX)
            mask |= (1u << id);
    }

    g_config.enabledGameMask = mask;
}

static void GameToggleDlg_ToggleItem(HWND hDlg, int index)
{
    HWND hList = GetDlgItem(hDlg, IDC_GAMETOGGLE_LIST);
    if (!hList || index < 0)
        return;

    const int cur = (int)SendMessageW(hList, LB_GETSEL, (WPARAM)index, 0);
    const int next = (cur > 0) ? 0 : 1;

    SendMessageW(hList, LB_SETSEL, (WPARAM)next, (LPARAM)index);
    InvalidateRect(hList, NULL, FALSE);
}

static void GameToggleDlg_DrawItem(const DRAWITEMSTRUCT* dis)
{
    if (!dis || dis->CtlType != ODT_LISTBOX)
        return;

    if (dis->itemID == (UINT)-1)
        return;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    const BOOL selected = (dis->itemState & ODS_SELECTED) != 0;
    const COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
    const COLORREF fg = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);

    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);

    SetBkColor(hdc, bg);
    SetTextColor(hdc, fg);

    wchar_t text[256] = {};
    SendMessageW(dis->hwndItem, LB_GETTEXT, (WPARAM)dis->itemID, (LPARAM)text);

    const int checked = (int)SendMessageW(dis->hwndItem, LB_GETSEL, (WPARAM)dis->itemID, 0);

    RECT rcCheck = rc;
    rcCheck.left += 6;
    rcCheck.right = rcCheck.left + 16;
    rcCheck.top += ((rc.bottom - rc.top) - 16) / 2;
    rcCheck.bottom = rcCheck.top + 16;

    UINT state = DFCS_BUTTONCHECK;
    if (checked > 0)
        state |= DFCS_CHECKED;

    DrawFrameControl(hdc, &rcCheck, DFC_BUTTON, state);

    RECT rcText = rc;
    rcText.left = rcCheck.right + 8;
    rcText.right -= 6;

    DrawTextW(hdc, text, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (dis->itemState & ODS_FOCUS)
        DrawFocusRect(hdc, &rc);
}

INT_PTR CALLBACK GameToggleDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDialogIcon(IDI_LAMESPY, hDlg);

        RECT rc = {};
        GetClientRect(hDlg, &rc);

        const int pad = 8;
        const int btnH = 14;

        const int labelH = 16;
        const int gap = 9;

        const int bottomOffset = 25;

        HWND hLabel = CreateWindowExW(
            0,
            L"STATIC",
            L"  Select which games show in the tree:",
            WS_CHILD | WS_VISIBLE,
            pad, pad,
            (rc.right - rc.left) - pad * 2,
            labelH,
            hDlg,
            NULL,
            g_ui.hInst,
            NULL);

        if (hLabel)
        {
            HFONT hFont = UI_GetListFont() ? UI_GetListFont() : UI_GetDefaultGuiFont();
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        HWND hList = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
            LBS_MULTIPLESEL,
            pad,
            pad + labelH + gap,
            (rc.right - rc.left) - pad * 2,
            (rc.bottom - rc.top - bottomOffset) - pad * 3 - btnH - (labelH + gap),
            hDlg,
            (HMENU)(INT_PTR)IDC_GAMETOGGLE_LIST,
            g_ui.hInst,
            NULL);

        if (hList)
        {
            HFONT hFont = UI_GetListFont() ? UI_GetListFont() : UI_GetDefaultGuiFont();
            SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        GameToggleDlg_Populate(hDlg);
        return (INT_PTR)TRUE;
    }

    case WM_DRAWITEM:
        if ((int)wParam == IDC_GAMETOGGLE_LIST)
        {
            GameToggleDlg_DrawItem((const DRAWITEMSTRUCT*)lParam);
            return (INT_PTR)TRUE;
        }
        break;

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_GAMETOGGLE_LIST)
        {
            // With LBS_MULTIPLESEL, Windows already toggles selection on click.
            // If we toggle again here, it cancels out (flicker/no change).
            if (!g_gameTogglePopulating && code == LBN_SELCHANGE)
            {
                HWND hList = GetDlgItem(hDlg, IDC_GAMETOGGLE_LIST);
                if (hList)
                    InvalidateRect(hList, NULL, FALSE);
                return (INT_PTR)TRUE;
            }

            return (INT_PTR)TRUE;
        }

        if (id == IDOK)
        {
            GameToggleDlg_Save(hDlg);
            Config_Save();
            UI_BuildTree();
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }

        if (id == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }

    case WM_VKEYTOITEM:
        // Space toggles current item in the listbox.
        if (LOWORD(wParam) == VK_SPACE)
        {
            int idx = (int)HIWORD(wParam);
            GameToggleDlg_ToggleItem(hDlg, idx);
            return (INT_PTR)-2;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

//
// Set dialog icon
//

void SetDialogIcon(int resourceId, HWND hDlg)
{
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCEW(resourceId));
    SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
}
