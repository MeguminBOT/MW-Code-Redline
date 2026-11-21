/**
 * Frame rate counter (interface/hud/overlay.h). Not in the original game.
 *
 * It is drawn on the back buffer after the frame is finished, as solid quads: a 5x7 pixel font scaled to the screen
 * height, and one bar per frame for the graph. Direct3D 9 draws it after the render scale's copy, so it stays sharp at
 * any scale, with the fixed function pipeline, the device's state saved in a state block around it so the game's next
 * frame starts as it would have. Vulkan draws it with the post effect's overlay_text technique (assets/shaders/post.fx): a
 * quad per character from a font texture made from the same glyphs, a tenth of the vertices.
 */
#include "game/platform/Platform.h"
#include "core/config.h"
#include "dev/dev.h"
#include "engine/rendering/display/display.h"
#include "core/log.h"
#include "interface/hud/overlay.h"
#include "dev/pose.h"
#include "dev/proving.h"
#include "dev/pursuitpanel.h"
#include "engine/rendering/shadows/shadowdebug.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::overlay
{
namespace
{
const int kGlyphRows = 7;
const int kGlyphColumns = 5;

struct Glyph
{
    char character;
    /** One row per byte, top first; bit 4 is the leftmost column. */
    unsigned char rows[kGlyphRows];
};

const Glyph kGlyphs[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}}, {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}}, {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}, {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}}, {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}}, {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}}, {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}}, {'*', {0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}}, {'\\', {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}}, {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};

/** Frames shown in the graph, and how long the numbers average over. */
const int kHistory = 120;
const double kWindowSeconds = 0.5;
/** Screen pixels per font pixel: one per this many lines of screen height. */
const int kLinesPerPixel = 400;

const DWORD kTextColor = 0xFFFFFFFF;
const DWORD kBackgroundColor = 0xA0000000;
const DWORD kDevStatusColor = 0xC0A00000;
const DWORD kAverageLineColor = 0x60FFFFFF;
const DWORD kFrameColor = 0xFF40E040;
/** Frames this much longer than the average show as hitches. */
const float kSlowFrame = 1.5f;
const float kHitchFrame = 2.0f;
const DWORD kSlowFrameColor = 0xFFFFC040;
const DWORD kHitchFrameColor = 0xFFFF4040;

struct Vertex
{
    float x, y, z, rhw;
    DWORD color;
    /** The font texture's texel (Vulkan); Direct3D 9 draws without it. */
    float u, v;
};
const DWORD kVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
const int kMaxVertices = 65536;
const int kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);
/** The font texture: a 6 x 8 cell per glyph in kGlyphs' order, then a lit cell for the solid quads. */
const int kCellWidth = kGlyphColumns + 1;
const int kCellHeight = kGlyphRows + 1;
const int kFontWidth = 512;
const int kFontHeight = kCellHeight;
static_assert((kGlyphCount + 1) * kCellWidth <= kFontWidth, "font texture width");

struct State
{
    bool loaded;
    bool shown;
    bool key_down;
    LARGE_INTEGER frequency;
    LARGE_INTEGER last;
    /** Frame times in milliseconds, oldest overwritten first. */
    float history[kHistory];
    int next;
    double window_seconds;
    int window_frames;
    float window_longest;
    int fps;
    float average_ms;
    float longest_ms;
    IDirect3DStateBlock9 *saved_state;
    ID3DXEffect *vk_effect;
    void *vk_layout;
    nfs::vk::Texture *vk_font;
    /** Quads from the font texture (Vulkan) rather than one per run of lit glyph pixels. */
    bool textured;
    Vertex vertices[kMaxVertices];
    int vertex_count;
    /** When the Vulkan overlay's quads were last built, and for what back buffer size: they are kept for frames
     * closer together than a 60 Hz display's. */
    LARGE_INTEGER built;
    unsigned int built_size;
};

State g_state;

