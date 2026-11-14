#include "nvn_spirv_instance.h"
#include <algorithm>
#include <map>

namespace nx::nvn {
namespace {
struct Inst { size_t at; uint32_t op,n; };
bool link_index(const std::vector<uint32_t>& in,bool fragment,bool generated_index,std::vector<uint32_t>& out) {
    if (in.size()<5 || in[0]!=0x07230203) return false;
    std::vector<Inst> instructions;
    std::map<uint32_t,std::vector<uint32_t>> types;
    std::map<uint32_t,uint32_t> variables,locations;
    uint32_t entry=0,index_var=0,i32=0,entries=0;
    size_t first_type=in.size(),first_function=in.size();
    for (size_t i=5;i<in.size();) {
        const auto* w=&in[i]; uint32_t n=w[0]>>16,op=w[0]&65535;
        if (!n || n>in.size()-i) return false;
        instructions.push_back({i,op,n});
        if (op==15) {
            if (n<4 || ++entries!=1 || w[1]!=(fragment?4u:0u)) return false;
            entry=w[2];
        }
        if (op>=19 && op<=39) first_type=std::min(first_type,i);
        if (op>=19 && op<=32 && n>=2) types[w[1]]=std::vector<uint32_t>(w,w+n);
        if (op==21 && n==4 && w[2]==32 && w[3]==1) i32=w[1];
        if (op==59 && n>=4) variables[w[2]]=w[1];
        if (op==54) first_function=std::min(first_function,i);
        if (op==71 && n==4) {
            if (w[2]==30) locations[w[1]]=w[3];
            if (w[2]==11 && w[3]==43) {
                if (index_var || !generated_index) return false;
                index_var=w[1];
            }
        }
        if (op==72 && n>=4 && w[3]==30) return false; // interface blocks need span analysis
        i+=n;
    }
    if (!entry || first_function==in.size() || first_type==in.size() || (fragment && !index_var)) return false;
    for (auto [var,location]:locations) {
        if (location>=kInstanceMaterialLocation || !variables.count(var)) return false;
        const auto& ptr=types[variables[var]];
        if (ptr.size()!=4 || (ptr[0]&65535)!=32) return false;
        const auto& value=types[ptr[3]];
        if (value.size()<3) return false;
        uint32_t scalar=ptr[3];
        if ((value[0]&65535)==23) {
            if (value.size()!=4 || value[3]>4) return false;
            scalar=value[2];
        }
        const auto& t=types[scalar];
        if (t.size()<3 || ((t[0]&65535)!=21 && (t[0]&65535)!=22) || t[2]!=32) return false;
    }
    std::vector<uint32_t> declarations;
    uint32_t bound=in[3],output=0;
    if (!i32) { i32=bound++; declarations.insert(declarations.end(),{(4u<<16)|21,i32,32,1}); }
    const bool new_input=!index_var;
    if (index_var) {
        if (!variables.count(index_var)) return false;
        const auto& p=types[variables[index_var]];
        if (p.size()!=4 || (p[0]&65535)!=32 || p[2]!=1 || p[3]!=i32) return false;
    } else {
        uint32_t ptr=bound++; index_var=bound++;
        declarations.insert(declarations.end(),{(4u<<16)|32,ptr,1,i32,(4u<<16)|59,ptr,index_var,1});
    }
    if (!fragment) {
        uint32_t ptr=bound++; output=bound++;
        declarations.insert(declarations.end(),{(4u<<16)|32,ptr,3,i32,(4u<<16)|59,ptr,output,3});
    }
    std::vector<uint32_t> result(in.begin(),in.begin()+5);
    bool decorations=false,declared=false,in_entry=false,pending=false,stored=false;
    auto decorate=[&]() {
        if (fragment) result.insert(result.end(),{(4u<<16)|71,index_var,30,kInstanceMaterialLocation,(3u<<16)|71,index_var,14});
        else {
            if (new_input) result.insert(result.end(),{(4u<<16)|71,index_var,11,43});
            result.insert(result.end(),{(4u<<16)|71,output,30,kInstanceMaterialLocation,(3u<<16)|71,output,14});
        }
    };
    for (const auto& inst:instructions) {
        const auto* w=&in[inst.at];
        if (!decorations && inst.at>=first_type) { decorate(); decorations=true; }
        if (!declared && inst.at>=first_function) {
            result.insert(result.end(),declarations.begin(),declarations.end()); declared=true;
        }
        if (inst.op==15) {
            const uint32_t extra=uint32_t(new_input)+uint32_t(!fragment);
            result.push_back(((inst.n+extra)<<16)|15); result.insert(result.end(),w+1,w+inst.n);
            if (new_input) result.push_back(index_var);
            if (!fragment) result.push_back(output);
            continue;
        }
        if (fragment && inst.op==71 && inst.n==4 && w[1]==index_var && w[2]==11 && w[3]==43) continue;
        if (inst.op==54) in_entry=inst.n>=3 && w[2]==entry;
        if (!fragment && in_entry && inst.op==248 && !stored && !pending) {
            result.insert(result.end(),w,w+inst.n); pending=true; continue;
        }
        // Function-local variables and debug line records precede executable instructions.
        if (pending && inst.op!=59 && inst.op!=8 && inst.op!=317) {
            const uint32_t loaded=bound++;
            result.insert(result.end(),{(4u<<16)|61,i32,loaded,index_var,(3u<<16)|62,output,loaded});
            pending=false; stored=true;
        }
        result.insert(result.end(),w,w+inst.n);
        if (inst.op==56) in_entry=false;
    }
    if (!fragment && !stored) return false;
    result[3]=bound; out=std::move(result); return true;
}
}

bool patch_instance_material_blocks(const std::vector<uint32_t>& in,const uint32_t* bindings,
                                    const uint32_t* steps,uint32_t nb,bool fragment,std::vector<uint32_t>& out) {
    if (fragment && !nb) return false;
    std::vector<uint32_t> gathered;
    if (nb && !patch_instance_blocks(in,bindings,steps,nb,gathered)) return false;
    return link_index(nb?gathered:in,fragment,nb!=0,out);
}
}
