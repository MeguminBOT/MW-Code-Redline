#pragma once

#include "engine/rendering/vulkan/effect.h"
#include "engine/rendering/vulkan/formats.h"
#include "engine/rendering/vulkan/pipeline.h"
#include "engine/rendering/vulkan/resources.h"

struct IDirect3DDevice9;

/**
 * The Vulkan renderer the ported zPlatform code draws with (docs/VULKAN.md). The game sets state one value at a time
 * with Direct3D 9 enums, as its code and data store them; the renderer keeps that state and turns it into pipelines,
 * dynamic state, pushed descriptors and draws. One instance, made when the game creates its window in Vulkan mode.
 */
namespace nfs::vk
{
/** True when speed.ini asks for the Vulkan renderer ([Display] Renderer = vulkan) or the command line has -vulkan.
 * Read once. */
bool Enabled();
/** True when the command line has `name` as one of its arguments. */
bool HasArgument(const char *name);

/**
 * Renderer objects the game keeps where it would keep Direct3D objects. The only methods its own code calls on them
 * are IUnknown's AddRef and Release (texture unloading, effect destructors); everything else goes through ported code.
 * So they start with an IUnknown-shaped header, and the last Release frees them.
 */
enum ObjectKind : uint32_t
{
    kObjectTexture,
    kObjectVertexLayout,
    kObjectGeometryBuffer,
    kObjectSurface,
    /** Written over a texture's kind as it is freed, so a pointer kept past its last Release shows up (PrepareDraw). */
    kObjectFreed = 0xDEADu,
};

struct Object
{
    const void *const *vtable;
    uint32_t refs;
    ObjectKind kind;
};

/** QueryInterface (no interfaces), AddRef and Release (Renderer::Release) of every renderer object. */
extern const void *const kObjectVtable[3];

/** A texture, render target or depth buffer: what the game's texture and surface pointers hold in Vulkan mode. */
struct Texture : Object
{
    Image image;
    FormatInfo format;
    uint32_t d3dFormat;
    uint32_t width;
    uint32_t height;
    uint32_t levels;
    /** Slices of a volume texture (1 otherwise). */
    uint32_t depth;
    bool cube;
    bool volume;
    bool renderTarget;
    VkImageLayout layout;
    /** Cube render targets: a view of each face to render into. */
    VkImageView faceViews[6];
};

/** A cube face as a render target (Direct3D 9's GetCubeMapSurface); holds a reference to its texture. A 2D texture's
 * top level is the texture itself. */
struct Surface : Object
{
    Texture *texture;
    uint32_t face;
};

/** A vertex declaration (D3DVERTEXELEMENT9 list) as Vulkan vertex attributes at the HLSL attribute locations. */
struct VertexLayout : Object
{
    uint32_t attributeCount;
    VertexAttribute attributes[kMaxVertexAttributes];
    uint32_t streamCount;
    /** Bytes per vertex of each stream as far as the declaration goes (the draw's stride is what counts). */
    uint32_t streamSizes[kMaxVertexBindings];
};

/** A vertex or index buffer of the game's geometry (solids, models), filled once when it is made. */
struct GeometryBuffer : Object
{
    Buffer buffer;
    uint32_t bytes;
    /** Buffers the game fills itself: the memory it writes, uploaded by UploadGeometryBuffer. */
    void *cpu;
    /** Static geometry lives in an arena (Renderer::Arena) instead of `buffer`: its index plus one (0: none) and the
     * byte offset there. A vertex buffer moves into one at its first draw; `moveTried` once it has been tried. */
    uint32_t arena;
    uint32_t arenaOffset;
    /** A vertex buffer's arenaOffset in vertices of the arena's stride (its offsets are multiples of it). */
    uint32_t arenaVertex;
    bool moveTried;
};

struct Viewport
{
    uint32_t x, y, width, height;
    float minZ, maxZ;
};

class Renderer : public EffectStateSink
{
  public:
    /** Without a window the renderer is headless: it draws into the back buffer but presents nothing (tests). */
    bool Initialize(HWND window, uint32_t width, uint32_t height);
    void Shutdown();
    /** New back buffer size (render scale applied); recreates the back buffer and, later, the swapchain. */
    bool Resize(uint32_t width, uint32_t height);
    void SetVSync(bool on);
    /** Writes the pipeline list and cache if they changed (when the game quits: see WritePipelineCache). */
    void FlushPipelineCache();

    const Device &GetDevice() const
    {
        return device_;
    }

