/**
 * The game's PC input on SDL (docs/CONTROLS.md). The original read the keyboard, the mouse and the game controllers
 * through EA's RealSystem library, which drove DirectInput; here the GameDevice (the one InputDevice PC registers)
 * reads them from SDL itself, and neither RealSystem's devices nor DirectInput exist.
 *
 * What the rest of the original code still reads is kept in its place: the action table `GameAction` (76 actions of a
 * name and four bindings {type, device, index}: two keyboard bindings, then two controller ones), the device records
 * at 0x91f1d8 (keyboard), 0x91f1e0 (mouse) and 0x91f150 (controllers), of which the controls page and the default
 * bindings read the kind (+8) and the name (+0x114), and the mouse button states the front end's cursor reads.
 *
 * A game controller SDL knows as a gamepad reads as Windows' driver showed an Xbox 360 pad to DirectInput (left stick
 * X/Y, right stick Rx/Ry, the triggers on Z, ten buttons, the pad as a POV) and is named as that driver named it, so
 * the game's own Xbox 360 default bindings (0x63cd80) apply. Other controllers report their axes, hats and buttons in
 * DirectInput's order under their own names.
 */
#include <windows.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "gameplay/ai/aituning.h"
#include "core/config.h"
#include "core/input.h"
#include "legacy/link.h"
#include "core/log.h"
#include "core/sdl.h"

NFS_REPLACE(0x628410, InputSystemStart)
NFS_REPLACE(0x6282e0, InputReadControllerLayout)
NFS_REPLACE(0x628530, InputControllerHasPov)
NFS_REPLACE(0x628230, InputBindingName)
NFS_REPLACE(0x63f850, GameDeviceInitialize)
NFS_REPLACE(0x6349b0, GameDevicePoll)
NFS_REPLACE(0x6dc590, WheelEnumerate)
NFS_REPLACE(0x6e0ec0, WheelStart)

