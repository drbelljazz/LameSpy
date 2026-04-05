#pragma once

#include <Windows.h>
#include <CommCtrl.h>

#include "LameCore.h"

void ServerListView_Init(HWND listView, HINSTANCE hInst);
void ServerListView_Shutdown(void);

int ServerListView_IsFavoritesView(HWND treeMenu);

void ServerListView_Rebuild(HWND serverList, HWND treeMenu, const LameMaster* activeMaster);
void ServerListView_UpdateSelectedRow(HWND serverList, const LameServer* selectedServer);

int ServerListView_FindServerRow(HWND lv, const LameServer* server);
int ServerListView_InsertServerRowFromServer(HWND lv, const LameServer* s);
void ServerListView_ApplyServerResultToRow(HWND lv, int row, const LameServer* server);

LRESULT ServerListView_OnCustomDraw(LPNMLVCUSTOMDRAW cd);