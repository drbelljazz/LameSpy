#include "LameCore.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameGameUE.h"
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <string>

#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static void UE_AddRule(LameServer* outServer, const char* keyA, const char* valA);

// ------------------------------------------------------------
// Shared Unreal (UT/UG/DX) helpers
// ------------------------------------------------------------

static void UE_LogMasterJsonResponse(GameId game, int masterIndex, const wchar_t* host, int port, const wchar_t* path, const char* json, int len)
{
    FILE* f = NULL;
    fopen_s(&f, "master_responses.txt", "a");
    if (!f) return;

    fprintf(f, "\n========================================\n");
    fprintf(f, "Game: %S (ID: %d)\n", Game_PrefixW(game), (int)game);
    fprintf(f, "Master: %S:%d (Index: %d)\n", host ? host : L"(null)", port, masterIndex);
    fprintf(f, "Path: %S\n", path ? path : L"(null)");
    fprintf(f, "Length: %d bytes\n", len);
    fprintf(f, "Type: HTTP JSON\n");
    fprintf(f, "========================================\n");

    if (json && len > 0)
        fwrite(json, 1, (size_t)len, f);

    fprintf(f, "\n\n");
    fclose(f);
}

static int UE_WinHttpGet(const wchar_t* host, int port, const wchar_t* path, std::string& outBody)
{
    outBody.clear();

    HINTERNET hSession = WinHttpOpen(L"LameSpyRedux/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession)
        return 0;

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return 0;
    }

    DWORD flags = (port == 443) ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
        L"GET",
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    WinHttpAddRequestHeaders(hRequest, L"Accept: application/json\r\n", -1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);

    if (ok)
        ok = WinHttpReceiveResponse(hRequest, NULL);

    if (!ok)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail))
            break;
        if (avail == 0)
            break;

        size_t oldSize = outBody.size();
        outBody.resize(oldSize + avail);

        DWORD read = 0;
        if (!WinHttpReadData(hRequest, (LPVOID)(outBody.data() + oldSize), avail, &read))
            break;

        if (read == 0)
            break;

        if (read < avail)
            outBody.resize(oldSize + read);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return (int)outBody.size();
}

static int UE_MasterHasServer(const LameMaster* master, const wchar_t* ipW, int port)
{
    if (!master || !ipW || port <= 0) return 0;

    for (int i = 0; i < master->count; i++)
    {
        const LameServer* s = master->servers[i];
        if (!s) continue;
        if (s->port != port) continue;
        if (_wcsicmp(s->ip, ipW) == 0)
            return 1;
    }
    return 0;
}

static int UE_CopyJsonIntFieldRange(const char* start, const char* end, const char* field, int* outVal)
{
    if (!start || !end || start >= end || !field || !field[0] || !outVal)
        return 0;

    char needle[96] = {};
    _snprintf_s(needle, _countof(needle), _TRUNCATE, "\"%s\":", field);

    const char* k = strstr(start, needle);
    if (!k || k >= end)
        return 0;

    k += (int)strlen(needle);
    while (k < end && (*k == ' ' || *k == '\t'))
        k++;

    if (k >= end)
        return 0;

    *outVal = atoi(k);
    return 1;
}

static int UE_CopyJsonStringFieldRange(const char* start, const char* end, const char* key, char* out, int outCap)
{
    if (!start || !end || start >= end || !key || !out || outCap <= 1)
        return 0;

    out[0] = 0;

    char needle[64];
    _snprintf_s(needle, _countof(needle), _TRUNCATE, "\"%s\":\"", key);

    const char* k = strstr(start, needle);
    if (!k || k >= end)
        return 0;

    k += (int)strlen(needle);
    if (k >= end)
        return 0;

    const char* vEnd = strchr(k, '\"');
    if (!vEnd || vEnd > end)
        return 0;

    int n = (int)(vEnd - k);
    if (n <= 0)
        return 0;

    if (n >= outCap)
        n = outCap - 1;

    memcpy(out, k, n);
    out[n] = 0;
    return 1;
}

