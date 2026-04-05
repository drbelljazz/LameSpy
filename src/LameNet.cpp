#include "LameCore.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameNet.h"
#include "LameWin.h"
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// -----------------------------------------------------------------------------
// Global state (thread-safe)
// -----------------------------------------------------------------------------

static volatile LONG g_queryGeneration = 0;
static volatile LONG g_queryStop = 0;
static volatile LONG g_queriesInFlight = 0;
static volatile LONG g_masterQueriesInFlight = 0;

// One-shot latch: last generation that already fired Net_NotifyAllDone()
static volatile LONG g_allDoneNotifiedGeneration = 0;

// Version checking
static PVOID g_versionCheckCallback = NULL;
static PVOID g_versionCheckUser = NULL;

// Callback for query completion
static PVOID g_queryFinishedCallback = NULL;

// Counts queued workers that schedule server queries (Query_StartAllServersWorker)
static volatile LONG g_startAllWorkersInFlight = 0;

// Hard cap simultaneous active UDP queries to avoid scheduler/queueing "fake ping"
#define MAX_ACTIVE_SERVER_QUERIES 64 // was 64
static volatile LONG g_activeServerQueries = 0;

static LameNetNotify g_notify = { 0 };

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

static const char* Net_GetLameServerHost(void)
{
    return g_config.lameServerHost[0] ? g_config.lameServerHost : LAME_SERVER_IP;
}

static int Net_GetLameServerPort(void)
{
    if (g_config.lameServerPort > 0 && g_config.lameServerPort <= 65535)
        return g_config.lameServerPort;
    return LAME_SERVER_PORT;
}

static int AcquireActiveServerSlot(void)
{
    for (;;)
    {
        LONG cur = InterlockedCompareExchange(&g_activeServerQueries, 0, 0);
        if (cur >= MAX_ACTIVE_SERVER_QUERIES)
        {
            Sleep(1);
            continue;
        }
        if (InterlockedCompareExchange(&g_activeServerQueries, cur + 1, cur) == cur)
            return 1;
    }
}

static void ReleaseActiveServerSlot(void)
{
    InterlockedDecrement(&g_activeServerQueries);
}

// -----------------------------------------------------------------------------
// Notification dispatch
// -----------------------------------------------------------------------------

static void Net_NotifyVersionCheck(VersionCheckResult result)
{
    VersionCheckCallback cb =
        (VersionCheckCallback)InterlockedCompareExchangePointer(&g_versionCheckCallback, NULL, NULL);
    void* user = InterlockedCompareExchangePointer(&g_versionCheckUser, NULL, NULL);

    if (cb)
        cb(result, user);
}

static void Net_NotifyMasterDone(GameId game, int masterIndex, int serverCount)
{
    if (g_notify.OnMasterDone)
        g_notify.OnMasterDone(game, masterIndex, serverCount, g_notify.user);
}

static void Net_NotifyAllDone(void)
{
    if (g_notify.OnAllDone)
        g_notify.OnAllDone(g_notify.user);
}

static void Net_TryNotifyAllDone(void)
{
    if (InterlockedCompareExchange(&g_masterQueriesInFlight, 0, 0) != 0)
        return;

    if (InterlockedCompareExchange(&g_queriesInFlight, 0, 0) != 0)
        return;

    if (InterlockedCompareExchange(&g_startAllWorkersInFlight, 0, 0) != 0)
        return;

    LONG gen = Query_GetGeneration();
    if (gen <= 0)
        return;

    // One-shot per generation
    for (;;)
    {
        LONG seen = InterlockedCompareExchange(&g_allDoneNotifiedGeneration, 0, 0);
        if (seen == gen)
            return; // already notified for this generation

        if (InterlockedCompareExchange(&g_allDoneNotifiedGeneration, gen, seen) == seen)
            break; // won the race to notify
    }

    Net_NotifyAllDone();
}

// -----------------------------------------------------------------------------
// Networking lifecycle
// -----------------------------------------------------------------------------

int InitializeNetworking(void)
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    return (result == 0) ? 1 : 0;
}

