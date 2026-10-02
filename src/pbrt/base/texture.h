// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_BASE_TEXTURE_H
#define PBRT_BASE_TEXTURE_H

#include <pbrt/pbrt.h>

#include <pbrt/util/taggedptr.h>

#include <string>

namespace pbrt {

struct TextureEvalContext;

class FloatConstantTexture;
class FloatBilerpTexture;
class FloatCheckerboardTexture;
class FloatDotsTexture;
class FBmTexture;
class GPUFloatImageTexture;
class FloatImageTexture;
class FloatMixTexture;
class FloatDirectionMixTexture;
class FloatPtexTexture;
class GPUFloatPtexTexture;
class FloatScaledTexture;
class WindyTexture;
class WrinkledTexture;

#if defined(PBRT_WITH_OSL)
class OSLFloatTexture;
#endif

// FloatTexture Definition
class FloatTexture
    : public TaggedPointer<  // FloatTextures
          FloatImageTexture, GPUFloatImageTexture, FloatMixTexture,
          FloatDirectionMixTexture, FloatScaledTexture, FloatConstantTexture,
          FloatBilerpTexture, FloatCheckerboardTexture, FloatDotsTexture, FBmTexture,
          FloatPtexTexture, GPUFloatPtexTexture, WindyTexture, WrinkledTexture
#if defined(PBRT_WITH_OSL)
          ,
          OSLFloatTexture
#endif
          > {
  public:
    // FloatTexture Interface
    using TaggedPointer::TaggedPointer;

    static FloatTexture Create(const std::string &name,
                               const Transform &renderFromTexture,
                               const TextureParameterDictionary &parameters,
                               const FileLoc *loc, Allocator alloc, bool gpu);

    std::string ToString() const;

    PBRT_CPU_GPU inline Float Evaluate(TextureEvalContext ctx) const;
};

class RGBConstantTexture;
class RGBReflectanceConstantTexture;
class SpectrumConstantTexture;
class SpectrumBilerpTexture;
class SpectrumCheckerboardTexture;
class SpectrumImageTexture;
class GPUSpectrumImageTexture;
class MarbleTexture;
class SpectrumMixTexture;
class SpectrumDirectionMixTexture;
class SpectrumDotsTexture;
class SpectrumPtexTexture;
class GPUSpectrumPtexTexture;
class SpectrumScaledTexture;

#if defined(PBRT_WITH_OSL)
class OSLSpectrumTexture;
#endif

// SpectrumTexture Definition
class SpectrumTexture
    : public TaggedPointer<  // SpectrumTextures
          SpectrumImageTexture, GPUSpectrumImageTexture, SpectrumMixTexture,
          SpectrumDirectionMixTexture, SpectrumScaledTexture, SpectrumConstantTexture,
          SpectrumBilerpTexture, SpectrumCheckerboardTexture, MarbleTexture,
          SpectrumDotsTexture, SpectrumPtexTexture, GPUSpectrumPtexTexture
#if defined(PBRT_WITH_OSL)
          ,
          OSLSpectrumTexture
#endif
          > {
  public:
    // SpectrumTexture Interface
    using TaggedPointer::TaggedPointer;

    static SpectrumTexture Create(const std::string &name,
                                  const Transform &renderFromTexture,
                                  const TextureParameterDictionary &parameters,
                                  SpectrumType spectrumType, const FileLoc *loc,
                                  Allocator alloc, bool gpu);

    std::string ToString() const;

    PBRT_CPU_GPU inline SampledSpectrum Evaluate(TextureEvalContext ctx,
                                                 SampledWavelengths lambda) const;
};

}  // namespace pbrt

#endif  // PBRT_BASE_TEXTURE_H