void Load()
{
    if (g_state.loaded)
        return;
    g_state.loaded = true;
    QueryPerformanceFrequency(&g_state.frequency);
    char text[16];
    config::String("Display", "FpsCounter", "off", text, sizeof(text));
    g_state.shown = !lstrcmpA(text, "on");
}

void Toggle()
{
    g_state.shown = !g_state.shown;
    config::WriteString("Display", "FpsCounter", g_state.shown ? "on" : "off");
}

void CountFrame()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_state.last.QuadPart)
    {
        const double seconds =
            static_cast<double>(now.QuadPart - g_state.last.QuadPart) / static_cast<double>(g_state.frequency.QuadPart);
        const float ms = static_cast<float>(seconds * 1000.0);
        g_state.history[g_state.next] = ms;
        g_state.next = (g_state.next + 1) % kHistory;
        g_state.window_seconds += seconds;
        ++g_state.window_frames;
        if (ms > g_state.window_longest)
            g_state.window_longest = ms;
        if (g_state.window_seconds >= kWindowSeconds)
        {
            g_state.fps = static_cast<int>(g_state.window_frames / g_state.window_seconds + 0.5);
            g_state.average_ms = static_cast<float>(g_state.window_seconds * 1000.0 / g_state.window_frames);
            g_state.longest_ms = g_state.window_longest;
            static const bool log = nfs::vk::HasArgument("-fpslog");
            if (log)
                nfs::Log("fps: %d, longest frame %d us", g_state.fps, static_cast<int>(g_state.longest_ms * 1000.0f));
            g_state.window_seconds = 0.0;
            g_state.window_frames = 0;
            g_state.window_longest = 0.0f;
        }
    }
    g_state.last = now;
}

/** A quad showing the font texture's texels [u0, u1) x [v0, v1). */
void AddTexturedQuad(float left, float top, float right, float bottom, DWORD color, float u0, float v0, float u1,
                     float v1)
{
    if (g_state.vertex_count + 6 > kMaxVertices)
        return;
    // Pre-transformed vertices sit on pixel centres half a pixel in.
    left -= 0.5f;
    top -= 0.5f;
    right -= 0.5f;
    bottom -= 0.5f;
    const Vertex corners[6] = {{left, top, 0.0f, 1.0f, color, u0, v0},     {right, top, 0.0f, 1.0f, color, u1, v0},
                               {left, bottom, 0.0f, 1.0f, color, u0, v1},  {right, top, 0.0f, 1.0f, color, u1, v0},
                               {right, bottom, 0.0f, 1.0f, color, u1, v1}, {left, bottom, 0.0f, 1.0f, color, u0, v1}};
    Vertex *out = g_state.vertices + g_state.vertex_count;
    for (int i = 0; i < 6; ++i)
        out[i] = corners[i];
    g_state.vertex_count += 6;
}

void AddQuad(float left, float top, float right, float bottom, DWORD color)
{
    // The middle of the font texture's lit cell.
    const float u = (kGlyphCount * kCellWidth + kCellWidth * 0.5f) / kFontWidth, v = 0.5f;
    AddTexturedQuad(left, top, right, bottom, color, u, v, u, v);
}

/** kGlyphs' index of each character, -1 for none. */
struct GlyphTable
{
    signed char index[128];
    GlyphTable()
    {
        memset(index, -1, sizeof(index));
        for (int i = 0; i < kGlyphCount; ++i)
            index[static_cast<unsigned char>(kGlyphs[i].character) & 127] = static_cast<signed char>(i);
    }
};

int GlyphIndex(char character)
{
    static const GlyphTable table;
    return static_cast<unsigned char>(character) < 128 ? table.index[static_cast<unsigned char>(character)] : -1;
}

