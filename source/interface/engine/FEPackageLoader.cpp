/**
 * Front-end packages from our runtime format (docs/FRONTEND.md "FEPK", tools/assetc/fepackage.py native): FEngine::
 * LoadPackage (0x5c4d90) builds the FEPackage, its objects, scripts, responses and message targets as
 * FEPackageReader::Load (0x5c4cb0) does from EA's chunks, then starts it. Anything else goes to the original.
 *
 * `-set Debug.FEPackageCheck=1` (2: also every other package of the loaded bundles, at each load) with packages compiled with EA's package after ours (assetc.fepackage native(check)):
 * the original loads that too, and every part of the two packages is compared and the differences logged. A check run
 * that loads a race in the cycling weather crashed in a streamed texture's load (TextureInfoPlatInterface::Init);
 * the cause is not known. Runs without the check, and check runs in fixed weather, load it.
 */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

using Malloc = void *(__cdecl *)(unsigned int);
using Construct = void *(__thiscall *)(void *);
using WithText = void(__thiscall *)(void *, const char *);
using WithWord = void(__thiscall *)(void *, unsigned int);
using WithPointer = void(__thiscall *)(void *, void *);
using Find = void *(__thiscall *)(void *, unsigned int);
using Alloc = void *(__thiscall *)(void *);

const Malloc kMalloc = reinterpret_cast<Malloc>(0x652ac0);
using Free = void(__cdecl *)(void *);
const Free kFree = reinterpret_cast<Free>(0x652b10);
const Construct kPackageConstruct = reinterpret_cast<Construct>(0x5bc240);
const WithText kNodeSetName = reinterpret_cast<WithText>(0x5b59c0);
const WithText kSetFilename = reinterpret_cast<WithText>(0x5b1480);
const Find kCreateObject = reinterpret_cast<Find>(0x5bee70);
const Find kFindByGuid = reinterpret_cast<Find>(0x5b85a0);
const WithWord kSetLabelHash = reinterpret_cast<WithWord>(0x50d8b0);
const WithWord kWideSetLength = reinterpret_cast<WithWord>(0x5b91c0);
const WithPointer kWideAssign = reinterpret_cast<WithPointer>(0x5bcd20);
void *const kScriptPool = reinterpret_cast<void *>(0x91dd0c);
const Alloc kScriptAlloc = reinterpret_cast<Alloc>(0x5c1c40);
const Construct kScriptConstruct = reinterpret_cast<Construct>(0x5b3d60);
const WithWord kSetTrackCount = reinterpret_cast<WithWord>(0x5bf230);
void *const kKeyPool = reinterpret_cast<void *>(0x91dd2c);
const Alloc kKeyAlloc = reinterpret_cast<Alloc>(0x5bffe0);
const unsigned int kKeyVtable = 0x8a2c24;
const WithWord kEventSetCount = reinterpret_cast<WithWord>(0x5b5350);
void *const kResponsePool = reinterpret_cast<void *>(0x91dd3c);
const Alloc kResponseAlloc = reinterpret_cast<Alloc>(0x5c1d00);
const unsigned int kResponseVtable = 0x8a2c28;
const WithWord kResponseSetCount = reinterpret_cast<WithWord>(0x5bb370);
const WithText kResponseSetParam = reinterpret_cast<WithText>(0x5b6df0);
const WithPointer kListAddTail = reinterpret_cast<WithPointer>(0x50d700);
const WithPointer kAddResponse = reinterpret_cast<WithPointer>(0x5b3da0);
const WithPointer kAddObject = reinterpret_cast<WithPointer>(0x50db90);
const WithPointer kGroupAddObject = reinterpret_cast<WithPointer>(0x5b5420);
const Construct kSetupMoveToTracks = reinterpret_cast<Construct>(0x5b7c30);
const WithWord kTargetsAllocate = reinterpret_cast<WithWord>(0x5b6f20);
using KeyInterp = void(__cdecl *)(void *, unsigned int, int, void *);
const KeyInterp kKeyInterp = reinterpret_cast<KeyInterp>(0x5c07b0);
using Shutdown = void(__thiscall *)(void *, void *);
const Shutdown kShutdown = reinterpret_cast<Shutdown>(0x5b14d0);
using Startup = bool(__thiscall *)(void *, void *);
const Startup kStartup = reinterpret_cast<Startup>(0x5bc4d0);
using Original = FEPackage *(__thiscall *)(FEngine *, const void *, bool);