void CleanupNetworking(void)
{
    WSACleanup();
}

// -----------------------------------------------------------------------------
// Version check
// -----------------------------------------------------------------------------

void VersionCheck_SetNotify(VersionCheckCallback callback, void* user)
{
    InterlockedExchangePointer(&g_versionCheckUser, user);
    InterlockedExchangePointer(&g_versionCheckCallback, (PVOID)callback);
}

static int Net_CompareVersion(const char* a, const char* b)
{
    // returns: 1 if a>b, 0 if a==b, -1 if a<b
    const char* pa = a ? a : "";
    const char* pb = b ? b : "";

    for (int i = 0; i < 4; i++)
    {
        char* enda = NULL;
        char* endb = NULL;

        long va = strtol(pa, &enda, 10);
        long vb = strtol(pb, &endb, 10);

        if (va > vb) return 1;
        if (va < vb) return -1;

        pa = (enda && *enda == '.') ? enda + 1 : (enda ? enda : pa);
        pb = (endb && *endb == '.') ? endb + 1 : (endb ? endb : pb);

        if ((*pa == '\0') && (*pb == '\0'))
            break;
    }

    return 0;
}

static int CheckVersionWithServerStatus(void)
{
    char portStr[16];
    sprintf_s(portStr, sizeof(portStr), "%d", Net_GetLameServerPort());

    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo* result = NULL;
    if (getaddrinfo(Net_GetLameServerHost(), portStr, &hints, &result) != 0 || !result)
        return VERSION_CHECK_UNAVAILABLE;

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        return VERSION_CHECK_UNAVAILABLE;
    }

    DWORD timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    char user_utf8[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, g_config.playerName, -1, user_utf8, sizeof(user_utf8), NULL, NULL);

    char req[192] = {};
    snprintf(req, sizeof(req), "VERSION?\t%s\t%s\n", user_utf8[0] ? user_utf8 : "Unknown", LAMESPY_VERSION);

    if (sendto(sock, req, (int)strlen(req), 0, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
    {
        closesocket(sock);
        freeaddrinfo(result);
        return VERSION_CHECK_UNAVAILABLE;
    }

    char buf[64] = {};
    int ret = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);

    closesocket(sock);
    freeaddrinfo(result);

    if (ret <= 0)
        return VERSION_CHECK_UNAVAILABLE;

    if (strncmp(buf, "VERSION\t", 8) != 0)
        return VERSION_CHECK_UNAVAILABLE;

    char* ver = buf + 8;
    size_t end = strcspn(ver, "\r\n");
    ver[end] = '\0';

    // Client >= server means up to date
    int cmp = Net_CompareVersion(LAMESPY_VERSION, ver);
    if (cmp >= 0)
        return VERSION_CHECK_OK;

    return VERSION_CHECK_OUTDATED;
}

static DWORD WINAPI VersionCheckWorker(LPVOID param)
{
    (void)param;

    VersionCheckResult r = (VersionCheckResult)CheckVersionWithServerStatus();
    Net_NotifyVersionCheck(r);

    return 0;
}

