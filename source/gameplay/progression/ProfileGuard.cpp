/**
 * Profile guard for scripted runs (original: zFe MemoryCard::Save 0x5188f0, Load 0x5189f0, Delete 0x518ac0). Not in
 * the original game.
 *
 *     -set Debug.Profiles=test,testrl
 *
 * names the only profiles the game may load, save or delete. Asked for another (a scripted menu path that went
 * astray onto someone's own profile), the game logs it and quits at once, before the memory card task starts, so
 * nothing is read or written. Without the setting nothing changes.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"

NFS_REPLACE(0x5188f0, GuardedMemoryCard::Save)
NFS_REPLACE(0x5189f0, GuardedMemoryCard::Load)
NFS_REPLACE(0x518ac0, GuardedMemoryCard::Delete)

namespace
{
const UINT kRefusedExitCode = 3;

char g_allowed[256];
int g_loaded;

/** Whether the profile may be touched: always without Debug.Profiles, else when it is in the list. */
bool Allowed(const char *name)
{
    if (!g_loaded)
    {
        nfs::config::String("Debug", "Profiles", "", g_allowed, sizeof(g_allowed));
        g_loaded = 1;
    }
    if (!g_allowed[0] || !name)
        return true;
    for (const char *p = g_allowed; *p;)
    {
        char token[32];
        int n = 0;
        for (; *p && *p != ','; ++p)
        {
            if (n < static_cast<int>(sizeof(token)) - 1)
                token[n++] = *p;
        }
        token[n] = 0;
        if (*p == ',')
            ++p;
        if (!lstrcmpiA(token, name))
            return true;
    }
    return false;
}

void Refuse(const char *what, const char *name)
{
    nfs::Log("profile guard: refusing to %s profile %s (Debug.Profiles=%s), quitting", what, name, g_allowed);
    ExitProcess(kRefusedExitCode);
}
} // namespace

struct GuardedMemoryCard
{
    void Save(const char *name);
    void Load(const char *name);
    void Delete(const char *name);
};

void GuardedMemoryCard::Save(const char *name)
{
    if (!Allowed(name))
        Refuse("save", name);
    NFS_ORIGINAL(0x5188f0, void(__thiscall *)(GuardedMemoryCard *, const char *))(this, name);
}

void GuardedMemoryCard::Load(const char *name)
{
    if (!Allowed(name))
        Refuse("load", name);
    NFS_ORIGINAL(0x5189f0, void(__thiscall *)(GuardedMemoryCard *, const char *))(this, name);
}

void GuardedMemoryCard::Delete(const char *name)
{
    if (!Allowed(name))
        Refuse("delete", name);
    NFS_ORIGINAL(0x518ac0, void(__thiscall *)(GuardedMemoryCard *, const char *))(this, name);
}
