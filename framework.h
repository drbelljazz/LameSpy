#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN  

#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdio.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Iphlpapi.lib")

#pragma warning( disable : 4996)