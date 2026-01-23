#include "lamespy.h"

Q3Server Servers[MAX_SERVERS];  
struct ApplicationSettings LamespySettings;

// Maybe put in text file
static const char* DefaultFavorites[] = {
    "108.36.69.49:27960",
    "108.36.69.49:27961",
    "108.36.69.49:27962" };

char Q3_Servers[MAX_SERVERS][IP_LENGTH];
char Q3_Favorites[MAX_SERVERS][IP_LENGTH];
int Q3_ServerCount;
int Q3_FavoriteCount;


int New_LoadMasterServerList(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp)
        return 0;

    Q3_ServerCount = 0;

    char line[LINE_BUFFER];

    while (fgets(line, sizeof(line), fp) &&
        Q3_ServerCount < MAX_SERVERS)
    {
        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* Skip empty lines */
        if (line[0] == '\0')
            continue;

        Q3Server* s = &Servers[Q3_ServerCount];

        /* Parse ip:port */
        if (sscanf(line, "%63[^:]:%d", s->ip, &s->port) != 2)
            continue;   /* malformed line */

        /* Initialize record */
        s->sv_hostname[0] = '\0';
        //s->queried = 0;

        // New for player struct insertion
        s->num_players = 0;
        //s->queried = 0;
        //s->failed = 0;

        Q3_ServerCount++;
    }

    fclose(fp);
    return Q3_ServerCount;
}


int File_LoadMasterList(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp)
        return 0;

    Q3_ServerCount = 0;
    char line[LINE_BUFFER];   // <-- FIX is HERE

    while (fgets(line, sizeof(line), fp) &&
        Q3_ServerCount < MAX_SERVERS)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
            continue;

        snprintf(Q3_Servers[Q3_ServerCount],
            IP_LENGTH,
            "%s",
            line);

        int i = Q3_ServerCount;

        sscanf(line, "%63[^:]:%d", Servers[i].ip, &Servers[i].port);
        //Servers[i].sv_hostname[0] = '\0';
        //Servers[i].queried = 0;

        Q3_ServerCount++;
    }

    fclose(fp);
    return Q3_ServerCount;
}


static void File_CreateDefaultFavorites(const char* filename)  // rename
{
    FILE* fp = fopen(filename, "w");
    if (!fp)
        return;

    int count = sizeof(DefaultFavorites) / sizeof(DefaultFavorites[0]);

    for (int i = 0; i < count; i++) {
        fprintf(fp, "%s\n", DefaultFavorites[i]);
    }

    fclose(fp);
}


int File_LoadFavorites(const char* filename) // rename
{
    FILE* fp = fopen(filename, "r");

    if (!fp) {
        File_CreateDefaultFavorites(filename);
        fp = fopen(filename, "r");
        if (!fp)
            return 0;
    }

    Q3_FavoriteCount = 0;
    char line[IP_LENGTH];

    while (fgets(line, sizeof(line), fp) &&
        Q3_FavoriteCount < MAX_SERVERS)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
            continue;

        snprintf(Q3_Favorites[Q3_FavoriteCount],
            IP_LENGTH,
            "%s",
            line);

        Q3_FavoriteCount++;
    }

    fclose(fp);
    return Q3_FavoriteCount;
}


// Writes list of servers to local .cfg file
int File_WriteMasterList(char answer[ANSWER_SIZE], int answerSize)
{
    if (answerSize >= ANSWER_SIZE)
        answerSize = ANSWER_SIZE - 1;

    // Null-terminate the response
    answer[answerSize] = '\0';

    const char* filename = Q3_FILENAME;
    FILE* fp = fopen(filename, "w");

    if (!fp)
        return 0;

    for (int i = 0; i < MAX_SERVERS; i++)
    {
        size_t out_size = 7;  // #define
        unsigned char octet[7];
        const char* start = strchr(answer, '\\') + (out_size * i);
        const char* end = strchr(start + 1, '\\');

        size_t len = end - (start + 1);
        if (len >= out_size)
            len = out_size - 1;

        memcpy(octet, start + 1, out_size);
        octet[len] = '\0';

        if (octet[0] == 204 && octet[1] == 204 && octet[2] == 204 && octet[3] == 204)
            break;

        fprintf(fp, "%d.", octet[0]);
        fprintf(fp, "%d.", octet[1]);
        fprintf(fp, "%d.", octet[2]);
        fprintf(fp, "%d:", octet[3]);

        // Calculation between bytes 5 and 6 to get port number
        int port = octet[4] * 256 + octet[5];

        fprintf(fp, "%d", port);

        fprintf(fp, "\n\0");
    }

    fclose(fp);

    return 1;
}