static int UE_ParseMasterJsonPage(const char* json, int len, LameMaster* master, GameId game)
{
    if (!json || len <= 0 || !master)
        return 0;

    int added = 0;
    const char* p = json;
    const char* end = json + len;

    while (p < end)
    {
        const char* ipKey = strstr(p, "\"ip\":\"");
        if (!ipKey)
            break;
        ipKey += 6; // strlen("\"ip\":\"")

        const char* ipEnd = strchr(ipKey, '\"');
        if (!ipEnd)
            break;

        // Find object bounds for THIS entry: { ... }
        const char* objStart = ipKey;
        while (objStart > json && *objStart != '{')
            objStart--;
        if (objStart < json || *objStart != '{')
        {
            p = ipEnd + 1;
            continue;
        }

        const char* objEnd = strchr(ipEnd, '}');
        if (!objEnd)
            break;

        char ipA[128];
        int ipLen = (int)(ipEnd - ipKey);
        if (ipLen <= 0)
        {
            p = objEnd + 1;
            continue;
        }
        if (ipLen > (int)sizeof(ipA) - 1)
            ipLen = (int)sizeof(ipA) - 1;
        memcpy(ipA, ipKey, ipLen);
        ipA[ipLen] = 0;

        // "::ffff:x.x.x.x" -> "x.x.x.x"
        if (strncmp(ipA, "::ffff:", 7) == 0)
            memmove(ipA, ipA + 7, strlen(ipA + 7) + 1);

        // Find hostport ANYWHERE inside this object (order-independent)
        const char* portKey = strstr(objStart, "\"hostport\":");
        if (!portKey || portKey > objEnd)
        {
            p = objEnd + 1;
            continue;
        }

        portKey += 11; // strlen("\"hostport\":")
        while (portKey < objEnd && (*portKey == ' ' || *portKey == '\t'))
            portKey++;

        int port = atoi(portKey);
        if (port <= 0 || port > 65535)
        {
            p = objEnd + 1;
            continue;
        }

        wchar_t ipW[64] = {};
        MultiByteToWideChar(CP_UTF8, 0, ipA, -1, ipW, _countof(ipW));

        // de-dupe inside this master list (important for DX deusex + hx)
        if (UE_MasterHasServer(master, ipW, port))
        {
            p = objEnd + 1;
            continue;
        }

        if (master->count >= master->cap)
            break;

        LameServer* s = master->servers[master->count];
        if (!s)
            break;

        ZeroMemory(s, sizeof(*s));
        s->game = game;
        s->state = QUERY_IDLE;
        s->port = port;
        wcsncpy_s(s->ip, _countof(s->ip), ipW, _TRUNCATE);

        // --- pull extra fields from the same JSON object (master-only fields) ---
        char gnameA[64] = {};
        char labelA[64] = {};

        // Gamename is only provided by the master response, not the direct server query
        if (UE_CopyJsonStringFieldRange(objStart, objEnd, "gamename", gnameA, (int)sizeof(gnameA)))
            MultiByteToWideChar(CP_UTF8, 0, gnameA, -1, s->gamename, _countof(s->gamename));

        // Label is only provided by the master response, not the direct server query
        if (UE_CopyJsonStringFieldRange(objStart, objEnd, "label", labelA, (int)sizeof(labelA)))
            MultiByteToWideChar(CP_UTF8, 0, labelA, -1, s->label, _countof(s->label));

        // --- pull common server display fields from JSON (preferred; avoids extra UDP work) ---
        char hostnameA[192] = {};
        char mapnameA[64] = {};
        char gametypeA[64] = {};
        char gameverA[32] = {};
        char shortnameA[64] = {};
        char queryidA[32] = {};

        int numplayers = 0;
        int maxplayers = 0;

        UE_CopyJsonStringFieldRange(objStart, objEnd, "hostname", hostnameA, (int)sizeof(hostnameA));
        UE_CopyJsonStringFieldRange(objStart, objEnd, "mapname", mapnameA, (int)sizeof(mapnameA));
        UE_CopyJsonStringFieldRange(objStart, objEnd, "gametype", gametypeA, (int)sizeof(gametypeA));
        UE_CopyJsonStringFieldRange(objStart, objEnd, "gamever", gameverA, (int)sizeof(gameverA));
        UE_CopyJsonStringFieldRange(objStart, objEnd, "shortname", shortnameA, (int)sizeof(shortnameA));
        UE_CopyJsonStringFieldRange(objStart, objEnd, "queryid", queryidA, (int)sizeof(queryidA));

        UE_CopyJsonIntFieldRange(objStart, objEnd, "numplayers", &numplayers);
        UE_CopyJsonIntFieldRange(objStart, objEnd, "maxplayers", &maxplayers);

        // Fill primary columns from JSON if present
        if (hostnameA[0])
            MultiByteToWideChar(CP_UTF8, 0, hostnameA, -1, s->name, _countof(s->name));
        if (mapnameA[0])
            MultiByteToWideChar(CP_UTF8, 0, mapnameA, -1, s->map, _countof(s->map));
        if (gametypeA[0])
            MultiByteToWideChar(CP_UTF8, 0, gametypeA, -1, s->gametype, _countof(s->gametype));

        if (numplayers >= 0)  s->players = numplayers;
        if (maxplayers >= 0)  s->maxPlayers = maxplayers;

        // Optional: expose useful JSON fields as rules too
        if (shortnameA[0]) UE_AddRule(s, "shortname", shortnameA);
        if (gameverA[0])   UE_AddRule(s, "gamever", gameverA);
        if (queryidA[0])   UE_AddRule(s, "queryid", queryidA);

        master->count++;
        added++;

        // Advance to after this object so we don't rescan inside it
        p = objEnd + 1;
    }

    return added;
}