/** Adds `text` in the pixel font with its top-left corner at (x, y); returns the width it took. */
float AddText(float x, float y, float pixel, const char *text)
{
    const float start = x;
    for (; *text; ++text, x += (kGlyphColumns + 1) * pixel)
    {
        const char c = *text >= 'a' && *text <= 'z' ? static_cast<char>(*text - 'a' + 'A') : *text;
        const int index = GlyphIndex(c);
        if (index < 0)
            continue;
        if (g_state.textured)
        {
            const float u0 = static_cast<float>(index * kCellWidth) / kFontWidth;
            AddTexturedQuad(x, y, x + kGlyphColumns * pixel, y + kGlyphRows * pixel, kTextColor, u0, 0.0f,
                            u0 + static_cast<float>(kGlyphColumns) / kFontWidth,
                            static_cast<float>(kGlyphRows) / kFontHeight);
            continue;
        }
        const Glyph *glyph = &kGlyphs[index];
        // One quad per run of lit pixels in a row.
        for (int row = 0; row < kGlyphRows; ++row)
            for (int column = 0; column < kGlyphColumns; ++column)
            {
                if (!(glyph->rows[row] & (0x10 >> column)))
                    continue;
                int end = column + 1;
                while (end < kGlyphColumns && (glyph->rows[row] & (0x10 >> end)))
                    ++end;
                AddQuad(x + column * pixel, y + row * pixel, x + end * pixel, y + (row + 1) * pixel, kTextColor);
                column = end;
            }
    }
    return x - start;
}

/** "12.34" for 12.34 milliseconds (wsprintf has no floats). */
void FormatMs(char *text, float ms)
{
    const int hundredths = static_cast<int>(ms * 100.0f + 0.5f);
    wsprintfA(text, "%d.%02d", hundredths / 100, hundredths % 100);
}

/** A box of text lines against the right edge from `top` down; returns the bottom of the box. */
template <int Length> float AddRightBox(int width, float top, float pixel, float margin, float padding, float line,
                                        const char (*lines)[Length], int count)
{
    if (!count)
        return top;
    int longest = 0;
    for (int i = 0; i < count; ++i)
        longest = lstrlenA(lines[i]) > longest ? lstrlenA(lines[i]) : longest;
    const float text_width = (kGlyphColumns + 1) * pixel * static_cast<float>(longest);
    const float right = static_cast<float>(width) - margin;
    const float left = right - padding - text_width;
    const float bottom = top + padding + count * line;
    AddQuad(left - padding, top, right, bottom, kBackgroundColor);
    for (int i = 0; i < count; ++i)
        AddText(left, top + padding + i * line, pixel, lines[i]);
    return bottom;
}

/** The car and camera position box in the top-right corner (dev/pose.h), and under it the shadow debug panel
 * (engine/rendering/shadows/shadowdebug.h, F10). */
void AddPose(int width, float pixel, float margin, float padding, float line)
{
    char lines[nfs::pose::kMaxLines][nfs::pose::kLineLength];
    float bottom = AddRightBox(width, margin, pixel, margin, padding, line, lines, nfs::pose::Text(lines));
    char panel[nfs::shadow::debug::kMaxLines][nfs::shadow::debug::kLineLength];
    const int count = nfs::shadow::debug::Text(panel);
    if (count)
        AddRightBox(width, bottom + padding, pixel, margin, padding, line, panel, count);
}

/** The proving ground's panel (dev/proving.h) against the left edge, under the counter's place. */
void AddProving(float pixel, float margin, float padding, float line)
{
    char lines[nfs::proving::kMaxLines][nfs::proving::kLineLength];
    const int count = nfs::proving::Text(lines);
    if (!count)
        return;
    int longest = 0;
    for (int i = 0; i < count; ++i)
        longest = lstrlenA(lines[i]) > longest ? lstrlenA(lines[i]) : longest;
    const float text_width = (kGlyphColumns + 1) * pixel * static_cast<float>(longest);
    const float top = margin + 2.0f * line + 30.0f * pixel;
    const float left = margin + padding;
    AddQuad(margin, top, left + text_width + padding, top + 2.0f * padding + count * line, kBackgroundColor);
    for (int i = 0; i < count; ++i)
        AddText(left, top + padding + i * line, pixel, lines[i]);
}

