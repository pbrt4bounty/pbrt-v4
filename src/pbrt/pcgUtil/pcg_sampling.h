// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0
//
// Copyright(c) 2025 Raj Nitin Gar
// This code is implemented in a fork created by https://github.com/rOnInRaJ-dev
// and added to this fork by Pedro A., pbrt4bounty@gmail.com
// It is assumed that it is under the same type of license as the original

#ifndef PBRT_PCGUTIL_PCG_SAMPLING_H
#define PBRT_PCGUTIL_PCG_SAMPLING_H

#include <tuple>
#include <vector>
#include <string>
#include <pbrt/util/sampling.h>
#include <pbrt/util/vecmath.h>

namespace pbrt {

class PCGSampling {
  public:
    std::tuple<std::vector<float>, int, int> loadDensityMap(const std::string &filename);

    std::vector<Point2f> sampleUVValues(
        const std::tuple<std::vector<float>, int, int> &densityMapData, int nSamples);
};

}  // namespace pbrt

#endif  