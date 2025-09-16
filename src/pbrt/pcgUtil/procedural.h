#ifndef PBRT_PROCEDURAL_MESH
#define PBRT_PROCEDURAL_MESH

#include <string>
#include <pbrt/util/vecmath.h>

namespace pbrt {
    
    class Procedural {
        public:
        Procedural(const std::string& filepath,
                const std::string &namedMaterial = "", // Optional 
                const std::string &materialType = "", 
                const std::string &texture = "",
                const std::string &bumpMap = "",
                const std::string &normalMap = "",
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
}

#endif 