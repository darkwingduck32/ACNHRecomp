#include "nvn_uniform_reads.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

using Code=std::vector<uint32_t>;
void check(bool value) { if (!value) { std::cerr<<"uniform read proof failed\n"; std::exit(1); } }
void emit(Code& c,uint32_t op,std::initializer_list<uint32_t> args) {
    c.push_back((uint32_t(args.size()+1)<<16)|op); c.insert(c.end(),args);
}
Code module(bool vector=false,uint32_t index=21,uint32_t lane=20,uint32_t stride=16,uint32_t offset=0) {
    Code c{0x07230203,0x10000,0,1000,0};
    emit(c,71,{100,33,23}); emit(c,71,{100,34,0}); emit(c,71,{4,6,stride}); emit(c,72,{5,0,35,offset});
    emit(c,22,{1,32}); emit(c,21,{2,32,0}); emit(c,23,{3,1,4});
    emit(c,43,{2,20,0}); emit(c,43,{2,21,8}); emit(c,43,{2,22,2}); emit(c,43,{2,23,4096}); emit(c,43,{2,24,4});
    emit(c,28,{4,3,23}); emit(c,30,{5,4}); emit(c,32,{6,2,5}); emit(c,32,{7,2,1}); emit(c,32,{8,2,3});
    emit(c,59,{6,100,2}); emit(c,54,{11,300,0,12}); emit(c,248,{301});
    if(vector) emit(c,65,{8,110,100,20,index}); else emit(c,65,{7,110,100,20,index,lane});
    emit(c,61,{vector?3u:1u,111,110}); emit(c,253,{}); emit(c,56,{}); return c;
}
int main(int argc,char** argv) {
    struct Range { uint64_t address=0,size=0; } before[2][18],after[6][18];
    after[5][17]={123,456}; // untracked stages must never be inspected
    auto reject=[](size_t,size_t) { return false; };
    check(nx::nvn::tracked_uniform_ranges_equal(before,after,reject));
    after[1][3]={123,16};
    check(!nx::nvn::tracked_uniform_ranges_equal(before,after,reject));
    auto material=[](size_t stage,size_t bank) { return stage==1 && bank==3; };
    check(nx::nvn::tracked_uniform_ranges_equal(before,after,material));
    after[0][4]={123,16};
    check(!nx::nvn::tracked_uniform_ranges_equal(before,after,material));
    uint8_t a[20]={}, b[20]={};
    a[0]=1; b[0]=2; // unrelated transform bytes must not affect the material read
    a[16]=b[16]=7;
    check(nx::nvn::uniform_read_values_equal(a,20,b,20,{16}));
    b[16]=8;
    check(!nx::nvn::uniform_read_values_equal(a,20,b,20,{16}));
    b[16]=7; b[18]=9;
    check(nx::nvn::uniform_read_values_equal(a,18,b,18,{16})); // truncated bytes are zero
    check(!nx::nvn::uniform_read_values_equal(a,20,b,20,{16}));
    check(!nx::nvn::uniform_read_values_equal(a,20,b,20,{}));
    std::vector<uint32_t> words;
    check(nx::nvn::uniform_read_words(module(),23,words) && words==Code{128});
    check(nx::nvn::uniform_read_words(module(false,21,22),23,words) && words==Code{136});
    check(nx::nvn::uniform_read_words(module(true),23,words) && words==Code({128,132,136,140}));
    check(!nx::nvn::uniform_read_words(module(false,500),23,words)); // dynamic row
    check(!nx::nvn::uniform_read_words(module(false,21,500),23,words)); // dynamic lane
    check(!nx::nvn::uniform_read_words(module(false,21,24),23,words)); // lane out of range
    check(!nx::nvn::uniform_read_words(module(false,23),23,words)); // row out of range
    check(!nx::nvn::uniform_read_words(module(false,21,20,32),23,words));
    check(!nx::nvn::uniform_read_words(module(false,21,20,16,16),23,words));
    check(!nx::nvn::uniform_read_words(module(),24,words));
    auto escaped=module(); emit(escaped,83,{7,130,110});
    check(!nx::nvn::uniform_read_words(escaped,23,words)); // pointer alias
    escaped=module(); emit(escaped,57,{11,131,400,110});
    check(!nx::nvn::uniform_read_words(escaped,23,words)); // pointer argument
    escaped=module(); emit(escaped,61,{5,132,100});
    check(!nx::nvn::uniform_read_words(escaped,23,words)); // whole block
    escaped=module(); escaped.push_back(0);
    check(!nx::nvn::uniform_read_words(escaped,23,words));
    if(argc==2) {
        for (auto pair: {std::pair{"2330b41795e4250d",128u},std::pair{"f2501b67d1064071",136u}}) {
            std::ifstream f(std::string(argv[1])+"/"+pair.first+".spv",std::ios::binary|std::ios::ate);
            check(bool(f)); auto bytes=f.tellg(); check(bytes>0 && size_t(bytes)%4==0);
            Code c(size_t(bytes)/4); f.seekg(0); f.read(reinterpret_cast<char*>(c.data()),bytes);
            check(nx::nvn::uniform_read_words(c,23,words) && words==Code{pair.second});
        }
    }
    std::cout<<"uniform read proof checks passed\n";
}
