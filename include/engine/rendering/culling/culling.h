#pragma once

/**
 * The scenery culling (source/engine/rendering/culling/SceneryCulling.cpp, docs/CULLING.md): which track scenery each view draws and with
 * which model. Replaces GrandSceneryCullInfo::DoCulling (0x731b80) and CullView (0x731b10) and
 * ScenerySectionHeader::DrawAScenery (0x729120).
 */
struct bMatrix4;

namespace nfs::culling
{
/** Which scenery casters a cascade's culling takes. The far cascades keep a static layer (engine/rendering/shadows/shadow.h): the scenery
 * in full drawn once, then each frame only what is not in it. */
enum CasterSet
{
    kAllCasters,
    /** Sections shown in full; the cascade's static layer holds them from now on. */
    kStaticCasters,
    /** Sections its static layer does not hold in full: fading in or out, loaded since, a low detail twin giving way. */
    kDynamicCasters,
    /** One slice of a static layer drawn over several frames (BeginStaticBuild): the sections in full whose number
     * falls in the slice. The layer in use keeps its own until FinishStaticBuild. */
    kStaticSlice,
};

/** While on, the views being culled are sun shadow cascades: every loaded track section (not the player's visibility
 * list), no preculler, nothing left out for lying behind the camera or being small, and full detail models, so casters
 * do not come and go as the player drives. Set around the cascades' DoCulling. */
void SetShadowCasters(bool on, int cascade = -1, CasterSet set = kAllCasters);
/** True when the cascade's static layer no longer matches the scenery: a section in it fades out, gives way to its
 * section or is gone, or the game changed scenery (InvalidateStaticCasters). */
bool StaticCastersChanged(int cascade);
/** Sections shown in full that the cascade's static layer does not hold yet (drawn each frame meanwhile). */
int UnbakedSections(int cascade);
/** A cascade's next static layer, drawn `slices` frames a slice (kStaticSlice passes with SetStaticSlice), then
 * taken into use with FinishStaticBuild. Changes from now on count against the new layer. */
void BeginStaticBuild(int cascade);
void SetStaticSlice(int slice, int slices);
void FinishStaticBuild(int cascade);
/** The game showed, hid or knocked over scenery (a smashed prop, a barrier group): every static layer is redrawn. */
void InvalidateStaticCasters();
/** How much of a draw with this world matrix to show (1 in full; less while its scenery instance fades in or out). */
float DrawFade(const bMatrix4 *world);
/** The track streamer is about to unload the section (true): its scenery and shadows fade out, and SectionFadedOut says
 * when they are gone. False: it stays after all (it fades back in) or has been unloaded. source/gameplay/world/TrackStreaming.cpp. */
void SectionUnloading(int section, bool unloading);
bool SectionFadedOut(int section);
} // namespace nfs::culling
