// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0
//
// Copyright(c) 2025 Raj Nitin Gar
// This code is implemented in a fork created by https://github.com/rOnInRaJ-dev
// and added to this fork by Pedro A., pbrt4bounty@gmail.com
// It is assumed that it is under the same type of license as the original

#ifndef PBRT_PCGUTIL_FIND_SAMPLES_ON_MESH_H
#define PBRT_PCGUTIL_FIND_SAMPLES_ON_MESH_H

#include <vector>
#include <pbrt/util/mesh.h> 
#include <pbrt/util/vecmath.h> 

namespace pbrt {

typedef struct SampleOnMesh {
    Point3f p;
    Normal3f n;
} SampleOnMesh;

std::vector<SampleOnMesh> findSampleOnMesh(const TriQuadMesh *mesh,
                                           const Point2f &sampleUV);

}  // namespace pbrt
#endif  