namespace
{
// ---- The original's data ---------------------------------------------------------------------------------------

/** GameAction: 76 actions of {name, then 4 bindings}; the defaults at 0x8f6d80. A keyboard binding is {1, key, 0},
 * a controller's {type, controller, index}. */
int *const kActions = reinterpret_cast<int *>(0x91f418);
const int *const kDefaultActions = reinterpret_cast<const int *>(0x8f6d80);
const int kActionCount = 76;
const int kActionWords = 13;
const int kBindings = 4;

/** Binding types (docs/CONTROLS.md). */
enum BindingType
{
    kNone,
    kKey,
    kButton,
    kPov,
    kAxisLow,
    kAxisHigh,
    kRotationLow,
    kRotationHigh,
    kSliderLow,
    kSliderHigh,
};

/** Device kinds of RealSystem's records (+8). */
enum Kind
{
    kKeyboardKind = 1,
    kMouseKind = 2,
    kControllerKind = 3,
};

/** What the original code reads of a RealSystem device record. */
struct DeviceRecord
{
    void *vtable;
    unsigned int unused_4;
    int kind;
    char unused_c[0x114 - 0xc];
    char name[0x104];
};
static_assert(offsetof(DeviceRecord, kind) == 8 && offsetof(DeviceRecord, name) == 0x114, "RealSystem device record");

DeviceRecord **const g_keyboardRecord = reinterpret_cast<DeviceRecord **>(0x91f1d8);
DeviceRecord **const g_mouseRecord = reinterpret_cast<DeviceRecord **>(0x91f1e0);
DeviceRecord **const g_controllerRecords = reinterpret_cast<DeviceRecord **>(0x91f150);
int *const g_controllerRecordCount = reinterpret_cast<int *>(0x9203b8);
const int kMaxControllerRecords = 8;
/** RealSystem's system object: only GameDevice's destructor (0x634d00) still calls it (+4, shut down). */
void **const g_system = reinterpret_cast<void **>(0x9203b4);
/** Set from the first controller's object names (0x6282e0); nothing reads it for SDL's controllers. */
int *const g_controllerLayoutFlag = reinterpret_cast<int *>(0x9203bc);

/** The mouse as the front end's cursor reads it (0x628cd0 .. 0x628d00): wheel, then down / pressed / released. */
int *const g_mouseWheel = reinterpret_cast<int *>(0x91f148);
unsigned char *const g_leftButton = reinterpret_cast<unsigned char *>(0x91f1d4);
unsigned char *const g_rightButton = reinterpret_cast<unsigned char *>(0x91f1dc);
unsigned char *const g_middleButton = reinterpret_cast<unsigned char *>(0x91f14c);

/** Nonzero while the game is the active application (WM_ACTIVATEAPP in the window procedure). */
const int *const g_gameActive = reinterpret_cast<const int *>(0x982bec);

/** GameDevice (docs/CONTROLS.md). */
struct GameDevice
{
    void *vtable;
    char unused_4[0x14];
    unsigned int *channels; // +0x18: {1, name hash, previous*, current*} each
    float *previous;        // +0x1c
    float *current;         // +0x20
    int port;               // +0x24
    char unused_28[0x1838 - 0x28];
    int channelCount;       // +0x1838
    bool capturing;         // +0x183c
    char unused_183d[3];
    int captureKind;        // +0x1840
    int captureSlot;        // +0x1844
    int captureAction;      // +0x1848
};
static_assert(offsetof(GameDevice, channels) == 0x18 && offsetof(GameDevice, channelCount) == 0x1838 &&
                  offsetof(GameDevice, captureAction) == 0x1848,
              "GameDevice layout");

using AssignBinding = void(__thiscall *)(GameDevice *, const void *event, const int *binding);
const AssignBinding kAssignBinding = NFS_ORIGINAL(0x6285f0, AssignBinding);
using ApplyDefaultBindings = void(__cdecl *)();
const ApplyDefaultBindings kApplyDefaultBindings = NFS_ORIGINAL(0x63cd80, ApplyDefaultBindings);
using KeyNameHash = unsigned int(__cdecl *)(const int *binding);
const KeyNameHash kKeyNameHash = NFS_ORIGINAL(0x627ce0, KeyNameHash);

/** The name Windows' driver gave an Xbox 360 pad, which the game's default bindings know. */
const char kXbox360Name[] = "XBOX 360 For Windows (Controller)";

// ---- The devices -----------------------------------------------------------------------------------------------

const int kAxes = 3;
const int kSliders = 2;
const int kPovs = 4;
const int kButtons = 128;
const int kGamepadButtons = 10;
const int kCentre = 32768;
/** An axis counts as pushed to its end for a binding past these (as the original's capture: 0x200 from either end). */
const int kLowEnd = 0x201;
const int kHighEnd = 0xfdfe;

/** A controller in DirectInput's terms: axes 0..65535, POVs in hundredths of a degree (-1 centred). */
struct ControllerState
{
    int axis[kAxes];
    int rotation[kAxes];
    int slider[kSliders];
    unsigned int pov[kPovs];
    bool button[kButtons];
    /** A gamepad's triggers on their own (0 to 32767): Z above holds them together, as Windows' driver did. */
    int leftTrigger, rightTrigger;
};

struct Controller
{
    SDL_JoystickID id;
    SDL_Joystick *joystick;
    SDL_Gamepad *gamepad;
    int buttons, hats, axes;
    ControllerState state, previous, rest;
    DeviceRecord record;
};

DeviceRecord g_keyboard, g_mouse;
Controller g_controllers[kMaxControllerRecords];
int g_controllerCount;
int g_rescanCountdown;
/** RealSystem's system object as GameDevice's destructor calls it: nothing to shut down. */
void __fastcall SystemNoOp(void *, void *)
{
}
void *const kSystemVtable[2] = {nullptr, reinterpret_cast<void *>(&SystemNoOp)};
const void *g_systemObject = kSystemVtable;

/** DirectInput's key codes (the PC keyboard's scan codes, 0x80 for E0 prefixed keys) as SDL's (USB's) scan codes. */
SDL_Scancode g_scancodeOfKey[256];
bool g_keyDown[256], g_keyWasDown[256];
/** Test channel presses (key, virtual pad button): polls left to hold each. */
int g_injectedKey[256];
SDL_JoystickID g_virtualPad;
SDL_Joystick *g_virtualPadJoystick;
int g_virtualButton = -1, g_virtualButtonPolls;
bool g_rescanNow;

void MapKeys()
{
    static const struct
    {
        unsigned char key;
        SDL_Scancode sdl;
    } kKeys[] = {
        {0x01, SDL_SCANCODE_ESCAPE},     {0x02, SDL_SCANCODE_1},           {0x03, SDL_SCANCODE_2},
        {0x04, SDL_SCANCODE_3},          {0x05, SDL_SCANCODE_4},           {0x06, SDL_SCANCODE_5},
        {0x07, SDL_SCANCODE_6},          {0x08, SDL_SCANCODE_7},           {0x09, SDL_SCANCODE_8},
        {0x0a, SDL_SCANCODE_9},          {0x0b, SDL_SCANCODE_0},           {0x0c, SDL_SCANCODE_MINUS},
        {0x0d, SDL_SCANCODE_EQUALS},     {0x0e, SDL_SCANCODE_BACKSPACE},   {0x0f, SDL_SCANCODE_TAB},
        {0x10, SDL_SCANCODE_Q},          {0x11, SDL_SCANCODE_W},           {0x12, SDL_SCANCODE_E},
        {0x13, SDL_SCANCODE_R},          {0x14, SDL_SCANCODE_T},           {0x15, SDL_SCANCODE_Y},
        {0x16, SDL_SCANCODE_U},          {0x17, SDL_SCANCODE_I},           {0x18, SDL_SCANCODE_O},
        {0x19, SDL_SCANCODE_P},          {0x1a, SDL_SCANCODE_LEFTBRACKET}, {0x1b, SDL_SCANCODE_RIGHTBRACKET},
        {0x1c, SDL_SCANCODE_RETURN},     {0x1d, SDL_SCANCODE_LCTRL},       {0x1e, SDL_SCANCODE_A},
        {0x1f, SDL_SCANCODE_S},          {0x20, SDL_SCANCODE_D},           {0x21, SDL_SCANCODE_F},
        {0x22, SDL_SCANCODE_G},          {0x23, SDL_SCANCODE_H},           {0x24, SDL_SCANCODE_J},
        {0x25, SDL_SCANCODE_K},          {0x26, SDL_SCANCODE_L},           {0x27, SDL_SCANCODE_SEMICOLON},
        {0x28, SDL_SCANCODE_APOSTROPHE}, {0x29, SDL_SCANCODE_GRAVE},       {0x2a, SDL_SCANCODE_LSHIFT},
        {0x2b, SDL_SCANCODE_BACKSLASH},  {0x2c, SDL_SCANCODE_Z},           {0x2d, SDL_SCANCODE_X},
        {0x2e, SDL_SCANCODE_C},          {0x2f, SDL_SCANCODE_V},           {0x30, SDL_SCANCODE_B},
        {0x31, SDL_SCANCODE_N},          {0x32, SDL_SCANCODE_M},           {0x33, SDL_SCANCODE_COMMA},
        {0x34, SDL_SCANCODE_PERIOD},     {0x35, SDL_SCANCODE_SLASH},       {0x36, SDL_SCANCODE_RSHIFT},
        {0x37, SDL_SCANCODE_KP_MULTIPLY}, {0x38, SDL_SCANCODE_LALT},       {0x39, SDL_SCANCODE_SPACE},
        {0x3a, SDL_SCANCODE_CAPSLOCK},   {0x3b, SDL_SCANCODE_F1},          {0x3c, SDL_SCANCODE_F2},
        {0x3d, SDL_SCANCODE_F3},         {0x3e, SDL_SCANCODE_F4},          {0x3f, SDL_SCANCODE_F5},
        {0x40, SDL_SCANCODE_F6},         {0x41, SDL_SCANCODE_F7},          {0x42, SDL_SCANCODE_F8},
        {0x43, SDL_SCANCODE_F9},         {0x44, SDL_SCANCODE_F10},         {0x45, SDL_SCANCODE_NUMLOCKCLEAR},
        {0x46, SDL_SCANCODE_SCROLLLOCK}, {0x47, SDL_SCANCODE_KP_7},        {0x48, SDL_SCANCODE_KP_8},
        {0x49, SDL_SCANCODE_KP_9},       {0x4a, SDL_SCANCODE_KP_MINUS},    {0x4b, SDL_SCANCODE_KP_4},
        {0x4c, SDL_SCANCODE_KP_5},       {0x4d, SDL_SCANCODE_KP_6},        {0x4e, SDL_SCANCODE_KP_PLUS},
        {0x4f, SDL_SCANCODE_KP_1},       {0x50, SDL_SCANCODE_KP_2},        {0x51, SDL_SCANCODE_KP_3},
        {0x52, SDL_SCANCODE_KP_0},       {0x53, SDL_SCANCODE_KP_PERIOD},   {0x56, SDL_SCANCODE_NONUSBACKSLASH},
        {0x57, SDL_SCANCODE_F11},        {0x58, SDL_SCANCODE_F12},         {0x64, SDL_SCANCODE_F13},
        {0x65, SDL_SCANCODE_F14},        {0x66, SDL_SCANCODE_F15},         {0x70, SDL_SCANCODE_INTERNATIONAL2},
        {0x73, SDL_SCANCODE_INTERNATIONAL1}, {0x79, SDL_SCANCODE_INTERNATIONAL4}, {0x7b, SDL_SCANCODE_INTERNATIONAL5},
        {0x7d, SDL_SCANCODE_INTERNATIONAL3}, {0x8d, SDL_SCANCODE_KP_EQUALS}, {0x9c, SDL_SCANCODE_KP_ENTER},
        {0x9d, SDL_SCANCODE_RCTRL},      {0xb3, SDL_SCANCODE_KP_COMMA},    {0xb5, SDL_SCANCODE_KP_DIVIDE},
        {0xb7, SDL_SCANCODE_PRINTSCREEN}, {0xb8, SDL_SCANCODE_RALT},       {0xc5, SDL_SCANCODE_PAUSE},
        {0xc7, SDL_SCANCODE_HOME},       {0xc8, SDL_SCANCODE_UP},          {0xc9, SDL_SCANCODE_PAGEUP},
        {0xcb, SDL_SCANCODE_LEFT},       {0xcd, SDL_SCANCODE_RIGHT},       {0xcf, SDL_SCANCODE_END},
        {0xd0, SDL_SCANCODE_DOWN},       {0xd1, SDL_SCANCODE_PAGEDOWN},    {0xd2, SDL_SCANCODE_INSERT},
        {0xd3, SDL_SCANCODE_DELETE},     {0xdb, SDL_SCANCODE_LGUI},        {0xdc, SDL_SCANCODE_RGUI},
        {0xdd, SDL_SCANCODE_APPLICATION},
    };
    for (SDL_Scancode &s : g_scancodeOfKey)
        s = SDL_SCANCODE_UNKNOWN;
    for (const auto &k : kKeys)
        g_scancodeOfKey[k.key] = k.sdl;
}

void ReadKeyboard()
{
    memcpy(g_keyWasDown, g_keyDown, sizeof(g_keyDown));
    int count = 0;
    const bool *state = SDL_GetKeyboardState(&count);
    for (int key = 0; key < 256; ++key)
    {
        const SDL_Scancode s = g_scancodeOfKey[key];
        g_keyDown[key] = state && s != SDL_SCANCODE_UNKNOWN && s < count && state[s];
        if (g_injectedKey[key] > 0)
        {
            g_keyDown[key] = true;
            --g_injectedKey[key];
        }
    }
    if (g_virtualButton >= 0 && --g_virtualButtonPolls <= 0)
    {
        SDL_SetJoystickVirtualButton(g_virtualPadJoystick, g_virtualButton, false);
        g_virtualButton = -1;
    }
}

void SetName(DeviceRecord &record, int kind, const char *name)
{
    memset(&record, 0, sizeof(record));
    record.kind = kind;
    lstrcpynA(record.name, name, sizeof(record.name));
}

void Centre(ControllerState &s)
{
    memset(&s, 0, sizeof(s));
    for (int a = 0; a < kAxes; ++a)
        s.axis[a] = s.rotation[a] = kCentre;
    for (int a = 0; a < kSliders; ++a)
        s.slider[a] = kCentre;
    for (unsigned int &p : s.pov)
        p = 0xFFFFFFFFu;
}

int Unsigned(Sint16 value)
{
    return static_cast<int>(value) + kCentre;
}

unsigned int PovOf(bool up, bool right, bool down, bool left)
{
    const int x = static_cast<int>(right) - static_cast<int>(left), y = static_cast<int>(up) - static_cast<int>(down);
    if (!x && !y)
        return 0xFFFFFFFFu;
    static const unsigned int kAngles[3][3] = {{22500, 18000, 13500}, {27000, 0, 9000}, {31500, 0, 4500}};
    return kAngles[y + 1][x + 1];
}

/** A gamepad's buttons in the order Windows' driver numbered an Xbox 360 pad's. */
const SDL_GamepadButton kOrder[kGamepadButtons] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_BACK,  SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK};

