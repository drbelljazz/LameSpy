#pragma once

#include <Windows.h>
#include <commctrl.h>

#include "LameCore.h"

// Tree control creation
HWND Tree_Create(HWND parent, HINSTANCE hInst);

// Tree content
void UI_BuildTree(void);
void Tree_ExpandAllGameNodesIfEnabled(HWND treeMenu);

// Tree state/bolding helpers
void Tree_UpdateMasterNodeBold(HWND treeMenu, GameId game, int masterIndex, BOOL bold);
void Tree_UpdateGameNodeBold(HWND treeMenu, GameId game);

// Finds the tree node for the specified game in the given tree view control.
// Returns the HTREEITEM if found, or NULL if not found.
HTREEITEM Tree_FindGameNode(HWND hTree, GameId game);

// Returns the HTREEITEM for the "Tutorials" node, or NULL if not found.
HTREEITEM Tree_FindTutorialsNode(HWND hTree);

static const GameId kUiGameOrder[] =
{
    GAME_UT99,
    GAME_UG,
    GAME_DX,
    GAME_Q3,
    GAME_Q2,
    GAME_QW
 };