    void BeginFrame();
    /** Ends the frame: the back buffer is scaled onto the swapchain image and presented. */
    void EndFrame();
    bool InFrame() const
    {
        return frameActive_;
    }
    /** Copies a 32-bit texture's top level into `out` (width * height texels). Ends the frame and waits for the GPU. */
    bool ReadPixels(Texture *texture, void *out);
    /** Saves this frame, as presented, to `name` (a .bmp next to the exe) when it ends, and the debug texture
     * (SetDebugTexture) to `debugName` if given. */
    void RequestCapture(const char *name, const char *debugName = nullptr);
    /** The scene set (set 2, docs/SHADOWS.md): constants for this frame's draws (at most kSceneConstantsBytes; the
     * last set before the frame ends is what every draw of the frame reads), and the sun's shadow atlas (a depth
     * texture; null for none, which reads as fully lit). */
    void SetSceneConstants(const void *data, uint32_t bytes);
    void SetSunShadowAtlas(Texture *atlas);
    /** The player's headlights' shadow map the scene set shows (a depth texture; null for none). */
    void SetHeadlightShadow(Texture *depth);
    /** The lamps' baked light the scene set shows (engine/rendering/lights/lamps.h): kLampVolumes volume textures, uploaded at least once;
     * null for none (black). */
    void SetLampVolumes(Texture *const *volumes);
    /** -gpucrumbs: logs the commands around the last one the GPU finished (after the device is lost). */
    void ReportCrumbs();
    /** -gpucrumbs: logs `what` with the frame and the last command serial, to place it among the commands. */
    void LogCrumbMark(const char *what);
    /** The draw's object-to-world transform (a D3D row-vector matrix, translation in the last row), given to the
     * rebuild's own vertex shaders with the draw (kDrawConstantsBinding). */
    void SetWorldMatrix(const float *matrix);
    /** The share of the next draws' pixels the rebuild's own shaders keep (scenery fading in or out, engine/rendering/culling/culling.h). */
    void SetDrawFade(float fade)
    {
        drawFade_ = fade;
    }
    /** Whether the next draws' particles take the hour's light (D3D9State::litParticles). */
    void SetDrawLitParticles(bool lit)
    {
        drawLitParticles_ = lit;
    }
    /** Draws a pass in two: 1 its depth alone (no colour writes; D3D9State::shadowQuality bit 1 tells the shaders that
     * know it to stop after their alpha test), then 2 its colour where the depth is the draw's own (depth test equal, no
     * depth writes), which shades each pixel once however many layers an alpha-tested pass has; 0 draws as asked. */
    void SetDepthPrepass(int mode)
    {
        if (depthPrepass_ != mode)
        {
            depthPrepass_ = mode;
            ++stateSerial_;
        }
    }
    /** True while the GPU holds the frames up: the CPU has been waiting for it at the start of its frames (averaged over
     * a second or so, with some hysteresis). Work that moves load from the GPU to the CPU is worth it then only. */
    bool GpuBound() const
    {
        return gpuBound_;
    }
    /** The next draws' texture's blend type (D3D9State::blend). */
    void SetDrawBlend(int blend)
    {
        drawBlend_ = blend;
    }
    /** The next draws' surface detail (D3D9State::surface). */
    void SetDrawSurface(uint32_t surface)
    {
        drawSurface_ = surface;
    }
    /** Debugging: notes the shaders of the draws made while on and logs them when turned off. */
    void SetShaderLog(bool on);
    void NoteShaders(int vertexShader, int pixelShader);
    /** Frame profiler timestamps (dev/profile.h): WriteTimestamp records one into this frame's commands once the work
     * before it has finished; LatestTimestamps gives the newest finished frame's in nanoseconds, with a bit per slot
     * written. False when the device has no timestamps. */
    static const uint32_t kMaxTimestamps = 32;
    void WriteTimestamp(uint32_t slot);
    bool LatestTimestamps(uint64_t (&nanoseconds)[kMaxTimestamps], uint32_t &written) const;
    /** -gpuprofile: GPU time by pixel shader and target (the back buffer, a cube face, another target), from timestamps
     * written where either changes; logged with the frame profiler (dev/profile.h), the costliest first. */
    void LogShaderTimes();
    static const uint32_t kReadbackBytes = 256;
    /** Queues a copy of a small texture's top level (at most kReadbackBytes) to memory the CPU can read once this
     * frame's GPU work is done; nothing waits for it. */
    void RequestReadback(Texture *texture);
    /** The newest finished readback (a few frames old); false until one has finished. */
    bool ReadbackResult(void *out, uint32_t bytes) const;

