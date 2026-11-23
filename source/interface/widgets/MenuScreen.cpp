/**
 * Every menu screen's common message handling before its own (original: zFe2 MenuScreen::CheckKeyboard 0x59b2c0,
 * BaseNotify 0x5a4820, BaseNotifySound 0x5724d0).
 *
 * MenuScreen: +0xc takes messenger popups, +0x10 the package's name, +0x18 its FEPackage (+0x10 name hash, +0x20 mouse
 * objects, +0x28 has control), +0x24 the text input object, +0x28 its keyboard state (1 opened, 2 shown, 3 typing).
 * BaseNotifySound plays a front-end sound per message (its table; the screen's NotifySoundMessage may change it).
 * The online branches (lobby invites, messenger, LAN login, connection loss) are left out with the online screens.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include <string.h>

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kTick = 0xc98356ba;
const unsigned int kTextCancel = 0xd15c04ec;
const unsigned int kTextEscape = 0x911ab364;
const unsigned int kTextReturn = 0x406415e3;
const unsigned int kQuitGame = 0x62799a4c;
const unsigned int kExitGame = 0xbe34e004;
const unsigned int kMessengerIcons = 0x9757a0c0;
const unsigned int kMouse = 0x9803f6e2;
const unsigned int kButtonAction = 0xe92e4847;
const unsigned int kButtonActionSoon = 0xf764cbe7;

bool *const kTextInputActive = reinterpret_cast<bool *>(0x91d8c0);
int *const kTextInputObject = reinterpret_cast<int *>(0x91d8c4);
char *const kKeyboardManager = reinterpret_cast<char *>(0x91d4b0);
char *const kKeyboardText = reinterpret_cast<char *>(0x91d5b0);
char *const kKeyboardOriginal = reinterpret_cast<char *>(0x91d7b4);
int *const kButtonActionCountdown = reinterpret_cast<int *>(0x8f48e8);
bool *const kEnteringChallenge = reinterpret_cast<bool *>(0x91d261);
const int *const kGameFlowState = reinterpret_cast<const int *>(0x925e90);
int *const kExitTheGame = reinterpret_cast<int *>(0x9257ec);
const char *const kEmpty = reinterpret_cast<const char *>(0x890978);

using ShowTwoButtons = int(__cdecl *)(const char *, const char *, int, unsigned int, unsigned int, unsigned int,
                                      unsigned int, unsigned int, int, unsigned int, ...);
const ShowTwoButtons kShowTwoButtons = reinterpret_cast<ShowTwoButtons>(0x598c00);
using Plain = void(__thiscall *)(void *);
const Plain kUpdateEAMIcons = reinterpret_cast<Plain>(0x572ed0);

struct MessageSound
{
    unsigned int message;
    int sound;
};

/** BaseNotifySound's messages and their sounds, by message. */
const MessageSound kMessageSounds[] = {
    {0x00b06e92, 140}, {0x01cd9276, 114}, {0x0217c99f, 146}, {0x08464444, 111}, {0x0a888ddc, 107}, {0x0bddc0d2, 142},
    {0x145b13c0, 88}, {0x14c293b4, 131}, {0x14c85c88, 126}, {0x1abbe49b, 103}, {0x1b325f9d, 138}, {0x1ca2354c, 148},
    {0x1cfad52b, 20}, {0x1e9ae519, 42}, {0x299148c8, 87}, {0x2ac2cd24, 47}, {0x2dd4fe18, 38}, {0x2e21e843, 54},
    {0x2e287c68, 51}, {0x2e70dc92, 127}, {0x3159062b, 40}, {0x334f3b79, 27}, {0x334f3b7a, 28}, {0x3a62513e, 111},
    {0x3b7fac4c, 112}, {0x3b7ff2ab, 117}, {0x40927e58, 116}, {0x43be891a, 26}, {0x44d43738, 145}, {0x4688f23f, 115},
    {0x46bb84c6, 41}, {0x478ffb47, 89}, {0x480c9a58, 5}, {0x480df13f, 0}, {0x4810a91b, 28}, {0x48122792, 2},
    {0x48ed7349, 144}, {0x4949060a, 106}, {0x4a7acc5a, 5}, {0x4a7c2341, 0}, {0x4a7e0941, 133}, {0x4a805994, 2},
    {0x4ac5e165, 2}, {0x4b680587, 155}, {0x4beaf019, 50}, {0x4e1da7e5, 85}, {0x4e7ccc80, 18}, {0x4f072eca, 157},
    {0x4fb99ccd, 26}, {0x4fb99cce, 41}, {0x4fbfb02b, 74}, {0x50b3fa79, 135}, {0x52f29a56, 118}, {0x52f69b81, 105},
    {0x54ebd897, 123}, {0x56d0cb98, 39}, {0x58aa827a, 88}, {0x59e562d8, 109}, {0x5a0fdf7c, 24}, {0x5a0fdf7d, 25},
    {0x5c928ac2, 14}, {0x610fb233, 7}, {0x610fb238, 7}, {0x63e5ed6f, 150}, {0x65a03426, 17}, {0x672dfafd, 134},
    {0x6a95f040, 15}, {0x6a9aa0ea, 16}, {0x6b267e15, 113}, {0x6b283007, 10}, {0x6bc1cd5d, 10}, {0x6e437a7b, 159},
    {0x7238782d, 104}, {0x7358a1f0, 74}, {0x746d7cdb, 152}, {0x7b6b89d7, 4}, {0x7c002dca, 153}, {0x7d35da25, 12},
    {0x7f63059b, 136}, {0x802009f9, 48}, {0x80f21152, 156}, {0x81da4b22, 108}, {0x84d07af9, 115}, {0x8c40ed95, 89},
    {0x8e2658c6, 112}, {0x8e269f25, 117}, {0x8f7385ab, 110}, {0x90429475, 46}, {0x905fb857, 12}, {0x91970159, 21},
    {0x958e0bd3, 151}, {0x97b397b0, 114}, {0x997dd91d, 158}, {0x9afa53a7, 3}, {0x9b59e056, 7}, {0x9f032957, 137},
    {0xa07f7ea9, 132}, {0xa3cc3462, 107}, {0xa4f871db, 75}, {0xa6362b4b, 139}, {0xaab576ab, 74}, {0xab7a5fc7, 105},
    {0xac57bdb2, 19}, {0xad0fbb98, 130}, {0xadce9eec, 125}, {0xb11fb113, 147}, {0xb1bf9795, 103}, {0xb205316c, 0},
    {0xb25c8563, 86}, {0xb4ff8cd3, 124}, {0xb6471cc6, 90}, {0xb86367cb, 154}, {0xb8afe646, 53}, {0xba0159b3, 129},
    {0xc45af53b, 25}, {0xc477473a, 22}, {0xc4a06d0f, 89}, {0xc989909f, 10}, {0xcaf4d21e, 116}, {0xd15f7512, 109},
    {0xd792ff31, 110}, {0xd93bcf6e, 141}, {0xdc49a694, 6}, {0xddd9c41e, 49}, {0xdedf0b4c, 75}, {0xe28cac90, 106},
    {0xe2a0904f, 113}, {0xe3fb93be, 11}, {0xe7dafaee, 0}, {0xe9b28a67, 104}, {0xea2747c5, 149}, {0xed78878c, 52},
    {0xee0eea7e, 143}, {0xee54c987, 13}, {0xf4b32d4d, 11}, {0xf9545d5c, 108}, {0xfa274177, 128}, {0xfabbbf40, 11},
    {0xfc543e69, 4},
};