static int UE_NextToken(const char* list, int* ioPos, char* outTok, int outCap)
{
    if (!list || !ioPos || !outTok || outCap <= 1)
        return 0;

    int pos = *ioPos;
    int n = (int)strlen(list);

    while (pos < n && (list[pos] == ' ' || list[pos] == '\t' || list[pos] == ';' || list[pos] == ','))
        pos++;

    if (pos >= n)
    {
        *ioPos = pos;
        return 0;
    }

    int start = pos;
    while (pos < n && list[pos] != ';' && list[pos] != ',' && list[pos] != ' ' && list[pos] != '\t')
        pos++;

    int len = pos - start;
    if (len <= 0)
    {
        *ioPos = pos;
        return 0;
    }

    if (len >= outCap)
        len = outCap - 1;

    memcpy(outTok, list + start, len);
    outTok[len] = 0;

    *ioPos = pos;
    return 1;
}

// ------------------------------------------------------------
// Shared Master Query (JSON)
//  - desc->masterQueryString supports multi endpoints via "deusex;hx"
// ------------------------------------------------------------

int UE_QueryMasterServer_JSON(GameId game, int masterIndex, const LameMasterAddress* masterAddr)
{
    if (!masterAddr)
        return 0;

    const LameGameDescriptor* desc = Game_GetDescriptor(game);
    if (!desc)
        return 0;

    LameMaster* master = Data_GetMasterInternet(game, masterIndex);
    if (!master)
        return 0;

    int port = masterAddr->port;
    if (port <= 0) port = desc->defaultMasterPort;
    if (port <= 0) port = 443;

    const int r = 1000;
    const int maxPages = 32;

    Data_Lock();
    master->count = 0;
    master->rawCount = 0;
    Data_Unlock();

    int addedTotal = 0;

    // If no token provided, default to "unreal"
    const char* tokenList = (desc->masterQueryString && desc->masterQueryString[0]) ? desc->masterQueryString : "unreal";

    int pos = 0;
    char tok[64];

    while (UE_NextToken(tokenList, &pos, tok, (int)sizeof(tok)))
    {
        for (int page = 1; page <= maxPages; page++)
        {
            if (master->count >= master->cap)
                break;

            wchar_t path[256];
            swprintf_s(path, _countof(path), L"/json/%S?r=%d&p=%d", tok, r, page);

            std::string body;
            int got = UE_WinHttpGet(masterAddr->address, port, path, body);
            if (got <= 0)
                break;

            // Log the response (debug only)
            //UE_LogMasterJsonResponse(game, masterIndex, masterAddr->address, port, path, body.data(), got);

            Data_Lock();
            int added = UE_ParseMasterJsonPage(body.data(), got, master, game);
            master->rawCount = master->count;
            Data_Unlock();

            addedTotal += added;

            // Stop only when the page contains no server entries at all.
            if (strstr(body.c_str(), "\"ip\":\"") == NULL)
                break;

            // small delay so we don't hammer the host
            Sleep(150);
        }
    }

    return addedTotal;
}

