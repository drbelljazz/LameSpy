#include "lamespy.h"

// 
// Original server query method, based on global arrays
//

struct OldPlayerList // rename
{
    char PlayerName[32];
    int Score;
    int Ping;
    int Time;
};

struct OldPlayerList players[MAX_PLAYERS];
char SelectedServer[NUMSTATS][LENGTH];

void GenericServerBrowserMenu(char serverlist[][IP_LENGTH], int count, int list_type)
{
    int selected = 0;       /* absolute index */
    int page = 0;
    int pages = (count + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE;
    int running = 1;

    while (running)
    {
        system("cls");

        int start = page * SERVERS_PER_PAGE;
        int end = start + SERVERS_PER_PAGE;
        if (end > count)
            end = count;

        switch (list_type)
        {
        case PUBLIC_LIST:
            printf("=== QUAKE 3 SERVERS ===  Page %d/%d\n\n",
                page + 1, pages);
            break;

        case FAVORITES_LIST:
            printf("=== FAVORITE QUAKE 3 SERVERS ===  Page %d/%d\n\n",
                page + 1, pages);
            break;
        }

        for (int i = start; i < end; i++)
        {
            if (i == selected)
                printf(" > %s\n", serverlist[i]);
            else
                printf("   %s\n", serverlist[i]);
        }

        printf("\n( Up/Down = Navigate | Enter = Query | J = Join | ESC = Close ) ");

        int ch = _getch();

        if (ch == 0 || ch == 224)
        {
            ch = _getch();

            if (ch == KEY_UP)
            {
                if (selected > 0)
                    selected--;

                if (selected < start)
                    page--;
            }
            else if (ch == KEY_DOWN)
            {
                if (selected < count - 1)
                    selected++;

                if (selected >= end)
                    page++;
            }
        }
        else
        {
            switch (ch)
            {
            case KEY_ENTER:
            {
                char ip[64];
                int port;

                if (sscanf(serverlist[selected], "%63[^:]:%d", ip, &port) == 2)
                    Net_ManualQuery(ip, port);

                running = 0;
                break;
            }

            case KEY_J:
            case KEY_J_CAP:
            {
                char ip[64];
                int port;

                if (sscanf(serverlist[selected], "%63[^:]:%d", ip, &port) == 2)
                {
                    LaunchQ3(ip, port);
                    running = 0;
                }

                running = 0;
                break;
            }

            case 'q':
            case 'Q':
            case KEY_ESC:
                running = 0;
                break;
            }
        }

        /* Clamp page */
        if (page < 0)
            page = 0;
        if (page >= pages)
            page = pages - 1;
    }
}


// Legacy method of info received from game server in the SelectedServer[] array
int BuildServerArray(const char* info, const char* player_list, const char* server_ip, int port)
{
    char buf[64];

    // Reset the SelectedServer array
    memset(SelectedServer, 0, sizeof(SelectedServer));

    /* Helper macro to reduce repetition */
#define COPY_KEY(dst, key) \
    do { \
        const char *v = GetKeyValue(info, key, buf, sizeof(buf)); \
        if (v) strncpy(dst, v, LENGTH - 1); \
    } while (0)

    COPY_KEY(SelectedServer[SERVER_NAME], "sv_hostname");
    StripQ3Colors(SelectedServer[SERVER_NAME]);

    COPY_KEY(SelectedServer[CURRENT_MAP], "mapname");
    COPY_KEY(SelectedServer[MAXPLAYERS], "sv_maxclients");
    COPY_KEY(SelectedServer[FRAGLIMIT], "fraglimit");
    COPY_KEY(SelectedServer[TIMELIMIT], "timelimit");
    COPY_KEY(SelectedServer[CAPTURELIMIT], "capturelimit");
    COPY_KEY(SelectedServer[GAMETYPE], "g_gametype");
    COPY_KEY(SelectedServer[GAMENAME], "gamename");
    COPY_KEY(SelectedServer[MINPLAYERS], "bot_minplayers");
    COPY_KEY(SelectedServer[PUNKBUSTER], "sv_punkbuster");
    COPY_KEY(SelectedServer[PROTOCOL], "protocol");
    COPY_KEY(SelectedServer[VERSION], "version");
    COPY_KEY(SelectedServer[ALLOW_DOWNLOAD], "sv_allowDownload");
    COPY_KEY(SelectedServer[MAX_PING], "sv_maxPing");
    COPY_KEY(SelectedServer[MIN_PING], "sv_minPing");
    COPY_KEY(SelectedServer[MAX_RATE], "sv_maxRate");
    COPY_KEY(SelectedServer[PRIVATE_CLIENTS], "sv_privateClients");
    COPY_KEY(SelectedServer[MAX_GAMECLIENTS], "g_maxGameClients");

    COPY_KEY(SelectedServer[ADMIN], "Administrator");
    COPY_KEY(SelectedServer[EMAIL], "Email");
    COPY_KEY(SelectedServer[WEBSITE], "URL");
    COPY_KEY(SelectedServer[LOCATION], "Location");
    COPY_KEY(SelectedServer[CPU], "CPU");

#undef COPY_KEY

    /* Parse player list */
    int num_players = 0;
    if (player_list && *player_list)
    {
        num_players = Old_SavePlayerList(player_list, players, MAX_PLAYERS);
    }

    snprintf(SelectedServer[ACTIVE_PLAYERS],
        sizeof(SelectedServer[ACTIVE_PLAYERS]),
        "%d", num_players);

    /* Ping */
    int ping = Net_PingServer(server_ip, PING_NOTEXT);
    //int ping = 69;
    if (ping < 0)
    {
        snprintf(SelectedServer[SERVER_PING],
            sizeof(SelectedServer[SERVER_PING]),
            "Failed (ICMP Blocked)");
    }
    else
    {
        snprintf(SelectedServer[SERVER_PING],
            sizeof(SelectedServer[SERVER_PING]),
            "%d ms", ping);
    }


    /* Address */
    strncpy(SelectedServer[IP_ADDRESS], server_ip,
        sizeof(SelectedServer[IP_ADDRESS]) - 1);

    snprintf(SelectedServer[UDP_PORT],
        sizeof(SelectedServer[UDP_PORT]),
        "%d", port);

    return 1;
}


void Print_ServerInfo(char server_answer[ANSWER_SIZE])
{
    // Print the response (for debugging)
    printf("\n\n=== Raw Server Response ===\n\n");
    printf("%s\n\n", server_answer);

    printf("\n=== Server Info ===\n\n");

    printf("Server name: %s\n", SelectedServer[SERVER_NAME]);
    printf("Current map: %s\n", SelectedServer[CURRENT_MAP]);
    printf("Players: %s/%s\n", SelectedServer[ACTIVE_PLAYERS], SelectedServer[MAXPLAYERS]);

    Print_LineBreak(1);

    printf("Frag Limit: %s\n", SelectedServer[FRAGLIMIT]);
    printf("Time Limit: %s\n", SelectedServer[TIMELIMIT]);

    // Only show capture limit for CTF games
    if (!strcmp(SelectedServer[GAMETYPE], "4"))
        printf("Capture Limit: %s\n", SelectedServer[CAPTURELIMIT]);

    int gametype;
    //string_to_int(SelectedServer[GAMETYPE], &gametype);
    gametype = atoi(SelectedServer[GAMETYPE]);
    printf("Gametype: %s\n", DecodeGameType(gametype));

    //printf("DM Flags: %s\n", SelectedServer[DMFLAGS]);

    Print_LineBreak(1);

    printf("Gamename: %s\n", SelectedServer[GAMENAME]);
    printf("Minumum players: %s\n", SelectedServer[MINPLAYERS]);
    printf("Punkbuster: %s\n", SelectedServer[PUNKBUSTER]);
    printf("Protocol: %s\n", SelectedServer[PROTOCOL]);
    printf("Build: %s\n", SelectedServer[VERSION]);

    Print_LineBreak(1);

    printf("Allow Download: %s\n", SelectedServer[ALLOW_DOWNLOAD]);
    //printf("Needs Password: %s\n", SelectedServer[NEEDS_PASSWORD]);
    printf("Minimum Ping: %s\n", SelectedServer[MIN_PING]);
    printf("Maximum Ping: %s\n", SelectedServer[MAX_PING]);
    printf("Maximum Rate: %s\n", SelectedServer[MAX_RATE]);
    printf("Flood Protection: %s\n", SelectedServer[FLOOD_PROTECT]);
    printf("Private Clients: %s\n", SelectedServer[PRIVATE_CLIENTS]);
    printf("Max Game Clients: %s\n", SelectedServer[MAX_GAMECLIENTS]);

    Print_LineBreak(1);

    printf("Administrator: %s\n", SelectedServer[ADMIN]);
    printf("E-mail: %s\n", SelectedServer[EMAIL]);
    printf("Website: %s\n", SelectedServer[WEBSITE]);
    printf("Location: %s\n", SelectedServer[LOCATION]);
    printf("CPU: %s\n", SelectedServer[CPU]);

    Print_LineBreak(1);

    printf("IP Address: %s:%s\n", SelectedServer[IP_ADDRESS], SelectedServer[UDP_PORT]);

    printf("Ping: %s\n", SelectedServer[SERVER_PING]);

    // Legacy player list
    Old_PrintPlayerList();
}


// Parse text received from master server
void Print_MasterServerInfo(char master_answer[ANSWER_SIZE], int answerSize)
{
    if (answerSize >= ANSWER_SIZE)
        answerSize = ANSWER_SIZE - 1;

    // Null-terminate the response
    master_answer[answerSize] = '\0';

    printf("\n=== Master Server List ===\n\n");

    for (int i = 0; i < MAX_SERVERS; i++)
    {
        size_t out_size = 7;  // #define
        unsigned char octet[7];
        const char* start = strchr(master_answer, '\\') + (out_size * i);
        const char* end = strchr(start + 1, '\\');

        size_t len = end - (start + 1);
        if (len >= out_size)
            len = out_size - 1;

        memcpy(octet, start + 1, out_size);
        octet[len] = '\0';

        if (octet[0] == 204 && octet[1] == 204 && octet[2] == 204 && octet[3] == 204)
            break;

        // Print it to stdout (console only)
        printf("%d.", octet[0]);
        printf("%d.", octet[1]);
        printf("%d.", octet[2]);
        printf("%d:", octet[3]);

        // Calculation between bytes 5 and 6 to get port number
        int port = octet[4] * 256 + octet[5];

        printf("%d", port);

        Print_LineBreak(1);
    }
}


int Old_PrintPlayerList()
{
    printf("\n\n=== Player List ===\n\n");

    int count;

    //string_to_int(SelectedServer[ACTIVE_PLAYERS], &count);
    count = atoi(SelectedServer[ACTIVE_PLAYERS]);

    for (int i = 0; i < count; i++)
    {
        StripQ3Colors(players[i].PlayerName);

        printf("%-16s  Score: %4d  Ping: %4d  Time: %4d\n",
            players[i].PlayerName,
            players[i].Score,
            players[i].Ping,
            players[i].Time);
    }

    return 1;
}


// Extracts player list into its own string
char* SplitAnswer(char* answer, int answerSize)
{
    if (answerSize >= ANSWER_SIZE)
        answerSize = ANSWER_SIZE - 1;

    /* Always null-terminate UDP data */
    answer[answerSize] = '\0';

    /* === SPLIT RESPONSE HERE === */
    char* info;
    char* player_list;

    /* Find first newline (after statusResponse) */
    info = strchr(answer, '\n');
    if (!info)
    {
        //closesocket(sock);
        return 0;
    }
    info++;

    /* Find second newline (after key/value string) */
    player_list = strchr(info, '\n');
    if (!player_list)
    {
        //closesocket(sock);
        return 0;
    }

    /* Terminate info string */
    *player_list = '\0';
    player_list++;

    return player_list;
}





int Old_SavePlayerList(const char* player_list, struct OldPlayerList* players, int max_players)
{
    int count = 0;
    const char* p = player_list;

    while (*p && count < max_players)
    {
        int score, ping;
        char name[32];

        /* Parse one player line */
        int parsed = sscanf(p, "%d %d \"%31[^\"]\"", &score, &ping, name);
        if (parsed != 3)
            break;

        players[count].Score = score;
        players[count].Ping = ping;
        players[count].Time = 0;   /* Q3 doesn't send time here */
        strncpy(players[count].PlayerName, name,
            sizeof(players[count].PlayerName) - 1);

        count++;

        /* Move to next line */
        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }

    return count;
}


const char* GetKeyValue(const char* info, const char* key, char* out, size_t outSize)
{
    if (!info || !key || !out || outSize == 0)
        return NULL;

    // TODO: special case parse for g_needpass since its value does not end with '\\'

    char pattern[64];
    _snprintf(pattern, sizeof(pattern), "\\%s\\", key);

    const char* pos = strstr(info, pattern);
    if (!pos)
        return NULL;

    pos += strlen(pattern);

    size_t i = 0;
    while (*pos && *pos != '\\' && i < outSize - 1) {
        out[i++] = *pos++;
    }

    out[i] = '\0';
    return out;
}


int Net_ManualQuery(const char* server_ip, int port)
{
    SOCKET sock;
    struct sockaddr_in serverAddr;
    int serverAddrLen = sizeof(serverAddr);
    int querySize, answerSize;

    fd_set readfds;
    struct timeval tv;

    char server_answer[ANSWER_SIZE];

    //if (!Net_InitSockets())
    //    return 0;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return 0;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(server_ip);

    //printf("\n === Server Query ===\n\n");

    //printf("Sending query to server: ");

    querySize = sendto(sock,
        QUERY_STRING,
        (int)strlen(QUERY_STRING),
        0,
        (struct sockaddr*)&serverAddr,
        serverAddrLen);

    if (querySize == SOCKET_ERROR)
    {
        printf("Failed (%d)\n", WSAGetLastError());
        closesocket(sock);
        return 0;
    }

    // printf("Success!\n");
    // printf("Waiting for server response: ");

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec = 3;
    tv.tv_usec = 0;

    int ret = select(0, &readfds, NULL, NULL, &tv);

    if (ret == 0)
    {
        printf("\nNo response from server.\n");
        closesocket(sock);
        return 0;
    }
    else if (ret == SOCKET_ERROR)
    {
        printf("select failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return 0;
    }

    answerSize = recvfrom(sock,
        server_answer,
        ANSWER_SIZE,
        0,
        (struct sockaddr*)&serverAddr,
        &serverAddrLen);

    if (answerSize == SOCKET_ERROR)
    {
        printf("recvfrom failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return 0;
    }

    closesocket(sock);

    //printf("Success!\n\n");   

    // Legacy array-based querying method
    char* player_list = SplitAnswer(server_answer, answerSize);
    BuildServerArray(server_answer, player_list, server_ip, port);
    Print_ServerInfo(server_answer);  // TODO: add input to toggle stdout text

    printf("\n[Type 'join' to connect to this server]\n\n");

    return 1;
}


const char* DecodeGameType(int gametype)
{
    switch (gametype)
    {
    case 0: return "Deathmatch";
    case 1: return "Tournament";
    case 2: return "Singleplayer";
    case 3: return "Team Deathmatch";
    case 4: return "Capture the Flag";
    default: return "Other";
    }
}


const char* DecodeProtocol(const char* protocol)
{
    if (!strcmp(protocol, "66"))
        return "Quake 3 - v1.30";

    if (!strcmp(protocol, "67"))
        return "Quake 3 - v1.31";

    if (!strcmp(protocol, "68"))
        return "Quake 3 - v1.32";

    return "Unknown";
}


const char* YesOrNo(int num)
{
    if (num == 0)
        return "No";

    if (num == 1)
        return "Yes";

    return "(unknown)";
}


int string_to_int(const char* s, int* out)
{
    char* end;
    long val;

    errno = 0;
    val = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0' ||
        val < INT_MIN || val > INT_MAX) {
        return 0; // failure
    }

    *out = (int)val;
    return 1; // success
}