void Read(Controller &c)
{
    ControllerState &s = c.state;
    if (c.gamepad)
    {
        SDL_Gamepad *g = c.gamepad;
        s.axis[0] = Unsigned(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX));
        s.axis[1] = Unsigned(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY));
        // Both triggers on Z, as Windows' driver had them: the left one towards the high end, the right one the low.
        s.leftTrigger = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        s.rightTrigger = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        const int z = kCentre + s.leftTrigger - s.rightTrigger;
        s.axis[2] = z < 0 ? 0 : z > 65535 ? 65535 : z;
        s.rotation[0] = Unsigned(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX));
        s.rotation[1] = Unsigned(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY));
        for (int b = 0; b < kGamepadButtons; ++b)
            s.button[b] = SDL_GetGamepadButton(g, kOrder[b]);
        s.pov[0] = PovOf(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP),
                         SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
                         SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN),
                         SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        return;
    }
    // DirectInput's order of a joystick's axes: X, Y, Z, Rx, Ry, Rz, then the sliders.
    for (int a = 0; a < c.axes; ++a)
    {
        const int v = Unsigned(SDL_GetJoystickAxis(c.joystick, a));
        if (a < kAxes)
            s.axis[a] = v;
        else if (a < 2 * kAxes)
            s.rotation[a - kAxes] = v;
        else
            s.slider[a - 2 * kAxes] = v;
    }
    for (int b = 0; b < c.buttons; ++b)
        s.button[b] = SDL_GetJoystickButton(c.joystick, b);
    for (int p = 0; p < c.hats; ++p)
    {
        const Uint8 hat = SDL_GetJoystickHat(c.joystick, p);
        s.pov[p] = PovOf(hat & SDL_HAT_UP, hat & SDL_HAT_RIGHT, hat & SDL_HAT_DOWN, hat & SDL_HAT_LEFT);
    }
}

} // namespace

