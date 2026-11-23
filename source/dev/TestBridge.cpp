/**
 * The test channel (dev/testbridge.h, docs/TESTING.md). A thread of its own serves the pipe \\.\pipe\nfsmw_test_<pid>:
 * a request is a line of text, the answer a line of JSON. The game's thread answers it in Main_DisplayFrame, so the
 * front end is read between its frames; a request the game does not take within kAnswerMs (a load that holds the
 * frame) is answered {"ok": false, "busy": true}.
 *
 *   state                the game flow's state, the front-end packages (name, whether it has the pads, priority), the
 *                        one with the pads, and its screen's kind
 *   screen [PACKAGE]     every shown text of a package (the one with the pads by default): text, the text of its
 *                        language label (what a string set by label shows), position, colour, alpha, object name
 *                        hash; and the object the pads are on (its text)
 *   menu [PACKAGE]       an icon-scroller screen's options (their names) and the chosen one; for other screens, the
 *                        object the pads are on
 *   press BUTTON         a front-end button, as a pad press (up, down, left, right, accept, back, start, button0-5,
 *                        ltrigger, rtrigger); pause opens the pause menu in a race, as the pause key does
 *   key NAME             a keyboard key through the game's own input (up, down, left, right, enter, escape, space,
 *                        1, 2, backspace), held 6 polls; needs -set Debug.InputAlways=1 when the window is not active
 *   vpad                 a virtual SDL gamepad attached, read as a real controller
 *   pad NAME             the virtual pad's button (a, b, x, y, back, start, lb, rb, dpup, dpdown, dpleft, dpright),
 *                        6 polls
 *   zone NAME            a career shop zone from free roam (safehouse, carlot, customshop)
 *   shot NAME            the next frame saved as NAME.bmp beside the exe (Vulkan)
 *   car                  the car the car select shows, the one the customization shop works on, the player's in a
 *                        race (car type names)
 *
 * What it reads (the PC exe: FEngine::FindPackageWithControl 0x5b0be0, FEPackage::GetFirstObject 0x564aa0,
 * FEPackage::SetCurrentButton 0x5b8610; the decompiled FEng packs its booleans in words): cFEng's FEngine at +0, its
 * package list at +0xd8 (head +0xe0); a package's name +0xc, priority +0x1c, pads +0x20, objects +0x44 (head +0x4c),
 * the object with the pads +0x8c; an object's next +4, name hash +0x10, type +0x18 (2 string, 5 group), flags +0x1c (bit 0 hidden), data +0x2c;
 * a group's children +0x5c (head +0x64); a string's label hash +0x60 and wide text +0x64. An IconScrollerMenu (its
 * scroller's vtable at +0x2c) has its options' list at +0x30 (nodes at an option's +4), the chosen option at +0x38 and
 * the chosen option's name string at +0x12c (IconScrollerMenu::RefreshHeader 0x573870); an option's name hash is at
 * +0x2c.
 */
#include <windows.h>

#include <string.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "core/input.h"
#include "core/log.h"
#include "dev/fescript.h"
#include "dev/pose.h"
#include "dev/testbridge.h"
#include "gameplay/vehicles/cars.h"
#include "legacy/link.h"

