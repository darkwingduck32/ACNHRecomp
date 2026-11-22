#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace nx::nvn {
template<class Range, size_t Stages, size_t Banks, size_t CurrentStages, size_t CurrentBanks, class Equal>
inline bool tracked_uniform_ranges_equal(const Range (&before)[Stages][Banks],
                                         const Range (&after)[CurrentStages][CurrentBanks], Equal equal) {
    static_assert(CurrentStages>=Stages && CurrentBanks>=Banks);
    // A merge snapshot can track fewer stages than the full bound state.
    for (size_t stage=0;stage<Stages;++stage) for (size_t bank=0;bank<Banks;++bank) {
        const auto& a=before[stage][bank]; const auto& b=after[stage][bank];
        if (a.address==b.address && a.size==b.size) continue;
        if (!equal(stage,bank)) return false;
    }
    return true;
}
inline bool uniform_read_values_equal(const uint8_t* a, uint64_t a_size, const uint8_t* b, uint64_t b_size,
                                      const std::vector<uint32_t>& offsets) {
    if (!a || !b || offsets.empty()) return false;
    for (uint32_t offset:offsets) {
        if (offset>65532) return false;
        for (uint32_t byte=offset;byte<offset+4;++byte)
            if ((byte<a_size?a[byte]:0)!=(byte<b_size?b[byte]:0)) return false;
    }
    return true;
}
// A deliberately narrow proof for translated c[] blocks. Unknown layouts,
// dynamic indexing and pointer escapes fail closed. Offsets include whole
// vectors when a vector is loaded, even if later arithmetic uses one lane.
inline bool uniform_read_words(const std::vector<uint32_t>& code, uint32_t binding,
                               std::vector<uint32_t>& offsets) {
    offsets.clear();
    if (code.size()<5 || code[0]!=0x07230203) return false;
    struct Inst { size_t at; uint32_t op, n; };
    std::vector<Inst> instructions;
    std::map<uint32_t,std::vector<uint32_t>> types;
    std::map<uint32_t,uint32_t> bindings, sets, strides, constants, constant_types, variables, member_offsets;
    size_t first_function=code.size();
    for (size_t i=5;i<code.size();) {
        const uint32_t n=code[i]>>16, op=code[i]&65535;
        if (!n || n>code.size()-i) return false;
        const auto* w=&code[i];
        instructions.push_back({i,op,n});
        if (op>=21 && op<=32 && n>=2) types[w[1]]=std::vector<uint32_t>(w,w+n);
        if (op==71 && n==4) {
            if (w[2]==33) bindings[w[1]]=w[3];
            if (w[2]==34) sets[w[1]]=w[3];
            if (w[2]==6) strides[w[1]]=w[3];
        }
        if (op==72 && n==5 && w[2]==0 && w[3]==35) member_offsets[w[1]]=w[4];
        if (op==43 && n==4) constants[w[2]]=w[3],constant_types[w[2]]=w[1];
        if (op==59 && n==4 && w[3]==2) variables[w[2]]=w[1];
        if (op==54) first_function=std::min(first_function,i);
        i+=n;
    }
    auto integer=[&](uint32_t id, uint32_t& value) {
        if (!constants.count(id)) return false;
        const auto& t=types[constant_types[id]];
        if (t.size()!=4 || (t[0]&65535)!=21 || t[2]!=32) return false;
        value=constants[id]; return true;
    };
    bool found=false;
    for (const auto& [var,ptr]:variables) {
        if (!bindings.count(var) || bindings[var]!=binding || !sets.count(var) || sets[var]!=0) continue;
        found=true;
        const auto& p=types[ptr];
        if (p.size()!=4 || (p[0]&65535)!=32 || p[2]!=2) return false;
        const auto& st=types[p[3]];
        if (st.size()!=3 || (st[0]&65535)!=30 || !member_offsets.count(p[3]) || member_offsets[p[3]]!=0) return false;
        const auto& ar=types[st[2]];
        uint32_t count=0;
        if (ar.size()!=4 || (ar[0]&65535)!=28 || strides[st[2]]!=16 ||
            !integer(ar[3],count) || !count || count>4096) return false;
        const auto& vec=types[ar[2]];
        if (vec.size()!=4 || (vec[0]&65535)!=23 || vec[3]!=4) return false;
        const auto& scalar=types[vec[2]];
        if (scalar.size()<3 || ((scalar[0]&65535)!=21 && (scalar[0]&65535)!=22) || scalar[2]!=32) return false;
        std::map<uint32_t,uint32_t> pointers;
        for (const auto& inst:instructions) {
            if (inst.at<first_function) continue;
            const auto* w=&code[inst.at];
            for (uint32_t k=1;k<inst.n;++k) if (w[k]==var) {
                if ((inst.op!=65 && inst.op!=66) || k!=3 || (inst.n!=6 && inst.n!=7)) return false;
                uint32_t member=0,index=0,lane=0;
                if (!integer(w[4],member) || member || !integer(w[5],index) || index>=count) return false;
                if (inst.n==7 && (!integer(w[6],lane) || lane>=4)) return false;
                const uint32_t expected=inst.n==7?vec[2]:ar[2];
                const auto& result=types[w[1]];
                if (result.size()!=4 || (result[0]&65535)!=32 || result[2]!=2 || result[3]!=expected) return false;
                pointers[w[2]]=expected;
                for (uint32_t l=lane;l<(inst.n==7?lane+1:4);++l) offsets.push_back(index*16+l*4);
            }
        }
        for (const auto& inst:instructions) {
            if (inst.at<first_function) continue;
            const auto* w=&code[inst.at];
            for (uint32_t k=1;k<inst.n;++k) if (pointers.count(w[k])) {
                if ((inst.op==65 || inst.op==66) && k==2) continue; // definition
                if (inst.op!=61 || k!=3 || inst.n<4 || w[1]!=pointers[w[k]]) return false;
            }
        }
    }
    if (!found || offsets.empty()) { offsets.clear(); return false; }
    std::sort(offsets.begin(),offsets.end());
    offsets.erase(std::unique(offsets.begin(),offsets.end()),offsets.end());
    return true;
}
}
