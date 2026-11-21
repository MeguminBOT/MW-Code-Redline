#pragma once
/**
 * The particle system's data as EmitterSystem::UpdateParticles (source/engine/rendering/particles/EmitterUpdate.cpp) uses it: groups of
 * emitters, each with its particles in a list and its attributes and curves.
 */
#include <stddef.h>

namespace nfs::particles
{
/** A particle (EmitterParticle, ParticleSlotPool), linked into its emitter's list. */
struct Particle
{
    Particle *next;
    Particle *prev;
    /** A8R8G8B8. */
    unsigned int color;          // 0x08
    float size;                  // 0x0c
    float velocity[3];           // 0x10
    char unknown_1c[0x14];
    float acceleration[3];       // 0x30
    char unknown_3c[0x14];
    float position[3];           // 0x50
    int unknown_5c;
    /** The texture animation's frame (GetAnimatedUVs). */
    unsigned int uv0;            // 0x60
    unsigned int uv1;            // 0x64
    /** Life left, in 1/1024 s. */
    unsigned short life;         // 0x68
    unsigned short rotation;     // 0x6a
    /** Texture animation phase, 0 to 65535 over a cycle. */
    unsigned short phase;        // 0x6c
    unsigned char spin;          // 0x6e
    unsigned char spinSpeed;     // 0x6f
};

/** The emitter's attributes (EmitterDataAttribWrapper). */
struct Attributes
{
    char unknown_00[0x90];
    /** EffectParticleAnimation: frames per side of the texture's animation grid; 0 for none. */
    int animation;               // 0x90
    unsigned char animationFrames; // 0x94
    char unknown_95[0x13];
    float drag;                  // 0xa8
    char unknown_ac[0x10];
    /** Seconds. */
    float lifetime;              // 0xbc
    char unknown_c0[0x48];
    /** Downwards along z when not 0; otherwise the particle's own acceleration applies. */
    float gravity;               // 0x108
    char unknown_10c[4];
    /** Particles at or below this alpha are freed unless keepFaded is set. */
    signed char killAlpha;       // 0x110
    unsigned char keepFaded;     // 0x111
};

struct Data
{
    int unknown_00[2];
    Attributes *attributes; // 0x08
    char unknown_0c[0x14];
    /** Colour (red, green, blue, alpha) and size curves: cubic in the particle's age (t^3, t^2, t, 1 times rows). */
    float colorCurve[16];        // 0x20
    float sizeCurve[16];         // 0x60
};

struct Emitter
{
    Emitter *next;
    Emitter *prev;
    char unknown_08[0x14];
    unsigned short particleCount; // 0x1c
    unsigned short type;          // 0x1e
    char unknown_20[0x5c];
    Data *data;            // 0x7c
    /** The list's head: its next and prev. */
    Particle *first;              // 0x80
    Particle *last;               // 0x84
};

struct Group
{
    Group *next;
    Group *prev;
    Emitter *first;              // 0x08
    Emitter *last;               // 0x0c
    char unknown_10[8];
    unsigned int flags;          // 0x18
    char unknown_1c[0x34];
    float position[3];           // 0x50
    char unknown_5c[8];
    float radius;                // 0x64
};
static_assert(offsetof(Group, radius) == 0x64, "EmitterGroup layout");
static_assert(offsetof(Emitter, data) == 0x7c, "Emitter layout");
static_assert(offsetof(Particle, life) == 0x68, "EmitterParticle layout");
static_assert(offsetof(Attributes, keepFaded) == 0x111, "EmitterDataAttribWrapper layout");
static_assert(offsetof(Data, sizeCurve) == 0x60, "EmitterData layout");

/** EmitterSystem: the particle counts (all, then per emitter type) and the emitter groups. */
struct System
{
    int particleCount;
    int typeCounts[(0x378 - 4) / 4];
    Group *firstGroup;    // 0x378
    Group *lastGroup;
};
} // namespace nfs::particles
