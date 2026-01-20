//pbrt4bounty
#include "point_cloud.h"

#include <pbrt/util/mesh.h>

#include <pbrt/util/buffercache.h>
#include <pbrt/util/check.h>
#include <pbrt/util/error.h>
#include <pbrt/util/log.h>
#include <pbrt/util/print.h>
#include <pbrt/util/stats.h>
#include <pbrt/util/transform.h>

#include <rply/rply.h>
#include <cstdio>
#include <string>

#include "pbrt/base/shape.h"
#include "pbrt/cpu/primitive.h"
#include "pbrt/materials.h"
#include "pbrt/pbrt.h"
#include "pbrt/shapes.h"
#include "pbrt/textures.h"
#include "pbrt/util/colorspace.h"
#include "pbrt/util/math.h"
#include "pbrt/util/pstd.h"
#include "pbrt/util/spectrum.h"

namespace pbrt {
// Global registry for colored point cloud shapes
std::map<Shape, RGB> coloredPointCloudShapes;
}  // namespace pbrt

using namespace pbrt;

PointCloud::Point::Point(Vector3f position) : transform(Translate(position)) {}

PointCloud::Point::Point(Vector3f position, const RGB &color)
    : transform(Translate(position)), colors({color}) {}

PointCloud::Point::Point(Vector3f position, const pstd::vector<RGB> &colors)
    : transform(Translate(position)), colors(colors) {}

const RGB &PointCloud::Point::operator[](size_t i) const {
    return colors[i];
}

RGB PointCloud::Point::color() const {
    if (colors.empty()) {
        // Return a default gray color if no colors are available
        LOG_VERBOSE("NO COLOR FOUND");
        return RGB(0.5f, 0.5f, 0.5f);
    }

    if (colors.size() == 1)
        return colors[0];

    RGB ret(0.0f, 0.0f, 0.0f);  // Initialize to black
    for (auto color : colors) {
        ret += color;
    }

    return ret / static_cast<Float>(colors.size());
}

std::string PointCloud::Point::ToString() const {
    std::string ret = "Position: " + transform.ToString() + "\n";

    for (const auto &color : colors)
        ret += "    Color reading: " + color.ToString() + "\n";

    return ret;
}

void rply_message_callback(p_ply ply, const char *message) {
    Warning("rply: %s", message);
}

int rply_vertex_callback(p_ply_argument argument) {
    Float *buffer;
    long index, flags;

    ply_get_argument_user_data(argument, (void **)&buffer, &flags);
    ply_get_argument_element(argument, nullptr, &index);

    int stride = (flags & 0x0F0) >> 4;
    int offset = flags & 0x00F;

    buffer[index * stride + offset] = (float)ply_get_argument_value(argument);

    return 1;
}

PointCloud PointCloud::ReadPLY(const std::string &filename) {
    PointCloud pcl;

    p_ply ply = ply_open(filename.c_str(), rply_message_callback, 0, nullptr);
    if (!ply)
        ErrorExit("Couldn't open PLY file \"%s\"", filename);

    if (ply_read_header(ply) == 0)
        ErrorExit("Unable to read the header of PLY file \"%s\"", filename);

    p_ply_element element = nullptr;
    size_t point_count;

    while ((element = ply_get_next_element(ply, element)) != nullptr) {
        const char *name;
        long nInstances;

        ply_get_element_info(element, &name, &nInstances);

        if (strcmp(name, "vertex") == 0) {
            point_count = nInstances;
            break;
        }
    }

    pcl.points.resize(point_count);

    // Buffers for position data
    std::vector<Float> positions(point_count * 3);

    // Set up position callbacks
    if (ply_set_read_cb(ply, "vertex", "x", rply_vertex_callback, positions.data(),
                        0x30) == 0 ||
        ply_set_read_cb(ply, "vertex", "y", rply_vertex_callback, positions.data(),
                        0x31) == 0 ||
        ply_set_read_cb(ply, "vertex", "z", rply_vertex_callback, positions.data(),
                        0x32) == 0) {
        ErrorExit("%s: Vertex coordinate properties (x,y,z) not found!", filename);
    }

    // Discover all color properties dynamically
    std::vector<std::string> color_props;
    std::map<std::string, std::vector<Float>> color_data;

    // Reset element iterator to find vertex element again
    element = nullptr;
    while ((element = ply_get_next_element(ply, element)) != nullptr) {
        const char *name;
        ply_get_element_info(element, &name, nullptr);

        if (strcmp(name, "vertex") == 0) {
            p_ply_property property = nullptr;
            while ((property = ply_get_next_property(element, property)) != nullptr) {
                const char *prop_name;
                ply_get_property_info(property, &prop_name, nullptr, nullptr, nullptr);

                std::string name_str(prop_name);
                // Look for color properties
                if (name_str.find("red") != std::string::npos ||
                    name_str.find("green") != std::string::npos ||
                    name_str.find("blue") != std::string::npos || name_str == "r" ||
                    name_str == "g" || name_str == "b") {
                    color_props.push_back(name_str);
                    color_data[name_str].resize(point_count);
                }
            }
            break;
        }
    }

    // Set up color callbacks
    for (const auto &color_prop : color_props) {
        ply_set_read_cb(ply, "vertex", color_prop.c_str(), rply_vertex_callback,
                        color_data[color_prop].data(), 0x10);  // stride=1, offset=0
    }

    // Read the data
    if (ply_read(ply) == 0) {
        ErrorExit("Unable to read data from PLY file \"%s\"", filename);
    }

    // Convert to Point objects with multiple RGB values
    for (size_t i = 0; i < point_count; ++i) {
        Vector3f pos(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);

        // Group color properties into RGB triplets
        pstd::vector<RGB> vertex_colors;

        // Simple grouping: assume properties come in r,g,b order
        for (size_t c = 0; c + 2 < color_props.size(); c += 3) {
            Float r = color_data[color_props[c]][i];
            Float g = color_data[color_props[c + 1]][i];
            Float b = color_data[color_props[c + 2]][i];

            // Normalize colors if they appear to be in 0-255 range
            if (r > 1.0f || g > 1.0f || b > 1.0f) {
                r /= 255.0f;
                g /= 255.0f;
                b /= 255.0f;
            }

            // Clamp to valid range and check for NaN
            r = Clamp(r, 0.0f, 1.0f);
            g = Clamp(g, 0.0f, 1.0f);
            b = Clamp(b, 0.0f, 1.0f);

            if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b)) {
                RGB color(r, g, b);
                vertex_colors.push_back(color);
            }
        }

        // Create Point with position and multiple colors
        if (vertex_colors.empty()) {
            pcl.points[i] = Point(pos);
        } else {
            pcl.points[i] = Point(pos, vertex_colors);
        }
    }

    ply_close(ply);
    return pcl;
}