    // Resources
    Texture *CreateTexture(uint32_t width, uint32_t height, uint32_t levels, uint32_t d3dFormat, bool renderTarget,
                           bool cube = false);
    /** A volume texture (Direct3D 9's CreateVolumeTexture); its levels are uploaded whole with UploadTexture. */
    Texture *CreateVolumeTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t d3dFormat);
    /** Copies one mip level (or cube face) from memory laid out as Direct3D 9 locks it with an unpadded pitch. */
    void UploadTexture(Texture *texture, uint32_t level, uint32_t face, const void *data, uint32_t bytes);
    /** Levels 0 to `levels` - 1 of a 2D texture from `data`, where each level's data is a quarter of the one before
     * (`baseBytes` the first's), in one copy between one pair of barriers. */
    void UploadTextureLevels(Texture *texture, const void *data, uint32_t baseBytes, uint32_t levels);
    /** A box of a volume texture's first level and its texels (x fastest, then y, then z). */
    struct VolumeBox
    {
        uint32_t at[3];
        uint32_t extent[3];
        const void *data;
    };
    /** Copies boxes of a volume texture's first level, in one copy (up to 64); before the frame's draws. */
    void UploadVolumeBoxes(Texture *texture, const VolumeBox *boxes, uint32_t count);
    /** Drops a reference; the last one destroys the texture once the GPU is done with the frames that may use it. */
    void ReleaseTexture(Texture *texture);
    /** One level's texels in memory, read back from the GPU, for code that edits textures on the CPU (car skin and
     * wheel compositing); Direct3D 9's LockRect. UnlockLevel uploads the changes. Null when it cannot be done.
     * `overwrite`: the caller writes every texel, so nothing is read back. */
    void *LockLevel(Texture *texture, uint32_t level, bool overwrite = false);
    void UnlockLevel(Texture *texture, uint32_t level);
    /** The levels under the first of a 4-byte texture, box-filtered from `top` (the first level's texels). */
    void UploadBoxMips(Texture *texture, const uint8_t *top);
    /** A cube face to render into. */
    Surface *CreateSurface(Texture *texture, uint32_t face);
    /** The texture (and cube face) behind a target as the game stores it: a Texture or a Surface. */
    static Texture *Resolve(void *object, uint32_t *face = nullptr);
    /** StretchRect: copies a rectangle (all of it when null) of one target to another, scaled with D3DTEXF filtering. */
    void StretchRect(void *source, const RECT *sourceRect, void *target, const RECT *targetRect, uint32_t filter);
    /** Gives a render target (texture or cube face; cubes stay square) the size the game's target table asks for, in
     * place, so every reference stays valid. Mods change target sizes in the game's own code, which the table follows;
     * the back and depth buffers are left alone. */
    void FitRenderTarget(void *target, uint32_t width, uint32_t height);
    /** Copies one level (or cube face level) to another of the same size and format, on the GPU. */
    void CopyLevel(Texture *source, uint32_t sourceFace, uint32_t sourceLevel, Texture *target, uint32_t targetFace,
                   uint32_t targetLevel);
    /** Copies a rectangle of the first level to another texture of the same format, on the GPU, inside the frame. */
    void CopyRect(Texture *source, uint32_t sourceX, uint32_t sourceY, Texture *target, uint32_t targetX,
                  uint32_t targetY, uint32_t width, uint32_t height);
    Texture *BackBuffer()
    {
        return backBuffer_;
    }
    Texture *DepthBuffer()
    {
        return depthBuffer_;
    }
    /** A D3DVERTEXELEMENT9 list ending with D3DDECL_END. */
    VertexLayout *CreateVertexLayout(const void *elements);
    /** Drops a reference; the last one frees the layout. */
    void ReleaseVertexLayout(VertexLayout *layout);
    /** A device-local buffer with `bytes` of `data` (16-bit indices when `index`); the copy runs before the next frame. */
    GeometryBuffer *CreateGeometryBuffer(const void *data, uint32_t bytes, bool index);
    /** A static vertex buffer straight into the arena of its stride (a streamed solid's vertices), else as
     * CreateGeometryBuffer. */
    GeometryBuffer *CreateStaticVertexBuffer(const void *data, uint32_t bytes, uint32_t stride);
    /** Drops a reference; the last one destroys the buffer once the GPU is done with the frames that may use it. */
    void ReleaseGeometryBuffer(GeometryBuffer *buffer);
    /** A buffer with memory the game fills (cpu); UploadGeometryBuffer copies the first `bytes` of it to the GPU. */
    GeometryBuffer *CreateDynamicGeometryBuffer(uint32_t bytes, bool index);
    void UploadGeometryBuffer(GeometryBuffer *buffer, uint32_t bytes);
    /** -vkcapture also saves this depth texture once (vkdepth.bmp), for looking at shadow maps. */
    void SetDebugTexture(Texture *texture)
    {
        debugTexture_ = texture;
    }
    /** Release of any renderer object (the vtable's). */
    void Release(Object *object);