namespace
{
const DWORD kAnswerMs = 15000;
const int kRequestBytes = 512;
const int kAnswerBytes = 512 * 1024;
const int kMaxDepth = 16;

const int kEngine = 0;
const int kPackageList = 0xd8;
const int kListHead = 8;
const int kNodeNext = 4;
const int kNodePrev = 8;
const int kPackageName = 0xc;
const int kPackagePriority = 0x1c;
const int kPackagePads = 0x20;
const int kPackageObjects = 0x44;
const int kPackageButton = 0x8c;
const int kGroupChildren = 0x5c;
const int kStringLabel = 0x60;
const int kStringText = 0x64;
const int kTypeString = 2;
const int kTypeGroup = 5;
const unsigned int kHidden = 1;

const int kScreenPackage = 0x10;
const int kScroller = 0x2c;
const int kScrollerOptions = 0x30;
const int kScrollerChosen = 0x38;
const int kScrollerNameString = 0x12c;
const int kOptionNode = 4;
const int kOptionName = 0x2c;
const int kMaxOptions = 200;
/** IconScroller's and IconPanel's vtables. */
const unsigned int kScrollerVtables[] = {0x8a1e10, 0x89cde0};

const int kFlowFrontEnd = 3;
const int kSlotBody = 23;
/** IVehicle's _IHandle (UTL::COM::Object::_IList::Find) and its name's vtable slot. */
const unsigned int kVehicleHandle = 0x4040e0;
const int kVehicleNameSlot = 0x4c / 4;

const char *const kFlowStates[] = {"none",           "loading_frontend", "unloading_frontend", "frontend",
                                   "loading_region", "loading_track",    "racing",             "unloading_track",
                                   "unloading_region", "exit_demo"};

HANDLE g_ready;
HANDLE g_done;
char g_request[kRequestBytes];
char *g_answer;
int g_length;
volatile LONG g_waiting;
bool g_started;
unsigned int g_frame;

template <typename T> T At(const void *base, int offset)
{
    return *reinterpret_cast<const T *>(static_cast<const char *>(base) + offset);
}

// -- the answer, as JSON

void Raw(const char *text)
{
    while (*text && g_length < kAnswerBytes - 16)
        g_answer[g_length++] = *text++;
}

void Number(int value)
{
    char text[16];
    wsprintfA(text, "%d", value);
    Raw(text);
}

void Hex(unsigned int value)
{
    char text[16];
    wsprintfA(text, "\"0x%08x\"", value);
    Raw(text);
}

void Tenths(float value)
{
    const int t = static_cast<int>(value * 10.0f + (value < 0.0f ? -0.5f : 0.5f));
    char text[24];
    wsprintfA(text, "%s%d.%d", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
    Raw(text);
}

void PutChar(unsigned int c)
{
    char text[12];
    if (c == '"' || c == '\\')
    {
        text[0] = '\\';
        text[1] = static_cast<char>(c);
        text[2] = 0;
    }
    else if (c < 0x20 || c > 0x7e)
        wsprintfA(text, "\\u%04x", c & 0xffff);
    else
    {
        text[0] = static_cast<char>(c);
        text[1] = 0;
    }
    Raw(text);
}

void String(const char *text)
{
    Raw("\"");
    for (const char *p = text ? text : ""; *p; ++p)
        PutChar(static_cast<unsigned char>(*p));
    Raw("\"");
}

void WideString(const short *text)
{
    Raw("\"");
    for (int i = 0; text && text[i] && i < 512; ++i)
        PutChar(static_cast<unsigned short>(text[i]));
    Raw("\"");
}

void Key(const char *name)
{
    if (g_length && g_answer[g_length - 1] != '{' && g_answer[g_length - 1] != '[')
        Raw(",");
    Raw("\"");
    Raw(name);
    Raw("\":");
}

void Comma()
{
    if (g_length && g_answer[g_length - 1] != '{' && g_answer[g_length - 1] != '[')
        Raw(",");
}

// -- the front end

/** A language label's text, or null when the language files have none (the game shows its default-string error). */
const char *Localized(unsigned int label)
{
    if (!label || label == 0xffffffff)
        return nullptr;
    const char *text = GetLocalizedString(label);
    return text && *text && _strnicmp(text, "FENG: Default string error", 26) ? text : nullptr;
}

char *Engine()
{
    char *fe = reinterpret_cast<char *>(cFEng::mInstance);
    return fe ? At<char *>(fe, kEngine) : nullptr;
}

/** Calls f(package) for each package, first to last. */
template <typename F> void EachPackage(F f)
{
    char *engine = Engine();
    if (!engine)
        return;
    for (char *p = At<char *>(engine, kPackageList + kListHead); p; p = At<char *>(p, kNodeNext))
        f(p);
}

const char *PackageName(const char *package)
{
    const char *name = At<const char *>(package, kPackageName);
    return name ? name : "";
}

/** The last package with the pads (FEngine::FindPackageWithControl). */
char *Controlling()
{
    char *found = nullptr;
    EachPackage([&](char *p) {
        if (At<unsigned int>(p, kPackagePads))
            found = p;
    });
    return found;
}

char *PackageNamed(const char *name)
{
    if (!name || !*name)
        return Controlling();
    char *found = nullptr;
    EachPackage([&](char *p) {
        if (!lstrcmpiA(PackageName(p), name))
            found = p;
    });
    return found;
}

bool IsScroller(const char *screen)
{
    if (!screen)
        return false;
    const unsigned int vtable = At<unsigned int>(screen, kScroller);
    for (unsigned int v : kScrollerVtables)
        if (vtable == v)
            return true;
    return false;
}

const char *ScreenOf(const char *package)
{
    return package ? reinterpret_cast<const char *>(FEngFindScreen(PackageName(package))) : nullptr;
}

const short *TextOf(const FEObject *object)
{
    return object && object->Type == kTypeString ? At<const short *>(object, kStringText) : nullptr;
}

/** A string's text, or a group's strings' texts joined by " | ". */
void ObjectTexts(const FEObject *object, int depth, bool &first)
{
    for (; object; object = object->Next)
    {
        if (object->Flags & kHidden)
            continue;
        if (const short *text = TextOf(object))
        {
            const char *label = Localized(At<unsigned int>(object, kStringLabel));
            if (!text[0] && !label)
                continue;
            if (!first)
                Raw(" | ");
            first = false;
            if (label)
                for (const char *p = label; *p; ++p)
                    PutChar(static_cast<unsigned char>(*p));
            else
                for (int i = 0; text[i] && i < 256; ++i)
                    PutChar(static_cast<unsigned short>(text[i]));
        }
        else if (object->Type == kTypeGroup && depth < kMaxDepth)
            ObjectTexts(At<const FEObject *>(object, kGroupChildren + kListHead), depth + 1, first);
    }
}

void ObjectText(const FEObject *object, int depth)
{
    Raw("\"");
    bool first = true;
    if (object && object->Type == kTypeGroup)
        ObjectTexts(At<const FEObject *>(object, kGroupChildren + kListHead), depth + 1, first);
    else if (const short *text = TextOf(object))
    {
        if (const char *label = Localized(At<unsigned int>(object, kStringLabel)))
            for (const char *p = label; *p; ++p)
                PutChar(static_cast<unsigned char>(*p));
        else
            for (int i = 0; text[i] && i < 256; ++i)
                PutChar(static_cast<unsigned short>(text[i]));
    }
    Raw("\"");
}

void Strings(const FEObject *object, float x, float y, int depth)
{
    for (; object; object = object->Next)
    {
        const FEObjData *d = object->pData;
        if ((object->Flags & kHidden) || (d && !d->Col[3]))
            continue;
        const float ox = x + (d ? d->Pos[0] : 0.0f), oy = y + (d ? d->Pos[1] : 0.0f);
        if (object->Type == kTypeString)
        {
            const short *text = TextOf(object);
            if (!text || !text[0])
                continue;
            Comma();
            Raw("{");
            Key("text");
            WideString(text);
            Key("x");
            Tenths(ox);
            Key("y");
            Tenths(oy);
            Key("alpha");
            Number(d ? d->Col[3] : 255);
            Key("colour");
            Raw("[");
            Number(d ? d->Col[2] : 255);
            Raw(",");
            Number(d ? d->Col[1] : 255);
            Raw(",");
            Number(d ? d->Col[0] : 255);
            Raw("]");
            Key("name");
            Hex(object->NameHash);
            const unsigned int label = At<unsigned int>(object, kStringLabel);
            Key("label");
            Hex(label);
            if (const char *text = Localized(label))
            {
                Key("label_text");
                String(text);
            }
            Raw("}");
        }
        else if (object->Type == kTypeGroup && depth < kMaxDepth)
            Strings(At<const FEObject *>(object, kGroupChildren + kListHead), ox, oy, depth + 1);
    }
}

void Button(const char *package)
{
    const FEObject *button = At<const FEObject *>(package, kPackageButton);
    Key("button");
    if (!button)
    {
        Raw("null");
        return;
    }
    Raw("{");
    Key("name");
    Hex(button->NameHash);
    Key("guid");
    Number(static_cast<int>(button->GUID));
    Key("text");
    ObjectText(button, 0);
    Raw("}");
}

void Scroller(const char *screen)
{
    const char *sentinel = screen + kScrollerOptions;
    const char *chosen = At<const char *>(screen, kScrollerChosen);
    Key("kind");
    String("icon_scroller");
    Key("options");
    Raw("[");
    int index = 0, chosenIndex = -1;
    for (const char *node = At<const char *>(sentinel, 0); node && node != sentinel && index < kMaxOptions;
         node = At<const char *>(node, 0), ++index)
    {
        const char *option = node - kOptionNode;
        if (option == chosen)
            chosenIndex = index;
        const unsigned int name = At<unsigned int>(option, kOptionName);
        Comma();
        Raw("{");
        Key("name");
        const char *text = Localized(name);
        String(text ? text : "");
        Key("hash");
        Hex(name);
        Raw("}");
    }
    Raw("]");
    Key("chosen");
    Number(chosenIndex);
    Key("chosen_text");
    const FEObject *shown = At<const FEObject *>(screen, kScrollerNameString);
    if (TextOf(shown))
        WideString(TextOf(shown));
    else
    {
        const char *text = chosen ? Localized(At<unsigned int>(chosen, kOptionName)) : nullptr;
        String(text ? text : "");
    }
}

// -- the requests

const char *Argument(const char *request)
{
    const char *space = strchr(request, ' ');
    if (!space)
        return "";
    while (*space == ' ')
        ++space;
    return space;
}

bool Is(const char *request, const char *command)
{
    const int n = lstrlenA(command);
    return !_strnicmp(request, command, n) && (request[n] == 0 || request[n] == ' ');
}

void State()
{
    Key("frame");
    Number(static_cast<int>(g_frame));
    const int flow = TheGameFlowManager.CurrentGameFlowState;
    Key("flow");
    String(flow >= 0 && flow < static_cast<int>(sizeof(kFlowStates) / sizeof(kFlowStates[0])) ? kFlowStates[flow] : "?");
    Key("packages");
    Raw("[");
    EachPackage([](char *p) {
        Comma();
        Raw("{");
        Key("name");
        String(PackageName(p));
        Key("pads");
        Raw(At<unsigned int>(p, kPackagePads) ? "true" : "false");
        Key("priority");
        Number(At<int>(p, kPackagePriority));
        Raw("}");
    });
    Raw("]");
    char *control = Controlling();
    Key("control");
    if (control)
        String(PackageName(control));
    else
        Raw("null");
    Key("screen");
    const char *screen = ScreenOf(control);
    String(!screen ? "none" : IsScroller(screen) ? "icon_scroller" : "other");
    Key("screen_vtable");
    Hex(screen ? At<unsigned int>(screen, 0) : 0);
}

void Screen(const char *name)
{
    char *package = PackageNamed(name);
    if (!package)
    {
        Key("error");
        String("no such package");
        return;
    }
    Key("package");
    String(PackageName(package));
    Key("strings");
    Raw("[");
    Strings(At<const FEObject *>(package, kPackageObjects + kListHead), 0.0f, 0.0f, 0);
    Raw("]");
    Button(package);
}

void Menu(const char *name)
{
    char *package = PackageNamed(name);
    if (!package)
    {
        Key("error");
        String("no such package");
        return;
    }
    Key("package");
    String(PackageName(package));
    const char *screen = ScreenOf(package);
    if (IsScroller(screen))
        Scroller(screen);
    else
    {
        Key("kind");
        String(screen ? "other" : "none");
    }
    Button(package);
}

/** A car type's name (CarTypeInfo's first 16 bytes), or "". */
const char *CarTypeName(int type)
{
    if (type < 0 || type >= nfs::cars::NumCarTypes())
        return "";
    return reinterpret_cast<const char *>(CarTypeInfoArray) + type * nfs::cars::kCarTypeInfoSize;
}

/** The cars on the screen: the car select's (UIQRCarSelect::GetSelectedCarRecord 0x7a3db0, FECarRecord::GetType), the
 * one the customization shop works on (its body part's car type: CarCustomizeManager 0x9b9ec0's installed part,
 * 0x7a4b10), and in a race the player's (its IVehicle's name, vtable +0x4c). */
void Cars()
{
    char *control = Controlling();
    Key("select");
    const char *screen = control && !lstrcmpiA(PackageName(control), "Car_Select.fng") ? ScreenOf(control) : nullptr;
    FECarRecord *record = screen ? NFS_ORIGINAL(0x7a3db0, FECarRecord *(__thiscall *)(const char *))(screen) : nullptr;
    if (record)
    {
        String(CarTypeName(static_cast<int>(record->GetType())));
        Key("select_name");
        String(record->GetDebugName());
    }
    else
        Raw("null");
    Key("customize");
    // The shop's manager has a car only while its screens are up (its installed part reads its car record).
    const bool shop = control && !_strnicmp(PackageName(control), "Customize", 9);
    CarPart *body = shop && TheGameFlowManager.CurrentGameFlowState == kFlowFrontEnd
                        ? NFS_ORIGINAL(0x7a4b10, CarPart *(__thiscall *)(char *, int))(reinterpret_cast<char *>(0x9b9ec0),
                                                                                         kSlotBody)
                        : nullptr;
    if (body)
        String(CarTypeName(static_cast<int>(CarPartDB.GetCarType(body->GetCarTypeNameHash()))));
    else
        Raw("null");
    Key("player");
    void *vehicle = nfs::pose::PlayerSimable() ? nfs::pose::PlayerInterface(kVehicleHandle) : nullptr;
    if (vehicle)
    {
        typedef const char *(__thiscall * NameOf)(void *);
        String(reinterpret_cast<NameOf>((*reinterpret_cast<void ***>(vehicle))[kVehicleNameSlot])(vehicle));
    }
    else
        Raw("null");
}

/** A keyboard key through the game's own input (its SDL key state), held a few polls. */
bool PressKey(const char *name)
{
    static const struct
    {
        const char *name;
        int key;
    } kKeys[] = {{"up", 200}, {"down", 208}, {"left", 203}, {"right", 205}, {"enter", 28},
                 {"escape", 1}, {"space", 57},  {"1", 2},      {"2", 3},      {"backspace", 14}};
    for (const auto &k : kKeys)
        if (!lstrcmpiA(name, k.name))
        {
            nfs::input::InjectKey(k.key, 6);
            return true;
        }
    return false;
}

/** A virtual gamepad's button (SDL's names: a, b, x, y, back, start, dpup, dpdown, dpleft, dpright). */
bool PressPad(const char *name)
{
    // SDL_GamepadButton's numbers.
    static const char *const kButtons[] = {"a",  "b",  "x",  "y",      "back",     "guide", "start", "ls",
                                           "rs", "lb", "rb", "dpup", "dpdown", "dpleft", "dpright"};
    for (int b = 0; b < static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0])); ++b)
        if (!lstrcmpiA(name, kButtons[b]))
            return nfs::input::PressVirtualPad(b, 6);
    return false;
}

