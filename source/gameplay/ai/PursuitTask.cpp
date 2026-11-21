/** The pursuit's per-frame task: heat and its escalation, formations, which cops see the car, the bust meter, busting,
 * and the evade meter once no cop sees the car (original: zAI AIPursuit::OnTask). */
#include <float.h>
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

void __fastcall AIPursuitUpdateJerk(char *self, void *, float dt);
void __fastcall AIPursuitUpdateFormation(char *self, void *, float dt);
void __fastcall AIPursuitRemoveUnwantedVehicles(char *self);
void __fastcall AIPursuitInitFormation(char *self, void *, int cops);
void *__fastcall MPerpBustedConstruct(unsigned int *self, void *, void *simable);
void *__fastcall MNotifyPursuitLengthConstruct(unsigned int *self, void *, void *simable, float length);

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

float Constant(unsigned int address)
{
    return *reinterpret_cast<const float *>(address);
}

void *Find(void *object, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4),
                                                                       reinterpret_cast<void *>(handle));
}

float Attribute(void *instance, unsigned int key)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        instance, key, 0);
    if (!value)
        value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
    return *static_cast<const float *>(value);
}

unsigned int AttributeWord(void *instance, unsigned int key)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        instance, key, 0);
    if (!value)
        value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
    return *static_cast<const unsigned int *>(value);
}

float RandomRange(float range)
{
    void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
    return NFS_ORIGINAL(0x402870, float(__thiscall *)(void *, float))(random, range);
}

float Clamp(float value, float least, float most)
{
    return NFS_ORIGINAL(0x402060, float(__cdecl *)(float, float, float))(value, least, most);
}

bool IsPursuitRace(void *parameters)
{
    return NFS_ORIGINAL(0x5fbe70, bool(__thiscall *)(void *))(parameters);
}

void ReportInfraction(int infraction)
{
    NFS_ORIGINAL(0x5fdda0, void(__thiscall *)(void *, int))(*reinterpret_cast<void **>(0x91e014), infraction);
}

void Deliver(void *message)
{
    NFS_ORIGINAL(0x65faf0, void(__thiscall *)(void *))(message);
}

bool Query(void *object, unsigned int function, void **out)
{
    return NFS_ORIGINAL(function, bool(__thiscall *)(void *, void **))(object, out);
}

void **CopsBegin(char *self)
{
    return At<void **>(self, 0x58);
}

void **CopsEnd(char *self)
{
    return At<void **>(self, 0x58) + At<int>(self, 0x60);
}

int Now()
{
    return *reinterpret_cast<const int *>(0x925ae8);
}

const unsigned int kPlayerPort = 0x20d60dbf;
const unsigned int kBustedLatePort = 0xfea34c0a;
const unsigned int kPursuitAiHandle = 0x4038e0;
} // namespace

/** The pursuit task as the original has it; bustHeight is how far over or under the car a cop may be for the bust
 * meter (1.5 m in the original). */
