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

static void QW_LogResponse(const char* ip, int port, const char* data, int dataLen)
{
    FILE* f = NULL;
    fopen_s(&f, "qw_responses.txt", "a");
    if (!f)
        return;

    fprintf(f, "\n========================================\n");
    fprintf(f, "Server: %s:%d\n", ip, port);
    fprintf(f, "Length: %d bytes\n", dataLen);
    fprintf(f, "========================================\n");
    fwrite(data, 1, dataLen, f);
    fprintf(f, "\n");

    fclose(f);
}

static void QW_ParseServerInfo(const char* data, int dataLen, LameServer* outServer)
{
    const char* p = data;
    const char* rulesStart;
    const char* rulesEnd;
    char key[64], value[256];

    if (!data || !outServer || dataLen < 10)
        return;

    // Clear counts
    outServer->ruleCount = 0;
    outServer->playerCount = 0;

    //int sawHostname = 0;   // <-- ADD (place right here)

    // Skip 0xFFFFFFFF header
    if (dataLen >= 4 && (unsigned char)p[0] == 0xFF)
    {
        while ((unsigned char)*p == 0xFF && (p - data) < dataLen)
            p++;
    }

    // Skip 'n' if followed by '\'
    if (p[0] == 'n' && p[1] == '\\')
        p++;

    rulesStart = p;

    // Find end of rules line (start of players)
    rulesEnd = strchr(rulesStart, '\n');
    if (!rulesEnd)
        rulesEnd = strchr(rulesStart, '\r');

    if (!rulesEnd)
        return;

    // Parse \key\val pairs in rules line
    p = rulesStart;

    int isQTV = 0;      // Flag for filtering
    int isQWFWD = 0;

    while (*p && p < rulesEnd)
    {
        int i;

        if (*p != '\\')
            break;
        p++;

        // Read key
        key[0] = 0;
        i = 0;
        while (*p && *p != '\\' && *p != '\n' && *p != '\r' && i < 63)
            key[i++] = *p++;
        key[i] = 0;

        if (*p != '\\')
            break;
        p++;

        // Read value
        value[0] = 0;
        i = 0;
        while (*p && *p != '\\' && *p != '\n' && *p != '\r' && i < 255)
            value[i++] = *p++;
        value[i] = 0;

        // Convert to wide
        wchar_t keyW[128], valueW[256];
        MultiByteToWideChar(CP_ACP, 0, key, -1, keyW, _countof(keyW));
        MultiByteToWideChar(CP_ACP, 0, value, -1, valueW, _countof(valueW));

        // CHECK FOR QTV/QWFWD
        if (_strnicmp(value, "QTV", 3) == 0 ||
            _strnicmp(value, "Qtv", 3) == 0 ||
            _strnicmp(value, "QTVGO", 5) == 0)
        {
            outServer->state = QUERY_FAILED;
            return;  // Stop parsing this server
        }

        // Extract important fields
        if (!_stricmp(key, "hostname"))
        {
            wcsncpy_s(outServer->name, _countof(outServer->name), valueW, _TRUNCATE);
            //sawHostname = 1;   // <-- ADD
        }
        else if (!_stricmp(key, "map") || !_stricmp(key, "mapname"))
            wcsncpy_s(outServer->map, _countof(outServer->map), valueW, _TRUNCATE);
        else if (!_stricmp(key, "maxclients") || !_stricmp(key, "sv_maxclients"))
            outServer->maxPlayers = _wtoi(valueW);
        else if (!_stricmp(key, "deathmatch"))
        {
            int dm = _wtoi(valueW);
            if (dm == 1) wcscpy_s(outServer->gametype, _countof(outServer->gametype), L"DM");
            else if (dm == 3) wcscpy_s(outServer->gametype, _countof(outServer->gametype), L"TeamDM");
        }
        else if (!_stricmp(key, "mode"))
        {
            if (!_stricmp(value, "ffa"))
                wcscpy_s(outServer->gametype, _countof(outServer->gametype), L"FFA");
            else if (!_stricmp(value, "2on2") || !_stricmp(value, "4on4"))
                wcsncpy_s(outServer->gametype, _countof(outServer->gametype), valueW, _TRUNCATE);
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

	// Helps filter QTV/qwfwd servers that omit *version but include it in hostname
    /*if (!sawHostname)
    {
        // Fallback: keep it valid, show IP as name
        wcsncpy_s(outServer->name, _countof(outServer->name), outServer->ip, _TRUNCATE);
    }*/

    // Parse players (rest of existing code)
    p = rulesEnd;
    while (*p == '\r' || *p == '\n')
        p++;

    int playerCount = 0;
    while (*p && (p - data) < dataLen && outServer->playerCount < LAME_MAX_PLAYERS)
    {
        int userid, score, ping, time;
        char name[64] = { 0 };
        char skin[32] = { 0 };
        int topcolor, bottomcolor;
        int consumed = 0;

        if (sscanf_s(p, "%d %d %d %d \"%63[^\"]\" \"%31[^\"]\" %d %d%n",
            &userid, &score, &ping, &time,
            name, (unsigned)_countof(name),
            skin, (unsigned)_countof(skin),
            &topcolor, &bottomcolor, &consumed) >= 4 && consumed > 0)
        {
            MultiByteToWideChar(CP_ACP, 0, name, -1,
                outServer->playerList[outServer->playerCount].name,
                _countof(outServer->playerList[0].name));
            outServer->playerList[outServer->playerCount].score = score;
            outServer->playerList[outServer->playerCount].ping = ping;
            outServer->playerCount++;
            playerCount++;
            p += consumed;
        }

        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }

    outServer->players = playerCount;
}

static int QW_QueryServer(const wchar_t* ip, int port, LameServer* outServer)
{
    SOCKET sock;
    struct sockaddr_in addr;
    char sendBuf[256];
    char recvBuf[16384];
    int result;
    DWORD t0, t1;
    addrinfo hints;
    addrinfo* ai;
    int gaiResult;

    char narrowIP[256];
    WideCharToMultiByte(CP_ACP, 0, ip, -1, narrowIP, (int)sizeof(narrowIP), NULL, NULL);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    DWORD timeout = 3500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    // QW query: "\xff\xff\xff\xffstatus\n"
    memset(sendBuf, 0xFF, 4);
    strcpy_s(sendBuf + 4, sizeof(sendBuf) - 4, "status\n");

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    ai = NULL;
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

    int attempts = 0;
    int maxAttempts = 2;

    while (attempts < maxAttempts)
    {
        t0 = GetTickCount();

        result = sendto(sock, sendBuf, 11, 0, (struct sockaddr*)&addr, sizeof(addr));
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

        // Receive all packets (QW may send multiple)
        int total = 0;
        int gotAny = 0;
        DWORD tLast = GetTickCount();

        for (;;)
        {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            struct timeval tv = { 0, 50000 };
            int sel = select(0, &rfds, NULL, NULL, &tv);

            if (sel == SOCKET_ERROR)
                break;

            if (sel == 0)
            {
                // Hard stop if we’ve waited too long overall (no packets at all)
                if ((GetTickCount() - t0) > 1500)
                    break;

                // If we already got something, stop shortly after the last packet
                if (gotAny && (GetTickCount() - tLast) > 150)
                    break;

                continue;
            }

            if (FD_ISSET(sock, &rfds))
            {
                char pkt[2048];
                int r = recv(sock, pkt, sizeof(pkt), 0);
                if (r <= 0)
                    continue;

                gotAny = 1;
                tLast = GetTickCount();

                // Strip per-packet headers
                int i = 0;
                while (i < r && pkt[i] == 0xFF)
                    i++;
                if (i + 2 <= r && pkt[i] == 'n' && pkt[i + 1] == '\\')
                    i++;

                int payload = r - i;
                if (total + payload < (int)sizeof(recvBuf) - 1)
                {
                    memcpy(recvBuf + total, pkt + i, payload);
                    total += payload;
                }

                if ((GetTickCount() - t0) > 1500)
                    break;
            }
        }

        if (total > 0)
        {
            t1 = GetTickCount();
            recvBuf[total] = '\0';

            // Log the response (debug only)
            //QW_LogResponse(narrowIP, port, recvBuf, total);

            outServer->ping = (int)(t1 - t0);
            QW_ParseServerInfo(recvBuf, total, outServer);

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
    return 0;
}


static int QW_ParseMasterPacket(const unsigned char* data, int length, LameMaster* master, GameId game)
{
    int added = 0;
    const unsigned char* p = data;
    int remaining = length;

    if (!data || !master || length <= 0)
        return 0;

    if (remaining < 6)
        return 0;

    // Require 0xFFFFFFFF header + "d\n"
    if (p[0] != 0xFF || p[1] != 0xFF || p[2] != 0xFF || p[3] != 0xFF)
        return 0;
    if (p[4] != 'd')
        return 0;
    if (p[5] != '\n' && p[5] != 0)
        return 0;

    p += 6;
    remaining -= 6;

    // Parse 6-byte entries: ip[4] + port[2] (big-endian)
    while (remaining >= 6)
    {
        unsigned int port = ((unsigned int)p[4] << 8) | (unsigned int)p[5];

        // Optional end marker
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0 && port == 0)
            break;

        if (port == 0 || port > 65535)
        {
            p += 6;
            remaining -= 6;
            continue;
        }

        if (master->count < master->cap)
        {
            wchar_t ip[64];
            swprintf_s(ip, _countof(ip), L"%u.%u.%u.%u",
                (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);

            int duplicate = 0;
            for (int j = 0; j < master->count; j++)
            {
                if (master->servers[j]->port == (int)port &&
                    wcscmp(master->servers[j]->ip, ip) == 0)
                {
                    duplicate = 1;
                    break;
                }
            }

            if (!duplicate)
            {
                LameServer* s = master->servers[master->count];
                ZeroMemory(s, sizeof(*s));

                s->game = game;
                wcsncpy_s(s->ip, _countof(s->ip), ip, _TRUNCATE);
                s->port = (int)port;
                s->ping = 999;
                s->state = QUERY_IDLE;

                wcscpy_s(s->name, _countof(s->name), ip);
                wcscpy_s(s->map, _countof(s->map), L" ");
                wcscpy_s(s->gametype, _countof(s->gametype), L" ");

                master->count++;
                added++;
            }
        }

        p += 6;
        remaining -= 6;
    }

    return added;
}

void QW_RegisterGame(void)
{
    LameGameDescriptor desc = {};

    desc.id = GAME_QW;
    desc.name = L"Quake";
    desc.masterQueryString = "c\n";
    //desc.masterQueryString = "\xff\xff\xff\xffgetstatus\n";
    desc.shortName = L"QW";
    desc.configFile = L"qwmasters.cfg";
    desc.defaultMasterPort = 27000;
    desc.defaultServerPort = 27500;
    desc.QueryGameServer = QW_QueryServer;
    desc.ParseServerInfo = QW_ParseServerInfo;
    desc.ParseMasterPacket = QW_ParseMasterPacket;
    desc.ParsePlayerInfo = NULL;
    
    Game_Register(GAME_QW, &desc);
}