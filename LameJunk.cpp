
//
//  Old code
//


#if 0
void New_ServerBrowserMenu(Q3Server servers[], int count)
{
    int selected = 0;
    int page = 0;

    for (;;)
    {
        system("cls");

        /* Build list of reachable servers */
        int visible_indices[MAX_SERVERS];
        int visible_count = 0;

        for (int i = 0; i < count; i++)
        {
            if (!servers[i].failed)
            {
                visible_indices[visible_count++] = i;
            }
        }

        if (visible_count == 0)
        {
            printf("No reachable servers.\n\nPress ESC to exit.\n");

            if (_getch() == KEY_ESC)
                return;

            continue;
        }

        int pages = (visible_count + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE;

        if (page >= pages) page = pages - 1;
        if (page < 0) page = 0;

        int start = page * SERVERS_PER_PAGE;
        int end = start + SERVERS_PER_PAGE;
        if (end > visible_count) end = visible_count;

        printf("Building server list...\n\n\n");

        /* Auto-query visible servers */
        for (int i = start; i < end; i++)
        {
            Q3Server* s = &servers[visible_indices[i]];

            if (!s->queried)
            {
                s->queried = 1;
                New_Net_QueryServer(s);
            }
        }

        printf("=== QUAKE 3 SERVERS === Page %d/%d\n\n",
            page + 1, pages);

        for (int i = start; i < end; i++)
        {
            int idx = visible_indices[i];
            Q3Server* s = &servers[idx];

            printf(i == selected ? " > " : "   ");

            if (!s->hostname[0])
            {
                printf("(querying...) %s:%d\n", s->ip, s->port);
            }
            else
            {
                printf("%s\n", s->hostname);
            }
        }

        printf("\nUp/Down = Navigate | Enter = Info | J = Join | ESC = Close\n");

        int ch = _getch();

        if (ch == 0 || ch == 224)
        {
            ch = _getch();

            if (ch == KEY_UP && selected > 0)
            {
                selected--;
                if (selected < start)
                    page--;
            }
            else if (ch == KEY_DOWN && selected < visible_count - 1)
            {
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
                Q3Server* s = &servers[visible_indices[selected]];

                if (!s->failed)
                {
                    if (New_PrintServerInfo(s))  /* returns 1 if join */
                        return;
                }
                break;
            }

            case KEY_J:
            case KEY_J_CAP:
            {
                Q3Server* s = &servers[visible_indices[selected]];
                LaunchQ3(s->ip, s->port);
                return;
            }

            case KEY_ESC:
                return;
            }
        }
    }
}
#endif


#if 0
void QueryServerIfNeeded(Q3Server* s)
{
    if (s->queried)
        return;

    if (Net_QueryServer(s->ip, s->port))
    {
        strncpy(s->hostname, SelectedServer[SERVER_NAME], sizeof(s->hostname) - 1);
    }

    s->queried = 1;
}
#endif


#if 0
void PrintPlayerList(const Q3Server* server)
{
    printf("\n=== Players (%d) ===\n\n", server->num_players);

    for (int i = 0; i < server->num_players; i++)
    {
        const Q3Player* p = &server->players[i];

        printf("%-20s  Score:%4d  Ping:%4d  Time:%4d\n",
            p->name, p->score, p->ping, p->time);
    }
}
#endif