const unsigned int kInitScript = 0x1744b3;
const unsigned int kReferenceFlag = 0x100000;
const unsigned int kButtonFlag = 0x10000000;
const int kGroupType = 5;
const int kStringType = 2;
const int kMultiImageType = 12;

enum ObjectBits
{
    kParent = 1,
    kImageFlags = 2,
    kData = 4,
    kString = 8,
    kMulti = 16,
};
enum ResponseBits
{
    kParam = 1,
    kText = 2,
    kTarget = 4,
};

/** A forward reader over the runtime package; `ok` goes false at the first read past the end. */
struct Reader
{
    const unsigned char *p;
    const unsigned char *end;
    bool ok = true;

    unsigned int Word()
    {
        if (p + 4 > end)
        {
            ok = false;
            return 0;
        }
        unsigned int v;
        memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    const unsigned char *Bytes(unsigned int n)
    {
        if (static_cast<unsigned int>(end - p) < n)
        {
            ok = false;
            return nullptr;
        }
        const unsigned char *at = p;
        p += n;
        return at;
    }
    const char *Text()
    {
        const unsigned int n = Word();
        const char *s = reinterpret_cast<const char *>(Bytes((n + 3) & ~3u));
        return s && n && s[n - 1] == 0 ? s : (ok = false, "");
    }
};

/** What FEPackageReader keeps while it reads (only the parts CreateObject uses: the type sizes, the game interface). */
struct ReaderState
{
    unsigned char raw[0x50];
};

struct Load
{
    Reader in;
    void *package = nullptr;
    void *gameInterface;
    bool skipInit;
    ReaderState state = {};
    unsigned int buttonsSet = 0;
    void *parentCache = nullptr;
};

bool ReadResponses(Load &l, void *owner, bool package)
{
    if (!l.in.Word())
        return l.in.ok;
    const unsigned int messages = l.in.Word();
    for (unsigned int m = 0; m < messages && l.in.ok; ++m)
    {
        const unsigned int message = l.in.Word();
        const unsigned int count = l.in.Word();
        const unsigned int listed = l.in.Word();
        void *response = kResponseAlloc(kResponsePool);
        if (!response)
            return false;
        At<unsigned int>(response, 0) = kResponseVtable;
        At<unsigned int>(response, 4) = 0xabadcafe;
        At<unsigned int>(response, 8) = 0xabadcafe;
        At<unsigned int>(response, 0xc) = message;
        At<unsigned int>(response, 0x10) = 0;
        At<unsigned int>(response, 0x14) = 0;
        if (package)
            kAddResponse(owner, response);
        else
            kListAddTail(static_cast<char *>(owner) + 0x34, response);
        kResponseSetCount(response, count);
        for (unsigned int r = 0; r < listed && l.in.ok; ++r)
        {
            const unsigned int id = l.in.Word();
            const unsigned int bits = l.in.Word();
            unsigned int *entry = r < count ? reinterpret_cast<unsigned int *>(At<char *>(response, 0x14) + r * 0xc)
                                            : nullptr;
            if (entry)
                entry[0] = id;
            if (bits & kParam)
            {
                const unsigned int param = l.in.Word();
                if (entry)
                    entry[1] = param;
            }
            if (bits & kText)
            {
                const char *text = l.in.Text();
                if (entry)
                    kResponseSetParam(entry, text);
            }
            if (bits & kTarget)
            {
                const unsigned int target = l.in.Word();
                if (entry)
                    entry[2] = target;
            }
        }
    }
    return l.in.ok;
}

void AppendKey(char *track, unsigned int *node)
{
    unsigned int *&head = At<unsigned int *>(track, 0x30);
    unsigned int *&tail = At<unsigned int *>(track, 0x34);
    node[1] = reinterpret_cast<unsigned int>(nullptr);
    node[2] = reinterpret_cast<unsigned int>(tail);
    if (tail)
        tail[1] = reinterpret_cast<unsigned int>(node);
    else
        head = node;
    tail = node;
}

bool ReadScript(Load &l, void *object)
{
    const unsigned int id = l.in.Word();
    const unsigned int length = l.in.Word();
    const unsigned int flags = l.in.Word();
    const unsigned int trackCount = l.in.Word();
    const unsigned int hasChain = l.in.Word();
    const unsigned int chain = l.in.Word();
    const unsigned int tracks = l.in.Word();
    if (!l.in.ok || tracks > trackCount)
        return false;
    char *slot = static_cast<char *>(kScriptAlloc(kScriptPool));
    if (!slot)
        return false;
    At<unsigned int>(slot, 0x2c) = 0;
    At<unsigned int>(slot, 0x30) = 0;
    At<unsigned int>(slot, 0xc) = 0;
    At<unsigned int>(slot, 0x14) = 0;
    At<unsigned int>(slot, 0x18) = 0;
    At<unsigned int>(slot, 0x1c) = 0;
    At<unsigned int>(slot, 0x20) = 0;
    char *script = static_cast<char *>(kScriptConstruct(slot));
    At<unsigned int>(script, 0x10) = 0;
    At<unsigned int>(script, 0x30) = id;
    At<unsigned int>(script, 0xc) = length;
    At<unsigned int>(script, 0x14) = flags;
    kSetTrackCount(script, trackCount);
    if (hasChain)
        At<unsigned int>(script, 0x18) = chain;
    unsigned char place = 0;
    for (unsigned int t = 0; t < tracks; ++t)
    {
        const unsigned char *head = l.in.Bytes(4);
        const unsigned int trackLength = l.in.Word();
        const unsigned int offset = l.in.Word();
        const unsigned int keys = l.in.Word();
        if (!l.in.ok)
            return false;
        char *track = At<char *>(script, 0x20) + t * 0x38;
        track[0] = static_cast<char>(head[0]);
        track[1] = static_cast<char>(head[1]);
        track[2] = static_cast<char>(head[2]);
        track[3] = static_cast<char>(head[3]);
        unsigned int &word = At<unsigned int>(track, 4);
        word = (word & 0xff000000u) | (trackLength & 0xffffffu);
        track[7] = static_cast<char>(place);
        place = static_cast<unsigned char>(place + (head[1] >> 2));
        track[7] = static_cast<char>(offset);
        const unsigned int step = head[1] + 4u;
        const unsigned char *raw = l.in.Bytes(keys * step);
        if (!raw)
            return false;
        if (track[0x2c])
        {
            At<unsigned int>(track, 0x30) = 0;
            At<unsigned int>(track, 0x34) = 0;
            track[0x2c] = 0;
        }
        for (unsigned int k = 0; k < keys; ++k)
        {
            unsigned int *node;
            if (k == 0)
                node = reinterpret_cast<unsigned int *>(track + 8);
            else
            {
                node = static_cast<unsigned int *>(kKeyAlloc(kKeyPool));
                if (!node)
                    return false;
                node[0] = kKeyVtable;
                node[1] = 0xabadcafe;
                node[2] = 0xabadcafe;
            }
            memcpy(&node[3], raw + k * step, step);
            if (k)
                AppendKey(track, node);
        }
    }
    const unsigned int hasEvents = l.in.Word();
    const unsigned int events = l.in.Word();
    if (hasEvents)
    {
        const unsigned char *raw = l.in.Bytes(events * 12);
        if (!raw)
            return false;
        kEventSetCount(script + 0x24, events);
        memcpy(At<void *>(script, 0x28), raw, events * 12);
    }
    char *&first = At<char *>(object, 0x4c);
    char *&last = At<char *>(object, 0x50);
    At<char *>(script, 4) = nullptr;
    At<char *>(script, 8) = last;
    if (last)
        At<char *>(last, 4) = script;
    else
        first = script;
    last = script;
    ++At<unsigned int>(object, 0x48);
    if (id == kInitScript)
        At<char *>(object, 0x54) = script;
    return l.in.ok;
}

bool ReadObject(Load &l)
{
    const unsigned int type = l.in.Word();
    const unsigned int nameHash = l.in.Word();
    const unsigned int guid = l.in.Word();
    const unsigned int hash = l.in.Word();
    const unsigned int flags = l.in.Word();
    const unsigned int resource = l.in.Word();
    const unsigned int bits = l.in.Word();
    if (!l.in.ok || (flags & kReferenceFlag))
        return false;
    char *object = static_cast<char *>(kCreateObject(&l.state, type));
    if (!object)
        return false;
    At<unsigned int>(object, 0x10) = nameHash;
    At<unsigned int>(object, 0xc) = guid;
    At<unsigned int>(object, 0x10) = hash;
    At<unsigned int>(object, 0x1c) = flags;
    At<unsigned short>(object, 0x22) = static_cast<unsigned short>(resource);
    if (flags & kButtonFlag)
        At<char **>(l.package, 0x90)[l.buttonsSet++] = object;
    void *parent = nullptr;
    if (bits & kParent)
    {
        const unsigned int parentGuid = l.in.Word();
        if (!l.parentCache || At<unsigned int>(l.parentCache, 0xc) != parentGuid)
            l.parentCache = kFindByGuid(l.package, parentGuid);
        parent = l.parentCache;
    }
    if (bits & kImageFlags)
    {
        const unsigned int imageFlags = l.in.Word();
        if (type == 1 || type == 9 || type == 10 || type == kMultiImageType)
            At<unsigned int>(object, 0x5c) = imageFlags;
    }
    if (bits & kString)
    {
        const unsigned int s = l.in.Word();
        void *wide = object + 0x64;
        if (s & 1)
            kWideSetLength(wide, l.in.Word());
        if (s & 2)
        {
            const unsigned int chars = l.in.Word();
            const unsigned char *text = l.in.Bytes((chars * 2 + 3) & ~3u);
            if (text && chars && type == kStringType)
                kWideAssign(wide, const_cast<unsigned char *>(text));
        }
        if (s & 4)
            At<unsigned int>(object, 0x6c) = l.in.Word();
        if (s & 8)
            At<unsigned int>(object, 0x70) = l.in.Word();
        if (s & 16)
            At<unsigned int>(object, 0x74) = l.in.Word();
        if (s & 32)
            kSetLabelHash(object, l.in.Word());
    }
    if (bits & kMulti)
    {
        const unsigned int m = l.in.Word();
        static const int kAt[6] = {0x60, 0x64, 0x68, 0x6c, 0x70, 0x74};
        for (int i = 0; i < 6; ++i)
            if (m & (1u << i))
                At<unsigned int>(object, kAt[i]) = l.in.Word();
    }
    if (bits & kData)
    {
        const unsigned int size = l.in.Word();
        const unsigned char *data = l.in.Bytes(size);
        if (!data)
            return false;
        memcpy(At<void *>(object, 0x2c), data, size & ~3u);
    }
    const unsigned int scripts = l.in.Word();
    for (unsigned int s = 0; s < scripts; ++s)
        if (!ReadScript(l, object))
            return false;
    if (!ReadResponses(l, object, false))
        return false;
    for (char *script = At<char *>(object, 0x4c); script; script = At<char *>(script, 4))
    {
        const unsigned int chain = At<unsigned int>(script, 0x18);
        if (!chain)
            continue;
        char *to = At<char *>(object, 0x4c);
        while (to && At<unsigned int>(to, 0x30) != chain)
            to = At<char *>(to, 4);
        At<char *>(script, 0x18) = to;
    }
    char *init = At<char *>(object, 0x4c);
    while (init && At<unsigned int>(init, 0x30) != kInitScript)
        init = At<char *>(init, 4);
    At<char *>(object, 0x54) = init;
    if (init)
    {
        kSetupMoveToTracks(object);
        At<unsigned int>(init, 0x10) = 0;
        if (!l.skipInit)
            for (unsigned int t = 0; t < At<unsigned int>(init, 0x1c); ++t)
                kKeyInterp(init, t & 0xff, 0, object);
    }
    if (parent)
        kGroupAddObject(parent, object);
    else
        kAddObject(l.package, object);
    return true;
}

void *LoadNative(const unsigned char *data, unsigned int size, void *gameInterface, bool skipInit)
{
    Load l{{data + 12, data + size}};
    l.gameInterface = gameInterface;
    l.skipInit = skipInit;
    const char *name = l.in.Text();
    const char *file = l.in.Text();
    l.in.Word();
    l.in.Word();
    const unsigned int types = l.in.Word();
    const unsigned char *typeSizes = l.in.Bytes(types * 8);
    if (!l.in.ok)
        return nullptr;
    void *memory = kMalloc(0xb8);
    if (!memory)
        return nullptr;
    l.package = kPackageConstruct(memory);
    At<unsigned int>(l.package, 0x8c) = 0;
    kNodeSetName(l.package, name);
    kSetFilename(l.package, file);
    At<unsigned int>(&l.state, 0x30) = types;
    At<const unsigned char *>(&l.state, 0x34) = typeSizes;
    At<void *>(&l.state, 0x38) = gameInterface;

    bool ok = true;
    const unsigned int resources = l.in.Word();
    if (resources)
    {
        const unsigned char *start = l.in.p;
        unsigned int blob = 0;
        for (unsigned int r = 0; r < resources && l.in.ok; ++r)
        {
            l.in.Bytes(20);
            blob += static_cast<unsigned int>(strlen(l.in.Text())) + 1;
        }
        l.in.p = start;
        unsigned int *entries = static_cast<unsigned int *>(kMalloc(resources * 0x18));
        char *names = static_cast<char *>(kMalloc(blob ? blob : 1));
        At<unsigned int>(l.package, 0x64) = resources;
        At<unsigned int *>(l.package, 0x68) = entries;
        At<char *>(l.package, 0x98) = names;
        char *at = names;
        for (unsigned int r = 0; r < resources && l.in.ok; ++r)
        {
            unsigned int *e = entries + r * 6;
            e[0] = l.in.Word();
            e[2] = l.in.Word();
            e[3] = l.in.Word();
            e[4] = l.in.Word();
            e[5] = l.in.Word();
            const char *text = l.in.Text();
            const size_t n = strlen(text) + 1;
            memcpy(at, text, n);
            e[1] = reinterpret_cast<unsigned int>(at);
            at += n;
        }
    }
    const unsigned int buttons = l.in.Word();
    At<void *>(l.package, 0x90) = buttons ? kMalloc(buttons * 4) : nullptr;
    At<unsigned int>(l.package, 0x94) = buttons;
    const unsigned int objects = l.in.Word();
    for (unsigned int o = 0; o < objects && ok; ++o)
        ok = l.in.ok && ReadObject(l);
    ok = ok && ReadResponses(l, l.package, true);
    const unsigned int hasTargets = l.in.Word();
    const unsigned int messages = l.in.Word();
    if (ok && hasTargets)
    {
        unsigned int *block = static_cast<unsigned int *>(kMalloc(messages * 0x10 + 4));
        block[0] = messages;
        memset(block + 1, 0, messages * 0x10);
        At<unsigned int>(l.package, 0x6c) = messages;
        At<unsigned int *>(l.package, 0x70) = block + 1;
        for (unsigned int m = 0; m < messages && l.in.ok; ++m)
        {
            unsigned int *list = block + 1 + m * 4;
            list[0] = l.in.Word();
            const unsigned int targets = l.in.Word();
            kTargetsAllocate(list, targets);
            for (unsigned int t = 0; t < targets && l.in.ok; ++t)
            {
                void *target = kFindByGuid(l.package, l.in.Word());
                if (list[2] == list[1])
                    kTargetsAllocate(list, list[1] + 1);
                reinterpret_cast<void **>(list[3])[list[2]++] = target;
            }
        }
    }
    ok = ok && l.in.ok;
    if (ok)
    {
        At<bool>(l.package, 0x16) = skipInit;
        ok = kStartup(l.package, gameInterface);
    }
    if (!ok)
    {
        nfs::Log("fe: %s does not load from our package", name);
        (*reinterpret_cast<void(__thiscall ***)(void *, int)>(l.package))[0](l.package, 1);
        return nullptr;
    }
    return l.package;
}

// ---------------------------------------------------------------------------------------------------- the check

struct Check
{
    const char *package;
    int differences = 0;
    void Differ(const char *what, unsigned int guid, unsigned int a, unsigned int b)
    {
        if (++differences <= 40)
            nfs::Log("fe check: %s: object %u: %s %08x (ours) %08x (EA's)", package, guid, what, a, b);
    }
};

void CompareResponses(Check &c, unsigned int guid, char *a, char *b)
{
    for (; a || b; a = At<char *>(a, 4), b = At<char *>(b, 4))
    {
        if (!a || !b)
        {
            c.Differ("response list", guid, a != nullptr, b != nullptr);
            return;
        }
        if (At<unsigned int>(a, 0xc) != At<unsigned int>(b, 0xc) || At<unsigned int>(a, 0x10) != At<unsigned int>(b, 0x10))
        {
            c.Differ("response", guid, At<unsigned int>(a, 0xc), At<unsigned int>(b, 0xc));
            continue;
        }
        for (unsigned int i = 0; i < At<unsigned int>(a, 0x10); ++i)
        {
            const unsigned int *x = reinterpret_cast<unsigned int *>(At<char *>(a, 0x14) + i * 0xc);
            const unsigned int *y = reinterpret_cast<unsigned int *>(At<char *>(b, 0x14) + i * 0xc);
            const bool text = x[0] >= 0x200 && x[0] < 0x205 && x[0] != 0x203;
            if (x[0] != y[0] || x[2] != y[2] ||
                (text ? (x[1] && y[1] ? strcmp(reinterpret_cast<char *>(x[1]), reinterpret_cast<char *>(y[1])) != 0
                                      : x[1] != y[1])
                      : x[1] != y[1]))
                c.Differ("response entry", guid, x[0], y[0]);
        }
    }
}

void CompareScripts(Check &c, unsigned int guid, char *a, char *b)
{
    for (; a || b; a = At<char *>(a, 4), b = At<char *>(b, 4))
    {
        if (!a || !b)
        {
            c.Differ("script list", guid, a != nullptr, b != nullptr);
            return;
        }
        static const int kWords[] = {0xc, 0x10, 0x14, 0x1c, 0x24, 0x30};
        for (int w : kWords)
            if (At<unsigned int>(a, w) != At<unsigned int>(b, w))
                c.Differ("script word", guid, w, At<unsigned int>(a, 0x30));
        char *ca = At<char *>(a, 0x18), *cb = At<char *>(b, 0x18);
        if ((ca ? At<unsigned int>(ca, 0x30) : 0) != (cb ? At<unsigned int>(cb, 0x30) : 0))
            c.Differ("chain", guid, ca ? At<unsigned int>(ca, 0x30) : 0, cb ? At<unsigned int>(cb, 0x30) : 0);
        if (At<unsigned int>(a, 0x24) && memcmp(At<void *>(a, 0x28), At<void *>(b, 0x28), At<unsigned int>(a, 0x24) * 12))
            c.Differ("events", guid, At<unsigned int>(a, 0x24), At<unsigned int>(a, 0x30));
        for (unsigned int t = 0; t < At<unsigned int>(a, 0x1c) && t < At<unsigned int>(b, 0x1c); ++t)
        {
            char *x = At<char *>(a, 0x20) + t * 0x38, *y = At<char *>(b, 0x20) + t * 0x38;
            if (memcmp(x, y, 8) || memcmp(x + 0x14, y + 0x14, 0x14) || x[0x2c] != y[0x2c])
                c.Differ("track", guid, t, At<unsigned int>(a, 0x30));
            const unsigned int n = static_cast<unsigned char>(x[1]);
            unsigned int *kx = At<unsigned int *>(x, 0x30), *ky = At<unsigned int *>(y, 0x30);
            for (; kx || ky; kx = reinterpret_cast<unsigned int *>(kx[1]), ky = reinterpret_cast<unsigned int *>(ky[1]))
            {
                if (!kx || !ky)
                {
                    c.Differ("key list", guid, t, At<unsigned int>(a, 0x30));
                    break;
                }
                if (memcmp(kx + 3, ky + 3, 4 + n))
                    c.Differ("key", guid, t, kx[3]);
            }
        }
    }
}

void CompareObjects(Check &c, FEObject *a, FEObject *b)
{
    for (; a || b; a = a->Next, b = b->Next)
    {
        if (!a || !b)
        {
            c.Differ("object list", a ? a->GUID : b->GUID, a != nullptr, b != nullptr);
            return;
        }
        static const int kWords[] = {0xc, 0x10, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x30, 0x48};
        for (int w : kWords)
            if (At<unsigned int>(a, w) != At<unsigned int>(b, w))
                c.Differ("object word", a->GUID, w, At<unsigned int>(b, w));
        if (a->DataSize == b->DataSize && memcmp(a->pData, b->pData, a->DataSize))
            c.Differ("data", a->GUID, a->DataSize, 0);
        if (a->Type == kStringType)
        {
            const short *ta = At<short *>(a, 0x64), *tb = At<short *>(b, 0x64);
            int i = 0;
            while (ta && tb && ta[i] && ta[i] == tb[i])
                ++i;
            if ((ta ? ta[i] : 0) != (tb ? tb[i] : 0) || At<unsigned int>(a, 0x68) != At<unsigned int>(b, 0x68))
                c.Differ("text", a->GUID, At<unsigned int>(a, 0x68), At<unsigned int>(b, 0x68));
            for (int w = 0x60; w <= 0x74; w += 4)
                if (w != 0x64 && w != 0x68 && At<unsigned int>(a, w) != At<unsigned int>(b, w))
                    c.Differ("string word", a->GUID, w, At<unsigned int>(b, w));
        }
        else if (a->Type == 1 || a->Type == 9 || a->Type == 10 || a->Type == kMultiImageType)
        {
            const int last = a->Type == kMultiImageType ? 0x74 : 0x5c;
            for (int w = 0x5c; w <= last; w += 4)
                if (At<unsigned int>(a, w) != At<unsigned int>(b, w))
                    c.Differ("image word", a->GUID, w, At<unsigned int>(b, w));
        }
        CompareScripts(c, a->GUID, At<char *>(a, 0x4c), At<char *>(b, 0x4c));
        char *ia = At<char *>(a, 0x54), *ib = At<char *>(b, 0x54);
        if ((ia ? At<unsigned int>(ia, 0x30) : 0) != (ib ? At<unsigned int>(ib, 0x30) : 0))
            c.Differ("init script", a->GUID, 0, 0);
        CompareResponses(c, a->GUID, At<char *>(a, 0x3c), At<char *>(b, 0x3c));
        if (a->Type == kGroupType)
            CompareObjects(c, At<FEObject *>(a, 0x64), At<FEObject *>(b, 0x64));
    }
}

void ComparePackages(const char *name, void *ours, void *theirs)
{
    Check c{name};
    if (At<unsigned int>(ours, 0x64) != At<unsigned int>(theirs, 0x64))
        c.Differ("resources", 0, At<unsigned int>(ours, 0x64), At<unsigned int>(theirs, 0x64));
    else
        for (unsigned int r = 0; r < At<unsigned int>(ours, 0x64); ++r)
        {
            unsigned int *x = At<unsigned int *>(ours, 0x68) + r * 6, *y = At<unsigned int *>(theirs, 0x68) + r * 6;
            if (x[0] != y[0] || x[2] != y[2] || x[3] != y[3] || x[4] != y[4] || x[5] != y[5] ||
                strcmp(reinterpret_cast<char *>(x[1]), reinterpret_cast<char *>(y[1])))
                c.Differ("resource", r, x[0], y[0]);
        }
    if (At<unsigned int>(ours, 0x94) != At<unsigned int>(theirs, 0x94))
        c.Differ("buttons", 0, At<unsigned int>(ours, 0x94), At<unsigned int>(theirs, 0x94));
    else
        for (unsigned int i = 0; i < At<unsigned int>(ours, 0x94); ++i)
        {
            FEObject *x = At<FEObject **>(ours, 0x90)[i], *y = At<FEObject **>(theirs, 0x90)[i];
            if ((x ? x->GUID : 0) != (y ? y->GUID : 0))
                c.Differ("button", i, x ? x->GUID : 0, y ? y->GUID : 0);
        }
    if (At<unsigned int>(ours, 0x6c) != At<unsigned int>(theirs, 0x6c))
        c.Differ("message targets", 0, At<unsigned int>(ours, 0x6c), At<unsigned int>(theirs, 0x6c));
    else
        for (unsigned int m = 0; m < At<unsigned int>(ours, 0x6c); ++m)
        {
            unsigned int *x = At<unsigned int *>(ours, 0x70) + m * 4, *y = At<unsigned int *>(theirs, 0x70) + m * 4;
            if (x[0] != y[0] || x[2] != y[2])
                c.Differ("target list", m, x[0], y[0]);
            else
                for (unsigned int t = 0; t < x[2]; ++t)
                {
                    FEObject *ox = reinterpret_cast<FEObject **>(x[3])[t], *oy = reinterpret_cast<FEObject **>(y[3])[t];
                    if ((ox ? ox->GUID : 0) != (oy ? oy->GUID : 0))
                        c.Differ("target", m, ox ? ox->GUID : 0, oy ? oy->GUID : 0);
                }
        }
    static const int kBytes[] = {0x14, 0x16};
    for (int w : kBytes)
        if (At<unsigned char>(ours, w) != At<unsigned char>(theirs, w))
            c.Differ("package byte", 0, w, At<unsigned char>(theirs, w));
    CompareResponses(c, 0, At<char *>(ours, 0x5c), At<char *>(theirs, 0x5c));
    CompareObjects(c, static_cast<FEPackage *>(ours)->GetFirstObject(), static_cast<FEPackage *>(theirs)->GetFirstObject());
    nfs::Log("fe check: %s: %d differences", name, c.differences);
}
/** FEPackageManager::mInstance: its packages' data in a list at +4 (each node 4 bytes into an FEPackageData, whose
 * chunk is at +0x18). */
char **const kPackageManager = reinterpret_cast<char **>(0x91d21c);
using Decompress = int(__cdecl *)(const void *, void *);
const Decompress kDecompress = reinterpret_cast<Decompress>(0x650350);
const int kMaxChecked = 1024;
unsigned int g_checked[kMaxChecked];
int g_checkedCount;

bool Checked(unsigned int hash)
{
    for (int i = 0; i < g_checkedCount; ++i)
        if (g_checked[i] == hash)
            return true;
    if (g_checkedCount < kMaxChecked)
        g_checked[g_checkedCount++] = hash;
    return false;
}

/** Debug.FEPackageCheck=2: every packed package the loaded bundles hold, not yet checked, loaded both ways, compared
 * and let go again. */
void CheckAll(FEngine *engine)
{
    char *manager = *kPackageManager;
    if (!manager)
        return;
    char *sentinel = manager + 4;
    int checked = 0;
    for (char *node = At<char *>(sentinel, 0); node && node != sentinel; node = At<char *>(node, 0))
    {
        const unsigned int *chunk = At<unsigned int *>(node - 4, 0x18);
        if (!chunk || chunk[0] != 0x30210 || Checked(chunk[2]))
            continue;
        void *data = kMalloc(chunk[5] + 16);
        if (!data)
            continue;
        kDecompress(chunk + 3, data);
        if (FEPackage *ours = engine->LoadPackage(data, false))
        {
            kShutdown(ours, At<void *>(engine, 0x108));
            (*reinterpret_cast<void(__thiscall ***)(void *, int)>(ours))[0](ours, 1);
        }
        kFree(data);
        ++checked;
    }
    if (checked)
        nfs::Log("fe check: %d more packages checked", checked);
}
} // namespace

