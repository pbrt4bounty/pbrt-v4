#include "../util/mesh.h"  // for TriQuadMesh
#include <vector>
#include <limits>
#include <pbrt/pbrt.h>
#include <pbrt/shapes.h>
#include <pbrt/util/mesh.h>


namespace pbrt {

typedef struct SampleOnMesh {
    Point3f p;
    Normal3f n;
} SampleOnMesh;
    
std::vector<SampleOnMesh> findSampleOnMesh (
    const TriQuadMesh *mesh,
    const Point2f    &sampleUV
) {
    std::vector<SampleOnMesh> results;
    const auto &idx = mesh->triIndices;
    const auto &uvs = mesh->uv;
    const auto &P   = mesh->p;

    // Optional per-vertex normals (same length as P) 
    const bool hasVertexNormals = (mesh->n.size() == mesh->p.size());

    int nTris = idx.size()/3;
    results.reserve(1);

    for (int t = 0; t < nTris; ++t) {
        int v0 = idx[3*t + 0],
            v1 = idx[3*t + 1],
            v2 = idx[3*t + 2];

        // fetch UVs
        Point2f uv0 = uvs[v0], uv1 = uvs[v1], uv2 = uvs[v2];
        Vector2f d1 = uv1 - uv0, d2 = uv2 - uv0, duv = sampleUV - uv0;
        Float det = d1.x*d2.y - d2.x*d1.y;
        if (std::abs(det) < std::numeric_limits<Float>::epsilon())
            continue;
        Float invDet = 1/det;
        Float b = (duv.x*d2.y - d2.x*duv.y)*invDet;
        Float g = (d1.x*duv.y - duv.x*d1.y)*invDet;
        if (b < 0 || g < 0 || b+g > 1) 
            continue;

        // interpolate position
        Float a = 1 - b - g;
        Point3f p0 = P[v0], p1 = P[v1], p2 = P[v2];
        Point3f p  = a*p0 + b*p1 + g*p2;

        // compute normal
        Normal3f n;
        if (hasVertexNormals) {
            // smooth interpolation of vertex normals
            Normal3f n0 = mesh->n[v0],
                      n1 = mesh->n[v1],
                      n2 = mesh->n[v2];
            n = Normalize(a*n0 + b*n1 + g*n2);
        } else {
            // flat: face normal
            Vector3f e1 = p1 - p0, e2 = p2 - p0;
            n = Normalize(Normal3f(Cross(e1, e2)));
        }

        results.push_back({p, n});
    }
    
    return results;
}

}  