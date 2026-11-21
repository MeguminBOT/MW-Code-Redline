#pragma once

/**
 * Changing front-end package objects at run time, for layouts the packages (.fng files) do not have.
 *
 * An object's look is its data (FEObjData: color, position, size and, for images, texture coordinates), which its
 * scripts' key tracks write while they run. A script that starts again later writes its keys back, so a change goes
 * into the base keys of the object's scripts as well as into the data; the helpers below do both.
 */
#include "game/gen/api.h"

namespace nfs::feng
{
/** FEKeyTrack_Indices: what a key track animates. */
enum Track
{
    kColor = 0,
    kPivot = 1,
    kPosition = 2,
    kRotation = 3,
    kSize = 4,
    kUpperLeft = 5,
    kLowerRight = 6,
};

/** Sets one component (0 = x, 1 = y, 2 = z) of what `track` animates, in the data and in every script's base key. */
void SetTrack(FEObject *object, Track track, int component, float value);
/** Multiplies the same component by `factor`. */
void ScaleTrack(FEObject *object, Track track, int component, float factor);
/** Stretches an image vertically to `height`, centred on `center_y`, keeping its texture's scale (texture coordinates
 * grow with the height). */
void StretchImageY(FEObject *image, float center_y, float height);
/** A copy of `source` and its scripts, added to `group` under `name_hash`. */
FEObject *Clone(FEObject *source, FEObject *group, unsigned int name_hash);
/** The object's script with this ID; if it has none, a copy of `model` (one of its scripts) under that ID. */
FEScript *FindOrCloneScript(FEObject *object, unsigned int id, FEScript *model);
/** The object with this GUID (the numbering of the package file) in a loaded package. */
FEObject *FindByGuid(const char *package, unsigned int guid);
/** How a string's text lies about its position (FEString's format bits): left and top without the bits. */
enum TextFormat
{
    kTextCentre = 1,
    kTextRight = 2,
    kTextMiddle = 4,
    kTextBottom = 8,
    /** Lines break between words where they pass the string's width (in the font's units, before its scale). */
    kTextWrap = 0x10,
};

/** Sets how a string's text lies about its position, and the width it wraps at (kTextWrap). */
void SetTextFormat(FEString *string, unsigned int format, unsigned int wrap_width = 0);
/** Room for text of `characters` in a string: FEPrintf cuts what it prints to the room the string has (the package
 * gives each string the length of its own text). */
void Reserve(FEString *string, unsigned int characters);
/** Keeps a script's colour keys from fading the object below `least_alpha` (0 to 255): the packages' HIGHLIGHT script
 * blinks a row's name from nothing to full and back every second. */
void LimitScriptFade(FEObject *object, unsigned int script_id, int least_alpha);
/** The object's colour (0 to 255 each), in its data and in every script's base key. */
void SetColour(FEObject *object, int red, int green, int blue, int alpha);
/** The colour the object shows (its script's, or its data's without one): red, green, blue, alpha. */
void GetColour(FEObject *object, int colour[4]);
/** The object's position within its group. */
void SetPosition(FEObject *object, float x, float y);
/** Logs every object of a loaded package: GUID, name hash, type, flags, position, size, colour (source/dev/FEDump.cpp). */
void Dump(const char *package);
} // namespace nfs::feng
