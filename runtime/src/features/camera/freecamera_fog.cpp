#include "native.h"
#include "version_303.h"
#include "freecamera_fog.h"
#include "platform.h"
#include <nx/runtime.h>
#include <sstream>
#include <unordered_set>

namespace nx {
namespace { std::unordered_set<std::string> fog_shaders, scattering_shaders; }
void freecamera_fog_install(bool enabled) {
    if (!enabled) return;
    std::istringstream input{std::string(camera_profile("view_fog_shaders.txt"))};
    std::string word;
    if (!(input>>word) || (word!="ACNH_VIEW_FOG_V1" && word!="ACNH_VIEW_FOG_V2")) return;
    const bool staged=word=="ACNH_VIEW_FOG_V2";
    std::unordered_set<std::string> candidate, fragments;
    while (input>>word) {
        bool fragment=false;
        if (staged) {
            if (word!="vertex" && word!="fragment") return;
            fragment=word=="fragment";
            if (!(input>>word)) return;
        }
        if (word.size()!=16 || word.find_first_not_of("0123456789abcdef")!=std::string::npos || candidate.size()+fragments.size()>20000) {
            log("freecamera: invalid view-fog shader profile; native fog retained");
            return;
        }
        (fragment?fragments:candidate).insert(word);
    }
    if (input.bad()) {
        log("freecamera: unreadable view-fog shader profile; native fog retained");
        return;
    }
    fog_shaders=std::move(candidate);
    scattering_shaders=std::move(fragments);
    log("freecamera: view-relative fog enabled for %zu vertex and %zu fragment shaders",fog_shaders.size(),scattering_shaders.size());
}
bool freecamera_fog_shader(const std::string& hash, bool fragment) {
    return (fragment?scattering_shaders:fog_shaders).contains(hash);
}
}
