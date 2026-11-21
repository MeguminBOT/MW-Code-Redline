/** A perp (the player or a racer cops can chase): skill and catch-up, heat bookkeeping, cost to the state, pending
 * reputation, hiding zones, collisions it causes (original: zAI AIPerpVehicle). */
#include <math.h>

#include "legacy/link.h"

NFS_REPLACE(0x408e30, AIPerpVehicleComputeSkill)
NFS_REPLACE(0x408fc0, AIPerpVehicleSet911CallTime)
NFS_REPLACE(0x408ff0, AIPerpVehicleIsPartiallyHidden)
NFS_REPLACE(0x409340, AIPerpVehicleGetSkill)
NFS_REPLACE(0x409390, AIPerpVehicleGetCatchupCheat)
NFS_REPLACE(0x409410, AIPerpVehicleAddCostToState)
NFS_REPLACE(0x409480, AIPerpVehicleAddToPendingRepPointsNormal)
NFS_REPLACE(0x4094c0, AIPerpVehicleAddToPendingRepPointsFromCopDestruction)
NFS_REPLACE(0x409500, AIPerpVehicleIsRacing)
NFS_REPLACE(0x409550, AIPerpVehicleGetPercentRaceComplete)
NFS_REPLACE(0x409060, AIPerpVehicleSetHeat)
NFS_REPLACE(0x43cff0, AIPerpVehicleConstructor)
NFS_REPLACE(0x4293d0, AIPerpVehicleUpdate)
NFS_REPLACE(0x4299b0, AIPerpVehicleOnCausedCollision)

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

void **const kRaceStatus = reinterpret_cast<void **>(0x91e000);
const int kRaceMode = 0x1964;
const int kRaceParameters = 0x1968;
const int kRaceActivity = 0x1960;
const int kModeRace = 0;
const int kModeCareer = 2;
const int kRaceTypeChallenge = 8;
const unsigned int *const kCatchUpSkill = reinterpret_cast<const unsigned int *>(0x890724);
const unsigned int *const kSkill = reinterpret_cast<const unsigned int *>(0x890730);
void *const kHarderTable = reinterpret_cast<void *>(0x8eb4ac);
void *const kEasierTable = reinterpret_cast<void *>(0x8eb4c0);
void *const kCatchUpTable = reinterpret_cast<void *>(0x8eb4fc);
const int kVehicleFromPerp = 0x70c;

float Clamp(float value, float least, float most)
{
    return NFS_ORIGINAL(0x401d70, float(__cdecl *)(float, float, float))(value, least, most);
}

float TableValue(void *table, float at)
{
    return NFS_ORIGINAL(0x6533b0, float(__thiscall *)(void *, float))(table, at);
}

int Difficulty(char *race)
{
    return NFS_ORIGINAL(0x5fac10, int(__thiscall *)(void *))(At<void *>(race, kRaceParameters));
}

bool IsChallenge(char *race)
{
    void *parameters = At<void *>(race, kRaceParameters);
    return parameters && NFS_ORIGINAL(0x5faa20, int(__thiscall *)(void *))(parameters) == kRaceTypeChallenge;
}

void AddPending(char *self, int offset, int points)
{
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race)
        return;
    if (At<void *>(race, kRaceActivity) && !IsChallenge(race))
        return;
    At<int>(self, offset) += points;
}
} // namespace

void __fastcall AIPerpVehicleComputeSkill(char *self)
{
    At<float>(self, 0x7a8) = 0.0f;
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race || Virtual<bool>(At<void *>(self, 0x34), 0x24))
        return;
    const int mode = At<int>(race, kRaceMode);
    if (mode == kModeRace)
    {
        void *parameters = At<void *>(race, kRaceParameters);
        if (parameters && !NFS_ORIGINAL(0x5fc7a0, bool(__thiscall *)(void *))(parameters))
        {
            At<unsigned int>(self, 0x7a8) = kSkill[Difficulty(race)];
            return;
        }
        At<unsigned int>(self, 0x7a8) = kCatchUpSkill[Difficulty(static_cast<char *>(*kRaceStatus))];
        return;
    }
    if (mode != kModeCareer)
        return;
    char *racer = At<char *>(self, 0x7a4);
    if (!racer || !At<char *>(racer, 4))
        return;
    const int *level = NFS_ORIGINAL(0x404990, const int *(__thiscall *)(void *))(At<char *>(racer, 4) + 4);
    const float skill = Clamp(static_cast<float>(*level) * 0.01f, 0.0f, 1.0f);
    const float adaptive = NFS_ORIGINAL(0x5dc180, float(__thiscall *)(void *))(*kRaceStatus);
    const float scale = TableValue(adaptive > 0.0f ? kHarderTable : kEasierTable, skill);
    At<float>(self, 0x7a8) = Clamp(scale * adaptive + skill, 0.0f, 1.0f);
}

