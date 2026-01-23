#include "resource.h"
#include "lamespy.h"

extern char SelectedServer[NUMSTATS][LENGTH];


HWND InitServerListBox(HWND hDlg, HINSTANCE hInst)
{
    HWND hList = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        15, 35, 750, 500,
        hDlg,
        (HMENU)1001,
        hInst,
        NULL
    );

    ListView_SetExtendedListViewStyle(
        hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES
    );

    LVCOLUMNA col = { 0 };
    col.mask = LVCF_TEXT | LVCF_WIDTH;

    col.pszText = (LPSTR)TEXT("Server");
    col.cx = 200;
    ListView_InsertColumn(hList, 0, &col);

    col.pszText = (LPSTR)TEXT("Map");
    col.cx = 100;
    ListView_InsertColumn(hList, 1, &col);

    col.pszText = (LPSTR)TEXT("Ping");
    col.cx = 80;
    ListView_InsertColumn(hList, 2, &col);

    /*col.pszText = (LPSTR)TEXT("Players");
    col.cx = 60;
    ListView_InsertColumn(hList, 3, &col);*/

    return hList;
}

// Created for Windows GUI version
void FillServerList(HWND hWnd, HINSTANCE hInst)
{
    SERVER servers[MAX_SERVERS];

    int serverCount = File_LoadServers(
        "q3favorites.cfg",
        servers,
        MAX_SERVERS
    );

    int row = 0;

    for (row = 0; row < serverCount; row++)
    {
        wchar_t serverName[64];
        wchar_t mapName[64];
        wchar_t pingToString[8];

        // Fill hostname column
        MultiByteToWideChar(CP_ACP, 0, SelectedServer[SERVER_NAME], -1, serverName, LENGTH);

        LVITEM item = { row };  // compiler warning here
        item.mask = LVIF_TEXT;
        item.iItem = row;  
        item.pszText = serverName;

        HWND hList = InitServerListBox(hWnd, hInst);

        int itemIndex = ListView_InsertItem(hList, &item);

        //  Fill map column
        MultiByteToWideChar(CP_ACP, 0, SelectedServer[CURRENT_MAP], -1, mapName, LENGTH);
        ListView_SetItemText(hList, itemIndex, 1, mapName);  // TODO: Make enum of column numbers

        // Fill ping column
        int ping = Net_PingServer(TEST_IP, NULL);
        swprintf(pingToString, 8, L"%d ms", ping);

        ListView_SetItemText(hList, itemIndex, 2, pingToString);
    }
}

// Created for Windows GUI version
int File_LoadServers(const char* filename, SERVER* servers, int maxServers)
{
    FILE* f = fopen(filename, "r");
    if (!f) return 0;

    char line[128];
    int count = 0;

    while (fgets(line, sizeof(line), f) && count < maxServers)
    {
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;

        if (ParseLine(line, &servers[count]))
        {
            count++;
        }
    }

    fclose(f);
    return count;
}


// Created for Windows GUI version
int ParseLine(const char* line, SERVER* server)
{
    const char* colon = strchr(line, ':');
    if (!colon) return 0;

    size_t ipLen = colon - line;
    if (ipLen == 0 || ipLen >= IP_LENGTH) return 0;

    strncpy(server->ip, line, ipLen);
    server->ip[ipLen] = '\0';

    server->port = atoi(colon + 1);
    if (server->port <= 0 || server->port > 65535)
        return 0;

    return 1; // success
}


// Only used by GUI
void PlayLameSound(LPCSTR sound)
{
    static int SoundsEnabled = false;  // TEMP: go global

    if (SoundsEnabled)
        PlaySoundA(sound, NULL, SND_FILENAME | SND_ASYNC);
}

// Only used by GUI
void LaunchWebsite()
{
    const char* url = "http://www.nanotechstudios.com";

    // Launch default browser to open URL
    HINSTANCE result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);

    // Check for errors
    if ((INT_PTR)result <= 32) {
        MessageBoxA(NULL, "Failed to open URL", "Error", MB_OK | MB_ICONERROR);
    }
}


// For launching the console from our GUI 
void OpenConsole()
{
    if (AllocConsole())
    {
        HANDLE hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);

        // Redirect C runtime stdout, stdin, and stderr to the console
        // Use freopen or _dup2 and _open_osfhandle
        //FILE* fp;
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);

        // Optional: clear the error state for the streams
        _flushall();
        // Optional: set the console title
        SetConsoleTitle(L"LameSpy3D Console");
    }
}