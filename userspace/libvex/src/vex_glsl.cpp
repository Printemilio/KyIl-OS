#include "vex_glsl.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/MachineIndependent/localintermediate.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <mutex>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>

/* Validate the real ESSL/GLSL dialect first. SPIR-V is a private execution IR:
 * do not impose Vulkan's minimum ESSL 310 or Vulkan uniform-block syntax on GL.
 * Glslang's linked AST handles functions, preprocessing and interface matching. */
static std::mutex compiler_lock;
static bool compiler_ready;
extern "C" void vex_glsl_release(void){std::lock_guard<std::mutex> lock(compiler_lock);if(compiler_ready){glslang::FinalizeProcess();compiler_ready=false;}}
static void message(char *out,size_t n,const char *s){if(out&&n)std::snprintf(out,n,"%s",s?s:"");}
static bool prepare(glslang::TShader& shader,const char *source,char *log,size_t n){

    shader.setStrings(&source,1);
    shader.setAutoMapBindings(true);shader.setAutoMapLocations(true);
    if(!shader.parse(GetDefaultResources(),100,false,EShMsgDefault)){
        message(log,n,shader.getInfoLog());return false;
    }return true;
}
extern "C" int vex_glsl_validate(const char *source,int fragment,char *log,size_t n){

    std::lock_guard<std::mutex> lock(compiler_lock);
    if(!source){message(log,n,"No shader source");return 0;}
    if(!compiler_ready){compiler_ready=glslang::InitializeProcess();if(!compiler_ready)return 0;}
    glslang::TShader shader(fragment?EShLangFragment:EShLangVertex);
    return prepare(shader,source,log,n);
}
static bool bind_attributes(std::vector<unsigned>& words,const vex_glsl_binding *bindings,size_t count){
    // Binding locations is an API operation on compiled interface IDs, not a source rewrite.
    struct Attr{unsigned id,location;std::string name;};std::vector<Attr> attrs;
    for(size_t o=5;o<words.size();){unsigned wc=words[o]>>16,op=words[o]&65535;
        if(!wc||wc>words.size()-o)return false;
        if(op==59&&wc>=4&&words[o+3]==1){unsigned id=words[o+2];std::string name;
            for(size_t k=5;k<words.size();k+=words[k]>>16)if((words[k]&65535)==5&&words[k+1]==id)name=reinterpret_cast<char*>(&words[k+2]);
            for(size_t k=5;k<words.size();k+=words[k]>>16)if((words[k]&65535)==71&&words[k+1]==id&&words[k+2]==30)attrs.push_back({id,words[k+3],name});
        }o+=wc;
    }
    unsigned used=0;
    for(auto& a:attrs)for(size_t b=0;b<count;++b)if(a.name==bindings[b].name){if(bindings[b].location>=8||(used&(1u<<bindings[b].location)))return false;a.location=bindings[b].location;used|=1u<<a.location;break;}
    for(auto& a:attrs){bool explicit_binding=false;for(size_t b=0;b<count;++b)if(a.name==bindings[b].name)explicit_binding=true;
        if(!explicit_binding){unsigned loc=0;while(loc<8&&(used&(1u<<loc)))++loc;if(loc==8)return false;a.location=loc;used|=1u<<loc;}}
    for(size_t o=5;o<words.size();o+=words[o]>>16)if((words[o]&65535)==71&&words[o+2]==30)for(auto& a:attrs)if(a.id==words[o+1])words[o+3]=a.location;
    return true;
}
extern "C" int vex_glsl_link(const char *vertex,const char *fragment,const vex_glsl_binding *bindings,size_t count,vex_glsl_binary out[2],char *log,size_t n){
    std::lock_guard<std::mutex> lock(compiler_lock);out[0]={};out[1]={};
    if(!compiler_ready){compiler_ready=glslang::InitializeProcess();if(!compiler_ready)return 0;}
    glslang::TShader vs(EShLangVertex),fs(EShLangFragment);glslang::TProgram program;
    if(!prepare(vs,vertex,log,n)||!prepare(fs,fragment,log,n))return 0;
    program.addShader(&vs);program.addShader(&fs);
    if(!program.link(EShMsgDefault)||!program.mapIO()){message(log,n,program.getInfoLog());return 0;}
    for(int stage=0;stage<2;++stage){
        auto *ir=program.getIntermediate(stage?EShLangFragment:EShLangVertex);
        glslang::SpvVersion target={};target.spv=0x10000;ir->setSpv(target);
        std::vector<unsigned> words;glslang::SpvOptions options;options.disableOptimizer=true;
        spv::SpvBuildLogger logger;glslang::GlslangToSpv(*ir,words,&logger,&options);
        if(stage){
            // Legacy ESSL gl_FragColor denotes color output zero without a Location
            // qualifier. Lower that language-defined semantic into our private IR.
            unsigned frag_color=0;size_t types=words.size();
            for(size_t o=5;o<words.size();o+=words[o]>>16){unsigned op=words[o]&65535;
                if(op==5&&!std::strcmp(reinterpret_cast<const char*>(&words[o+2]),"gl_FragColor"))frag_color=words[o+1];
                if(op>=19&&op<=39&&types==words.size())types=o;
            }
            if(frag_color){unsigned decoration[]={0x40047,frag_color,30,0};words.insert(words.begin()+types,decoration,decoration+4);}
        }
        if(words.empty()||!logger.getAllMessages().empty()||(!stage&&!bind_attributes(words,bindings,count))){message(log,n,logger.getAllMessages().empty()?"Unsupported vertex interface":logger.getAllMessages().c_str());vex_glsl_binary_free(&out[0]);return 0;}
        out[stage].bytes=words.size()*4;out[stage].words=static_cast<uint32_t*>(std::malloc(out[stage].bytes));
        if(!out[stage].words){vex_glsl_binary_free(&out[0]);return 0;}std::memcpy(out[stage].words,words.data(),out[stage].bytes);
    }
    message(log,n,"");return 1;
}
extern "C" void vex_glsl_binary_free(vex_glsl_binary *b){if(b){std::free(b->words);b->words=nullptr;b->bytes=0;}}
