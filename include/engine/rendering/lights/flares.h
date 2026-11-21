#pragma once

/**
 * The light flare pool (source/engine/rendering/lights/LightFlarePool.cpp): the flares the scenery culling places at models' position
 * markers each frame (street lamps, signs, traffic lights), drawn by eRenderLightFlarePool.
 */
class eLightFlare;

namespace nfs::flares
{
/** A pool flare for this frame, hidden from the views whose bit is set in `excludedViews` (bit n: view n), drawn at
 * `intensity` (the fade of the scenery it belongs to); null when the pool is full. */
eLightFlare *Allocate(unsigned int excludedViews, float intensity);
} // namespace nfs::flares
