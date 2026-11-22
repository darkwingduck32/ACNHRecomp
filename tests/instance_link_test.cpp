#include "nvn_spirv_instance.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using Code=std::vector<uint32_t>;
void check(bool b) { if(!b) { std::cerr<<"instance link check failed\n"; std::exit(1); } }
void emit(Code& c,uint32_t op,std::initializer_list<uint32_t> a) {
    c.push_back((uint32_t(a.size()+1)<<16)|op); c.insert(c.end(),a);
}
Code fixture(bool fragment,bool local=false) {
    Code c{0x07230203,0x10000,0,1000,0};
    emit(c,17,{1}); emit(c,14,{0,1}); emit(c,15,{fragment?4u:0u,100,0x6e69616d,0,8});
    if(fragment) emit(c,16,{100,7});
    emit(c,71,{8,fragment?30u:11u,0}); emit(c,71,{120,33,fragment?23u:5u});
    emit(c,71,{120,34,0}); emit(c,71,{22,2}); emit(c,71,{21,6,16}); emit(c,72,{22,0,35,0});
    emit(c,19,{1}); emit(c,33,{2,1}); emit(c,22,{4,32}); emit(c,23,{5,4,4});
    emit(c,21,{6,32,1}); emit(c,32,{7,3,5}); emit(c,59,{7,8,3});
    emit(c,43,{6,20,4096}); emit(c,43,{6,25,0}); emit(c,43,{6,26,8});
    emit(c,28,{21,5,20}); emit(c,30,{22,21}); emit(c,32,{23,2,22});
    emit(c,32,{24,2,4}); emit(c,59,{23,120,2});
    if(local) emit(c,32,{27,7,4});
    emit(c,54,{1,100,0,2}); emit(c,248,{101});
    if(local) emit(c,59,{27,130,7});
    emit(c,65,{24,110,120,25,26,25}); emit(c,61,{4,111,110});
    emit(c,253,{}); emit(c,56,{}); return c;
}
uint32_t decorated(const Code& c,uint32_t decoration,uint32_t value) {
    for(size_t i=5;i<c.size();i+=c[i]>>16)
        if((c[i]&65535)==71 && c[i]>>16==4 && c[i+2]==decoration && c[i+3]==value) return c[i+1];
    return 0;
}
bool flat(const Code& c,uint32_t id) {
    for(size_t i=5;i<c.size();i+=c[i]>>16)
        if((c[i]&65535)==71 && c[i]>>16==3 && c[i+1]==id && c[i+2]==14) return true;
    return false;
}
void write(const std::filesystem::path& path,const Code& c) {
    std::ofstream f(path,std::ios::binary); f.write(reinterpret_cast<const char*>(c.data()),c.size()*4); check(bool(f));
}
Code read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate); check(bool(f)); auto bytes=f.tellg();
    check(bytes>0 && size_t(bytes)%4==0); Code c(size_t(bytes)/4); f.seekg(0); f.read(reinterpret_cast<char*>(c.data()),bytes); check(bool(f)); return c;
}
int main(int argc,char** argv) {
    const uint32_t vb=5,fb=23,step=12;
    Code v,f,out;
    check(nx::nvn::patch_instance_material_blocks(fixture(false,true),&vb,&step,1,false,v));
    check(nx::nvn::patch_instance_material_blocks(fixture(true),&fb,&step,1,true,f));
    for(const auto* c:{&v,&f}) check(flat(*c,decorated(*c,30,29)));
    check(decorated(v,11,43)!=0 && decorated(f,11,43)==0);
    check(nx::nvn::patch_instance_material_blocks(fixture(false),nullptr,nullptr,0,false,out));
    check(!nx::nvn::patch_instance_material_blocks(v,nullptr,nullptr,0,false,out)); // existing instance semantics
    check(!nx::nvn::patch_instance_material_blocks(fixture(false),&vb,&step,1,true,out)); // wrong stage
    check(!nx::nvn::patch_instance_material_blocks(fixture(true),nullptr,nullptr,0,true,out));
    auto collision=fixture(true);
    for(size_t i=5;i<collision.size();i+=collision[i]>>16)
        if((collision[i]&65535)==71 && collision[i]>>16==4 && collision[i+2]==30) collision[i+3]=29;
    check(!nx::nvn::patch_instance_material_blocks(collision,&fb,&step,1,true,out));
    auto malformed=fixture(false); malformed.push_back(0);
    check(!nx::nvn::patch_instance_material_blocks(malformed,&vb,&step,1,false,out));
    if(argc==3) {
        const std::filesystem::path source=argv[1],target=argv[2]; std::filesystem::create_directories(target);
        write(target/"fixture.vert.spv",v); write(target/"fixture.frag.spv",f);
        auto original=read(source/"51cc72321ae5026e.spv");
        check(nx::nvn::patch_instance_material_blocks(original,nullptr,nullptr,0,false,out));
        write(target/"material-export-only.vert.spv",out);
        check(nx::nvn::patch_instance_material_blocks(original,&vb,&step,1,false,v));
        write(target/"material.vert.spv",v);
        for(const char* hash:{"2330b41795e4250d","f2501b67d1064071","63a579cd6aed8229"}) {
            original=read(source/(std::string(hash)+".spv"));
            check(nx::nvn::patch_instance_material_blocks(original,&fb,&step,1,true,f));
            write(target/(std::string(hash)+".frag.spv"),f);
        }
    }
    std::cout<<"instance link checks passed\n";
}
