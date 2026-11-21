#pragma once

/**
 * Shadow debugging while playing (source/engine/rendering/shadows/ShadowDebug.cpp): F10 shows a panel under the position box (sun and
 * shadow direction, each cascade's size, age and casters, what the culling fades) and logs what changes to nfs.log
 * with the frame number the panel shows, so a recording of the screen and the log line up. F8 tints the rebuild's own
 * shaders by the cascade that shadows each pixel. Debug.ShadowHud=1 starts with the panel on, Debug.ShadowTint=1 with
 * the tint.
 */
namespace nfs::shadow::debug
{
const int kMaxLines = 10;
const int kLineLength = 72;

/** Why a cascade was drawn this frame. */
enum Redraw
{
    kNotRedrawn,
    kScheduled,
    kViewLeft,
    kJump,
    /** A far cascade's static layer, for scenery that changed (engine/rendering/culling/culling.h StaticCastersChanged). */
    kSceneryChanged,
    /** A far cascade's static layer, for the sun the time of day's clock moved (gameplay/world/timeofday.h). */
    kSunMoved,
};

/** Reads the keys; call once a frame. */
void Poll();
/** Panel and log on. */
bool Enabled();
/** 0 normal, 2 cascade tint (atlasInfo.w; 1 is -shadowdebug's full view). */
float ViewMode();

void NoteFrame(const float sun[3], const float cameraDirection[3]);
void NoteCascade(int index, Redraw redraw, float split, float radius, float texel);
/** The cascade being drawn now (-1 when none), which NoteCaster counts for. */
void SetCurrentCascade(int index);
/** A scenery caster drawn into the cascade being drawn. */
void NoteCaster();
/** End of a cascade's culling: casters that appeared or vanished inside it since its last drawing. */
void NoteCasterChanges(int cascade, int appeared, int vanished, int streamedIn, int streamedOut);
/** One caster that appeared or vanished, for the log. */
void NoteCasterEvent(int cascade, bool appeared, const char *name, int section, const float centre[3], int metres,
                     const char *cause);
/** Player view: a scenery draw fading in or out. */
void NoteFade(bool in);
/** Player view: a section entering or leaving its visibility list. */
void NoteList(int section, bool entered);
/** The track streamer activating or unloading a section. */
void NoteStreaming(int section, bool loaded);

/** Testing: the nearest cascade receivers may use this frame (Debug.MinCascade), shown on the panel. */
void NoteForcedCascade(int cascade);
/** What NoteForcedCascade last noted (0 when nothing is forced); burst captures carry it in their names. */
int ForcedCascade();
/** The panel's lines; returns how many (0 when off). */
int Text(char (*lines)[kLineLength]);
} // namespace nfs::shadow::debug
