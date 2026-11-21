/**
 * ID3DXEffect over the native effect runtime (engine/rendering/vulkan/d3dx_effect.h). Handles are addresses inside a private block,
 * so they can be told apart from parameter names, which D3DX also accepts as handles.
 */
#include <windows.h>
#include <string.h>

#include "utils/simd.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::vk
{
namespace
{
char g_handles[0x10000];

enum : uint32_t
{
    kParameterHandle = 0x0001,
    kTechniqueHandle = 0x1000,
    kPassHandle = 0x2000,
    kAnnotationHandle = 0x3000,
    kElementHandle = 0x4000,
    kElementsPerParameter = 64,
};

struct EffectDesc
{
    const char *creator;
    UINT parameters;
    UINT techniques;
    UINT functions;
};

struct ParameterDesc
{
    const char *name;
    const char *semantic;
    UINT cls;
    UINT type;
    UINT rows;
    UINT columns;
    UINT elements;
    UINT annotations;
    UINT structMembers;
    DWORD flags;
    UINT bytes;
};

struct TechniqueDesc
{
    const char *name;
    UINT passes;
    UINT annotations;
};

struct PassDesc
{
    const char *name;
    UINT annotations;
    const DWORD *vertexShader;
    const DWORD *pixelShader;
};

uint32_t Code(D3DXHANDLE handle)
{
    uintptr_t p = reinterpret_cast<uintptr_t>(handle), base = reinterpret_cast<uintptr_t>(g_handles);
    return p >= base && p < base + sizeof(g_handles) ? static_cast<uint32_t>(p - base) : 0;
}

D3DXHANDLE Handle(uint32_t code)
{
    return g_handles + code;
}

class NullSink : public EffectStateSink
{
  public:
    void SetRenderState(uint32_t, uint32_t) override
    {
    }
    void SetSamplerState(uint32_t, uint32_t, uint32_t) override
    {
    }
    void SetTexture(uint32_t, void *) override
    {
    }
    void SetShaders(int, int) override
    {
    }
    void SetConstants(bool, uint32_t, uint32_t, const float (*)[4]) override
    {
    }
};

NullSink g_nullSink;

EffectStateSink &Sink()
{
    if (g_renderer)
        return *g_renderer;
    return g_nullSink;
}

const uint32_t kMaxSaved = 128;

/** What Begin saves so End can put back the state the technique changes (D3DX does this unless told not to). */
struct SavedState
{
    uint32_t renderIds[kMaxSaved];
    uint32_t renderValues[kMaxSaved];
    uint32_t renderCount;
    uint8_t samplerStages[kMaxSaved];
    uint8_t samplerIds[kMaxSaved];
    uint32_t samplerValues[kMaxSaved];
    uint32_t samplerCount;
    uint32_t textureMask;
    void *textures[kMaxSamplers];
    bool shaders;
    int vertexShader;
    int pixelShader;
};

class Adapter : public ID3DXEffect
{
  public:
    int ParameterIndex(D3DXHANDLE handle) const
    {
        int element;
        const int p = ParameterOf(handle, element);
        return element < 0 ? p : -1;
    }
    explicit Adapter(const EffectInfo &info) : effect_(info), refs_(1), saving_(false), plans_(nullptr)
    {
        memset(&saved_, 0, sizeof(saved_));
    }
    ~Adapter()
    {
        if (!plans_)
            return;
        for (int t = 0; t < effect_.TechniqueCount(); ++t)
            ::operator delete(plans_[t]);
        ::operator delete(plans_);
    }

    Effect effect_;

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID, void **object) override
    {
        if (object)
            *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override
    {
        return ++refs_;
    }
    ULONG __stdcall Release() override
    {
        ULONG refs = --refs_;
        if (!refs)
            delete this;
        return refs;
    }

    // ID3DXBaseEffect
    HRESULT __stdcall GetDesc(void *desc) override
    {
        EffectDesc *d = static_cast<EffectDesc *>(desc);
        d->creator = "nfs::vk::Effect";
        d->parameters = effect_.Info().parameterCount;
        d->techniques = effect_.Info().techniqueCount;
        d->functions = 0;
        return D3D_OK;
    }
    HRESULT __stdcall GetParameterDesc(D3DXHANDLE handle, void *desc) override
    {
        ParameterDesc *d = static_cast<ParameterDesc *>(desc);
        memset(d, 0, sizeof(*d));
        uint32_t code = Code(handle);
        if (code >= kAnnotationHandle && code < kElementHandle)
        {
            const EffectAnnotation &a = effect_.Info().annotations[code - kAnnotationHandle];
            d->name = a.name;
            d->type = a.type;
            d->cls = a.type == kParamString ? kClassObject : kClassScalar;
            d->rows = d->columns = 1;
            d->bytes = 4;
            return D3D_OK;
        }
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        const EffectParameter &e = effect_.Info().parameters[p];
        d->name = e.name;
        d->semantic = e.semantic;
        d->cls = e.cls;
        d->type = e.type;
        d->rows = e.rows;
        d->columns = e.columns;
        d->elements = element >= 0 ? 0 : e.elements;
        d->annotations = e.annotationCount;
        uint32_t elementBytes = e.rows * e.columns * 4;
        d->bytes = e.valueCount ? (element >= 0 ? elementBytes : e.valueCount * 4u) : 4u;
        return D3D_OK;
    }
    HRESULT __stdcall GetTechniqueDesc(D3DXHANDLE handle, void *desc) override
    {
        int t = TechniqueOf(handle);
        if (t < 0)
            return E_FAIL;
        TechniqueDesc *d = static_cast<TechniqueDesc *>(desc);
        const EffectTechnique &e = effect_.Info().techniques[t];
        d->name = e.name;
        d->passes = e.passCount;
        d->annotations = e.annotationCount;
        return D3D_OK;
    }
    HRESULT __stdcall GetPassDesc(D3DXHANDLE handle, void *desc) override
    {
        uint32_t code = Code(handle);
        if (code < kPassHandle || code >= kAnnotationHandle)
            return E_FAIL;
        const EffectPass &e = effect_.Info().passes[code - kPassHandle];
        PassDesc *d = static_cast<PassDesc *>(desc);
        d->name = e.name;
        d->annotations = 0;
        d->vertexShader = e.vertexShader >= 0 ? reinterpret_cast<const DWORD *>(kShaders[e.vertexShader].dxso) : nullptr;
        d->pixelShader = e.pixelShader >= 0 ? reinterpret_cast<const DWORD *>(kShaders[e.pixelShader].dxso) : nullptr;
        return D3D_OK;
    }
    HRESULT __stdcall GetFunctionDesc(D3DXHANDLE, void *) override
    {
        return E_NOTIMPL;
    }
    D3DXHANDLE __stdcall GetParameter(D3DXHANDLE parent, UINT index) override
    {
        if (!parent)
            return index < effect_.Info().parameterCount ? Handle(kParameterHandle + index) : nullptr;
        return GetParameterElement(parent, index);
    }
    D3DXHANDLE __stdcall GetParameterByName(D3DXHANDLE parent, const char *name) override
    {
        int p = parent ? -1 : effect_.Parameter(name);
        return p >= 0 ? Handle(kParameterHandle + p) : nullptr;
    }
    D3DXHANDLE __stdcall GetParameterBySemantic(D3DXHANDLE parent, const char *semantic) override
    {
        int p = parent ? -1 : effect_.ParameterBySemantic(semantic);
        return p >= 0 ? Handle(kParameterHandle + p) : nullptr;
    }
    D3DXHANDLE __stdcall GetParameterElement(D3DXHANDLE parameter, UINT index) override
    {
        int element;
        int p = ParameterOf(parameter, element);
        if (p < 0 || element >= 0 || index >= effect_.Info().parameters[p].elements || index >= kElementsPerParameter)
            return nullptr;
        return Handle(kElementHandle + p * kElementsPerParameter + index);
    }
    D3DXHANDLE __stdcall GetTechnique(UINT index) override
    {
        return index < effect_.Info().techniqueCount ? Handle(kTechniqueHandle + index) : nullptr;
    }
    D3DXHANDLE __stdcall GetTechniqueByName(const char *name) override
    {
        int t = effect_.Technique(name);
        return t >= 0 ? Handle(kTechniqueHandle + t) : nullptr;
    }
    D3DXHANDLE __stdcall GetPass(D3DXHANDLE technique, UINT index) override
    {
        int t = TechniqueOf(technique);
        if (t < 0 || index >= effect_.Info().techniques[t].passCount)
            return nullptr;
        return Handle(kPassHandle + effect_.Info().techniques[t].passStart + index);
    }
    D3DXHANDLE __stdcall GetPassByName(D3DXHANDLE technique, const char *name) override
    {
        int t = TechniqueOf(technique);
        if (t < 0)
            return nullptr;
        const EffectTechnique &e = effect_.Info().techniques[t];
        for (UINT i = 0; i < e.passCount; ++i)
            if (lstrcmpA(effect_.Info().passes[e.passStart + i].name, name) == 0)
                return Handle(kPassHandle + e.passStart + i);
        return nullptr;
    }
    D3DXHANDLE __stdcall GetFunction(UINT) override
    {
        return nullptr;
    }
    D3DXHANDLE __stdcall GetFunctionByName(const char *) override
    {
        return nullptr;
    }
    D3DXHANDLE __stdcall GetAnnotation(D3DXHANDLE object, UINT index) override
    {
        uint16_t start, count;
        if (!AnnotationsOf(object, start, count) || index >= count)
            return nullptr;
        return Handle(kAnnotationHandle + start + index);
    }
    D3DXHANDLE __stdcall GetAnnotationByName(D3DXHANDLE object, const char *name) override
    {
        uint16_t start, count;
        if (!AnnotationsOf(object, start, count))
            return nullptr;
        for (uint16_t i = 0; i < count; ++i)
            if (lstrcmpA(effect_.Info().annotations[start + i].name, name) == 0)
                return Handle(kAnnotationHandle + start + i);
        return nullptr;
    }
    HRESULT __stdcall SetValue(D3DXHANDLE handle, const void *data, UINT bytes) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        if (element < 0)
        {
            effect_.SetValue(p, data, bytes);
            return D3D_OK;
        }
        uint32_t *values = ElementValues(p, element);
        uint32_t size = ElementSize(p) * 4;
        if (values && nfs::StoreIfChanged(values, data, bytes < size ? bytes : size))
            effect_.Touch(p);
        return D3D_OK;
    }
    HRESULT __stdcall GetValue(D3DXHANDLE handle, void *data, UINT bytes) override
    {
        uint32_t code = Code(handle);
        if (code >= kAnnotationHandle && code < kElementHandle)
        {
            const EffectAnnotation &a = effect_.Info().annotations[code - kAnnotationHandle];
            if (a.type == kParamString)
                memcpy(data, &a.string, bytes < 4 ? bytes : 4);
            else
                memcpy(data, &a.value, bytes < 4 ? bytes : 4);
            return D3D_OK;
        }
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        const EffectParameter &e = effect_.Info().parameters[p];
        if (!e.valueCount)
        {
            void *texture = effect_.GetTexture(p);
            memcpy(data, &texture, bytes < sizeof(texture) ? bytes : sizeof(texture));
            return D3D_OK;
        }
        uint32_t *values = element >= 0 ? ElementValues(p, element) : effect_.Values(p);
        uint32_t size = (element >= 0 ? ElementSize(p) : e.valueCount) * 4;
        memcpy(data, values, bytes < size ? bytes : size);
        return D3D_OK;
    }
    HRESULT __stdcall SetBool(D3DXHANDLE handle, BOOL value) override
    {
        return SetScalar(handle, value ? 1u : 0u, kParamBool);
    }
    HRESULT __stdcall GetBool(D3DXHANDLE handle, BOOL *value) override
    {
        *value = GetScalar(handle, kParamBool) != 0;
        return D3D_OK;
    }
    HRESULT __stdcall SetBoolArray(D3DXHANDLE handle, const BOOL *values, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        bool changed = false;
        for (UINT i = 0; i < count; ++i)
            changed |= WriteNumber(p, element, i, values[i] ? 1u : 0u, kParamBool);
        if (changed)
            effect_.Touch(p);
        return D3D_OK;
    }
    HRESULT __stdcall GetBoolArray(D3DXHANDLE handle, BOOL *values, UINT count) override
    {
        return ReadArray(handle, values, count, kParamBool);
    }
    HRESULT __stdcall SetInt(D3DXHANDLE handle, INT value) override
    {
        return SetScalar(handle, static_cast<uint32_t>(value), kParamInt);
    }
    HRESULT __stdcall GetInt(D3DXHANDLE handle, INT *value) override
    {
        *value = static_cast<INT>(GetScalar(handle, kParamInt));
        return D3D_OK;
    }
    HRESULT __stdcall SetIntArray(D3DXHANDLE handle, const INT *values, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        bool changed = false;
        for (UINT i = 0; i < count; ++i)
            changed |= WriteNumber(p, element, i, static_cast<uint32_t>(values[i]), kParamInt);
        if (changed)
            effect_.Touch(p);
        return D3D_OK;
    }
    HRESULT __stdcall GetIntArray(D3DXHANDLE handle, INT *values, UINT count) override
    {
        return ReadArray(handle, values, count, kParamInt);
    }
    HRESULT __stdcall SetFloat(D3DXHANDLE handle, FLOAT value) override
    {
        uint32_t word;
        memcpy(&word, &value, 4);
        return SetScalar(handle, word, kParamFloat);
    }
    HRESULT __stdcall GetFloat(D3DXHANDLE handle, FLOAT *value) override
    {
        uint32_t word = GetScalar(handle, kParamFloat);
        memcpy(value, &word, 4);
        return D3D_OK;
    }
    HRESULT __stdcall SetFloatArray(D3DXHANDLE handle, const FLOAT *values, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        bool changed = false;
        for (UINT i = 0; i < count; ++i)
        {
            uint32_t word;
            memcpy(&word, &values[i], 4);
            changed |= WriteNumber(p, element, i, word, kParamFloat);
        }
        if (changed)
            effect_.Touch(p);
        return D3D_OK;
    }
    HRESULT __stdcall GetFloatArray(D3DXHANDLE handle, FLOAT *values, UINT count) override
    {
        return ReadArray(handle, values, count, kParamFloat);
    }
    HRESULT __stdcall SetVector(D3DXHANDLE handle, const void *vector) override
    {
        return SetVectorArray(handle, vector, 1);
    }
    HRESULT __stdcall GetVector(D3DXHANDLE handle, void *vector) override
    {
        return GetVectorArray(handle, vector, 1);
    }
    HRESULT __stdcall SetVectorArray(D3DXHANDLE handle, const void *vectors, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        if (element < 0)
        {
            effect_.SetVectorArray(p, static_cast<const float *>(vectors), count);
            return D3D_OK;
        }
        const EffectParameter &e = effect_.Info().parameters[p];
        const float *v = static_cast<const float *>(vectors);
        bool changed = false;
        for (UINT c = 0; c < e.columns && c < 4; ++c)
        {
            uint32_t word;
            memcpy(&word, &v[c], 4);
            changed |= WriteNumber(p, element, c, word, kParamFloat);
        }
        if (changed)
            effect_.Touch(p);
        return D3D_OK;
    }
    HRESULT __stdcall GetVectorArray(D3DXHANDLE handle, void *vectors, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        const EffectParameter &e = effect_.Info().parameters[p];
        float *out = static_cast<float *>(vectors);
        memset(out, 0, count * 16);
        uint32_t *values = element >= 0 ? ElementValues(p, element) : effect_.Values(p);
        uint32_t rows = element >= 0 ? e.rows : (e.elements ? e.elements : 1) * e.rows;
        for (UINT v = 0; v < count && v < rows && values; ++v)
            for (UINT c = 0; c < e.columns && c < 4; ++c)
                out[v * 4 + c] = ToFloat(values[v * e.columns + c], e.type);
        return D3D_OK;
    }
    HRESULT __stdcall SetMatrix(D3DXHANDLE handle, const void *matrix) override
    {
        return SetMatrixArray(handle, matrix, 1);
    }
    HRESULT __stdcall GetMatrix(D3DXHANDLE handle, void *matrix) override
    {
        return GetMatrixArray(handle, matrix, 1);
    }
    HRESULT __stdcall SetMatrixArray(D3DXHANDLE handle, const void *matrices, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        if (element < 0)
        {
            effect_.SetMatrixArray(p, static_cast<const float *>(matrices), count);
            return D3D_OK;
        }
        WriteMatrix(p, element, static_cast<const float *>(matrices), false);
        return D3D_OK;
    }
    HRESULT __stdcall GetMatrixArray(D3DXHANDLE handle, void *matrices, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        const EffectParameter &e = effect_.Info().parameters[p];
        float *out = static_cast<float *>(matrices);
        memset(out, 0, count * 64);
        uint32_t first = element >= 0 ? static_cast<uint32_t>(element) : 0;
        uint32_t elements = e.elements ? e.elements : 1;
        for (UINT m = 0; m < count && first + m < elements; ++m)
        {
            uint32_t *values = ElementValues(p, static_cast<int>(first + m));
            for (UINT r = 0; r < e.rows && r < 4; ++r)
                for (UINT c = 0; c < e.columns && c < 4; ++c)
                    out[m * 16 + r * 4 + c] = ToFloat(values[r * e.columns + c], e.type);
        }
        return D3D_OK;
    }
    HRESULT __stdcall SetMatrixPointerArray(D3DXHANDLE handle, const void **matrices, UINT count) override
    {
        for (UINT i = 0; i < count; ++i)
        {
            HRESULT hr = SetMatrix(GetParameterElement(handle, i), matrices[i]);
            if (FAILED(hr))
                return hr;
        }
        return D3D_OK;
    }
    HRESULT __stdcall GetMatrixPointerArray(D3DXHANDLE, void **, UINT) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall SetMatrixTranspose(D3DXHANDLE handle, const void *matrix) override
    {
        return SetMatrixTransposeArray(handle, matrix, 1);
    }
    HRESULT __stdcall GetMatrixTranspose(D3DXHANDLE, void *) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall SetMatrixTransposeArray(D3DXHANDLE handle, const void *matrices, UINT count) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        const float *m = static_cast<const float *>(matrices);
        uint32_t first = element >= 0 ? static_cast<uint32_t>(element) : 0;
        uint32_t elements = effect_.Info().parameters[p].elements ? effect_.Info().parameters[p].elements : 1;
        for (UINT i = 0; i < count && first + i < elements; ++i)
            WriteMatrix(p, static_cast<int>(first + i), m + i * 16, true);
        return D3D_OK;
    }
    HRESULT __stdcall GetMatrixTransposeArray(D3DXHANDLE, void *, UINT) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall SetMatrixTransposePointerArray(D3DXHANDLE, const void **, UINT) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall GetMatrixTransposePointerArray(D3DXHANDLE, void **, UINT) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall SetString(D3DXHANDLE, const char *) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall GetString(D3DXHANDLE handle, const char **string) override
    {
        uint32_t code = Code(handle);
        if (code >= kAnnotationHandle && code < kElementHandle)
        {
            *string = effect_.Info().annotations[code - kAnnotationHandle].string;
            return *string ? D3D_OK : E_FAIL;
        }
        return E_FAIL;
    }
    HRESULT __stdcall SetTexture(D3DXHANDLE handle, IDirect3DBaseTexture9 *texture) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        effect_.SetTexture(p, texture);
        return D3D_OK;
    }
    HRESULT __stdcall GetTexture(D3DXHANDLE handle, IDirect3DBaseTexture9 **texture) override
    {
        int element;
        int p = ParameterOf(handle, element);
        *texture = p >= 0 ? static_cast<IDirect3DBaseTexture9 *>(effect_.GetTexture(p)) : nullptr;
        return p >= 0 ? D3D_OK : E_FAIL;
    }
    HRESULT __stdcall GetPixelShader(D3DXHANDLE, IDirect3DPixelShader9 **shader) override
    {
        *shader = nullptr;
        return E_FAIL;
    }
    HRESULT __stdcall GetVertexShader(D3DXHANDLE, IDirect3DVertexShader9 **shader) override
    {
        *shader = nullptr;
        return E_FAIL;
    }
    HRESULT __stdcall SetArrayRange(D3DXHANDLE, UINT, UINT) override
    {
        return D3D_OK;
    }

    // ID3DXEffect
    HRESULT __stdcall GetPool(ID3DXEffectPool **pool) override
    {
        *pool = nullptr;
        return E_FAIL;
    }
    HRESULT __stdcall SetTechnique(D3DXHANDLE technique) override
    {
        int t = TechniqueOf(technique);
        if (t < 0)
            return E_FAIL;
        effect_.SetTechnique(t);
        return D3D_OK;
    }
    D3DXHANDLE __stdcall GetCurrentTechnique() override
    {
        return Handle(kTechniqueHandle + effect_.CurrentTechnique());
    }
    HRESULT __stdcall ValidateTechnique(D3DXHANDLE technique) override
    {
        int t = TechniqueOf(technique);
        return t >= 0 && Shaded(t) ? D3D_OK : E_FAIL;
    }
    HRESULT __stdcall FindNextValidTechnique(D3DXHANDLE technique, D3DXHANDLE *next) override
    {
        int t = technique ? TechniqueOf(technique) + 1 : 0;
        for (; t >= 0 && t < effect_.TechniqueCount(); ++t)
        {
            if (Shaded(t))
            {
                *next = Handle(kTechniqueHandle + t);
                return D3D_OK;
            }
        }
        *next = nullptr;
        return D3D_OK;
    }
    BOOL __stdcall IsParameterUsed(D3DXHANDLE, D3DXHANDLE) override
    {
        return TRUE;
    }
    HRESULT __stdcall Begin(UINT *passes, DWORD flags) override
    {
        saving_ = (flags & D3DXFX_DONOTSAVESTATE) == 0 && g_renderer;
        if (saving_)
            Save(flags);
        UINT count = effect_.Begin();
        if (passes)
            *passes = count;
        return D3D_OK;
    }
    HRESULT __stdcall BeginPass(UINT pass) override
    {
        ProfileScope profile(kProfileEffectPass);
        effect_.BeginPass(pass, Sink());
        return D3D_OK;
    }
    HRESULT __stdcall CommitChanges() override
    {
        ProfileScope profile(kProfileEffectCommit);
        effect_.CommitChanges(Sink());
        return D3D_OK;
    }
    HRESULT __stdcall EndPass() override
    {
        effect_.EndPass();
        return D3D_OK;
    }
    HRESULT __stdcall End() override
    {
        effect_.End();
        if (saving_ && g_renderer)
            Restore();
        saving_ = false;
        return D3D_OK;
    }
    HRESULT __stdcall GetDevice(IDirect3DDevice9 **device) override
    {
        *device = nullptr;
        return E_FAIL;
    }
    HRESULT __stdcall OnLostDevice() override
    {
        return D3D_OK;
    }
    HRESULT __stdcall OnResetDevice() override
    {
        return D3D_OK;
    }
    HRESULT __stdcall SetStateManager(ID3DXEffectStateManager *) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall GetStateManager(ID3DXEffectStateManager **manager) override
    {
        *manager = nullptr;
        return E_NOTIMPL;
    }
    HRESULT __stdcall BeginParameterBlock() override
    {
        return E_NOTIMPL;
    }
    D3DXHANDLE __stdcall EndParameterBlock() override
    {
        return nullptr;
    }
    HRESULT __stdcall ApplyParameterBlock(D3DXHANDLE) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall DeleteParameterBlock(D3DXHANDLE) override
    {
        return E_NOTIMPL;
    }
    HRESULT __stdcall CloneEffect(IDirect3DDevice9 *, ID3DXEffect **effect) override
    {
        *effect = nullptr;
        return E_NOTIMPL;
    }
    HRESULT __stdcall SetRawValue(D3DXHANDLE handle, const void *data, UINT offset, UINT bytes) override
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0 || !effect_.Values(p))
            return E_FAIL;
        uint32_t size = effect_.Info().parameters[p].valueCount * 4;
        if (offset >= size)
            return E_FAIL;
        memcpy(reinterpret_cast<char *>(effect_.Values(p)) + offset, data, bytes < size - offset ? bytes : size - offset);
        effect_.Touch(p);
        return D3D_OK;
    }

  private:
    int ParameterOf(D3DXHANDLE handle, int &element) const
    {
        element = -1;
        if (!handle)
            return -1;
        uint32_t code = Code(handle);
        if (code >= kParameterHandle && code < kTechniqueHandle)
            return static_cast<int>(code - kParameterHandle) < effect_.Info().parameterCount
                       ? static_cast<int>(code - kParameterHandle)
                       : -1;
        if (code >= kElementHandle)
        {
            element = static_cast<int>((code - kElementHandle) % kElementsPerParameter);
            return static_cast<int>((code - kElementHandle) / kElementsPerParameter);
        }
        if (code)
            return -1;
        return effect_.Parameter(handle);
    }

    int TechniqueOf(D3DXHANDLE handle) const
    {
        if (!handle)
            return -1;
        uint32_t code = Code(handle);
        if (code >= kTechniqueHandle && code < kPassHandle)
            return static_cast<int>(code - kTechniqueHandle);
        if (code)
            return -1;
        return effect_.Technique(handle);
    }

    bool AnnotationsOf(D3DXHANDLE object, uint16_t &start, uint16_t &count) const
    {
        uint32_t code = Code(object);
        if (code >= kTechniqueHandle && code < kPassHandle)
        {
            const EffectTechnique &t = effect_.Info().techniques[code - kTechniqueHandle];
            start = t.annotationStart;
            count = t.annotationCount;
            return true;
        }
        int element;
        int p = ParameterOf(object, element);
        if (p < 0)
            return false;
        start = effect_.Info().parameters[p].annotationStart;
        count = effect_.Info().parameters[p].annotationCount;
        return true;
    }

    bool Shaded(int technique) const
    {
        const EffectTechnique &t = effect_.Info().techniques[technique];
        for (uint16_t i = 0; i < t.passCount; ++i)
        {
            const EffectPass &p = effect_.Info().passes[t.passStart + i];
            if (p.vertexShader < 0 || p.pixelShader < 0)
                return false;
        }
        return true;
    }

    uint32_t ElementSize(int p) const
    {
        const EffectParameter &e = effect_.Info().parameters[p];
        return e.rows * e.columns;
    }

    uint32_t *ElementValues(int p, int element)
    {
        uint32_t *values = effect_.Values(p);
        return values ? values + static_cast<uint32_t>(element) * ElementSize(p) : nullptr;
    }

    static float ToFloat(uint32_t word, uint8_t type)
    {
        float f;
        if (type == kParamFloat)
            memcpy(&f, &word, 4);
        else if (type == kParamInt)
            f = static_cast<float>(static_cast<int>(word));
        else
            f = word ? 1.0f : 0.0f;
        return f;
    }

    static uint32_t Convert(uint32_t word, uint8_t from, uint8_t to)
    {
        if (from == to)
            return word;
        float f;
        memcpy(&f, &word, 4);
        switch (to)
        {
        case kParamFloat: {
            float out = from == kParamInt ? static_cast<float>(static_cast<int>(word)) : (word ? 1.0f : 0.0f);
            uint32_t bits;
            memcpy(&bits, &out, 4);
            return bits;
        }
        case kParamBool:
            return from == kParamFloat ? (f != 0.0f ? 1u : 0u) : (word ? 1u : 0u);
        case kParamInt:
            return from == kParamFloat ? static_cast<uint32_t>(static_cast<int>(f)) : word;
        default:
            return word;
        }
    }

    /** Value `index` (DWORD) of the parameter, or of one of its elements; true when that changed it (the caller then
     * touches the parameter, once for all it writes). */
    bool WriteNumber(int p, int element, uint32_t index, uint32_t word, uint8_t type)
    {
        const EffectParameter &e = effect_.Info().parameters[p];
        uint32_t *values = element >= 0 ? ElementValues(p, element) : effect_.Values(p);
        uint32_t limit = element >= 0 ? ElementSize(p) : e.valueCount;
        if (!values || index >= limit)
            return false;
        const uint32_t value = Convert(word, type, e.type);
        if (values[index] == value)
            return false;
        values[index] = value;
        return true;
    }

    HRESULT SetScalar(D3DXHANDLE handle, uint32_t word, uint8_t type)
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        if (WriteNumber(p, element, 0, word, type))
            effect_.Touch(p);
        return D3D_OK;
    }

    uint32_t GetScalar(D3DXHANDLE handle, uint8_t type) const
    {
        uint32_t code = Code(handle);
        if (code >= kAnnotationHandle && code < kElementHandle)
        {
            const EffectAnnotation &a = effect_.Info().annotations[code - kAnnotationHandle];
            return Convert(a.value, a.type, type);
        }
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return 0;
        const EffectParameter &e = effect_.Info().parameters[p];
        const uint32_t *values = const_cast<Adapter *>(this)->effect_.Values(p);
        if (!values)
            return 0;
        if (element >= 0)
            values += static_cast<uint32_t>(element) * e.rows * e.columns;
        return Convert(values[0], e.type, type);
    }

    template <typename T> HRESULT ReadArray(D3DXHANDLE handle, T *out, UINT count, uint8_t type)
    {
        int element;
        int p = ParameterOf(handle, element);
        if (p < 0)
            return E_FAIL;
        const EffectParameter &e = effect_.Info().parameters[p];
        uint32_t *values = element >= 0 ? ElementValues(p, element) : effect_.Values(p);
        uint32_t limit = element >= 0 ? ElementSize(p) : e.valueCount;
        for (UINT i = 0; i < count; ++i)
        {
            uint32_t word = values && i < limit ? Convert(values[i], e.type, type) : 0;
            memcpy(&out[i], &word, 4);
        }
        return D3D_OK;
    }

    void WriteMatrix(int p, int element, const float *m, bool transpose)
    {
        const EffectParameter &e = effect_.Info().parameters[p];
        uint32_t *values = ElementValues(p, element);
        if (!values)
            return;
        bool changed = false;
        for (uint32_t r = 0; r < e.rows && r < 4; ++r)
            for (uint32_t c = 0; c < e.columns && c < 4; ++c)
            {
                float v = transpose ? m[c * 4 + r] : m[r * 4 + c];
                uint32_t word;
                memcpy(&word, &v, 4);
                const uint32_t value = Convert(word, kParamFloat, e.type);
                changed |= values[r * e.columns + c] != value;
                values[r * e.columns + c] = value;
            }
        if (changed)
            effect_.Touch(p);
    }

    void AddRender(uint32_t id)
    {
        for (uint32_t i = 0; i < saved_.renderCount; ++i)
            if (saved_.renderIds[i] == id)
                return;
        if (saved_.renderCount < kMaxSaved)
        {
            saved_.renderIds[saved_.renderCount] = id;
            saved_.renderValues[saved_.renderCount++] = g_renderer->GetRenderState(id);
        }
    }

    void AddSampler(uint32_t stage, uint32_t id)
    {
        if (stage >= kMaxSamplers)
            return;
        for (uint32_t i = 0; i < saved_.samplerCount; ++i)
            if (saved_.samplerStages[i] == stage && saved_.samplerIds[i] == id)
                return;
        if (saved_.samplerCount < kMaxSaved)
        {
            saved_.samplerStages[saved_.samplerCount] = static_cast<uint8_t>(stage);
            saved_.samplerIds[saved_.samplerCount] = static_cast<uint8_t>(id);
            saved_.samplerValues[saved_.samplerCount++] = g_renderer->GetSamplerState(stage, id);
        }
    }

    void AddTexture(uint32_t stage)
    {
        if (stage < kMaxSamplers && !(saved_.textureMask & (1u << stage)))
        {
            saved_.textureMask |= 1u << stage;
            saved_.textures[stage] = g_renderer->GetTexture(stage);
        }
    }

    void SaveSampler(int parameter, uint32_t stage, DWORD flags)
    {
        if (parameter < 0)
            return;
        const EffectParameter &p = effect_.Info().parameters[parameter];
        for (uint32_t i = 0; i < p.samplerStateCount; ++i)
            SaveState(effect_.Info().states[p.samplerStateStart + i], stage, flags);
    }

    void SaveState(const EffectState &s, uint32_t boundStage, DWORD flags)
    {
        uint32_t stage = s.slot == kBoundStage ? boundStage : s.slot;
        switch (s.type)
        {
        case EffectStateType::Render:
            AddRender(s.id);
            break;
        case EffectStateType::Sampler:
            if (!(flags & D3DXFX_DONOTSAVESAMPLERSTATE))
                AddSampler(stage, s.id);
            break;
        case EffectStateType::Texture:
            AddTexture(stage);
            break;
        case EffectStateType::SetSampler:
            SaveSampler(static_cast<int>(s.value), stage, flags);
            break;
        case EffectStateType::VertexShader:
        case EffectStateType::PixelShader:
            if (s.value != ~0u)
            {
                const ShaderBlob &blob = kShaders[s.value];
                for (uint32_t i = 0; i < blob.constantInfoCount; ++i)
                    if (blob.constants[i].registerSet == 3)
                        for (uint32_t k = 0; k < blob.constants[i].count; ++k)
                            SaveSampler(effect_.Parameter(blob.constants[i].name), blob.constants[i].index + k, flags);
            }
            if (!(flags & D3DXFX_DONOTSAVESHADERSTATE) && !saved_.shaders)
            {
                saved_.shaders = true;
                saved_.vertexShader = g_renderer->GetVertexShader();
                saved_.pixelShader = g_renderer->GetPixelShader();
            }
            break;
        default:
            break;
        }
    }

    /** Begin's state save (flags 0, as the game begins its effects): what a technique's passes set is worked out once,
     * then each Begin only reads those states' values. */
    void Save(DWORD flags)
    {
        const int t = effect_.CurrentTechnique();
        if (flags || t < 0)
        {
            SaveStates(flags);
            return;
        }
        if (!plans_)
        {
            const size_t bytes = static_cast<size_t>(effect_.TechniqueCount()) * sizeof(SavedState *);
            plans_ = static_cast<SavedState **>(::operator new(bytes));
            memset(plans_, 0, bytes);
        }
        if (!plans_[t])
        {
            SaveStates(0);
            plans_[t] = static_cast<SavedState *>(::operator new(sizeof(SavedState)));
            memcpy(plans_[t], &saved_, sizeof(SavedState));
            return;
        }
        const SavedState &plan = *plans_[t];
        saved_.renderCount = plan.renderCount;
        for (uint32_t i = 0; i < plan.renderCount; ++i)
        {
            saved_.renderIds[i] = plan.renderIds[i];
            saved_.renderValues[i] = g_renderer->GetRenderState(plan.renderIds[i]);
        }
        saved_.samplerCount = plan.samplerCount;
        for (uint32_t i = 0; i < plan.samplerCount; ++i)
        {
            saved_.samplerStages[i] = plan.samplerStages[i];
            saved_.samplerIds[i] = plan.samplerIds[i];
            saved_.samplerValues[i] = g_renderer->GetSamplerState(plan.samplerStages[i], plan.samplerIds[i]);
        }
        saved_.textureMask = plan.textureMask;
        for (uint32_t s = 0; s < kMaxSamplers; ++s)
            if (plan.textureMask & (1u << s))
                saved_.textures[s] = g_renderer->GetTexture(s);
        saved_.shaders = plan.shaders;
        if (plan.shaders)
        {
            saved_.vertexShader = g_renderer->GetVertexShader();
            saved_.pixelShader = g_renderer->GetPixelShader();
        }
    }

    void SaveStates(DWORD flags)
    {
        memset(&saved_, 0, sizeof(saved_));
        const EffectTechnique &t = effect_.Info().techniques[effect_.CurrentTechnique()];
        for (uint16_t p = 0; p < t.passCount; ++p)
        {
            const EffectPass &pass = effect_.Info().passes[t.passStart + p];
            for (uint16_t i = 0; i < pass.stateCount; ++i)
                SaveState(effect_.Info().states[pass.stateStart + i], 0, flags);
        }
    }

    void Restore()
    {
        for (uint32_t i = 0; i < saved_.renderCount; ++i)
            g_renderer->SetRenderState(saved_.renderIds[i], saved_.renderValues[i]);
        for (uint32_t i = 0; i < saved_.samplerCount; ++i)
            g_renderer->SetSamplerState(saved_.samplerStages[i], saved_.samplerIds[i], saved_.samplerValues[i]);
        for (uint32_t s = 0; s < kMaxSamplers; ++s)
            if (saved_.textureMask & (1u << s))
                g_renderer->SetTexture(s, saved_.textures[s]);
        if (saved_.shaders)
            g_renderer->SetShaders(saved_.vertexShader, saved_.pixelShader);
    }

    ULONG refs_;
    bool saving_;
    SavedState saved_;
    /** Per technique, what Save saves for it (the ids; values unused), once worked out. */
    SavedState **plans_;
};
} // namespace

ID3DXEffect *CreateEffect(const char *resource)
{
    for (uint32_t i = 0; i < kEffectCount; ++i)
        if (lstrcmpiA(kEffects[i].resource, resource) == 0)
            return new Adapter(kEffects[i]);
    return nullptr;
}

Effect *NativeEffect(ID3DXEffect *effect)
{
    return effect ? &static_cast<Adapter *>(effect)->effect_ : nullptr;
}

int NativeParameter(ID3DXEffect *effect, D3DXHANDLE handle)
{
    return effect && handle ? static_cast<Adapter *>(effect)->ParameterIndex(handle) : -1;
}
} // namespace nfs::vk
