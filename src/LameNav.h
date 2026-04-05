#pragma once

#include <Windows.h>

#include "LameCore.h"
#include "LameUiSession.h"

typedef struct NavUi
{
    HWND hwndMain;
    HWND hTreeMenu;
    HWND hServerList;
} NavUi;

typedef struct NavContext
{
    const NavUi* ui;

    // Home Favorites "combined master" storage owned by HomeView module
    LameMaster* homeFavoritesMaster;

    // UI-owned helpers that Nav should not implement
    void (*BuildHomeFavoritesMaster)(void);
    void (*RebuildPlayersRulesFromServer)(const LameServer* s);

} NavContext;

void Nav_OnTreeSelectionChanged(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled,
    int* soundPlayedForBatch,
    int* soundBatchGen);

void Nav_OnTreeReclicked(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled,
    int* soundPlayedForBatch,
    int* soundBatchGen);

void Nav_OnCommandFetchNewList(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled);

void Nav_OnCommandRefreshList(
    const NavContext* ctx,
    HWND hWnd,
    GameId* activeGame,
    LameMaster** activeMaster,
    LameServer** selectedServer,
    GameId* combinedQueryGame,
    int* combinedQueryGen,
    GameId* masterFetchGame,
    int* masterFetchRemaining,
    BOOL* queryBatchCanceled,
    int* soundPlayedForBatch,
    int* soundBatchGen);

void Nav_OnTreeSelectionChangedS(
    const NavContext* ctx,
    HWND hWnd,
    UiSession* s);

void Nav_OnTreeReclickedS(
    const NavContext* ctx,
    HWND hWnd,
    UiSession* s);

void Nav_OnCommandFetchNewListS(
    const NavContext* ctx,
    HWND hWnd,
    UiSession* s);

void Nav_OnCommandRefreshListS(
    const NavContext* ctx,
    HWND hWnd,
    UiSession* s);