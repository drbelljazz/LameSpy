#include "lamespy.h"


int Net_DownloadMasterList()
{
    SOCKET sock;
    struct sockaddr_in serverAddr;
    char master_answer[ANSWER_SIZE];
    int serverAddrLen = sizeof(serverAddr);
    int querySize, answerSize;

    // Initialize Winsock
    //if (!Net_InitSockets())
    //    return 0;

    // Create a UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return 0;
    }

    // Prepare server address structure
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(MASTERSERVER_PORT);  // TODO: variable port from cfg list
    serverAddr.sin_addr.s_addr = inet_addr(IOQUAKE3_MASTER);

    //printf("Sending query to master server: ");

    // Send query to the server
    querySize = sendto(sock, MASTERSERVER_QUERYSTRING, strlen(MASTERSERVER_QUERYSTRING), 0,
        (struct sockaddr*)&serverAddr, serverAddrLen);

    if (querySize == SOCKET_ERROR)
    {
        printf("Failed with error code: %d\n", WSAGetLastError());
        closesocket(sock);
        return 0;
    }
    else
    {
        //printf("Success!\n");
    }

    //printf("Waiting for master server response: ");

    // Wait for response from the server
    answerSize = recvfrom(sock, master_answer, ANSWER_SIZE, 0, 
        (struct sockaddr*)&serverAddr, &serverAddrLen);

    if (answerSize == SOCKET_ERROR)
    {
        printf("Failed with error code: %d\n", WSAGetLastError());
        closesocket(sock);
        return 0;
    }


    printf("Downloaded new master server list from %s\n\n", IOQUAKE3_MASTER);


    // For console stdout only
    // MOVE THIS
    //Print_MasterServerInfo(master_answer, answerSize);

    // Used by both GUI and console
    if (!File_WriteMasterList(master_answer, answerSize))
        return 0;

    //New_LoadMasterServerList("q3servers.cfg");
    File_LoadMasterList("q3servers.cfg");

    return 1;
}


int Net_QueryServer(Q3Server* server)
{
    SOCKET sock;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    char buffer[4096];
    int len;

    //server->failed = 0;
    server->sv_hostname[0] = '\0';
    server->num_players = 0;

    //if (!Net_InitSockets())
    //    return 0;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return 0;

    /* 1 second timeout */
    DWORD timeout = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
        (const char*)&timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server->port);
    addr.sin_addr.s_addr = inet_addr(server->ip);

    sendto(sock,
        QUERY_STRING,
        (int)strlen(QUERY_STRING),
        0,
        (struct sockaddr*)&addr,
        addrlen);

    len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
        (struct sockaddr*)&addr, &addrlen);

    closesocket(sock);

    if (len <= 0)
    {
        //server->failed = 1;
        return 0;
    }

    buffer[len] = '\0';

    /* -------- Parse server CVARs -------- */
    ParseServerInfo(buffer, server);

    /* -------- Parse players -------- */
    server->num_players =
        New_SavePlayerList(buffer, server->players, MAX_PLAYERS);

    //server->queried = 1;
    return 1;
}


// Server pinging 
int Net_PingServer (const char* destination, int style) 
{
    HANDLE icmp_handle = NULL;
    IPAddr dest_ip = INADDR_NONE;
    DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + sizeof(DWORD);
    VOID* reply_buffer = NULL;
    struct hostent* host;

    int latency;  // Return value

    // Initialize Winsock
    /*WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) 
    {
        fprintf(stderr, "WSAStartup failed\n");
        return -1;
    }*/

    // Resolve the destination address
    dest_ip = inet_addr(destination);

    if (dest_ip == INADDR_NONE) 
    {
        host = gethostbyname(destination);

        if (host == NULL) 
        {
            fprintf(stderr, "Could not find IP address for %s\n", destination);
            //Net_ShutdownSockets();
            return -1;
        }

        dest_ip = *(IPAddr*)host->h_addr_list[0];
    }

    // Create an ICMP handle
    icmp_handle = IcmpCreateFile();

    if (icmp_handle == INVALID_HANDLE_VALUE) 
    {
        fprintf(stderr, "Unable to open handle to ICMP.dll\n");
        //Net_ShutdownSockets();
        return -1;
    }

    // Allocate reply buffer
    reply_buffer = malloc(reply_size);

    if (reply_buffer == NULL) 
    {
        fprintf(stderr, "Memory allocation failed\n");
        IcmpCloseHandle(icmp_handle);
        //Net_ShutdownSockets();
        return -1;
    }

    // Send the ICMP echo request
    DWORD replies_count = IcmpSendEcho (
        icmp_handle,
        dest_ip,
        NULL,   // RequestData (optional data buffer)
        0,      // RequestSize (size of optional data)
        NULL,   // RequestOptions (optional IP options)
        reply_buffer,
        reply_size,
        1000    // Timeout in milliseconds
    );

    // Process the reply
    if (replies_count > 0) 
    {
        ICMP_ECHO_REPLY* reply = (ICMP_ECHO_REPLY*)reply_buffer;
        struct in_addr reply_addr;
        reply_addr.S_un.S_addr = reply->Address;

        if (reply->Status == IP_SUCCESS) 
        {
            latency = (int)reply->RoundTripTime;

            switch (style)
            {
            case PING_NOTEXT:
            default:
                break;

            case PING_IPONLY:

                printf("Server at %s: %dms\n\n",
                    inet_ntoa(reply_addr),
                    reply->RoundTripTime);

                break;

            case PING_ALLINFO:

                printf("Reply from %s: bytes=%d time=%dms TTL=%d\n\n",
                    inet_ntoa(reply_addr),
                    reply->DataSize,
                    reply->RoundTripTime,
                    reply->Options.Ttl);

                break;
            }
        }
        else 
        {
            fprintf(stderr, "No response from server. IP Status: %ld\n", reply->Status);
            return -1;
        }
    }
    else 
    {
        if (style != PING_NOTEXT)
            fprintf(stderr, "Request timed out or general error. WSAGetLastError: %d\n", WSAGetLastError());
        return -1;
    }

    free(reply_buffer);
    IcmpCloseHandle(icmp_handle);
    //Net_ShutdownSockets();

    return latency;
}


// Function to initialize Winsock
int Net_InitSockets()
{
    WSADATA wsaData;
    int wsaInitResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (wsaInitResult != 0)
    {
        printf("Winsock startup failed with error code %d\n", wsaInitResult);
        return 0;
    }

    return 1;
}


// Cleanup sockets
void Net_ShutdownSockets()
{
    WSACleanup();
}