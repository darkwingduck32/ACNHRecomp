#include "freecamera_fog.h"
#include "platform.h"
#include <nx/runtime.h>
#include <fstream>
#include <unordered_set>

namespace nx {
namespace { std::unordered_set<std::string> fog_shaders; }
void freecamera_fog_install(bool enabled) {
    if (!enabled) return;
    std::ifstream input(plat::data_root()+"/mods/freecamera/view_fog_shaders.txt");
    std::string word;
    if (!(input>>word) || word!="ACNH_VIEW_FOG_V1") return;
    std::unordered_set<std::string> candidate;
    while (input>>word) {
        if (word.size()!=16 || word.find_first_not_of("0123456789abcdef")!=std::string::npos || candidate.size()>20000) {
            log("freecamera: invalid view-fog shader profile; native fog retained");
            return;
        }
        candidate.insert(word);
    }
    if (input.bad()) {
        log("freecamera: unreadable view-fog shader profile; native fog retained");
        return;
    }
    fog_shaders=std::move(candidate);
    log("freecamera: view-relative fog enabled for %zu profiled Park UBER vertex shaders",fog_shaders.size());
}
bool freecamera_fog_shader(const std::string& hash) { return fog_shaders.contains(hash); }
}