void __fastcall AIPerpVehicleSet911CallTime(char *self, void *, float time)
{
    if (time > At<float>(self, 0x68))
        At<float>(self, 0x68) = time;
}

bool __fastcall AIPerpVehicleIsPartiallyHidden(char *self, void *, float *amount)
{
    if (!(At<float>(self, 0x30) > 0.07f))
    {
        *amount = 0.0f;
        return false;
    }
    float hidden = At<float>(self, 0x30) / At<float>(self, 0x34);
    if (1.0f < hidden)
        hidden = 1.0f;
    *amount = hidden;
    return true;
}

float __fastcall AIPerpVehicleGetSkill(char *self)
{
    const float skill = At<float>(self, 0x760) + At<float>(self, 0x75c);
    if (!(skill > 0.0f))
        return 0.0f;
    return 1.0f < skill ? 1.0f : skill;
}

float __fastcall AIPerpVehicleGetCatchupCheat(char *self)
{
    const float cheat = At<float>(self, 0x40) + At<float>(self, 0x3c) - 1.0f;
    float scale;
    if (!(cheat < 1.0f))
        scale = 1.0f;
    else if (0.0f > cheat)
        scale = 0.0f;
    else
        scale = cheat;
    return TableValue(kCatchUpTable, At<float>(self, 0x3c)) * scale;
}

void __fastcall AIPerpVehicleAddCostToState(char *self, void *, int cost)
{
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race)
        return;
    void *ai = Virtual<void *>(self - kVehicleFromPerp, 0xa0);
    if (!ai)
        return;
    const bool challenge = IsChallenge(static_cast<char *>(*kRaceStatus));
    if (At<int>(static_cast<char *>(*kRaceStatus), kRaceMode) != kModeCareer && !challenge)
        return;
    At<int>(self, 0x20) += cost;
    Virtual<void, int>(ai, 0xf4, cost);
}

void __fastcall AIPerpVehicleAddToPendingRepPointsNormal(char *self, void *, int points)
{
    AddPending(self, 0x24, points);
}

void __fastcall AIPerpVehicleAddToPendingRepPointsFromCopDestruction(char *self, void *, int points)
{
    AddPending(self, 0x28, points);
}

bool __fastcall AIPerpVehicleIsRacing(char *self)
{
    if (!Virtual<void *>(self, 0x50))
        return false;
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race || At<int>(race, kRaceActivity) != 1)
        return false;
    char *racer = Virtual<char *>(self - kVehicleFromPerp, 0x64);
    return racer && At<unsigned char>(racer, 0x2c);
}

float __fastcall AIPerpVehicleGetPercentRaceComplete(char *self)
{
    char *racer = At<char *>(self, 0x758);
    return racer ? At<float>(racer, 0x18) : 0.0f;
}

namespace
{
void **const kAttribAllocator = reinterpret_cast<void **>(0x8f7ea8);
const unsigned int kInstanceSize = 0x14;

void Account(bool allocating)
{
    NFS_ORIGINAL(0x452880, void(__cdecl *)(unsigned int, int))(kInstanceSize, allocating);
}

void FreeInstance(void *instance)
{
    if (!instance)
        return;
    NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(instance);
    Account(false);
    Virtual<void, void *, unsigned int, int>(*kAttribAllocator, 4, instance, kInstanceSize, 0);
}

void *NewInstance()
{
    Account(true);
    return Virtual<void *, unsigned int, int>(*kAttribAllocator, 0, kInstanceSize, 0);
}

const void *LevelReference(char *self, unsigned int key, int level)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, int))(
        At<void *>(self, 0x48), key, level);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0xc);
}
} // namespace

