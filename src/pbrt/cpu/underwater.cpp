// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// Modifications Copyright 2023 Intel Corporation.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

// Modifications added by Angelo Felipe in a pbrt-v4 fork
// Copyright(c) Angelo Felipe
// Included here by Pedro A. "povmaniac"

#if defined(PBRT_WITH_UNDERWATER)

#include <pbrt/cpu/integrators.h>

#include <pbrt/bsdf.h>
#include <pbrt/bssrdf.h>
#include <pbrt/cameras.h>
#include <pbrt/film.h>
#include <pbrt/filters.h>
#include <pbrt/interaction.h>
#include <pbrt/lights.h>
#include <pbrt/materials.h>
#include <pbrt/media.h>
#include <pbrt/options.h>
#include <pbrt/paramdict.h>
#include <pbrt/samplers.h>
#include <pbrt/shapes.h>
#include <pbrt/util/bluenoise.h>
#include <pbrt/util/check.h>
#include <pbrt/util/color.h>
#include <pbrt/util/colorspace.h>
#include <pbrt/util/containers.h>
#include <pbrt/util/display.h>
#include <pbrt/util/error.h>
#include <pbrt/util/file.h>
#include <pbrt/util/hash.h>
#include <pbrt/util/image.h>
#include <pbrt/util/lowdiscrepancy.h>
#include <pbrt/util/math.h>
#include <pbrt/util/memory.h>
#include <pbrt/util/parallel.h>
#include <pbrt/util/print.h>
#include <pbrt/util/progressreporter.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/rng.h>
#include <pbrt/util/sampling.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/stats.h>
#include <pbrt/util/string.h>
#include <pbrt/util/gui.h>

#include <algorithm>
#include <iostream>

// explicit underwater headers additions
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <vector>

