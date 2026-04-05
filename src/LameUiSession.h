#pragma once

#include "LameCore.h"

typedef struct UiSession
{
    GameId activeGame;

    LameMaster* activeMaster;
    LameServer* selectedServer;

    BOOL suppressTreeSelChanged;

    // Shared generation for "combined view" refresh so that starting queries for one master
    // doesn't cancel in-flight queries for another master.
    GameId combinedQueryGame;
    int combinedQueryGen;

    GameId masterFetchGame;
    int masterFetchRemaining;
    BOOL queryBatchCanceled;

    int soundPlayedForBatch;
    int soundBatchGen;

} UiSession;