void StartVersionCheckAsync(void)
{
    HANDLE h = CreateThread(NULL, 0, VersionCheckWorker, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
}

// -----------------------------------------------------------------------------
// Query callback registration
// -----------------------------------------------------------------------------

void Query_SetNotify(const LameNetNotify* n)
{
    if (n)
        g_notify = *n;
    else
        ZeroMemory(&g_notify, sizeof(g_notify));
}

void Query_SetFinishedCallback(ServerQueryCallback callback)
{
    InterlockedExchangePointer(&g_queryFinishedCallback, (PVOID)callback);
}

// -----------------------------------------------------------------------------
// Query state / batch control
// -----------------------------------------------------------------------------

void Query_InitState(void)
{
    InterlockedExchange(&g_queryGeneration, 0);
    InterlockedExchange(&g_queryStop, 0);
    InterlockedExchange(&g_queriesInFlight, 0);
    InterlockedExchange(&g_masterQueriesInFlight, 0);
    InterlockedExchange(&g_activeServerQueries, 0);
    InterlockedExchange(&g_startAllWorkersInFlight, 0);
    InterlockedExchange(&g_allDoneNotifiedGeneration, 0);
}

void Query_Stop(void)
{
    InterlockedExchange(&g_queryStop, 1);

    // Wait briefly for both server and master queries to wind down.
    for (int i = 0; i < 50; i++)
    {
        LONG serverInFlight = InterlockedCompareExchange(&g_queriesInFlight, 0, 0);
        LONG masterInFlight = InterlockedCompareExchange(&g_masterQueriesInFlight, 0, 0);

        if (serverInFlight <= 0 && masterInFlight <= 0)
            break;

        Sleep(50);
    }
}

void Query_IncrementGeneration(void)
{
    InterlockedIncrement(&g_queryGeneration);
}

int Query_GetGeneration(void)
{
    return InterlockedCompareExchange(&g_queryGeneration, 0, 0);
}

int Query_HasActiveQueries()
{
    return (InterlockedCompareExchange(&g_queriesInFlight, 0, 0) > 0) ||
        (InterlockedCompareExchange(&g_masterQueriesInFlight, 0, 0) > 0);
}

int Query_BeginBatch(void)
{
    InterlockedExchange(&g_queryStop, 0);
    Query_IncrementGeneration();
    return (int)Query_GetGeneration();
}

// -----------------------------------------------------------------------------
// Server querying
// -----------------------------------------------------------------------------

typedef struct StartAllServersParams
{
    GameId game;
    int masterIndex;
    int gen;
} StartAllServersParams;

void Query_OnServerFinished(LameServer* server)
{
    if (!server)
        return;

    // Only process if current generation
    if (server->gen != Query_GetGeneration())
        return;

    // ADD THIS DEBUG LINE
    //StatusTextFmt(L"DEBUG: Query_OnServerFinished called for %s:%d, callback=%p",
    //    server->ip, server->port, g_queryFinishedCallback);

    ServerQueryCallback cb = (ServerQueryCallback)InterlockedCompareExchangePointer(&g_queryFinishedCallback, NULL, NULL);
    if (cb)
        cb(server);
}

// Worker thread function
static DWORD WINAPI Query_ServerThread(LPVOID param)
{
    LameServer* server = (LameServer*)param;
    if (!server)
        return 0;

    LONG myGen = server->gen;

    if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0)
    {
        InterlockedExchange(&server->state, QUERY_CANCELED);

        InterlockedDecrement(&g_queriesInFlight);
        Net_TryNotifyAllDone();

        return 0;
    }

    if (myGen != Query_GetGeneration())
    {
        InterlockedExchange(&server->state, QUERY_CANCELED);

        InterlockedDecrement(&g_queriesInFlight);
        Net_TryNotifyAllDone();

        return 0;
    }

    GameId game = server->game;
    const LameGameDescriptor* desc = Game_GetDescriptor(game);
    if (game <= GAME_NONE || game >= GAME_MAX || !desc || !desc->QueryGameServer)
    {
        InterlockedExchange(&server->state, QUERY_FAILED);

        InterlockedDecrement(&g_queriesInFlight);
        Net_TryNotifyAllDone();

        return 0;
    }

    AcquireActiveServerSlot();

    if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 ||
        myGen != Query_GetGeneration())
    {
        InterlockedExchange(&server->state, QUERY_CANCELED);
        ReleaseActiveServerSlot();

        InterlockedDecrement(&g_queriesInFlight);
        Net_TryNotifyAllDone();

        return 0;
    }

    server->queryStartTime = GetTickCount();
    InterlockedExchange(&server->state, QUERY_IN_PROGRESS);

    int success = desc->QueryGameServer(server->ip, server->port, server);

    ReleaseActiveServerSlot();

    if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 ||
        myGen != Query_GetGeneration())
    {
        InterlockedExchange(&server->state, QUERY_CANCELED);

        InterlockedDecrement(&g_queriesInFlight);
        Net_TryNotifyAllDone();

        return 0;
    }

    if (success && server->state != QUERY_FAILED)
        InterlockedExchange(&server->state, QUERY_DONE);
    else if (server->state != QUERY_CANCELED && server->state != QUERY_FAILED)
        InterlockedExchange(&server->state, QUERY_FAILED);

    if (myGen == Query_GetGeneration())
        Query_OnServerFinished(server);

    InterlockedDecrement(&g_queriesInFlight);
    Net_TryNotifyAllDone();

    return 0;
}

