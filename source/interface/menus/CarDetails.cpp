/**
 * The drift/grip bar and the extended performance details under the car select screen's bars (original: xPCFrontEnd
 * UIQRCarSelect::UpdateSliders 0x7ad7d0 runs as before, then fills them in). Not in the original game.
 *
 * Car_Select.fng's stats panel holds three groups, TOP SPEED, ACCELERATION and HANDLING, each a label (and its
 * shadow, a second string) over a slider. The drift/grip bar is a copy of the HANDLING label and of the HANDLING
 * slider's objects one group lower, each in the group that holds its original, drawn by a copy of the screen's
 * HANDLING slider whose objects are the copies (its own Draw lays out the fill bar and caps from the base); it shows
 * nfs::performance::GripRating. The details are copies of the HANDLING
 * label and its shadow below that, one line per detail: the times to 60 mph and 100 km/h, the top speed, power and
 * torque, and the gearbox, drive and weight of the car as configured, from gameplay/vehicles/performance.h (the figures
 * UpdateSliders' first rating was made from).
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "interface/engine/feng.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"

NFS_REPLACE(0x7ad7d0, CarSelectDetails::UpdateSliders)

namespace
{
/** Car_Select.fng objects (GUIDs). */
const unsigned int kAccelerationGroup = 0x0f176f;
const unsigned int kHandlingGroup = 0x0f1776;
const unsigned int kHandlingLabel = 0x0f173c;
const unsigned int kHandlingShadow = 0x0f173d;
/** The copies' name hashes: kDetailHash + 2 x line (label) and + 1 (shadow); kGripHash + the drift/grip bar's
 * object (its label, shadow, then the slider's objects). */
const unsigned int kDetailHash = 0x4e465300;
const unsigned int kGripHash = 0x4e465400;
const int kLines = 4;
const int kPackageName = 0x10;
/** FEString's text (an FEWideString: characters, then the buffer's length). */
const int kStringText = 0x64;
const int kLineLength = 96;
/** FEGroup's first child (FEObject::Next links the rest) and its FEObject::Type; FEPackage's first object. */
const int kGroupChildren = 0x64;
const int kGroupType = 5;
const int kPackageObjects = 0x4c;
const int kMaxChildren = 16;
/** UIQRCarSelect's HANDLING slider (a TwoStageSlider) and its fields: the range, the value the preview bar shows and
 * the objects it draws (base, fill bar, handle, left and right caps, value string, preview bar). */
const int kHandlingSlider = 0x160;
const int kSliderSize = 0x44;
const int kSliderMax = 0x1c;
const int kSliderMin = 0x20;
const int kSliderPreview = 0x40;
const int kSliderObjects[] = {0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x3c};
/** cSlider::Draw's place in the vtable. */
const int kSliderDraw = 9;
const char kGripLabel[] = "DRIFT / GRIP";
/** In distances between two of the groups: the drift/grip bar a group below HANDLING, the first detail line a group
 * below that, the rest closer. */
const float kGripBar = 1.0f;
const float kFirstLine = 2.0f;
const float kLineStep = 0.55f;
const float kMph = 0.44704f;

typedef void(__thiscall *SetValue)(void *, float);

/** The drift/grip bar of the package it was made in. */
struct GripBar
{
    FEPackage *package;
    bool ready;
    alignas(4) unsigned char slider[kSliderSize];
    FEObject *copies[kMaxChildren];
    int count;
};

GripBar g_grip;

float *Position(FEObject *object)
{
    return reinterpret_cast<float *>(reinterpret_cast<char *>(object->pData) +
                                     object->GetDataOffset(static_cast<FEKeyTrack_Indices>(nfs::feng::kPosition)));
}

void SetText(FEObject *string, const char *text, bool allocate)
{
    if (allocate)
        reinterpret_cast<FEWideString *>(reinterpret_cast<char *>(string) + kStringText)->AllocateString(kLineLength);
    FEPrintf(reinterpret_cast<FEString *>(string), "%s", text);
}

void SetVisible(FEObject *object, bool visible)
{
    if (visible)
        FEngSetVisible(object);
    else
        FEngSetInvisible(object);
}

FEObject *FirstChild(FEObject *group)
{
    return *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(group) + kGroupChildren);
}

/** The group under `group` (from its first child `first` on) that holds `target`. */
FEObject *ParentOf(FEObject *first, FEObject *group, const FEObject *target)
{
    for (FEObject *o = first; o; o = o->Next)
    {
        if (o == target)
            return group;
        if (o->Type == kGroupType)
            if (FEObject *parent = ParentOf(FirstChild(o), o, target))
                return parent;
    }
    return nullptr;
}

/** Copies the HANDLING label and the objects of the screen's HANDLING slider a group lower, each into the group that
 * holds it, and the slider to draw the copies. */
void MakeGripBar(FEPackage *package, FEObject *label, FEObject *shadow, const unsigned char *screen, float spacing)
{
    g_grip = GripBar();
    g_grip.package = package;
    memcpy(g_grip.slider, screen + kHandlingSlider, kSliderSize);
    FEObject *sources[kMaxChildren] = {label, shadow};
    int count = 2;
    for (int offset : kSliderObjects)
        if (FEObject *object = *reinterpret_cast<FEObject **>(g_grip.slider + offset))
            sources[count++] = object;
    FEObject *root = *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(package) + kPackageObjects);
    for (int i = 0; i < count; ++i)
    {
        FEObject *parent = ParentOf(root, nullptr, sources[i]);
        FEObject *copy = parent ? nfs::feng::Clone(sources[i], parent, kGripHash + i) : nullptr;
        g_grip.copies[i] = copy;
        if (!copy)
        {
            nfs::Log("car details: no copy of %08x for the drift/grip bar", sources[i]->NameHash);
            continue;
        }
        nfs::feng::SetTrack(copy, nfs::feng::kPosition, 1, Position(sources[i])[1] + spacing * kGripBar);
        if (i < 2)
            SetText(copy, kGripLabel, true);
    }
    g_grip.count = count;
    for (int offset : kSliderObjects)
    {
        FEObject *&object = *reinterpret_cast<FEObject **>(g_grip.slider + offset);
        if (!object)
            continue;
        FEObject *mapped = nullptr;
        for (int i = 2; i < count && !mapped; ++i)
            if (sources[i] == object)
                mapped = g_grip.copies[i];
        if (!mapped)
            return;
        object = mapped;
    }
    g_grip.ready = true;
}

