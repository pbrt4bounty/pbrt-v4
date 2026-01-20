// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0
//
// Copyright(c) 2025 Raj Nitin Gar
// This code is implemented in a fork created by https://github.com/rOnInRaJ-dev
// and added to this fork by Pedro A., pbrt4bounty@gmail.com
// It is assumed that it is under the same type of license as the original

#ifndef PBRT_PROCEDURAL_MESH
#define PBRT_PROCEDURAL_MESH

#include <string>
#include <pbrt/util/vecmath.h>

namespace pbrt {

class Procedural {
  public:
    Procedural(const std::string &filepath,
               const std::string &namedMaterial = "",  // Optional
               const std::string &materialType = "", const std::string &texture = "",
               const std::string &bumpMap = "", const std::string &normalMap = "",
               const std::string &opacityMap = "");

    std::string constructPbrtShapeBlock();
    std::string constructPbrtMaterialBlock();
    std::string addOpacityBlock();

  private:
    std::string filepath;
    std::string namedMaterial;
    std::string materialType;
    std::string texture;
    std::string bumpMap;
    std::string normalMap;
    std::string opacityMap;
};
}  // namespace pbrt

#endif 