void Query_StartSingleServer(LameServer* server)
{
    if (!server)
        return;

    if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0)
        return;

    if (server->state == QUERY_DONE || server->state == QUERY_IN_PROGRESS)
        return;

    if (server->gen != Query_GetGeneration())
        return;

    server->state = QUERY_IN_PROGRESS;

    // Count it as in-flight BEFORE queueing so Stop() can’t “win” early.
    InterlockedIncrement(&g_queriesInFlight);

    if (!QueueUserWorkItem((LPTHREAD_START_ROUTINE)Query_ServerThread,
        server, WT_EXECUTELONGFUNCTION))
    {
        server->state = QUERY_FAILED;

        // Undo the increment because the work item will never run.
        InterlockedDecrement(&g_queriesInFlight);
        Net_TryNotifyAllDone();
        return;
    }
}

static DWORD WINAPI Query_StartAllServersWorker(LPVOID param)
{
    StartAllServersParams* p = (StartAllServersParams*)param;
    if (!p)
    {
        InterlockedDecrement(&g_startAllWorkersInFlight);
        Net_TryNotifyAllDone();
        return 0;
    }

    GameId game = p->game;
    int masterIndex = p->masterIndex;
    int gen = p->gen;

    free(p);
    p = NULL;

    LameServer** list = NULL;
    int count = 0;

    Data_Lock();
    LameMaster* master = Data_GetMasterInternet(game, masterIndex);
    if (master && master->count > 0)
    {
        count = master->count;
        list = (LameServer**)malloc(sizeof(LameServer*) * (size_t)count);
        if (list)
        {
            for (int i = 0; i < count; i++)
                list[i] = master->servers[i];
        }
        else
        {
            count = 0;
        }
    }
    Data_Unlock();

    if (!list || count <= 0)
    {
        if (list)
            free(list);

        InterlockedDecrement(&g_startAllWorkersInFlight);
        Net_TryNotifyAllDone();
        return 0;
    }

    int batchSize = 10;
    int queriesStarted = 0;

    for (int i = 0; i < count; i++)
    {
        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0)
            break;

        if (gen != Query_GetGeneration())
            break;

        LameServer* s = list[i];
        if (!s)
            continue;

        if (s->gen != (LONG)gen)
        {
            s->gen = (LONG)gen;
            s->state = QUERY_IDLE;
        }
        else
        {
            if (s->state == QUERY_IN_PROGRESS || s->state == QUERY_DONE)
                continue;
        }

        s->game = game;
        Query_StartSingleServer(s);

        queriesStarted++;
        if (queriesStarted % batchSize == 0)
            Sleep(10);

        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0)
            break;
    }

    free(list);

    InterlockedDecrement(&g_startAllWorkersInFlight);
    Net_TryNotifyAllDone();

    return 0;
}

void Query_StartAllServersWithGen(GameId game, int masterIndex, int gen)
{
    // This may be called from UI code paths; never block the UI thread here.
    StartAllServersParams* p = (StartAllServersParams*)malloc(sizeof(StartAllServersParams));
    if (!p)
        return;

    p->game = game;
    p->masterIndex = masterIndex;
    p->gen = gen;

    InterlockedIncrement(&g_startAllWorkersInFlight);

    if (!QueueUserWorkItem((LPTHREAD_START_ROUTINE)Query_StartAllServersWorker, p, WT_EXECUTELONGFUNCTION))
    {
        InterlockedDecrement(&g_startAllWorkersInFlight);
        free(p);
        Net_TryNotifyAllDone();
        return;
    }
}

void Query_StartAllServers(GameId game, int masterIndex)
{
    int gen = Query_BeginBatch();
    Query_StartAllServersWithGen(game, masterIndex, gen);
}

