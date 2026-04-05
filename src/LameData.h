// LameData.h - Data management (favorites, masters, server data)

#pragma once

#include "LameCore.h"
#include "LameNet.h"

#define CFG_EXEPATH_LEN 512
#define CFG_CMD_LEN 512

// Game ID conversion
const wchar_t* Game_PrefixW(GameId game);
GameId Game_FromPrefixW(const wchar_t* s);
GameId Game_FromPrefixA(const char* s);

// String utilities
wchar_t* WTrim(wchar_t* s);

// Path utilities
void Path_GetExeDir(wchar_t* out, int outCap);
void Path_BuildFavoritesCfg(wchar_t* out, int outCap);

// Main app configuration (lamespy.cfg)
void Path_BuildLameSpyCfg(wchar_t* out, int outCap);

void Config_EnsureFileExists(void);
void Config_Load(void);
void Config_Save(void);

const wchar_t* Config_GetExePath(GameId game);
void Config_SetExePath(GameId game, const wchar_t* path);

const wchar_t* Config_GetCmdArgs(GameId game);
void Config_SetCmdArgs(GameId game, const wchar_t* args);

// Favorites management
void Favorites_LoadFile(const wchar_t* path);
void Favorites_SaveFile(const wchar_t* path);
void Favorites_EnsureFileExists(const wchar_t* path);
void Favorites_ClearGame(GameId game);
void Favorites_AddInternal(GameId game, const wchar_t* ip, int port);
int Favorites_ParseLine(const wchar_t* line, wchar_t* ipOut, int ipCap, int* portOut);
int Favorites_ParseLineWithGame(const wchar_t* line, GameId* gameOut, wchar_t* ipOut, int ipCap, int* portOut);

// Server utilities
const wchar_t* Server_FindRuleValue(const LameServer* s, const wchar_t* key);
int Server_MatchIPPort(const LameServer* s, const wchar_t* ip, int port);

// Master data management
void Masters_InitData(void);
void Internet_FillDummy(GameId game, int masterIndex);
void Master_BuildCombinedForGame(GameId game);
void Master_RemoveFailedServers(LameMaster* master);

int Data_GetMasterRawCount(GameId game, int masterIndex);
int Data_GetMasterRespondedCount(GameId game, int masterIndex);

// Comparison functions for qsort
int LameServerPtrCompare(const void* a, const void* b);
int LamePlayerCompare(const void* a, const void* b);
int LameRuleCompare(const void* a, const void* b);

// Global data accessors
LameMaster* Data_GetMasterInternet(GameId game, int index);
LameMaster* Data_GetMasterFavorites(GameId game);
LameMaster* Data_GetMasterCombined(void);
//LameServer** Data_GetInternetStorage(GameId game, int masterIndex);

// Sort state accessors
void Data_SetSortState(int column, int ascending);
void Data_GetSortState(int* column, int* ascending);
void Data_SetPlayerSortState(int column, int ascending);
void Data_GetPlayerSortState(int* column, int* ascending);
void Data_SetRuleSortState(int column, int ascending);
void Data_GetRuleSortState(int* column, int* ascending);

// Master server configuration loading
void Masters_LoadConfigForGame(GameId game);
void Masters_EnsureConfigFilesExist(void);
void Path_BuildMastersCfg(GameId game, wchar_t* out, int outCap);
int Data_GetMasterCountForGame(GameId game);

// Master server address accessors
void Masters_InitializeFromAddresses(GameId game);
const LameMasterAddress* Data_GetMasterAddress(GameId game, int index);

// Query management functions
void Query_IncrementGeneration(void);
int Query_GetGeneration(void);

void Query_StartSingleServer(LameServer* server);
void Query_StartAllServers(GameId game, int masterIndex);
void Query_OnServerFinished(LameServer* server);

void Query_StartFavorites(GameId game);
void Query_StartFavoritesWithGen(GameId game, int gen);

int  Query_BeginBatch(void);
void Query_StartAllServersWithGen(GameId game, int masterIndex, int gen);

void Data_Lock(void);
void Data_Unlock(void);

const wchar_t* Config_GetWebGameSettings(GameId game);
void Config_SetWebGameSettings(GameId game, const wchar_t* json);

// Temporary debug function to dump master data to a file
void Data_DumpMasterToFile(const wchar_t* path, const LameMaster* master);

int LameServerPtrCompareAutoPopulated(const void* a, const void* b);