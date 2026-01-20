// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0
//
// Copyright(c) 2025 Raj Nitin Gar
// This code is implemented in a fork created by https://github.com/rOnInRaJ-dev
// and added to this fork by Pedro A., pbrt4bounty@gmail.com
// It is assumed that it is under the same type of license as the original

#ifndef PBRT_EXPORTER
#define PBRT_EXPORTER

#include "procedural.h"

#include <vector>
#include <pbrt/util/vecmath.h>
#include <pbrt/util/transform.h>


namespace pbrt {

class PBRTExporter {
  public:
    PBRTExporter(Procedural &procedural);

    void exportInstances(std::vector<Transform> instanceTransforms,
                         std::string outputFile);

  private:
    Procedural &procedural;
};

}  // namespace pbrt

#endif
