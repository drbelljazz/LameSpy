#pragma once

#include <Windows.h>

#include "LameNav.h"
#include "LameUiSession.h"

void UiMessages_OnAppMessage(
    const NavContext* navCtx,
    const UiSession* session,
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);