/** The pursuit panel (dev/pursuitpanel.h) against the right edge, under the top's margin. */
void AddPursuitPanel(int width, float pixel, float margin, float padding, float line)
{
    char lines[nfs::pursuitpanel::kMaxLines][nfs::pursuitpanel::kLineLength];
    const int count = nfs::pursuitpanel::Text(lines);
    if (!count)
        return;
    int longest = 0;
    for (int i = 0; i < count; ++i)
        longest = lstrlenA(lines[i]) > longest ? lstrlenA(lines[i]) : longest;
    const float text_width = (kGlyphColumns + 1) * pixel * static_cast<float>(longest);
    const float right = static_cast<float>(width) - margin;
    const float left = right - padding - text_width;
    const float top = margin + 2.0f * line + 30.0f * pixel;
    AddQuad(left - padding, top, right, top + 2.0f * padding + count * line, kBackgroundColor);
    for (int i = 0; i < count; ++i)
        AddText(left, top + padding + i * line, pixel, lines[i]);
}

/** The dev tools' status (dev/dev.h: recording, playing) in a red box at the top's middle. */
void AddDevStatus(int width, float pixel, float margin, float padding)
{
    char status[96];
    if (!nfs::dev::Status(status, sizeof(status)))
        return;
    const float text_width = (kGlyphColumns + 1) * pixel * static_cast<float>(lstrlenA(status));
    const float left = (static_cast<float>(width) - text_width) * 0.5f;
    AddQuad(left - padding, margin, left + text_width + padding, margin + 2.0f * padding + kGlyphRows * pixel,
            kDevStatusColor);
    AddText(left, margin + padding, pixel, status);
}

/** Builds the overlay's quads for a back buffer `width` by `height` pixels: the dev tools' status, and the counter
 * when it is on. */
void BuildOverlay(int screen_width, int height)
{
    g_state.vertex_count = 0;
    const float pixel = static_cast<float>(height > kLinesPerPixel ? height / kLinesPerPixel : 1);
    const float margin = 6.0f * pixel;
    const float padding = 3.0f * pixel;
    AddDevStatus(screen_width, pixel, margin, padding);
    const float line = (kGlyphRows + 2) * pixel;
    AddProving(pixel, margin, padding, line);
    AddPursuitPanel(screen_width, pixel, margin, padding, line);
    if (!g_state.shown && !nfs::shadow::debug::Enabled())
        return;
    const float graph_height = 24.0f * pixel;
    const float graph_width = kHistory * pixel;
    const float left = margin + padding;
    const float top = margin + padding;

    char average[16], longest[16], line1[32], line2[48];
    FormatMs(average, g_state.average_ms);
    FormatMs(longest, g_state.longest_ms);
    wsprintfA(line1, "%d FPS  %s", g_state.fps, nfs::vk::g_renderer ? "VULKAN" : "D3D9");
    wsprintfA(line2, "%s MS  MAX %s", average, longest);

    // Background first (it is drawn in the order added).
    const int longest_line = lstrlenA(line1) > lstrlenA(line2) ? lstrlenA(line1) : lstrlenA(line2);
    const float text_width = (kGlyphColumns + 1) * pixel * static_cast<float>(longest_line);
    const float width = text_width > graph_width ? text_width : graph_width;
    AddQuad(margin, margin, left + width + padding, top + 2.0f * line + graph_height + padding, kBackgroundColor);

    AddText(left, top, pixel, line1);
    AddText(left, top + line, pixel, line2);

    // Graph: one bar per frame, newest on the right, scaled so the average sits at half height.
    const float graph_bottom = top + 2.0f * line + graph_height;
    const float scale_ms = g_state.average_ms > 0.0f ? g_state.average_ms * 2.0f : 1.0f;
    for (int i = 0; i < kHistory; ++i)
    {
        const float ms = g_state.history[(g_state.next + i) % kHistory];
        if (ms <= 0.0f)
            continue;
        const float fraction = ms < scale_ms ? ms / scale_ms : 1.0f;
        const DWORD color = ms > g_state.average_ms * kHitchFrame  ? kHitchFrameColor
                            : ms > g_state.average_ms * kSlowFrame ? kSlowFrameColor
                                                                    : kFrameColor;
        const float x = left + i * pixel;
        AddQuad(x, graph_bottom - fraction * graph_height, x + pixel, graph_bottom, color);
    }
    const float average_y = graph_bottom - graph_height * 0.5f;
    AddQuad(left, average_y, left + graph_width, average_y + 1.0f, kAverageLineColor);
    AddPose(screen_width, pixel, margin, padding, line);
}

