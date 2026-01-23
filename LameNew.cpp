#include "lamespy.h"


DWORD WINAPI ServerQueryThread(LPVOID param) 
{
    Q3Server* server = (Q3Server*)param;

    if (Net_QueryServer(server))
        server->state = QUERY_DONE;
    else
        server->state = QUERY_FAILED;

    return 0;
}


void StartServerQuery(Q3Server* server)
{
    if (server->state == QUERY_DONE)
        return;

    server->state = QUERY_IN_PROGRESS;

    HANDLE hThread = CreateThread(
        NULL,
        0,
        ServerQueryThread,
        server,
        0,
        NULL
    );

    if (hThread)
        CloseHandle(hThread);  /* detach */
}


int New_PrintServerInfo(const Q3Server* s)
{
    system("cls");

    printf("=== SERVER INFO ===\n\n");

    // Detect if the server is running ioQuake3
    int ioQuake3 = 0;
    if (strstr(s->version, "ioq3"))
        ioQuake3 = 1;

    // Determine if it's a CTF game
    int ctf_game = 0;
    if (s->g_gametype == 4)
        ctf_game = 1;

    // Strip the color codes out of the hostname
    char hostname[HOSTNAME_LEN];
    strncpy(hostname, s->sv_hostname, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = '\0';
    StripQ3Colors(hostname);

    // Change gametype to a string
    const char* gametype;
    gametype = DecodeGameType(s->g_gametype);

    // Change 0/1 to No/Yes
    const char* password_only;
    password_only = YesOrNo(s->g_needpass);

    // Now print the info
    printf("Hostname   : %s\n", hostname[0] ? hostname : "(unknown)");
    printf("Address    : %s:%d\n", s->ip, s->port);
    printf("Ping       : %d ms\n", s->ping);
    printf("\n");
    printf("Game Type  : %s\n", gametype);
    printf("Map        : %s\n", s->mapname);
    printf("Players    : %d/%d\n", s->num_players, s->sv_maxclients);
    printf("\n");

    if (ctf_game)
    {
        printf("Capturelimit : %d\n", s->capturelimit);
        printf("Timelimit    : %d\n", s->timelimit);
    }
    else
    {
        printf("Fraglimit : %d\n", s->fraglimit);
        printf("Timelimit : %d\n", s->timelimit);
    }

    printf("\n");
    printf("Location  : %s\n", s->Location[0] ? s->Location : "(unknown)");
    printf("Password  : %s\n", password_only);

    if (!ioQuake3)
        printf("Protocol  : %d\n", s->protocol);
    
    printf("Version   : %s\n", s->version);
    printf("\n");    

    // ioQuake3-specific server info
    if (ioQuake3)
    {
        //printf("ioQ3                 : %d\n", s->ioq3);
        printf("ioQ3 Protocol        : %d\n", s->com_protocol);
        //printf("ioQ3 Legacy Protocol : %d\n", s->com_legacyprotocol);

        const char* allow_DL;
        allow_DL = YesOrNo(s->sv_allowDownload);
        printf("Allow Download       : %s\n", allow_DL);
        
        printf("Download URL         : %s\n", s->sv_dlURL[0] ? s->sv_dlURL : "(none specified)");
    }

    printf("\n");

    New_PrintPlayerList(s);

    printf("\n( J = Join | ESC = Return to Browser ) ");
    int ch = _getch();

    printf("\n");

    switch (ch)
    {
    case KEY_J:
    case KEY_J_CAP:
        LaunchQ3(s->ip, s->port);
        return 1;

    default:
        return 0;
    }
}


void New_PrintPlayerList(const Q3Server* server)
{
    printf("=== PLAYERS (%d) ===\n\n", server->num_players);

    if (server->num_players == 0)
    {
        printf("(no players connected)\n");
        return;
    }

    printf("%-20s  %5s  %5s  %5s\n",
        "Name", "Score", "Ping", "Time");
    printf("-------------------------------------------------\n");

    for (int i = 0; i < server->num_players; i++)
    {
        const Q3Player* p = &server->players[i];

        char name[32];
        strncpy(name, p->name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        /* Strip color codes for display */
        StripQ3Colors(name);

        printf("%-20s  %5d  %5d  %5d\n",
            name,
            p->score,
            p->ping,
            p->time);
    }
}


void New_ServerBrowserMenu(Q3Server servers[], int count)
{
    int selected = 0;
    int page = 0;
    int pages = (count + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE;

    for (;;)
    {
        system("cls");

        int start = page * SERVERS_PER_PAGE;
        int end = start + SERVERS_PER_PAGE;
        if (end > count) end = count;

        //printf("Building server list. This may take a moment... ");

        /* Auto-query visible servers */
        for (int i = start; i < end; i++)
        {
            if (servers[i].state == QUERY_IDLE)
            {
                StartServerQuery(&servers[i]);
                //EnqueueServer(&servers[i]);
            }
        }

        printf("\n\n=== QUAKE 3 SERVERS === Page %d/%d\n\n",
            page + 1, pages);

        for (int i = start; i < end; i++)
        {
            Q3Server* s = &servers[i];

            printf(i == selected ? " > " : "   ");

            switch (s->state)
            {
            case QUERY_IDLE:
                /* Not queried yet */
                printf("(waiting) %s:%d\n", s->ip, s->port);
                break;

            case QUERY_QUEUED:
                /* Worker thread hasn’t picked it up yet */
                printf("(queued)  %s:%d\n", s->ip, s->port);
                break;

            case QUERY_IN_PROGRESS:
                /* Actively querying */
                printf("(querying...) %s:%d\n", s->ip, s->port);
                break;

            case QUERY_FAILED:
                /* Timed out / unreachable */
                printf("(no response) %s:%d\n", s->ip, s->port);
                break;

            case QUERY_DONE:
                /* Success */
                if (s->sv_hostname[0])
                    printf("%s\n", s->sv_hostname);
                else
                    printf("(unnamed) %s:%d\n", s->ip, s->port);
                break;
            }
        }

        printf("\n ( Up/Down = Navigate | Enter = Refresh | J = Join | ESC = Close ) ");

        //int ch = _getch();

        int ch = 0;

        /* Non-blocking input + auto refresh */
        if (!_kbhit())
        {
            Sleep(50);   // ~20 FPS redraw
            continue;    // go back to top of loop
        }

        ch = _getch();   // ONLY called when a key exists

        /*
        if (_kbhit())
        {
            ch = _getch();
        }
        else
        {
            Sleep(50);   // 20 FPS refresh
            continue;    // redraw menu
        }*/

        Print_LineBreak(2);

        if (ch == 0 || ch == 224)
        {
            //ch = _getch();
            int ext = _getch();   /* ALWAYS consume it */

            //switch (ch)
            switch (ext)
            {
            case (KEY_UP/* | 0x100*/):
                if (selected > 0)
                {
                    selected--;
                    if (selected < start) page--;
                }
                break;

            case (KEY_DOWN/* | 0x100*/):
                if (selected < count - 1)
                {
                    selected++;
                    if (selected >= end) page++;
                }
                break;

            case (KEY_PGUP/* | 0x100*/):
                if (page > 0)
                {
                    page--;
                    selected = page * SERVERS_PER_PAGE;

                    if (selected >= count)
                        selected = count - 1;
                }
                break;

            case (KEY_PGDN/* | 0x100*/):
                if (page < pages - 1)
                {
                    page++;
                    selected = page * SERVERS_PER_PAGE;

                    if (selected >= count)
                        selected = count - 1;
                }
                break;

            default:
                /* swallow everything else to avoid beep */
                break;
            }
        }
        else
        {
            switch (ch)
            {
            case KEY_ENTER:
            {
                Q3Server* s = &servers[selected];

                if (s->state == QUERY_IDLE)
                {
                    StartServerQuery(s);
                    //EnqueueServer(s);
                }
                else if (s->state == QUERY_DONE)
                {
                    if (New_PrintServerInfo(s))
                        return;
                }

                break;  /* return to browser */
            }

            case KEY_J:
            case KEY_J_CAP:
            {
                Q3Server* s = &servers[selected];
                LaunchQ3(s->ip, s->port);
                return;
            }
            break;

            case KEY_ESC:
                return;

            default:
                /* swallow everything else to avoid beep */
                break;
            }
        }

        if (page < 0) page = 0;
        if (page >= pages) page = pages - 1;

    }
}


int New_SavePlayerList(const char* response,
    Q3Player* players,
    int max_players)
{
    const char* p;
    int count = 0;

    /* Find start of player list:
       statusResponse\n
       \key\value...\key\value\n
       <player lines>
    */

    p = strchr(response, '\n');
    if (!p)
        return 0;

    /* Skip server info block */
    while (*p)
    {
        if (*p == '\n' && *(p + 1) != '\\')
        {
            p++;   /* start of players */
            break;
        }
        p++;
    }

    /* Parse player lines */
    while (*p && count < max_players)
    {
        int score, ping;
        char name[64];

        /* Expected format:
           <score> <ping> "<name>"
        */

        if (sscanf(p, "%d %d \"%63[^\"]\"",
            &score, &ping, name) == 3)
        {
            Q3Player* pl = &players[count];

            strncpy(pl->name, name, sizeof(pl->name) - 1);
            pl->name[sizeof(pl->name) - 1] = '\0';

            pl->score = score;
            pl->ping = ping;
            pl->time = 0;  /* not provided by Q3 status */

            count++;
        }

        /* Move to next line */
        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }

    return count;
}


void StripQ3Colors(char* text)
{
    char* src = text;
    char* dst = text;

    while (*src)
    {
        if (*src == '^' && src[1])
        {
            src += 2; /* skip color code */
            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}


#define COPY_STRING(dst, src) \
    do { strncpy((dst), (src), sizeof(dst) - 1); (dst)[sizeof(dst) - 1] = '\0'; } while (0)

#define PARSE_INT(dst, src) \
    do { (dst) = atoi(src); } while (0)

void ParseServerInfo(const char* packet, Q3Server* server)
{
    const char* p = packet;

    /* skip connectionless header */
    if (*(int*)p == -1)
        p += 4;

    /* skip statusResponse / infoResponse */
    if (!strncmp(p, "statusResponse\n", 15))
        p += 15;
    else if (!strncmp(p, "infoResponse\n", 13))
        p += 13;
    else
        return;

    /* info string must start with '\' */
    if (*p != '\\')
        return;

    p++; /* skip first '\' */

    while (*p)
    {
        char key[64];
        char value[256];
        int k = 0, v = 0;

        while (*p && *p != '\\' && k < (int)sizeof(key) - 1)
            key[k++] = *p++;
        key[k] = '\0';

        if (*p != '\\')
            break;
        p++;

        while (*p && *p != '\\' && v < (int)sizeof(value) - 1)
            value[v++] = *p++;
        value[v] = '\0';

        if (*p == '\\')
            p++;

        /* -------- Strip Q3 color codes from hostname -------- */
        if (!strcmp(key, "sv_hostname"))
        {
            StripQ3Colors(value);

            COPY_STRING(server->sv_hostname, value);
        }

        /* ---- string fields ---- */
        else if (!strcmp(key, "mapname"))     COPY_STRING(server->mapname, value);
        else if (!strcmp(key, "version"))     COPY_STRING(server->version, value);
        else if (!strcmp(key, "g_gametype"))        PARSE_INT(server->g_gametype, value);
        else if (!strcmp(key, "Administrator")) COPY_STRING(server->Administrator, value);
        else if (!strcmp(key, "Email"))       COPY_STRING(server->Email, value);
        else if (!strcmp(key, "URL"))         COPY_STRING(server->URL, value);
        else if (!strcmp(key, "Location"))    COPY_STRING(server->Location, value);
        else if (!strcmp(key, "CPU"))         COPY_STRING(server->CPU, value);

        /* ---- integer fields ---- */
        else if (!strcmp(key, "fraglimit"))        PARSE_INT(server->fraglimit, value);
        else if (!strcmp(key, "timelimit"))        PARSE_INT(server->timelimit, value);
        else if (!strcmp(key, "capturelimit"))     PARSE_INT(server->capturelimit, value);
        else if (!strcmp(key, "sv_maxclients"))    PARSE_INT(server->sv_maxclients, value);
        else if (!strcmp(key, "sv_maxPing"))       PARSE_INT(server->sv_maxPing, value);
        else if (!strcmp(key, "sv_minPing"))       PARSE_INT(server->sv_minPing, value);
        else if (!strcmp(key, "sv_maxRate"))       PARSE_INT(server->sv_maxRate, value);
        else if (!strcmp(key, "sv_privateClients"))PARSE_INT(server->sv_privateClients, value);
        else if (!strcmp(key, "g_maxGameClients")) PARSE_INT(server->g_maxGameClients, value);
        else if (!strcmp(key, "g_needpass"))       PARSE_INT(server->g_needpass, value);
        else if (!strcmp(key, "protocol"))         PARSE_INT(server->protocol, value);

        /* ---- ioquake3 specific ---- */
        else if (!strcmp(key, "com_protocol"))       PARSE_INT(server->com_protocol, value);
        else if (!strcmp(key, "com_legacyprotocol"))PARSE_INT(server->com_legacyprotocol, value);
        else if (!strcmp(key, "ioq3"))               PARSE_INT(server->ioq3, value);
        else if (!strcmp(key, "sv_dlRate"))          PARSE_INT(server->sv_dlRate, value);
        else if (!strcmp(key, "sv_dlURL"))           COPY_STRING(server->sv_dlURL, value);
        else if (!strcmp(key, "sv_allowDownload"))  PARSE_INT(server->sv_allowDownload, value);
        else if (!strcmp(key, "sv_minRate"))         PARSE_INT(server->sv_minRate, value);
        else if (!strcmp(key, "sv_floodProtect"))    PARSE_INT(server->sv_floodProtect, value);
        else if (!strcmp(key, "sv_lanForceRate"))    PARSE_INT(server->sv_lanForceRate, value);

        // Ping the bastard
        int ping = Net_PingServer(server->ip, PING_NOTEXT);
        server->ping = ping;
    }
}