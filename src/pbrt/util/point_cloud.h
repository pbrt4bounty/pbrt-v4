// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0
//
// Copyright(c) 2025 Lucas Rocha, 'Kitsai'
// This code is implemented in a fork created by https://github.com/Kitsai/pbrt-PC
// and added to this fork by Pedro A., pbrt4bounty@gmail.com
// It is assumed that it is under the same type of license as the original

#pragma once

#include <pbrt/pbrt.h>

#include <pbrt/base/material.h>
#include <pbrt/base/shape.h>
#include <pbrt/cpu/primitive.h>
#include <pbrt/util/color.h>
#include <pbrt/util/containers.h>
#include <pbrt/util/error.h>
#include <pbrt/util/hash.h>
#include <pbrt/util/parallel.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/vecmath.h>

#include <map>
#include <cstddef>
#include <string>

#include "pbrt/util/transform.h"

namespace pbrt {

// Global registry for colored point cloud shapes
extern std::map<Shape, RGB> coloredPointCloudShapes;

struct PointCloud {
    enum class PointRenderMode {
        Spheres,  // Small spheres (volumetric)
        Cubes     // Small voxels
    };

    struct Point {
        Point() = default;
        Point(Vector3f position);
        Point(Vector3f position, const RGB &color);
        Point(Vector3f position, const pstd::vector<RGB> &colors);

        Transform transform;
        pstd::vector<RGB> colors;

        std::string ToString() const;

        const RGB &operator[](size_t i) const;

        RGB color() const;
    };

    static PointCloud ReadPLY(const std::string &filename);

    void WritePLY(const std::string &filename) const;

    void cat() const;

    void info() const;

    pstd::vector<Point> points;

    PointRenderMode pointShape = PointRenderMode::Spheres;
    Float pointSize = 0.001f;

    pstd::vector<Shape> get_point_as_shapes(const Transform *renderFromObject,
                                            const Transform *objectFromRender,
                                            bool reverseOrientation,
                                            Allocator alloc) const;

    pstd::vector<Shape> get_point_as_colored_shapes(const Transform *renderFromObject,
                                                    const Transform *objectFromRender,
                                                    bool reverseOrientation,
                                                    Allocator alloc) const;

    pstd::vector<Primitive> get_point_as_primitives(const Transform *renderFromObject,
                                                    const Transform *objectFromRender,
                                                    bool reverseOrientation,
                                                    Allocator alloc) const;
};
}  // namespace pbrt