// ------------------------------------------------------------
// Shared Server Query (UDP "\info\")
// ------------------------------------------------------------

static int UE_FirstRecordSlice(const char* big, int biglen, const char** outStart, int* outLen)
{
    if (!big || biglen <= 0 || !outStart || !outLen)
        return 0;

    *outStart = NULL;
    *outLen = 0;

    const char* f = strstr(big, "\\final\\");
    if (!f)
        return 0;

    *outStart = big;
    *outLen = (int)((f + 7) - big); // include "\final\"
    return (*outLen > 0);
}

static int UE_GetKV_BSlash(const char* buf, int len, const char* wantKey, char* out, int outCap)
{
    if (!buf || len <= 0 || !wantKey || !out || outCap <= 1)
        return 0;

    out[0] = 0;

    const int keyLen = (int)strlen(wantKey);
    if (keyLen <= 0)
        return 0;

    for (int i = 0; i + keyLen + 2 < len; i++)
    {
        if (buf[i] != '\\')
            continue;

        if (_strnicmp(buf + i + 1, wantKey, keyLen) != 0)
            continue;

        if (buf[i + 1 + keyLen] != '\\')
            continue;

        const char* p = buf + i + 1 + keyLen + 1;

        int n = 0;
        while ((p + n) < (buf + len) && p[n] && p[n] != '\\')
            n++;

        if (n >= outCap)
            n = outCap - 1;

        memcpy(out, p, n);
        out[n] = 0;
        return 1;
    }

    return 0;
}

static void UE_SanitizeValue(char* s)
{
    if (!s) return;

    for (int i = 0; s[i]; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == 127)
            s[i] = ' ';
    }

    int i = (int)strlen(s);
    while (i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t' || s[i - 1] == '\r' || s[i - 1] == '\n'))
        s[--i] = 0;
}

// Add ALL \key\value pairs from an Unreal "\info\" record as rules.
// Call this AFTER outServer->ruleCount = 0; and AFTER you validated it's a real reply.
static void UE_AddAllRulesFromRecord(LameServer* outServer, const char* rec, int reclen)
{
    if (!outServer || !rec || reclen <= 0)
        return;

    int i = 0;

    // record is like: \key\val\key\val\final\     //
    while (i < reclen)
    {
        // seek to '\'
        while (i < reclen && rec[i] != '\\')
            i++;

        if (i >= reclen)
            break;

        i++; // skip '\'
        if (i >= reclen)
            break;

        // key start
        int k0 = i;
        while (i < reclen && rec[i] != '\\' && rec[i] != '\0')
            i++;
        int klen = i - k0;

        if (klen <= 0 || i >= reclen || rec[i] != '\\')
            break;

        // key string
        char key[128];
        if (klen >= (int)sizeof(key))
            klen = (int)sizeof(key) - 1;
        memcpy(key, rec + k0, klen);
        key[klen] = 0;

        i++; // skip '\' before value
        if (i >= reclen)
            break;

        // value start
        int v0 = i;
        while (i < reclen && rec[i] != '\\' && rec[i] != '\0')
            i++;
        int vlen = i - v0;

        char val[256];
        if (vlen < 0) vlen = 0;
        if (vlen >= (int)sizeof(val))
            vlen = (int)sizeof(val) - 1;
        memcpy(val, rec + v0, vlen);
        val[vlen] = 0;

        UE_SanitizeValue(key);
        UE_SanitizeValue(val);

        // stop marker (sometimes comes with empty value)
        if (_stricmp(key, "final") == 0)
            break;

        // Optional: skip the ones you already show in columns
        if (_stricmp(key, "hostname") != 0 &&
            _stricmp(key, "mapname") != 0 &&
            _stricmp(key, "gametype") != 0 &&
            _stricmp(key, "hostport") != 0 &&
            _stricmp(key, "numplayers") != 0 &&
            _stricmp(key, "maxplayers") != 0)
        {
            UE_AddRule(outServer, key, val);
        }

        // continue (i is either at '\' starting next key or at end)
    }
}

