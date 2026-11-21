#pragma once

namespace nfs
{
namespace cars
{
/** Type 84 means any car (or none) to the original code; data with more car types keeps a placeholder there. */
const int kAnyCarType = 0x54;
const int kCarTypeInfoSize = 0xD0;

/** The car types GLOBALB's CarTypeInfo table holds (84 in the shipped data, more once Carbon's cars are added). */
int NumCarTypes();

/** Sizes the car loader's memory pool from speed.ini before the game sets it up (source/gameplay/vehicles/CarPool.cpp).
 */
void ConfigureLoaderPool();

/** Unloads one thing from the car loader's pool; 0 when nothing could go. */
int MakeRoomInLoaderPool(class CarLoader *loader, bool urgent);

/** The parts -set Debug.RideParts lists, put on the ride (source/gameplay/vehicles/RideParts.cpp). */
void ApplyDebugParts(class RideInfo *ride);

/** Whether a car's pop-up headlights are shut now (CarRenderInfo; source/gameplay/vehicles/PopupHeadlights.cpp): shut,
 * they give no light. */
bool PopupsShut(const void *render_info);
} // namespace cars
} // namespace nfs