int File_LoadSettings(const char* filename, struct ApplicationSettings* settings)
{
    FILE* f = fopen(filename, "r");

    if (!f)
    {
        if (File_WriteDefaultSettings(filename)) 
        {
            //printf("Created default configuration file.\n");
            return 1;
        }
        else
        {
            printf("Error opening lamespy.cfg for writing!\n");
            return 0;
        }
    }

    char line[256];

    /* Set defaults in case lines are missing */
    settings->SoundsEnabled = 0;
    settings->RefreshOnStart = 0;
    settings->ConsoleOnStart = 0;

    while (fgets(line, sizeof(line), f)) {
        char key[64];
        int value;

        /* Parse: <key> <int> */
        if (sscanf(line, "%63s %d", key, &value) != 2)
            continue;

        if (strcmp(key, "SoundsEnabled") == 0) {
            settings->SoundsEnabled = value;
        }
        else if (strcmp(key, "RefreshOnStart") == 0) {
            settings->RefreshOnStart = value;
        }
        else if (strcmp(key, "ConsoleOnStart") == 0) {
            settings->ConsoleOnStart = value;
        }
    }

    fclose(f);
    return 1; // success
}


// Currently only used by console version
int File_CheckSettings(const char* filename)
{
    if (!File_LoadSettings(filename, &LamespySettings))
    {
        printf("Failed to load lamespy.cfg\n");
        return 0;
    }
    else
    {
        printf("Loaded settings.\n\n");
        return 1;
    }
}


void File_PrintServerList()
{
    const char* filename = Q3_FILENAME;
    FILE* fp = fopen(filename, "r");

    if (!fp) {
        perror("fopen");
        printf("%s not found! Type 'fetch' to download a new server list.\n", filename);
        return;
    }

    char buffer[1024];

    while (fgets(buffer, sizeof buffer, fp)) {
        printf("%s", buffer);
    }

    fclose(fp);
}


int File_WriteDefaultSettings(const char* filename)
{
    FILE* fp = fopen(filename, "w");

    if (!fp) {
        perror("fopen");
        return 0;
    }

    // Set default settings
    LamespySettings.ConsoleOnStart = 0;
    LamespySettings.RefreshOnStart = 1;
    LamespySettings.SoundsEnabled = 0;

    char console_on_start[4];
    char refresh_on_start[4];
    char sounds_enabled[4];
    snprintf(console_on_start, sizeof(console_on_start), "%d", LamespySettings.ConsoleOnStart);
    snprintf(refresh_on_start, sizeof(refresh_on_start), "%d", LamespySettings.RefreshOnStart);
    snprintf(sounds_enabled, sizeof(sounds_enabled), "%d", LamespySettings.SoundsEnabled);

    fprintf(fp, "ConsoleOnStart %s\n", console_on_start);
    fprintf(fp, "RefreshOnStart %s\n", refresh_on_start);
    fprintf(fp, "SoundsEnabled %s\n", sounds_enabled);

    fclose(fp);

    return 1;
}


int File_WriteDefaultFavorites(const char* filename)
{
    FILE* fp = fopen(filename, "w");

    if (!fp) {
        perror("fopen");
        return 0;
    }

    // Set default settings
    LamespySettings.ConsoleOnStart = 0;
    LamespySettings.RefreshOnStart = 1;
    LamespySettings.SoundsEnabled = 0;

    char console_on_start[4];
    char refresh_on_start[4];
    char sounds_enabled[4];
    snprintf(console_on_start, sizeof(console_on_start), "%d", LamespySettings.ConsoleOnStart);
    snprintf(refresh_on_start, sizeof(refresh_on_start), "%d", LamespySettings.RefreshOnStart);
    snprintf(sounds_enabled, sizeof(sounds_enabled), "%d", LamespySettings.SoundsEnabled);

    fprintf(fp, "ConsoleOnStart %s\n", console_on_start);
    fprintf(fp, "RefreshOnStart %s\n", refresh_on_start);
    fprintf(fp, "SoundsEnabled %s\n", sounds_enabled);

    fclose(fp);

    return 1;
}


int File_DeleteServerList()
{
    if (!DeleteFileW(L"q3servers.cfg"))
        return 0;
    else
        return 1;
}


void InitServerLists()
{
    if (File_LoadFavorites("q3favorites.cfg") == 0)
    {
        printf("No favorites loaded.\n");
    }
    else
    {
        printf("%d favorites loaded.\n", Q3_FavoriteCount);
    }

    //if (LoadMasterServerList("q3servers.cfg") == 0)
    if (New_LoadMasterServerList("q3servers.cfg") == 0)
    {
        printf("No public servers loaded. Type 'fetch' to download public server list.\n");
    }
    else
    {
        printf("%d public servers loaded.\n", Q3_ServerCount);
        Print_LineBreak(2);
    }
}