    // EffectStateSink: what effect passes set, and the same calls from ported code
    void SetRenderState(uint32_t state, uint32_t value) override;
    void SetSamplerState(uint32_t stage, uint32_t state, uint32_t value) override;
    void SetTexture(uint32_t stage, void *texture) override;
    void SetShaders(int vertexShader, int pixelShader) override;
    void SetConstants(bool pixel, uint32_t first, uint32_t count, const float (*registers)[4]) override;
    uint32_t ConstantsStamp(bool pixel) const override
    {
        return constantsStamp_[pixel];
    }
    void SetBoolConstants(bool pixel, uint32_t bits) override
    {
        (pixel ? psBools_ : vsBools_) = bits & 0xFFFF;
    }

    /** The vertex shader constant registers as set (debugging). */
    const float (*VertexConstants() const)[4]
    {
        return vsConstants_;
    }
    const float (*PixelConstants() const)[4]
    {
        return psConstants_;
    }
    uint32_t GetRenderState(uint32_t state) const
    {
        return state < 256 ? render_[state] : 0;
    }
    uint32_t GetSamplerState(uint32_t stage, uint32_t state) const
    {
        return stage < kMaxSamplers && state < 14 ? sampler_[stage][state] : 0;
    }
    void *GetTexture(uint32_t stage) const
    {
        return stage < kMaxSamplers ? textures_[stage] : nullptr;
    }
    int GetVertexShader() const
    {
        return vertexShader_;
    }
    int GetPixelShader() const
    {
        return pixelShader_;
    }

    void SetVertexLayout(const VertexLayout *layout);
    /** D3DFVF code of a fixed-function vertex format, for draws that use one instead of a declaration. */
    void SetFVF(uint32_t fvf);
    void SetRenderTarget(Texture *color, Texture *depth, uint32_t colorFace = 0);
    /** A target as the game stores it: a Texture or a Surface (a cube face), or null. */
    void SetRenderTarget(Object *color, Object *depth);
    Texture *RenderTarget() const
    {
        return colorTarget_;
    }
    /** The target that gives the drawing area its size: the colour target, or the depth target alone (a depth-only
     * target: nothing but its depth is written, as for shadow maps). */
    Texture *AreaTarget() const
    {
        return colorTarget_ ? colorTarget_ : depthTarget_;
    }
    Texture *DepthTarget() const
    {
        return depthTarget_;
    }
    void SetViewport(const Viewport &viewport);
    const Viewport &GetViewport() const
    {
        return viewport_;
    }
    void SetScissor(int left, int top, int right, int bottom);
    /** D3DCLEAR flags (1 target, 2 z, 4 stencil) and a D3DCOLOR; clears the viewport, as Direct3D 9 does. */
    void Clear(uint32_t flags, uint32_t color, float z, uint32_t stencil);

    /** DrawPrimitiveUP: D3DPRIMITIVETYPE, primitive count, vertices of `stride` bytes. Unbinds stream 0, as
     * Direct3D 9 does. */
    void DrawUP(uint32_t primitiveType, uint32_t primitiveCount, const void *vertices, uint32_t stride);
    /** SetStreamSource: the vertices of a stream (null: none), from `offset` bytes in, `stride` bytes apart. */
    void SetStreamSource(uint32_t stream, GeometryBuffer *buffer, uint32_t offset, uint32_t stride);
    /** SetIndices: 16-bit indices for DrawIndexed. */
    void SetIndices(GeometryBuffer *buffer);
    /** DrawIndexedPrimitive with vertices in memory (copied into the frame: those up to minIndex + vertexCount) and
     * `indices` (the particles' quads). */
    void DrawIndexedUP(uint32_t primitiveType, uint32_t minIndex, uint32_t vertexCount, uint32_t startIndex,
                       uint32_t primitiveCount, const void *vertices, uint32_t stride, GeometryBuffer *indices);
    /** DrawIndexedPrimitive from the bound streams and indices (`minIndex` and `vertexCount` are only hints). */
    void DrawIndexed(uint32_t primitiveType, int baseVertex, uint32_t minIndex, uint32_t vertexCount,
                     uint32_t startIndex, uint32_t primitiveCount);
    /** The rows of an instance block of the current vertex shader (its float registers, then the three world rows):
     * 0 when it does not read instance blocks (only the rebuild's own vertex shaders do, from one stream). */
    uint32_t InstanceBlockRows() const;
    /** Queues a draw of a block run (DrawBlockRun) from the bound stream and indices: one more instance of the draw
     * queued last when it is the same strip. At most kMaxBlockDraws draws. */
    void QueueBlockDraw(uint32_t startIndex, uint32_t primitiveCount);
    /** Draws the queued draws with the states as they are now, one block each per instance in queue order (`rows`
     * rows each, InstanceBlockRows: what the vertex shader's registers and the world rows would hold for it): the
     * blocks go to the ring at once, the pipeline and bindings are recorded once, and each draw finds its blocks
     * through its first instance (docs/NATIVE_WORLD.md). False, nothing drawn, when they do not fit. */
    bool DrawBlockRun(uint32_t primitiveType, const float (*blocks)[4], uint32_t rows);
    static const uint32_t kMaxBlockDraws = 64;
    /** The world rows as last set (SetWorldMatrix): x, y and z of the world position. */
    const float (*WorldRows() const)[4]
    {
        return world_;
    }