void Query_StartFavoritesWithGen(GameId game, int gen)
{
    LameMaster* master = Data_GetMasterFavorites(game);
    if (!master)
        return;

    for (int i = 0; i < master->count; i++)
    {
        LameServer* s = master->servers[i];
        if (!s)
            continue;

        // Same batch for all games
        s->gen = (LONG)gen;
        s->state = QUERY_IDLE;
        s->game = game;

        Query_StartSingleServer(s);
    }
}

void Query_StartFavorites(GameId game)
{
    int gen = Query_BeginBatch();
    Query_StartFavoritesWithGen(game, gen);
}

// -----------------------------------------------------------------------------
// Master querying
// -----------------------------------------------------------------------------

typedef struct MasterQueryResult
{
    GameId game;
    int masterIndex;
    int serverCount;
} MasterQueryResult;

static void LogMasterResponse(GameId game, int masterIndex, const unsigned char* buf, int len)
{
    FILE* f = NULL;
    fopen_s(&f, "master_responses.txt", "a");
    if (!f)
        return;

    const LameMasterAddress* addr = Data_GetMasterAddress(game, masterIndex);

    fprintf(f, "\n========================================\n");
    fprintf(f, "Game: %S (ID: %d)\n", Game_PrefixW(game), game);
    if (addr)
        fprintf(f, "Master: %S:%d (Index: %d)\n", addr->address, addr->port, masterIndex);
    else
        fprintf(f, "Master: (unknown) Index: %d\n", masterIndex);
    fprintf(f, "Length: %d bytes\n", len);
    fprintf(f, "========================================\n");

    // Write raw bytes as hex dump
    fprintf(f, "Hex dump:\n");
    for (int i = 0; i < len; i++)
    {
        if (i > 0 && i % 16 == 0)
            fprintf(f, "\n");
        fprintf(f, "%02X ", (unsigned)buf[i]);
    }
    fprintf(f, "\n\n");

    // Write as ASCII (printable chars only)
    fprintf(f, "ASCII (printable):\n");
    for (int i = 0; i < len; i++)
    {
        if (buf[i] >= 32 && buf[i] < 127)
            fprintf(f, "%c", buf[i]);
        else
            fprintf(f, ".");
    }
    fprintf(f, "\n\n");

    fclose(f);
}