static void UE_AddRule(LameServer* outServer, const char* keyA, const char* valA)
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

// LameGameUE.cpp
// Put these helpers ABOVE UE_QueryGameServer_UDP (same section as UE_GetKV_BSlash / UE_AddAllRulesFromRecord)

static int UE_DoUDPQuery(SOCKET sock, const sockaddr_in* to, const char* query, char* big, int bigCap, int* outLen)
{
    if (!query || !big || bigCap <= 1 || !outLen)
        return 0;

    *outLen = 0;
    big[0] = 0;

    const int qlen = (int)strlen(query);
    if (qlen <= 0)
        return 0;

    if (sendto(sock, query, qlen, 0, (const sockaddr*)&to[0], (int)sizeof(*to)) == SOCKET_ERROR)
        return 0;

    for (;;)
    {
        char buf[4096];
        sockaddr_in from = {};
        int fromlen = (int)sizeof(from);

        int len = recvfrom(sock, buf, (int)sizeof(buf), 0, (sockaddr*)&from, &fromlen);
        if (len <= 0)
            break;

        if (*outLen + len < bigCap)
        {
            memcpy(big + *outLen, buf, len);
            *outLen += len;
            big[*outLen] = 0;
        }

        if (strstr(big, "\\final\\") != NULL)
            break;
    }

    return (*outLen > 0);
}

static void UE_ParsePlayersFromRecord(LameServer* outServer, const char* rec, int reclen)
{
    if (!outServer || !rec || reclen <= 0)
        return;

    outServer->playerCount = 0;

    for (int idx = 0; idx < LAME_MAX_PLAYERS; idx++)
    {
        char key[32];
        char nameA[256] = {};
        char scoreA[64] = {};
        char pingA[64] = {};

        sprintf_s(key, _countof(key), "player_%d", idx);
        if (!UE_GetKV_BSlash(rec, reclen, key, nameA, (int)sizeof(nameA)))
            continue;

        UE_SanitizeValue(nameA);
        if (!nameA[0])
            continue;

        int score = 0;
        int ping = 0;

        sprintf_s(key, _countof(key), "score_%d", idx);
        if (UE_GetKV_BSlash(rec, reclen, key, scoreA, (int)sizeof(scoreA)))
            score = atoi(scoreA);

        sprintf_s(key, _countof(key), "ping_%d", idx);
        if (UE_GetKV_BSlash(rec, reclen, key, pingA, (int)sizeof(pingA)))
            ping = atoi(pingA);

        int outIdx = outServer->playerCount;
        if (outIdx >= LAME_MAX_PLAYERS)
            break;

        MultiByteToWideChar(CP_UTF8, 0, nameA, -1,
            outServer->playerList[outIdx].name, _countof(outServer->playerList[outIdx].name));

        outServer->playerList[outIdx].score = score;
        outServer->playerList[outIdx].ping = ping;

        outServer->playerCount++;
    }
}