void SetDrawingState(IDirect3DDevice9 *device)
{
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetFVF(kVertexFormat);
    device->SetTexture(0, nullptr);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

void DrawOnBackBuffer(IDirect3DDevice9 *device)
{
    IDirect3DSurface9 *back_buffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer)))
        return;
    D3DSURFACE_DESC desc;
    back_buffer->GetDesc(&desc);
    g_state.textured = false;
    BuildOverlay(static_cast<int>(desc.Width), static_cast<int>(desc.Height));
    if (!g_state.saved_state && FAILED(device->CreateStateBlock(D3DSBT_ALL, &g_state.saved_state)))
        g_state.saved_state = nullptr;
    if (g_state.saved_state && g_state.vertex_count)
    {
        g_state.saved_state->Capture();
        IDirect3DSurface9 *target = nullptr, *depth = nullptr;
        device->GetRenderTarget(0, &target);
        device->GetDepthStencilSurface(&depth);
        device->SetRenderTarget(0, back_buffer);
        device->SetDepthStencilSurface(nullptr);
        if (SUCCEEDED(device->BeginScene()))
        {
            SetDrawingState(device);
            device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, g_state.vertex_count / 3, g_state.vertices, sizeof(Vertex));
            device->EndScene();
        }
        device->SetRenderTarget(0, target);
        device->SetDepthStencilSurface(depth);
        g_state.saved_state->Apply();
        if (target)
            target->Release();
        if (depth)
            depth->Release();
    }
    back_buffer->Release();
}
const D3DVERTEXELEMENT9 kVertexElements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END(),
};

/** The font texture: kGlyphs drawn white on transparent, a cell each, and a lit cell after them. */
nfs::vk::Texture *MakeFont(nfs::vk::Renderer *r)
{
    nfs::vk::Texture *font = r->CreateTexture(kFontWidth, kFontHeight, 1, D3DFMT_A8R8G8B8, false);
    if (!font)
        return nullptr;
    static DWORD texels[kFontWidth * kFontHeight];
    for (DWORD &t : texels)
        t = 0x00FFFFFF;
    for (int i = 0; i < kGlyphCount; ++i)
        for (int row = 0; row < kGlyphRows; ++row)
            for (int column = 0; column < kGlyphColumns; ++column)
                if (kGlyphs[i].rows[row] & (0x10 >> column))
                    texels[row * kFontWidth + i * kCellWidth + column] = 0xFFFFFFFF;
    for (int row = 0; row < kCellHeight; ++row)
        for (int column = 0; column < kCellWidth; ++column)
            texels[row * kFontWidth + kGlyphCount * kCellWidth + column] = 0xFFFFFFFF;
    r->UploadTexture(font, 0, 0, texels, sizeof(texels));
    return font;
}