void ShowGripBar(const nfs::performance::Figures *figures)
{
    for (int i = 0; i < g_grip.count; ++i)
        if (g_grip.copies[i])
            SetVisible(g_grip.copies[i], figures && g_grip.ready);
    if (!figures || !g_grip.ready)
        return;
    const float value = nfs::performance::GripRating(*figures);
    NFS_ORIGINAL(0x56cd20, SetValue)(g_grip.slider, value);
    float &preview = *reinterpret_cast<float *>(g_grip.slider + kSliderPreview);
    const float low = *reinterpret_cast<float *>(g_grip.slider + kSliderMin);
    const float high = *reinterpret_cast<float *>(g_grip.slider + kSliderMax);
    preview = value < low ? low : (value > high ? high : value);
    void **vtable = *reinterpret_cast<void ***>(g_grip.slider);
    reinterpret_cast<void(__thiscall *)(void *)>(vtable[kSliderDraw])(g_grip.slider);
}

/** "4.5" (wsprintf has no floats). */
void Tenths(char *out, float value)
{
    const int t = static_cast<int>(value * 10.0f + 0.5f);
    wsprintfA(out, "%d.%d", t / 10, t % 10);
}

void Lines(const nfs::performance::Figures &f, char (*lines)[kLineLength])
{
    char t60[16], t100[16];
    if (f.zeroTo60Mph > 0.0f)
        Tenths(t60, f.zeroTo60Mph);
    else
        lstrcpyA(t60, "-");
    if (f.zeroTo100Kmh > 0.0f)
        Tenths(t100, f.zeroTo100Kmh);
    else
        lstrcpyA(t100, "-");
    wsprintfA(lines[0], "0-60 MPH %s S   0-100 KM/H %s S", t60, t100);
    wsprintfA(lines[1], "TOP SPEED %d KM/H (%d MPH)", static_cast<int>(f.topSpeed * 3.6f + 0.5f),
              static_cast<int>(f.topSpeed / kMph + 0.5f));
    wsprintfA(lines[2], "%d HP @ %d RPM   %d NM", static_cast<int>(f.peakPower / 745.7f + 0.5f),
              static_cast<int>(f.peakPowerRpm + 0.5f), static_cast<int>(f.peakTorque + 0.5f));
    const char *drive = f.frontDrive <= 0.0f ? "RWD" : (f.frontDrive >= 1.0f ? "FWD" : "AWD");
    wsprintfA(lines[3], "%d-SPEED %s   %s   %d KG", f.gears, drive, f.forcedInduction ? "TURBO" : "N/A",
              static_cast<int>(f.mass + 0.5f));
}

void Show(const unsigned char *screen, const nfs::performance::Figures *figures)
{
    const char *package_name = *reinterpret_cast<const char *const *>(screen + kPackageName);
    FEPackage *package = package_name ? cFEng::mInstance->FindPackage(package_name) : nullptr;
    if (!package)
        return;
    FEObject *group = package->FindObjectByGUID(kHandlingGroup);
    FEObject *label = package->FindObjectByGUID(kHandlingLabel);
    FEObject *shadow = package->FindObjectByGUID(kHandlingShadow);
    FEObject *above = package->FindObjectByGUID(kAccelerationGroup);
    if (!group || !label || !shadow || !above)
        return;
    const float spacing = Position(group)[1] - Position(above)[1];
    if (g_grip.package != package || !package->FindObjectByHash(kGripHash))
        MakeGripBar(package, label, shadow, screen, spacing);
    ShowGripBar(figures);
    char text[kLines][kLineLength] = {};
    if (figures)
        Lines(*figures, text);
    for (int line = 0; line < kLines; ++line)
    {
        FEObject *sources[2] = {label, shadow};
        for (int s = 0; s < 2; ++s)
        {
            const unsigned int hash = kDetailHash + 2 * line + s;
            FEObject *copy = package->FindObjectByHash(hash);
            const bool made = !copy;
            if (!copy)
            {
                copy = nfs::feng::Clone(sources[s], group, hash);
                if (!copy)
                    continue;
                nfs::feng::SetTrack(copy, nfs::feng::kPosition, 1,
                                    Position(sources[s])[1] + spacing * (kFirstLine + kLineStep * line));
            }
            // The label's text buffer holds its 15 characters; FEPrintf cuts a string at the buffer's length.
            SetText(copy, text[line], made);
            SetVisible(copy, figures != nullptr);
        }
    }
}
} // namespace

/** UIQRCarSelect (xPCFrontEnd). */
struct CarSelectDetails
{
    void UpdateSliders();
};

void CarSelectDetails::UpdateSliders()
{
    nfs::performance::BeginCapture();
    NFS_ORIGINAL(0x7ad7d0, void(__thiscall *)(CarSelectDetails *))(this);
    nfs::performance::Figures figures;
    const bool have = nfs::performance::EndCapture(figures);
    Show(reinterpret_cast<const unsigned char *>(this), have ? &figures : nullptr);
}