pstd::vector<Shape> PointCloud::get_point_as_shapes(const Transform *renderFromObject,
                                                    const Transform *objectFromRender,
                                                    bool reverseOrientation,
                                                    Allocator alloc) const {
    pstd::vector<Shape> shapes;
    for (const auto &p : points) {
        // Compose the base transform with the point's local transform
        Transform *pointRenderFromObject =
            alloc.new_object<Transform>((*renderFromObject) * p.transform);
        Transform *pointObjectFromRender =
            alloc.new_object<Transform>(Inverse(p.transform) * (*objectFromRender));

        Shape sphere = alloc.new_object<Sphere>(pointRenderFromObject,
                                                pointObjectFromRender, reverseOrientation,
                                                pointSize, -pointSize, pointSize, 360);

        shapes.push_back(sphere);
    }

    return shapes;
}

pstd::vector<Shape> PointCloud::get_point_as_colored_shapes(
    const Transform *renderFromObject, const Transform *objectFromRender,
    bool reverseOrientation, Allocator alloc) const {
    pstd::vector<Shape> shapes;
    for (const auto &p : points) {
        // Compose the base transform with the point's local transform
        Transform *pointRenderFromObject =
            alloc.new_object<Transform>((*renderFromObject) * p.transform);
        Transform *pointObjectFromRender =
            alloc.new_object<Transform>(Inverse(p.transform) * (*objectFromRender));

        Shape sphere = alloc.new_object<Sphere>(pointRenderFromObject,
                                                pointObjectFromRender, reverseOrientation,
                                                pointSize, -pointSize, pointSize, 360);

        // Register the shape's color in the global registry
        RGB pointColor = p.color();
        coloredPointCloudShapes[sphere] = pointColor;

        shapes.push_back(sphere);
    }

    return shapes;
}

pstd::vector<Primitive> PointCloud::get_point_as_primitives(
    const Transform *renderFromObject, const Transform *objectFromRender,
    bool reverseOrientation, Allocator alloc) const {
    pstd::vector<Primitive> primitives;
    for (const auto &p : points) {
        // Compose the base transform with the point's local transform
        Transform *pointRenderFromObject =
            alloc.new_object<Transform>((*renderFromObject) * p.transform);
        Transform *pointObjectFromRender =
            alloc.new_object<Transform>(Inverse(p.transform) * (*objectFromRender));

        Shape sphere = alloc.new_object<Sphere>(pointRenderFromObject,
                                                pointObjectFromRender, reverseOrientation,
                                                pointSize, -pointSize, pointSize, 360);

        // Create a colored material for this point
        RGB pointColor = p.color();  // Get the averaged color from the point

        // Create a constant texture with the point's color
        Spectrum colorSpectrum =
            alloc.new_object<RGBAlbedoSpectrum>(*RGBColorSpace::sRGB, pointColor);
        SpectrumTexture colorTexture =
            alloc.new_object<SpectrumConstantTexture>(colorSpectrum);

        // Create a diffuse material with the point's color
        Material material =
            alloc.new_object<DiffuseMaterial>(colorTexture, nullptr, nullptr);

        // Create a primitive with the sphere and colored material
        Primitive primitive = alloc.new_object<SimplePrimitive>(sphere, material);
        primitives.push_back(primitive);
    }

    return primitives;
}
