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

//Add color code stripping function
static void StripQ3Colors(char* text)
{
    char* src = text;
    char* dst = text;

    while (*src)
    {
        if (*src == '^' && src[1])
        {
            src += 2;  // Skip color code (^0, ^1, ^2, etc.)
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void Q3_ParseServerInfo(const char* data, int dataLen, LameServer* outServer)
{
    const char* p = data;
    char key[128], value[256];

    if (!data || !outServer || dataLen < 20)
        return;

    // Clear counts
    outServer->ruleCount = 0;
    outServer->playerCount = 0;

    // Skip 0xFFFFFFFF header
    if (dataLen >= 4 && (unsigned char)p[0] == 0xFF)
        p += 4;

    // Skip "statusResponse\n"
    if (!strncmp(p, "statusResponse\n", 15))
        p += 15;
    else
        return;

    // Must start with '\'
    if (*p != '\\')
        return;

    p++;

    // Parse key-value pairs
    while (*p && (p - data) < dataLen)
    {
        int ki = 0, vi = 0;

        // Read key
        while (*p && *p != '\\' && ki < (int)sizeof(key) - 1)
            key[ki++] = *p++;
        key[ki] = '\0';

        if (*p != '\\')
            break;
        p++;

        // Read value
        while (*p && *p != '\\' && *p != '\n' && vi < (int)sizeof(value) - 1)
            value[vi++] = *p++;
        value[vi] = '\0';

        if (*p == '\\')
            p++;

        // Strip color codes from hostname
        if (!_stricmp(key, "sv_hostname") || !_stricmp(key, "hostname"))
            StripQ3Colors(value);

        // Convert to wide
        wchar_t keyW[128], valueW[256];
        MultiByteToWideChar(CP_UTF8, 0, key, -1, keyW, _countof(keyW));
        MultiByteToWideChar(CP_UTF8, 0, value, -1, valueW, _countof(valueW));

        // Extract important fields
        if (!_stricmp(key, "sv_hostname") || !_stricmp(key, "hostname"))
            wcsncpy_s(outServer->name, _countof(outServer->name), valueW, _TRUNCATE);
        else if (!_stricmp(key, "mapname"))
            wcsncpy_s(outServer->map, _countof(outServer->map), valueW, _TRUNCATE);
        else if (!_stricmp(key, "sv_maxclients"))
            outServer->maxPlayers = _wtoi(valueW);
        else if (!_stricmp(key, "g_gametype"))
        {
            int gt = _wtoi(valueW);
            const wchar_t* gtName = L"Unknown";
            if (gt == 0) gtName = L"FFA";
            else if (gt == 1) gtName = L"Tournament";
            else if (gt == 3) gtName = L"Team DM";
            else if (gt == 4) gtName = L"CTF";
            wcsncpy_s(outServer->gametype, _countof(outServer->gametype), gtName, _TRUNCATE);
        }

        // Store as rule
        if (outServer->ruleCount < LAME_MAX_RULES)
        {
            wcsncpy_s(outServer->ruleList[outServer->ruleCount].key,
                _countof(outServer->ruleList[0].key), keyW, _TRUNCATE);
            wcsncpy_s(outServer->ruleList[outServer->ruleCount].value,
                _countof(outServer->ruleList[0].value), valueW, _TRUNCATE);
            outServer->ruleCount++;
        }
    }

    // Find start of player list
    p = strchr(data, '\n');
    if (!p)
        return;

    // Skip server info block
    while (*p)
    {
        if (*p == '\n' && *(p + 1) != '\\')
        {
            p++;
            break;
        }
        p++;
    }

    // Parse players AND COUNT THEM
    int playerCount = 0;  //ADD THIS
    while (*p && (p - data) < dataLen && outServer->playerCount < LAME_MAX_PLAYERS)
    {
        int score, ping;
        char name[64];

        if (sscanf_s(p, "%d %d \"%63[^\"]\"", &score, &ping, name, (unsigned)_countof(name)) == 3)
        {
            // Strip color codes from player name
            StripQ3Colors(name);

            MultiByteToWideChar(CP_UTF8, 0, name, -1,
                outServer->playerList[outServer->playerCount].name,
                _countof(outServer->playerList[0].name));
            outServer->playerList[outServer->playerCount].score = score;
            outServer->playerList[outServer->playerCount].ping = ping;
            outServer->playerCount++;
            playerCount++;  //INCREMENT
        }

        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }

    //SET THE PLAYER COUNT FIELD
    outServer->players = playerCount;
}

static int Q3_QueryServer(const wchar_t* ip, int port, LameServer* outServer)
{
    SOCKET sock;
    struct sockaddr_in addr;
    char sendBuf[256];
    char recvBuf[4096];
    int result;
    DWORD t0, t1;

    char narrowIP[64];
    WideCharToMultiByte(CP_ACP, 0, ip, -1, narrowIP, sizeof(narrowIP), NULL, NULL);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    DWORD timeout = 1200;  // was 3500
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    memset(sendBuf, 0xFF, 4);
    strcpy_s(sendBuf + 4, sizeof(sendBuf) - 4, "getstatus");

    addrinfo hints;
    addrinfo* ai = NULL;
    int gaiResult = 0;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    gaiResult = getaddrinfo(narrowIP, NULL, &hints, &ai);
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

    //t0 = GetTickCount();

    // RETRY LOGIC: Try up to 3 times
    int attempts = 0;
    int maxAttempts = 3;
    Sleep(60);

    while (attempts < maxAttempts)
    {
        // ADD THIS LINE: start timing THIS attempt, immediately before sendto()
        t0 = GetTickCount();

        result = sendto(sock, sendBuf, 13, 0, (struct sockaddr*)&addr, sizeof(addr));
        if (result == SOCKET_ERROR)
        {
            attempts++;
            if (attempts < maxAttempts)
            {
                Sleep(100);
                continue;
            }
            closesocket(sock);
            return 0;
        }

        result = recvfrom(sock, recvBuf, sizeof(recvBuf) - 1, 0, NULL, NULL);

        if (result > 0)
        {
            // THIS is your “response handler” location.
            t1 = GetTickCount();
            recvBuf[result] = '\0';

            // Keep this here, but now it uses per-attempt t0 (correct)
            outServer->ping = (int)(t1 - t0);

            Q3_ParseServerInfo(recvBuf, result, outServer);

            closesocket(sock);
            return 1;
        }

        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT)
        {
            attempts++;
            if (attempts < maxAttempts)
                continue;
        }

        break;
    }

    closesocket(sock);
    //outServer->ping = 999; // or -1 if you prefer “no ping”
    return 0;
}

void Q3_RegisterGame(void)
{
    LameGameDescriptor desc = {};

    desc.id = GAME_Q3;
    desc.name = L"Quake 3 Arena";

    desc.masterQueryString = "\xFF\xFF\xFF\xFFgetservers 68 empty full";
    
    desc.shortName = L"Q3";
    desc.configFile = L"q3masters.cfg";
    desc.defaultMasterPort = 27950;
    desc.defaultServerPort = 27960;
    //desc.masterProtocol = MASTER_PROTO_Q3;
    desc.QueryGameServer = Q3_QueryServer;
    desc.ParseServerInfo = Q3_ParseServerInfo;
    desc.ParsePlayerInfo = NULL;

    Game_Register(GAME_Q3, &desc);
}