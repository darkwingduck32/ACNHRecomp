#pragma once
#include <array>
#include <cmath>
#include <istream>
#include <string>

namespace nx::freecamera {
using FruitPoints = std::array<float, 9>;
struct FruitLayouts {
    FruitPoints oak{}, sakura{}, snow{};
    std::array<float, 6> palm{};
};
inline constexpr FruitPoints stock_oak_fruit{-6.f,16.8f,6.2f, 6.2f,16.f,6.9f, 0.f,24.5f,3.f};
inline constexpr std::array<float, 6> stock_palm_fruit{-2.5f,15.5f,3.2f, 2.6f,16.3f,2.3f};
inline bool read_fruit_layouts(std::istream& in, FruitLayouts& result) {
    FruitLayouts candidate;
    std::string token;
    if (!(in >> token) || token != "ACNH_TREE_FRUIT_V1") return false;
    auto row = [&](const char* name, auto& points) {
        if (!(in >> token) || token != name) return false;
        for (size_t i=0; i<points.size(); ++i) {
            if (!(in >> points[i]) || !std::isfinite(points[i])) return false;
            // Model-local fruit positions only: reject malformed or world-space data.
            if (std::fabs(points[i]) > 40 || (i%3==1 && points[i]<0)) return false;
        }
        return true;
    };
    if (!row("oak",candidate.oak) || !row("sakura",candidate.sakura) ||
        !row("snow",candidate.snow) || !row("palm",candidate.palm) || (in >> token)) return false;
    result=candidate;
    return true;
}
inline int fruit_variant(const std::string& path) {
    const auto slash=path.find_last_of("/\\");
    const std::string name=path.substr(slash==std::string::npos?0:slash+1);
    if (name=="PltTreeOak.Nin_NX_NVN.zs") return 0;
    if (name=="PltTreeOakSakura.Nin_NX_NVN.zs") return 1;
    if (name=="PltTreeOakSnow.Nin_NX_NVN.zs") return 2;
    return -1;
}
}