FEPackage *FEngine::LoadPackage(void const *data, bool skipInit)
{
    const Original original = NFS_ORIGINAL(0x5c4d90, Original);
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    if (!bytes || memcmp(bytes, "FEPK", 4) || At<unsigned int>(const_cast<unsigned char *>(bytes), 4) != 1)
        return original(this, data, skipInit);
    void *gameInterface = At<void *>(this, 0x108);
    const unsigned int size = At<unsigned int>(const_cast<unsigned char *>(bytes), 8);
    static const bool check = nfs::config::Int("Debug", "FEPackageCheck", 0) != 0;
    const unsigned char *ea = check && !memcmp(bytes + size, "EAPK", 4) ? bytes + size + 8 : nullptr;
    void *package = LoadNative(bytes, size, gameInterface, skipInit);
    static const bool all = nfs::config::Int("Debug", "FEPackageCheck", 0) == 2;
    static bool inCheckAll;
    if (all && ea && !inCheckAll)
    {
        inCheckAll = true;
        CheckAll(this);
        inCheckAll = false;
    }
    if (package && ea)
    {
        void *theirs = original(this, ea, skipInit);
        if (theirs)
        {
            ComparePackages(At<const char *>(package, 0xc), package, theirs);
            kShutdown(theirs, gameInterface);
            (*reinterpret_cast<void(__thiscall ***)(void *, int)>(theirs))[0](theirs, 1);
        }
        else
            nfs::Log("fe check: EA's package does not load");
    }
    return static_cast<FEPackage *>(package);
}
