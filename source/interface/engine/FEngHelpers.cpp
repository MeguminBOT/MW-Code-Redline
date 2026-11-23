/**
 * The screens' helpers for front-end objects (original: zFe FEngSetInvisible 0x514c70, FEngSetVisible 0x514cc0 and
 * 0x527df0, FEngSetScript 0x514d10 and 0x524920, FEngIsScriptRunning 0x514df0 and 0x524aa0, FEngIsScriptSet 0x524a10,
 * FEngSetMultiImageRot 0x514fb0, FEngSetSize 0x5155e0, FEngSetLanguageHash 0x515c00, FEngSetMovieName 0x515e60,
 * FEngSetCurrentButton 0x515e90, FEngSetButtonState 0x515f20, FEngDisableButton 0x51a1e0, FEngEnableButton 0x51a230,
 * FEngFindObject 0x524850, FEngFindGroup 0x524880, FEngFindImage 0x5251a0, FEngFindString 0x5251e0,
 * FEngMapJoyParamToJoyport 0x572ff0, FEngMapJoyportToJoyParam 0x573020, FEngSetCreateCallback 0x571f30).
 *
 * FEObject flags: 1 hidden, 0x40 kept hidden, 0x400000 changed, 0x2400000 changed and to be drawn again, 0x4000000
 * disabled. A group (type 5) keeps its children at +0x64 and their count at +0x60. FEPackage: +0x90 its buttons, +0x94
 * their count. FEString: +0x60 its label hash, flag 2 its own text.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kHidden = 1;
const unsigned int kKeptHidden = 0x40;
const unsigned int kChanged = 0x400000;
const unsigned int kRedraw = 0x2400000;
const unsigned int kDisabled = 0x4000000;
const int kGroup = 5;
const int kImage = 1;
const int kString = 2;
void **const kLabelCallback = reinterpret_cast<void **>(0x91dd08);

FEObject *FindIn(const char *package, unsigned int hash)
{
    if (!package)
        return nullptr;
    FEPackage *p = cFEng::mInstance->FindPackage(package);
    return p ? p->FindObjectByHash(hash) : nullptr;
}

FEObject *FindButton(const char *package, unsigned int hash)
{
    FEPackage *p = FEPackageManager::Get()->FindPackage(package);
    if (!p)
        return nullptr;
    FEObject *found = nullptr;
    FEObject **buttons = At<FEObject **>(p, 0x90);
    for (unsigned int i = 0, n = At<unsigned int>(p, 0x94); i < n; ++i)
        if (buttons[i]->NameHash == hash)
            found = buttons[i];
    return found;
}

bool ScriptIs(FEObject *o, unsigned int script, bool running)
{
    if (!o)
        return false;
    FEScript *current = o->pCurrentScript;
    if (!current || current->ID != script)
        return false;
    return !running || current->CurrentTime <= current->Length;
}
} // namespace

void __cdecl FEngSetInvisible(FEObject *o)
{
    if (!o)
        return;
    o->Flags |= kRedraw | kHidden;
    if (o->Type != kGroup)
        return;
    FEObject *child = At<FEObject *>(o, 0x64);
    for (int n = At<int>(o, 0x60); n > 0; --n, child = child->Next)
        FEngSetInvisible(child);
}

void __cdecl FEngSetVisible(FEObject *o)
{
    if (!o || (o->Flags & kKeptHidden))
        return;
    o->Flags = (o->Flags & ~kHidden) | kRedraw;
    if (o->Type != kGroup)
        return;
    FEObject *child = At<FEObject *>(o, 0x64);
    for (int n = At<int>(o, 0x60); n > 0; --n, child = child->Next)
        FEngSetVisible(child);
}

void __cdecl FEngSetVisible(char const *package, unsigned int hash)
{
    FEngSetVisible(FindIn(package, hash));
}

void __cdecl FEngSetScript(FEObject *o, unsigned int script, bool restart)
{
    if (!o)
        return;
    if (FEScript *s = o->FindScript(script))
    {
        o->SetScript(s, false);
        if (restart)
            s->CurrentTime = 0;
    }
}

void __cdecl FEngSetScript(char const *package, unsigned int hash, unsigned int script, bool restart)
{
    FEngSetScript(FindIn(package, hash), script, restart);
}

bool __cdecl FEngIsScriptRunning(FEObject *o, unsigned int script)
{
    return ScriptIs(o, script, true);
}

bool __cdecl FEngIsScriptRunning(char const *package, unsigned int hash, unsigned int script)
{
    return ScriptIs(FindIn(package, hash), script, true);
}

bool __cdecl FEngIsScriptSet(char const *package, unsigned int hash, unsigned int script)
{
    return ScriptIs(FindIn(package, hash), script, false);
}

void __cdecl FEngSetMultiImageRot(FEMultiImage *image, float angle)
{
    if (!image)
        return;
    FEObject *o = reinterpret_cast<FEObject *>(image);
    At<float>(o->pData, 0x8c) = angle;
    o->Flags |= kChanged;
}

void __cdecl FEngSetSize(FEObject *o, float x, float y)
{
    if (!o)
        return;
    o->pData->Size[0] = x;
    o->pData->Size[1] = y;
    o->Flags |= kChanged;
}

void __cdecl FEngSetLanguageHash(FEString *string, unsigned int hash)
{
    if (!string)
        return;
    FEObject *o = reinterpret_cast<FEObject *>(string);
    o->Flags |= kChanged;
    At<unsigned int>(o, 0x60) = hash;
    if (void *callback = *kLabelCallback)
        reinterpret_cast<void(__thiscall *)(void *, FEString *)>((*static_cast<void ***>(callback))[0])(callback, string);
    o->Flags = (o->Flags & ~2u) | kChanged;
}

void __cdecl FEngSetMovieName(FEMovie *movie, char const *name)
{
    if (!movie)
        return;
    FEObject *o = reinterpret_cast<FEObject *>(movie);
    bStrNCpy(At<char *>(o, 0x24), name, 0x100);
    o->Flags |= kChanged;
}

void __cdecl FEngSetCurrentButton(char const *package, unsigned int hash)
{
    FEPackage *p = FEPackageManager::Get()->FindPackage(package);
    if (!p)
        return;
    if (FEObject *button = FindButton(package, hash))
        p->SetCurrentButton(button, true);
}

void __cdecl FEngSetButtonState(char const *package, unsigned int hash, bool enabled)
{
    if (FEObject *button = FindButton(package, hash))
    {
        if (enabled)
            button->Flags &= ~kDisabled;
        else
            button->Flags |= kDisabled;
    }
}

void __cdecl FEngDisableButton(char const *package, unsigned int hash)
{
    if (FEObject *button = FindButton(package, hash))
        button->Flags |= kDisabled;
}

void __cdecl FEngEnableButton(char const *package, unsigned int hash)
{
    if (FEObject *button = FindButton(package, hash))
        button->Flags &= ~kDisabled;
}

FEObject *__cdecl FEngFindObject(char const *package, unsigned int hash)
{
    return FindIn(package, hash);
}

FEGroup *__cdecl FEngFindGroup(char const *package, unsigned int hash)
{
    FEObject *o = FindIn(package, hash);
    return o && o->Type == kGroup ? reinterpret_cast<FEGroup *>(o) : nullptr;
}

FEImage *__cdecl FEngFindImage(char const *package, int hash)
{
    FEObject *o = FindIn(package, static_cast<unsigned int>(hash));
    return o && o->Type == kImage ? reinterpret_cast<FEImage *>(o) : nullptr;
}

FEString *__cdecl FEngFindString(char const *package, int hash)
{
    FEObject *o = FindIn(package, static_cast<unsigned int>(hash));
    return o && o->Type == kString ? reinterpret_cast<FEString *>(o) : nullptr;
}

int __cdecl FEngMapJoyParamToJoyport(int param)
{
    if (param & 1)
        return 0;
    if (param & 2)
        return 1;
    if (param & 4)
        return 2;
    return (param & 8) ? 3 : -1;
}

int __cdecl FEngMapJoyportToJoyParam(int port)
{
    switch (port)
    {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 4;
    case 3:
        return 8;
    default:
        return 0;
    }
}

void __cdecl FEngSetCreateCallback(char const *package, MenuScreen *(__cdecl *create)(ScreenConstructorData *))
{
    struct Entry
    {
        const char *name;
        MenuScreen *(__cdecl *create)(ScreenConstructorData *);
    };
    Entry *table = reinterpret_cast<Entry *>(0x8f4320);
    for (Entry *e = table; reinterpret_cast<char *>(e) < reinterpret_cast<char *>(0x8f48b0); ++e)
        if (!bStrICmp(package, e->name))
            e->create = create;
}