static int ParseMasterPacket(const unsigned char* buf, int len, GameId game, int masterIndex)
{
    LameMaster* master = Data_GetMasterInternet(game, masterIndex);
    if (!master)
        return 0;

    const LameGameDescriptor* desc = Game_GetDescriptor(game);
    if (!desc)
        return 0;

    Data_Lock(); // Ensure thread safety when modifying master server list

    int added = 0;

    // Use game-specific parser if available
    if (desc->ParseMasterPacket)
    {
        added = desc->ParseMasterPacket(buf, len, master, game);
        Data_Unlock();
        return added;
    }

    // Fallback: Q3-style parser for games without custom parser
    int i = 0;

    // Skip 0xFFFFFFFF header if present
    if (len >= 4 && buf[0] == 0xFF && buf[1] == 0xFF &&
        buf[2] == 0xFF && buf[3] == 0xFF)
        i = 4;

    // Skip ASCII response line (getserversResponse...)
    while (i < len && buf[i] != '\n')
        i++;
    if (i < len && buf[i] == '\n')
        i++;

    // Parse server entries: '\' + 4 IP bytes + 2 port bytes
    while (i + 6 < len)
    {
        if (buf[i] != '\\')
        {
            i++;
            continue;
        }

        // Check for EOT marker
        if (i + 3 < len && buf[i + 1] == 'E' &&
            buf[i + 2] == 'O' && buf[i + 3] == 'T')
            break;

        // Extract IP and port
        unsigned char b1 = buf[i + 1];
        unsigned char b2 = buf[i + 2];
        unsigned char b3 = buf[i + 3];
        unsigned char b4 = buf[i + 4];
        int port = ((int)buf[i + 5] << 8) | (int)buf[i + 6];

        if (port <= 0 || port > 65535)
        {
            i += 7;
            continue;
        }

        // Format IP string
        wchar_t ip[64];
        swprintf_s(ip, _countof(ip), L"%u.%u.%u.%u",
            (unsigned)b1, (unsigned)b2, (unsigned)b3, (unsigned)b4);

        // Check for duplicate (by IP:port)
        int duplicate = 0;
        for (int j = 0; j < master->count; j++)
        {
            if (master->servers[j]->port == port &&
                wcscmp(master->servers[j]->ip, ip) == 0)
            {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate && master->count < master->cap)
        {
            LameServer* s = master->servers[master->count];

            ZeroMemory(s, sizeof(*s));
            s->game = game;
            wcsncpy_s(s->ip, _countof(s->ip), ip, _TRUNCATE);
            s->port = port;
            s->ping = 999;
            s->state = QUERY_IDLE;
            s->gen = 0;

            swprintf_s(s->name, _countof(s->name), L"%s", ip);
            wcscpy_s(s->map, _countof(s->map), L"...");
            wcscpy_s(s->gametype, _countof(s->gametype), L"...");

            master->count++;
            added++;
        }

        i += 7;
    }

    Data_Unlock(); // Unlock after modifying master server list
    return added;
}

static DWORD WINAPI Query_MasterThread(LPVOID param)
{
    MasterQueryResult* result = (MasterQueryResult*)param;

    GameId game = GAME_NONE;
    int masterIndex = -1;
    LONG myGen = Query_GetGeneration();

    const LameMasterAddress* addr = NULL;
    const LameGameDescriptor* desc = NULL;
    LameMaster* master = NULL;

    SOCKET sock = INVALID_SOCKET;
    struct addrinfo hints;
    struct addrinfo* addrResult = NULL;

    unsigned char buf[4096];
    char narrowAddr[256];
    char portStr[16];

    DWORD firstTimeout = 300;
    DWORD normalTimeout = 5000;
    int attempts = 3;
    int r = 0;
    int added = 0;
    bool sent = false;

    ZeroMemory(&hints, sizeof(hints));
    ZeroMemory(buf, sizeof(buf));
    ZeroMemory(narrowAddr, sizeof(narrowAddr));
    ZeroMemory(portStr, sizeof(portStr));

    if (!result)
        return 0;

    game = result->game;
    masterIndex = result->masterIndex;

    if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 || myGen != Query_GetGeneration())
        goto cleanup;

    addr = Data_GetMasterAddress(game, masterIndex);
    desc = Game_GetDescriptor(game);
    if (!addr || !desc)
        goto cleanup;

    master = Data_GetMasterInternet(game, masterIndex);
    if (!master)
        goto cleanup;

    Data_Lock();
    master->count = 0;
    master->rawCount = 0;
    Data_Unlock();

    if (desc->QueryMasterServer)
    {
        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 || myGen != Query_GetGeneration())
            goto cleanup;

        added = desc->QueryMasterServer(game, masterIndex, addr);

        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 || myGen != Query_GetGeneration())
            goto cleanup;

        Data_Lock();
        master->rawCount = added;
        result->serverCount = master->count;
        Data_Unlock();

        if (myGen == Query_GetGeneration() &&
            InterlockedCompareExchange(&g_queryStop, 0, 0) == 0)
        {
            Net_NotifyMasterDone(game, masterIndex, result->serverCount);
        }

        goto cleanup;
    }

    if (!desc->masterQueryString)
        goto cleanup;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    WideCharToMultiByte(CP_ACP, 0, addr->address, -1, narrowAddr, (int)sizeof(narrowAddr), NULL, NULL);
    sprintf_s(portStr, sizeof(portStr), "%d", addr->port);

    if (getaddrinfo(narrowAddr, portStr, &hints, &addrResult) != 0 || !addrResult)
        goto cleanup;

    sock = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
    if (sock == INVALID_SOCKET)
        goto cleanup;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&normalTimeout, sizeof(normalTimeout));

    while (attempts > 0)
    {
        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 || myGen != Query_GetGeneration())
            goto cleanup;

        if (sendto(sock,
            desc->masterQueryString,
            (int)strlen(desc->masterQueryString),
            0,
            addrResult->ai_addr,
            (int)addrResult->ai_addrlen) != SOCKET_ERROR)
        {
            sent = true;
            break;
        }

        attempts--;
        Sleep(250);

        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 || myGen != Query_GetGeneration())
            goto cleanup;
    }

    if (!sent)
        goto cleanup;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&firstTimeout, sizeof(firstTimeout));

    for (;;)
    {
        if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0 || myGen != Query_GetGeneration())
            break;

        r = recvfrom(sock, (char*)buf, (int)sizeof(buf), 0, NULL, NULL);

        if (r == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAETIMEDOUT)
                break;
            break;
        }

        if (r <= 0)
            break;

        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&normalTimeout, sizeof(normalTimeout));
        ParseMasterPacket(buf, r, game, masterIndex);

        if (master->count >= master->cap)
            break;
    }

    Data_Lock();
    result->serverCount = master->count;
    master->rawCount = master->count;
    Data_Unlock();

    if (myGen == Query_GetGeneration() &&
        InterlockedCompareExchange(&g_queryStop, 0, 0) == 0)
    {
        Net_NotifyMasterDone(game, masterIndex, result->serverCount);
    }

