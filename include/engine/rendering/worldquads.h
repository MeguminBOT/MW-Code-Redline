#pragma once

class TextureInfo;

/**
 * Quads in world space drawn in one go (source/engine/rendering/frame/PolyRender.cpp), the way the game draws one with
 * RenderPolyTransformed (the current effect, the texture's state, the view's matrices). Not in the original game.
 */
namespace nfs::worldquads
{
struct Vertex
{
    float x, y, z;
    /** D3DCOLOR (0x80 per channel leaves the texture as it is). */
    unsigned int color;
    float u, v;
    float maskU, maskV;
};

/** `quads` quads, four corners each in order round the quad (as an ePoly's), with `texture`. */
void Draw(const Vertex *corners, int quads, TextureInfo *texture);
} // namespace nfs::worldquads