void nfs::input::Rumble(float low, float high, unsigned int ms)
{
    static float lastLow = -1.0f, lastHigh = -1.0f;
    if (!g_controllerCount)
        return;
    Controller &c = g_controllers[0];
    const Uint16 l = static_cast<Uint16>((low < 0.0f ? 0.0f : low > 1.0f ? 1.0f : low) * 65535.0f);
    const Uint16 h = static_cast<Uint16>((high < 0.0f ? 0.0f : high > 1.0f ? 1.0f : high) * 65535.0f);
    // Unchanged strengths go out again only before they run out (the call is a device write).
    static DWORD sentAt;
    const DWORD now = GetTickCount();
    if (l == static_cast<Uint16>(lastLow) && h == static_cast<Uint16>(lastHigh) && now - sentAt < ms / 2)
        return;
    lastLow = l;
    lastHigh = h;
    sentAt = now;
    if (c.gamepad)
        SDL_RumbleGamepad(c.gamepad, l, h, ms);
    else if (c.joystick)
        SDL_RumbleJoystick(c.joystick, l, h, ms);
}

namespace
{
void Close(Controller &c)
{
    if (c.gamepad)
        SDL_CloseGamepad(c.gamepad);
    else if (c.joystick)
        SDL_CloseJoystick(c.joystick);
    c.gamepad = nullptr;
    c.joystick = nullptr;
}

bool Open(Controller &c, SDL_JoystickID id)
{
    memset(&c, 0, sizeof(c));
    c.id = id;
    if (SDL_IsGamepad(id))
        c.gamepad = SDL_OpenGamepad(id);
    c.joystick = c.gamepad ? SDL_GetGamepadJoystick(c.gamepad) : SDL_OpenJoystick(id);
    if (!c.joystick)
        return false;
    if (c.gamepad)
    {
        c.buttons = kGamepadButtons;
        c.hats = 1;
    }
    else
    {
        const int axes = SDL_GetNumJoystickAxes(c.joystick), hats = SDL_GetNumJoystickHats(c.joystick),
                  buttons = SDL_GetNumJoystickButtons(c.joystick);
        c.axes = axes < 2 * kAxes + kSliders ? axes : 2 * kAxes + kSliders;
        c.hats = hats < kPovs ? hats : kPovs;
        c.buttons = buttons < kButtons ? buttons : kButtons;
    }
    const char *name = SDL_GetJoystickName(c.joystick);
    SetName(c.record, kControllerKind, c.gamepad ? kXbox360Name : name && *name ? name : "Game Controller");
    Centre(c.state);
    Read(c);
    c.previous = c.rest = c.state;
    const bool rumble = SDL_GetBooleanProperty(SDL_GetJoystickProperties(c.joystick),
                                               SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
    nfs::Log("input: %s (%04x:%04x) as %s; rumble %s", name ? name : "?", SDL_GetJoystickVendor(c.joystick),
             SDL_GetJoystickProduct(c.joystick), c.gamepad ? "an Xbox 360 pad" : "a joystick",
             rumble ? "yes" : "no (for an Xbox pad try [Controls] XboxRumble=1)");
    if (c.gamepad)
        nfs::Log("input: gamepad type %d, face labels south %d east %d west %d north %d", SDL_GetGamepadType(c.gamepad),
                 SDL_GetGamepadButtonLabel(c.gamepad, SDL_GAMEPAD_BUTTON_SOUTH),
                 SDL_GetGamepadButtonLabel(c.gamepad, SDL_GAMEPAD_BUTTON_EAST),
                 SDL_GetGamepadButtonLabel(c.gamepad, SDL_GAMEPAD_BUTTON_WEST),
                 SDL_GetGamepadButtonLabel(c.gamepad, SDL_GAMEPAD_BUTTON_NORTH));
    return true;
}

void PadMenuDefaults();

/** The controllers SDL lists, in its order, into the original's records (the bindings name the first one, 0). */
void ScanControllers()
{
    for (int i = 0; i < g_controllerCount; ++i)
        Close(g_controllers[i]);
    g_controllerCount = 0;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    // The test channel's virtual pad first, so it is the one the bindings name.
    for (int i = 1; ids && g_virtualPad && i < count; ++i)
        if (ids[i] == g_virtualPad)
        {
            ids[i] = ids[0];
            ids[0] = g_virtualPad;
        }
    for (int i = 0; ids && i < count && g_controllerCount < kMaxControllerRecords; ++i)
        if (Open(g_controllers[g_controllerCount], ids[i]))
            ++g_controllerCount;
    SDL_free(ids);
    for (int i = 0; i < kMaxControllerRecords; ++i)
        g_controllerRecords[i] = i < g_controllerCount ? &g_controllers[i].record : nullptr;
    *g_controllerRecordCount = g_controllerCount;
    // A pad plugged in later gets the front end's gamepad layout too.
    PadMenuDefaults();
}

/** Set while a controller, not the keyboard or the mouse, was the last thing used (the front end's prompts). */
bool g_padInUse;

/** A stick or trigger counts as used past half its travel. */
bool Pushed(int value)
{
    return value < kCentre - kCentre / 2 || value > kCentre + kCentre / 2;
}

/** The device last used: a key going down, or a controller's button, pad, stick or trigger. */
void NoteDevice()
{
    for (int key = 0; key < 256; ++key)
        if (g_keyDown[key] && !g_keyWasDown[key])
        {
            g_padInUse = false;
            return;
        }
    for (int i = 0; i < g_controllerCount; ++i)
    {
        const ControllerState &s = g_controllers[i].state, &was = g_controllers[i].previous;
        bool used = s.pov[0] != 0xFFFFFFFFu && s.pov[0] != was.pov[0];
        for (int b = 0; b < kGamepadButtons && !used; ++b)
            used = s.button[b] && !was.button[b];
        for (int a = 0; a < kAxes && !used; ++a)
            used = (Pushed(s.axis[a]) && !Pushed(was.axis[a])) || (Pushed(s.rotation[a]) && !Pushed(was.rotation[a]));
        if (used)
        {
            g_padInUse = true;
            return;
        }
    }
}

/** Everything read once a poll; a controller unplugged is dropped, and the list is looked at again now and then. */
void Update()
{
    ReadKeyboard();
    SDL_UpdateJoysticks();
    bool lost = false;
    for (int i = 0; i < g_controllerCount; ++i)
    {
        Controller &c = g_controllers[i];
        c.previous = c.state;
        if (SDL_JoystickConnected(c.joystick))
            Read(c);
        else
            lost = true;
    }
    if (lost || g_rescanNow || (--g_rescanCountdown <= 0 && !g_controllerCount))
    {
        g_rescanCountdown = 120;
        g_rescanNow = false;
        if (lost)
            nfs::Log("input: a game controller was unplugged");
        ScanControllers();
    }
    NoteDevice();
}

/** [Controls] shaping of the sticks' and triggers' travel (percent; the curve as a power, 1 straight). */
struct Shaping
{
    float deadZone, outerZone, curve, triggerDeadZone;
};

bool g_shapingRead;

const Shaping &Shape()
{
    static Shaping s;
    bool &read = g_shapingRead;
    if (!read)
    {
        read = true;
        const auto percent = [](const char *key, int fallback, int most) {
            const int v = nfs::config::Int("Controls", key, fallback);
            return (v < 0 ? 0 : v > most ? most : v) / 100.0f;
        };
        s.deadZone = percent("DeadZone", 0, 50);
        s.outerZone = percent("OuterDeadZone", 0, 40);
        s.triggerDeadZone = percent("TriggerDeadZone", 0, 50);
        s.curve = percent("SteeringCurve", 100, 300);
        if (s.curve < 0.25f)
            s.curve = 0.25f;
    }
    return s;
}

/** A travel 0 to 1 past the dead zone and short of the outer one, bent by the curve. */
float Shaped(float v, float dead, float outer, float curve)
{
    if (dead <= 0.0f && outer <= 0.0f && curve == 1.0f)
        return v;
    float t = (v - dead) / (1.0f - dead - outer);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return curve == 1.0f ? t : powf(t, curve);
}

/** A binding's value, 0 to 1 (the original's 0x628940). A gamepad's Z (the two triggers together, as the bindings
 * know them) reads each trigger on its own: gas and brake no longer cancel out when both are pressed. */
float BindingValue(const int *binding)
{
    const int type = binding[0];
    if (type == kKey)
        return binding[1] >= 0 && binding[1] < 256 && g_keyDown[binding[1]] ? 1.0f : 0.0f;
    const int device = binding[1], index = binding[2];
    if (device < 0 || device >= g_controllerCount)
        return 0.0f;
    const ControllerState &s = g_controllers[device].state;
    const ControllerState &rest = g_controllers[device].rest;
    int raw, rested;
    switch (type)
    {
    case kButton:
        return index >= 0 && index < kButtons && s.button[index] ? 1.0f : 0.0f;
    case kPov:
    {
        // A direction counts with its diagonals; the original read the first POV only.
        const unsigned int p = s.pov[0];
        if (p == 0xFFFFFFFFu || index < 0 || index > 3)
            return 0.0f;
        const unsigned int centre = index * 9000u, left = (centre + 31500u) % 36000u, right = (centre + 4500u) % 36000u;
        return p == centre || p == left || p == right ? 1.0f : 0.0f;
    }
    case kAxisLow:
    case kAxisHigh:
        if (index == 2 && g_controllers[device].gamepad)
        {
            // Towards Z's low end was the right trigger, towards its high end the left one.
            const int pressed = type == kAxisLow ? s.rightTrigger : s.leftTrigger;
            const Shaping &shape = Shape();
            return Shaped(pressed * (1.0f / 32767.0f), shape.triggerDeadZone, 0.0f, 1.0f);
        }
        raw = s.axis[index > 2 ? 2 : index < 0 ? 0 : index];
        rested = rest.axis[index > 2 ? 2 : index < 0 ? 0 : index];
        break;
    case kRotationLow:
    case kRotationHigh:
        raw = s.rotation[index > 2 ? 2 : index < 0 ? 0 : index];
        rested = rest.rotation[index > 2 ? 2 : index < 0 ? 0 : index];
        break;
    case kSliderLow:
    case kSliderHigh:
        raw = s.slider[index > 1 ? 1 : index < 0 ? 0 : index];
        rested = rest.slider[index > 1 ? 1 : index < 0 ? 0 : index];
        break;
    default:
        return 0.0f;
    }
    // An axis that rests away from its centre (a pedal, or a device that only says it has axes, as some keyboards'
    // HID interfaces do) counts nothing while it stays where it rests: otherwise it would hold its direction down for
    // good and the same action's keys would never change it.
    if (raw == rested && Pushed(rested))
        return 0.0f;
    // Towards the axis's low end (the even types) or its high end, 0 at the centre.
    const float sign = (type == kAxisLow || type == kRotationLow || type == kSliderLow) ? 1.0f : -1.0f;
    const float v = (32767.5f - static_cast<float>(raw)) * sign;
    if (v <= 0.0f)
        return 0.0f;
    const Shaping &shape = Shape();
    return Shaped(v * (1.0f / 32767.5f), shape.deadZone, shape.outerZone, shape.curve);
}

/** Hands a binding to the original's assignment (0x6285f0), which reads the device's kind through an event. */
void Assign(GameDevice *device, const DeviceRecord &record, const int (&binding)[3])
{
    const struct
    {
        int unused;
        const DeviceRecord *record;
    } event = {0, &record};
    kAssignBinding(device, &event, binding);
}

/** An axis newly at one of its ends, away from where it rests: its binding type (low or high), else 0. */
int AxisPushed(int now, int before, int rest, int low)
{
    if (now < kLowEnd && before >= kLowEnd && now != rest)
        return low;
    if (now > kHighEnd && before <= kHighEnd && now != rest)
        return low + 1;
    return 0;
}

/** The controls page's "press a key" (the original's 0x634900 and 0x634490): the first key or control used since
 * the last poll becomes the binding; Escape ends it without one. */
void Capture(GameDevice *device)
{
    Update();
    for (int key = 1; key < 256; ++key)
        if (g_keyDown[key] && !g_keyWasDown[key])
        {
            if (key == 1)
            {
                device->capturing = false;
                return;
            }
            if (device->captureKind == kKeyboardKind)
            {
                const int binding[3] = {kKey, key, 0};
                Assign(device, g_keyboard, binding);
                device->capturing = false;
                return;
            }
        }
    if (device->captureKind != kControllerKind || !g_controllerCount)
        return;
    const Controller &c = g_controllers[0];
    const ControllerState &s = c.state, &p = c.previous, &r = c.rest;
    int type = 0, index = 0;
    for (int b = 0; b < kButtons && !type; ++b)
        if (s.button[b] && !p.button[b])
            type = kButton, index = b;
    for (int a = 0; a < kAxes && !type; ++a)
        if ((type = AxisPushed(s.axis[a], p.axis[a], r.axis[a], kAxisLow)) != 0)
            index = a;
    for (int a = 0; a < kAxes && !type; ++a)
        if ((type = AxisPushed(s.rotation[a], p.rotation[a], r.rotation[a], kRotationLow)) != 0)
            index = a;
    for (int a = 0; a < kSliders && !type; ++a)
        if ((type = AxisPushed(s.slider[a], p.slider[a], r.slider[a], kSliderLow)) != 0)
            index = a;
    if (!type && s.pov[0] != p.pov[0] && s.pov[0] % 9000u == 0)
        type = kPov, index = static_cast<int>(s.pov[0] / 9000u);
    if (!type)
        return;
    const int binding[3] = {type, 0, index};
    Assign(device, c.record, binding);
    device->capturing = false;
}

// ---- Names for the controls page --------------------------------------------------------------------------------

void KeyName(char *out, int size, const int *binding, bool characters)
{
    const int key = binding[1];
    if (characters)
    {
        // As the original did for French and German: the character the key types, else the game's own key names.
        BYTE state[256] = {};
        WCHAR text[16] = {};
        const UINT vk = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VSC_TO_VK_EX);
        const int n = ToUnicode(vk, 0, state, text, 16, 0);
        if (n > 0 && text[0] != L' ' && text[0] != L'\b' && text[0] != L'\t' && text[0] != L'\r')
        {
            WideStringToPackedString(out, size, reinterpret_cast<const unsigned short *>(text));
            return;
        }
        if (const unsigned int hash = kKeyNameHash(binding))
        {
            GetLocalizedString(out, size, hash);
            return;
        }
    }
    // Windows' name for the key on this keyboard layout, as DirectInput gave it.
    const LONG param = ((key & 0x7f) << 16) | ((key & 0x80) ? (1 << 24) : 0);
    if (key == 0xc5)
        lstrcpynA(out, "Pause", size);
    else if (!GetKeyNameTextA(param, out, size))
        wsprintfA(out, "Key %d", key);
    if (!lstrcmpA(out, "LEER"))
        lstrcpynA(out, "LEERTASTE", size);
}

void ControlName(char *out, int size, const int *binding)
{
    const int type = binding[0], device = binding[1], index = binding[2];
    const bool pad = device >= 0 && device < g_controllerCount && g_controllers[device].gamepad;
    static const char *const kPadButtons[kGamepadButtons] = {"A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS"};
    static const char *const kDirections[4] = {"Up", "Right", "Down", "Left"};
    static const char *const kAxisNames[3] = {"X Axis", "Y Axis", "Z Axis"};
    static const char *const kRotationNames[3] = {"X Rotation", "Y Rotation", "Z Rotation"};
    const bool low = type == kAxisLow || type == kRotationLow || type == kSliderLow;
    const int clamped = index < 0 ? 0 : index > 2 ? 2 : index;
    switch (type)
    {
    case kButton:
        if (pad && index >= 0 && index < kGamepadButtons)
            lstrcpynA(out, kPadButtons[index], size);
        else
            wsprintfA(out, "Button %d", index + 1);
        return;
    case kPov:
        wsprintfA(out, "%s %s", pad ? "D-Pad" : "POV", kDirections[index & 3]);
        return;
    case kAxisLow:
    case kAxisHigh:
        if (pad && clamped == 0)
            wsprintfA(out, "Left Stick %s", low ? "Left" : "Right");
        else if (pad && clamped == 1)
            wsprintfA(out, "Left Stick %s", low ? "Up" : "Down");
        else if (pad)
            lstrcpynA(out, low ? "RT" : "LT", size);
        else
            wsprintfA(out, "%s %c", kAxisNames[clamped], low ? '-' : '+');
        return;
    case kRotationLow:
    case kRotationHigh:
        if (pad && clamped == 0)
            wsprintfA(out, "Right Stick %s", low ? "Left" : "Right");
        else if (pad && clamped == 1)
            wsprintfA(out, "Right Stick %s", low ? "Up" : "Down");
        else
            wsprintfA(out, "%s %c", kRotationNames[clamped], low ? '-' : '+');
        return;
    case kSliderLow:
    case kSliderHigh:
        wsprintfA(out, "Slider %d %c", (index & 1) + 1, low ? '-' : '+');
        return;
    default:
        lstrcpynA(out, "---", size);
    }
}

/** The mouse wheel since the last poll, in DirectInput's units (120 a notch). */
int g_wheel;
} // namespace