void *__fastcall PursuitLevelsFromSpec(unsigned int *self, void *, const void *spec, unsigned int index, void *owner);

void __fastcall AIPerpVehicleSetHeat(char *self, void *, float heat)
{
    const int was = static_cast<int>(At<float>(self, 0x1c));
    const int level = static_cast<int>(heat);
    bool roaming = false;
    char *race = static_cast<char *>(*kRaceStatus);
    if (race && At<int>(race, kRaceActivity) == 1)
    {
        void *parameters = At<void *>(race, kRaceParameters);
        roaming = !parameters || !NFS_ORIGINAL(0x5fbe70, bool(__thiscall *)(void *))(parameters);
    }
    const bool switched = roaming != (At<unsigned char>(self, 0x2e) != 0);
    At<unsigned char>(self, 0x2e) = roaming;
    if (level != was || switched)
    {
        FreeInstance(At<void *>(self, 0x40));
        At<void *>(self, 0x40) = nullptr;
        FreeInstance(At<void *>(self, 0x44));
        At<void *>(self, 0x44) = nullptr;
    }
    if (!At<void *>(self, 0x40))
    {
        const unsigned int levelsKey = roaming ? 0x2283ecaf : 0xd4b0cc11;
        const unsigned int supportKey = roaming ? 0xe5332008 : 0xf3918f68;
        void *levels = NewInstance();
        if (levels)
            levels = PursuitLevelsFromSpec(static_cast<unsigned int *>(levels), nullptr,
                                           LevelReference(self, levelsKey, level - 1), 0, nullptr);
        At<void *>(self, 0x40) = levels;
        void *support = NewInstance();
        if (support)
            NFS_ORIGINAL(0x456cb0, void *(__thiscall *)(void *, const void *, unsigned int, void *))(
                support, LevelReference(self, supportKey, level - 1), 0, nullptr);
        At<void *>(self, 0x44) = support;
    }
    At<float>(self, 0x1c) = heat;
}

namespace
{
float *const kPerpPhase = reinterpret_cast<float *>(0x90d600);

void AttachPerpInterface(char *iface, const void *params, unsigned int handle)
{
    char *object = At<char *>(const_cast<void *>(params), 4);
    At<unsigned int>(iface, 0) = 0x890970;
    At<char *>(iface, 4) = object ? object + 0x10 : nullptr;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(At<void *>(iface, 4),
                                                                        reinterpret_cast<void *>(handle), iface);
}
} // namespace

