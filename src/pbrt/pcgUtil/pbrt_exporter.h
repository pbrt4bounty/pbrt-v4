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

}

#endif
