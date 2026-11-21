#pragma once

#include "engine/rendering/vulkan/effect_data.h"
#include "engine/rendering/vulkan/shaders.h"

namespace nfs::vk
{
/**
 * Where an effect's passes put their state: the renderer's state tracker, or a test that compares with D3DX. Values
 * are Direct3D 9 enums, as the effects (and the rest of the game) store them.
 */
class EffectStateSink
{
  public:
    virtual void SetRenderState(uint32_t state, uint32_t value) = 0;
    virtual void SetSamplerState(uint32_t stage, uint32_t state, uint32_t value) = 0;
    virtual void SetTexture(uint32_t stage, void *texture) = 0;
    /** kShaders indices, -1 for none. */
    virtual void SetShaders(int vertexShader, int pixelShader) = 0;
    /** Float constant registers [first, first + count) of the vertex (pixel == false) or pixel shader. */
    virtual void SetConstants(bool pixel, uint32_t first, uint32_t count, const float (*registers)[4]) = 0;
    /** Boolean constant registers b0-b15 of the vertex or pixel shader, one bit each (SM3 `if b#`). */
    virtual void SetBoolConstants(bool pixel, uint32_t bits)
    {
    }
    /** Counts the changes to the vertex or pixel shader's float constants; 0 when the sink does not count them. While it
     * stays the same the sink holds the constants an effect last set, so the effect need not set them again. */
    virtual uint32_t ConstantsStamp(bool pixel) const
    {
        return 0;
    }
};

/**
 * One effect (the game's eEffect wraps one per fx resource): parameter values, the current technique and pass, and
 * applying a pass the way D3DX does. Parameters are addressed by index into EffectInfo::parameters (-1: none).
 */
class Effect
{
  public:
    explicit Effect(const EffectInfo &info);
    ~Effect();
    Effect(const Effect &) = delete;
    Effect &operator=(const Effect &) = delete;

    const EffectInfo &Info() const
    {
        return info_;
    }

    int Parameter(const char *name) const;
    int ParameterBySemantic(const char *semantic) const;
    /** -1 when the parameter has no annotation of that name. */
    int Annotation(int parameter, const char *name) const;

    void SetValue(int parameter, const void *data, uint32_t bytes);
    void SetBool(int parameter, bool value);
    void SetInt(int parameter, int value);
    void SetFloat(int parameter, float value);
    void SetIntArray(int parameter, const int *values, uint32_t count);
    void SetFloatArray(int parameter, const float *values, uint32_t count);
    void SetVector(int parameter, const float *vector);
    void SetVectorArray(int parameter, const float *vectors, uint32_t count);
    void SetMatrix(int parameter, const float *matrix);
    void SetMatrixArray(int parameter, const float *matrices, uint32_t count);
    void SetMatrixTranspose(int parameter, const float *matrix);
    void SetTexture(int parameter, void *texture);
    int GetInt(int parameter) const;
    float GetFloat(int parameter) const;
    void *GetTexture(int parameter) const;
    /** The parameter's stored DWORDs (numeric parameters; matrices row by row), for element-wise access. Call Touch
     * after writing through it. */
    uint32_t *Values(int parameter)
    {
        return parameter >= 0 && info_.parameters[parameter].valueCount ? slots_[parameter] : nullptr;
    }
    /** Notes that the parameter's value changed, so the next CommitChanges or BeginPass applies what reads it. */
    void Touch(int parameter)
    {
        *changedAt_[parameter] = ++s_changes;
    }

    int TechniqueCount() const
    {
        return info_.techniqueCount;
    }
    int Technique(const char *name) const;
    /** -1 when the technique has no annotation of that name. */
    int TechniqueAnnotation(int technique, const char *name) const;
    void SetTechnique(int technique);
    int CurrentTechnique() const
    {
        return technique_;
    }
    int CurrentPass() const
    {
        return pass_;
    }
    /** The current technique's pass count. */
    uint32_t TechniquePasses() const
    {
        return info_.techniques[technique_].passCount;
    }
    int VertexShader() const
    {
        return vertexShader_;
    }
    int PixelShader() const
    {
        return pixelShader_;
    }

    /** Returns the current technique's pass count. */
    uint32_t Begin();
    void BeginPass(uint32_t pass, EffectStateSink &sink);
    /** Applies parameter changes made since BeginPass: shader constants, textures and states that read parameters. */
    void CommitChanges(EffectStateSink &sink);
    /** The render, sampler and texture states BeginPass of passes 0 to `pass` in turn would leave (the last of each
     * wins), and the samplers of that pass's shaders, from the parameters as they are: for a draw in pass `pass`
     * after code set states of its own since the pass began (a run of strips drawn a pass at a time,
     * source/engine/rendering/frame/RenderList.cpp). The shader constants are CommitChanges's. */
    void ApplyPassStates(uint32_t pass, EffectStateSink &sink);
    /** Writes the float registers `shader` reads `parameter` into from the parameter's current value into `registers`
     * (indexed by register number), as a commit would set them. */
    void CopyParameterRegisters(int shader, int parameter, float (*registers)[4]);
    /** The register runs of `shader` that read any of `parameters`, at most `max` of them into `runs`: for
     * CopyRuns, again and again while the parameters change. Returns how many. */
    int ParameterRuns(int shader, const int *parameters, int count, uint16_t *runs, int max);
    /** CopyParameterRegisters for the runs ParameterRuns found. */
    void CopyRuns(int shader, const uint16_t *runs, int count, float (*registers)[4]);
    void EndPass();
    void End();