void *__fastcall AIPerpVehicleConstructor(char *self, void *, const void *params)
{
    NFS_ORIGINAL(0x43ada0, void *(__thiscall *)(void *, const void *, float, float, int))(self, params, 0.5f,
                                                                                         *kPerpPhase, 0);
    char *perp = self + 0x758;
    AttachPerpInterface(perp, params, 0x4037e0);
    At<unsigned int>(perp, 0) = 0x890a90;
    char *object = At<char *>(const_cast<void *>(params), 4);
    NFS_ORIGINAL(0x42e4a0, void *(__thiscall *)(void *, void *))(self + 0x760, object ? object + 0x10 : nullptr);
    char *cheater = self + 0x76c;
    AttachPerpInterface(cheater, params, 0x405240);
    At<unsigned int>(cheater, 0) = 0x890c1c;
    At<unsigned int>(self, 0) = 0x892398;
    At<unsigned int>(self, 8) = 0x892390;
    At<unsigned int>(self, 0x4c) = 0x8922b8;
    At<unsigned int>(self, 0x54) = 0x8922b0;
    At<unsigned int>(perp, 0) = 0x892250;
    At<unsigned int>(self, 0x760) = 0x892244;
    At<unsigned int>(cheater, 0) = 0x89223c;
    static const int kCleared[] = {0x774, 0x778, 0x77c, 0x780, 0x788, 0x794, 0x7a4, 0x7ac, 0x7b8, 0x7c0};
    for (const int offset : kCleared)
        At<unsigned int>(self, offset) = 0;
    At<unsigned char>(self, 0x784) = 0;
    At<unsigned char>(self, 0x785) = 0;
    At<unsigned char>(self, 0x786) = 0;
    At<unsigned char>(self, 0x790) = 0;
    At<float>(self, 0x7a8) = 0.5f;
    At<float>(self, 0x7bc) = -1.0f;
    *kPerpPhase += 0.5f;
    if (*kPerpPhase > 1.0f)
        *kPerpPhase = 0.0f;
    void *instance = NewInstance();
    if (instance)
    {
        const void *collection = NFS_ORIGINAL(0x455fd0, const void *(__cdecl *)(unsigned int, unsigned int))(0xd6d4330b,
                                                                                                         0xeec2271a);
        NFS_ORIGINAL(0x452380, void *(__thiscall *)(void *, const void *, unsigned int, void *))(instance, collection, 0,
                                                                                               nullptr);
    }
    At<void *>(self, 0x7a0) = instance;
    At<unsigned int>(self, 0x798) = 0;
    At<unsigned int>(self, 0x79c) = 0;
    AIPerpVehicleSetHeat(perp, nullptr, 1.0f);
    Virtual<void, unsigned int, int>(At<void *>(self, 0x34), 0x84, At<unsigned int>(self, 0x768), 0);
    At<float>(self, 0x78c) = 0.05f;
    void *error = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), 0x84, nullptr);
    if (error)
        error = NFS_ORIGINAL(0x403e50, void *(__thiscall *)(void *, int, int, float))(error, 10, 5, 1.0f);
    At<void *>(self, 0x7b0) = error;
    At<float>(self, 0x7b4) = NFS_ORIGINAL(0x45da70, float(__cdecl *)(float))(1.0f);
    return self;
}

void *__fastcall MForcePursuitStartConstruct(unsigned int *self, void *, int heat);

namespace
{
void *Find(void *object, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4),
                                                                       reinterpret_cast<void *>(handle));
}

void Brake(void *input)
{
    Virtual<void, unsigned int>(input, 0x18, 0);
    Virtual<void, unsigned int>(input, 0x1c, 0x3f800000);
    Virtual<void, unsigned int>(input, 0x14, 0);
    Virtual<void, unsigned int>(input, 0x2c, 0);
    Virtual<void, unsigned int>(input, 0x24, 0x3f800000);
    Virtual<void, unsigned int>(input, 0x20, 0);
}

void *FindZone(const float *point, int kind, void *after)
{
    return NFS_ORIGINAL(0x727480, void *(__thiscall *)(void *, const float *, int, void *))(
        reinterpret_cast<void *>(0x9aa540), point, kind, after);
}

void ReportInfraction(int infraction)
{
    NFS_ORIGINAL(0x5fdda0, void(__thiscall *)(void *, int))(*reinterpret_cast<void **>(0x91e014), infraction);
}

bool SpeedingInView(void *vehicle)
{
    return vehicle && Virtual<bool>(vehicle, 0x28) && Virtual<bool>(vehicle, 0x8c) &&
           Virtual<float>(vehicle, 0x128) < 25.0f;
}

bool IsSpeedTrapRace()
{
    return NFS_ORIGINAL(0x404af0, bool(__cdecl *)())();
}

struct AverageComplete
{
    int sum;
    int count;
};

unsigned int &ZoneHashesMade()
{
    return *reinterpret_cast<unsigned int *>(0x90d990);
}

unsigned int ZoneHash(unsigned int address)
{
    return NFS_ORIGINAL(0x460bf0, unsigned int(__cdecl *)(const char *))(reinterpret_cast<const char *>(address));
}
} // namespace

