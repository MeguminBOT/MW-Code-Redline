#pragma once

#include "game/platform/D3DX.h"
#include "engine/rendering/vulkan/effect.h"

namespace nfs::vk
{
/**
 * The game's effect objects in Vulkan mode. The effect code the game keeps (zPlatform's eEffect: parameter binding by
 * semantic, default values, technique choice by annotation, per-draw parameter updates) calls effects through the
 * ID3DXEffect interface; this gives it the native effect runtime behind that interface, with passes applied to the
 * Vulkan renderer. There is no Direct3D 9 device or D3DX underneath.
 */
ID3DXEffect *CreateEffect(const char *resource);
/** The runtime behind an effect made by CreateEffect. */
Effect *NativeEffect(ID3DXEffect *effect);
/** The runtime's index of a parameter handle of such an effect (-1 for none, or for an element's handle). */
int NativeParameter(ID3DXEffect *effect, D3DXHANDLE handle);
} // namespace nfs::vk
