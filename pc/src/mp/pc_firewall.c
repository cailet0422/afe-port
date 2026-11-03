/* pc_firewall.c - the Windows firewall rule for the multiplayer (see pc_firewall.h) */
#include "pc_firewall.h"

#ifdef _WIN32
#include <SDL.h>
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
#include <shellapi.h>

static volatile int s_state = PC_FIREWALL_IDLE;

static int SDLCALL allow_thread(void* arg) {
    wchar_t exe[MAX_PATH];
    wchar_t params[4 * MAX_PATH + 512];
    SHELLEXECUTEINFOW sei;
    DWORD code = 1;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);

    (void)arg;
    if (n == 0 || n >= MAX_PATH) {
        s_state = PC_FIREWALL_FAILED;
        return 0;
    }
    /* cmd /s /c "...": the outer quotes go, the program's stay. All the
     * game's rules first (the prompt's block rule for public networks
     * with them), then one rule allowing it everywhere. */
    _snwprintf(params, sizeof(params) / sizeof(params[0]),
               L"/s /c \"netsh advfirewall firewall delete rule name=all program=\"%ls\" >nul & "
               L"netsh advfirewall firewall add rule name=\"Dobutsu no Mori e+ (multiplayer)\" dir=in "
               L"action=allow program=\"%ls\" protocol=UDP profile=any enable=yes\"",
               exe, exe);
    params[sizeof(params) / sizeof(params[0]) - 1] = L'\0';

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        s_state = GetLastError() == ERROR_CANCELLED ? PC_FIREWALL_DECLINED : PC_FIREWALL_FAILED;
        printf("[MP] firewall: %s\n", s_state == PC_FIREWALL_DECLINED ? "not allowed (consent refused)" : "could not run netsh");
        return 0;
    }
    if (sei.hProcess != NULL) {
        WaitForSingleObject(sei.hProcess, 30000);
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
    }
    s_state = code == 0 ? PC_FIREWALL_DONE : PC_FIREWALL_FAILED;
    printf("[MP] firewall: %s (netsh %lu)\n", code == 0 ? "the game is allowed on every network" : "netsh failed",
           (unsigned long)code);
    return 0;
}

void pc_firewall_allow_start(void) {
    SDL_Thread* t;

    if (s_state == PC_FIREWALL_RUNNING) return;
    s_state = PC_FIREWALL_RUNNING;
    t = SDL_CreateThread(allow_thread, "firewall", NULL);
    if (t == NULL) {
        s_state = PC_FIREWALL_FAILED;
        return;
    }
    SDL_DetachThread(t);
}

int pc_firewall_state(void) {
    return s_state;
}

#else

void pc_firewall_allow_start(void) {
}

int pc_firewall_state(void) {
    return PC_FIREWALL_NONE;
}

#endif