void DrawWithVulkan(nfs::vk::Renderer *r)
{
    nfs::vk::Texture *back_buffer = r->BackBuffer();
    if (!back_buffer)
        return;
    if (!g_state.vk_effect)
        g_state.vk_effect = nfs::vk::CreateEffect("NFS_post");
    ID3DXEffect *effect = g_state.vk_effect;
    if (!effect)
        return;
    if (!g_state.vk_layout)
        g_state.vk_layout = r->CreateVertexLayout(kVertexElements);
    if (!g_state.vk_font && !(g_state.vk_font = MakeFont(r)))
        return;
    g_state.textured = true;
    const unsigned int size = back_buffer->width << 16 | back_buffer->height;
    if (size != g_state.built_size ||
        (g_state.last.QuadPart - g_state.built.QuadPart) * 60 >= g_state.frequency.QuadPart)
    {
        BuildOverlay(static_cast<int>(back_buffer->width), static_cast<int>(back_buffer->height));
        g_state.built = g_state.last;
        g_state.built_size = size;
    }
    if (!g_state.vertex_count)
        return;
    nfs::vk::Texture *color = r->RenderTarget(), *depth = r->DepthTarget();
    const nfs::vk::Viewport viewport = r->GetViewport();
    r->SetRenderTarget(back_buffer, nullptr);
    r->SetViewport({0, 0, back_buffer->width, back_buffer->height, 0.0f, 1.0f});
    // Pixel (x, y) to clip space; the vertices already carry the half pixel offset of Direct3D 9's pixel centres.
    const float scale[4] = {2.0f / static_cast<float>(back_buffer->width),
                            -2.0f / static_cast<float>(back_buffer->height),
                            -1.0f + 1.0f / static_cast<float>(back_buffer->width),
                            1.0f - 1.0f / static_cast<float>(back_buffer->height)};
    effect->SetVector("OverlayScale", scale);
    effect->SetTexture("FontTex", reinterpret_cast<IDirect3DBaseTexture9 *>(g_state.vk_font));
    effect->SetTechnique("overlay_text");
    effect->Begin(nullptr, 0);
    effect->BeginPass(0);
    r->SetVertexLayout(static_cast<nfs::vk::VertexLayout *>(g_state.vk_layout));
    r->DrawUP(D3DPT_TRIANGLELIST, static_cast<uint32_t>(g_state.vertex_count / 3), g_state.vertices, sizeof(Vertex));
    effect->EndPass();
    effect->End();
    r->SetRenderTarget(color, depth);
    r->SetViewport(viewport);
}
} // namespace

bool Shown()
{
    Load();
    return g_state.shown;
}

void SetShown(bool shown)
{
    Load();
    if (shown != g_state.shown)
        Toggle();
}

void Draw()
{
    Load();
    const bool key_down = display::DebugKeyDown(VK_F11);
    if (key_down && !g_state.key_down)
        Toggle();
    g_state.key_down = key_down;

    CountFrame();
    char status[96];
    char panel[nfs::pursuitpanel::kMaxLines][nfs::pursuitpanel::kLineLength];
    if (!g_state.shown && !nfs::shadow::debug::Enabled() && !nfs::dev::Status(status, sizeof(status)) &&
        !nfs::proving::Enabled() && !nfs::pursuitpanel::Text(panel))
        return;
    if (nfs::vk::Renderer *r = nfs::vk::g_renderer)
        DrawWithVulkan(r);
    else if (g_D3DDevice)
        DrawOnBackBuffer(g_D3DDevice);
}

void ReleaseDeviceObjects()
{
    if (g_state.saved_state)
    {
        g_state.saved_state->Release();
        g_state.saved_state = nullptr;
    }
    if (nfs::vk::Renderer *r = nfs::vk::g_renderer)
    {
        if (g_state.vk_effect)
            g_state.vk_effect->Release();
        if (g_state.vk_layout)
            r->Release(static_cast<nfs::vk::Object *>(g_state.vk_layout));
        if (g_state.vk_font)
            r->ReleaseTexture(g_state.vk_font);
        g_state.vk_effect = nullptr;
        g_state.vk_layout = nullptr;
        g_state.vk_font = nullptr;
    }
}
} // namespace nfs::overlay
