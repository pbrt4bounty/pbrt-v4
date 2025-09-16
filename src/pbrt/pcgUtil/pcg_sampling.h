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

    std::tuple<std::vector<float>, int, int>
    loadDensityMap(const std::string &filename);

    std::vector<Point2f>
    sampleUVValues(const std::tuple<std::vector<float>, int, int> &densityMapData,
                   int nSamples);
};

}

#endif  