  private:
    struct Frame;
    struct PipelineEntry;
    struct SamplerEntry;
    struct Upload;

    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateBackBuffer(uint32_t width, uint32_t height);
    void DestroyBackBuffer();
    void ResetState();
    void DestroyStateObjects();
    /** `bytes` from this frame's ring; at least `span` bytes stay inside the ring from the returned offset (a
     * constants descriptor's range). */
    void *AllocateRing(uint32_t bytes, uint32_t alignment, VkBuffer &buffer, VkDeviceSize &offset, uint32_t span = 0);
    /** Room for `bytes` in the upload staging buffer, recording the upload commands; null if it cannot fit. */
    void *Stage(uint32_t bytes, VkDeviceSize &offset);
    void Retire(Object *object);
    void Transition(Texture *texture, VkImageLayout layout);
    void EndRendering();
    void RecordDynamicState(bool fresh);
    bool RecordBindings(const ShaderBlob &vs, const ShaderBlob &ps, nfs::vk::Texture *const *bound, uint32_t missing,
                        bool fresh);
    bool BeginRendering();
    /** Clear of the whole area as the load operation of a new rendering; false when the clear is not of that kind. */
    bool FullClear(uint32_t flags, uint32_t color, float z, uint32_t stencil);
    bool PrepareDraw(uint32_t primitiveType);
    /** Makes the pipeline description for the current state and looks up lastPipeline_ for it (null when there is
     * none); returns the vertex shader inputs the vertex layout lacks. */
    uint32_t MakePipelineDesc(uint32_t primitiveType, const ShaderBlob &vs);
    /** Forgets the pipelines found by key (pipelines destroyed, or a vertex layout freed whose address may come back). */
    void ClearPipelineKeys();
    /** The rest of PrepareDraw once the pipeline is bound: dynamic state, push constants, descriptors. */
    bool Record(uint32_t primitiveType, const ShaderBlob &vs, const ShaderBlob &ps, nfs::vk::Texture *const *bound,
                uint32_t missing);
    VkPipeline Pipeline(const PipelineDesc &desc);
    /** A new pipeline for `desc`: linked from pipeline libraries when the device has them, else compiled whole. */
    VkPipeline MakePipeline(const PipelineDesc &desc);
    VkPipeline ShaderLibrary(int shader);
    VkPipeline InterfaceLibrary(const PipelineDesc &desc, bool output);
    /** Pipeline libraries: the shader parts (index: kShaders), and the vertex input and fragment output parts by
     * their part of the pipeline description. */
    struct InterfaceEntry;
    VkPipeline *shaderLibraries_ = nullptr;
    InterfaceEntry *interfaceLibraries_ = nullptr;
    uint32_t interfaceLibraryCount_ = 0;
    /** Puts a made pipeline into the table; false when the table is full or already has the description. */
    bool InsertPipeline(const PipelineDesc &desc, VkPipeline pipeline);
    /** Before the first frame: with pipeline libraries, compiles every shader's library on several threads (draws
     * then only link). Without them, makes the pipelines this machine used before (vkpipelines.lst next to the exe,
     * written as new ones appear). */
    void PrewarmPipelines();
    void SavePipelineList();
    VkSampler Sampler(uint32_t stage, uint32_t levels);
    void FlushUploads();
    void FlushGeometryCopies();
    void WriteCapture();
    void DestroyTexture(Texture *texture);
    void DestroyObject(Object *object);
    void CollectGarbage(Frame &frame);

