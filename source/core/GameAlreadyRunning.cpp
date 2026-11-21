#include <windows.h>
#include <tlhelp32.h>
#include <string.h>

#include "game/gen/api.h"


bool IsTheGameAlreadyRunning(const char *exe_name, int allowed_instances)
{
    int attempts = 3;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    for (;;)
    {
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        int instances = 0;
        PROCESSENTRY32 entry = {};
        entry.dwSize = sizeof(entry);
        for (BOOL ok = Process32First(snapshot, &entry); ok; ok = Process32Next(snapshot, &entry))
        {
            char base_name[MAX_PATH];
            const char *slash = strrchr(entry.szExeFile, '\\');
            strcpy(base_name, slash ? slash + 1 : entry.szExeFile);
            if (_stricmp(exe_name, base_name) == 0)
                ++instances;
        }
        CloseHandle(snapshot);

        --attempts;
        if (instances <= allowed_instances || attempts < 0)
            return allowed_instances < instances;
        Sleep(1000);
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    }
}
