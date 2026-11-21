#pragma once

class CarPart;
class RideInfo;

/**
 * Customization options (docs/CUSTOMIZATION.md, source/gameplay/vehicles/Customize.cpp): what the parts shop lists, from
 * data instead of code. An option is one of the original shop's own categories, or the parts of a car slot under a name
 * and an icon of its own, with the slots its parts carry along (a left headlight's right one and their glass). Options
 * come from CUSTOMIZE\OPTIONS.INI; a car's own file (CUSTOMIZE\CARS\<CAR>.INI) or an NFSMW Unlimiter car file
 * (UnlimiterData\<CAR>.ini) turns them on or off and renames them, so add-on cars made for the Unlimiter's Extra
 * Customization show their parts as they do there.
 */
namespace nfs::customize
{
const int kMaxSlots = 6;
const int kMaxHides = 4;
const int kIdLength = 32;

enum Kind
{
    /** One of the original's categories (body kits, spoilers, rims, hoods, roof scoops): its own screen. */
    kOriginal,
    /** The parts of a slot, on the parts screen. */
    kSlot,
};

enum Show
{
    kAlways,
    /** When the car has parts for it: a part carrying the filter attribute, or with no filter a second part. */
    kHasParts,
    /** Only when the car's file turns it on. */
    kWhenEnabled,
};

struct Option
{
    char id[kIdLength];
    Kind kind;
    /** The shop's category: the original's own, or one of ours (0x110 on: the parts screen's range). */
    unsigned int category;
    /** Language hash of its name, its icon and the back room's icon (bStringHashUpper of their names). */
    unsigned int title;
    unsigned int icon;
    unsigned int iconBackRoom;
    /** The slot whose parts it lists, then the slots its parts carry along (by name: <CAR>_STYLE01_LEFT_HEADLIGHT
     * takes <CAR>_STYLE01_RIGHT_HEADLIGHT). */
    int slots[kMaxSlots];
    int slotCount;
    /** An attribute a part must carry to count as one of this option's (0: any part of the slot). */
    unsigned int filter;
    /** Original categories it takes the place of while it shows. */
    unsigned int hides[kMaxHides];
    int hideCount;
    Show show;
    /** What else choosing a part does, by name (docs/CUSTOMIZATION.md "Effects"); empty for nothing more. */
    char effect[kIdLength];
};

/** The parts menu of a car type, in order: what shows for it. Returns the count. */
int PartsMenu(int carType, const Option **out, int capacity);

/** The option with this shop category, or null. */
const Option *ByCategory(unsigned int category);

/** The slot option that lists or carries this slot, or null. */
const Option *BySlot(int slot);

/** The part a ride takes in a slot another option carries along: the one named after the part in the option's first
 * slot, else the part given. */
CarPart *Carried(RideInfo *ride, int slot, CarPart *part);

/** After a ride took a part in an option's first slot: the parts named after it in the slots it carries along. */
typedef CarPart *(*SetPartFunction)(RideInfo *, int, CarPart *, bool);
void CarryAlong(RideInfo *ride, int slot, CarPart *part, bool update, SetPartFunction set);
} // namespace nfs::customize
