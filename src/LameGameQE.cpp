#include "LameCore.h"
#include "LameData.h"
#include "LameGame.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// LameGameQE.cpp
// Shared UDP transport for Quake-engine style games (Q3/Q2/QW).
//
// This file intentionally does NOT know any wire formats.
//  - request bytes are provided by the caller
//  - parsing is provided by callbacks (master packet parser / server reply parser)
// -----------------------------------------------------------------------------

static int QE_ResolveIPv4(const wchar_t* hostW, int port, sockaddr_in* outAddr)
{
    if (!hostW || !hostW[0] || !outAddr || port <= 0 || port > 65535)
        return 0;

    char hostA[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, hostW, -1, hostA, (int)sizeof(hostA), NULL, NULL);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);

    // Fast path: numeric IPv4
    unsigned long ip = inet_addr(hostA);
    if (ip != INADDR_NONE)
    {
        addr.sin_addr.s_addr = ip;
        *outAddr = addr;
        return 1;
    }

    // DNS
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* res = NULL;
    if (getaddrinfo(hostA, NULL, &hints, &res) != 0 || !res)
        return 0;

    sockaddr_in* a4 = (sockaddr_in*)res->ai_addr;
    addr.sin_addr = a4->sin_addr;

    freeaddrinfo(res);

    *outAddr = addr;
    return 1;
}

static int QE_SelectRecv(SOCKET sock, int waitMs)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    timeval tv;
    tv.tv_sec = waitMs / 1000;
    tv.tv_usec = (waitMs % 1000) * 1000;

    return select(0, &rfds, NULL, NULL, &tv);
}

// recvUntilIdleMs:
//  - if <= 0: stop after the first packet received
//  - if >  0: keep receiving until no packets arrive for recvUntilIdleMs (or totalTimeoutMs)
static int QE_UdpExchange(
    const sockaddr_in* to,
    const unsigned char* req, int reqLen,
    int recvUntilIdleMs,
    int totalTimeoutMs,
    int (*OnPacket)(const unsigned char* data, int len, void* user),
    void* user)
{
    if (!to || !req || reqLen <= 0 || !OnPacket)
        return 0;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    // Non-blocking; we drive timeouts with select()
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);

    // Slightly larger buffers helps multi-packet masters
    int sz = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&sz, sizeof(sz));

    int sent = sendto(sock, (const char*)req, reqLen, 0, (const sockaddr*)to, sizeof(*to));
    if (sent <= 0)
    {
        closesocket(sock);
        return 0;
    }

    unsigned char buf[65535];

    DWORD tStart = GetTickCount();
    DWORD tLastPacket = tStart;

    int gotAny = 0;
    int totalPackets = 0;

    for (;;)
    {
        DWORD now = GetTickCount();
        DWORD elapsed = now - tStart;

        if (totalTimeoutMs > 0 && (int)elapsed >= totalTimeoutMs)
            break;

        // If we're in "first packet" mode and already got one, we're done.
        if (recvUntilIdleMs <= 0 && gotAny)
            break;

        // If we're in "until idle" mode and have been idle long enough, we're done.
        if (recvUntilIdleMs > 0 && gotAny)
        {
            DWORD idle = now - tLastPacket;
            if ((int)idle >= recvUntilIdleMs)
                break;
        }

        int sel = QE_SelectRecv(sock, 50);
        if (sel <= 0)
            continue;

        sockaddr_in from = {};
        int fromLen = sizeof(from);

        int len = recvfrom(sock, (char*)buf, (int)sizeof(buf), 0, (sockaddr*)&from, &fromLen);
        if (len <= 0)
            continue;

        gotAny = 1;
        tLastPacket = GetTickCount();
        totalPackets++;

        if (!OnPacket(buf, len, user))
            break; // parser asked us to stop
    }

    closesocket(sock);
    return gotAny ? totalPackets : 0;
}

// -----------------------------------------------------------------------------
// Public helpers (called by Q3/Q2/QW modules)
// -----------------------------------------------------------------------------

typedef struct QE_MasterCtx
{
    GameId game;
    LameMaster* master;
    GameParseMasterPacketFn parse;
    int addedTotal;
} QE_MasterCtx;

static int QE_OnMasterPacket(const unsigned char* data, int len, void* user)
{
    QE_MasterCtx* ctx = (QE_MasterCtx*)user;
    if (!ctx || !ctx->master || !ctx->parse)
        return 0;

    int added = ctx->parse(data, len, ctx->master, ctx->game);
    if (added > 0)
        ctx->addedTotal += added;

    // Keep going regardless; master replies are often multi-packet.
    return 1;
}

int QE_QueryMaster_UDP(
    GameId game,
    int masterIndex,
    const LameMasterAddress* masterAddr,
    const unsigned char* requestBytes,
    int requestLen,
    GameParseMasterPacketFn parseMasterPacket,
    int recvUntilIdleMs,
    int totalTimeoutMs)
{
    if (!masterAddr || !requestBytes || requestLen <= 0 || !parseMasterPacket)
        return 0;

    const LameGameDescriptor* desc = Game_GetDescriptor(game);
    if (!desc)
        return 0;

    LameMaster* master = Data_GetMasterInternet(game, masterIndex);
    if (!master)
        return 0;

    int port = masterAddr->port;
    if (port <= 0)
        port = desc->defaultMasterPort;

    sockaddr_in to = {};
    if (!QE_ResolveIPv4(masterAddr->address, port, &to))
        return 0;

    QE_MasterCtx ctx = {};
    ctx.game = game;
    ctx.master = master;
    ctx.parse = parseMasterPacket;
    ctx.addedTotal = 0;

    // NOTE: caller decides whether master->count/rawCount are reset before calling us.

    int packets = QE_UdpExchange(&to,
        requestBytes, requestLen,
        recvUntilIdleMs,
        totalTimeoutMs,
        QE_OnMasterPacket,
        &ctx);

    if (packets <= 0)
        return 0;

    return ctx.addedTotal;
}

typedef struct QE_ServerCtx
{
    LameServer* out;
    GameParseServerInfoFn parse;
    int sawValid;
} QE_ServerCtx;

static int QE_OnServerPacket(const unsigned char* data, int len, void* user)
{
    QE_ServerCtx* ctx = (QE_ServerCtx*)user;
    if (!ctx || !ctx->out)
        return 0;

    if (ctx->parse)
        ctx->parse((const char*)data, len, ctx->out);

    ctx->sawValid = 1;

    // In server querying we almost always want the first reply.
    return 0; // stop after first packet
}

int QE_QueryGameServer_UDP(
    const wchar_t* ipW,
    int port,
    LameServer* outServer,
    const unsigned char* requestBytes,
    int requestLen,
    GameParseServerInfoFn parseServerReply,
    int totalTimeoutMs)
{
    if (!ipW || !ipW[0] || port <= 0 || port > 65535 || !outServer)
        return 0;
    if (!requestBytes || requestLen <= 0)
        return 0;

    sockaddr_in to = {};
    if (!QE_ResolveIPv4(ipW, port, &to))
        return 0;

    QE_ServerCtx ctx = {};
    ctx.out = outServer;
    ctx.parse = parseServerReply;
    ctx.sawValid = 0;

    int packets = QE_UdpExchange(&to,
        requestBytes, requestLen,
        0,                 // stop after first packet
        totalTimeoutMs,
        QE_OnServerPacket,
        &ctx);

    return (packets > 0 && ctx.sawValid) ? 1 : 0;
}