    Device device_ = {};
    PipelineLayout layout_ = {};
    /** The frames' constants sets (one per ring buffer). */
    VkDescriptorPool constantsPool_ = VK_NULL_HANDLE;
    /** The recording frame's constants set. */
    VkDescriptorSet frameConstants_ = VK_NULL_HANDLE;
    /** The recording frame's scene set and constants, and what the scene set shows. */
    VkDescriptorSet frameScene_ = VK_NULL_HANDLE;
    void *frameSceneConstants_ = nullptr;
    Texture *sunShadowAtlas_ = nullptr;
    Texture *headlightShadow_ = nullptr;
    VkSampler shadowCompareSampler_ = VK_NULL_HANDLE;
    VkSampler shadowPointSampler_ = VK_NULL_HANDLE;
    Texture *lampVolumes_[4] = {};
    /** Linear and wrapping: the lamp volumes are rings around the camera (engine/rendering/lights/lamps.h). */
    VkSampler lampSampler_ = VK_NULL_HANDLE;
    void CopyStaged(Texture *texture, const VkBufferImageCopy *copies, uint32_t count);
    float world_[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    float drawFade_ = 1.0f;
    bool drawLitParticles_ = false;
    int drawBlend_ = 0;
    uint32_t drawSurface_ = 0;
    int depthPrepass_ = 0;
    double gpuWaitAverage_ = 0.0;
    bool gpuBound_ = false;
    /** Debugging: the shader pairs drawn with while on (SetShaderLog). */
    bool shaderLog_ = false;
    int16_t shaderPairs_[64][2] = {};
    int shaderPairCount_ = 0;
    void UpdateSceneShadow();
    HWND window_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_ = {};
    VkImage swapchainImages_[8] = {};
    VkSemaphore presentReady_[8] = {};
    uint32_t swapchainImageCount_ = 0;
    bool vsync_ = true;
    bool swapchainDirty_ = false;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    uint32_t depthD3DFormat_ = 0;

    Texture *backBuffer_ = nullptr;
    Texture *depthBuffer_ = nullptr;

    Frame *frames_ = nullptr;
    uint32_t frameIndex_ = 0;
    uint64_t frameNumber_ = 0;
    bool frameActive_ = false;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;

    uint32_t render_[256] = {};
    uint32_t sampler_[kMaxSamplers][14] = {};
    Texture *textures_[kMaxSamplers] = {};
    int vertexShader_ = -1;
    int pixelShader_ = -1;
    float vsConstants_[256][4] = {};
    float psConstants_[224][4] = {};
    /** Changes to vsConstants_ and psConstants_, counted from 1 (EffectStateSink::ConstantsStamp). */
    uint32_t constantsStamp_[2] = {1, 1};
    bool vsDirty_ = true;
    bool psDirty_ = true;
    uint32_t vsBools_ = 0;
    uint32_t psBools_ = 0;
    const VertexLayout *vertexLayout_ = nullptr;
    VertexLayout fvfLayout_ = {};
    uint32_t fvf_ = 0;
    Texture *colorTarget_ = nullptr;
    Texture *depthTarget_ = nullptr;
    uint32_t colorFace_ = 0;
    GeometryBuffer *streams_[kMaxVertexBindings] = {};
    uint32_t streamOffsets_[kMaxVertexBindings] = {};
    uint32_t streamStrides_[kMaxVertexBindings] = {};
    GeometryBuffer *indices_ = nullptr;
    Viewport viewport_ = {};
    VkRect2D scissor_ = {};
    /** The area the current rendering may touch: the colour target, cut to the depth target if that is smaller. */
    uint32_t renderWidth_ = 0;
    uint32_t renderHeight_ = 0;

    bool rendering_ = false;
    /** A clear of the whole area that the next BeginRendering does as its load operation (Clear): bit 0 colour, bit 1
     * depth and stencil, and their values. */
    uint32_t pendingClear_ = 0;
    VkClearValue pendingColor_ = {};
    VkClearValue pendingDepth_ = {};
    VkPipeline boundPipeline_ = VK_NULL_HANDLE;
    /** What the frame's command buffer already holds (dynamic state, push constants, descriptors, vertex and index
     * buffers), so a draw records only what changed. Cleared when a command buffer begins. */
    struct Recorded
    {
        bool valid;
        VkViewport viewport;
        VkRect2D scissor;
        uint32_t dynamic[16];
        float blend[4];
        float depthBias[2];
        D3D9State push;
        VkDescriptorSet constantsSet;
        uint32_t constantOffsets[4];
        VkDescriptorSet sceneSet;
        VkSampler samplers[kMaxSamplers];
        VkImageView views[kMaxSamplers];
        VkBuffer vertexBuffers[kMaxVertexBindings];
        VkDeviceSize vertexOffsets[kMaxVertexBindings];
        VkDeviceSize vertexStrides[kMaxVertexBindings];
        uint32_t vertexBindings;
        VkBuffer indexBuffer;
        /** stateSerial_ when the viewport, scissor and dynamic states were last recorded. */
        uint32_t dynamicSerial;
    };
    Recorded recorded_ = {};
    /** Bumped whenever a render state, the viewport, the scissor, the targets, the shaders or the vertex layout change:
     * a draw that finds the serial of the last one skips its pipeline key and its dynamic states (most draws in a row
     * change only their matrices and textures). */
    uint32_t stateSerial_ = 1;
    /** stateSerial_ and the topology class when the pipeline key was last checked. */
    uint32_t pipelineSerial_ = 0;
    uint32_t pipelineTopology_ = ~0u;
    /** minUniformBufferOffsetAlignment, for the constants' ring allocations. */
    uint32_t constantsAlignment_ = 256;
    /** -gpucrumbs: what each recent command serial was (kind, frame and three details), and the buffer the GPU writes
     * the serials it started (word 0) and finished (word 1) into. */
    struct CrumbInfo
    {
        uint32_t serial, frame, kind, a, b, c;
    };
    Buffer crumbBuffer_ = {};
    CrumbInfo *crumbs_ = nullptr;
    uint32_t crumbSerial_ = 0;
    bool crumbsReported_ = false;
    void CrumbBegin(uint32_t kind, uint32_t a, uint32_t b, uint32_t c)
    {
        if (crumbBuffer_.buffer)
            WriteCrumb(kind, a, b, c);
    }
    void CrumbEnd()
    {
        if (crumbBuffer_.buffer)
            FinishCrumb();
    }
    void WriteCrumb(uint32_t kind, uint32_t a, uint32_t b, uint32_t c);
    void FinishCrumb();
    /** The last pipeline looked up, and what it was made from (shaders, vertex layout, topology class, blend and colour
     * write states, target formats) with the vertex shader inputs the layout lacked; a draw with the same skips the
     * lookup. */
    uint32_t lastPipelineKey_[16] = {};
    /** -vkstats: the bound pipeline's key, and why binds happened (what changed: shaders, layout, topology, blending,
     * colour writes, targets). */
    uint32_t boundKey_[16] = {};
    uint32_t bindReasons_[6] = {};
    /** -vkstats: pipeline binds by the pixel shaders before and after, logged the most frequent first. */
    void CountBind(int fromPixelShader, int toPixelShader);
    void LogBinds(uint32_t frames);
    uint32_t lastMissing_ = 0;
    VkPipeline lastPipeline_ = VK_NULL_HANDLE;
    /** Pipelines by key, a direct-mapped cache in front of the description hash table: the state changes between
     * draws mostly go back and forth among a few dozen pipelines. */
    struct PipelineKeyEntry
    {
        uint32_t key[16];
        VkPipeline pipeline;
        uint32_t missing;
    };
    static const uint32_t kPipelineKeySlots = 512;
    PipelineKeyEntry pipelineKeys_[kPipelineKeySlots] = {};
    /** The sampler state key each stage had last and the sampler it got. */
    uint32_t stageSamplerKeys_[kMaxSamplers][10] = {};
    VkSampler stageSamplers_[kMaxSamplers] = {};
    /** A bit per stage whose sampler states changed since its sampler was looked up. */
    uint32_t samplerDirty_ = ~0u;
    void BindVertexBuffers(uint32_t bindings, const VkBuffer *buffers, const VkDeviceSize *offsets,
                           const VkDeviceSize *strides);
    /** Large device-local buffers that hold many geometry buffers each, so consecutive draws of different solids keep
     * one bound buffer: index buffers in those of stride 0, vertex buffers in those of their stride, each at a multiple
     * of it (a draw reaches its vertices through vertexOffset). First fit from a list of free ranges. */
    struct Arena
    {
        Buffer buffer;
        uint32_t stride;
        uint32_t freeCount;
        uint32_t *free;
        /** Where the last allocation was found: the next search starts there (ArenaAllocate). */
        uint32_t rover;
    };
    static const uint32_t kMaxArenas = 64;
    static const uint32_t kArenaBytes = 32u << 20;
    static const uint32_t kArenaRanges = 8192;
    Arena arenas_[kMaxArenas] = {};
    uint32_t arenaCount_ = 0;
    bool ArenaAllocate(uint32_t stride, uint32_t bytes, uint32_t &arena, uint32_t &offset);
    void ArenaFree(uint32_t arena, uint32_t offset, uint32_t bytes);
    /** Moves a static vertex buffer into an arena of `stride`: a copy queued with the uploads (which run before the
     * frame's draws), its own buffer retired once the frames that drew from it are done. */
    void MoveToArena(GeometryBuffer *b, uint32_t stride);
    VkBuffer BufferOf(const GeometryBuffer *b) const
    {
        return b->arena ? arenas_[b->arena - 1].buffer.buffer : b->buffer.buffer;
    }
    void BindIndexBuffer(VkBuffer buffer);
    VkBuffer vsConstantsBuffer_ = VK_NULL_HANDLE;
    VkBuffer psConstantsBuffer_ = VK_NULL_HANDLE;
    VkDeviceSize vsConstantsOffset_ = 0;
    VkDeviceSize psConstantsOffset_ = 0;
    uint32_t vsConstantsCount_ = 0;
    uint32_t psConstantsCount_ = 0;
    /** The world rows as last written to the ring, and where (kDrawConstantsBinding); worldDirty_ when world_ changed
     * since. */
    bool worldDirty_ = true;
    VkBuffer worldBuffer_ = VK_NULL_HANDLE;
    VkDeviceSize worldOffset_ = 0;
    /** DrawBlockRun's blocks and count, while it draws. */
    const float (*instanceBlocks_)[4] = nullptr;
    uint32_t instanceCount_ = 1;
    struct BlockDraw
    {
        GeometryBuffer *stream;
        uint32_t offset;
        uint32_t stride;
        GeometryBuffer *indices;
        uint32_t startIndex;
        uint32_t primitiveCount;
        uint32_t instances;
    };
    BlockDraw blockDraws_[kMaxBlockDraws];
    uint32_t blockDrawCount_ = 0;

    PipelineEntry *pipelines_ = nullptr;
    uint32_t pipelineCapacity_ = 0;
    uint32_t pipelineCount_ = 0;
    /** The pipeline cache is kept in vkpipelines.bin next to the exe: loaded at start, written when the game quits
     * (FlushPipelineCache) if pipelines were compiled; the list of pipelines (vkpipelines.lst) once new ones stop
     * appearing for a moment. */
    uint32_t savedPipelineCount_ = 0;
    DWORD lastPipelineCreated_ = 0;
    bool cacheDirty_ = false;
    void LoadPipelineCache(VkPipelineCacheCreateInfo &info, void *&data);
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;

    SamplerEntry *samplers_ = nullptr;
    uint32_t samplerCount_ = 0;

    Texture *defaultTextures_[3] = {};
    /** The newest finished readback (RequestReadback). */
    unsigned char readback_[kReadbackBytes] = {};
    bool readbackValid_ = false;
    uint64_t latestTimestamps_[kMaxTimestamps] = {};
    uint32_t latestTimestampsWritten_ = 0;
    bool timestamps_ = false;
    /** -gpuprofile: the span being timed (pixel shader * 3 + target class), and what the finished frames' spans added
     * up to per key. */
    struct ShaderTime
    {
        double ns;
        uint32_t draws;
    };
    bool shaderTiming_ = false;
    int spanKey_ = -1;
    ShaderTime *shaderTimes_ = nullptr;
    uint32_t shaderTimeFrames_ = 0;
    void StartSpan(int key);
    void TimeDraw(int key);
    Upload *upload_ = nullptr;

    /** -vkcapture: the back buffer is saved every few seconds (vkshot_N.bmp), for tests that cannot see the window. */
    bool capture_ = false;
    /** RequestCapture's file name; empty when none is wanted. */
    char captureName_[64] = {};
    char debugDumpName_[64] = {};
    DWORD lastCapture_ = 0;
    uint32_t captureCount_ = 0;
    Buffer captureBuffer_ = {};
    Texture *debugTexture_ = nullptr;
    void DumpDebugTexture();

    struct Lock
    {
        Texture *texture;
        uint32_t level;
        Buffer buffer;
    };
    Lock locks_[16] = {};

    /** -vkstats: draws made and dropped (and why), logged every few seconds. */
    enum Stat
    {
        kStatDrawn,
        kStatNoShaders,
        kStatNoLayout,
        kStatNoTarget,
        kStatNoPipeline,
        kStatNoBuffers,
        kStatCount
    };
    bool stats_ = false;
    DWORD lastStats_ = 0;
    uint64_t statsFrame_ = 0;
    uint32_t statCounts_[kStatCount] = {};
    /** -vkstats: what the current frame did that can take long, logged with frames much slower than the average. */
    struct FrameCost
    {
        uint32_t pipelines;
        int64_t pipelineTicks;
        uint32_t uploadBytes;
        uint32_t textures;
        int64_t fenceTicks;
        int64_t acquireTicks;
        int64_t uploadWaitTicks;
        /** When the frame's recording began (BeginFrame): before it the game's update, after it the drawing. */
        int64_t beginTick;
    };
    FrameCost cost_ = {};
    int64_t lastFrameEnd_ = 0;
    double averageFrame_ = 0.0;
    uint32_t spikesLogged_ = 0;
    void LogSpike(int64_t now);
};

/** -vkstats: time spent per kind of work since the last stats line (QueryPerformanceCounter ticks). */
enum ProfileSlot
{
    kProfileEffectPass,
    kProfileEffectCommit,
    kProfilePrepareDraw,
    kProfileFrame,
    kProfileCount
};
extern int64_t g_profile[kProfileCount];
/** Set with -vkstats; without it the scopes cost nothing but the check. */
extern bool g_profileEnabled;

struct ProfileScope
{
    explicit ProfileScope(ProfileSlot slot) : slot(slot), start(0)
    {
        if (g_profileEnabled)
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
    }
    ~ProfileScope()
    {
        if (!g_profileEnabled)
            return;
        int64_t end;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
        g_profile[slot] += end - start;
    }
    ProfileSlot slot;
    int64_t start;
};

/** The renderer while the game runs in Vulkan mode; null otherwise. */
extern Renderer *g_renderer;

/** What g_D3DDevice points to in Vulkan mode: does nothing, and logs each unported Direct3D 9 call (bring-up aid). */
IDirect3DDevice9 *ReportingDevice();
} // namespace nfs::vk