int SoundFor(unsigned int message)
{
    int low = 0, high = static_cast<int>(sizeof(kMessageSounds) / sizeof(kMessageSounds[0])) - 1;
    while (low <= high)
    {
        const int mid = (low + high) >> 1;
        const unsigned int m = kMessageSounds[mid].message;
        if (m == message)
            return kMessageSounds[mid].sound;
        if (m < message)
            low = mid + 1;
        else
            high = mid - 1;
    }
    return -1;
}

EAXSound *const *const kSound = reinterpret_cast<EAXSound *const *>(0x911fa8);

bool IsQuittable(const MenuScreen *screen)
{
    const unsigned int name = At<unsigned int>(At<char *>(screen, 0x18), 0x10);
    static const char *const kNot[] = {"EA_TRAX_JUKEBOX.FNG", "MC_MAIN.FNG", "FEANYMOVIE.FNG", "LS_PSA.FNG",
                                       "LS_EALOGO.FNG", "MW_LS_ATTRACTFMV.FNG", "MW_LS_SPLASH.FNG", "LOADING.FNG"};
    for (const char *n : kNot)
        if (name == FEngHashString(n))
            return false;
    return true;
}

bool IsPackage(const MenuScreen *screen, const char *name)
{
    return At<unsigned int>(At<char *>(screen, 0x18), 0x10) == FEHashUpper(name);
}
} // namespace

