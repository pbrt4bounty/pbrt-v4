#include "pcg_sampling.h"

#include <pbrt/util/image.h>
#include <pbrt/util/pstd.h>

#include <cstdlib> 
#include <tuple>

namespace pbrt {

std::tuple<std::vector<float>, int, int>
PCGSampling::loadDensityMap(const std::string &filename) {
    auto imgData = Image::Read(filename);
    auto &image = imgData.image;

    int nu = image.Resolution().y;
    int nv = image.Resolution().x;
    std::vector<float> densities;
    densities.reserve(nu * nv);

    for (int y = 0; y < nu; ++y) {
        for (int x = 0; x < nv; ++x) {
            densities.push_back(image.GetChannel({x, y}, 0));
        }
    }
    return {densities, nu, nv};
}

std::vector<Point2f>
PCGSampling::sampleUVValues(
    const std::tuple<std::vector<float>, int, int> &densityMapData,
    int nSamples
) {
    auto &densityVec = std::get<0>(densityMapData);
    int nu = std::get<1>(densityMapData);
    int nv = std::get<2>(densityMapData);

    Bounds2f domain(Point2f(0, 0), Point2f(1, 1));
    pstd::span<const Float> densitySpan(densityVec);
    PiecewiseConstant2D sampler(densitySpan, nu, nv, domain);

    std::vector<Point2f> samples;
    samples.reserve(nSamples);
    for (int i = 0; i < nSamples; ++i) {
        Float u = (Float)std::rand() / RAND_MAX;
        Float v = (Float)std::rand() / RAND_MAX;
        samples.push_back(sampler.Sample(Point2f(u, v)));
    }
    return samples;
}

} 