namespace pbrt {

STAT_COUNTER("Integrator/Surface interactions", surfaceInteractions);
STAT_PERCENT("Integrator/Regularized BSDFs", regularizedBSDFs, totalBSDFs);

std::unique_ptr<UnderwaterIntegrator> UnderwaterIntegrator::Create(
    const ParameterDictionary &parameters, Camera camera, Sampler sampler,
    Primitive aggregate, std::vector<Light> lights, const FileLoc *loc) {
    // Settings struct
    UnderWaterSettings settings;
    // Parser phase
    // Policy: command line overrides the scene settings
    int maxDepth = parameters.GetOneInt("maxdepth", 5);
    std::string lightStrategy = parameters.GetOneString("lightsampler", "bvh");
    bool regularize = parameters.GetOneBool("regularize", false);
    // int rayMarchingStepsTemp = parameters.GetOneInt("raymarchingsteps", 16);

    // Multiple Scattering
    // Don't evaluates the multiple scattering light contribution.
    bool disableMS = parameters.GetOneBool("disablems", false);
    settings.disableMultipleScattering =
        Options->disableMultipleScattering ? true : disableMS;
    // Floor
    // Don't evaluates the floor reflectance in multiple scattering.
    bool disableFloor = parameters.GetOneBool("disablefloor", false);
    settings.disableFloorReflectanceInMS =
        settings.disableMultipleScattering ? true : disableFloor;
    // Don't evaluates the floor reflectance with bsdf in multiple scattering.
    bool disableFloorBsdf = parameters.GetOneBool("disablefloorbsdf", false);
    settings.disableFloorBsdfReflectanceInMS = disableMS ? true : disableFloorBsdf;

    // Single Scattering
    // Don't evaluates the single scattering light based on the surface contribution.
    bool disableSSSurface = parameters.GetOneBool("disablesssurface", false);
    settings.disableSingleScatteringSurface =
        Options->disableSingleScatteringSurface ? true : disableSSSurface;

    // Don't evaluates the single scattering light contribution
    bool disableSSVolume = parameters.GetOneBool("disablessvolume", false);
    settings.disableSingleScatteringVolume =
        Options->disableSingleScatteringVolume ? true : disableSSVolume;

    Float volumeUniformDistance = parameters.GetOneFloat("volumeuniformdistance", 7.f);
    settings.volumeUniformDistance =
        volumeUniformDistance < 0.f ? 7.f : volumeUniformDistance;

    int volumeUniformSPP = parameters.GetOneInt("volumeuniformspp", 16);
    settings.volumeUniformSPP = volumeUniformSPP < 0 ? 16 : volumeUniformSPP;

    int volumeVariableSPP = parameters.GetOneInt("volumevariablespp", 8);
    settings.volumeVariableSPP = volumeVariableSPP < 0 ? 8 : volumeVariableSPP;
    //
    bool disableSSVolumeUniform = parameters.GetOneBool("disablessvolumeuniform", false);
    if (settings.volumeUniformDistance == 0.f || settings.volumeUniformSPP == 0 ||
        settings.disableSingleScatteringVolume)
        disableSSVolumeUniform = true;
    settings.disableSingleScatteringVolumeUniform = disableSSVolumeUniform;

    // Don't evaluates the single scattering light based on the volume contribution
    bool disableSSVolumeVariable = parameters.GetOneBool("disablessvolumevariable", false);
    if (settings.volumeVariableSPP == 0 || settings.disableSingleScatteringVolume)
        disableSSVolumeVariable = true;
    settings.disableSingleScatteringVolumeVariable = disableSSVolumeVariable;

    // Evaluate light using only FastExp.
    bool fastExp = parameters.GetOneBool("enablefastexp", false);
    settings.fastExponentialOnly = Options->fastExponentialOnly ? true : fastExp;

    // TO DO: investigate the most suitable values and reasonable limits.
    // Caustics:
    // Don't evaluates the direct light with the caustic shader.
    bool disableCaustics = parameters.GetOneBool("disablecaustics", false);
    settings.disableCaustics = Options->disableCaustics ? true : disableCaustics;

    // If enabled, the time for the caustics in float; this will change their position.
    Float causticsTime = parameters.GetOneFloat("causticstime", 0.f);
    settings.causticsTime = Clamp(causticsTime, 0.f, 10.f);

    Float causticsFreq = parameters.GetOneFloat("causticsfrequency", 1.2f);
    settings.causticsFrequency = causticsFreq;

    Float causticsPower = parameters.GetOneFloat("causticspower", 9.0f);
    settings.causticsPower = Clamp(causticsPower, 1.f, 30.f);

    int causticsIterationss = parameters.GetOneInt("causticsiterations", 4);
    settings.causticsIterations = Clamp(causticsIterationss, 0, 6);

    Float causticsIntensity = parameters.GetOneFloat("causticsintensity", 0.005f);
    settings.Intensity = Clamp(causticsIntensity, 0.0025, 0.0075);

    Float ssCausticsMultiply = parameters.GetOneFloat("sscausticsmultiply", 10.0f);
    settings.ssCausticsMultiply = Clamp(ssCausticsMultiply, 1.f, 30.f);

    // Sun selection phase
    std::vector<Light> lightsExceptSun = lights;
    Light sunLight = nullptr;

    auto it = std::find_if(
        lightsExceptSun.begin(), lightsExceptSun.end(),
        [](const Light &l) { return l.Type() == LightType::DeltaDirection; });

    if (it != lightsExceptSun.end()) {
        sunLight = *it;
        // lightsExceptSun.erase(it);
    } else {
        Warning("[ No DistantLight found! To fully utilize the UnderwaterIntegrator's"
                "capabilities, it is recommended to specify a light of this type. ]\n\n");
    }
    return std::make_unique<UnderwaterIntegrator>(maxDepth, camera, sampler, aggregate,
                                                  settings, lights, sunLight,
                                                  lightStrategy, regularize);
}

void UnderwaterIntegrator::Render() {
    // Handle debugStart, if set
    if (!Options->debugStart.empty()) {
        std::vector<int> c = SplitStringToInts(Options->debugStart, ',');
        if (c.empty())
            ErrorExit("Didn't find integer values after --debugstart: %s",
                      Options->debugStart);
        if (c.size() != 3)
            ErrorExit("Didn't find three integer values after --debugstart: %s",
                      Options->debugStart);

        Point2i pPixel(c[0], c[1]);
        int sampleIndex = c[2];

        ScratchBuffer scratchBuffer(65536);
        Sampler tileSampler = samplerPrototype.Clone(Allocator());
        tileSampler.StartPixelSample(pPixel, sampleIndex);

        EvaluatePixelSample(pPixel, sampleIndex, tileSampler, scratchBuffer);

        return;
    }

    thread_local Point2i threadPixel;
    thread_local int threadSampleIndex;
    CheckCallbackScope _([&]() {
        return StringPrintf("Rendering failed at pixel (%d, %d) sample %d. Debug with "
                            "\"--debugstart %d,%d,%d\"\n",
                            threadPixel.x, threadPixel.y, threadSampleIndex,
                            threadPixel.x, threadPixel.y, threadSampleIndex);
    });

    // Declare common variables for rendering image in tiles
    ThreadLocal<ScratchBuffer> scratchBuffers([]() { return ScratchBuffer(); });

    ThreadLocal<Sampler> samplers([this]() { return samplerPrototype.Clone(); });

    Bounds2i pixelBounds = camera.GetFilm().PixelBounds();
    int spp = samplerPrototype.SamplesPerPixel();
    ProgressReporter progress(int64_t(spp) * pixelBounds.Area(), "Rendering",
                              Options->quiet);

    // UNDER_WATER INSERTION: Setup Statistics
    std::vector<double> perSampleTimes;
    Timer totalProcessTimer;  // Timer for the whole process
    int width = pixelBounds.Diagonal().x;
    int height = pixelBounds.Diagonal().y;  // Unused variable warning fix

    // Only allocate memory if statistics are enabled to save RAM
    bool enableStats = water.timeStatistics;

    if (enableStats) {
        size_t totalSamples =
            (size_t)pixelBounds.Area() * samplerPrototype.SamplesPerPixel();
        // Pre-allocate to allow thread-safe random access without locking
        perSampleTimes.resize(totalSamples, 0.0);
    }
    // UNDER_WATER INSERTION-END
    
    int waveStart = 0, waveEnd = 1, nextWaveSize = 1;

    if (Options->recordPixelStatistics)
        StatsEnablePixelStats(pixelBounds,
                              RemoveExtension(camera.GetFilm().GetFilename()));
    // Handle MSE reference image, if provided
    pstd::optional<Image> referenceImage;
    FILE *mseOutFile = nullptr;
    if (!Options->mseReferenceImage.empty()) {
        auto mse = Image::Read(Options->mseReferenceImage);
        referenceImage = mse.image;

        Bounds2i msePixelBounds =
            mse.metadata.pixelBounds
                ? *mse.metadata.pixelBounds
                : Bounds2i(Point2i(0, 0), referenceImage->Resolution());
        if (!Inside(pixelBounds, msePixelBounds))
            ErrorExit("Output image pixel bounds %s aren't inside the MSE "
                      "image's pixel bounds %s.",
                      pixelBounds, msePixelBounds);

        // Transform the pixelBounds of the image we're rendering to the
        // coordinate system with msePixelBounds.pMin at the origin, which
        // in turn gives us the section of the MSE image to crop. (This is
        // complicated by the fact that Image doesn't support pixel
        // bounds...)
        Bounds2i cropBounds(Point2i(pixelBounds.pMin - msePixelBounds.pMin),
                            Point2i(pixelBounds.pMax - msePixelBounds.pMin));
        *referenceImage = referenceImage->Crop(cropBounds);
        CHECK_EQ(referenceImage->Resolution(), Point2i(pixelBounds.Diagonal()));

        mseOutFile = FOpenWrite(Options->mseReferenceOutput);
        if (!mseOutFile)
            ErrorExit("%s: %s", Options->mseReferenceOutput, ErrorString());
    }

    // Connect to display server if needed
    if (!Options->displayServer.empty()) {
        Film film = camera.GetFilm();
        DisplayDynamic(film.GetFilename(), Point2i(pixelBounds.Diagonal()),
                       {"R", "G", "B"},
                       [&](Bounds2i b, pstd::span<pstd::span<float>> displayValue) {
                           int index = 0;
                           for (Point2i p : b) {
                               RGB rgb = film.GetPixelRGB(pixelBounds.pMin + p,
                                                          2.f / (waveStart + waveEnd));
                               for (int c = 0; c < 3; ++c)
                                   displayValue[c][index] = rgb[c];
                               ++index;
                           }
                       });
    }

    // Render image in waves
    while (waveStart < spp) {
        // Render current wave's image tiles in parallel
        ParallelFor2D(pixelBounds, [&](Bounds2i tileBounds) {
            // Render image tile given by _tileBounds_
            ScratchBuffer &scratchBuffer = scratchBuffers.Get();
            Sampler &sampler = samplers.Get();
            PBRT_DBG("Starting image tile (%d,%d)-(%d,%d) waveStart %d, waveEnd %d\n",
                     tileBounds.pMin.x, tileBounds.pMin.y, tileBounds.pMax.x,
                     tileBounds.pMax.y, waveStart, waveEnd);
            for (Point2i pPixel : tileBounds) {
                StatsReportPixelStart(pPixel);
                threadPixel = pPixel;
                // Render samples in pixel _pPixel_
                for (int sampleIndex = waveStart; sampleIndex < waveEnd; ++sampleIndex) {
                    threadSampleIndex = sampleIndex;
                    sampler.StartPixelSample(pPixel, sampleIndex);

                    EvaluatePixelSample(pPixel, sampleIndex, sampler, scratchBuffer);

                    // UNDER_WATER INSERTION: Timer per sample
                    Timer sampleTimer;
                    if (enableStats) {
                        double elapsed = sampleTimer.ElapsedSeconds();

                        // Calculate unique thread-safe index:
                        // (row * width + col) * total_spp + current_sample
                        int localX = pPixel.x - pixelBounds.pMin.x;
                        int localY = pPixel.y - pixelBounds.pMin.y;
                        size_t globalIndex =
                            ((size_t)localY * width + localX) * spp + sampleIndex;

                        // Store without mutex (thread-safe due to unique index)
                        perSampleTimes[globalIndex] = elapsed;
                    }
                    // UNDER_WATER INSERTION-END 
                    
                    scratchBuffer.Reset();
                }

                StatsReportPixelEnd(pPixel);
            }
            PBRT_DBG("Finished image tile (%d,%d)-(%d,%d)\n", tileBounds.pMin.x,
                     tileBounds.pMin.y, tileBounds.pMax.x, tileBounds.pMax.y);
            progress.Update((waveEnd - waveStart) * tileBounds.Area());
        });

        // Update start and end wave
        waveStart = waveEnd;
        waveEnd = std::min(spp, waveEnd + nextWaveSize);
        if (!referenceImage)
            nextWaveSize = std::min(2 * nextWaveSize, 64);
        if (waveStart == spp)
            progress.Done();

        // Optionally write current image to disk
        if (waveStart == spp || Options->writePartialImages || referenceImage) {
            LOG_VERBOSE("Writing image with spp = %d", waveStart);
            ImageMetadata metadata;
            metadata.renderTimeSeconds = progress.ElapsedSeconds();
            metadata.samplesPerPixel = waveStart;
            if (referenceImage) {
                ImageMetadata filmMetadata;
                Image filmImage =
                    camera.GetFilm().GetImage(&filmMetadata, 1.f / waveStart);
                ImageChannelValues mse =
                    filmImage.MSE(filmImage.AllChannelsDesc(), *referenceImage);
                fprintf(mseOutFile, "%d, %.9g\n", waveStart, mse.Average());
                metadata.MSE = mse.Average();
                fflush(mseOutFile);
            }
            if (waveStart == spp || Options->writePartialImages) {
                camera.InitMetadata(&metadata);
                camera.GetFilm().WriteImage(metadata, 1.0f / waveStart);
            }
        }
    }

    if (mseOutFile)
        fclose(mseOutFile);
    DisconnectFromDisplayServer();
    LOG_VERBOSE("Rendering finished");
}

// UnderwaterIntegrator Method Definitions
SampledSpectrum UnderwaterIntegrator::Li(RayDifferential ray, SampledWavelengths &lambda,
                                         Sampler sampler, ScratchBuffer &scratchBuffer,
                                         VisibleSurface *visibleSurf) const {
    // Declare state variables for volumetric path sampling - default of the
    // VolpathIntegrator
    SampledSpectrum L(0.f), beta(1.f), r_u(1.f), r_l(1.f);
    bool specularBounce = false, anyNonSpecularBounces = false;
    Float etaScale = 1;
    int bounce = 0;  // Using "bounce" instead of "depth," as other integrators do, to
                     // avoid confusion.

    LightSampleContext prevIntrContext;
    bool achievedWaterBoundary = false;

    // Setting variables to calculate ambient light
    Float distance = 0.f;
    // const int rayMarchingSteps = Options->rayMarchingSteps ? *Options->rayMarchingSteps
    // : rayMarchingSteps;
    Vector3f dirToSun;
    SampledSpectrum sunIrradiance = SampledSpectrum(0.f),
                    multipleScatteringL = SampledSpectrum(0.f);
    SampledSpectrum transmittance;
    pstd::optional<LightLiSample> sunSample =
        GetSunProps(sunIrradiance, dirToSun, lambda);

    UnderwaterMediumProperties mediumProps;
    pstd::optional<UnderwaterMediumProperties> mediumPropsOpt =
        waterMedium.SampleUnderwaterHomogeneousMedium(lambda);

    if (mediumPropsOpt) {
        mediumProps = *mediumPropsOpt;
    } else {
        ErrorExit("[ The waterMedium isn't of the type UnderwaterHomogeneousMedium, "
                  "therefore it isn't possible to obtain the correct properties. ]\n");
    }

    const SampledSpectrum &mediumScattering = mediumProps.sigma_s;
    const SampledSpectrum &mediumExtinction = mediumProps.sigma_t;

    // UNDER_WATER | ASSUMPTION: The medium must be the same for the first bounce, since
    // the camera is inside the waterboundary box that contains the "global" medium.
    if (ray.medium != waterMedium) {
        ErrorExit("[ UnderwaterIntegrator ]\n"
                  "[ Your camera's interface is not configured correctly. ]\n"
                  "[ Define the external medium as being the same as the internal medium "
                  "of your waterBoundary. ]\n"
                  "[ e.g.: MediumInterface \"\" \"yourMedium\" ]\n\n");
    }

    while (true) {
        // Sample segment of volumetric scattering path
        PBRT_DBG("%s\n", StringPrintf("Path tracer depth %d, current L = %s, beta = %s\n",
                                      depth, L, beta)
                             .c_str());
        pstd::optional<ShapeIntersection> si = Intersect(ray);

        if (!si) {
            break;
        }  // UNDER_WATER | ASSUMPTION: Did you find anything? Then undefined behavior
           // occurred. Return the current SampledSpectrum.
        SurfaceInteraction &isect = si->intr;

        distance = Length(ray.o - si->intr.p());  // Distance between the origin of the
                                                  // ray and the point of interaction.
        if (sunSample) {
            // ===================================================================================
            // UNDER_WATER | VOLUMETRIC MULTIPLE SCATTERING (Kd apparent optical property
            // applied)
            // ===================================================================================
            if (!water.disableMultipleScattering) {
                Float uStep = sampler.Get1D();
                uStep = uStep > 0.95f ? 0.95f : uStep;
                Point3f sampleRayPath = ray(uStep * distance);
                RayDifferential rayToGround(sampleRayPath, Vector3f(0.f, -1.f, 0.f),
                                            ray.time, ray.medium);
                pstd::optional<ShapeIntersection> siGround = Intersect(rayToGround);
                // SurfaceInteraction *intrGround = nullptr;
                BSDF bsdfGround;
                BSDF *bsdfGroundPointer = nullptr;
                if (siGround && !(siGround->intr.material.Is<ThinDielectricMaterial>() ||
                                  siGround->intr.material.Is<DielectricMaterial>())) {
                    // intrGround = &siGround->intr;
                    // if (/*!intrGround->*/ ) {
                    bsdfGround = siGround->intr.GetBSDF(rayToGround, lambda, camera,
                                                        scratchBuffer, sampler);
                    bsdfGroundPointer = &bsdfGround;
                    // }
                }

                multipleScatteringL = MultipleScatteringLight(
                    ray, *si, mediumProps, sunIrradiance, bsdfGroundPointer);

                multipleScatteringL = beta * multipleScatteringL;

                L += multipleScatteringL;

                if (water.disableSingleScatteringVolume)
                    break;
            }
        }

        // =========================================================================
        // UNDER_WATER | VOLUMETRIC SINGLE SCATTERING (Ray Marching)
        // =========================================================================
        // if ((bounce == 0 || water.singleScatteringVolumeAlways) && !water.disableSingleScatteringVolume)
        if ((bounce == 0 && !water.disableSingleScatteringVolume)) {
            if (distance > 0) {
                Float uniformStepSize = 0.f;
                Float variableStepSize = 0.f;

                if (water.volumeUniformSPP > 0) {
                    uniformStepSize = water.volumeUniformDistance / water.volumeUniformSPP;
                }
                if (water.volumeVariableSPP > 0) {
                    variableStepSize = distance / water.volumeVariableSPP;
                }

                Interaction newIntr;
                SampledSpectrum directLight;
                SampledSpectrum singleScatteringL(0.f);

                // bounty: both methods are return the same value.
                // Float g = mediumProps.phase.GetGAsymmetryParam();
                Float g = waterMedium.GetGAsymmetryParam();
                // TODO_UNDERWATER: Test if 'g' param affect the render result.
                HGPhaseFunction hGPhaseFunction(g);

                int stepsActuallyCounted = 0;

                auto volumeEvaluation = [&](Float stepSize, Float distanceVol,
                                            int rayMarchingSPP) -> void {
                    for (int i = 0; i < rayMarchingSPP; ++i) {
                        Float t = i * stepSize;
                        // Boundary check: don't march past the distance
                        if (t >= distanceVol) {
                            break;
                        }

                        Point3f pSample = ray(t);
                        Float distRayOriginToSample = t;

                        // Transmittance (Beer's Law): exp(-sigma_t * dist)
                        transmittance =
                            FastExp(-distRayOriginToSample * mediumExtinction);

                        newIntr = MediumInteraction(pSample, -ray.d, ray.time,
                                                    waterMedium, &hGPhaseFunction);
                        directLight = SampleLdUnderwater(newIntr, nullptr, lambda,
                                                         sampler, beta, r_u, mediumProps);

                        singleScatteringL += transmittance * directLight;
                        ++stepsActuallyCounted;
                    }
                };

                if (!water.disableSingleScatteringVolumeUniform) {
                    volumeEvaluation(uniformStepSize, distance, water.volumeUniformSPP);
                }

                if (!water.disableSingleScatteringVolumeVariable) {
                    volumeEvaluation(variableStepSize, distance, water.volumeVariableSPP);
                }

                // int nRayMarchingSteps2 = 1000;
                // for (int i = 0; i < nRayMarchingSteps2; ++i) {
                //     Float t = i * uniformStepSize;
                //     // Boundary check: don't march past the object
                //     if (t >= distance) { break; }
                //
                //     Point3f pSample = ray(t);
                //     Float distRayOriginToSample = t;
                //
                //     // Transmittance (Beer's Law): exp(-sigma_t * dist)
                //     transmittance = FastExp(-distRayOriginToSample * mediumExtinction);
                //
                //     newIntr = MediumInteraction(pSample, -ray.d, ray.time, waterMedium,
                //     &hGPhaseFunction); directLight = SampleLdUnderwater(newIntr,
                //     nullptr, lambda, sampler, beta, r_u,
                //                                      mediumProps);
                //
                //     singleScatteringL += transmittance * directLight;
                //     ++stepsActuallyCounted;
                // }

                // for (int i = 0; i < nRayMarchingSteps; ++i) {
                //     Float t = i * stepSize2;
                //     // Boundary check: don't march past the object
                //     if (t >= distance) { break; }
                //     // if (t >= distance) {
                //     //     t = (nRayMarchingSteps-1) * stepSize2;
                //     //     if (t >= distance) { break; }
                //     //     i = nRayMarchingSteps;
                //     // }
                //
                //     Point3f pSample = ray(t);
                //     Float distRayOriginToSample = t;
                //
                //     // Transmittance (Beer's Law): exp(-sigma_t * dist)
                //     transmittance = FastExp(-distRayOriginToSample * mediumExtinction);
                //
                //     newIntr = MediumInteraction(pSample, -ray.d, ray.time, waterMedium,
                //     &hGPhaseFunction); directLight = SampleLdUnderwater(newIntr,
                //     nullptr, lambda, sampler, beta, r_u,
                //                                      mediumProps);
                //
                //     singleScatteringL += transmittance * directLight;
                //     ++stepsActuallyCounted;
                // }

                if (stepsActuallyCounted > 0) {
                    // beta * normalizedSingleScattering
                    L += beta * (singleScatteringL / stepsActuallyCounted);
                }
            }
        }

        if (water.disableSingleScatteringSurface) {
            break;
        }

        // Handle surviving unscattered rays
        // Add emitted light at volume path vertex or from the environment
        transmittance = FastExp(-distance * mediumExtinction);

        // && !Options->disableSingleScatteringSurface
        // UNDER_WATER | ASSUMPTION: No one object beyond the boundary (e.g.: birds).
        if (isect.material && (isect.material.Is<ThinDielectricMaterial>() ||
                               isect.material.Is<DielectricMaterial>())) {
            // Accumulate contributions from infinite light sources
            for (const auto &light : infiniteLights) {
                if (SampledSpectrum Le = light.Le(ray, lambda); Le) {
                    if (bounce == 0 || specularBounce)
                        L += beta * Le * transmittance * mediumScattering / r_u.Average();
                    else {
                        // Add infinite light contribution using both PDFs with MIS
                        Float p_l = lightSampler.PMF(prevIntrContext, light) *
                                    light.PDF_Li(prevIntrContext, ray.d, true);
                        r_l *= p_l;
                        L += beta * Le * transmittance * mediumScattering /
                             (r_u + r_l).Average();
                    }
                }
            }
            achievedWaterBoundary = true;
        }

        // && !Options->disableSingleScatteringSurface
        if (SampledSpectrum Le = isect.Le(-ray.d, lambda); Le) {
            // Add contribution of emission from intersected surface
            if (bounce == 0 || specularBounce)
                L += beta * Le * transmittance * mediumScattering / r_u.Average();
            else {
                // Add surface light contribution using both PDFs with MIS
                Light areaLight(isect.areaLight);
                Float p_l = lightSampler.PMF(prevIntrContext, areaLight) *
                            areaLight.PDF_Li(prevIntrContext, ray.d, true);
                r_l *= p_l;
                L += beta * Le * transmittance * mediumScattering / (r_u + r_l).Average();
            }
        }

        // Get BSDF and skip over medium boundaries
        BSDF bsdf = isect.GetBSDF(ray, lambda, camera, scratchBuffer, sampler);
        if (!bsdf) {
            isect.SkipIntersection(&ray, si->tHit);
            continue;
        }

        // Initialize _visibleSurf_ at first intersection
        if (bounce == 0 && visibleSurf) {
            // Estimate BSDF's albedo
            // Define sample arrays _ucRho_ and _uRho_ for reflectance estimate
            constexpr int nRhoSamples = 16;
            const Float ucRho[nRhoSamples] = {
                0.75741637, 0.37870818, 0.7083487, 0.18935409, 0.9149363, 0.35417435,
                0.5990858,  0.09467703, 0.8578725, 0.45746812, 0.686759,  0.17708716,
                0.9674518,  0.2995429,  0.5083201, 0.047338516};
            const Point2f uRho[nRhoSamples] = {
                Point2f(0.855985, 0.570367), Point2f(0.381823, 0.851844),
                Point2f(0.285328, 0.764262), Point2f(0.733380, 0.114073),
                Point2f(0.542663, 0.344465), Point2f(0.127274, 0.414848),
                Point2f(0.964700, 0.947162), Point2f(0.594089, 0.643463),
                Point2f(0.095109, 0.170369), Point2f(0.825444, 0.263359),
                Point2f(0.429467, 0.454469), Point2f(0.244460, 0.816459),
                Point2f(0.756135, 0.731258), Point2f(0.516165, 0.152852),
                Point2f(0.180888, 0.214174), Point2f(0.898579, 0.503897)};

            SampledSpectrum albedo = bsdf.rho(isect.wo, ucRho, uRho);

            *visibleSurf = VisibleSurface(isect, albedo, lambda);
        }

        // Terminate path if maximum depth reached
        if (bounce++ >= maxDepth || achievedWaterBoundary) {
            return L;
        }

        ++surfaceInteractions;
        // Possibly regularize the BSDF
        if (regularize && anyNonSpecularBounces) {
            ++regularizedBSDFs;
            bsdf.Regularize();
        }

        // Sample illumination from lights to find attenuated path contribution
        if (IsNonSpecular(bsdf.Flags())) {
            L += transmittance * SampleLdUnderwater(isect, &bsdf, lambda, sampler, beta,
                                                    r_u, mediumProps);
            DCHECK(IsInf(L.y(lambda)) == false);
        }
        prevIntrContext = LightSampleContext(isect);

        // Sample BSDF to get new volumetric path direction
        Vector3f wo = isect.wo;
        Float u = sampler.Get1D();
        pstd::optional<BSDFSample> bs = bsdf.Sample_f(wo, u, sampler.Get2D());
        if (!bs)
            break;
        // Update _beta_ and rescaled path probabilities for BSDF scattering
        beta *= transmittance * bs->f * AbsDot(bs->wi, isect.shading.n) / bs->pdf;
        if (bs->pdfIsProportional)
            r_l = r_u / bsdf.PDF(wo, bs->wi);
        else
            r_l = r_u / bs->pdf;

        PBRT_DBG("%s\n", StringPrintf("Sampled BSDF, f = %s, pdf = %f -> beta = %s",
                                      bs->f, bs->pdf, beta)
                             .c_str());
        DCHECK(IsInf(beta.y(lambda)) == false);
        // Update volumetric integrator path state after surface scattering
        specularBounce = bs->IsSpecular();
        anyNonSpecularBounces |= !bs->IsSpecular();
        if (bs->IsTransmission())
            etaScale *= Sqr(bs->eta);
        ray = isect.SpawnRay(ray, bsdf, bs->wi, bs->flags, bs->eta);

        // Account for attenuated subsurface scattering, if applicable
        BSSRDF bssrdf = isect.GetBSSRDF(ray, lambda, camera, scratchBuffer);
        if (bssrdf && bs->IsTransmission()) {
            // Sample BSSRDF probe segment to find exit point
            Float uc = sampler.Get1D();
            Point2f up = sampler.Get2D();
            pstd::optional<BSSRDFProbeSegment> probeSeg = bssrdf.SampleSp(uc, up);
            if (!probeSeg)
                break;

            // Sample random intersection along BSSRDF probe segment
            uint64_t seed = MixBits(FloatToBits(sampler.Get1D()));
            WeightedReservoirSampler<SubsurfaceInteraction> interactionSampler(seed);
            // Intersect BSSRDF sampling ray against the scene geometry
            Interaction base(probeSeg->p0, ray.time, Medium());
            while (true) {
                Ray r = base.SpawnRayTo(probeSeg->p1);
                if (r.d == Vector3f(0, 0, 0))
                    break;
                pstd::optional<ShapeIntersection> si = Intersect(r, 1);
                if (!si)
                    break;
                base = si->intr;
                if (si->intr.material == isect.material)
                    interactionSampler.Add(SubsurfaceInteraction(si->intr), 1.f);
            }

            if (!interactionSampler.HasSample())
                break;

            // Convert probe intersection to _BSSRDFSample_
            SubsurfaceInteraction ssi = interactionSampler.GetSample();
            BSSRDFSample bssrdfSample =
                bssrdf.ProbeIntersectionToSample(ssi, scratchBuffer);
            if (!bssrdfSample.Sp || !bssrdfSample.pdf)
                break;

            // Update path state for subsurface scattering
            Float pdf = interactionSampler.SampleProbability() * bssrdfSample.pdf[0];
            beta *= bssrdfSample.Sp / pdf;
            r_u *= bssrdfSample.pdf / bssrdfSample.pdf[0];
            SurfaceInteraction pi = ssi;
            pi.wo = bssrdfSample.wo;
            prevIntrContext = LightSampleContext(pi);
            // Possibly regularize subsurface BSDF
            BSDF &Sw = bssrdfSample.Sw;
            anyNonSpecularBounces = true;
            if (regularize) {
                ++regularizedBSDFs;
                Sw.Regularize();
            } else
                ++totalBSDFs;

            // Account for attenuated direct illumination subsurface scattering
            L += transmittance *
                 SampleLdUnderwater(pi, &Sw, lambda, sampler, beta, r_u, mediumProps);

            // Sample ray for indirect subsurface scattering
            Float u = sampler.Get1D();
            pstd::optional<BSDFSample> bs = Sw.Sample_f(pi.wo, u, sampler.Get2D());
            if (!bs)
                break;
            beta *= bs->f * AbsDot(bs->wi, pi.shading.n) / bs->pdf;
            r_l = r_u / bs->pdf;
            // Don't increment depth this time...
            DCHECK(!IsInf(beta.y(lambda)));
            specularBounce = bs->IsSpecular();
            ray = RayDifferential(pi.SpawnRay(bs->wi));
        }

        // Possibly terminate volumetric path with Russian roulette
        if (!beta)
            break;
        SampledSpectrum rrBeta = beta * etaScale / r_u.Average();
        Float uRR = sampler.Get1D();
        PBRT_DBG("%s\n",
                 StringPrintf("etaScale %f -> rrBeta %s", etaScale, rrBeta).c_str());
        if (rrBeta.MaxComponentValue() < 1 && bounce > 1) {
            Float q = std::max<Float>(0, 1 - rrBeta.MaxComponentValue());
            if (uRR < q)
                break;
            beta /= 1 - q;
        }
    }

    return L;
}

SampledSpectrum UnderwaterIntegrator::SampleLdUnderwater(
    const Interaction &intr, const BSDF *bsdf, SampledWavelengths &lambda,
    Sampler sampler, SampledSpectrum beta, SampledSpectrum r_p,
    const UnderwaterMediumProperties &mediumProperties) const {
    // Estimate light-sampled direct illumination at _intr_
    // Initialize _LightSampleContext_ for volumetric light sampling
    LightSampleContext ctx;
    if (bsdf) {
        ctx = LightSampleContext(intr.AsSurface());
        // Try to nudge the light sampling position to correct side of the surface
        BxDFFlags flags = bsdf->Flags();
        if (IsReflective(flags) && !IsTransmissive(flags))
            ctx.pi = intr.OffsetRayOrigin(intr.wo);
        else if (IsTransmissive(flags) && !IsReflective(flags))
            ctx.pi = intr.OffsetRayOrigin(-intr.wo);

    } else
        ctx = LightSampleContext(intr);

    // Sample a light source using _lightSampler_
    Float u = sampler.Get1D();
    pstd::optional<SampledLight> sampledLight = lightSampler.Sample(ctx, u);
    Point2f uLight = sampler.Get2D();
    if (!sampledLight)
        return SampledSpectrum(0.f);
    Light light = sampledLight->light;
    DCHECK(light && sampledLight->p != 0);

    // Sample a point on the light source
    pstd::optional<LightLiSample> ls = light.SampleLi(ctx, uLight, lambda, true);
    if (!ls || !ls->L || ls->pdf == 0)
        return SampledSpectrum(0.f);
    Float p_l = sampledLight->p * ls->pdf;

    // Evaluate BSDF or phase function for light sample direction
    Float scatterPDF;
    SampledSpectrum f_hat;
    Vector3f wo = intr.wo, wi = ls->wi;
    if (bsdf) {
        // Update _f_hat_ and _scatterPDF_ accounting for the BSDF
        f_hat = bsdf->f(wo, wi) * AbsDot(wi, intr.AsSurface().shading.n);
        scatterPDF = bsdf->PDF(wo, wi);

    } else {
        // Update _f_hat_ and _scatterPDF_ accounting for the phase function
        CHECK(intr.IsMediumInteraction());
        // bounty: Seems that now, this access to _phase_ is OK..
        PhaseFunction phase = intr.AsMedium().phase;  // set but unused in orig.
        f_hat = SampledSpectrum(phase.p(wo, wi));     // commented in orig.
        scatterPDF = phase.PDF(wo, wi);               // commented in orig.
        // f_hat = SampledSpectrum(Inv4Pi);           // used in org.
        // scatterPDF = Inv4Pi;                       // used in org.
    }
    if (!f_hat)
        return SampledSpectrum(0.f);

    // Declare path state variables for ray to light source
    Ray lightRay = intr.SpawnRayTo(ls->pLight);
    SampledSpectrum T_ray(1.f), r_l(1.f), r_u(1.f);

    while (lightRay.d != Vector3f(0, 0, 0)) {
        // Trace ray through media to estimate transmittance
        pstd::optional<ShapeIntersection> si = Intersect(lightRay, 1 - ShadowEpsilon);

        // Handle opaque surface along ray's path
        if (si && si->intr.material &&
            !(si->intr.material.Is<ThinDielectricMaterial>() ||
              si->intr.material.Is<DielectricMaterial>())) {
            return SampledSpectrum(0.f);
        }

        // Generate next ray segment
        if (!si)
            break;
        lightRay = si->intr.SpawnRayTo(ls->pLight);
    }

    Float causticIntensity = 1.f;

    // Setting the variables for transmittance calculation.
    Vector3f vecToLight = ls->pLight.p() - intr.p();
    Float intrDistToLight = Length(vecToLight);

    // Adjusting variables if there is sun.
    if (light.Type() == LightType::DeltaDirection) {
        // Calculating the parameter t that reaches the waterBoundary
        Vector3f normalizedVecToSun = Normalize(vecToLight);
#ifdef NESTOR_WAY  // org. paper author Nestor code
        intrDistToLight =
            GetTParamIntersectPlaneWater(intr.p(), normalizedVecToSun, waterDepth / 2.5f);
#else  // Angelo Felipe way..?
        intrDistToLight =
            GetTParamIntersectPlaneWater(intr.p(), normalizedVecToSun, waterDepth);
#endif
        if (intrDistToLight == Infinity) {
            intrDistToLight = waterDepth;
        }
        // Calculating caustics using already stored information.
        causticIntensity =
            GetCaustics(intr.p(), normalizedVecToSun, intrDistToLight, water);
    }

    // Separating the properties for greater clarity.
    const SampledSpectrum &mediumScattering = mediumProperties.sigma_s;
    const SampledSpectrum &mediumExtinction = mediumProperties.sigma_t;
    const SampledSpectrum &mediumKd = mediumProperties.kd;

    // Update transmittance and caustics for current ray segment
    if (bsdf && light.Type() == LightType::DeltaDirection) {
        // If the interaction is with a surface, the transmittance will be related to the
        // Kd donwelling coefficient (vertical, from surface to interaction).
        Float depth = waterDepth - intr.p().y;
        T_ray = FastExp(-depth * mediumKd);
        if (!water.disableCaustics) {
            // Caustic settings from Gutierrez
            Float normalY = intr.AsSurface().shading.n.y;
            Float diffuseAmbient = SafeSqrt(Clamp(0.5f + 0.5f * normalY, 0.f, 1.f));
            f_hat *= (causticIntensity * 0.8f);
            f_hat *= diffuseAmbient;
            if (IsReflective(bsdf->Flags())) {
                f_hat *= (1.0f + causticIntensity * causticIntensity * 0.4f);
            }
        }
    } else if (light.Type() == LightType::DeltaDirection) {
        // If the interaction is with a medium, the transmittance will be related to the
        // extinction coefficient (sigma_t).
        T_ray = FastExp(-intrDistToLight * mediumExtinction);
        if (!water.disableCaustics) {
            // Caustic settings from Gutierrez
            // Apply the Godray sharpening power
            Float scaledCaustic = 0.2f * causticIntensity;
            causticIntensity = water.ssCausticsMultiply * std::pow(scaledCaustic, 5.0f);
        }
    } else {
        T_ray = FastExp(-intrDistToLight * mediumExtinction);
    }

    // Return path contribution function estimate for direct lighting
    r_l *= r_p * p_l;
    r_u *= r_p * scatterPDF;
    if (bsdf && light.Type() == LightType::DeltaDirection)
        return beta * f_hat * T_ray * ls->L / r_l.Average();
    else if (IsDeltaLight(light.Type()))
        return beta * f_hat * T_ray * mediumScattering * (ls->L * causticIntensity) /
               r_l.Average();
    else
        return beta * f_hat * T_ray * mediumScattering * (ls->L * causticIntensity) /
               (r_l + r_u).Average();
}

std::string UnderwaterIntegrator::ToString() const {
    return StringPrintf(
        "[ UnderwaterIntegrator maxDepth: %d lightSampler: %s regularize: %s ]", maxDepth,
        lightSampler, regularize);
}

void UnderwaterIntegrator::SettingWaterDepthAndMedium() {
    Point3f pos = Point3f(0.f, 0.f, 0.f);
    Vector3f dir = Vector3f(0.f, 1.f, 0.f);
    Ray ray{pos, dir};

    if (pstd::optional<ShapeIntersection> siWaterBoundary = IntersectWaterBoundary(ray)) {
        SurfaceInteraction &waterIntr = siWaterBoundary->intr;
        waterDepth = waterIntr.p().y;
        waterDepth += 0.01f;  // Adds an offset to resolve floating-point discrepancies.
                              // For different ray origins, the intersection point in the
                              // y-dimension may be slightly different; I am defining a
                              // value above the likely maximum value.

        if (bool isUnderwaterHomogeneousMedium =
                IsUnderwaterHomogeneousMedium(waterIntr)) {
            waterMedium = waterIntr.mediumInterface->inside;
            this->siWaterBoundary = *siWaterBoundary;
        } else {
            ErrorExit(
                "[ UnderwaterIntegrator ]\n"
                "[ Hit WaterBoundary but it has inside Medium different of the type"
                " \"underwaterhomogeneous\" (class UnderwaterHomogeneousMedium)! ]");
        }

    } else {
        ErrorExit(
            "[ UnderwaterIntegrator ]\n"
            "[ The constructor of the UnderwaterIntegrator didn't discover the water "
            "boundary material.\n"
            "The ray with global origin in (0 0 0) and direction to (0 1 0) should "
            "achieve a object with material of the class WaterBoundaryMaterial.\n"
            "This object should contain your MediumInterface with the inside Medium "
            "of the type \"underwaterhomogeneous\" (outside is air: \"\").\n"
            "Your scene should have a large box containing \"waterboundary\" material "
            "that holds the entire scene. ]\n\n");
    }
}

pstd::optional<ShapeIntersection> UnderwaterIntegrator::IntersectWaterBoundary(
    Ray ray) const {
    while (true) {
        pstd::optional<ShapeIntersection> si = Intersect(ray);
        if (!si)
            return {};

        const SurfaceInteraction &intr = si->intr;
        if (intr.material && (intr.material.Is<ThinDielectricMaterial>() ||
                              intr.material.Is<DielectricMaterial>())) {
            ShapeIntersection siCopy = *si;
            return siCopy;
        }

        ray = static_cast<Ray>(intr.SpawnRay(ray.d));
    }
}

bool UnderwaterIntegrator::IsUnderwaterHomogeneousMedium(const SurfaceInteraction &intr) {
    if (!intr.mediumInterface)
        return false;
    const Medium medium = intr.mediumInterface->inside;
    return medium && medium.Is<UnderwaterHomogeneousMedium>();
}

void UnderwaterIntegrator::SetOnePixelToRender() {
    if (Options->pixelBounds &&
        Options->pixelBounds->pMin.x == (Options->pixelBounds->pMax.x - 1) &&
        Options->pixelBounds->pMin.y == (Options->pixelBounds->pMax.y - 1)) {
        if (Options->pixelSamples && *Options->pixelSamples == 1)
            onePixelToRender = true;
    } else {
        onePixelToRender = false;
    }
}

pstd::optional<LightLiSample> UnderwaterIntegrator::GetSunProps(
    SampledSpectrum &sunIrradiance, Vector3f &dirToSun,
    const SampledWavelengths &lambda) const {
    if (sun) {
        LightSampleContext ctx = LightSampleContext(siWaterBoundary.intr);
        // Never used by the DistantLight, no need to real use of the Sampler
        Point2f uLight = Point2f(5.f, 5.f);
        pstd::optional<LightLiSample> ls = sun.SampleLi(ctx, uLight, lambda, true);
        if (ls && ls->L) {
            // TODO-UNDER_WATER: Multiply it by Inv4Pi turn it so small in the other
            // method? Verify after.
            sunIrradiance = ls->L;  // * Inv4Pi;
            dirToSun = Normalize(ls->pLight.p() - ctx.p());
            return ls;
        } else {
            Warning("[ Don't can sample the sun light. ]\n");
        }
    }
    sunIrradiance = SampledSpectrum(0.f);
    dirToSun = Vector3f(0.f, 0.f, 0.f);
    return {};
}

SampledSpectrum UnderwaterIntegrator::MultipleScatteringLight(
    const RayDifferential &ray, const ShapeIntersection &si,
    const UnderwaterMediumProperties &mediumProps, const SampledSpectrum &sunIrradiance,
    const BSDF *bsdf) const {
    // Implements Eq 9 https://doi.org/10.1111/cgf.15009
    // Note: unlike in the paper here y points up.
    // Note: This code follows the modified equation from the example code available in
    // the article's supplementary material. Official site:
    // https://graphics.unizar.es/projects/EG24Underwater/ Demo Code:
    // https://www.shadertoy.com/view/4cVGD3 Original Name in Demo: integrate_L_anim

    // Setting variables to calculate ambient light
    // Ray origin depth for this bounce.
    const Float rayDepth = waterDepth - ray.o.y;
    // Dot(invNormal, ray.d). Cosine with respect to the Vector3f(0.f, -1.f, 0.f)
    // [invNormal] which is the normal used.
    const Float woDotWi = -ray.d.y;
    // Distance between the origin of the ray and the point of interaction.
    const Float distance = Length(ray.o - si.intr.p());
    const SampledSpectrum &sigmaS = mediumProps.sigma_s;
    const SampledSpectrum &sigmaT = mediumProps.sigma_t;
    const SampledSpectrum &kd = mediumProps.kd;
    constexpr Float floorReflectance = 0.3f;
    const BSDF *bsdfFloorReflectance = bsdf;

    // Choosing between Exp and FastExp
    ExponentialType expType =
        water.fastExponentialOnly ? ExponentialType::Fast : ExponentialType::Classic;
    auto IfExp = [&](SampledSpectrum spectrum) -> SampledSpectrum {
        if (expType == ExponentialType::Classic) {
            return Exp(spectrum);
        }
        return FastExp(spectrum);
    };

    // A term that is used many times.
    SampledSpectrum kdCosPlusExt = kd * woDotWi + sigmaT;

    // Avoiding NaNs and infinite values when dividing by kdCosPlusExt.
    constexpr Float kDivEpsilon = (sizeof(Float) >= 8) ? 1e-9 : 1e-5;  // double : float
    for (int i = 0; i < NSpectrumSamples; ++i) {
        if (std::abs(kdCosPlusExt[i]) < kDivEpsilon) {
            kdCosPlusExt[i] = (kdCosPlusExt[i] >= 0) ? kDivEpsilon : -kDivEpsilon;
        }
    }

    SampledSpectrum spectralColor = (sigmaS * sunIrradiance) / (4 * Pi * kdCosPlusExt);

    SampledSpectrum attenuationByDepth = IfExp(-kd * rayDepth);

    // attenuationByDistance = 1 - Exp(kdCosPlusExt * (-distance));
    // attenuationByDistance = 1 - Exp(exponentVal);
    SampledSpectrum exponentVal = kdCosPlusExt * (-distance);
    // Checking and adjusting the exponent value within the loop before calling the
    // exponential function.
    SampledSpectrum attenuationByDistance;

    // Avoiding NaNs and infinite values when calculating the exponential.
    constexpr Float kMaxExpInput = (sizeof(Float) >= 8) ? 700.0 : 85.0;  // double : float
    for (int i = 0; i < NSpectrumSamples; ++i) {
        if (exponentVal[i] > kMaxExpInput) {
            attenuationByDistance[i] = 0.f;
        } else {
            if (expType == ExponentialType::Classic)
                attenuationByDistance[i] = 1.f - std::exp(exponentVal[i]);
            else
                attenuationByDistance[i] = 1.f - FastExp(exponentVal[i]);
        }
    }

    SampledSpectrum floorContribution;
    if (!water.disableFloorReflectanceInMS) {
        if (bsdfFloorReflectance && !water.disableFloorBsdfReflectanceInMS) {
            floorContribution =
                (*bsdfFloorReflectance / Pi) * IfExp(-kd * (2 * waterDepth - rayDepth));
        } else {
            floorContribution =
                (floorReflectance / Pi) * IfExp(-kd * (2 * waterDepth - rayDepth));
        }
    } else {
        floorContribution = SampledSpectrum(0.f);
    }

    // Inline expression
    // ((sigmaS * sunIrradiance) / (4 * Pi * kdCosPlusExt))*(1 - Exp(kdCosPlusExt *
    // (-distance)))*((Exp(-kd * rayDepth))+((floorReflectance / Pi) * Exp(-kd *
    // (2*waterDepth - rayDepth))))
    SampledSpectrum multipleScatteringLight =
        spectralColor * attenuationByDistance * (attenuationByDepth + floorContribution);

    return multipleScatteringLight;
}

// Returns the distance 't' along the ray to the water surface plane.
// Returns generic 'Infinity' if the ray is parallel, points away or is behind the vector.
Float UnderwaterIntegrator::GetTParamIntersectPlaneWater(const Point3f &origin,
                                                         const Vector3f &direction,
                                                         const Float &planeWater) {
    // Avoid division by zero (ray parallel to surface)
    if (std::abs(direction.y) < 1e-6f)
        return Infinity;

    Float t = (planeWater - origin.y) / direction.y;

    // Return Infinity if t < 0 (intersection is behind the ray)
    if (t < 0)
        return Infinity;

    return t;
}

// Very nice tileable caustics
// from @DaveHoskins https://www.shadertoy.com/view/MdlXz8
Float UnderwaterIntegrator::CausticPattern(const Point2f &p, const Float &time,
                                           const UnderWaterSettings &water) {
    Float tMod = time * 0.5f + 23.0f;

    Point2f i = p;
    Float c = 1.0f;

    for (int n = 0; n < water.causticsIterations; n++) {
        Float t = tMod * (1.0f - (3.5f / Float(n + 1)));

        Float newX = p.x + std::cos(t - i.x) + std::sin(t + i.y);
        Float newY = p.y + std::sin(t - i.y) + std::cos(t + i.x);
        i = Point2f(newX, newY);

        Vector2f divVec(p.x / (std::sin(i.x + t) / water.Intensity),
                        p.y / (std::cos(i.y + t) / water.Intensity));

        // Safety check to avoid division by zero if length is extremely small
        Float len = Length(divVec);
        if (len > 1e-6f)
            c += 1.0f / len;
        else
            c += 1.0f;  // Fallback
    }

    c /= Float(water.causticsIterations);
    c = 1.17f - std::pow(c, 1.4f);

    return std::pow(std::abs(c), 8.0f);
}

Float UnderwaterIntegrator::GetCaustics(const Point3f &p, const Vector3f &sunDir,
                                        const Float &tToSurface,
                                        const UnderWaterSettings &water) {
    // UNDER_WATER | ASSUMPTION: We assume tToSurface is valid (checked by the caller
    // logic) Calculate the point on the water surface
    Point3f pSurf = p + sunDir * tToSurface;

    Float Tau = 2 * Pi;
    Float time = water.causticsTime;
    // Apply frequency scaling
    pSurf *= (0.11f * water.causticsFrequency);

    // Mapping 3D (xz) to 2D
    Point2f pXZ(pSurf.x, pSurf.z);

    // Calculate UV argument
    Point2f uvArg = pXZ * Tau * 2.0f;

    // Apply Time shift
    Float uvX = std::fmod(time + uvArg.x, Tau) - 250.0f;
    Float uvY = std::fmod(time + uvArg.y, Tau) - 250.0f;
    Point2f uv(uvX, uvY);

    // First layer
    Float relative_speed = time * water.causticsSpeed;
    Float c = CausticPattern(uv, relative_speed, water);

    // Second layer (Simulating #if CAUSTICS > 1)
    Float uv2X = std::fmod(-0.4f * time + uvArg.x * 0.56f, Tau) - 250.0f;
    Float uv2Y = std::fmod(-0.7f * time + uvArg.y * 0.56f, Tau) - 250.0f;
    Point2f uv2(uv2X, uv2Y);

    c += 1.1f * CausticPattern(uv2, relative_speed * 1.1f, water);

    // Final power curve (Simulating #if CAUSTICS > 2)
    Float finalCaustic = c * water.causticsPower;

    return finalCaustic;
}

}  // namespace pbrt
#endif