  private:
    uint32_t StateValue(const EffectState &state) const;
    void ApplyState(const EffectState &state, uint32_t boundStage, EffectStateSink &sink, bool parametersOnly);
    void ApplySampler(int parameter, uint32_t stage, EffectStateSink &sink, bool parametersOnly);
    void ApplyShader(int shader, bool pixel, EffectStateSink &sink, bool parametersOnly);
    /** How a shader's constants are filled from this effect's parameters, worked out once per shader: one copy per
     * float register, and the sampler parameters whose states the shader's samplers take. */
    struct RegisterCopy
    {
        const uint32_t *source;
        uint16_t reg;
        int16_t parameter;
        uint8_t count;
        uint8_t stride;
        uint8_t type;
    };
    struct SamplerBinding
    {
        int16_t parameter;
        uint16_t stage;
    };
    /** Consecutive register copies of one parameter (a matrix's columns, an array's elements): a commit checks the
     * parameter's change serial once for them all. */
    struct RegisterRun
    {
        int16_t parameter;
        uint16_t first;
        uint16_t count;
        /** The registers they fill, [low, high). */
        uint16_t low;
        uint16_t high;
        /** The columns of a 4x4 float matrix into consecutive registers (column-major constants of a row-major
         * parameter): one SSE transpose instead of a strided copy per register. */
        bool transposed;
    };
    /** A parameter a shader reads and what of the shader it feeds (a bit per run and per sampler binding): a commit
     * checks each parameter's change serial once, without a branch. */
    struct ParameterWatch
    {
        const uint64_t *serial;
        uint32_t runs;
        uint32_t samplers;
        /** Nonzero when the parameter feeds a boolean register. */
        uint32_t bools;
    };
    /** A boolean register (b#) read from a parameter's value. */
    struct BoolRegister
    {
        const uint32_t *source;
        int16_t parameter;
        uint8_t reg;
        uint8_t type;
    };
    struct ConstantLayout
    {
        RegisterCopy *copies;
        uint32_t copyCount;
        RegisterRun *runs;
        uint32_t runCount;
        BoolRegister *bools;
        uint32_t boolCount;
        /** The boolean registers as last worked out from their parameters. */
        uint32_t boolBits;
        SamplerBinding *samplers;
        uint32_t samplerCount;
        uint32_t first;
        uint32_t end;
        /** Registers [first, end) as last set; only those whose parameters changed are filled again. */
        float (*registers)[4];
        /** The parameters the runs and samplers read; none when there are more than 32 of either (then every run and
         * sampler is checked on its own). */
        ParameterWatch *watches;
        uint32_t watchCount;
        uint32_t allRuns;
        uint32_t allSamplers;
        /** s_changes when the shader was last applied (built: ever), and the sink's constants stamp after that. */
        uint64_t applied;
        bool built;
        uint32_t stamp;
    };
    ConstantLayout &Layout(int shader);
    void CopyRun(const ConstantLayout &layout, const RegisterRun &run, float (*registers)[4]) const;
    /** Whether a parameter a sampler parameter's states read changed after serial `since`. */
    bool SamplerChanged(int parameter, uint64_t since);
    /** The states CommitChanges applies (they read parameters or set shaders, textures or samplers), per pass and
     * per sampler parameter: a count, then indices into EffectInfo::states. Worked out on first use. */
    const uint16_t *CommitList(uint16_t **&lists, uint32_t index, uint32_t listCount, uint32_t stateStart,
                               uint32_t stateCount);
    void ApplyPass(EffectStateSink &sink, bool parametersOnly);
    /** ApplyPassStates's states for a pass (its index in EffectInfo::passes, `first` the technique's first): a count,
     * then indices into EffectInfo::states. Worked out on first use. */
    const uint16_t *PassStates(uint32_t passIndex, uint32_t first);
    uint16_t **passStateLists_;

    const EffectInfo &info_;
    uint32_t *values_;
    /** Where each numeric parameter's value lives: in values_, or in the pool for shared ones. */
    uint32_t **slots_;
    void **textures_;
    /** Each parameter's last change as a serial of s_changes: in changes_, or in the pool for shared ones. */
    uint64_t **changedAt_;
    uint64_t *changes_;
    /** s_changes when the current pass was last applied. */
    uint64_t passApplied_;
    /** Counts parameter changes in every effect (shared parameters change in all effects that have them). */
    static uint64_t s_changes;
    ConstantLayout **layouts_;
    uint16_t **passCommitLists_;
    uint16_t **samplerCommitLists_;
    uint32_t passCount_;
    int technique_;
    int pass_;
    int vertexShader_;
    int pixelShader_;
};
} // namespace nfs::vk