void Answer(const char *request)
{
    g_length = 0;
    Raw("{");
    bool ok = true;
    if (Is(request, "state"))
        State();
    else if (Is(request, "screen"))
        Screen(Argument(request));
    else if (Is(request, "menu"))
        Menu(Argument(request));
    else if (Is(request, "car"))
        Cars();
    else if (Is(request, "press"))
        ok = nfs::fescript::Press(Argument(request));
    else if (Is(request, "key"))
        ok = PressKey(Argument(request));
    else if (Is(request, "vpad"))
        ok = nfs::input::AttachVirtualPad();
    else if (Is(request, "pad"))
        ok = PressPad(Argument(request));
    else if (Is(request, "click"))
    {
        const char *a = Argument(request);
        const int x = static_cast<int>(bStrToFloat(a));
        while (*a && *a != ' ')
            ++a;
        while (*a == ' ')
            ++a;
        nfs::fescript::ClickAt(x, static_cast<int>(bStrToFloat(a)));
    }
    else if (Is(request, "zone"))
        ok = nfs::fescript::Zone(Argument(request));
    else if (Is(request, "shot"))
        ok = nfs::fescript::Shot(Argument(request));
    else
    {
        ok = false;
        Key("error");
        String("unknown request");
    }
    Key("ok");
    Raw(ok ? "true" : "false");
    Raw("}\n");
    g_answer[g_length] = 0;
}

