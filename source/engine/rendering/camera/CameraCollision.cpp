/**
 * Two fixes of the chase camera far from the world's middle (the proving ground, 20 km out, dev/proving.h).
 *
 * The camera's shake and wobble (original: zMisc ApplyCameraShake 0x64c420, zCamera 0x467420) multiply a small
 * rotation onto the camera's matrix (world to view) on the world's side, so besides turning the view they turn the eye
 * round the world's origin. Near Rockport's middle that moves the eye by a few centimetres; 29 km from the origin it
 * put the camera 0.4 to 0.7 m to one side of the car. The view now turns as before and the eye stays where the camera
 * put it (the translation is solved again for the eye).
 *
 * The camera's checks against the world's collision geometry (zCamera 0x470fe0, its sphere against the barriers and
 * the ground, and 0x47c6f0, pushing it out of what it is inside; both move the eye and aim it back at the car) are left
 * out at the proving ground, where there is no geometry: the lookup fell on the edge of the collision grid and found
 * barriers that are not there, and the camera looked past the car.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "dev/proving.h"

bool __fastcall CameraWorldCheck(void *self, void *, void *matrix, const float *car);
bool __fastcall CameraPushOut(void *self, void *, float *matrix, const float *car, float *anchor);
void __fastcall CameraWobble(void *self, void *, float *matrix, float time, float scale);
void __cdecl CameraShake(int view, float *matrix);

NFS_REPLACE(0x470fe0, CameraWorldCheck)
NFS_REPLACE(0x47c6f0, CameraPushOut)
NFS_REPLACE(0x467420, CameraWobble)
NFS_REPLACE(0x64c420, CameraShake)

namespace
{
/** The eye of a world-to-view matrix (rows the view's axes in world space, row 3 the translation). */
void Eye(const float *m, float *eye)
{
    for (int j = 0; j < 3; ++j)
        eye[j] = -(m[12] * m[j * 4 + 0] + m[13] * m[j * 4 + 1] + m[14] * m[j * 4 + 2]);
}

/** The translation that puts the matrix's eye at `eye` with its axes as they are now (Eye solved for it). */
void PlaceEye(float *m, const float *eye)
{
    // eye = -A t with A[j][i] = m[j * 4 + i]: t = -A^-1 eye.
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    const double det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (det == 0.0)
        return;
    const double inv[9] = {(e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
                           (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
                           (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int i = 0; i < 3; ++i)
        m[12 + i] = static_cast<float>(-(inv[i * 3 + 0] * eye[0] + inv[i * 3 + 1] * eye[1] + inv[i * 3 + 2] * eye[2]));
}
} // namespace

bool __fastcall CameraWorldCheck(void *self, void *, void *matrix, const float *car)
{
    if (car && nfs::proving::AtSite(car))
        return false;
    return NFS_ORIGINAL(0x470fe0, bool(__thiscall *)(void *, void *, const float *))(self, matrix, car);
}

bool __fastcall CameraPushOut(void *self, void *, float *matrix, const float *car, float *anchor)
{
    if (car && nfs::proving::AtSite(car))
        return false;
    return NFS_ORIGINAL(0x47c6f0, bool(__thiscall *)(void *, float *, const float *, float *))(self, matrix, car, anchor);
}

void __fastcall CameraWobble(void *self, void *, float *matrix, float time, float scale)
{
    float eye[3];
    Eye(matrix, eye);
    NFS_ORIGINAL(0x467420, void(__thiscall *)(void *, float *, float, float))(self, matrix, time, scale);
    PlaceEye(matrix, eye);
}

void __cdecl CameraShake(int view, float *matrix)
{
    float eye[3];
    Eye(matrix, eye);
    NFS_ORIGINAL(0x64c420, void(__cdecl *)(int, float *))(view, matrix);
    PlaceEye(matrix, eye);
}

