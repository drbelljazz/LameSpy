#pragma once
#include <windows.h>
#include "LameCore.h"
#include "LameUI.h"

static __forceinline LPARAM TreeTag_Make(GameId game, TreeNodeKind kind, int masterIndex)
{
    /*  [31..16]=game, [15..8]=kind, [7..0]=masterIndex (0..255) */
    return (LPARAM)(((unsigned)game << 16) | ((unsigned)kind << 8) | ((unsigned)masterIndex & 0xFF));
}

static __forceinline GameId TreeTag_Game(LPARAM tag)
{
    return (GameId)(((unsigned)tag >> 16) & 0xFFFF);
}

static __forceinline TreeNodeKind TreeTag_Kind(LPARAM tag)
{
    return (TreeNodeKind)(((unsigned)tag >> 8) & 0xFF);
}

static __forceinline TreeNodeKind UI_NormalizeTreeKind(LPARAM tag)
{
    // The "Games" root node is currently created with lParam == 0.
    // Treat it like Home so selecting it shows the home page.
    if (tag == 0)
        return TREE_NODE_HOME;

    return TreeTag_Kind(tag);
}

static __forceinline int TreeTag_MasterIndex(LPARAM tag)
{
    return (int)((unsigned)tag & 0xFF);
}