/** The pipe's thread: one client at a time, a line in, a line out. */
DWORD WINAPI Serve(void *)
{
    char name[64];
    wsprintfA(name, "\\\\.\\pipe\\nfsmw_test_%u", GetCurrentProcessId());
    for (;;)
    {
        HANDLE pipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                       kAnswerBytes, kRequestBytes, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            nfs::Log("testbridge: no pipe %s (%lu)", name, GetLastError());
            return 1;
        }
        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
        {
            CloseHandle(pipe);
            continue;
        }
        char line[kRequestBytes];
        int have = 0;
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(pipe, line + have, kRequestBytes - 1 - have, &read, nullptr) || !read)
                break;
            have += static_cast<int>(read);
            line[have] = 0;
            char *end;
            while ((end = strchr(line, '\n')) != nullptr)
            {
                *end = 0;
                if (end > line && end[-1] == '\r')
                    end[-1] = 0;
                lstrcpynA(g_request, line, kRequestBytes);
                ResetEvent(g_done);
                InterlockedExchange(&g_waiting, 1);
                SetEvent(g_ready);
                const char *answer = g_answer;
                char busy[64];
                if (WaitForSingleObject(g_done, kAnswerMs) != WAIT_OBJECT_0)
                {
                    if (InterlockedExchange(&g_waiting, 0))
                    {
                        lstrcpyA(busy, "{\"ok\":false,\"busy\":true}\n");
                        answer = busy;
                    }
                    else
                        WaitForSingleObject(g_done, INFINITE);
                }
                DWORD written = 0;
                WriteFile(pipe, answer, lstrlenA(answer), &written, nullptr);
                const int rest = have - static_cast<int>(end + 1 - line);
                memmove(line, end + 1, rest + 1);
                have = rest;
            }
            if (have >= kRequestBytes - 1)
                have = 0;
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void Start()
{
    g_started = true;
    if (!nfs::config::Int("Debug", "TestBridge", 0))
        return;
    g_answer = static_cast<char *>(VirtualAlloc(nullptr, kAnswerBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_ready = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    g_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!g_answer || !g_ready || !g_done || !CreateThread(nullptr, 0, Serve, nullptr, 0, nullptr))
    {
        nfs::Log("testbridge: could not start");
        return;
    }
    nfs::Log("testbridge: serving \\\\.\\pipe\\nfsmw_test_%u", GetCurrentProcessId());
}
} // namespace