#if 0
void PrintServersMenu(int selected) // add PGUP/PGDN
{
    system("cls");

    int page = selected / SERVERS_PER_PAGE;
    int start = page * SERVERS_PER_PAGE;
    int end = start + SERVERS_PER_PAGE;

    if (end > Q3_ServerCount)
        end = Q3_ServerCount;

    printf("Server Browser (Page %d of %d)\n",
        page + 1,
        (Q3_ServerCount + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);

    printf("---------------------------------\n\n");

    for (int i = start; i < end; i++)
    {
        if (i == selected)
            printf("> %s\n", Q3_Servers[i]);
        else
            printf("  %s\n", Q3_Servers[i]);
    }

    printf("\nUP/DOWN select, ENTER query, ESC exit\n\n");
}


void ServerBrowserMenu(void)
{
    int selected = 0;       /* absolute index */
    int page = 0;
    int pages = (Q3_ServerCount + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE;
    int running = 1;

    while (running)
    {
        system("cls");

        int start = page * SERVERS_PER_PAGE;
        int end = start + SERVERS_PER_PAGE;
        if (end > Q3_ServerCount)
            end = Q3_ServerCount;

        printf("=== QUAKE 3 SERVERS ===  Page %d/%d\n\n",
            page + 1, pages);

        for (int i = start; i < end; i++)
        {
            if (i == selected)
                printf(" > %s\n", Q3_Servers[i]);
            else
                printf("   %s\n", Q3_Servers[i]);
        }

        printf("\nUp/Down = Navigate | Enter = Query | J = Join | ESC = Close\n");

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
                if (selected < Q3_ServerCount - 1)
                    selected++;

                if (selected >= end)
                    page++;
            }
            /* else if (ch == KEY_PGUP)   // ask why this isn't working right
             {
                 selected -= SERVERS_PER_PAGE;
                 if (selected < 0)
                     selected = 0;
             }
             else if (ch == KEY_PGDN)
             {
                 selected += SERVERS_PER_PAGE;
                 if (selected >= Q3_ServerCount)
                     selected = Q3_ServerCount - 1;
             }*/
        }
        else
        {
            switch (ch)
            {
            case KEY_ENTER:
            {
                char ip[64];
                int port;

                if (sscanf(Q3_Servers[selected], "%63[^:]:%d", ip, &port) == 2)
                    Net_QueryServer(ip, port);

                running = 0;
                break;
            }

            case KEY_J:
            case KEY_J_CAP:
            {
                char ip[64];
                int port;

                if (sscanf(Q3_Servers[selected], "%63[^:]:%d", ip, &port) == 2)
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


void PrintFavoritesMenu(int selected)
{
    system("cls");

    printf("Favorite Servers\n");
    printf("----------------\n\n");

    for (int i = 0; i < Q3_FavoriteCount; i++)
    {
        if (i == selected)
            printf("> %s\n", Q3_Favorites[i]);
        else
            printf("  %s\n", Q3_Favorites[i]);
    }

    printf("\nUse UP/DOWN to select, ENTER to query, ESC to exit\n");
}


void FavoritesMenu(void)
{
    int selected = 0;
    int ch;

    if (Q3_FavoriteCount == 0)
        return;

    PrintFavoritesMenu(selected);

    while (1)
    {
        ch = _getch();

        if (ch == 0 || ch == 224)
        {
            ch = _getch();

            if (ch == KEY_UP)
            {
                if (selected > 0)
                    selected--;
            }
            else if (ch == KEY_DOWN)
            {
                if (selected < Q3_FavoriteCount - 1)
                    selected++;
            }
            else if (ch == KEY_PGUP)
            {
                selected -= SERVERS_PER_PAGE;
                if (selected < 0)
                    selected = 0;
            }
            else if (ch == KEY_PGDN)
            {
                selected += SERVERS_PER_PAGE;
                if (selected >= Q3_FavoriteCount)
                    selected = Q3_FavoriteCount - 1;
            }
        }
        else if (ch == KEY_ENTER)
        {
            char ip[MAX_IP_LEN];
            int port;

            if (sscanf(Q3_Favorites[selected], "%63[^:]:%d", ip, &port) == 2)
            {
                Net_QueryServer(ip, port);
            }
            else
            {
                printf("Invalid server entry\n");
                _getch();
            }

            break;
        }
        else if (ch == KEY_ESC)
        {
            break;
        }

        PrintFavoritesMenu(selected);
    }
}
#endif


#if 0
void File_StoreServerList(char src[][IP_LENGTH],
    char dst[][IP_LENGTH],
    int max_servers)
{
    for (int i = 0; i < max_servers; i++) {
        if (src[i][0] == '\0') {
            dst[i][0] = '\0';   // mark end
            break;
        }
        strncpy(dst[i], src[i], IP_LENGTH - 1);
        dst[i][IP_LENGTH - 1] = '\0';  // ensure null termination
    }
}
#endif
#if 0
void File_WriteMasterList(unsigned char octet1, unsigned char octet2, unsigned char octet3, unsigned char octet4, unsigned char octet5, unsigned char octet6/*, int iteration*/)
{
    const char* filename = Q3_FILENAME;
    FILE* fp = fopen(filename, "w");

    if (!fp)
    {
        perror("fopen");
        return/* 0*/;
    }

    //for (int i = 0; i < MAX_SERVERS; i++) {
      //  if (octet1 == '\0')
      //      break;  // stop at first empty entry

        //fprintf(fp, "%s\n", DefaultQ3Servers[i]);
    fprintf(fp, "%d.", octet1);
    fprintf(fp, "%d.", octet2);
    fprintf(fp, "%d.", octet3);
    fprintf(fp, "%d:", octet4);

    // Calculation for port number
    int port = octet5 * 256 + octet6;

    fprintf(fp, "%d", port);
    fprintf(fp, "\n");
    //}

    fclose(fp);
}
#endif

#if 0
// Not done/working yet
char* File_GetSingleAddress(int line)
{
    const char* filename = Q3_FILENAME;
    FILE* fp = fopen(filename, "r");
    char address[] = "127.0.0.1:65535";

    if (!fp) {
        perror("fopen");
        return 0;
    }

    int current_line = 1;

    while (fgets(address, sizeof address, fp)) {
        if (current_line == line) {
            fclose(fp);
            return 0;   // success
        }
        current_line++;
    }

    fclose(fp);

    printf("\n%s", address);

    return address;
}
#endif

#if 0
void File_SelectServer()
{
    const char* filename = Q3_FILENAME;
    FILE* fp = fopen(filename, "r");

    //char* Current_IP;

    if (!fp) {
        perror("fopen");
        return /*1*/;
    }

    char buffer[1024];
    int count = 1;

    while (fgets(buffer, sizeof buffer, fp))
    {
        printf("(%d) %s", count, buffer);
        count++;
    }

    printf("\n(0) Go back\n\n");

    fclose(fp);

    int choice;

    while (1)
    {
        printf("\nSelect server by number: ");
        scanf("%d", &choice);
        Print_LineBreak(1);

        if (choice == 0)
            return;

        //printf("\nYou chose: %d", choice);
        //Print_LineBreak(2);

        //Current_IP = File_GetSingleAddress(choice);
        //printf("\n%s", Current_IP);

        //strcpy(Current_IP, 

        break;
    }

    //char* server = ServerList_Quake3[choice];
    //strcpy(ServerList_Quake3[choice], server);
    //printf("Current server: %s\n", server);

    //selected_server = choice;
}
#endif


#if 0
// Only called in GUI version for now
int ParseServerAnswer()
{
    int key;

    for (key = 0; key < NUMSTATS; key++)
    {
        char value[64] = { 0 };

        // Search server info string for the item we want to print
        const char* pos = strstr(server_answer, infokeys[key]);

        if (!pos)
            return 0;

        // Move past current item and the following backslash
        pos += strlen(infokeys[key]);

        if (*pos != '\\')
            return 0;

        pos++; // skip '\'

        // Copy characters until the next backslash or end of string
        size_t i = 0;

        while (*pos && *pos != '\\' && i < sizeof(value) - 1)
        {
            value[i++] = *pos++;
        }

        value[i] = '\0';

        strncpy(ServerInfo[key], value, sizeof(value));
    }

    return 1;
}
#endif

#if 0
// Keys to search for in the server answer string (move this somewhere?)
const char* infokeys[NUMSTATS] = {

    "sv_hostname",
    "mapname",

    "sv_maxclients",
    "fraglimit",
    "timelimit",
    "g_gametype",

    "gamename",
    "bot_minplayers",
    "sv_punkbuster",
    "protocol",

    "Administrator",
    "Email",
    "URL",
    "Location",
    "CPU",

    "PLAYERCOUNT_GOES_HERE",
    "PING_GOES_HERE",

    "IP_GOES_HERE",
    "PORT_GOES_HERE",
};
#endif

#if 0
// Stores info received from game server in the ServerInfo[] array
int BuildServerArray(char* info_string, const char* server_ip, int port)
{
    for (int key = SERVER_NAME; key < ACTIVE_PLAYERS; key++)  // We write IP and port manually below
    {
        char value[64] = { 0 };

        // Search server info string for the item we want to print
        const char* pos = strstr(server_answer, infokeys[key]);

        if (!pos)
            return 0;

        // Move past current item and the following backslash
        pos += strlen(infokeys[key]);

        if (*pos != '\\')
            return 0;

        pos++; // skip '\'

        // Copy characters until the next backslash or end of string
        size_t i = 0;

        while (*pos && *pos != '\\' && i < sizeof(value) - 1)
        {
            value[i++] = *pos++;
        }

        value[i] = '\0';

        strncpy(ServerInfo[key], value, sizeof(value));
    }

    // Get number of active players and store it in player struct
    int num_players = SavePlayerList(server_answer, players, MAX_PLAYERS);
    char playernum[4];
    snprintf(playernum, sizeof(playernum), "%d", num_players);
    strncpy(ServerInfo[ACTIVE_PLAYERS], playernum, sizeof(ServerInfo[ACTIVE_PLAYERS]));

    // Ping field
    int ping_int = Net_PingServer(TEST_IP, PING_NOTEXT);
    char ping_string[8];
    snprintf(ping_string, sizeof(ping_string), "%d ms", ping_int);
    strncpy(ServerInfo[SERVER_PING], ping_string, sizeof(ServerInfo[SERVER_PING]));

    // IP address field
    strncpy(ServerInfo[IP_ADDRESS], server_ip, sizeof(ServerInfo[IP_ADDRESS]));

    // Port number field
    char udp_port[8];
    snprintf(udp_port, sizeof(udp_port), "%d", port);
    strncpy(ServerInfo[UDP_PORT], udp_port, sizeof(ServerInfo[UDP_PORT]));

    return 1;
}
#endif



#if 0
// For console output only
const char* infolabels[NUMSTATS] = {

    "Server name:",
    "Current map:",

    "MaxPlayers:",
    "Fraglimit:",
    "Timelimit:",
    "Gametype:",  // TODO: display dm/ctf/etc

    "Gamename:",
    "Minumum players:",
    "Punkbuster:",// TODO: display yes/no
    "Protocol:",  // TODO: display actual game/mod represented

    "Administrator:",
    "E-mail:",
    "Website:",
    "Location:",
    "CPU:",

    "Players online:",
    "Ping:",

    "IP Address:",
    "UDP Port:",
};
#endif


#if 0
void ShowServerStruct()
{
    printf("\n=== Server Info ===\n\n");

    int i;

    for (i = 0; i < NUMSTATS; i++)
        printf("%s %s\n", infolabels[i], ServerInfo[i]);
}
#endif


#if 0
// Parse text received from server (no longer called)
void Print_ServerInfo(char answer[ANSWER_SIZE], int answerSize)
{
    // Null-terminate the response
    answer[answerSize] = '\0';

    // Print the response (for debugging)
    //printf("%s\n\n", answer);

    printf("=== Server Info ===\n\n");

    // Extract info from received string
    Print_ServerSettings(answer);
    Print_PlayerList(/*answer*/);

    //printf("%s", ServerInfo[ACTIVE_PLAYERS]);

    Print_LineBreak(1);
}
#endif


#if 0
int Print_Setting(char* input, int index) // TODO: Make it return 1 for good, 0 for bad
{
    char value[64] = { 0 };

    // Search server info string for the item we want to print
    const char* pos = strstr(input, infokeys[index]);

    if (!pos) {
        printf("Key not found\n");
        return 1;
    }

    // Move past current item and the following backslash
    pos += strlen(infokeys[index]);

    if (*pos != '\\') {
        printf("Invalid format\n");
        return 1;
    }
    pos++; // skip '\'

    // Copy characters until the next backslash or end of string
    size_t i = 0;

    while (*pos && *pos != '\\' && i < sizeof(value) - 1) {
        value[i++] = *pos++;
    }
    value[i] = '\0';

    // Print the info
    printf("%s %s\n", infolabels[index], value);

    return 0;
}
#endif

#if 0
int Print_ServerSettings(char* input)
{
    printf("=== Server Info ===\n\n");

    int i;

    for (i = 0; i < NUMSTATS; i++)
    {
        Print_Setting(input, i);

        // Add specific line breaks in server info list
        if (i == LINEBREAK1 || i == LINEBREAK2 || i == LINEBREAK3)
            Print_LineBreak(1);
    }

    return 0;
}
#endif

#if 0
int Print_PlayerList(char* input)
{
    printf("\n\n=== Player List ===\n\n");

    const char* result = after_second_newline(input);

    if (result) {
        printf("%s\n", result);
    }
    else {
        printf("Less than two newlines found\n");
    }

    // TODO: Get rid of two trailing '\n's at the end of the player list string

    return 0;
}
#endif

#if 0
int Print_PlayerList(char* input)
{
    printf("\n\n=== Player List ===\n\n");

    const char* result = after_second_newline(input);

    if (result) {
        char buffer[2048];  // adjust size if needed

        // Copy safely
        snprintf(buffer, sizeof(buffer), "%s", result);

        // Strip up to two trailing '\n'
        size_t len = strlen(buffer);
        int removed = 0;

        while (len > 0 && buffer[len - 1] == '\n' && removed < 2) {
            buffer[len - 1] = '\0';
            len--;
            removed++;
        }

        printf("%s\n", buffer);


    }
    else {
        printf("Less than two newlines found\n");
    }

    return 0;
}
#endif

void RefreshServers(HWND hDlg)
{
    return;

    HWND hList = GetDlgItem(hDlg, IDC_SERVERLIST);

    LVITEM item = { 0 };

    // Main item (column 0)
    item.mask = LVIF_TEXT;
    item.iItem = 1;
    item.iSubItem = 0;
    item.pszText = (LPTSTR)TEXT("Nano's Fag Factory");

    int itemIndex = ListView_InsertItem(hList, &item);

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        1,
        (LPTSTR)TEXT("DM-Deck16"));

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        2,
        (LPTSTR)TEXT("12/16"));

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        3,
        (LPTSTR)TEXT("69.420.69.420"));
}
#endif


#if 0
void InitMainDlg(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_SERVERLIST);

    LVCOLUMN col = { 0 };
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = (LPTSTR)TEXT("Server Name");
    col.cx = 120;
    ListView_InsertColumn(hList, 0, &col);

    col.pszText = (LPTSTR)TEXT("Map");
    col.cx = 100;
    col.iSubItem = 1;
    ListView_InsertColumn(hList, 1, &col);

    col.pszText = (LPTSTR)TEXT("Players");
    col.cx = 100;
    col.iSubItem = 1;
    ListView_InsertColumn(hList, 2, &col);

    col.pszText = (LPTSTR)TEXT("IP Address");
    col.cx = 100;
    col.iSubItem = 1;
    ListView_InsertColumn(hList, 3, &col);

    col.pszText = (LPTSTR)TEXT("Port Number");
    col.cx = 100;
    col.iSubItem = 1;
    ListView_InsertColumn(hList, 4, &col);
}


void InitListBox(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_SERVERLIST);

    LVITEM item = { 0 };

    // Main item (column 0)
    item.mask = LVIF_TEXT;
    item.iItem = 1;
    item.iSubItem = 0;
    item.pszText = (LPTSTR)TEXT(" ");

    int itemIndex = ListView_InsertItem(hList, &item);

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        1,
        (LPTSTR)TEXT(" "));

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        2,
        (LPTSTR)TEXT(" "));

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        3,
        (LPTSTR)TEXT(" "));

    // Subitem (column 1)
    ListView_SetItemText(
        hList,
        itemIndex,
        4,
        (LPTSTR)TEXT(" "));
}
#endif



