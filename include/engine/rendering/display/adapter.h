#pragma once

struct IDirect3D9;

/**
 * The display adapter for the original code's g_D3D (source/engine/rendering/display/Adapter.cpp): what it asked Direct3D 9 (the
 * adapter's name and ids, its display modes, the device caps), answered without Direct3D 9.
 */
namespace nfs::adapter
{
/** The object that stands in for Direct3DCreate9's. */
IDirect3D9 *Create();
} // namespace nfs::adapter
