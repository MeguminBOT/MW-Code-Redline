/**
 * The effect runtime (engine/rendering/vulkan/effect.h). Parameter storage and conversions, pass state application and shader constant
 * layout follow D3DX 9 (checked against d3dx9_26.dll by the EffectParity self-test).
 */
#include <windows.h>
#include <intrin.h>
#include <string.h>
#include <unknwn.h>
#include <xmmintrin.h>

#include "core/log.h"
#include "utils/simd.h"
#include "engine/rendering/vulkan/effect.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::vk
{
namespace
{
const uint32_t kFloatRenderStates[] = {36, 37, 38, 154, 155, 158, 159, 160, 166, 170, 175, 178, 179, 180, 181, 182,
                                       183, 195};
const uint32_t kSamplerMipLodBias = 8;
const uint32_t kMaxRegisters = 256;

/** kFloatRenderStates as a table by state number (StateValue looks one up for every parameter-driven state). */
struct FloatRenderStates
{
    bool is[256];
    FloatRenderStates()
    {
        memset(is, 0, sizeof(is));
        for (uint32_t s : kFloatRenderStates)
            is[s] = true;
    }
};

const FloatRenderStates g_floatRenderStates;

bool IsFloatRenderState(uint32_t state)
{
    return state < 256 && g_floatRenderStates.is[state];
}

float AsFloat(uint32_t word)
{
    float f;
    memcpy(&f, &word, 4);
    return f;
}

uint32_t AsWord(float f)
{
    uint32_t word;
    memcpy(&word, &f, 4);
    return word;
}

/** A number stored as `from`, converted to `to` (D3DX's rules: floats truncate to integers, bools are 0 or 1). */
uint32_t Convert(uint32_t word, uint8_t from, uint8_t to)
{
    if (from == to)
        return word;
    switch (to)
    {
    case kParamFloat:
        if (from == kParamInt)
            return AsWord(static_cast<float>(static_cast<int>(word)));
        return AsWord(word ? 1.0f : 0.0f);
    case kParamBool:
        if (from == kParamFloat)
            return AsFloat(word) != 0.0f ? 1u : 0u;
        return word ? 1u : 0u;
    case kParamInt:
        if (from == kParamFloat)
            return static_cast<uint32_t>(static_cast<int>(AsFloat(word)));
        return word;
    default:
        return word;
    }
}

bool IsNumeric(const EffectParameter &p)
{
    return p.valueCount != 0;
}

/** The effect pool's shared parameters (D3DX matches them by name and type); the first effect's defaults start them. */
struct SharedParameter
{
    const EffectParameter *declaration;
    uint32_t *value;
    uint64_t *changedAt;
};

SharedParameter g_shared[256];
/** The shared parameters' change serials, together: a commit reads those of every parameter its shaders watch. */
uint64_t g_sharedSerials[256];
uint32_t g_sharedCount;

uint32_t *SharedSlot(const EffectParameter &p, const uint32_t *defaults, uint64_t *&changedAt)
{
    for (uint32_t i = 0; i < g_sharedCount; ++i)
    {
        const EffectParameter &d = *g_shared[i].declaration;
        if (lstrcmpA(d.name, p.name) == 0 && d.type == p.type && d.cls == p.cls && d.rows == p.rows &&
            d.columns == p.columns && d.elements == p.elements)
        {
            changedAt = g_shared[i].changedAt;
            return g_shared[i].value;
        }
    }
    uint32_t *value = static_cast<uint32_t *>(::operator new(p.valueCount * 4));
    memcpy(value, defaults, p.valueCount * 4);
    if (g_sharedCount < 256)
    {
        uint64_t *serial = &g_sharedSerials[g_sharedCount];
        *serial = 0;
        g_shared[g_sharedCount++] = {&p, value, serial};
        changedAt = serial;
    }
    return value;
}

/** An effect holds a reference to each texture it is given, as D3DX does: the game releases render targets it has set
 * on effects when the video options change, and an effect still pointing at one would bind it after it is freed.
 * Textures are COM objects (Direct3D 9's, or the renderer's IUnknown-shaped ones). */
void Hold(void *texture)
{
    if (texture)
        static_cast<IUnknown *>(texture)->AddRef();
}

void Drop(void *texture)
{
    if (texture)
        static_cast<IUnknown *>(texture)->Release();
}

/** Copies `bytes` to `to`; true when that changed it. */
bool Store(uint32_t *to, const void *from, uint32_t bytes)
{
    return nfs::StoreIfChanged(to, from, bytes);
}

bool Store(uint32_t &to, uint32_t word)
{
    if (to == word)
        return false;
    to = word;
    return true;
}
} // namespace

uint64_t Effect::s_changes;

Effect::Effect(const EffectInfo &info)
    : info_(info), values_(nullptr), slots_(nullptr), textures_(nullptr), changedAt_(nullptr), changes_(nullptr),
      passApplied_(0), layouts_(nullptr), passCommitLists_(nullptr), passStateLists_(nullptr),
      samplerCommitLists_(nullptr), passCount_(0), technique_(0),
      pass_(-1), vertexShader_(-1), pixelShader_(-1)
{
    if (info.valueCount)
    {
        values_ = static_cast<uint32_t *>(::operator new(info.valueCount * 4));
        memcpy(values_, info.defaults, info.valueCount * 4);
    }
    for (int t = 0; t < info.techniqueCount; ++t)
    {
        const uint32_t end = info.techniques[t].passStart + info.techniques[t].passCount;
        if (end > passCount_)
            passCount_ = end;
    }
    if (info.parameterCount)
    {
        textures_ = static_cast<void **>(::operator new(info.parameterCount * sizeof(void *)));
        memset(textures_, 0, info.parameterCount * sizeof(void *));
        slots_ = static_cast<uint32_t **>(::operator new(info.parameterCount * sizeof(uint32_t *)));
        changes_ = static_cast<uint64_t *>(::operator new(info.parameterCount * sizeof(uint64_t)));
        memset(changes_, 0, info.parameterCount * sizeof(uint64_t));
        changedAt_ = static_cast<uint64_t **>(::operator new(info.parameterCount * sizeof(uint64_t *)));
        for (int i = 0; i < info.parameterCount; ++i)
        {
            const EffectParameter &p = info.parameters[i];
            changedAt_[i] = &changes_[i];
            slots_[i] = p.shared && IsNumeric(p) ? SharedSlot(p, info.defaults + p.valueOffset, changedAt_[i])
                                                 : values_ ? values_ + p.valueOffset : nullptr;
        }
    }
}

Effect::~Effect()
{
    for (int i = 0; textures_ && i < info_.parameterCount; ++i)
        Drop(textures_[i]);
    ::operator delete(values_);
    ::operator delete(slots_);
    ::operator delete(textures_);
    ::operator delete(changedAt_);
    ::operator delete(changes_);
    for (uint32_t i = 0; passCommitLists_ && i < passCount_; ++i)
        ::operator delete(passCommitLists_[i]);
    ::operator delete(passCommitLists_);
    for (uint32_t i = 0; passStateLists_ && i < passCount_; ++i)
        ::operator delete(passStateLists_[i]);
    ::operator delete(passStateLists_);
    for (int i = 0; samplerCommitLists_ && i < info_.parameterCount; ++i)
        ::operator delete(samplerCommitLists_[i]);
    ::operator delete(samplerCommitLists_);
    if (layouts_)
    {
        for (uint32_t i = 0; i < kShaderCount; ++i)
        {
            if (!layouts_[i])
                continue;
            ::operator delete(layouts_[i]->copies);
            ::operator delete(layouts_[i]->runs);
            ::operator delete(layouts_[i]->watches);
            ::operator delete(layouts_[i]->samplers);
            ::operator delete(layouts_[i]->bools);
            ::operator delete(layouts_[i]->registers);
            ::operator delete(layouts_[i]);
        }
        ::operator delete(layouts_);
    }
}

int Effect::Parameter(const char *name) const
{
    for (int i = 0; i < info_.parameterCount; ++i)
        if (lstrcmpA(info_.parameters[i].name, name) == 0)
            return i;
    return -1;
}

int Effect::ParameterBySemantic(const char *semantic) const
{
    for (int i = 0; i < info_.parameterCount; ++i)
        if (info_.parameters[i].semantic && lstrcmpiA(info_.parameters[i].semantic, semantic) == 0)
            return i;
    return -1;
}

int Effect::Annotation(int parameter, const char *name) const
{
    if (parameter < 0)
        return -1;
    const EffectParameter &p = info_.parameters[parameter];
    for (int i = 0; i < p.annotationCount; ++i)
        if (lstrcmpA(info_.annotations[p.annotationStart + i].name, name) == 0)
            return p.annotationStart + i;
    return -1;
}

void Effect::SetValue(int parameter, const void *data, uint32_t bytes)
{
    if (parameter < 0)
        return;
    const EffectParameter &p = info_.parameters[parameter];
    if (!IsNumeric(p))
    {
        void *texture;
        if (bytes < sizeof(void *))
            return;
        memcpy(&texture, data, sizeof(void *));
        SetTexture(parameter, texture);
        return;
    }
    if (bytes > p.valueCount * 4u)
        bytes = p.valueCount * 4u;
    if (Store(slots_[parameter], data, bytes))
        Touch(parameter);
}

void Effect::SetBool(int parameter, bool value)
{
    if (parameter >= 0 && IsNumeric(info_.parameters[parameter]) &&
        Store(slots_[parameter][0], Convert(value ? 1u : 0u, kParamBool, info_.parameters[parameter].type)))
        Touch(parameter);
}

void Effect::SetInt(int parameter, int value)
{
    if (parameter >= 0 && IsNumeric(info_.parameters[parameter]) &&
        Store(slots_[parameter][0], Convert(static_cast<uint32_t>(value), kParamInt, info_.parameters[parameter].type)))
        Touch(parameter);
}

void Effect::SetFloat(int parameter, float value)
{
    if (parameter >= 0 && IsNumeric(info_.parameters[parameter]) &&
        Store(slots_[parameter][0], Convert(AsWord(value), kParamFloat, info_.parameters[parameter].type)))
        Touch(parameter);
}

void Effect::SetIntArray(int parameter, const int *values, uint32_t count)
{
    if (parameter < 0 || !IsNumeric(info_.parameters[parameter]))
        return;
    const EffectParameter &p = info_.parameters[parameter];
    bool changed = false;
    for (uint32_t i = 0; i < count && i < p.valueCount; ++i)
        changed |= Store(slots_[parameter][i], Convert(static_cast<uint32_t>(values[i]), kParamInt, p.type));
    if (changed)
        Touch(parameter);
}

void Effect::SetFloatArray(int parameter, const float *values, uint32_t count)
{
    if (parameter < 0 || !IsNumeric(info_.parameters[parameter]))
        return;
    const EffectParameter &p = info_.parameters[parameter];
    bool changed = false;
    for (uint32_t i = 0; i < count && i < p.valueCount; ++i)
        changed |= Store(slots_[parameter][i], Convert(AsWord(values[i]), kParamFloat, p.type));
    if (changed)
        Touch(parameter);
}

void Effect::SetVector(int parameter, const float *vector)
{
    SetVectorArray(parameter, vector, 1);
}

void Effect::SetVectorArray(int parameter, const float *vectors, uint32_t count)
{
    if (parameter < 0 || !IsNumeric(info_.parameters[parameter]))
        return;
    const EffectParameter &p = info_.parameters[parameter];
    // A vector fills one row of the parameter (its columns); an array of vectors fills consecutive rows.
    uint32_t rows = (p.elements ? p.elements : 1) * p.rows;
    if (p.type == kParamFloat)
    {
        const uint32_t n = count < rows ? count : rows, columns = p.columns < 4 ? p.columns : 4;
        uint32_t *out = slots_[parameter];
        bool changed = false;
        if (p.columns == 4)
            changed = Store(out, vectors, n * 16u);
        else
            for (uint32_t v = 0; v < n; ++v)
                changed |= Store(out + v * p.columns, vectors + v * 4, columns * 4u);
        if (changed)
            Touch(parameter);
        return;
    }
    bool changed = false;
    for (uint32_t v = 0; v < count && v < rows; ++v)
        for (uint32_t c = 0; c < p.columns && c < 4; ++c)
            changed |= Store(slots_[parameter][v * p.columns + c],
                             Convert(AsWord(vectors[v * 4 + c]), kParamFloat, p.type));
    if (changed)
        Touch(parameter);
}

void Effect::SetMatrix(int parameter, const float *matrix)
{
    SetMatrixArray(parameter, matrix, 1);
}

void Effect::SetMatrixArray(int parameter, const float *matrices, uint32_t count)
{
    if (parameter < 0 || !IsNumeric(info_.parameters[parameter]))
        return;
    const EffectParameter &p = info_.parameters[parameter];
    uint32_t elements = p.elements ? p.elements : 1;
    if (p.type == kParamFloat)
    {
        const uint32_t n = count < elements ? count : elements;
        const uint32_t rows = p.rows < 4 ? p.rows : 4, columns = p.columns < 4 ? p.columns : 4;
        uint32_t *out = slots_[parameter];
        bool changed = false;
        if (p.rows == 4 && p.columns == 4)
            changed = Store(out, matrices, n * 64u);
        else
            for (uint32_t e = 0; e < n; ++e)
                for (uint32_t r = 0; r < rows; ++r)
                    changed |= Store(out + (e * p.rows + r) * p.columns, matrices + e * 16 + r * 4, columns * 4u);
        if (changed)
            Touch(parameter);
        return;
    }
    bool changed = false;
    for (uint32_t e = 0; e < count && e < elements; ++e)
        for (uint32_t r = 0; r < p.rows && r < 4; ++r)
            for (uint32_t c = 0; c < p.columns && c < 4; ++c)
                changed |= Store(slots_[parameter][(e * p.rows + r) * p.columns + c],
                                 Convert(AsWord(matrices[e * 16 + r * 4 + c]), kParamFloat, p.type));
    if (changed)
        Touch(parameter);
}

void Effect::SetMatrixTranspose(int parameter, const float *matrix)
{
    float t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t[r * 4 + c] = matrix[c * 4 + r];
    SetMatrix(parameter, t);
}

void Effect::SetTexture(int parameter, void *texture)
{
    if (parameter >= 0 && !IsNumeric(info_.parameters[parameter]) && textures_[parameter] != texture)
    {
        Hold(texture);
        Drop(textures_[parameter]);
        textures_[parameter] = texture;
        Touch(parameter);
    }
}

int Effect::GetInt(int parameter) const
{
    if (parameter < 0 || !IsNumeric(info_.parameters[parameter]))
        return 0;
    const EffectParameter &p = info_.parameters[parameter];
    return static_cast<int>(Convert(slots_[parameter][0], p.type, kParamInt));
}

float Effect::GetFloat(int parameter) const
{
    if (parameter < 0 || !IsNumeric(info_.parameters[parameter]))
        return 0.0f;
    const EffectParameter &p = info_.parameters[parameter];
    return AsFloat(Convert(slots_[parameter][0], p.type, kParamFloat));
}

void *Effect::GetTexture(int parameter) const
{
    return parameter >= 0 ? textures_[parameter] : nullptr;
}

int Effect::Technique(const char *name) const
{
    for (int i = 0; i < info_.techniqueCount; ++i)
        if (lstrcmpA(info_.techniques[i].name, name) == 0)
            return i;
    return -1;
}

int Effect::TechniqueAnnotation(int technique, const char *name) const
{
    if (technique < 0 || technique >= info_.techniqueCount)
        return -1;
    const EffectTechnique &t = info_.techniques[technique];
    for (int i = 0; i < t.annotationCount; ++i)
        if (lstrcmpA(info_.annotations[t.annotationStart + i].name, name) == 0)
            return t.annotationStart + i;
    return -1;
}

void Effect::SetTechnique(int technique)
{
    if (technique >= 0 && technique < info_.techniqueCount)
        technique_ = technique;
}

uint32_t Effect::Begin()
{
    pass_ = -1;
    return info_.techniques[technique_].passCount;
}

void Effect::BeginPass(uint32_t pass, EffectStateSink &sink)
{
    if (pass >= info_.techniques[technique_].passCount)
        return;
    pass_ = static_cast<int>(pass);
    ApplyPass(sink, false);
}

void Effect::CommitChanges(EffectStateSink &sink)
{
    if (pass_ >= 0)
        ApplyPass(sink, true);
}

void Effect::EndPass()
{
    pass_ = -1;
}

void Effect::End()
{
    pass_ = -1;
}

uint32_t Effect::StateValue(const EffectState &state) const
{
    if (state.kind == EffectValueKind::Constant)
        return state.value;
    const EffectParameter &p = info_.parameters[state.value];
    uint32_t word = slots_[state.value][state.kind == EffectValueKind::Element ? state.offset : 0];
    bool floatState = (state.type == EffectStateType::Render && IsFloatRenderState(state.id)) ||
                      (state.type == EffectStateType::Sampler && state.id == kSamplerMipLodBias);
    if (floatState)
        return Convert(word, p.type, kParamFloat);
    return Convert(word, p.type, kParamInt);
}

void Effect::ApplyState(const EffectState &state, uint32_t boundStage, EffectStateSink &sink, bool parametersOnly)
{
    uint32_t stage = state.slot == kBoundStage ? boundStage : state.slot;
    switch (state.type)
    {
    case EffectStateType::Render:
        if (state.kind == EffectValueKind::Constant)
        {
            if (!parametersOnly)
                sink.SetRenderState(state.id, state.value);
        }
        else
        {
            sink.SetRenderState(state.id, StateValue(state));
        }
        break;
    case EffectStateType::Sampler:
        if (state.kind == EffectValueKind::Constant)
        {
            if (!parametersOnly)
                sink.SetSamplerState(stage, state.id, state.value);
        }
        else
        {
            sink.SetSamplerState(stage, state.id, StateValue(state));
        }
        break;
    case EffectStateType::Texture:
        sink.SetTexture(stage, state.kind == EffectValueKind::Parameter ? textures_[state.value] : nullptr);
        break;
    case EffectStateType::VertexShader:
        vertexShader_ = state.value == ~0u ? -1 : static_cast<int>(state.value);
        ApplyShader(vertexShader_, false, sink, parametersOnly);
        break;
    case EffectStateType::PixelShader:
        pixelShader_ = state.value == ~0u ? -1 : static_cast<int>(state.value);
        ApplyShader(pixelShader_, true, sink, parametersOnly);
        break;
    case EffectStateType::SetSampler:
        ApplySampler(static_cast<int>(state.value), stage, sink, parametersOnly);
        break;
    default:
        break;
    }
}

const uint16_t *Effect::CommitList(uint16_t **&lists, uint32_t index, uint32_t listCount, uint32_t stateStart,
                                   uint32_t stateCount)
{
    if (!lists)
    {
        lists = static_cast<uint16_t **>(::operator new(listCount * sizeof(uint16_t *)));
        memset(lists, 0, listCount * sizeof(uint16_t *));
    }
    uint16_t *&list = lists[index];
    if (list)
        return list;
    list = static_cast<uint16_t *>(::operator new((stateCount + 1) * sizeof(uint16_t)));
    list[0] = 0;
    for (uint32_t i = 0; i < stateCount; ++i)
    {
        const EffectState &state = info_.states[stateStart + i];
        const bool applied = state.kind != EffectValueKind::Constant || state.type == EffectStateType::Texture ||
                             state.type == EffectStateType::VertexShader ||
                             state.type == EffectStateType::PixelShader || state.type == EffectStateType::SetSampler;
        if (applied)
            list[++list[0]] = static_cast<uint16_t>(stateStart + i);
    }
    return list;
}

void Effect::ApplySampler(int parameter, uint32_t stage, EffectStateSink &sink, bool parametersOnly)
{
    if (parameter < 0)
        return;
    const EffectParameter &p = info_.parameters[parameter];
    if (parametersOnly)
    {
        const uint16_t *list = CommitList(samplerCommitLists_, static_cast<uint32_t>(parameter), info_.parameterCount,
                                          p.samplerStateStart, p.samplerStateCount);
        for (uint32_t i = 1; i <= list[0]; ++i)
            ApplyState(info_.states[list[i]], stage, sink, true);
        return;
    }
    for (uint32_t i = 0; i < p.samplerStateCount; ++i)
        ApplyState(info_.states[p.samplerStateStart + i], stage, sink, parametersOnly);
}

Effect::ConstantLayout &Effect::Layout(int shader)
{
    if (!layouts_)
    {
        layouts_ = static_cast<ConstantLayout **>(::operator new(kShaderCount * sizeof(ConstantLayout *)));
        memset(layouts_, 0, kShaderCount * sizeof(ConstantLayout *));
    }
    ConstantLayout *&layout = layouts_[shader];
    if (layout)
        return *layout;
    const ShaderBlob &blob = kShaders[shader];
    layout = static_cast<ConstantLayout *>(::operator new(sizeof(ConstantLayout)));
    memset(layout, 0, sizeof(ConstantLayout));
    uint32_t registers = 0, samplers = 0;
    for (uint32_t i = 0; i < blob.constantInfoCount; ++i)
    {
        registers += blob.constants[i].count;
        if (blob.constants[i].registerSet == 3)
            samplers += blob.constants[i].count;
    }
    layout->copies = static_cast<RegisterCopy *>(::operator new((registers + 1) * sizeof(RegisterCopy)));
    layout->samplers = static_cast<SamplerBinding *>(::operator new((samplers + 1) * sizeof(SamplerBinding)));
    layout->bools = static_cast<BoolRegister *>(::operator new((registers + 1) * sizeof(BoolRegister)));
    layout->first = kMaxRegisters;
    for (uint32_t i = 0; i < blob.constantInfoCount; ++i)
    {
        const ShaderConstant &c = blob.constants[i];
        const int parameter = Parameter(c.name);
        if (parameter < 0)
            continue;
        const EffectParameter &p = info_.parameters[parameter];
        if (c.registerSet == 3)
        {
            for (uint32_t s = 0; s < c.count; ++s)
                layout->samplers[layout->samplerCount++] = {static_cast<int16_t>(parameter),
                                                            static_cast<uint16_t>(c.index + s)};
            continue;
        }
        if (c.registerSet == 0 && IsNumeric(p))
        {
            for (uint32_t r = 0; r < c.count && c.index + r < 16 && r < p.valueCount; ++r)
                layout->bools[layout->boolCount++] = {slots_[parameter] + r, static_cast<int16_t>(parameter),
                                                      static_cast<uint8_t>(c.index + r), p.type};
            continue;
        }
        if (c.registerSet != 2 || !IsNumeric(p))
            continue;
        // Registers per element: a matrix takes one per column (column-major) or row; vectors and scalars take one.
        const uint32_t perElement = c.cls == kClassMatrixColumns ? c.columns : c.cls == kClassMatrixRows ? c.rows : 1;
        const uint32_t elements = p.elements ? p.elements : 1;
        const uint32_t elementSize = p.rows * p.columns;
        for (uint32_t r = 0; r < c.count && c.index + r < kMaxRegisters; ++r)
        {
            const uint32_t e = r / perElement, k = r % perElement;
            if (e >= elements)
                break;
            const uint32_t *value = slots_[parameter] + e * elementSize;
            RegisterCopy copy;
            copy.reg = static_cast<uint16_t>(c.index + r);
            copy.parameter = static_cast<int16_t>(parameter);
            copy.type = p.type;
            if (c.cls == kClassMatrixColumns)
            {
                copy.source = value + k;
                copy.count = static_cast<uint8_t>(c.rows < p.rows ? c.rows : p.rows);
                copy.stride = p.columns;
            }
            else if (c.cls == kClassMatrixRows)
            {
                copy.source = value + k * p.columns;
                copy.count = static_cast<uint8_t>(c.columns < p.columns ? c.columns : p.columns);
                copy.stride = 1;
            }
            else
            {
                copy.source = value;
                copy.count = static_cast<uint8_t>(c.columns < p.columns ? c.columns : p.columns);
                copy.stride = 1;
            }
            if (copy.count > 4)
                copy.count = 4;
            layout->copies[layout->copyCount++] = copy;
            if (copy.reg < layout->first)
                layout->first = copy.reg;
            if (copy.reg + 1u > layout->end)
                layout->end = copy.reg + 1u;
        }
    }
    if (layout->end > layout->first)
    {
        const uint32_t bytes = (layout->end - layout->first) * 16;
        layout->registers = static_cast<float(*)[4]>(::operator new(bytes));
        memset(layout->registers, 0, bytes);
    }
    layout->runs = static_cast<RegisterRun *>(::operator new((layout->copyCount + 1) * sizeof(RegisterRun)));
    for (uint32_t i = 0; i < layout->copyCount; ++i)
    {
        const RegisterCopy &copy = layout->copies[i];
        RegisterRun *run = layout->runCount ? &layout->runs[layout->runCount - 1] : nullptr;
        if (!run || run->parameter != copy.parameter)
        {
            run = &layout->runs[layout->runCount++];
            run->parameter = copy.parameter;
            run->first = static_cast<uint16_t>(i);
            run->count = 0;
            run->low = copy.reg;
            run->high = static_cast<uint16_t>(copy.reg + 1u);
        }
        ++run->count;
        run->low = copy.reg < run->low ? copy.reg : run->low;
        run->high = copy.reg + 1u > run->high ? static_cast<uint16_t>(copy.reg + 1u) : run->high;
    }
    for (uint32_t r = 0; r < layout->runCount; ++r)
    {
        RegisterRun &run = layout->runs[r];
        const RegisterCopy *c = layout->copies + run.first;
        const EffectParameter &p = info_.parameters[run.parameter];
        bool transposed = run.count <= 4 && p.rows == 4 && p.columns == 4 &&
                          (c[0].source - slots_[run.parameter]) % 16 == 0;
        for (uint32_t k = 0; transposed && k < run.count; ++k)
            transposed = c[k].type == kParamFloat && c[k].stride == 4 && c[k].count == 4 &&
                         c[k].source == c[0].source + k && c[k].reg == c[0].reg + k;
        run.transposed = transposed;
    }
    if (layout->runCount <= 32 && layout->samplerCount <= 32)
    {
        uint32_t states = 0;
        for (uint32_t s = 0; s < layout->samplerCount; ++s)
            states += info_.parameters[layout->samplers[s].parameter].samplerStateCount;
        layout->watches = static_cast<ParameterWatch *>(
            ::operator new((layout->runCount + states + layout->boolCount + 1) * sizeof(ParameterWatch)));
        const auto watch = [&](int parameter) -> ParameterWatch & {
            const uint64_t *serial = changedAt_[parameter];
            for (uint32_t i = 0; i < layout->watchCount; ++i)
                if (layout->watches[i].serial == serial)
                    return layout->watches[i];
            ParameterWatch &w = layout->watches[layout->watchCount++];
            w = {serial, 0, 0, 0};
            return w;
        };
        for (uint32_t r = 0; r < layout->runCount; ++r)
            watch(layout->runs[r].parameter).runs |= 1u << r;
        for (uint32_t i = 0; i < layout->boolCount; ++i)
            watch(layout->bools[i].parameter).bools = 1;
        for (uint32_t s = 0; s < layout->samplerCount; ++s)
        {
            const EffectParameter &p = info_.parameters[layout->samplers[s].parameter];
            const uint16_t *list = CommitList(samplerCommitLists_, static_cast<uint32_t>(layout->samplers[s].parameter),
                                              info_.parameterCount, p.samplerStateStart, p.samplerStateCount);
            for (uint32_t i = 1; i <= list[0]; ++i)
            {
                const EffectState &state = info_.states[list[i]];
                if (state.kind != EffectValueKind::Constant)
                    watch(static_cast<int>(state.value)).samplers |= 1u << s;
            }
        }
        layout->allRuns = layout->runCount == 32 ? ~0u : (1u << layout->runCount) - 1;
        layout->allSamplers = layout->samplerCount == 32 ? ~0u : (1u << layout->samplerCount) - 1;
    }
    return *layout;
}

void Effect::CopyParameterRegisters(int shader, int parameter, float (*registers)[4])
{
    if (shader < 0 || parameter < 0)
        return;
    const ConstantLayout &layout = Layout(shader);
    for (uint32_t r = 0; r < layout.runCount; ++r)
        if (layout.runs[r].parameter == parameter)
            CopyRun(layout, layout.runs[r], registers);
}

int Effect::ParameterRuns(int shader, const int *parameters, int count, uint16_t *runs, int max)
{
    if (shader < 0)
        return 0;
    const ConstantLayout &layout = Layout(shader);
    int n = 0;
    for (uint32_t r = 0; r < layout.runCount && n < max; ++r)
        for (int p = 0; p < count; ++p)
            if (layout.runs[r].parameter == parameters[p])
            {
                runs[n++] = static_cast<uint16_t>(r);
                break;
            }
    return n;
}

void Effect::CopyRuns(int shader, const uint16_t *runs, int count, float (*registers)[4])
{
    if (shader < 0 || !count)
        return;
    const ConstantLayout &layout = Layout(shader);
    for (int i = 0; i < count; ++i)
        CopyRun(layout, layout.runs[runs[i]], registers);
}

void Effect::CopyRun(const ConstantLayout &layout, const RegisterRun &run, float (*registers)[4]) const
{
    const RegisterCopy *copies = layout.copies + run.first;
    if (run.transposed)
    {
        const float *m = reinterpret_cast<const float *>(copies[0].source);
        __m128 r0 = _mm_loadu_ps(m), r1 = _mm_loadu_ps(m + 4), r2 = _mm_loadu_ps(m + 8), r3 = _mm_loadu_ps(m + 12);
        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
        float *out = registers[copies[0].reg];
        _mm_storeu_ps(out, r0);
        if (run.count > 1)
            _mm_storeu_ps(out + 4, r1);
        if (run.count > 2)
            _mm_storeu_ps(out + 8, r2);
        if (run.count > 3)
            _mm_storeu_ps(out + 12, r3);
        return;
    }
    for (uint32_t i = 0; i < run.count; ++i)
    {
        const RegisterCopy &copy = copies[i];
        float *reg = registers[copy.reg];
        const uint32_t *source = copy.source;
        if (copy.type != kParamFloat)
        {
            for (uint32_t n = 0; n < copy.count; ++n)
                reg[n] = AsFloat(Convert(source[n * copy.stride], copy.type, kParamFloat));
        }
        else if (copy.stride != 1)
        {
            for (uint32_t n = 0; n < copy.count; ++n)
                memcpy(&reg[n], source + n * copy.stride, 4);
        }
        else if (copy.count == 4)
        {
            _mm_storeu_ps(reg, _mm_loadu_ps(reinterpret_cast<const float *>(source)));
        }
        else
        {
            for (uint32_t n = 0; n < copy.count; ++n)
                memcpy(&reg[n], source + n, 4);
        }
    }
}

bool Effect::SamplerChanged(int parameter, uint64_t since)
{
    if (parameter < 0)
        return false;
    const EffectParameter &p = info_.parameters[parameter];
    const uint16_t *list = CommitList(samplerCommitLists_, static_cast<uint32_t>(parameter), info_.parameterCount,
                                      p.samplerStateStart, p.samplerStateCount);
    for (uint32_t i = 1; i <= list[0]; ++i)
    {
        const EffectState &state = info_.states[list[i]];
        if (state.kind != EffectValueKind::Constant && *changedAt_[state.value] > since)
            return true;
    }
    return false;
}

void Effect::ApplyShader(int shader, bool pixel, EffectStateSink &sink, bool parametersOnly)
{
    if (shader < 0)
        return;
    ConstantLayout &layout = layouts_ && layouts_[shader] ? *layouts_[shader] : Layout(shader);
    // A commit applies only what changed since the shader was applied; a pass applies every sampler, as another
    // effect may have set them since.
    const uint64_t since = layout.built ? layout.applied : 0;
    const bool everything = !layout.built;
    layout.applied = s_changes;
    layout.built = true;
    // The runs and samplers that read a parameter changed since the shader was last applied.
    uint32_t runs = layout.allRuns, samplers = layout.allSamplers, bools = 1;
    if (layout.watches && !everything)
    {
        runs = samplers = bools = 0;
        for (uint32_t i = 0; i < layout.watchCount; ++i)
        {
            const ParameterWatch &w = layout.watches[i];
            const uint32_t hit = 0u - static_cast<uint32_t>((since - *w.serial) >> 63);
            runs |= w.runs & hit;
            samplers |= w.samplers & hit;
            bools |= w.bools & hit;
        }
    }
    if (!parametersOnly)
        samplers = layout.allSamplers;
    for (uint32_t i = 0; i < layout.samplerCount; ++i)
    {
        const bool changed = layout.watches ? (samplers >> i & 1) != 0
                                            : !parametersOnly || everything ||
                                                  SamplerChanged(layout.samplers[i].parameter, since);
        if (changed)
            ApplySampler(layout.samplers[i].parameter, layout.samplers[i].stage, sink, parametersOnly);
    }
    if (layout.boolCount)
    {
        if (bools)
        {
            uint32_t bits = 0;
            for (uint32_t i = 0; i < layout.boolCount; ++i)
                if (Convert(*layout.bools[i].source, layout.bools[i].type, kParamBool))
                    bits |= 1u << layout.bools[i].reg;
            layout.boolBits = bits;
        }
        sink.SetBoolConstants(pixel, layout.boolBits);
    }
    if (layout.end <= layout.first)
        return;
    float(*registers)[4] = layout.registers - layout.first;
    uint32_t low = layout.end, high = layout.first;
    if (layout.watches)
    {
        for (unsigned long bits = runs, r; _BitScanForward(&r, bits); bits &= bits - 1)
        {
            const RegisterRun &run = layout.runs[r];
            low = run.low < low ? run.low : low;
            high = run.high > high ? run.high : high;
            CopyRun(layout, run, registers);
        }
    }
    else
    {
        for (uint32_t r = 0; r < layout.runCount; ++r)
        {
            const RegisterRun &run = layout.runs[r];
            if (!everything && *changedAt_[run.parameter] <= since)
                continue;
            low = run.low < low ? run.low : low;
            high = run.high > high ? run.high : high;
            CopyRun(layout, run, registers);
        }
    }
    // Unless the sink still holds what this shader last set, it gets every register.
    const uint32_t stamp = sink.ConstantsStamp(pixel);
    if (!stamp || stamp != layout.stamp)
    {
        low = layout.first;
        high = layout.end;
    }
    if (low < high)
    {
        sink.SetConstants(pixel, low, high - low, registers + low);
        layout.stamp = sink.ConstantsStamp(pixel);
    }
    else
    {
        layout.stamp = stamp;
    }
}

const uint16_t *Effect::PassStates(uint32_t passIndex, uint32_t first)
{
    if (!passStateLists_)
    {
        passStateLists_ = static_cast<uint16_t **>(::operator new(passCount_ * sizeof(uint16_t *)));
        memset(passStateLists_, 0, passCount_ * sizeof(uint16_t *));
    }
    uint16_t *&list = passStateLists_[passIndex];
    if (list)
        return list;
    uint32_t total = 0;
    for (uint32_t p = first; p <= passIndex; ++p)
        total += info_.passes[p].stateCount;
    list = static_cast<uint16_t *>(::operator new((total + 1) * sizeof(uint16_t)));
    list[0] = 0;
    // The state a later pass sets again goes; what is left keeps its order. The shaders count only in the last pass,
    // for their samplers.
    const auto key = [this](const EffectState &s) -> uint32_t {
        return static_cast<uint32_t>(s.type) << 24 | static_cast<uint32_t>(s.slot) << 16 | s.id;
    };
    for (uint32_t p = first; p <= passIndex; ++p)
    {
        const EffectPass &pass = info_.passes[p];
        for (uint32_t i = 0; i < pass.stateCount; ++i)
        {
            const uint32_t index = pass.stateStart + i;
            const EffectState &state = info_.states[index];
            const bool shader = state.type == EffectStateType::VertexShader || state.type == EffectStateType::PixelShader;
            if (shader ? p != passIndex
                       : state.type != EffectStateType::Render && state.type != EffectStateType::Sampler &&
                             state.type != EffectStateType::Texture && state.type != EffectStateType::SetSampler)
                continue;
            const uint32_t k = key(state);
            uint32_t kept = 0;
            for (uint32_t j = 1; j <= list[0]; ++j)
                if (key(info_.states[list[j]]) != k)
                    list[++kept] = list[j];
            list[0] = static_cast<uint16_t>(kept);
            list[++list[0]] = static_cast<uint16_t>(index);
        }
    }
    return list;
}

void Effect::ApplyPassStates(uint32_t pass, EffectStateSink &sink)
{
    const EffectTechnique &t = info_.techniques[technique_];
    if (pass >= t.passCount)
        return;
    const uint16_t *list = PassStates(t.passStart + pass, t.passStart);
    for (uint32_t i = 1; i <= list[0]; ++i)
    {
        const EffectState &state = info_.states[list[i]];
        if (state.type != EffectStateType::VertexShader && state.type != EffectStateType::PixelShader)
        {
            ApplyState(state, 0, sink, false);
            continue;
        }
        if (state.value == ~0u)
            continue;
        const ConstantLayout &layout = Layout(static_cast<int>(state.value));
        for (uint32_t s = 0; s < layout.samplerCount; ++s)
            ApplySampler(layout.samplers[s].parameter, layout.samplers[s].stage, sink, false);
    }
}

void Effect::ApplyPass(EffectStateSink &sink, bool parametersOnly)
{
    const EffectTechnique &t = info_.techniques[technique_];
    const uint32_t passIndex = t.passStart + static_cast<uint32_t>(pass_);
    const EffectPass &pass = info_.passes[passIndex];
    if (parametersOnly)
    {
        if (passApplied_ == s_changes)
            return;
        const uint64_t since = passApplied_;
        passApplied_ = s_changes;
        const uint16_t *list = passCommitLists_ && passCommitLists_[passIndex]
                                   ? passCommitLists_[passIndex]
                                   : CommitList(passCommitLists_, passIndex, passCount_, pass.stateStart, pass.stateCount);
        for (uint32_t i = 1; i <= list[0]; ++i)
        {
            const EffectState &state = info_.states[list[i]];
            if (state.type == EffectStateType::VertexShader || state.type == EffectStateType::PixelShader)
                ApplyShader(state.type == EffectStateType::VertexShader ? vertexShader_ : pixelShader_,
                            state.type == EffectStateType::PixelShader, sink, true);
            else if (state.type == EffectStateType::SetSampler
                         ? SamplerChanged(static_cast<int>(state.value), since)
                         : state.kind != EffectValueKind::Constant && *changedAt_[state.value] > since)
                ApplyState(state, 0, sink, true);
        }
        return;
    }
    passApplied_ = s_changes;
    vertexShader_ = pixelShader_ = -1;
    for (uint32_t i = 0; i < pass.stateCount; ++i)
        ApplyState(info_.states[pass.stateStart + i], 0, sink, false);
    sink.SetShaders(vertexShader_, pixelShader_);
}
} // namespace nfs::vk
