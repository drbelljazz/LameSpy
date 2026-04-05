#pragma once

#include <Windows.h>

#include "LameCore.h"

void ServerListTrickle_Init(HWND hwndMain, HWND serverList);
void ServerListTrickle_Shutdown(void);

// Called from worker thread callback (Query_SetFinishedCallback target)
void ServerListTrickle_OnServerQueryFinished(LameServer* server);

// Called from LameUI timer handler when IDT_QUERY_FLUSH fires
void ServerListTrickle_OnTimer(HWND hWnd, WPARAM wParam, LameMaster* activeMaster, LameServer** selectedServer);

// Called from WM_APP_QUERY_FLUSH handler
void ServerListTrickle_OnAppFlush(HWND hWnd, LameMaster* activeMaster, LameServer** selectedServer);

// Called by cancel logic (to show whatever is queued and then drop the rest)
void ServerListTrickle_FlushNow(HWND hWnd, int maxBatch, LameMaster* activeMaster, LameServer** selectedServer);
void ServerListTrickle_ClearPending(void);

// Expose timer id / batch size so UI can do minimal switching
#define IDT_QUERY_FLUSH  0x4A11
#define QUERY_FLUSH_BATCH_MAX  48