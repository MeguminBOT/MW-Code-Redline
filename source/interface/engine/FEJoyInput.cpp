/**
 * The front end's pad reading (original: zFe cFEngJoyInput::HandleJoy 0x559400): port 0's action queue ("FEng") is
 * read into the table of front-end pad events (MapJoyEventToFEPad 0x8f3428: 16 entries of an action, the pad button it
 * makes, its name, then its value per port), which the engine turns into its pads' buttons. A connect action (0x87)
 * enables the queue (and the second player's, in split screen); EA Trax's second button and the 0x28 action are
 * noted for FEManager. Then CheckUnplugged (FEManagerInput.cpp).
 *
 * Not in the original: `[Input] FEFirstConnect` (on by default). The queue only reads once it is enabled, and only a
 * connect action enables it. The rebuild's devices are there from the first frame, so that action can come before the
 * front end's reader exists, and the menus then ignored the keys until the window lost and regained focus (which sends
 * a disconnect and a connect). When the queue is connected but was never enabled, it is enabled as the connect action
 * would have done, once. The queue's port, connection, enabling and input map are logged when they change ("fe input:");
 * with -set Debug.KeyLog=1 also the map's entries (once) and every action the menus take from the queue. An empty map
 * means the menus hear nothing: the "controller" attribute collection's records lost their channel names (2026-11-21).
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const int kConnect = 0x87;
const int kEaTraxSecond = 0x27;
const int kAction28 = 0x28;
const int kEvents = 16;
const int kEventWords = 7;
int *const kEventTable = reinterpret_cast<int *>(0x8f3428);
char **const kFEDatabase = reinterpret_cast<char **>(0x91cf90);
char **const kFEManager = reinterpret_cast<char **>(0x91cae0);
const unsigned char *const kSuppressA = reinterpret_cast<const unsigned char *>(0x91d8c0);
const int *const kSuppressB = reinterpret_cast<const int *>(0x91d8c4);

using QueueTest = bool(__thiscall *)(void *);
using QueueCall = void(__thiscall *)(void *);
using QueueEnable = void(__thiscall *)(void *, bool);
using QueueGet = void(__thiscall *)(void *, int **);
const QueueTest kIsEmpty = reinterpret_cast<QueueTest>(0x633d80);
const QueueTest kIsEnabled = reinterpret_cast<QueueTest>(0x626dd0);
const QueueTest kIsConnected = reinterpret_cast<QueueTest>(0x626d70);
const QueueCall kPopAction = reinterpret_cast<QueueCall>(0x633d90);
const QueueEnable kEnable = reinterpret_cast<QueueEnable>(0x644060);
const QueueGet kGetAction = reinterpret_cast<QueueGet>(0x633dc0);
using UserMode = int(__cdecl *)();
const UserMode kGetUserMode = reinterpret_cast<UserMode>(0x6e8df0);
using ManagerCall = void(__thiscall *)(void *);
const ManagerCall kSetEaTraxSecondButton = reinterpret_cast<ManagerCall>(0x517070);

void EnableIfOff(void *queue)
{
    if (queue && !kIsEnabled(queue))
        kEnable(queue, true);
}

bool g_connectSeen;
} // namespace

bool __fastcall CheckUnplugged(cFEngJoyInput *j, void *);
void __fastcall HandleJoy(void *self, void *);
NFS_REPLACE(0x559400, HandleJoy)

void __fastcall HandleJoy(void *self, void *)
{
    void **queues = static_cast<void **>(self);
    const int port = 0;
    if (void *queue = queues[port])
    {
        // The queue's state as the menus see it, logged when it changes (the keyboard fault's evidence).
        static int lastState = -1, logged = 0;
        const int state = (kIsConnected(queue) ? 1 : 0) | (kIsEnabled(queue) ? 2 : 0) | (At<int>(queue, 0x268) << 2);
        if (state != lastState && logged < 32)
        {
            lastState = state;
            ++logged;
            nfs::Log("fe input: menus' queue port %d connected %d enabled %d map %p config %08x", At<int>(queue, 0x268),
                     state & 1, (state >> 1) & 1, At<void *>(queue, 0x270), At<unsigned int>(queue, 0x274));
        }
        static bool mapDumped;
        if (!mapDumped && At<void *>(queue, 0x270) && nfs::config::Int("Debug", "KeyLog", 0))
        {
            mapDumped = true;
            int **sentinel = At<int **>(At<void *>(queue, 0x270), 4);
            int n = 0;
            for (int *e = *reinterpret_cast<int **>(sentinel); e != reinterpret_cast<int *>(sentinel) && n < 64;
                 e = *reinterpret_cast<int **>(e), ++n)
                nfs::Log("fe input: map entry type %d action %#x channel %d", e[2], e[5], e[6]);
            nfs::Log("fe input: %d map entries", n);
        }
        static const bool firstConnect = nfs::config::Int("Input", "FEFirstConnect", 1) != 0;
        if (firstConnect && !g_connectSeen && kIsConnected(queue) && !kIsEnabled(queue))
        {
            g_connectSeen = true;
            kEnable(queue, true);
            nfs::Log("fe input: the menus' queue was connected but never enabled; enabled");
        }
        while (!kIsEmpty(queue))
        {
            int *action = nullptr;
            kGetAction(queue, &action);
            static const bool keyLog = nfs::config::Int("Debug", "KeyLog", 0) != 0;
            if (keyLog && action)
                nfs::Log("fe input: action %#x %d%% (connected %d enabled %d)", action[0],
                         static_cast<int>(*reinterpret_cast<const float *>(action + 2) * 100.0f),
                         kIsConnected(queue) ? 1 : 0, kIsEnabled(queue) ? 1 : 0);
            if (action && action[0] == kConnect)
            {
                g_connectSeen = true;
                const char *db = *kFEDatabase;
                const bool splitScreen = (At<unsigned int>(db, 300) & 4) && db[0] == 2;
                const bool twoPlayers = kGetUserMode() == 1 || splitScreen;
                const int first = db[0xc];
                const int second = twoPlayers ? db[0xd] : -1;
                if (port == first)
                {
                    if (twoPlayers && second != -1)
                        EnableIfOff(second == 4 ? queues[0] : queues[second]);
                }
                else if (port == second && twoPlayers && first != -1)
                    EnableIfOff(first == 4 ? queues[0] : queues[first]);
                EnableIfOff(queue);
                kPopAction(queue);
                continue;
            }
            kIsEnabled(queue);
            for (int e = 0; e < kEvents; ++e)
            {
                int *entry = kEventTable + e * kEventWords;
                if (!kIsConnected(queue))
                {
                    entry[3 + port] = 0;
                    continue;
                }
                if (entry[0] != (action ? action[0] : 0))
                    continue;
                const float value = action ? *reinterpret_cast<const float *>(action + 2) : 0.0f;
                entry[3 + port] = static_cast<int>(value + 0.5f);
                if ((!*kSuppressA || !*kSuppressB) && action)
                {
                    if (action[0] == kEaTraxSecond)
                    {
                        if (value == 1.0f)
                            kSetEaTraxSecondButton(*kFEManager);
                    }
                    else if (action[0] == kAction28)
                        At<unsigned char>(*kFEManager, 0x2c) = value == 1.0f ? 1 : 0;
                }
                break;
            }
            kPopAction(queue);
        }
    }
    CheckUnplugged(static_cast<cFEngJoyInput *>(self), nullptr);
}
