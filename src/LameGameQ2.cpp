#include "LameCore.h"
#include "LameData.h"
#include "LameGame.h"
#include <stdio.h>

#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

// 
// Master server querying
// 

// Q2 master query (dedicated, Q3-style transport)
#define Q2_MASTER_QUERY      "\xff\xff\xff\xffstatus"
#define Q2_MASTER_TIMEOUT_MS 5000

static int Q2_ParseMasterPacket(const unsigned char* data, int dataLen, LameMaster* master, GameId game);

static void Q2_LogMasterResponse(GameId game, int masterIndex, const LameMasterAddress* addr,
    const unsigned char* buf, int len)
{
    FILE* f = NULL;
    fopen_s(&f, "master_responses.txt", "a");
    if (!f)
        return;

    fprintf(f, "\n========================================\n");
    fprintf(f, "Game: %S (ID: %d)\n", Game_PrefixW(game), game);
    if (addr)
        fprintf(f, "Master: %S:%d (Index: %d)\n", addr->address, addr->port, masterIndex);
    else
        fprintf(f, "Master: (unknown) Index: %d\n", masterIndex);
    fprintf(f, "Length: %d bytes\n", len);
    fprintf(f, "========================================\n");

    fprintf(f, "Hex dump:\n");
    for (int i = 0; i < len; i++)
    {
        if (i > 0 && (i % 16) == 0)
            fprintf(f, "\n");
        fprintf(f, "%02X ", (unsigned)buf[i]);
    }
    fprintf(f, "\n\n");

    fprintf(f, "ASCII (printable):\n");
    for (int i = 0; i < len; i++)
    {
        if (buf[i] >= 32 && buf[i] < 127)
            fputc((int)buf[i], f);
        else
            fputc('.', f);
    }
    fprintf(f, "\n\n");

    fclose(f);
}

static int Q2_QueryMaster(GameId game, int masterIndex, const LameMasterAddress* masterAddr)
{
    if (!masterAddr)
        return 0;

    const LameGameDescriptor* desc = Game_GetDescriptor(game);
    if (!desc)
        return 0;

    LameMaster* master = Data_GetMasterInternet(game, masterIndex);
    if (!master)
        return 0;

    SOCKET sock = INVALID_SOCKET;
    struct addrinfo hints;
    struct addrinfo* addrResult = NULL;

    char narrowAddr[256] = {};
    char portStr[16] = {};

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    WideCharToMultiByte(CP_ACP, 0, masterAddr->address, -1,
        narrowAddr, (int)sizeof(narrowAddr), NULL, NULL);

    sprintf_s(portStr, sizeof(portStr), "%d", masterAddr->port);

    if (getaddrinfo(narrowAddr, portStr, &hints, &addrResult) != 0 || !addrResult)
        return 0;

    sock = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        freeaddrinfo(addrResult);
        return 0;
    }

    DWORD timeout = Q2_MASTER_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // Send query (retry a few times like Q3)
    int sent = 0;
    int attempts = 3;

    while (attempts-- > 0)
    {
        const int qlen = (int)strlen(Q2_MASTER_QUERY);

        if (sendto(sock, Q2_MASTER_QUERY, qlen, 0,
            addrResult->ai_addr, (int)addrResult->ai_addrlen) != SOCKET_ERROR)
        {
            sent = 1;
            break;
        }

        Sleep(250);
    }

    if (!sent)
    {
        closesocket(sock);
        freeaddrinfo(addrResult);
        return 0;
    }

    int totalAdded = 0;

    // Receive packets until timeout
    for (;;)
    {
        unsigned char buf[4096];
        int r = recvfrom(sock, (char*)buf, (int)sizeof(buf), 0, NULL, NULL);

        if (r == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT)
                break;
            break;
        }

        if (r <= 0)
            break;

        Q2_LogMasterResponse(game, masterIndex, masterAddr, buf, r);

        Data_Lock();
        totalAdded += Q2_ParseMasterPacket(buf, r, master, game);
        Data_Unlock();

        if (master->count >= master->cap)
            break;
    }

    closesocket(sock);
    freeaddrinfo(addrResult);
    return totalAdded;
}


//
// Game server querying
//

static int Q2_IsOOBHeader(const char* p)
{
    const unsigned char* b = (const unsigned char*)p;
    return (b && b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF && b[3] == 0xFF);
}