void __fastcall AIPerpVehicleUpdate(char *self, void *, float dt)
{
    unsigned int &hideHash = *reinterpret_cast<unsigned int *>(0x90d98c);
    unsigned int &escapeHash = *reinterpret_cast<unsigned int *>(0x90d988);
    if (!(ZoneHashesMade() & 1))
    {
        ZoneHashesMade() |= 1;
        hideHash = ZoneHash(0x89167c);
    }
    if (!(ZoneHashesMade() & 2))
    {
        ZoneHashesMade() |= 2;
        escapeHash = ZoneHash(0x891674);
    }
    At<float>(self, 0x7c0) = At<float>(self, 0x7c0) - dt;
    NFS_ORIGINAL(0x4292a0, void(__thiscall *)(void *, float))(self, dt);
    char *perp = self + 0x758;
    At<unsigned char>(At<char *>(self, 0x70), 0x51) =
        Virtual<bool>(perp, 0x24) && *reinterpret_cast<const unsigned char *>(0x9b3829) ? 1 : 0;
    float &catchUp = At<float>(self, 0x7ac);
    catchUp = 0.0f;
    At<float>(self, 0x7b8) = 0.0f;
    char *ai = self + 0x4c;
    const bool racing = Virtual<bool>(perp, 0x24) && Virtual<bool>(Virtual<void *>(ai, 8), 0x20);
    char *racer = Virtual<char *>(perp, 0x50);
    if (racing && racer && !Virtual<bool>(At<void *>(self, 0x34), 0x24))
    {
        void *player = *reinterpret_cast<int *>(0x92cd84) ? **reinterpret_cast<void ***>(0x92cd7c) : nullptr;
        At<float>(self, 0x7b4) = dt + At<float>(self, 0x7b4);
        if (At<float>(self, 0x7b4) > 1.0f && player)
        {
            const float done = At<float>(racer, 0x18);
            AverageComplete average;
            NFS_ORIGINAL(0x4255a0, AverageComplete *(__cdecl *)(AverageComplete *, int, int, int))(&average, 1, 0, 0);
            const float field = NFS_ORIGINAL(0x408fa0, float(__thiscall *)(void *))(&average);
            float error = (field - done) * At<float>(*reinterpret_cast<char **>(0x91e000), 0x4484) * 0.01f;
            float ahead[3] = {0.0f, 0.0f, 0.0f};
            if (Virtual<bool, float *>(player, 0xd4, ahead))
                error = ((1.0f - ahead[0]) * 0.5f + 1.0f) * error;
            NFS_ORIGINAL(0x653e20, void(__thiscall *)(void *, float, float, int, int))(At<void *>(self, 0x7b0), error,
                                                                                    At<float>(self, 0x7b4), 0, 0);
            At<float>(self, 0x7b4) = At<float>(self, 0x7b4) - 1.0f;
        }
        const bool career = Virtual<int>(Virtual<void *>(ai, 8), 0x38) == 2;
        if (NFS_ORIGINAL(0x5ff990, bool(__thiscall *)(void *, void *, void *, float *, float *, bool))(
                *reinterpret_cast<void **>(0x91e000), racer, At<void *>(self, 0x7b0), &At<float>(self, 0x7b8),
                &catchUp, career))
        {
            if (!career && (catchUp > 0.0f || catchUp < 0.0f))
            {
                if (IsSpeedTrapRace())
                    catchUp *= 0.5f;
                else
                    catchUp = TableValue(reinterpret_cast<void *>(catchUp > 0.0f ? 0x8eb4d4 : 0x8eb4e8),
                                         At<float>(self, 0x7a8)) *
                              catchUp;
            }
        }
        else
        {
            At<float>(self, 0x7b8) = 0.0f;
            catchUp = 0.0f;
        }
    }
    void *body = Virtual<void *>(Virtual<void *>(ai, 4), 0x54);
    const float *position = Virtual<const float *>(body, 0x20);
    const float x = position[0];
    float y = position[1];
    const float z = position[2];
    float box[4];
    y -= Virtual<const float *, float *>(body, 0x48, box)[1];
    const float flat[2] = {z, -x};
    At<unsigned char>(self, 0x784) = 0;
    At<unsigned char>(self, 0x785) = 0;
    bool moving = true;
    if (void *vehicle = Virtual<void *>(ai, 0xa0))
    {
        if (Virtual<float>(vehicle, 0x1c) < 0.05f)
            moving = false;
        if (Virtual<bool>(vehicle, 0x74))
        {
            if (void *input = Find(At<void *>(self, 0x34), 0x403f90))
                Brake(input);
        }
    }
    else if (--At<int>(self, 0x794) < 0)
    {
        At<int>(self, 0x794) = 10;
        void *manager = *reinterpret_cast<void **>(0x92c67c);
        char *race = *reinterpret_cast<char **>(0x91e000);
        if (manager && Virtual<bool, int>(manager, 4, 0) && (!race || !At<int>(race, 0x1960)) &&
            FindZone(flat, 0xe, nullptr))
        {
            Virtual<void, int>(*reinterpret_cast<void **>(0x92c67c), 0x2c, 0);
            unsigned int port;
            NFS_ORIGINAL(0x4023c0, void *(__thiscall *)(void *, const char *))(&port,
                                                                               reinterpret_cast<const char *>(0x891664));
            unsigned int message[8];
            MForcePursuitStartConstruct(message, nullptr, static_cast<int>(Virtual<float>(perp, 4)));
            NFS_ORIGINAL(0x654af0, void(__thiscall *)(void *, unsigned int))(message, port);
        }
    }
    int inside = 0;
    float elapsed = dt;
    for (char *zone = static_cast<char *>(FindZone(flat, 8, nullptr)); zone;
         zone = static_cast<char *>(FindZone(flat, 8, zone)))
    {
        const float floor = At<float>(zone, 0x14);
        if (floor != 0.0f && !(fabsf(y - floor) < 1.25f))
            continue;
        if (!At<unsigned char>(self, 0x790))
        {
            At<unsigned char>(self, 0x790) = 1;
            At<unsigned int>(self, 0x78c) = moving ? 0x3d4ccccd : 0x47c34f80;
        }
        else if (At<float>(self, 0x78c) > 999.0f)
            At<float>(self, 0x788) = 0.0f;
        else
            At<float>(self, 0x788) = elapsed + At<float>(self, 0x788);
        const bool stayed = At<float>(self, 0x788) > At<float>(self, 0x78c);
        elapsed = 0.0f;
        if (stayed)
        {
            const unsigned int kind = At<unsigned int>(zone, 0x30);
            if (kind == hideHash)
                At<unsigned char>(self, 0x784) = 1;
            else
            {
                At<unsigned char>(self, 0x785) = 1;
                if (kind != escapeHash)
                    At<unsigned char>(self, 0x784) = 1;
            }
        }
        ++inside;
    }
    if (!inside)
    {
        At<unsigned char>(self, 0x790) = 0;
        At<float>(self, 0x788) = 0.0f;
    }
}

