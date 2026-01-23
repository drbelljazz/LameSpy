#include "lamespy.h"


extern Q3Server Servers[MAX_SERVERS];

struct GameLaunchSettings
{
    char ExePath[260];     /* MAX_PATH */
    char CmdLine[512];
};

struct GameLaunchSettings Q3LaunchSettings;

extern char SelectedServer[NUMSTATS][LENGTH];

// TODO: Make this generic for all games
int LoadQ3LaunchSettings(const char* filename, struct GameLaunchSettings* cfg)
{
    FILE* fp = fopen(filename, "r");
    if (!fp)
        return 0;

    char line[1024];
    memset(cfg, 0, sizeof(*cfg));

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';

        /* Strip UTF-8 BOM */
        if ((unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
        {
            memmove(line, line + 3, strlen(line + 3) + 1);
        }

        /* Skip leading whitespace */
        char* p = line;
        while (*p == ' ' || *p == '\t')
            p++;

        if (strncmp(p, "Q3ExePath ", 10) == 0)
        {
            strncpy(cfg->ExePath, p + 10, sizeof(cfg->ExePath) - 1);
        }
        else if (strncmp(p, "Q3CmdLine ", 10) == 0)
        {
            strncpy(cfg->CmdLine, p + 10, sizeof(cfg->CmdLine) - 1);
        }
    }

    fclose(fp);

    return (cfg->ExePath[0] != '\0');
}

// TODO: Make this generic for all games
void LaunchQ3(const char* ip, int port)
{
    char cmd[1024];

    printf("\nLaunching Quake III Arena...\n\n");

    if (LamespySettings.SoundsEnabled)
        PlaySoundA("launch.wav", NULL, SND_FILENAME | SND_ASYNC);

    snprintf(cmd, sizeof(cmd),
        "\"%s\" %s +connect %s:%d",
        Q3LaunchSettings.ExePath,
        Q3LaunchSettings.CmdLine,
        ip, port);

    system(cmd);
}


// For console stdout only
int LameMain()
{
    //printf("=== Welcome to LameSpy! ===\n\n");
    printf("=== LameSpy Init ===\n\n");

    File_CheckSettings("lamespy.cfg");  // Eventually would go in WM_CREATE

    if (!LoadQ3LaunchSettings("q3path.cfg", &Q3LaunchSettings))
        printf("Failed to load Quake 3 launch settings\n");
    
    if (LamespySettings.RefreshOnStart)
        Net_DownloadMasterList();

    InitServerLists();

    Print_Help();

    //if (LamespySettings.SoundsEnabled)
    //    PlaySoundA("welcome.wav", NULL, SND_FILENAME | SND_ASYNC);

    while (1)
    {
        char userinput[CMD_SIZE];

        // Print command prompt and wait for user input
        if (!strcmp(SelectedServer[UDP_PORT], ""))
            printf("\n<LameSpyOS> ");
        else
            printf("\n<LameSpyOS ~ %s> ", SelectedServer[SERVER_NAME]);

        fgets(userinput, CMD_SIZE, stdin);
        Print_LineBreak(1);

        // User wants to view locally saved server list
        if (!strcmp(userinput, "list\n") || !strcmp(userinput, "l\n"))
        {
            File_PrintServerList();
            continue;
        }

        // Download new server list from master server
        if (!strcmp(userinput, "fetch\n") || !strcmp(userinput, "r\n"))
        {
            if (Net_DownloadMasterList())
                printf("Successfully downloaded public server list. Type 'search' to browse.\n");

            if (LamespySettings.SoundsEnabled)
                PlaySoundA("complete.wav", NULL, SND_FILENAME | SND_ASYNC);
            
            continue;
        }

        // User wants to select a server to query
        if (!strcmp(userinput, "browse\n") || !strcmp(userinput, "b\n"))
        {
            New_ServerBrowserMenu(Servers, Q3_ServerCount/*, PUBLIC_LIST*/);
            continue;
        }

        /*if (!strcmp(userinput, "browseip\n") || !strcmp(userinput, "i\n"))
        {
            GenericServerBrowserMenu(Q3_Servers, Q3_ServerCount, PUBLIC_LIST);
            continue;
        }*/

        // Ping a server
        if (strncmp(userinput, "ping", 4) == 0)
        {
            char ip[IP_LENGTH];

            /* Try to extract IP after "ping " */
            if (sscanf(userinput, "ping %63s", ip) == 1)
            {
                if (Net_PingServer(ip, PING_ALLINFO) < 0)
                {
                    printf("No response from server!\n");
                    //Net_ShutdownSockets();
                }
            }
            else
            {
                printf("Usage: ping <ip:port>\n");
            }

            continue;
        }

        // Query a server
        if (strncmp(userinput, "query", 5) == 0)
        {
            char ip[IP_LENGTH];
            int port;

            /* Try: query ip port */
            if (sscanf(userinput, "query %63s %d", ip, &port) == 2)
            {
                Net_ManualQuery(ip, port);
                //New_Net_QueryServer(ip, port);
            }

            /* Try: query ip:port */
            else if (sscanf(userinput, "query %63[^:]:%d", ip, &port) == 2)
            {
                Net_ManualQuery(ip, port);
                //New_Net_QueryServer(ip, port);
            }
            else if (strcmp(SelectedServer[UDP_PORT], ""))
            {
                int query_port;
                char query_ip[IP_LENGTH];

                //string_to_int(SelectedServer[UDP_PORT], &port_int);
                query_port = atoi(SelectedServer[UDP_PORT]);
                strncpy(query_ip, SelectedServer[IP_ADDRESS], sizeof(query_ip));

                Net_ManualQuery(query_ip, query_port);
                //New_Net_QueryServer(SelectedServer[IP_ADDRESS], port_int);
            }
            else
            {
                printf("Usage:\n\n");
                printf(" (1) query <ip> <port>\n");
                printf(" (2) query <ip>:<port>\n");

            }

            continue;
        }

        // Join currently selected server
        if (strcmp(userinput, "join\n"/*, 5*/) == 0)
        {
            if (strcmp(SelectedServer[UDP_PORT], ""))
            {
                int port;
                    
                //string_to_int(SelectedServer[UDP_PORT], &port);
                port = atoi(SelectedServer[UDP_PORT]);
                LaunchQ3(SelectedServer[IP_ADDRESS], port);

            }
            else
            {
                printf("Type 'browse' to select a server to join\n");
            }

            continue;
        }

        // Browse favorites list
        if (strcmp(userinput, "favorites\n") == 0 || strcmp(userinput, "f\n") == 0)
        {
            GenericServerBrowserMenu(Q3_Favorites, Q3_FavoriteCount, FAVORITES_LIST);
            //New_ServerBrowserMenu(Servers, Q3_ServerCount, PUBLIC_LIST);
            //FavoritesMenu();
            continue;
        }

        // Delete local master server list
        if (!strcmp(userinput, "reset\n"))
        {
            File_DeleteServerList();
            continue;
        }

        
        if (!strcmp(userinput, "clear\n"))
        {
            if (strcmp(SelectedServer[UDP_PORT], ""))
            {
                memset(SelectedServer, 0, sizeof(SelectedServer));
                printf("Cleared server selection. \n\n");
            }
            else
            {
                printf("No currently selected server. \n\n");
            }

            continue;
        }

        // List of valid commands
        if (!strcmp(userinput, "help\n"))
        {
            Print_Help();
            continue;
        }

        // 'Q' exits the program
        if (!strcmp(userinput, "q\n") || !strcmp(userinput, "quit\n") || !strcmp(userinput, "exit\n"))
            break;

        // User pressed ENTER
        if (!strcmp(userinput, "\n"))
        {
            printf("Type 'help' for a list of valid commands.\n");
            continue;
        }

        // If no known command is entered, recommend the 'help' command
        printf("Unknown command. Type 'help' for a list of valid commands.\n");

        // Clear the input string for another round
        userinput[0] = '\0';
    }

    //Net_ShutdownSockets();
    Print_LineBreak(1);

    return 0;
}


void Print_Help()
{
    printf("=== LameSpy Commands ===\n\n");

    printf("'browse'     Show public server list\n");
    printf("'favorites'  Show favorite server list\n");
    //Print_LineBreak(1);
    printf("'fetch'      Download new public server list\n");
    printf("'reset'      Clear local server list cache\n");
    //Print_LineBreak(1);
    printf("'join'       Connect to specified IP:Port or last queried server\n");
    printf("'clear'      Un-select currently selected server\n");
    //Print_LineBreak(1);
    printf("'ping'       Ping server\n");
    printf("'query'      Request server info\n");
    //Print_LineBreak(1);
    printf("'quit'       Exit\n\n");
}


void Print_LineBreak(int num)
{
    int i;

    for (i = 0; i < num; i++)
        printf("\n");
}