static void Q2_StripCaretColors(char* s)
{
    // Safe to do even if Q2 doesn’t use them; keeps behavior consistent with Q3 module.
    if (!s) return;
    char* src = s;
    char* dst = s;
    while (*src)
    {
        if (*src == '^' && src[1])
        {
            src += 2;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = 0;
}

static void Q2_AddRule(LameServer* outServer, const char* keyA, const char* valA)
{
    if (!outServer || !keyA) return;
    if (outServer->ruleCount >= LAME_MAX_RULES) return;

    wchar_t keyW[128] = {};
    wchar_t valW[256] = {};

    MultiByteToWideChar(CP_UTF8, 0, keyA, -1, keyW, _countof(keyW));
    MultiByteToWideChar(CP_UTF8, 0, (valA ? valA : ""), -1, valW, _countof(valW));

    wcsncpy_s(outServer->ruleList[outServer->ruleCount].key,
        _countof(outServer->ruleList[0].key), keyW, _TRUNCATE);

    wcsncpy_s(outServer->ruleList[outServer->ruleCount].value,
        _countof(outServer->ruleList[0].value), valW, _TRUNCATE);

    outServer->ruleCount++;
}

static int Q2_ParsePlayers(const char* data, int dataLen, LameServer* outServer)
{
    if (!data || dataLen <= 0 || !outServer)
        return 0;

    const char* p = data;

    if (Q2_IsOOBHeader(p))
        p += 4;

    if (!strncmp(p, "print\n", 6))
        p += 6;

    // players start after the first newline (end of info line)
    p = strchr(p, '\n');
    if (!p)
        return 0;
    p++;

    int count = 0;

    while (*p && (p - data) < dataLen && count < LAME_MAX_PLAYERS)
    {
        int score = 0, ping = 0;
        char name[128] = {};

        // Q2 format: <score> <ping> "<name>"
        if (sscanf_s(p, "%d %d \"%127[^\"]\"", &score, &ping, name, (unsigned)_countof(name)) == 3)
        {
            Q2_StripCaretColors(name);

            MultiByteToWideChar(CP_UTF8, 0, name, -1,
                outServer->playerList[count].name,
                _countof(outServer->playerList[0].name));

            outServer->playerList[count].score = score;
            outServer->playerList[count].ping = ping;
            count++;
        }

        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }

    outServer->playerCount = count;
    outServer->players = count;
    return count;
}

static void Q2_ParseServerInfo(const char* data, int dataLen, LameServer* outServer)
{
    if (!data || !outServer || dataLen <= 0)
        return;

    // reset dynamic sections
    outServer->ruleCount = 0;
    outServer->playerCount = 0;
    outServer->players = 0;

    const char* p = data;

    if (Q2_IsOOBHeader(p))
        p += 4;

    // Q2 status reply usually begins with "print\n"
    if (!strncmp(p, "print\n", 6))
        p += 6;

    // info string must start with '\'
    if (*p != '\\')
        return;

    p++; // skip first '\'

    char key[128];
    char val[256];

    int deathmatch = 0;
    int maxclients = 0;

    while (*p && (p - data) < dataLen)
    {
        int ki = 0, vi = 0;

        while (*p && *p != '\\' && *p != '\n' && ki < (int)sizeof(key) - 1)
            key[ki++] = *p++;
        key[ki] = 0;

        if (*p != '\\')
            break;
        p++;

        while (*p && *p != '\\' && *p != '\n' && vi < (int)sizeof(val) - 1)
            val[vi++] = *p++;
        val[vi] = 0;

        if (*p == '\\')
            p++;

        if (!key[0])
            break;

        // store rule (keep everything)
        Q2_AddRule(outServer, key, val);

        // extract common fields into generic columns
        if (!_stricmp(key, "hostname"))
        {
            Q2_StripCaretColors(val);
            MultiByteToWideChar(CP_UTF8, 0, val, -1, outServer->name, _countof(outServer->name));
        }
        else if (!_stricmp(key, "mapname"))
        {
            MultiByteToWideChar(CP_UTF8, 0, val, -1, outServer->map, _countof(outServer->map));
        }
        else if (!_stricmp(key, "maxclients"))
        {
            maxclients = atoi(val);
            outServer->maxPlayers = maxclients;
        }
        else if (!_stricmp(key, "deathmatch"))
        {
            deathmatch = atoi(val);
        }
        else if (!_stricmp(key, "gamename"))
        {
            // Handy for display: if gametype is empty, show gamename
            if (!outServer->gametype[0])
                MultiByteToWideChar(CP_UTF8, 0, val, -1, outServer->gametype, _countof(outServer->gametype));
        }
    }

    // If we didn’t set a gametype, use a simple, consistent one
    if (!outServer->gametype[0])
    {
        if (deathmatch)
            wcscpy_s(outServer->gametype, _countof(outServer->gametype), L"Deathmatch");
    }

    // Parse players from the same packet
    Q2_ParsePlayers(data, dataLen, outServer);

    // If name wasn’t provided, fall back to IP (useful for weird servers)
    if (!outServer->name[0])
        wcsncpy_s(outServer->name, _countof(outServer->name), outServer->ip, _TRUNCATE);

    // If maxclients wasn’t provided, keep it from whatever’s already there (or 0)
    if (outServer->maxPlayers <= 0 && maxclients > 0)
        outServer->maxPlayers = maxclients;
}

static int Q2_QueryServer(const wchar_t* ip, int port, LameServer* outServer)
{
    if (!ip || !ip[0] || port <= 0 || port > 65535 || !outServer)
        return 0;

    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in addr;
    char recvBuf[8192];
    int r = 0;

    char ipA[256] = {};
    addrinfo hints;
    addrinfo* ai = NULL;
    int gaiResult = 0;

    WideCharToMultiByte(CP_ACP, 0, ip, -1, ipA, (int)sizeof(ipA), NULL, NULL);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    DWORD timeout = 3500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    gaiResult = getaddrinfo(ipA, NULL, &hints, &ai);
    if (gaiResult != 0 || !ai || !ai->ai_addr)
    {
        if (ai)
            freeaddrinfo(ai);
        closesocket(sock);
        return 0;
    }

    ZeroMemory(&addr, sizeof(addr));
    addr = *(const sockaddr_in*)ai->ai_addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);

    freeaddrinfo(ai);

    // Q2 status query
    const char* req = "\xFF\xFF\xFF\xFF""status";
    const int reqLen = 10; // 4 FF + "status"(6)

    int attempts = 0;
    const int maxAttempts = 2;

    while (attempts < maxAttempts)
    {
        DWORD t0 = GetTickCount();

        int s = sendto(sock, req, reqLen, 0, (struct sockaddr*)&addr, sizeof(addr));
        if (s == SOCKET_ERROR)
        {
            attempts++;
            Sleep(100);
            continue;
        }

        r = recvfrom(sock, recvBuf, (int)sizeof(recvBuf) - 1, 0, NULL, NULL);
        if (r > 0)
        {
            DWORD t1 = GetTickCount();
            recvBuf[r] = 0;

            outServer->ping = (int)(t1 - t0);

            Q2_ParseServerInfo(recvBuf, r, outServer);

            closesocket(sock);
            return 1;
        }

        if (WSAGetLastError() == WSAETIMEDOUT)
        {
            attempts++;
            continue;
        }

        break;
    }

    closesocket(sock);
    return 0;
}

static int Q2_ParseMasterPacket(const unsigned char* data, int dataLen, LameMaster* master, GameId game)
{
    if (!data || dataLen <= 0 || !master)
        return 0;

    const unsigned char* p = data;
    const unsigned char* end = data + dataLen;

    // skip leading 0xFFs
    while (p < end && *p == 0xFF)
        p++;

    // skip header text until '\'
    while (p < end && *p != '\\')
        p++;

    int added = 0;

    while (p < end)
    {
        // termination: "\EOT"
        if ((end - p) >= 4 && p[0] == '\\' && p[1] == 'E' && p[2] == 'O' && p[3] == 'T')
            break;

        if (*p != '\\')
        {
            p++;
            continue;
        }

        // need '\' + 4 ip bytes + 2 port bytes
        if ((end - p) < 7)
            break;

        int port = ((int)p[5] << 8) | (int)p[6];
        if (port > 0 && port <= 65535)
        {
            wchar_t ipW[64];
            swprintf_s(ipW, _countof(ipW), L"%u.%u.%u.%u",
                (unsigned)p[1], (unsigned)p[2], (unsigned)p[3], (unsigned)p[4]);

            int dup = 0;
            for (int j = 0; j < master->count; j++)
            {
                if (master->servers[j]->port == port &&
                    wcscmp(master->servers[j]->ip, ipW) == 0)
                {
                    dup = 1;
                    break;
                }
            }

            if (!dup && master->count < master->cap)
            {
                LameServer* s = master->servers[master->count];

                ZeroMemory(s, sizeof(*s));
                s->game = game;
                wcsncpy_s(s->ip, _countof(s->ip), ipW, _TRUNCATE);
                s->port = port;
                s->ping = 999;
                s->state = QUERY_IDLE;
                s->gen = 0;

                // placeholders so the UI doesn’t look empty pre-query
                wcsncpy_s(s->name, _countof(s->name), ipW, _TRUNCATE);
                wcscpy_s(s->map, _countof(s->map), L" ");
                wcscpy_s(s->gametype, _countof(s->gametype), L" ");

                master->count++;
                added++;
            }
        }

        p += 7;
    }

    return added;
}

void Q2_RegisterGame(void)
{
    LameGameDescriptor desc = {};

    desc.id = GAME_Q2;
    desc.name = L"Quake 2";

    desc.masterQueryString = NULL; // Q2 uses dedicated QueryMasterServer()
    desc.QueryMasterServer = Q2_QueryMaster;

    desc.shortName = L"Q2";
    desc.configFile = L"q2masters.cfg";

    desc.defaultMasterPort = 27900;
    desc.defaultServerPort = 27910;

    desc.QueryGameServer = Q2_QueryServer;
    desc.ParseServerInfo = Q2_ParseServerInfo;
    desc.ParseMasterPacket = Q2_ParseMasterPacket;

    Game_Register(GAME_Q2, &desc);
}