cleanup:
    if (sock != INVALID_SOCKET)
        closesocket(sock);

    if (addrResult)
        freeaddrinfo(addrResult);

    if (result)
        free(result);

    InterlockedDecrement(&g_masterQueriesInFlight);
    Net_TryNotifyAllDone();

    return 0;
}

void Query_StartMaster(GameId game, int masterIndex)
{
    MasterQueryResult* result = (MasterQueryResult*)malloc(sizeof(MasterQueryResult));
    if (!result)
        return;

    result->game = game;
    result->masterIndex = masterIndex;
    result->serverCount = 0;

    const LameMasterAddress* addr = Data_GetMasterAddress(game, masterIndex);
    if (!addr)
    {
        free(result);
        return;
    }

    // Starting a fresh master-query batch should clear any old cancel state.
    InterlockedExchange(&g_queryStop, 0);

    // Unreachable code
    /*if (InterlockedCompareExchange(&g_queryStop, 0, 0) != 0)
    {
        free(result);
        return;
    }*/

    InterlockedIncrement(&g_masterQueriesInFlight);

    HANDLE hThread = CreateThread(NULL, 0, Query_MasterThread, result, 0, NULL);
    if (hThread)
    {
        CloseHandle(hThread);
    }
    else
    {
        InterlockedDecrement(&g_masterQueriesInFlight);
        free(result);

        Net_TryNotifyAllDone();
    }
}

// -----------------------------------------------------------------------------
// LameServer interaction
// -----------------------------------------------------------------------------

void SendSessionEvent(const char* event, const wchar_t* username, const wchar_t* server_ip, int server_port, const char* game_tag)
{
    char portStr[16];
    sprintf_s(portStr, sizeof(portStr), "%d", Net_GetLameServerPort());

    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo* result = NULL;
    if (getaddrinfo(Net_GetLameServerHost(), portStr, &hints, &result) != 0 || !result)
    {
        printf("DEBUG: Failed to resolve Lame server host\n");
        return;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        return;
    }

    char user_utf8[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, username, -1, user_utf8, sizeof(user_utf8), NULL, NULL);

    char ip_utf8[64] = {};
    if (server_ip && server_ip[0])
        WideCharToMultiByte(CP_UTF8, 0, server_ip, -1, ip_utf8, sizeof(ip_utf8), NULL, NULL);

    char msg[256];
    // Notice we added an extra \t%s at the end for the game tag
    snprintf(msg, sizeof(msg), "LOG\t%s\t%s\t%s\t%d\t%s\t%s\n",
        event, user_utf8, ip_utf8, server_port, LAMESPY_VERSION, game_tag ? game_tag : "");

    printf("DEBUG: Sending session log: %s", msg);

    sendto(sock, msg, (int)strlen(msg), 0, result->ai_addr, (int)result->ai_addrlen);
    closesocket(sock);
    freeaddrinfo(result);
}