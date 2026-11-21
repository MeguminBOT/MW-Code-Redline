#pragma once

/**
 * View helpers shared by the projection setup (source/engine/rendering/frame/eViewProjection.cpp) and the rebuild's own passes.
 */
struct bMatrix4;
struct bVector4;

/** The six clip planes (left, right, bottom, top, near, far; normalized, pointing in) of a world-to-clip matrix, as
 * eViewPlatInfo::FrustumPlanes holds them. */
void eBuildFrustumPlanes(const bMatrix4 &worldToClip, bVector4 *planes);