void __fastcall AIPerpVehicleOnCausedCollision(char *self, void *, char *info, void *mine, void *other)
{
    const float now = NFS_ORIGINAL(0x6e8de0, float(__cdecl *)())();
    void *previous = At<void *>(self, -0x72c);
    bool same;
    if (previous)
        same = mine && At<void *>(previous, 4) == At<void *>(mine, 4);
    else
        same = !mine;
    const int kind = Virtual<int>(other, 4);
    const float last = same ? now : Virtual<float>(mine, 0x8c);
    if (kind == 2)
    {
        const unsigned int flags = At<unsigned int>(info, 0x1c);
        const void *handle = At<void *>(other, 8);
        if (handle == At<void *>(info, 0x3c) && (flags & 8))
            return;
        if (handle == At<void *>(info, 0x4c) && (flags & 0x20))
            return;
    }
    if (!(now - last <= 2.0f) || (kind != 1 && kind != 2))
    {
        Virtual<void, void *, float>(other, 0x84, At<void *>(self, 8), last);
        return;
    }
    char *perp = self - 0x714;
    void *vehicle = Virtual<void *>(perp, 0xa0);
    int points = 0;
    if (kind == 2)
    {
        alignas(8) unsigned char smackable[32];
        NFS_ORIGINAL(0x407cc0, void *(__thiscall *)(void *, void *))(smackable, Virtual<void *>(other, 0x44));
        const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
            smackable, 0x6db7d192, 0);
        if (!value)
            value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
        points = *static_cast<const int *>(value);
        void *model = Virtual<void *>(other, 0x80);
        if (model && Virtual<bool>(model, 0x54) && same && SpeedingInView(vehicle))
            ReportInfraction(0x20);
        if (previous)
        {
            if (void *hud = Virtual<void *>(previous, 0x20))
                Virtual<void, int>(hud, 0x4c, 0);
        }
        if (void *sound = At<void *>(self, -0x684))
            Virtual<void, int>(sound, 0x30, 0);
        NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(smackable);
        if (points && vehicle)
            Virtual<void, int>(self - 8, 0x18, points);
        Virtual<void, void *, float>(other, 0x84, At<void *>(self, 8), last);
        return;
    }
    const float impact = NFS_ORIGINAL(0x410180, float(__cdecl *)(const float *))(reinterpret_cast<float *>(info + 0x20));
    const bool hard = impact > 4.0f;
    bool atFault = false;
    if (same)
    {
        const bool first = Virtual<void *>(mine, 0x34) == At<void *>(info, 0x3c);
        const float sign = first ? 1.0f : -1.0f;
        const float *mineNormal = reinterpret_cast<float *>(info + (first ? 0x50 : 0x60));
        const float *otherNormal = reinterpret_cast<float *>(info + (first ? 0x60 : 0x50));
        const float *velocity = reinterpret_cast<float *>(info + 0x10);
        const float mineInto =
            (velocity[2] * mineNormal[2] + velocity[1] * mineNormal[1] + mineNormal[0] * velocity[0]) * sign * -1.0f;
        const float otherInto =
            (velocity[2] * otherNormal[2] + velocity[1] * otherNormal[1] + velocity[0] * otherNormal[0]) * sign;
        atFault = otherInto < mineInto;
    }
    void *otherVehicle = Find(other, 0x4040e0);
    if (void *pursuitAi = Find(other, 0x4038e0))
    {
        if (!Virtual<bool>(pursuitAi, 0x6c) && same && hard)
        {
            Virtual<void, int>(pursuitAi, 0x68, 1);
            if (vehicle)
            {
                Virtual<void, void *>(vehicle, 0xf0, otherVehicle);
                if (Virtual<bool>(vehicle, 0x8c) && atFault)
                    ReportInfraction(8);
            }
        }
        if (!Virtual<bool>(otherVehicle, 0x7c))
        {
            if (atFault || !same)
            {
                const int worth = atFault ? 2000 : 500;
                const float ramp = NFS_ORIGINAL(0x401ae0, float(__cdecl *)(float, float, float))(impact, 4.0f, 30.0f);
                points = static_cast<int>(ramp * static_cast<float>(worth / 50)) * 50;
                if (points <= 50)
                    points = 50;
            }
            if (same && !Virtual<bool>(pursuitAi, 0x14))
            {
                void *vehicleAi = nullptr;
                if (NFS_ORIGINAL(0x40e020, bool(__thiscall *)(void *, void **))(other, &vehicleAi) &&
                    !At<unsigned char>(Virtual<char *>(vehicleAi, 0x64), 0x2c))
                {
                    const int mode = Virtual<int>(Virtual<void *>(perp, 8), 0x58);
                    void *manager = *reinterpret_cast<void **>(0x92c67c);
                    if ((mode == 0 || mode == 6 || (manager && Virtual<bool>(manager, 0x24))) && atFault)
                        NFS_ORIGINAL(0x423860, void(__thiscall *)(void *, void *))(Virtual<void *>(vehicleAi, 0x64),
                                                                                   mine);
                }
            }
        }
    }
    if (hard && atFault)
    {
        void *traffic = nullptr;
        if (NFS_ORIGINAL(0x40e080, bool(__thiscall *)(void *, void **))(other, &traffic))
        {
            At<float>(self, 0x5c) = now;
            NFS_ORIGINAL(0x5edb50, void(__thiscall *)(void *, const char *))(*reinterpret_cast<void **>(0x91e00c),
                                                                            reinterpret_cast<const char *>(0x891680));
            if (vehicle)
                Virtual<void>(vehicle, 0xf8);
            if (void *race = *reinterpret_cast<void **>(0x91e000))
            {
                if (char *racer = NFS_ORIGINAL(0x5e8b20, char *(__thiscall *)(void *, void *))(race, mine))
                    ++At<int>(racer, 0x118);
            }
            if (SpeedingInView(vehicle))
                ReportInfraction(0x10);
        }
    }
    if (points && vehicle)
        Virtual<void, int>(self - 8, 0x18, points);
    Virtual<void, void *, float>(other, 0x84, At<void *>(self, 8), atFault ? last : 0.0f);
}
