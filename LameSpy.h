#pragma once

#include "framework.h"

//
// Macros and enums
//

#define MAX_SERVERS  4096
#define MAX_PLAYERS 64

#define HOSTNAME_LEN 128

#define SERVERS_PER_PAGE 30
#define LINE_BUFFER 256

#define ANSWER_SIZE 4096
#define CMD_SIZE 4096

#define IP_LENGTH 32  // IPv4 only

#define TEST_IP "108.36.69.49"
#define TEST_PORT 27960

#define MASTERSERVER_PORT 27950
#define IOQUAKE3_MASTER "138.68.50.7" // TODO: display DNS name to user

#define MASTERSERVER_QUERYSTRING "\xff\xff\xff\xffgetservers 68"
#define QUERY_STRING "\xff\xff\xff\xffgetstatus"  // Quake 3

#define Q3_FILENAME "q3servers.cfg"

// Windows console scan codes
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_UP    72
#define KEY_DOWN  80
#define KEY_PGUP   73
#define KEY_PGDN   81
#define KEY_J      'j'
#define KEY_J_CAP  'J'

enum { PING_IPONLY, PING_ALLINFO, PING_NOTEXT };

typedef enum
{
    QUERY_IDLE,
    QUERY_QUEUED,
    QUERY_IN_PROGRESS,
    QUERY_DONE,
    QUERY_FAILED
} ServerQueryState;


//
// Structs
//

typedef struct
{
    char name[32];
    int score;
    int ping;
    int time;

} Q3Player;


typedef struct
{
    char ip[IP_LENGTH];
    int  port;
    int  ping;

    char sv_hostname[HOSTNAME_LEN];
    char mapname[64];
    char version[64];
    int  g_gametype;
    char Administrator[64];
    char Email[64];
    char URL[256];
    char Location[64];
    char CPU[64];

    int  fraglimit;
    int  timelimit;
    int  capturelimit;
    int  sv_maxclients;

    int sv_maxPing;
    int sv_minPing;
    int sv_maxRate;
    int sv_privateClients;
    int g_maxGameClients;
    int g_needpass;

    int protocol;
    
    /*ioQuake3-specific keys */
    int com_protocol;
    int com_legacyprotocol;
    int ioq3;
    int sv_dlRate;
    int sv_minRate;
    int sv_floodProtect;
    int sv_lanForceRate;
    int sv_allowDownload;
    char sv_dlURL[256];

    int num_players;
    Q3Player players[MAX_PLAYERS];

    volatile ServerQueryState state;

} Q3Server;  


struct ApplicationSettings
{
    int SoundsEnabled;
    int RefreshOnStart;
    int ConsoleOnStart;
};

extern struct ApplicationSettings LamespySettings;


//
// Function prototypes 
// 

// Game
void LaunchQ3(const char* ip, int port);

// Network
int Net_InitSockets();
int Net_PingServer(const char* destination, int style);
int Net_QueryServer(Q3Server* server);
int Net_ManualQuery(const char* server_ip, int port);
int Net_DownloadMasterList();
void Net_ShutdownSockets();

// Console output
int LameMain();
void Print_Help();
void Print_LineBreak(int num);
void StripQ3Colors(char* text);
const char* DecodeGameType(int gametype);
const char* DecodeProtocol(const char* protocol);
const char* YesOrNo(int num);

// Server lists
int New_SavePlayerList(const char* response, Q3Player* players, int max_players);
int New_PrintServerInfo(const Q3Server* s);
void New_ServerBrowserMenu(Q3Server servers[], int count);
void ParseServerInfo(const char* info, Q3Server* server);
void New_PrintPlayerList(const Q3Server* server);
void StartServerQuery(Q3Server* server);
DWORD WINAPI ServerQueryThread(LPVOID param);

// File I/O
int File_WriteDefaultSettings(const char* filename);
int File_DeleteServerList();
int File_WriteMasterList(char answer[ANSWER_SIZE], int answerSize);
int File_LoadSettings(const char* filename, struct ApplicationSettings* settings);
int File_LoadFavorites(const char* filename);
int File_LoadMasterList(const char* filename);
int File_CheckSettings(const char* filename);
void File_CreateDefaultFavorites(const char* filename);
void File_PrintServerList();
void InitServerLists();


//
// Windows GUI
//

#define MAX_LOADSTRING 100

typedef struct { char ip[IP_LENGTH]; int  port; } SERVER;

HWND InitServerListBox(HWND hDlg, HINSTANCE hInst);
void RefreshServers(HWND hDlg);
BOOL IsChecked(HWND hDlg, int checkboxID);
void PlayLameSound(LPCSTR sound);
int ParseLine(const char* line, SERVER* server);
int File_LoadServers(const char* filename, SERVER* servers, int maxServers);
void FillServerList(HWND hDlg);
void FillServerList(HWND hWnd, HINSTANCE hInst);
void LaunchWebsite();
void OpenConsole();


//
// Legacy array stuff
//

#define EXCLUDEDSTATS 7
#define NUMSTATS 30

#define LINEBREAK1 1
#define LINEBREAK2 5
#define LINEBREAK3 9
#define LENGTH 64  // rename

enum serversetting_t {

    SERVER_NAME, CURRENT_MAP, MAXPLAYERS, FRAGLIMIT, TIMELIMIT, CAPTURELIMIT,
    GAMETYPE, DMFLAGS, GAMENAME, MINPLAYERS, PUNKBUSTER, PROTOCOL, VERSION,
    ALLOW_DOWNLOAD, NEEDS_PASSWORD, MIN_PING, MAX_PING, MAX_RATE, FLOOD_PROTECT,
    PRIVATE_CLIENTS, MAX_GAMECLIENTS, ADMIN, EMAIL, WEBSITE, LOCATION, CPU,
    ACTIVE_PLAYERS, SERVER_PING, IP_ADDRESS, UDP_PORT
};

enum { PUBLIC_LIST, FAVORITES_LIST };

extern char SelectedServer[NUMSTATS][LENGTH];
//extern char Q3_Servers[MAX_SERVERS][IP_LENGTH];
extern char Q3_Favorites[MAX_SERVERS][IP_LENGTH];
extern int Q3_ServerCount;
extern int Q3_FavoriteCount;

int BuildServerArray(const char* info, const char* player_list, const char* server_ip, int port);
const char* GetKeyValue(const char* info, const char* key, char* out, size_t outSize);
char* SplitAnswer(char* answer, int answerSize);
void GenericServerBrowserMenu(char serverlist[][IP_LENGTH], int count, int list_type);
int Old_SavePlayerList(const char* player_list, struct OldPlayerList* players, int max_players);
void Print_ServerInfo(char server_answer[ANSWER_SIZE]);
int Old_PrintPlayerList();
void Print_MasterServerInfo(char answer[ANSWER_SIZE], int answerSize);