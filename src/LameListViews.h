#pragma once

#include <Windows.h>

HWND ListViews_CreateListView(HWND parent, HINSTANCE hInst, int id);

void ListViews_AddColumn(HWND lv, int i, int w, const wchar_t* txt, int hiddenByDefault);

void ListViews_InitServerColumns(HWND hServerList);
void ListViews_InitPlayerColumns(HWND hPlayerList);
void ListViews_InitRulesColumns(HWND hRulesList);