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


std::vector<SampleOnMesh> findSampleOnMesh (
    const TriQuadMesh *mesh,
    const Point2f    &sampleUV
);    

}  
#endif  