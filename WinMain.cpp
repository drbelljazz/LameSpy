#include "resource.h"
#include "framework.h"

#include "LameSpy.h"

HINSTANCE hInst;                                
WCHAR szTitle[MAX_LOADSTRING];                
WCHAR szWindowClass[MAX_LOADSTRING];           

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    AboutDlg(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    SettingsDlg(HWND, UINT, WPARAM, LPARAM);

//INT_PTR CALLBACK    MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
//HWND hMainDlg;  // This dialog box will be the main window

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LAMESPY, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAMESPY));

    MSG msg;

    // WndProc
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

#if 0
    // If we want to use a dialog box as our main window instead
    while (GetMessage(&msg, NULL, 0, 0)) 
    {
        if (!IsDialogMessage(hMainDlg, &msg)) 
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
#endif

    return (int) msg.wParam;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAMESPY));
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_LAMESPY));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = CreateSolidBrush(RGB(220, 220, 220));
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_LAMESPY);
    wcex.lpszClassName  = szWindowClass;

    return RegisterClassExW(&wcex);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance;

   // Main window (WndProc)
   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
       return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   // Alternative main window (MainDlgProc)
#if 0
   // Using a dialog box as our main window instead
   hMainDlg = CreateDialog(
       hInstance,
       MAKEINTRESOURCE(IDD_MAIN),
       NULL,
       MainDlgProc
   );

   if (!hMainDlg)
   {
       return FALSE;
   }

   ShowWindow(hMainDlg, SW_SHOW);
   UpdateWindow(hMainDlg);
#endif

   return TRUE;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   switch (message)
   {
   case WM_CREATE:

       //File_CheckCFG("lamespy.cfg");  // Enable this when we go back to GUI development

       Net_InitSockets(); // TODO: error check

       if (/*LamespySettings.ConsoleOnStart ==*/ 1)
       {
           OpenConsole();
           LameMain();
           FreeConsole();
           Net_ShutdownSockets();
           PostQuitMessage(0);  // Temporary for development; will usually go back to GUI
       }
       else
       {
           SetWindowPos(hWnd, NULL, 700, 400, 800, 600, NULL);
           FillServerList(hWnd, hInst);
       }

   break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId)
        {
        case ID_SERVERS_CLEARLISTCACHE:
            if (!File_DeleteServerList())
                MessageBoxA(hWnd, "Failed to delete q3servers.cfg", "Whoops!", MB_OK);
            break;

        case ID_SERVERS_GETMASTERLISTS:
            if (!Net_DownloadMasterList())
                MessageBoxA(hWnd, "Failed to create text file!", "Whoops!", MB_OK);

            FillServerList(hWnd, hInst);
            break;

        case ID_SERVERS_REFRESHALLSERVERS:
            //RefreshServers(hDlg);
            //FillServerList(hDlg);
            break;

        case ID_HELP_NANOTECHSTUIDIOS:
            LaunchWebsite();
            break;

        case ID_LAMESPY_CONSOLE:
            OpenConsole();
            LameMain();
            FreeConsole();
            PostQuitMessage(0);
            break;

        case ID_FILE_SETTINGS:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGSBOX), hWnd, SettingsDlg);
            break;

        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutDlg);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_CLOSE:
    case WM_QUIT:
        Net_ShutdownSockets(); // Right place for this?
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}


// Message handler for about box
INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {            
            PlaySoundA("launch.wav", NULL, SND_FILENAME | SND_ASYNC);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


// Message handler for settings dialog
INT_PTR CALLBACK SettingsDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_OK)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

#if 0
// Message handler for main dialog window
INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HICON hIcon;
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        /*
        hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_LAMESPY));
        SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        PlayLameSound("welcome.wav");

        InitMainDlg(hDlg, hInst);

        //InitListBox(hDlg);

        //FillServerList(hDlg);
        */
        return (INT_PTR)TRUE;

    case WM_COMMAND:

        int wmId = LOWORD(wParam);

        switch (wmId)
        {
            /*
        case ID_SERVERS_REFRESHALLSERVERS:
            //RefreshServers(hDlg);
            FillServerList(hDlg);
            break;

        case ID_HELP_NANOTECHSTUIDIOS:
            LaunchWebsite();
            break;

        case ID_LAMESPY_CONSOLE:
            OpenConsole();
            LameMain();
            FreeConsole();
            PostQuitMessage(0);
            break;
             
        case ID_FILE_SETTINGS:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGSBOX), hDlg, Settings);
            break;
            */
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, About);
            break;

        case IDM_EXIT:
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            PostQuitMessage(0);
            return (INT_PTR)TRUE;
        }
    }
    return (INT_PTR)FALSE;
}
#endif