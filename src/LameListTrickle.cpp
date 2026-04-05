#include "LameWin.h"
#include "LameListTrickle.h"

#include <vector>
#include <CommCtrl.h>

#include "LameFilters.h"
#include "LameServerListView.h"

static CRITICAL_SECTION g_csPendingResults;
static int g_pendingResultsInit = 0;
static std::vector<LameServer*> g_pendingResults;
static volatile LONG g_pendingWakePosted = 0;

static HWND g_hwndMain = NULL;
static HWND g_hServerList = NULL;

static void PendingResultsInitOnce(void)
{
    if (g_pendingResultsInit)
        return;

    InitializeCriticalSection(&g_csPendingResults);
    g_pendingResultsInit = 1;
}

void ServerListTrickle_Init(HWND hwndMain, HWND serverList)
{
    g_hwndMain = hwndMain;
    g_hServerList = serverList;

    PendingResultsInitOnce();
}

void ServerListTrickle_Shutdown(void)
{
    if (!g_pendingResultsInit)
        return;

    EnterCriticalSection(&g_csPendingResults);
    g_pendingResults.clear();
    LeaveCriticalSection(&g_csPendingResults);

    DeleteCriticalSection(&g_csPendingResults);
    g_pendingResultsInit = 0;

    g_hwndMain = NULL;
    g_hServerList = NULL;

    InterlockedExchange(&g_pendingWakePosted, 0);
}

static int PopPendingBatch(std::vector<LameServer*>& outBatch, int maxCount)
{
    outBatch.clear();

    if (!g_pendingResultsInit)
        return 0;

    EnterCriticalSection(&g_csPendingResults);

    int n = (int)g_pendingResults.size();
    if (n > 0)
    {
        int take = n;
        if (take > maxCount) take = maxCount;

        outBatch.reserve((size_t)take);

        for (int i = 0; i < take; i++)
        {
            LameServer* s = g_pendingResults.back();
            g_pendingResults.pop_back();
            outBatch.push_back(s);
        }
    }

    int remaining = (int)g_pendingResults.size();

    LeaveCriticalSection(&g_csPendingResults);
    return remaining;
}

static int MasterContainsServer(const LameMaster* m, const LameServer* s)
{
    if (!m || !s || m->count <= 0 || !m->servers)
        return 0;

    for (int i = 0; i < m->count; i++)
    {
        if (m->servers[i] == s)
            return 1;
    }
    return 0;
}

static void FlushInternal(HWND hWnd, int maxBatch, LameMaster* activeMaster, LameServer** selectedServer)
{
    if (!IsWindow(hWnd) || !IsWindow(g_hServerList))
        return;

    PendingResultsInitOnce();

    std::vector<LameServer*> batch;
    int remainingAfter = PopPendingBatch(batch, maxBatch);

    for (size_t k = 0; k < batch.size(); k++)
    {
        LameServer* server = batch[k];
        if (!server)
            continue;

        int row = ServerListView_FindServerRow(g_hServerList, server);

        if (row >= 0)
        {
            if (!UI_ServerPassesFilters(server))
            {
                ListView_DeleteItem(g_hServerList, row);

                if (selectedServer && *selectedServer == server)
                {
                    *selectedServer = NULL;
                    // Details refresh stays in LameUI; clearing selection is enough here.
                }
                continue;
            }
        }

        if (row < 0)
        {
            if (server->state == QUERY_DONE &&
                MasterContainsServer(activeMaster, server) &&
                UI_ServerPassesFilters(server))
            {
                row = ServerListView_InsertServerRowFromServer(g_hServerList, server);
            }
        }

        if (row < 0)
            continue;

        ServerListView_ApplyServerResultToRow(g_hServerList, row, server);
    }

    if (batch.size() > 0)
        InvalidateRect(g_hServerList, NULL, FALSE);

    if (remainingAfter <= 0)
        KillTimer(hWnd, IDT_QUERY_FLUSH);
}

void ServerListTrickle_OnServerQueryFinished(LameServer* server)
{
    if (!server)
        return;

    if (!IsWindow(g_hwndMain))
        return;

    PendingResultsInitOnce();

    EnterCriticalSection(&g_csPendingResults);
    g_pendingResults.push_back(server);
    LeaveCriticalSection(&g_csPendingResults);

    if (InterlockedCompareExchange(&g_pendingWakePosted, 1, 0) == 0)
        PostMessageW(g_hwndMain, WM_APP_QUERY_FLUSH, 0, 0);
}

void ServerListTrickle_OnAppFlush(HWND hWnd, LameMaster* activeMaster, LameServer** selectedServer)
{
    InterlockedExchange(&g_pendingWakePosted, 0);

    SetTimer(hWnd, IDT_QUERY_FLUSH, 16, NULL);

    FlushInternal(hWnd, QUERY_FLUSH_BATCH_MAX, activeMaster, selectedServer);
}

void ServerListTrickle_OnTimer(HWND hWnd, WPARAM wParam, LameMaster* activeMaster, LameServer** selectedServer)
{
    if (wParam != IDT_QUERY_FLUSH)
        return;

    FlushInternal(hWnd, QUERY_FLUSH_BATCH_MAX, activeMaster, selectedServer);
}

void ServerListTrickle_FlushNow(HWND hWnd, int maxBatch, LameMaster* activeMaster, LameServer** selectedServer)
{
    FlushInternal(hWnd, maxBatch, activeMaster, selectedServer);
}

void ServerListTrickle_ClearPending(void)
{
    PendingResultsInitOnce();

    EnterCriticalSection(&g_csPendingResults);
    g_pendingResults.clear();
    LeaveCriticalSection(&g_csPendingResults);
}