bool MenuScreen::CheckKeyboard(unsigned int message)
{
    FEngTextInputObject *input = At<FEngTextInputObject *>(this, 0x24);
    unsigned char &state = At<unsigned char>(this, 0x28);
    if (message == kTick && input)
    {
        if (state == 1)
        {
            state = 2;
            return true;
        }
        if (state == 2)
        {
            state = 3;
            *kTextInputActive = true;
        }
    }
    input = At<FEngTextInputObject *>(this, 0x24);
    if (!input)
        return false;
    if (message == kTextCancel)
    {
        reinterpret_cast<KeyboardEditString *>(kKeyboardManager)->RevertToOriginalString();
        *kTextInputActive = false;
        *kTextInputObject = 0;
        memset(kKeyboardText, 0, 0x200);
        memset(kKeyboardOriginal, 0, 0x100);
        memset(kKeyboardManager, 0, 0x100);
        At<FEngTextInputObject *>(this, 0x24)->RedrawString(true);
        FEngEndTextInput();
        return false;
    }
    if (*kTextInputActive && *kTextInputObject)
    {
        if (message == kTextEscape)
            input->EscapePressed();
        else if (message == kTextReturn)
            input->ReturnPressed();
        else
            input->Notify(message);
    }
    return true;
}

void MenuScreen::BaseNotify(unsigned int message, FEObject *o, unsigned int a, unsigned int b)
{
    if (CheckKeyboard(message))
        return;
    char *package = At<char *>(this, 0x18);
    const char *name = At<const char *>(this, 0x10);
    switch (message)
    {
    case kQuitGame:
    {
        FEPackage *base = cFEng::mInstance->FindPackageAtBase();
        FEPackage *control = cFEng::mInstance->FindPackageWithControl();
        if (*kGameFlowState == 3 && IsQuittable(this) && At<bool>(package, 0x28) &&
            package == reinterpret_cast<char *>(control) && control == base)
            kShowTwoButtons(name, kEmpty, 3, 0x70e01038, 0x417b25e4, kExitGame, 0x34dc1bcf, 0x34dc1bcf, 1, 0x75cac3b);
        break;
    }
    case kExitGame:
        *kExitTheGame = 1;
        break;
    case kMessengerIcons:
        kUpdateEAMIcons(this);
        break;
    case kMouse:
        if (!At<void *>(package, 0x20))
            return;
        break;
    case kTick:
        if (*kButtonActionCountdown >= 0 &&
            reinterpret_cast<char *>(cFEng::mInstance->FindPackageAtBase()) == package)
        {
            if (*kButtonActionCountdown > 0)
                --*kButtonActionCountdown;
            if (*kButtonActionCountdown == 0)
            {
                *kButtonActionCountdown = -1;
                cFEng::mInstance->QueueGameMessage(kButtonAction, name, 0xff);
            }
        }
        break;
    case kButtonActionSoon:
        *kButtonActionCountdown = 5;
        break;
    case kButtonAction:
        if (IsPackage(this, "GarageMain.fng") || IsPackage(this, "EA_Trax.fng") || IsPackage(this, "OL_EAMessenger.fng"))
            return;
        *kEnteringChallenge = false;
        break;
    }
    reinterpret_cast<void(__thiscall *)(void *, unsigned int, FEObject *, unsigned int, unsigned int)>(
        (*reinterpret_cast<void ***>(this))[1])(this, message, o, a, b);
}

void MenuScreen::BaseNotifySound(unsigned int message, FEObject *, unsigned int, unsigned int)
{
    const int sound = reinterpret_cast<int(__thiscall *)(void *, unsigned int, int)>(
        (*reinterpret_cast<void ***>(this))[2])(this, message, SoundFor(message));
    if (sound != -1)
        (*kSound)->PlayUISoundFX(static_cast<eMenuSoundTriggers>(sound));
}