#if 0
void FillServerList(HWND hDlg)
{
    SERVER servers[MAX_SERVERS];

    int serverCount = File_LoadServers(
        "q3servers.cfg",
        servers,
        MAX_SERVERS
    );
#if 0
    HWND hList = GetDlgItem(hDlg, IDC_SERVERLIST);

    //LVITEM item = { 0 };
    //int itemIndex = ListView_InsertItem(hList, &item);

    TCHAR portText[16];
    wsprintf(portText, TEXT("%d"), servers[0].port);

    TCHAR ipText[16];
    //wsprintf(ipText, TEXT("%s"), servers[0].ip);

    //int itemIndex = ListView_InsertItem(hList, &item);

    //int i;
    //for (int i = 0; i < serverCount; i++)
    {
        // Subitem (column 2)
        ListView_SetItemText(
            hList,
            itemIndex,
            3,
            ipText);

        //ListView_SetItemText(hList, itemIndex, 1, portText);

            // Subitem (column 1)
        ListView_SetItemText(
            hList,
            itemIndex,
            4,
            portText);
    }
#endif

    TCHAR ipText[64];
    TCHAR portText[16];

    wsprintf(ipText, TEXT("%s"), servers[0].ip);
    wsprintf(portText, TEXT("%d"), servers[0].port);

    LVITEM item = { 0 };
    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.pszText = ipText;

    HWND hList = GetDlgItem(hDlg, IDC_SERVERLIST);

    int itemIndex = ListView_InsertItem(hList, &item);

    ListView_SetItemText(hList, itemIndex, 1, portText);
}
#endif