namespace nfs::input
{
void OnMouseWheel(float notches)
{
    g_wheel += static_cast<int>(notches * WHEEL_DELTA);
}

void ReloadSettings()
{
    g_shapingRead = false;
}

int TakeMouseWheel()
{
    const int wheel = g_wheel;
    g_wheel = 0;
    return wheel;
}

bool PadInUse()
{
    // -set Debug.PadPrompts=1 shows the pad's buttons whatever was used last (tests).
    static const bool forced = nfs::config::Int("Debug", "PadPrompts", 0) != 0;
    return (g_padInUse || forced) && g_controllerCount && g_controllers[0].gamepad;
}

const char *ActionPicture(int action)
{
    if (!g_controllerCount || !g_controllers[0].gamepad || action < 0 || action >= kActionCount)
        return nullptr;
    SDL_Gamepad *pad = g_controllers[0].gamepad;
    const SDL_GamepadType type = SDL_GetGamepadType(pad);
    const bool playstation = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    const int *w = kActions + action * kActionWords;
    for (int slot = 2; slot < kBindings; ++slot)
    {
        const int *b = w + 1 + slot * 3;
        if (b[0] == kNone || b[1] != 0)
            continue;
        if (b[0] == kButton && b[2] >= 0 && b[2] < 4)
            switch (SDL_GetGamepadButtonLabel(pad, kOrder[b[2]]))
            {
            case SDL_GAMEPAD_BUTTON_LABEL_A: return "PAD_XB_A";
            case SDL_GAMEPAD_BUTTON_LABEL_B: return "PAD_XB_B";
            case SDL_GAMEPAD_BUTTON_LABEL_X: return "PAD_XB_X";
            case SDL_GAMEPAD_BUTTON_LABEL_Y: return "PAD_XB_Y";
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "PAD_PS_CROSS";
            case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "PAD_PS_CIRCLE";
            case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "PAD_PS_SQUARE";
            case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "PAD_PS_TRIANGLE";
            default: return nullptr;
            }
        if (b[0] == kButton && b[2] >= 4 && b[2] < kGamepadButtons)
        {
            static const char *const kXbox[] = {"PAD_XB_LB", "PAD_XB_RB", "PAD_XB_VIEW", "PAD_XB_MENU", "PAD_XB_LS",
                                                "PAD_XB_RS"};
            static const char *const kPlayStation[] = {"PAD_PS_L1",    "PAD_PS_R1",      "PAD_PS_SHARE",
                                                       "PAD_PS_OPTIONS", "PAD_PS_L3",    "PAD_PS_R3"};
            return (playstation ? kPlayStation : kXbox)[b[2] - 4];
        }
        if ((b[0] == kAxisLow || b[0] == kAxisHigh) && b[2] == 2)
            return b[0] == kAxisLow ? (playstation ? "PAD_PS_R2" : "PAD_XB_RT")
                                    : (playstation ? "PAD_PS_L2" : "PAD_XB_LT");
        return nullptr;
    }
    return nullptr;
}

const char *PromptPicture(int key)
{
    for (int a = 0; a < kActionCount; ++a)
    {
        const int *w = kActions + a * kActionWords;
        const char *name = reinterpret_cast<const char *>(w[0]);
        if (name && !strncmp(name, "FE_ACTION_", 10) && ((w[1] == kKey && w[2] == key) || (w[4] == kKey && w[5] == key)))
            return ActionPicture(a);
    }
    return nullptr;
}

bool ActionHeld(int action)
{
    if (action < 0 || action >= kActionCount)
        return false;
    const int *binding = kActions + action * kActionWords + 1;
    for (int b = 0; b < kBindings; ++b, binding += 3)
        if (binding[0] != kNone && BindingValue(binding) > 0.5f)
            return true;
    return false;
}
} // namespace nfs::input