int UE_QueryGameServer_UDP(const wchar_t* ipW, int port, LameServer* outServer)
{
    if (!ipW || !ipW[0] || port <= 0 || port > 65535 || !outServer)
        return 0;

    char ipA[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, ipW, -1, ipA, (int)sizeof(ipA), NULL, NULL);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    DWORD timeout = 800;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = NULL;

    if (getaddrinfo(ipA, NULL, &hints, &result) != 0 || !result)
    {
        closesocket(sock);
        return 0;
    }

    sockaddr_in* addr = (sockaddr_in*)result->ai_addr;
    to.sin_addr = addr->sin_addr;

    freeaddrinfo(result);

    int tryPorts[2] = { port + 1, port };

    char big[8192] = {};
    int biglen = 0;

    int success = 0;

    for (int pi = 0; pi < 2 && !success; pi++)
    {
        int qport = tryPorts[pi];
        if (qport <= 0 || qport > 65535)
            continue;

        to.sin_port = htons((u_short)qport);

        DWORD t0 = GetTickCount();

        // IMPORTANT:
        // - \info\ commonly does NOT include player_* fields
        // - \status\ is what typically carries player_* / score_* / ping_*
        const char* queries[2] = { "\\status\\", "\\info\\" };

        const char* rec = NULL;
        int reclen = 0;

        int gotRecord = 0;

        for (int qi = 0; qi < 2 && !gotRecord; qi++)
        {
            biglen = 0;
            big[0] = 0;

            if (!UE_DoUDPQuery(sock, &to, queries[qi], big, (int)sizeof(big), &biglen))
                continue;

            if (biglen <= 0)
                continue;

            if (!UE_FirstRecordSlice(big, biglen, &rec, &reclen))
                continue;

            gotRecord = 1;
        }

        if (!gotRecord || !rec || reclen <= 0)
            continue;

        char hostname[192] = {};
        char mapname[64] = {};
        char gametype[64] = {};
        char shortname[64] = {};
        char gamever[32] = {};
        char queryid[32] = {};
        char tmp[64] = {};

        int hostport = 0;
        int numplayers = 0;
        int maxplayers = 0;

        UE_GetKV_BSlash(rec, reclen, "hostname", hostname, (int)sizeof(hostname));
        UE_GetKV_BSlash(rec, reclen, "mapname", mapname, (int)sizeof(mapname));
        UE_GetKV_BSlash(rec, reclen, "gametype", gametype, (int)sizeof(gametype));
        UE_GetKV_BSlash(rec, reclen, "shortname", shortname, (int)sizeof(shortname));
        UE_GetKV_BSlash(rec, reclen, "gamever", gamever, (int)sizeof(gamever));
        UE_GetKV_BSlash(rec, reclen, "queryid", queryid, (int)sizeof(queryid));

        if (UE_GetKV_BSlash(rec, reclen, "hostport", tmp, (int)sizeof(tmp)))    hostport = atoi(tmp);
        if (UE_GetKV_BSlash(rec, reclen, "numplayers", tmp, (int)sizeof(tmp)))  numplayers = atoi(tmp);
        if (UE_GetKV_BSlash(rec, reclen, "maxplayers", tmp, (int)sizeof(tmp)))  maxplayers = atoi(tmp);

        UE_SanitizeValue(hostname);
        UE_SanitizeValue(mapname);
        UE_SanitizeValue(gametype);
        UE_SanitizeValue(shortname);
        UE_SanitizeValue(gamever);
        UE_SanitizeValue(queryid);

        if (!hostname[0] && !mapname[0] && !gametype[0])
            continue;

        // Only wipe once we KNOW the reply is valid
        outServer->ruleCount = 0;
        outServer->playerCount = 0;

        UE_AddAllRulesFromRecord(outServer, rec, reclen);
        UE_ParsePlayersFromRecord(outServer, rec, reclen);

        DWORD t1 = GetTickCount();
        outServer->ping = (int)(t1 - t0);

        MultiByteToWideChar(CP_UTF8, 0, hostname, -1, outServer->name, _countof(outServer->name));
        MultiByteToWideChar(CP_UTF8, 0, mapname, -1, outServer->map, _countof(outServer->map));
        MultiByteToWideChar(CP_UTF8, 0, gametype, -1, outServer->gametype, _countof(outServer->gametype));

        outServer->players = numplayers;
        outServer->maxPlayers = maxplayers;

        if (hostport > 0 && hostport <= 65535)
            outServer->port = hostport;

        success = 1;
    }

    closesocket(sock);
    return success;
}