namespace
{
/** The text of a string as it shows (its label's text when it has one), into out. */
void PlainText(const FEObject *object, char *out, int size)
{
    out[0] = 0;
    if (const char *label = Localized(At<unsigned int>(object, kStringLabel)))
    {
        lstrcpynA(out, label, size);
        return;
    }
    const short *text = TextOf(object);
    int n = 0;
    for (; text && text[n] && n < size - 1; ++n)
        out[n] = static_cast<char>(text[n] > 0 && text[n] < 0x80 ? text[n] : '?');
    out[n] = 0;
}

/** The one shown string drawn in the highlight colour (lists, dialogs: 190, 128, 43), or null; `count` how many. */
const FEObject *Highlighted(const FEObject *object, int depth, int &count)
{
    const FEObject *found = nullptr;
    for (; object; object = object->Next)
    {
        const FEObjData *d = object->pData;
        if ((object->Flags & kHidden) || (d && !d->Col[3]))
            continue;
        if (object->Type == kTypeString && d && d->Col[2] == 190 && d->Col[1] == 128 && d->Col[0] == 43)
        {
            found = object;
            ++count;
        }
        else if (object->Type == kTypeGroup && depth < kMaxDepth)
        {
            if (const FEObject *inner = Highlighted(At<const FEObject *>(object, kGroupChildren + kListHead), depth + 1, count))
                found = inner;
        }
    }
    return found;
}
} // namespace

const char *nfs::testbridge::ControlPackage()
{
    const char *package = Controlling();
    return package ? PackageName(package) : "";
}

bool nfs::testbridge::Chosen(char *out, int size)
{
    out[0] = 0;
    const char *package = Controlling();
    if (!package)
        return false;
    const char *screen = ScreenOf(package);
    if (IsScroller(screen))
    {
        const char *chosen = At<const char *>(screen, kScrollerChosen);
        const char *text = chosen ? Localized(At<unsigned int>(chosen, kOptionName)) : nullptr;
        lstrcpynA(out, text ? text : "", size);
        return text != nullptr;
    }
    int count = 0;
    const FEObject *lit = Highlighted(At<const FEObject *>(package, kPackageObjects + kListHead), 0, count);
    if (count != 1)
        return false;
    PlainText(lit, out, size);
    return true;
}

void nfs::testbridge::Update()
{
    if (!g_started)
        Start();
    ++g_frame;
    if (!g_ready || WaitForSingleObject(g_ready, 0) != WAIT_OBJECT_0)
        return;
    if (!InterlockedCompareExchange(&g_waiting, 0, 1))
        return;
    Answer(g_request);
    SetEvent(g_done);
}