// ---- The replacements -------------------------------------------------------------------------------------------

/** 0x628410: the action table from its defaults, and the devices (RealSystem's system and its device callbacks). */
namespace nfs::rumble
{
void Install();
}

extern "C" void InputSystemStart()
{
    memcpy(kActions, kDefaultActions, kActionCount * kActionWords * sizeof(int));
    MapKeys();
    SetName(g_keyboard, kKeyboardKind, "Keyboard");
    SetName(g_mouse, kMouseKind, "Mouse");
    *g_keyboardRecord = &g_keyboard;
    *g_mouseRecord = &g_mouse;
    *g_system = &g_systemObject;
    for (int i = 0; i < kMaxControllerRecords; ++i)
        g_controllerRecords[i] = nullptr;
    *g_controllerRecordCount = 0;
    if (nfs::sdl::Start(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
    {
        SDL_UpdateJoysticks();
        ScanControllers();
    }
    nfs::rumble::Install();
    nfs::aituning::Apply();
}

/** 0x6282e0: a flag from the first controller's Z axis name, for DirectInput drivers SDL does not have. */
extern "C" void InputReadControllerLayout()
{
    *g_controllerLayoutFlag = 0;
}

/** 0x628530: whether the first controller has a POV (the default bindings choose a layout by it). */
extern "C" bool InputControllerHasPov()
{
    return g_controllerCount && g_controllers[0].hats > 0;
}

/** 0x628230: a binding's name for the controls page: `primary` the first of the device's two bindings. */
extern "C" void InputBindingName(char *out, int kind, int action, char primary)
{
    const int slot = kind == kKeyboardKind ? (primary ? 0 : 1) : (primary ? 2 : 3);
    const int *binding = kActions + action * kActionWords + 1 + slot * 3;
    if (binding[0] == kNone)
    {
        lstrcpynA(out, "---", MAX_PATH);
        return;
    }
    if (kind == kKeyboardKind)
    {
        const int language = static_cast<int>(GetCurrentLanguage());
        KeyName(out, MAX_PATH, binding, language == 5 || language == 0xc);
    }
    else
        ControlName(out, MAX_PATH, binding);
}

namespace
{
/**
 * The front end's actions on a gamepad. The game's defaults for them are the same for every controller and were made
 * for a pad numbered as DirectInput numbered most (Logitech's: 0 the left face button, 1 the bottom one, 2 the right,
 * 9 Start); on an Xbox 360 pad that made B accept, X go back and the right stick Start, and left 1, 2 and 4 of the
 * footers with no button. A gamepad gets the layout its face shows (docs/CONTROLS.md "Front-end buttons"); the
 * shoulders (previous and next) and B2 keep the game's.
 */
void PadMenuDefaults()
{
    if (!g_controllerCount || !g_controllers[0].gamepad)
        return;
    static const struct
    {
        int action, type, index;
    } kMenu[] = {
        {32, kButton, 0},   // FE_ACTION_ACCEPT (Enter): A
        {33, kButton, 1},   // FE_ACTION_CANCEL (Esc): B
        {34, kButton, 7},   // FE_ACTION_START (Space): Start
        {41, kButton, 2},   // FE_ACTION_B5 (1): X, the game's button 0 (the left face button)
        {37, kButton, 3},   // FE_ACTION_B0 (3): Y
        {40, kButton, 6},   // FE_ACTION_B4 (2): Back
        {39, kButton, 9},   // FE_ACTION_B3 (4): the right stick
        {29, kAxisLow, 2},  // FE_ACTION_RTRIGGER (F1): the right trigger
        {30, kAxisHigh, 2}, // FE_ACTION_LTRIGGER (F2): the left trigger
    };
    for (const auto &m : kMenu)
    {
        int *binding = kActions + m.action * kActionWords + 1 + 2 * 3;
        binding[0] = m.type;
        binding[1] = 0;
        binding[2] = m.index;
    }
}
} // namespace

/** 0x63f850 (GameDevice +0xc): its channels, one per action, and the default bindings for the first controller. */
void __fastcall GameDeviceInitialize(void *self, void *)
{
    GameDevice *device = static_cast<GameDevice *>(self);
    if (device->port)
        return;
    *g_leftButton = g_leftButton[1] = g_leftButton[2] = 0;
    *g_rightButton = g_rightButton[1] = g_rightButton[2] = 0;
    *g_middleButton = g_middleButton[1] = g_middleButton[2] = 0;
    *g_mouseWheel = 0;
    device->channelCount = 0;
    for (int i = 0; i < kActionCount; ++i)
    {
        unsigned int *channel = device->channels + i * 4;
        channel[0] = 1;
        channel[1] = stringhash32(reinterpret_cast<const char *>(kActions[i * kActionWords]));
        channel[2] = reinterpret_cast<unsigned int>(device->previous + i);
        channel[3] = reinterpret_cast<unsigned int>(device->current + i);
        ++device->channelCount;
    }
    if (g_controllerCount)
    {
        Controller &c = g_controllers[0];
        c.rest = c.state;
        // The original's one wheel with pedals that rest at the far end (Logitech Driving Force).
        if (bStringHashUpper(c.record.name) == 0x5fb6ccba)
            c.rest.axis[1] = c.rest.rotation[2] = 0xffff;
    }
    kApplyDefaultBindings();
    PadMenuDefaults();
    if (nfs::config::Int("Debug", "ActionLog", 0))
        for (int a = 0; a < kActionCount; ++a)
        {
            const int *w = kActions + a * kActionWords;
            nfs::Log("action %d %s: {%d %d %d} {%d %d %d} {%d %d %d} {%d %d %d}", a, reinterpret_cast<const char *>(w[0]),
                     w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12]);
        }
    // The first action (GAME_ACTION_GAS): its keyboard and controller bindings, to see which defaults applied.
    const int *gas = kActions + 1;
    nfs::Log("input: %d game controller(s); gas on key %d and controller {%d %d %d}", g_controllerCount, gas[1], gas[6],
             gas[7], gas[8]);
}

/** 0x6349b0 (GameDevice +0x10): each action's value, the largest of its bindings', and the mouse for the cursor. */
void __fastcall GameDevicePoll(void *self, void *)
{
    GameDevice *device = static_cast<GameDevice *>(self);
    static const bool keyLog = nfs::config::Int("Debug", "KeyLog", 0) != 0;
    static int lastActive = -1;
    if (keyLog && !device->port && *g_gameActive != lastActive)
    {
        lastActive = *g_gameActive;
        nfs::Log("input: game active %d", lastActive);
    }
    static const bool always = nfs::config::Int("Debug", "InputAlways", 0) != 0;
    if (device->port || (!*g_gameActive && !always))
        return;
    if (device->capturing)
    {
        Capture(device);
        return;
    }
    // The previous values are the whole channel array's (256), as the original copied them.
    MEM_copy(device->previous, device->current, 0x400);
    Update();
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    *g_mouseWheel = nfs::input::TakeMouseWheel();
    const bool down[3] = {(buttons & SDL_BUTTON_LMASK) != 0, (buttons & SDL_BUTTON_RMASK) != 0,
                          (buttons & SDL_BUTTON_MMASK) != 0};
    unsigned char *const states[3] = {g_leftButton, g_rightButton, g_middleButton};
    for (int b = 0; b < 3; ++b)
    {
        unsigned char *s = states[b];
        s[1] = !s[0] && down[b];
        s[2] = s[0] && !down[b];
        s[0] = down[b];
        if (s[1])
            g_padInUse = false;
    }
    for (int a = 0; a < kActionCount; ++a)
    {
        const int *binding = kActions + a * kActionWords + 1;
        float value = 0.0f;
        for (int b = 0; b < kBindings; ++b, binding += 3)
            if (binding[0] != kNone)
            {
                const float v = BindingValue(binding);
                if (v > value)
                    value = v;
            }
        device->current[a] = value;
        if (keyLog && value != device->previous[a])
            nfs::Log("input: action %d %s %d%%", a, reinterpret_cast<const char *>(kActions[a * kActionWords]),
                     static_cast<int>(value * 100.0f));
    }
    if (keyLog)
        for (int k = 0; k < 256; ++k)
            if (g_keyDown[k] != g_keyWasDown[k])
                nfs::Log("input: key %d %s", k, g_keyDown[k] ? "down" : "up");
}

/** 0x6dc590 (the wheel manager): the force feedback controllers. SDL gives none on Windows without DirectInput, so
 * none are found; the settings it copies are kept. */
int __fastcall WheelEnumerate(char *manager, void *)
{
    int *fields = reinterpret_cast<int *>(manager + 0x2220);
    fields[0] = *reinterpret_cast<const int *>(0x982904);
    fields[1] = *reinterpret_cast<const int *>(0x982908);
    fields[2] = *reinterpret_cast<const int *>(0x93cc58);
    fields[3] = *reinterpret_cast<const int *>(0x93cc5c);
    fields[4] = *reinterpret_cast<const int *>(0x93d904);
    fields[5] = *reinterpret_cast<const int *>(0x93d908);
    return 0;
}

/** 0x6e0ec0 (the wheel manager's start): it made its own DirectInput object to look for force feedback. */
void __fastcall WheelStart(char *manager, void *)
{
    WheelEnumerate(manager, nullptr);
}

void nfs::input::InjectKey(int key, int polls)
{
    if (key >= 0 && key < 256)
        g_injectedKey[key] = polls;
}

bool nfs::input::AttachVirtualPad()
{
    if (g_virtualPad)
        return true;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Test Pad";
    g_virtualPad = SDL_AttachVirtualJoystick(&desc);
    if (!g_virtualPad)
    {
        nfs::Log("input: no virtual pad (%s)", SDL_GetError());
        return false;
    }
    g_virtualPadJoystick = SDL_OpenJoystick(g_virtualPad);
    nfs::Log("input: virtual test pad attached (%u)", static_cast<unsigned int>(g_virtualPad));
    g_rescanNow = true;
    return g_virtualPadJoystick != nullptr;
}

bool nfs::input::PressVirtualPad(int button, int polls)
{
    if (!g_virtualPadJoystick || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
        return false;
    if (g_virtualButton >= 0)
        SDL_SetJoystickVirtualButton(g_virtualPadJoystick, g_virtualButton, false);
    SDL_SetJoystickVirtualButton(g_virtualPadJoystick, button, true);
    g_virtualButton = button;
    g_virtualButtonPolls = polls;
    return true;
}
