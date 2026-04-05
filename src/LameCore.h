#pragma once

#define LAMESPY_USE_COMCTL6 1
#define LAMESPY_USE_NEW_FONTS 1

#define LAME_MAX_PLAYERS  64
#define LAME_MAX_RULES    128

#define CFG_NAME_MAX 32
#define CFG_GAMECODE_MAX 4

typedef enum GameId
{
    GAME_NONE = 0,

    GAME_Q3   = 1,
    GAME_QW   = 2,
    GAME_Q2   = 3,
    GAME_UT99 = 4,
    GAME_UG   = 5,
    GAME_DX   = 6,
    GAME_UE   = 7,

    GAME_MAX
} GameId;

typedef struct LamePlayer
{
    wchar_t name[128];
    int     score;
    int     ping;
} LamePlayer;

typedef struct LameRule
{
    wchar_t key[128];
    wchar_t value[256];
} LameRule;

typedef enum QueryState
{
    QUERY_IDLE = 0,
    QUERY_IN_PROGRESS,
    QUERY_DONE,
    QUERY_FAILED,
    QUERY_CANCELED
} QueryState;

typedef struct LameServer
{
    wchar_t name[256];
    wchar_t ip[64];
    int port;
    wchar_t map[64];
    int players;
    int maxPlayers;
    int ping;
    wchar_t gametype[64];

    // For UE games
    wchar_t label[64];
    wchar_t gamename[64];

    volatile long state;     // store QueryState values
    volatile long gen;       // Generation number for staleness checks

    int isFavorite;
    int source;

    LamePlayer playerList[LAME_MAX_PLAYERS];
    int playerCount;

    LameRule ruleList[LAME_MAX_RULES];
    int ruleCount;

    GameId  game;   // which game this server belongs to
    
    unsigned long queryStartTime;  // Time when query started (for Unreal list cleanup)

} LameServer;

typedef struct LameMaster
{
    wchar_t       name[64];     /* "Internet", "Favorites", ... */
    LameServer** servers;      /* array of pointers (stable) */
    int           count;
    int           rawCount;
    int           cap;

} LameMaster;

typedef struct LameMasterAddress
{
    wchar_t address[256];  /* DNS name or IP address */
    int     port;          /* Master server port */
} LameMasterAddress;

typedef struct
{
    wchar_t playerName[64];
    char    startupItem[16];  
    
    int  showMasters;              // 0/1
    int  dedupeLists;             // 0/1

    int  hideDeadFavorites;       // 0/1
    int  hideDeadInternets;       // 0/1

    int  expandTreeOnStartup;     // 0/1

    int  showQtvQwfwd;              // 0/1

    int  leftPaneFontPt;            // 9, 10, 11
    int  rightPaneFontPt;           // 9, 10, 11

    unsigned int soundFlags;       // bitmask

    unsigned int enabledGameMask;  // bitmask of enabled games (1u << GameId)

    char lameServerHost[256];      // for version checking
    int  lameServerPort;
    
} LameConfig;  extern LameConfig g_config;

enum LameSoundFlags
{
    LSOUND_WELCOME = 1 << 0,
    LSOUND_SCAN_COMPLETE = 1 << 1,
    LSOUND_UPDATE_ABORT = 1 << 2,
    LSOUND_LAUNCH = 1 << 3
};

typedef enum StatusPriority
{
    STATUS_INFO = 0,        // Normal info messages
    STATUS_OPERATION = 1,   // Ongoing operations (queries, etc.)
    STATUS_IMPORTANT = 2    // User-initiated actions, errors
} StatusPriority;

// Networking
int InitializeNetworking(void);
void CleanupNetworking(void);
void Query_InitState(void);
void Query_Stop(void);

// Lame Host (for chat client, etc)
int LameHost_Init();
void LameHost_Stop();

// Separates UI from networking layer by allowing UI to register a callback to be invoked when a query finishes (e.g., to refresh the server list). The callback will be called once per master server as each finishes, and once more when all querying is done. Note that the callback may be called from a different thread, so it should not directly manipulate UI elements without proper synchronization (e.g., by posting a message to the main thread).
typedef void (*ServerQueryCallback)(LameServer* server);
void Query_SetFinishedCallback(ServerQueryCallback callback);

void Config_Load(void);
void Config_Save(void);

enum SoundSetting { SOUND_NONE = 0, SOUND_LAUNCH_ONLY = 1, SOUND_ALL = 2 };

// Insulates the core and UI layers by allowing the core to post messages 
// to the UI thread when query results are ready, without needing direct references to UI elements. The UI can call Query_SetTargetWindow at startup to specify which window should receive these messages. This allows the core to remain decoupled from the UI implementation, and also allows for flexibility in how the UI handles updates (e.g., it could choose to batch updates or update immediately as results come in).
//void Query_SetTargetWindow(void* hwnd);