bool AIPursuitOnTaskBody(char *self, void *task, float dt, float bustHeight)
{
    if (task == At<void *>(self, 0x50))
    {
        float &bust = At<float>(self, 0x11c);
        bust = At<float>(self, 0x120) + bust;
        if (bust < 0.0f)
            bust = 0.0f;
        return true;
    }
    if (task != At<void *>(self, 0x4c))
        return true;
    char *target = At<char *>(self, 0xb4);
    if (!target[0x2c])
        return true;
    char *base = self - 8;
    char *pursuit = self + 0x40;
    char *race = *reinterpret_cast<char **>(0x91e000);
    bool counting = true;
    if (race && At<int>(race, 0x1960) == 1)
        counting = At<void *>(race, 0x1968) && IsPursuitRace(At<void *>(race, 0x1968));
    At<unsigned char>(self, 0xe8) = counting;
    AIPursuitUpdateJerk(base, nullptr, dt);
    AIPursuitUpdateFormation(base, nullptr, dt);
    const float lengthBefore = At<float>(self, 0xf4);
    void *levels = nullptr;
    const bool playerPursuit = Virtual<bool>(pursuit, 0x8c);
    void *perp = nullptr;
    if (Query(target, 0x40e200, &perp))
    {
        void *perpLevels = Virtual<void *>(perp, 0x10);
        if (!At<int>(self, 0x184))
            Virtual<void>(pursuit, 0x138);
        if (Virtual<int>(pursuit, 0x114) != 2)
        {
            if (At<unsigned char>(self, 0xe8))
            {
                const float length = dt + At<float>(self, 0xf4);
                At<float>(self, 0xf4) = length;
                NFS_ORIGINAL(0x5eda20, void(__thiscall *)(void *, const char *, float))(
                    *reinterpret_cast<void **>(0x91e00c), reinterpret_cast<const char *>(0x892fe8), length);
            }
            if (static_cast<int>(lengthBefore) != static_cast<int>(At<float>(self, 0xf4)))
            {
                void *simable = At<void *>(target, 0x1c);
                unsigned int message[8];
                MNotifyPursuitLengthConstruct(message, nullptr, Virtual<void *>(simable, 0x34), At<float>(self, 0xf4));
                NFS_ORIGINAL(0x654af0, void(__thiscall *)(void *, unsigned int))(message, kPlayerPort);
            }
            float heat = Virtual<float>(perp, 4);
            if (perpLevels)
            {
                const char *data = At<char *>(perpLevels, 8);
                float scale = At<float>(const_cast<char *>(data), 0x78);
                At<unsigned int>(self, 0x144) = At<unsigned int>(const_cast<char *>(data), 0x90);
                char *frontend = At<char *>(*reinterpret_cast<char **>(0x91cf90), 0x10);
                if (frontend + 0xa8)
                {
                    int bucket;
                    if (NFS_ORIGINAL(0x404ac0, bool(__cdecl *)())())
                        bucket = 14;
                    else
                    {
                        bucket = At<unsigned char>(frontend, 0xb0);
                        if (bucket > 14)
                            bucket = 14;
                    }
                    scale = *NFS_ORIGINAL(0x403240, const float *(__thiscall *)(void *, int))(perpLevels, bucket) *
                            scale;
                }
                heat = dt / scale + heat;
            }
            Virtual<void, float>(perp, 8, Clamp(heat, At<float>(self, 0xdc), At<float>(self, 0xe0)));
            const int level = static_cast<int>(Virtual<float>(perp, 4));
            if (level != At<int>(self, 0xd8))
            {
                At<float>(self, 0x104) = 0.0f;
                At<int>(self, 0xd8) = level;
                At<unsigned char>(self, 0x254) = 0;
                At<unsigned int>(self, 0x174) = At<unsigned int>(At<char *>(Virtual<char *>(perp, 0x10), 8), 0x88);
            }
        }
        levels = Virtual<void *>(perp, 0x10);
    }
    At<float>(self, 0x108) = At<float>(self, 0x108) - dt;
    At<float>(self, 0x104) = At<float>(self, 0x104) - dt;
    At<float>(self, 0x10c) = At<float>(self, 0x10c) - dt;
    At<float>(self, 0x110) = At<float>(self, 0x110) - dt;
    At<float>(self, 0x250) = At<float>(self, 0x250) - dt;
    At<float>(self, 0x12c) = At<float>(self, 0x12c) - dt;
    At<float>(self, 0x170) = dt + At<float>(self, 0x170);
    char *support = self + 0x1d4;
    if (At<void *>(support, 0x78))
    {
        At<float>(support, 0x74) = At<float>(support, 0x74) - dt;
        if (At<float>(support, 0x74) < 0.0f)
            NFS_ORIGINAL(0x42b410, void(__thiscall *)(void *))(support);
    }
    char *sound = *reinterpret_cast<char **>(0x993cc8);
    if (sound && At<int>(sound, 0x138) == 1)
        At<int>(self, 0x118) = Now();
    const float quiet = static_cast<float>(Now() - At<int>(self, 0x118)) * 0.00025f;
    race = *reinterpret_cast<char **>(0x91e000);
    bool formationsAllowed = true;
    if (!(At<void *>(race, 0x1968) && IsPursuitRace(At<void *>(race, 0x1968))) && *reinterpret_cast<int *>(0x8f8700))
        formationsAllowed = quiet > 15.0f;
    if (At<float>(self, 0x104) <= 0.0f && levels && !Virtual<bool>(pursuit, 0x58) && !At<unsigned char>(self, 0x128) &&
        !At<unsigned char>(self, 0x129) && formationsAllowed)
    {
        const int formations = NFS_ORIGINAL(0x402fb0, int(__thiscall *)(void *))(levels);
        if (formations > 0)
        {
            int kind = 7;
            float stagger = *NFS_ORIGINAL(0x403340, const float *(__thiscall *)(void *))(levels);
            void *current = At<void *>(self, 0xc4);
            if (current && !Virtual<bool>(current, 0x24) && !Virtual<short>(current, 0x28) &&
                !Virtual<short>(current, 0x2c))
                kind = 4;
            else if (At<unsigned int>(self, 0xfc) & 1)
            {
                using Formation = const char *(__thiscall *)(void *, int);
                float total = 0.0f;
                for (int i = 0; i < formations; ++i)
                    total = total + At<float>(const_cast<char *>(NFS_ORIGINAL(0x402f90, Formation)(levels, i)), 8);
                float pick = RandomRange(total);
                for (int i = 0; i < formations; ++i)
                {
                    const char *entry = NFS_ORIGINAL(0x402f90, Formation)(levels, i);
                    pick = pick - At<float>(const_cast<char *>(entry), 8);
                    if (pick < 0.0f || pick == 0.0f)
                    {
                        kind = At<int>(const_cast<char *>(entry), 0);
                        stagger = At<float>(const_cast<char *>(entry), 4);
                        break;
                    }
                }
            }
            ++At<unsigned int>(self, 0xfc);
            if (kind != At<int>(self, 0x100))
            {
                At<int>(self, 0x100) = kind;
                AIPursuitInitFormation(base, nullptr, Virtual<int>(pursuit, 0xc));
            }
            At<float>(self, 0x104) = RandomRange(3.0f) + stagger;
        }
    }
    AIPursuitRemoveUnwantedVehicles(base);
    At<float>(self, 0x168) = dt + At<float>(self, 0x168);
    float nearest = FLT_MAX;
    float nearestLevel = FLT_MAX;
    const float sight = levels ? At<float>(At<char *>(levels, 8), 0x98) : 150.0f;
    int seeing = 0;
    const float *chased = &At<float>(target, 0x10);
    for (void **it = CopsBegin(self); it != CopsEnd(self); ++it)
    {
        void *cop = *it;
        if (!Virtual<bool>(cop, 0x88) || Virtual<bool>(cop, 0x7c))
            continue;
        void *pursuitAi = Find(cop, kPursuitAiHandle);
        if (pursuitAi)
        {
            const float lastSeen = Virtual<float>(pursuitAi, 0x74);
            if (lastSeen < At<float>(self, 0x168))
                At<float>(self, 0x168) = lastSeen;
        }
        const float *at = Virtual<const float *>(cop, 0xc);
        const float rise = at[1] - chased[1];
        const float distance =
            NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const float *, const float *))(Virtual<const float *>(cop, 0xc),
                                                                                  chased);
        at = Virtual<const float *>(cop, 0xc);
        const float dz = at[2] - chased[2];
        const float dx = at[0] - chased[0];
        const float flat = sqrtf(dx * dx + dz * dz);
        if (fabsf(rise) < bustHeight && flat < nearestLevel)
            nearestLevel = flat;
        if (distance < nearest)
            nearest = distance;
        if (distance < sight)
        {
            if (!Virtual<bool, void *>(pursuit, 0x4c, cop))
                ++seeing;
            if (pursuitAi && At<int>(self, 0x258) != 2)
                Virtual<void>(pursuitAi, 0x88);
        }
    }
    At<int>(self, 0x14c) = seeing;
    int missing = At<int>(self, 0x184) - At<int>(self, 0x18c);
    if (missing <= 0)
    {
        ++At<int>(self, 0x184);
        ++missing;
    }
    if (seeing - missing > 0)
        At<int>(self, 0x184) += seeing - missing;
    if (At<int>(self, 0x184) && !At<int>(self, 0x258) && missing <= At<int>(self, 0x188) && levels)
    {
        At<int>(self, 0x258) = 1;
        At<unsigned int>(self, 0x25c) = At<unsigned int>(At<char *>(levels, 8), 0xa0);
    }
    if (race && At<void *>(race, 0x1968) &&
        NFS_ORIGINAL(0x5fc560, bool(__thiscall *)(void *))(At<void *>(race, 0x1968)))
        At<float>(self, 0x168) = 0.0f;
    if (void *formation = At<void *>(self, 0xc4))
    {
        if (perp && !Virtual<bool>(perp, 0x2c))
        {
            float flatGap;
            const float gap = Virtual<float, float, float *, void *>(formation, 0x44, dt, &flatGap, self + 0xb8);
            if (gap < nearest)
            {
                nearest = gap;
                if (gap < 60.0f)
                {
                    At<unsigned char>(self, 0x154) = 1;
                    At<float>(self, 0x168) = 0.0f;
                }
            }
            if (flatGap < nearestLevel)
                nearestLevel = flatGap;
            At<float>(self, 0xbc) = gap;
        }
        void *roadBlockCop = At<void *>(self, 0xb8);
        if (Virtual<bool>(At<void *>(self, 0xc4), 0x4c) && !At<int>(self, 0x27c) && roadBlockCop &&
            Virtual<bool, void *>(At<void *>(self, 0xc4), 0xc, roadBlockCop))
        {
            Virtual<void, void *>(pursuit, 0x78, At<void *>(self, 0xb8));
            ++At<int>(self, 0x184);
            ++At<int>(self, 0x27c);
        }
    }
    else
    {
        At<float>(self, 0xbc) = 0.0f;
        At<void *>(self, 0xb8) = nullptr;
    }
    At<float>(self, 0x260) = nearest;
    const float bustSpeed = (levels ? Attribute(levels, 0x769e8d9e) : 0.0f) * Constant(0x89098c);
    float &bustRate = At<float>(self, 0x120);
    if (At<unsigned char>(self, 0x128))
        bustRate = dt * 0.25f;
    else if (!At<unsigned char>(self, 0x154) || At<unsigned char>(self, 0x129))
        bustRate = dt * -0.5f;
    else
    {
        bool stuck = false;
        void *vehicle = nullptr;
        if (Query(target, 0x40e6c0, &vehicle) && Virtual<int>(vehicle, 0x14) == 1)
            stuck = true;
        const float reach = stuck ? Constant(0x892fa8) : 15.0f;
        const bool slow = stuck || NFS_ORIGINAL(0x409ed0, float(__thiscall *)(void *))(target) < bustSpeed;
        if (slow && nearestLevel < reach)
            bustRate = stuck ? dt * 0.25f * 4.0f : dt * 0.25f;
        else
            bustRate = dt * -0.5f;
        void *flow = *reinterpret_cast<void **>(0x9885c8);
        if (flow && Virtual<bool>(flow, 0x64))
            bustRate = 0.0f;
        if (static_cast<int>(lengthBefore * 0.1f) != static_cast<int>(At<float>(self, 0xf4) * 0.1f))
            Virtual<void, unsigned int>(perp, 0x3c, At<unsigned int>(self, 0x174));
        if (At<int>(self, 0x258) == 1)
        {
            At<float>(self, 0x25c) = At<float>(self, 0x25c) - dt;
            if (At<float>(self, 0x25c) < 0.0f)
            {
                At<int>(self, 0x258) = 0;
                Virtual<void>(pursuit, 0x138);
            }
        }
    }
    const bool online = NFS_ORIGINAL(0x735620, bool(__thiscall *)(void *))(reinterpret_cast<void *>(0x9b0fa0));
    if (!online && !At<unsigned char>(self, 0x128) && !At<unsigned char>(self, 0x129) &&
        At<float>(self, 0x11c) > 5.0f)
    {
        At<unsigned char>(self, 0x128) = 1;
        At<int>(self, 0x258) = 3;
        if (playerPursuit)
        {
            for (void **it = CopsBegin(self); it != CopsEnd(self); ++it)
            {
                void *cop = *it;
                void *pursuitAi = nullptr;
                if (!Virtual<bool>(cop, 0x88) || Virtual<bool>(cop, 0x7c) || !Query(cop, 0x40e100, &pursuitAi))
                    continue;
                Virtual<void, int>(cop, 0xa4, 0x7000);
                unsigned int goal = stringhash32(reinterpret_cast<const char *>(0x892fd8));
                Virtual<void, unsigned int *>(pursuitAi, 0x48, &goal);
                Virtual<void>(pursuitAi, 0x1c);
            }
            unsigned int message[8];
            MPerpBustedConstruct(message, nullptr, Virtual<void *>(At<void *>(At<char *>(self, 0xb4), 0x1c), 0x34));
            message[1] = kPlayerPort;
            Deliver(message);
        }
        else
        {
            Virtual<void>(pursuit, 0x110);
            if (void *status = *reinterpret_cast<void **>(0x91e000))
            {
                if (void *racer = NFS_ORIGINAL(0x5e8b20, void *(__thiscall *)(void *, void *))(
                        status, At<void *>(At<char *>(self, 0xb4), 0x1c)))
                {
                    NFS_ORIGINAL(0x600370, void(__thiscall *)(void *))(racer);
                    NFS_ORIGINAL(0x5e9240, void(__thiscall *)(void *))(racer);
                    void *simable = At<void *>(At<char *>(self, 0xb4), 0x1c);
                    unsigned int port;
                    NFS_ORIGINAL(0x4023c0, void *(__thiscall *)(void *, const char *))(
                        &port, reinterpret_cast<const char *>(0x892fc8));
                    unsigned int message[8];
                    MPerpBustedConstruct(message, nullptr, Virtual<void *>(simable, 0x34));
                    message[1] = port;
                    Deliver(message);
                }
            }
        }
    }
    else if (At<unsigned char>(self, 0x128) && playerPursuit)
    {
        const float late = Constant(0x8eb318);
        const bool already = At<float>(self, 0x124) > late;
        At<float>(self, 0x124) = dt + At<float>(self, 0x124);
        if (!already && At<float>(self, 0x124) > late)
        {
            unsigned int message[8];
            MPerpBustedConstruct(message, nullptr, Virtual<void *>(At<void *>(At<char *>(self, 0xb4), 0x1c), 0x34));
            message[1] = kBustedLatePort;
            Deliver(message);
        }
    }
    if (At<unsigned char>(self, 0x129))
    {
        for (void **it = CopsBegin(self); it != CopsEnd(self); ++it)
        {
            void *cop = *it;
            if (!Virtual<bool>(cop, 0x88) || Virtual<bool>(cop, 0x7c))
                continue;
            if (void *pursuitAi = Find(cop, kPursuitAiHandle))
                Virtual<void>(pursuitAi, 0xc);
        }
    }
    At<unsigned char>(self, 0x154) = At<float>(self, 0x168) < 7.0f;
    if (perp)
    {
        if ((Virtual<bool>(perp, 0x2c) || Virtual<bool>(perp, 0x30)) && !At<unsigned char>(self, 0x154))
        {
            const float rate = levels ? Attribute(levels, 0x7fcee250) : 3.0f;
            At<float>(self, 0x164) = rate * dt + At<float>(self, 0x164);
        }
        if (At<unsigned char>(self, 0x154))
        {
            At<float>(self, 0x164) = 0.0f;
            for (int i = 0; i < 3; ++i)
                At<float>(self, 0x158 + i * 4) = chased[i];
        }
    }
    const float unseen = At<float>(self, 0x164) + At<float>(self, 0x168);
    if (At<float>(self, 0x168) > 7.0f)
    {
        const float evade = unseen / At<float>(self, 0x144);
        At<float>(self, 0x150) = -1.0f;
        At<float>(self, 0x13c) = evade;
        if (!At<unsigned char>(self, 0x16c))
        {
            At<float>(self, 0x13c) = 0.0f;
            if (At<float>(self, 0x170) > 2.5f)
            {
                At<float>(self, 0x170) = 0.0f;
                At<unsigned char>(self, 0x16c) = 1;
                At<int>(self, 0x258) = 2;
                const float most = At<float>(At<char *>(levels, 8), 0x80);
                const float now = At<float>(self, 0x10c);
                At<float>(self, 0x10c) = !(now < most) ? most : now;
                At<float>(self, 0x25c) = 0.0f;
                At<unsigned char>(self, 0x114) = 1;
                if (Virtual<bool>(pursuit, 0x8c))
                    ReportInfraction(0x40);
            }
        }
        else if (evade < Constant(0x890ea8))
            At<unsigned int>(self, 0x13c) = 0x3d4ccccd;
        else if (evade >= 1.0f)
        {
            At<float>(self, 0x13c) = 1.0f;
            At<int>(self, 0x258) = 4;
            if (Virtual<bool>(pursuit, 0x8c))
            {
                if (void *manager = *reinterpret_cast<void **>(0x92c67c))
                    Virtual<void, int>(manager, 0x2c, 1);
            }
        }
    }
    else
    {
        if (At<float>(self, 0x168) > Constant(0x892fc4))
        {
            const float lean = -0.5f - At<float>(self, 0x168) * Constant(0x892fc0);
            float pull;
            if (!(lean > -1.0f))
                pull = -1.0f;
            else
                pull = -0.5f >= lean ? lean : -0.5f;
            At<float>(self, 0x150) = pull;
        }
        else if (levels)
        {
            const unsigned int nearBits = AttributeWord(levels, 0x8ce3219f);
            const float near = *reinterpret_cast<const float *>(&nearBits);
            const float far = Attribute(levels, 0xeda0e29c);
            const float range = At<float>(At<char *>(levels, 8), 0xb0);
            At<float>(self, 0x150) = 0.0f;
            if (nearest > far)
            {
                const float lean = static_cast<float>(-0.1 - (nearest - far) / (range - far) * 0.4f);
                float pull;
                if (!(lean > -0.5f))
                    pull = -0.5f;
                else
                    pull = Constant(0x891698) >= lean ? lean : Constant(0x891698);
                At<float>(self, 0x150) = pull;
            }
            else if (nearest < near)
            {
                void *simable = At<void *>(At<char *>(self, 0xb4), 0x1c);
                if (simable && Virtual<float>(Virtual<void *>(simable, 0x54), 0x30) > Constant(0x892fb0))
                    At<float>(self, 0x150) = 0.0f;
                else
                {
                    const float speed = NFS_ORIGINAL(0x409ed0, float(__thiscall *)(void *))(At<char *>(self, 0xb4));
                    const float fast = Constant(0x890d40);
                    const float slowing = Clamp((fast - speed) / (fast - bustSpeed), 0.0f, 1.0f) * Constant(0x89107c);
                    const float closing = Clamp((near - nearest) / (near - 15.0f), 0.0f, 1.0f) * Constant(0x8906a0);
                    At<float>(self, 0x150) = (closing + slowing) * Constant(0x891054) + Constant(0x890cd4);
                }
            }
        }
        if (At<unsigned char>(self, 0x16c))
        {
            const float faded = At<float>(self, 0x13c) * Constant(0x892fac);
            At<float>(self, 0x13c) = faded;
            if (faded < Constant(0x890ea8))
                At<unsigned int>(self, 0x13c) = 0x3d4ccccd;
            if (At<float>(self, 0x170) > 2.5f)
            {
                At<float>(self, 0x170) = 0.0f;
                At<unsigned char>(self, 0x16c) = 0;
                At<int>(self, 0x258) = 0;
                At<unsigned char>(self, 0x114) = 0;
            }
        }
        else
            At<float>(self, 0x13c) = 0.0f;
    }
    float left = At<float>(self, 0x144) - unseen;
    if (0.0f > left)
        left = 0.0f;
    At<float>(self, 0x140) = left;
    if (Virtual<float>(pursuit, 0x24) < At<float>(self, 0x140))
        At<float>(self, 0x140) = Virtual<float>(pursuit, 0x24);
    if (At<unsigned char>(self, 0x128) || At<unsigned char>(self, 0x129))
        At<float>(self, 0x13c) = 0.0f;
    return true;
}
