#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace nx::acnh {

struct ShovelStageKey { uint32_t hash; };

namespace shovel_stage_keys {

inline constexpr std::array<uint32_t, 48> primary{{
    0x02d2ee90, 0x03c7d8fe, 0x06d43333, 0x0dda8806, 0x14bab176, 0x1bd63718,
    0x1cebca34, 0x25e91c5d, 0x26f16472, 0x30d8d3d4, 0x32eb2fc1, 0x34cf1ed9,
    0x3dbdab35, 0x41f58673, 0x4e0039f2, 0x4ef823a6, 0x63e13da0, 0x68d091e5,
    0x7445a94a, 0x824d30ac, 0x830de541, 0x8968a7c6, 0x8c6d9a6a, 0x9738b870,
    0x996a4644, 0x9fc1415d, 0xa0a04413, 0xa100c3ce, 0xa1a96120, 0xafbc5d2a,
    0xb1c35fbb, 0xb74ed2d0, 0xbe5708d1, 0xc435955c, 0xc909c3f7, 0xcc634f23,
    0xd01f8a43, 0xd037af4d, 0xe03b00c6, 0xe5b0a722, 0xed5d964e, 0xf4eeb16e,
    0xf7eaadf4, 0xfc17dd92, 0xfcb10f3d, 0xfcd6fc35, 0xfe65a862, 0xff5beae3,
}};

inline constexpr std::array<uint32_t, 16> secondary{{
    0x068001ae, 0x091dc718, 0x1a13a2ea, 0x25b43b89, 0x2633946a, 0x2aefed47,
    0x302056e3, 0x340da5a5, 0x356323a8, 0x6043fd87, 0x7dfa9ed2, 0x85b28224,
    0xc1dab6e4, 0xdeb88c07, 0xf1a102a6, 0xf9ce892d,
}};

inline constexpr std::array<uint32_t, 66> survey{{
    0x005f6c88, 0x01ac1236, 0x0427dece, 0x0c672fa7, 0x0e716ff8, 0x10cdc40c,
    0x11889761, 0x1693a871, 0x16f2dadc, 0x17f77b87, 0x1d096e7d, 0x28c45759,
    0x3411ee5c, 0x3b3bf38a, 0x3ee08254, 0x3fce6459, 0x483e4538, 0x4d702d55,
    0x593d97a5, 0x5fbb6b26, 0x60c2c4f0, 0x6148bcad, 0x6168e44d, 0x65b7732d,
    0x66eec322, 0x6c9ebc71, 0x7194bd96, 0x72a13146, 0x756daeec, 0x77870a84,
    0x7c939885, 0x7fff3f46, 0x84ade7bd, 0x852d1e56, 0x85a821af, 0x868a4718,
    0x8a17f484, 0x8b6c7dce, 0x8bcc8555, 0x91e12aa0, 0x97565ce7, 0x9933b287,
    0xa06ad792, 0xa3d02574, 0xa82b5146, 0xb8855020, 0xb88d07fc, 0xb8fe23a8,
    0xbe3cad72, 0xc1f6dfce, 0xd3c049f0, 0xde45e2c6, 0xde5987e4, 0xdf6636fe,
    0xe6986d28, 0xea021025, 0xedd6a80b, 0xee097098, 0xee15e824, 0xf156181d,
    0xf19cdbad, 0xf40e33f8, 0xf6e81207, 0xf70487f3, 0xf7c314c3, 0xfe96ade4,
}};

inline constexpr std::array<uint32_t, 6> survey_tail{{
    0x080e20d8, 0x0b6a5301, 0x1490248c, 0x2922957e, 0x51b9e069, 0x7a616e2f,
}};

}  // namespace shovel_stage_keys

constexpr bool shovel_stage_primary(ShovelStageKey key) {
    const auto& keys = shovel_stage_keys::primary;
    return std::binary_search(keys.begin(), keys.end(), key.hash);
}

constexpr bool shovel_stage_secondary(ShovelStageKey key) {
    const auto& keys = shovel_stage_keys::secondary;
    return std::binary_search(keys.begin(), keys.end(), key.hash);
}

template<class Services>
bool shovel_stage_matches(ShovelStageKey key, Services& services) {
    using namespace shovel_stage_keys;
    return services.primary(key) || std::binary_search(survey.begin(), survey.end(), key.hash) ||
        services.secondary(key) || std::binary_search(survey_tail.begin(), survey_tail.end(), key.hash);
}

}  // namespace nx::acnh
