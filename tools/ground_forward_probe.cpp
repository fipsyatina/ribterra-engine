#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/world_projection.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unknown"
#endif

namespace {
using namespace ribterra;
using namespace ribterra::render;
using Json = nlohmann::json;
using Vec3 = std::array<float, 3>;
constexpr Extent logical{1280, 720};

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message) + (SDL_GetError()[0] ? ": " + std::string(SDL_GetError()) : ""));
}
std::string utf8(const std::filesystem::path& path) {
    const auto text=path.u8string(); return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::filesystem::path from_utf8(std::string_view value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));
}
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate); require(input.good(),"Could not read "+utf8(path));
    const auto size=input.tellg(); require(size>=0&&size<=256*1024*1024,"Input is too large");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size)); input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()),size); require(input.good(),"Incomplete input read"); return result;
}
void write_bytes(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
    require(!std::filesystem::exists(path),"Refusing to overwrite "+utf8(path));
    std::ofstream output(path,std::ios::binary); require(output.good(),"Could not create "+utf8(path));
    output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    output.flush(); output.close(); require(output.good(),"Incomplete output write");
}
void write_json(const std::filesystem::path& path,const Json& value) {
    const auto text=value.dump(2)+'\n'; write_bytes(path,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
}
Json parse_json(const std::filesystem::path& path) { const auto bytes=read_bytes(path); return Json::parse(bytes.begin(),bytes.end()); }
Vec3 subtract(Vec3 a,Vec3 b) { for(std::size_t i=0;i<3;++i)a[i]-=b[i]; return a; }
float dot(Vec3 a,Vec3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 cross(Vec3 a,Vec3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
Vec3 normalized(Vec3 v) { const float n=std::sqrt(dot(v,v)); require(std::isfinite(n)&&n>1.e-7f,"Invalid normal"); for(auto& x:v)x/=n; return v; }
std::array<int,4> pixel(SDL_Surface* image,int x,int y) {
    require(image&&x>=0&&y>=0&&x<image->w&&y<image->h,"Sample outside capture");
    Uint8 r{},g{},b{},a{}; require(SDL_ReadSurfacePixel(image,x,y,&r,&g,&b,&a),"Capture pixel read failed");
    return {r,g,b,a};
}
std::array<int,4> logical_pixel(SDL_Surface* image,Point p) {
    const float scale=std::min(image->w/static_cast<float>(logical.width),image->h/static_cast<float>(logical.height));
    const float x=(image->w-logical.width*scale)*.5f+p.x*scale,y=(image->h-logical.height*scale)*.5f+p.y*scale;
    return pixel(image,std::clamp(static_cast<int>(std::floor(x)),0,image->w-1),std::clamp(static_cast<int>(std::floor(y)),0,image->h-1));
}
double brightness(const std::array<int,4>& p) { return .2126*p[0]+.7152*p[1]+.0722*p[2]; }
float srgb_byte_to_linear(int encoded) {
    const float value=encoded/255.f; return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
}
int linear_to_srgb_byte(float linear) {
    linear=std::clamp(linear,0.f,1.f);const float value=linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1.f/2.4f)-.055f;
    return static_cast<int>(std::lround(value*255.f));
}
bool channels_near(const std::array<int,4>& a,const std::array<int,4>& b,int tolerance) {
    for(std::size_t i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>tolerance)return false;
    return true;
}
std::string pass_name(WorldGraphPass pass) {
    switch(pass) {
    case WorldGraphPass::GroundGBuffer:return "GroundGBuffer"; case WorldGraphPass::GroundLighting:return "GroundLighting";
    case WorldGraphPass::WorldForward:return "WorldForward"; case WorldGraphPass::Composite:return "Composite";
    case WorldGraphPass::Ui:return "Ui";
    }
    return "unknown";
}
Json status_json(const GroundForwardStatus& s) {
    return {{"available",s.available},{"format_supported",s.format_supported},{"target_formats",s.target_formats},
            {"dimensions",{s.width,s.height}},{"frame_sets",s.frame_sets},{"attachment_bytes",s.attachment_bytes},
            {"target_generation",s.target_generation},{"creation_attempts",s.creation_attempts},
            {"active_set_ids",s.active_set_ids},{"candidate_live_textures",s.candidate_live_textures},{"reason",s.reason}};
}
Json gpu_identity(SDL_GPUDevice* gpu) {
    const auto props=SDL_GetGPUDeviceProperties(gpu);const char* name=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,""):"";
    const char* driver_name=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,""):"";
    const char* driver_info=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,""):"";
    if(!driver_info||!*driver_info)driver_info=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,""):"";
    std::string driver=driver_name?driver_name:"";if(driver_info&&*driver_info){if(!driver.empty())driver+=' ';driver+=driver_info;}
    return {{"backend",SDL_GetGPUDeviceDriver(gpu)},{"device",name?name:""},{"driver",driver}};
}

struct Command {
    SDL_GPUCommandBuffer* value{};
    explicit Command(SDL_GPUDevice* gpu):value(SDL_AcquireGPUCommandBuffer(gpu)) { require(value,"GPU command acquisition failed"); }
    ~Command(){if(value)SDL_CancelGPUCommandBuffer(value);}
    SDL_GPUFence* submit(){auto* command=value; value=nullptr; auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command); require(fence,"GPU submit failed"); return fence;}
};

float half_float(std::uint16_t bits) {
    const auto mantissa=bits&1023u,exponent=(bits>>10u)&31u; float value{};
    if(exponent==0)value=std::ldexp(static_cast<float>(mantissa),-24);
    else if(exponent==31)value=mantissa?NAN:INFINITY;
    else value=std::ldexp(static_cast<float>(mantissa+1024u),static_cast<int>(exponent)-25);
    return bits&0x8000u?-value:value;
}

// Independent source-alpha diagnostic. It does not use, install, or inspect Renderer2D targets.
Json rg16f_blend_control(const std::filesystem::path& runtime_assets) {
    struct Gpu {
        SDL_GPUDevice* device{}; SDL_GPUShader *vs{},*fs{}; SDL_GPUGraphicsPipeline* pipeline{}; SDL_GPUSampler* sampler{};
        SDL_GPUTexture *white{},*scene{},*metadata{}; SDL_GPUBuffer *vertices{},*occluders{}; SDL_GPUTransferBuffer *upload{},*download{};
        ~Gpu(){
            if(!device)return; SDL_WaitForGPUIdle(device);
            if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline); if(vs)SDL_ReleaseGPUShader(device,vs); if(fs)SDL_ReleaseGPUShader(device,fs);
            if(sampler)SDL_ReleaseGPUSampler(device,sampler); if(white)SDL_ReleaseGPUTexture(device,white); if(scene)SDL_ReleaseGPUTexture(device,scene);
            if(metadata)SDL_ReleaseGPUTexture(device,metadata);
            if(vertices)SDL_ReleaseGPUBuffer(device,vertices); if(upload)SDL_ReleaseGPUTransferBuffer(device,upload);
            if(occluders)SDL_ReleaseGPUBuffer(device,occluders);
            if(download)SDL_ReleaseGPUTransferBuffer(device,download); SDL_DestroyGPUDevice(device);
        }
    } gpu;
    gpu.device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12"); require(gpu.device,"RG16F diagnostic D3D12 device unavailable");
    require(std::string_view(SDL_GetGPUDeviceDriver(gpu.device))=="direct3d12","RG16F diagnostic selected a non-D3D12 backend");
    constexpr auto format=SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    require(SDL_GPUTextureSupportsFormat(gpu.device,format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET),"RG16F color target unsupported");
    auto shader=[&](std::string_view name,SDL_GPUShaderStage stage,std::uint32_t samplers,std::uint32_t uniforms,std::uint32_t storage=0){
        const auto bytes=read_bytes(runtime_assets/"shaders"/name); SDL_GPUShaderCreateInfo info{}; info.code=bytes.data(); info.code_size=bytes.size();
        info.entrypoint="main"; info.format=SDL_GPU_SHADERFORMAT_DXIL; info.stage=stage; info.num_samplers=samplers; info.num_uniform_buffers=uniforms;
        info.num_storage_buffers=storage;
        auto* result=SDL_CreateGPUShader(gpu.device,&info); require(result,"RG16F diagnostic shader creation failed"); return result;
    };
    gpu.vs=shader("material_forward.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX,0,1);
    gpu.fs=shader("material_forward.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT,5,3,1);
    const SDL_GPUVertexBufferDescription stream{0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
    const std::array attributes{SDL_GPUVertexAttribute{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,position)},
        SDL_GPUVertexAttribute{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,uv)},
        SDL_GPUVertexAttribute{2,0,SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,offsetof(Vertex,color)}};
    std::array<SDL_GPUColorTargetDescription,2> colors{}; colors[0].format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; colors[1].format=format;
    auto& scene_blend=colors[0].blend_state; scene_blend.enable_blend=true;
    scene_blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA; scene_blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    scene_blend.color_blend_op=SDL_GPU_BLENDOP_ADD; scene_blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
    scene_blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; scene_blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    auto& meta_blend=colors[1].blend_state; meta_blend.enable_blend=true;
    meta_blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE; meta_blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    meta_blend.color_blend_op=SDL_GPU_BLENDOP_ADD; meta_blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
    meta_blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; meta_blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{}; pipeline.vertex_shader=gpu.vs; pipeline.fragment_shader=gpu.fs;
    pipeline.vertex_input_state={&stream,1,attributes.data(),3}; pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL; pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pipeline.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE; pipeline.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    pipeline.target_info={colors.data(),static_cast<Uint32>(colors.size()),SDL_GPU_TEXTUREFORMAT_INVALID,false,0,0,0};
    gpu.pipeline=SDL_CreateGPUGraphicsPipeline(gpu.device,&pipeline); require(gpu.pipeline,"RG16F blend pipeline creation failed");
    SDL_GPUSamplerCreateInfo sampler{}; sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST; sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    gpu.sampler=SDL_CreateGPUSampler(gpu.device,&sampler); require(gpu.sampler,"RG16F sampler creation failed");
    SDL_GPUTextureCreateInfo texture{}; texture.type=SDL_GPU_TEXTURETYPE_2D; texture.width=texture.height=texture.layer_count_or_depth=texture.num_levels=1;
    texture.sample_count=SDL_GPU_SAMPLECOUNT_1; texture.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    gpu.white=SDL_CreateGPUTexture(gpu.device,&texture); require(gpu.white,"RG16F source texture creation failed");
    texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; texture.width=texture.height=4; texture.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    gpu.scene=SDL_CreateGPUTexture(gpu.device,&texture); texture.format=format; gpu.metadata=SDL_CreateGPUTexture(gpu.device,&texture);
    require(gpu.scene&&gpu.metadata,"RG16F MRT target creation failed");
    constexpr Color first{255,255,255,64},second{255,255,255,153};
    const auto quad=[](Color c){return std::array<Vertex,6>{{{{0,0},{0,0},c},{{4,0},{1,0},c},{{4,4},{1,1},c},
        {{0,0},{0,0},c},{{4,4},{1,1},c},{{0,4},{0,1},c}}};};
    const auto a=quad(first),b=quad(second); std::array<Vertex,12> all{}; std::copy(a.begin(),a.end(),all.begin()); std::copy(b.begin(),b.end(),all.begin()+6);
    SDL_GPUBufferCreateInfo buffer{SDL_GPU_BUFFERUSAGE_VERTEX,static_cast<Uint32>(sizeof(all)),0}; gpu.vertices=SDL_CreateGPUBuffer(gpu.device,&buffer);
    require(gpu.vertices,"RG16F vertex buffer creation failed");
    buffer={SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,sizeof(OccluderGpu),0};
    gpu.occluders=SDL_CreateGPUBuffer(gpu.device,&buffer);require(gpu.occluders,"RG16F occluder buffer creation failed");
    SDL_GPUTransferBufferCreateInfo transfer{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,static_cast<Uint32>(sizeof(all)+4+sizeof(OccluderGpu)),0}; gpu.upload=SDL_CreateGPUTransferBuffer(gpu.device,&transfer);
    transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; transfer.size=256; gpu.download=SDL_CreateGPUTransferBuffer(gpu.device,&transfer);
    require(gpu.upload&&gpu.download,"RG16F transfer buffer creation failed");
    auto* mapped=static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(gpu.device,gpu.upload,false)); require(mapped,"RG16F upload map failed");
    std::memcpy(mapped,all.data(),sizeof(all)); std::memset(mapped+sizeof(all),255,4);
    const OccluderGpu unused{};std::memcpy(mapped+sizeof(all)+4,&unused,sizeof(unused));SDL_UnmapGPUTransferBuffer(gpu.device,gpu.upload);
    Command command(gpu.device); auto* copy=SDL_BeginGPUCopyPass(command.value); require(copy,"RG16F upload pass failed");
    const SDL_GPUTransferBufferLocation vertex_source{gpu.upload,0}; const SDL_GPUBufferRegion vertex_dest{gpu.vertices,0,sizeof(all)};
    SDL_UploadToGPUBuffer(copy,&vertex_source,&vertex_dest,false);
    const SDL_GPUTransferBufferLocation occluder_source{gpu.upload,static_cast<Uint32>(sizeof(all)+4)};
    const SDL_GPUBufferRegion occluder_dest{gpu.occluders,0,sizeof(OccluderGpu)};
    SDL_UploadToGPUBuffer(copy,&occluder_source,&occluder_dest,false);
    const SDL_GPUTextureTransferInfo tex_source{gpu.upload,static_cast<Uint32>(sizeof(all)),1,1}; const SDL_GPUTextureRegion tex_dest{gpu.white,0,0,0,0,0,1,1,1};
    SDL_UploadToGPUTexture(copy,&tex_source,&tex_dest,false); SDL_EndGPUCopyPass(copy);
    std::array<SDL_GPUColorTargetInfo,2> targets{}; targets[0].texture=gpu.scene; targets[1].texture=gpu.metadata;
    for(auto& target:targets){target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;target.clear_color={0,0,0,0};}
    auto* pass=SDL_BeginGPURenderPass(command.value,targets.data(),static_cast<Uint32>(targets.size()),nullptr); require(pass,"RG16F MRT render pass failed"); SDL_BindGPUGraphicsPipeline(pass,gpu.pipeline);
    const SDL_GPUViewport viewport{0,0,4,4,0,1}; SDL_SetGPUViewport(pass,&viewport); const SDL_GPUBufferBinding binding{gpu.vertices,0};
    SDL_BindGPUVertexBuffers(pass,0,&binding,1); const std::array<float,4> extent{4,4,0,0}; SDL_PushGPUVertexUniformData(command.value,0,extent.data(),sizeof(extent));
    std::array<SDL_GPUTextureSamplerBinding,5> images{}; for(auto& image:images)image={gpu.white,gpu.sampler};
    SDL_BindGPUFragmentSamplers(pass,0,images.data(),static_cast<Uint32>(images.size()));
    SDL_BindGPUFragmentStorageBuffers(pass,0,&gpu.occluders,1);
    SurfaceLightingFrame lighting{}; lighting.ambient={1,1,1,0}; SDL_PushGPUFragmentUniformData(command.value,0,&lighting,sizeof(lighting));
    SpriteSurfaceGpu surface{}; for(auto& uv:surface.uv_regions)uv={0,0,1,1}; surface.anchor_export={31,31,31,1};
    surface.source_size_pivot={1,1,0,0}; surface.depth_scale={0,1,1,0}; SDL_PushGPUFragmentUniformData(command.value,1,&surface,sizeof(surface));
    ForwardMetadataGpu metadata{{.8f,0,0,0}}; SDL_PushGPUFragmentUniformData(command.value,2,&metadata,sizeof(metadata));
    SDL_DrawGPUPrimitives(pass,6,1,0,0); metadata.reveal[0]=.2f; SDL_PushGPUFragmentUniformData(command.value,2,&metadata,sizeof(metadata));
    SDL_DrawGPUPrimitives(pass,6,1,6,0); SDL_EndGPURenderPass(pass);
    copy=SDL_BeginGPUCopyPass(command.value); require(copy,"RG16F download pass failed"); const SDL_GPUTextureRegion region{gpu.metadata,0,0,1,1,0,1,1,1};
    const SDL_GPUTextureTransferInfo download{gpu.download,0,64,1}; SDL_DownloadFromGPUTexture(copy,&region,&download); SDL_EndGPUCopyPass(copy);
    auto* fence=command.submit(); const std::array fences{fence}; const bool waited=SDL_WaitForGPUFences(gpu.device,true,fences.data(),1); SDL_ReleaseGPUFence(gpu.device,fence);
    require(waited,"RG16F fence wait failed"); const auto* readback_pixels=static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(gpu.device,gpu.download,false)); require(readback_pixels,"RG16F readback map failed");
    const std::array<float,2> actual{half_float(static_cast<std::uint16_t>(readback_pixels[0]|readback_pixels[1]<<8u)),half_float(static_cast<std::uint16_t>(readback_pixels[2]|readback_pixels[3]<<8u))};
    SDL_UnmapGPUTransferBuffer(gpu.device,gpu.download);
    const float a1=64/255.f,a2=153/255.f; const std::array<float,2> expected_y_terms{.8f*a1*(1-a2),.2f*a2};
    const float expected_y=expected_y_terms[0]+expected_y_terms[1];
    const float reconstructed_depth=31.f*std::sqrt(3.f)+1.f;
    const float expected_depth=reconstructed_depth*(a1*(1-a2)+a2); const float depth_error=std::abs(actual[0]-expected_depth);
    require(std::isfinite(actual[0])&&depth_error<=.03125f&&std::abs(actual[1]-expected_y)<.002f,
            "RG16F MRT metadata depth/reveal blending exceeded native readback tolerances");
    auto result=gpu_identity(gpu.device);result["scope"]="independent native shader/output-merger readback; not production Renderer2D proof";
    result["mrt_formats"]={"R16G16B16A16_FLOAT","R16G16_FLOAT"};result["metadata_reveal_coverage"]={{.8f,a1},{.2f,a2}};
    result["expected_reveal_terms"]=expected_y_terms;result["expected_reveal"]=expected_y;result["reconstructed_depth_before_blend"]=reconstructed_depth;
    result["expected_blended_depth"]=expected_depth;result["depth_absolute_error"]=depth_error;result["actual_depth_reveal_rg"]=actual;
    result["depth_tolerance"]=.03125f;result["reveal_tolerance"]=.002f;return result;
}

Json ground_position_precision_control(const std::filesystem::path& runtime_assets,const std::filesystem::path& output) {
    constexpr int width=16,height=8,pitch=256;
    struct Gpu {
        SDL_GPUDevice* device{}; SDL_GPUShader *vs{},*fs{}; SDL_GPUGraphicsPipeline* pipeline{}; SDL_GPUSampler* sampler{};
        SDL_GPUTexture* albedo{}; std::array<SDL_GPUTexture*,4> targets{}; std::array<SDL_GPUBuffer*,2> buffers{};
        SDL_GPUTransferBuffer *upload{},*download{};
        ~Gpu(){if(!device)return;SDL_WaitForGPUIdle(device);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vs)SDL_ReleaseGPUShader(device,vs);if(fs)SDL_ReleaseGPUShader(device,fs);if(sampler)SDL_ReleaseGPUSampler(device,sampler);
            if(albedo)SDL_ReleaseGPUTexture(device,albedo);for(auto* target:targets)if(target)SDL_ReleaseGPUTexture(device,target);
            for(auto* buffer:buffers)if(buffer)SDL_ReleaseGPUBuffer(device,buffer);if(upload)SDL_ReleaseGPUTransferBuffer(device,upload);
            if(download)SDL_ReleaseGPUTransferBuffer(device,download);SDL_DestroyGPUDevice(device);}
    } gpu;
    gpu.device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12");require(gpu.device,"Ground position diagnostic D3D12 device unavailable");
    auto shader=[&](std::string_view name,SDL_GPUShaderStage stage,std::uint32_t samplers,std::uint32_t uniforms){const auto bytes=read_bytes(runtime_assets/"shaders"/name);
        SDL_GPUShaderCreateInfo info{};info.code=bytes.data();info.code_size=bytes.size();info.entrypoint="main";info.format=SDL_GPU_SHADERFORMAT_DXIL;
        info.stage=stage;info.num_samplers=samplers;info.num_uniform_buffers=uniforms;auto* result=SDL_CreateGPUShader(gpu.device,&info);
        require(result,"Ground position diagnostic shader creation failed");return result;};
    gpu.vs=shader("ground_gbuffer.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX,0,1);gpu.fs=shader("ground_gbuffer.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT,1,1);
    const std::array streams{SDL_GPUVertexBufferDescription{0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0},
                             SDL_GPUVertexBufferDescription{1,sizeof(SurfaceVertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0}};
    const std::array attributes{SDL_GPUVertexAttribute{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,position)},
        SDL_GPUVertexAttribute{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,uv)},
        SDL_GPUVertexAttribute{2,0,SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,offsetof(Vertex,color)},
        SDL_GPUVertexAttribute{3,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(SurfaceVertex,world_position)},
        SDL_GPUVertexAttribute{4,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(SurfaceVertex,normal)}};
    std::array<SDL_GPUColorTargetDescription,4> colors{};colors[0].format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    colors[1].format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;colors[2].format=colors[3].format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};pipeline.vertex_shader=gpu.vs;pipeline.fragment_shader=gpu.fs;
    pipeline.vertex_input_state={streams.data(),static_cast<Uint32>(streams.size()),attributes.data(),static_cast<Uint32>(attributes.size())};
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pipeline.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
    pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;pipeline.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;pipeline.target_info={colors.data(),static_cast<Uint32>(colors.size()),SDL_GPU_TEXTUREFORMAT_INVALID,false,0,0,0};
    gpu.pipeline=SDL_CreateGPUGraphicsPipeline(gpu.device,&pipeline);require(gpu.pipeline,"Ground position MRT pipeline creation failed");
    SDL_GPUSamplerCreateInfo sampler{};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;gpu.sampler=SDL_CreateGPUSampler(gpu.device,&sampler);
    require(gpu.sampler,"Ground position sampler creation failed");
    SDL_GPUTextureCreateInfo texture{SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,SDL_GPU_TEXTUREUSAGE_SAMPLER,1,1,1,1,SDL_GPU_SAMPLECOUNT_1,0};
    gpu.albedo=SDL_CreateGPUTexture(gpu.device,&texture);require(gpu.albedo,"Ground position albedo creation failed");
    constexpr std::array formats{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                 SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
    for(std::size_t i=0;i<gpu.targets.size();++i){texture.format=formats[i];texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=width;texture.height=height;
        gpu.targets[i]=SDL_CreateGPUTexture(gpu.device,&texture);require(gpu.targets[i],"Ground position target creation failed");}
    std::vector<Vertex> vertices;std::vector<SurfaceVertex> surfaces;constexpr Color white{255,255,255,255};
    const std::array<Vec3,4> constants{{{31.99f,-31.99f,2.4f},{-32.f,32.f,-2.4f},{32.f,32.f,32.f},{-32.f,-32.f,-32.f}}};
    const auto append_triangle=[&](std::array<Point,3> screen,std::array<Vec3,3> positions){for(std::size_t i=0;i<3;++i){vertices.push_back({screen[i],{0,0},white});surfaces.push_back({positions[i],{0,0,1}});}};
    for(std::size_t i=0;i<constants.size();++i){const float x=static_cast<float>(i*4);const std::array<Point,4> p{{{x,0},{x+3,0},{x+3,3},{x,3}}};
        append_triangle({p[0],p[1],p[2]},{constants[i],constants[i],constants[i]});append_triangle({p[0],p[2],p[3]},{constants[i],constants[i],constants[i]});}
    const std::array<Point,3> varying_screen{{{2,4},{14,4},{8,8}}};
    const std::array<Vec3,3> varying_positions{{{-32.f,-16.f,0.f},{32.f,-8.f,2.4f},{0.f,32.f,-2.4f}}};append_triangle(varying_screen,varying_positions);
    const auto vertex_bytes=vertices.size()*sizeof(Vertex),surface_bytes=surfaces.size()*sizeof(SurfaceVertex),albedo_offset=vertex_bytes+surface_bytes;
    SDL_GPUBufferCreateInfo buffer{SDL_GPU_BUFFERUSAGE_VERTEX,static_cast<Uint32>(vertex_bytes),0};gpu.buffers[0]=SDL_CreateGPUBuffer(gpu.device,&buffer);
    buffer.size=static_cast<Uint32>(surface_bytes);gpu.buffers[1]=SDL_CreateGPUBuffer(gpu.device,&buffer);require(gpu.buffers[0]&&gpu.buffers[1],"Ground position buffers unavailable");
    SDL_GPUTransferBufferCreateInfo transfer{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,static_cast<Uint32>(albedo_offset+4),0};gpu.upload=SDL_CreateGPUTransferBuffer(gpu.device,&transfer);
    transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;transfer.size=pitch*height;gpu.download=SDL_CreateGPUTransferBuffer(gpu.device,&transfer);require(gpu.upload&&gpu.download,"Ground position transfers unavailable");
    auto* mapped=static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(gpu.device,gpu.upload,false));require(mapped,"Ground position upload map failed");
    std::memcpy(mapped,vertices.data(),vertex_bytes);std::memcpy(mapped+vertex_bytes,surfaces.data(),surface_bytes);std::memset(mapped+albedo_offset,255,4);SDL_UnmapGPUTransferBuffer(gpu.device,gpu.upload);
    Command command(gpu.device);auto* copy=SDL_BeginGPUCopyPass(command.value);require(copy,"Ground position upload pass failed");
    SDL_GPUTransferBufferLocation source{gpu.upload,0};SDL_GPUBufferRegion destination{gpu.buffers[0],0,static_cast<Uint32>(vertex_bytes)};SDL_UploadToGPUBuffer(copy,&source,&destination,false);
    source.offset=static_cast<Uint32>(vertex_bytes);destination={gpu.buffers[1],0,static_cast<Uint32>(surface_bytes)};SDL_UploadToGPUBuffer(copy,&source,&destination,false);
    const SDL_GPUTextureTransferInfo albedo_source{gpu.upload,static_cast<Uint32>(albedo_offset),1,1};const SDL_GPUTextureRegion albedo_destination{gpu.albedo,0,0,0,0,0,1,1,1};
    SDL_UploadToGPUTexture(copy,&albedo_source,&albedo_destination,false);SDL_EndGPUCopyPass(copy);
    std::array<SDL_GPUColorTargetInfo,4> target_info{};for(std::size_t i=0;i<target_info.size();++i){target_info[i].texture=gpu.targets[i];target_info[i].load_op=SDL_GPU_LOADOP_CLEAR;target_info[i].store_op=SDL_GPU_STOREOP_STORE;}
    auto* pass=SDL_BeginGPURenderPass(command.value,target_info.data(),static_cast<Uint32>(target_info.size()),nullptr);require(pass,"Ground position render pass failed");
    SDL_BindGPUGraphicsPipeline(pass,gpu.pipeline);const SDL_GPUViewport viewport{0,0,width,height,0,1};SDL_SetGPUViewport(pass,&viewport);
    const std::array bindings{SDL_GPUBufferBinding{gpu.buffers[0],0},SDL_GPUBufferBinding{gpu.buffers[1],0}};SDL_BindGPUVertexBuffers(pass,0,bindings.data(),2);
    const std::array<float,4> extent{width,height,0,0};SDL_PushGPUVertexUniformData(command.value,0,extent.data(),sizeof(extent));GroundMaterialGpu material;
    SDL_PushGPUFragmentUniformData(command.value,0,&material,sizeof(material));const SDL_GPUTextureSamplerBinding albedo_binding{gpu.albedo,gpu.sampler};SDL_BindGPUFragmentSamplers(pass,0,&albedo_binding,1);
    SDL_DrawGPUPrimitives(pass,static_cast<Uint32>(vertices.size()),1,0,0);SDL_EndGPURenderPass(pass);copy=SDL_BeginGPUCopyPass(command.value);require(copy,"Ground position download pass failed");
    const SDL_GPUTextureRegion world_region{gpu.targets[1],0,0,0,0,0,width,height,1};const SDL_GPUTextureTransferInfo world_download{gpu.download,0,pitch/8,height};
    SDL_DownloadFromGPUTexture(copy,&world_region,&world_download);SDL_EndGPUCopyPass(copy);auto* fence=command.submit();const std::array fences{fence};
    const bool waited=SDL_WaitForGPUFences(gpu.device,true,fences.data(),1);SDL_ReleaseGPUFence(gpu.device,fence);require(waited,"Ground position fence wait failed");
    const auto* downloaded=static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(gpu.device,gpu.download,false));require(downloaded,"Ground position readback map failed");
    std::vector<std::uint8_t> raw(width*height*8);for(int y=0;y<height;++y)std::memcpy(raw.data()+y*width*8,downloaded+y*pitch,width*8);SDL_UnmapGPUTransferBuffer(gpu.device,gpu.download);
    write_bytes(output/"ground-position-rgba16f.bin",raw);
    const auto read_pixel=[&](int x,int y){std::array<float,4> value{};const auto offset=(static_cast<std::size_t>(y)*width+x)*8;
        for(std::size_t c=0;c<4;++c)value[c]=half_float(static_cast<std::uint16_t>(raw[offset+c*2]|raw[offset+c*2+1]<<8u));return value;};
    Json cases=Json::array();float max_component=0,max_euclidean=0;for(std::size_t i=0;i<constants.size();++i){const auto actual=read_pixel(static_cast<int>(i*4+1),1);float squared=0;
        for(std::size_t c=0;c<3;++c){const float error=std::abs(actual[c]-constants[i][c]);max_component=std::max(max_component,error);squared+=error*error;}
        const float euclidean=std::sqrt(squared);max_euclidean=std::max(max_euclidean,euclidean);require(actual[3]==1.f&&max_component<=.0078125f&&euclidean<.02f,"Ground RGBA16F constant position precision exceeded bound");
        cases.push_back({{"id","constant-"+std::to_string(i)},{"expected",constants[i]},{"actual",actual},{"euclidean_error",euclidean}});}
    const Point sample{8.5f,5.5f};const auto& a=varying_screen[0];const auto& b=varying_screen[1];const auto& c=varying_screen[2];
    const float divisor=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);const float w0=((b.y-c.y)*(sample.x-c.x)+(c.x-b.x)*(sample.y-c.y))/divisor;
    const float w1=((c.y-a.y)*(sample.x-c.x)+(a.x-c.x)*(sample.y-c.y))/divisor,w2=1-w0-w1;Vec3 expected{};
    for(std::size_t axis=0;axis<3;++axis)expected[axis]=w0*varying_positions[0][axis]+w1*varying_positions[1][axis]+w2*varying_positions[2][axis];
    const auto varying_actual=read_pixel(8,5);float varying_squared=0;for(std::size_t axis=0;axis<3;++axis){const float error=std::abs(varying_actual[axis]-expected[axis]);max_component=std::max(max_component,error);varying_squared+=error*error;}
    const float varying_euclidean=std::sqrt(varying_squared);max_euclidean=std::max(max_euclidean,varying_euclidean);
    require(varying_actual[3]==1.f&&max_component<=.0078125f&&varying_euclidean<.02f,"Ground RGBA16F varying position precision exceeded bound");
    cases.push_back({{"id","affine-interior"},{"physical_pixel",{8,5}},{"barycentric",{w0,w1,w2}},{"expected",expected},{"actual",varying_actual},{"euclidean_error",varying_euclidean}});
    auto result=gpu_identity(gpu.device);result["scope"]="independent production GroundGBuffer shader/MRT readback; not Renderer2D frame-graph proof";
    result["format"]="R16G16B16A16_FLOAT";result["component_domain"]={-32,32};result["max_component_error"]=max_component;result["max_euclidean_error"]=max_euclidean;
    result["component_tolerance"]=.0078125;result["euclidean_tolerance"]=.02;result["raw_readback"]="ground-position-rgba16f.bin";
    result["vertex_shader_sha256"]=sha256(read_bytes(runtime_assets/"shaders/ground_gbuffer.vert.dxil"));
    result["fragment_shader_sha256"]=sha256(read_bytes(runtime_assets/"shaders/ground_gbuffer.frag.dxil"));result["cases"]=cases;return result;
}

struct Kit {
    AssetCatalog assets; Json calibration,world,manifest,master; std::filesystem::path master_path;
    std::array<Vec3,3> slope_a{},slope_b{}; Vec3 slope_normal{};
    explicit Kit(const std::filesystem::path& root):assets(root) {
        assets.verify(); calibration=parse_json(root/"art/material/calibration.json"); world=parse_json(root/"art/world.json"); manifest=parse_json(root/"manifest.json");
        for(auto candidate=std::filesystem::absolute(root);!candidate.empty();candidate=candidate.parent_path()) {
            const auto path=candidate/"art-source/engine-calibration/material-master.json";
            if(std::filesystem::is_regular_file(path)){master_path=path;break;}
            if(candidate==candidate.root_path())break;
        }
        require(!master_path.empty(),"Could not locate the editable calibration master above the kit root"); master=parse_json(master_path);
        require(calibration.at("schema")==1&&world.at("schema")==2&&manifest.at("schema")==1,"Unsupported source kit schema");
        for(const auto& triangle:master.at("triangles")) {
            if(triangle.at("id")=="slope.a")slope_a=triangle.at("vertices").get<std::array<Vec3,3>>();
            if(triangle.at("id")=="slope.b")slope_b=triangle.at("vertices").get<std::array<Vec3,3>>();
        }
        slope_normal=normalized(cross(subtract(slope_a[1],slope_a[0]),subtract(slope_a[2],slope_a[0])));
        require(std::abs(dot(slope_normal,calibration.at("probes").at(10).at("normal").get<Vec3>())-1.f)<1.e-5f,"Editable slope and export metadata disagree");
    }
};

void append_ground_triangle(Frame2D& frame,const std::array<Vec3,3>& points,Vec3 normal,Point origin,float zoom,std::uint32_t material) {
    const auto first=static_cast<std::uint32_t>(frame.vertices.size());
    constexpr std::array<Point,3> uv{{{0,0},{1,0},{0,1}}};
    for(std::size_t i=0;i<3;++i) { const auto projected=project_render_xyz(points[i]);
        frame.vertices.push_back({{origin.x+projected.x*zoom,origin.y+projected.y*zoom},uv[i],{255,255,255,255}});
        frame.surface_vertices.push_back({points[i],normal}); frame.indices.push_back(first+static_cast<std::uint32_t>(i)); }
    GeometryDraw draw{Pass::Terrain,Blend::Opaque,{},static_cast<std::uint32_t>(frame.indices.size()-3),3};
    draw.ground_material_index=material; frame.commands.emplace_back(draw);
}
void append_quad(Frame2D& frame,Rect r,Color color,Pass pass) {
    const auto first=static_cast<std::uint32_t>(frame.vertices.size()),index=static_cast<std::uint32_t>(frame.indices.size());
    frame.vertices.insert(frame.vertices.end(),{{{r.x,r.y},{0,0},color},{{r.x+r.w,r.y},{1,0},color},{{r.x+r.w,r.y+r.h},{1,1},color},{{r.x,r.y+r.h},{0,1},color}});
    frame.surface_vertices.insert(frame.surface_vertices.end(),4,SurfaceVertex{{0,0,0},{0,0,1}});
    frame.indices.insert(frame.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    frame.commands.emplace_back(GeometryDraw{pass,Blend::Alpha,{},index,6});
}
struct ScenePoints {Point slope{},flat{},window{},facade{},ui{48,48};};
Vec3 scene_sun(bool slope_favoring,const Kit& kit) { return slope_favoring?kit.slope_normal:Vec3{0,0,1}; }
Frame2D make_scene(Renderer2D& renderer,GameArt& art,const Kit& kit,TextureHandle ground,
                   bool slope_sun,bool emission_on,ScenePoints& samples) {
    Frame2D frame; frame.logical_size=logical; frame.clear={8,12,18,255}; frame.linear_hdr_world=true; frame.world_exposure=.72f;
    // Main causal oracle isolates diffuse normal response: dry, zero-roughness
    // controls make the narrow specular lobe negligible for this camera.
    frame.ground_material_draws.push_back({ground,{{0.f,0.f,0.f,1.f},{1,0,0,0},{1.f,1.f,0,0}}});
    constexpr Point origin{490,390}; constexpr float zoom=1.45f;
    append_ground_triangle(frame,kit.slope_a,kit.slope_normal,origin,zoom,1); append_ground_triangle(frame,kit.slope_b,kit.slope_normal,origin,zoom,1);
    std::array<Vec3,3> flat_a=kit.slope_a,flat_b=kit.slope_b; for(auto* triangle:{&flat_a,&flat_b})for(auto& p:*triangle){p[0]+=6.f;p[2]=0.f;}
    append_ground_triangle(frame,flat_a,{0,0,1},origin,zoom,1); append_ground_triangle(frame,flat_b,{0,0,1},origin,zoom,1);
    const auto bary=[](const std::array<Vec3,3>& t){return Vec3{.3f*t[0][0]+.3f*t[1][0]+.4f*t[2][0],.3f*t[0][1]+.3f*t[1][1]+.4f*t[2][1],.3f*t[0][2]+.3f*t[1][2]+.4f*t[2][2]};};
    const auto slope=bary(kit.slope_a); const auto flat=bary(flat_a);
    const auto sp=project_render_xyz(slope); const auto fp=project_render_xyz(flat);
    samples.slope={origin.x+sp.x*zoom,origin.y+sp.y*zoom}; samples.flat={origin.x+fp.x*zoom,origin.y+fp.y*zoom};
    auto& lighting=frame.surface_lighting; lighting.ambient={.025f,.025f,.025f,0}; const Vec3 sun=scene_sun(slope_sun,kit);
    lighting.sun_direction={sun[0],sun[1],sun[2],0}; lighting.sun_color_intensity={1,1,1,5.f};
    constexpr Point cabin_ground{950,565}; art.lit_sprite(frame,emission_on?"calibration.on":"calibration.off",cabin_ground,{0,0,0},1.25f);
    frame.surface_vertices.resize(frame.vertices.size(),SurfaceVertex{{0,0,0},{0,0,1}});
    frame.sprite_material_draws.back().reveal=1.f;
    const auto& window=kit.calibration.at("probes").at(3); const Point source{window.at("source_pixel").at(0),window.at("source_pixel").at(1)};
    const Point pivot{kit.calibration.at("pivot_px").at(0),kit.calibration.at("pivot_px").at(1)};
    const float eppp=kit.calibration.at("export_pixels_per_reference_pixel"); const auto offset=screen_offset_from_source_pixel(source,pivot,eppp,1.25f);
    samples.window={cabin_ground.x+offset.x,cabin_ground.y+offset.y};
    const auto& facade=kit.calibration.at("probes").at(0); const Point facade_source{facade.at("source_pixel").at(0),facade.at("source_pixel").at(1)};
    const auto facade_offset=screen_offset_from_source_pixel(facade_source,pivot,eppp,1.25f);
    samples.facade={cabin_ground.x+facade_offset.x,cabin_ground.y+facade_offset.y};
    append_quad(frame,{32,32,32,32},{37,211,149,255},Pass::Ui); (void)renderer; return frame;
}
Json telemetry_json(const RendererFrameTelemetry& t) {
    Json passes=Json::array(); for(std::uint32_t i=0;i<t.graph_pass_count;++i)passes.push_back(pass_name(t.graph_passes[i]));
    return {{"sequence",t.sequence},{"frame_slot",t.frame_slot},{"graph_pass_count",t.graph_pass_count},{"graph_passes",passes},
            {"submitted",t.submitted},{"capture",t.capture},{"ground_draws",t.ground_draws},{"material_draws",t.material_draws},
            {"gpu_execution_available",t.gpu_execution_ms.has_value()}};
}
Json production_captures(SDL_Window* window,const std::filesystem::path& runtime_assets,const std::filesystem::path& output,const Kit& kit) {
    RendererOptions options; options.backend=Backend::Direct3D12; options.world_graph=WorldGraph::GroundForwardSlice; options.collect_telemetry=true;
    options.presentation.frames_in_flight=2; Renderer2D renderer(window,runtime_assets,options); renderer.set_logical_size(logical);
    require(renderer.device_info().backend=="direct3d12"&&!renderer.device_info().software,"Production probe requires native D3D12");
    require(renderer.ground_forward_status().available,"Ground/forward production graph unavailable");
    GameArt art(renderer); art.load(kit.assets); require(art.contains_lit("calibration.on")&&art.contains_lit("calibration.off"),"Fresh schema-2 cabin kit did not install");
    const std::array<std::uint8_t,4> albedo{82,96,86,255}; const auto ground=renderer.create_sampled_texture({{1,1},SampledTextureFormat::Rgba8UnormSrgb},albedo);
    Json telemetry=Json::array(); std::array<std::vector<std::uint8_t>,4> capture_bytes; std::array<std::array<int,4>,4> slope{},flat{},window_sample{},ui{};
    // State and slot are deliberately crossed: each state must reproduce on both physical frame sets.
    constexpr std::array names{"A0","B1","B0","A1"}; constexpr std::array state_b{false,true,true,false};
    for(std::size_t i=0;i<names.size();++i) {
        ScenePoints points; auto frame=make_scene(renderer,art,kit,ground,state_b[i],state_b[i],points); const auto path=output/(std::string(names[i])+".png");
        renderer.render(frame,path); const auto& measured=renderer.frame_telemetry(); require(measured.frame_slot==i%2&&measured.graph_pass_count==5,"Frame slot/pass telemetry mismatch");
        for(std::size_t p=0;p<5;++p)require(measured.graph_passes[p]==static_cast<WorldGraphPass>(p),"Production graph pass order changed");
        auto row=telemetry_json(measured); row["state"]=state_b[i]?"B":"A"; row["capture_id"]=names[i]; telemetry.push_back(std::move(row)); capture_bytes[i]=read_bytes(path);
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface); require(image!=nullptr,"Could not decode production capture");
        slope[i]=logical_pixel(image.get(),points.slope); flat[i]=logical_pixel(image.get(),points.flat); window_sample[i]=logical_pixel(image.get(),points.window); ui[i]=logical_pixel(image.get(),points.ui);
    }
    require(renderer.ground_forward_status().frame_sets==2&&renderer.ground_forward_status().creation_attempts>=14,
            "Production captures did not allocate both ground/forward frame sets");
    const auto sun_a=scene_sun(false,kit),sun_b=scene_sun(true,kit); const Vec3 flat_normal{0,0,1};
    write_json(output/"production-samples.json",{{"capture_order",names},{"state_b",state_b},{"slope_rgba",slope},{"flat_rgba",flat},
        {"window_rgba",window_sample},{"ui_rgba",ui},{"brightness",{{"slope",{brightness(slope[0]),brightness(slope[1]),brightness(slope[2]),brightness(slope[3])}},
         {"flat",{brightness(flat[0]),brightness(flat[1]),brightness(flat[2]),brightness(flat[3])}}}},
        {"sun_a",sun_a},{"sun_b",sun_b},{"slope_normal",kit.slope_normal},{"ground_response",{{"roughness",0},{"wetness",0},{"shore",0},{"reveal",1}}},
        {"ndot",{{"a_flat",dot(flat_normal,sun_a)},{"a_slope",dot(kit.slope_normal,sun_a)},
                   {"b_flat",dot(flat_normal,sun_b)},{"b_slope",dot(kit.slope_normal,sun_b)}}}});
    require(capture_bytes[0]==capture_bytes[3]&&capture_bytes[1]==capture_bytes[2],"Cross-slot two-set reuse was not byte-exact");
    require(brightness(flat[0])>brightness(slope[0])&&brightness(slope[1])>brightness(flat[1])&&
            brightness(slope[1])>brightness(slope[0])&&brightness(flat[0])>brightness(flat[1]),
            "Diffuse sun direction did not produce both reversed receiver ordering and causal per-receiver deltas");
    require(window_sample[0]!=window_sample[1],"Cabin window did not react causally to emission/sun state");
    for(const auto& p:ui)require(p==std::array<int,4>{37,211,149,255},"UI reference color changed in production graph");
    std::array<std::array<int,4>,2> emission_window{},emission_facade{};
    for(std::size_t i=0;i<2;++i) {
        ScenePoints points; auto frame=make_scene(renderer,art,kit,ground,false,i==1,points);
        const auto path=output/(i?"emission-on-same-sun.png":"emission-off-same-sun.png"); renderer.render(frame,path);
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface); require(image!=nullptr,"Could not decode emission control");
        emission_window[i]=logical_pixel(image.get(),points.window); emission_facade[i]=logical_pixel(image.get(),points.facade);
    }
    const std::array<int,3> emission_delta{emission_window[1][0]-emission_window[0][0],emission_window[1][1]-emission_window[0][1],
                                            emission_window[1][2]-emission_window[0][2]};
    require(emission_delta[0]>=8&&emission_delta[0]>emission_delta[2],"Window emission did not produce a nontrivial positive warm-channel response under a fixed sun");
    require(emission_facade[0]==emission_facade[1],"Non-emissive facade changed in the fixed-sun emission control");
    Vec3 slope_centroid{};for(const auto& point:kit.slope_a)for(std::size_t axis=0;axis<3;++axis)slope_centroid[axis]+=point[axis]/3.f;
    Vec3 flat_centroid=slope_centroid;flat_centroid[0]+=6.f;flat_centroid[2]=0.f;
    const Vec3 slope_light_anchor{slope_centroid[0],slope_centroid[1],slope_centroid[2]+1.f};
    const Vec3 flat_light_anchor{flat_centroid[0],flat_centroid[1],flat_centroid[2]+1.f};
    struct LocalSamples {std::array<int,4> slope{},flat{},ui{};};
    const auto local_capture=[&](std::string_view name,Vec3 anchor,bool spot,bool aimed){
        ScenePoints points;auto frame=make_scene(renderer,art,kit,ground,false,false,points);auto& lighting=frame.surface_lighting;
        lighting.sun_color_intensity={0,0,0,0};lighting.counts[0]=1;auto& light=lighting.lights[0];
        light.anchor_radius={anchor[0],anchor[1],anchor[2],2.5f};light.color_intensity={1.f,.6f,.2f,5.f};
        if(spot){light.direction_cone={0,0,aimed?-1.f:1.f,.6f};light.height_falloff_reveal[0]=.9f;}
        const auto path=output/(std::string(name)+".png");renderer.render(frame,path);require(renderer.frame_telemetry().graph_pass_count==5,"Local-light capture bypassed the production graph");
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface);require(image!=nullptr,"Could not decode local-light capture");
        LocalSamples result{logical_pixel(image.get(),points.slope),logical_pixel(image.get(),points.flat),logical_pixel(image.get(),points.ui)};
        require(result.ui==ui[0],"Local-light control changed the UI reference pixel");return result;
    };
    const auto point_slope=local_capture("point-above-slope",slope_light_anchor,false,true);
    const auto point_flat=local_capture("point-above-flat",flat_light_anchor,false,true);
    require(brightness(point_slope.slope)>=brightness(point_slope.flat)+16&&brightness(point_flat.flat)>=brightness(point_flat.slope)+16,
            "Moving the world-anchored point light did not reverse ground response with the required luminance margin");
    require(brightness(point_slope.slope)>=brightness(point_flat.slope)+16&&brightness(point_flat.flat)>=brightness(point_slope.flat)+16,
            "Moving the world-anchored point light did not produce the required cross-state receiver response");
    const auto spot_aimed=local_capture("spot-aimed-at-slope",slope_light_anchor,true,true);
    const auto spot_away=local_capture("spot-aimed-away",slope_light_anchor,true,false);
    require(brightness(spot_aimed.slope)>=brightness(spot_away.slope)+16,"Spot direction did not causally change the slope response");
    require(spot_aimed.flat==spot_away.flat,"Ground outside the spot radius changed when only cone direction changed");
    const Json local_light_control{{"scope","production unoccluded local-light/world-position response; no occlusion claim"},
        {"slope_centroid",slope_centroid},{"flat_centroid",flat_centroid},{"slope_light_anchor",slope_light_anchor},{"flat_light_anchor",flat_light_anchor},
        {"radius",2.5f},{"color_intensity",{1.f,.6f,.2f,5.f}},{"ambient",.025f},{"sun_intensity",0},
        {"spot",{{"aimed_direction",{0,0,-1}},{"away_direction",{0,0,1}},{"outer_cosine",.6f},{"inner_cosine",.9f}}},
        {"point_above_slope",{{"slope",point_slope.slope},{"flat",point_slope.flat},{"ui",point_slope.ui}}},
        {"point_above_flat",{{"slope",point_flat.slope},{"flat",point_flat.flat},{"ui",point_flat.ui}}},
        {"spot_aimed",{{"slope",spot_aimed.slope},{"flat",spot_aimed.flat},{"ui",spot_aimed.ui}}},
        {"spot_away",{{"slope",spot_away.slope},{"flat",spot_away.flat},{"ui",spot_away.ui}}}};
    const auto initial=status_json(renderer.ground_forward_status()); const auto drawable=renderer.display_info().drawable; Json modes=Json::array();
    for(const auto mode:{PresentMode::Immediate,PresentMode::Mailbox,PresentMode::VSync}) {
        const auto before=renderer.presentation_status(); const bool advertised=mode==PresentMode::Immediate?before.immediate_supported:mode==PresentMode::Mailbox?before.mailbox_supported:true;
        const auto mode_name=std::string(mode==PresentMode::Immediate?"immediate":mode==PresentMode::Mailbox?"mailbox":"vsync");
        bool applied=false; Json row{{"mode",mode_name},{"advertised",advertised}};
        if(advertised){
            auto request=before.effective; request.mode=mode; applied=renderer.set_presentation(request); require(applied,"Advertised presentation mode could not be applied");
            ScenePoints points; auto frame=make_scene(renderer,art,kit,ground,false,false,points); const auto path=output/("presentation-"+mode_name+".png"); renderer.render(frame,path);
            const auto measured=renderer.frame_telemetry(); require(measured.submitted&&measured.graph_pass_count==5,"Presentation-mode graph capture was not submitted through five passes");
            for(std::size_t p=0;p<5;++p)require(measured.graph_passes[p]==static_cast<WorldGraphPass>(p),"Presentation-mode graph pass order changed");
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface); require(image!=nullptr,"Could not decode presentation-mode capture");
            const auto mode_slope=logical_pixel(image.get(),points.slope),mode_flat=logical_pixel(image.get(),points.flat);
            const auto mode_window=logical_pixel(image.get(),points.window),mode_ui=logical_pixel(image.get(),points.ui);
            require(mode_slope==slope[0]&&mode_flat==flat[0]&&mode_window==window_sample[0]&&mode_ui==ui[0],
                    "Supported presentation mode changed known production graph reference pixels");
            row["capture"]=path.filename().string(); row["telemetry"]=telemetry_json(measured);
            row["reference_pixels"]={{"slope",mode_slope},{"flat",mode_flat},{"window",mode_window},{"ui",mode_ui}};
        }
        row["applied"]=applied; modes.push_back(std::move(row));
    }
    Json resize=Json::array(); bool letterbox_verified=false;
    for(const auto size:{std::array<int,2>{1120,630},std::array<int,2>{800,600},std::array<int,2>{1280,720}}) {
        const auto before_resize=renderer.ground_forward_status();
        require(SDL_SetWindowSize(window,size[0],size[1]),"Repeated-resize window change failed"); SDL_SyncWindow(window); SDL_PumpEvents();
        ScenePoints points; auto frame=make_scene(renderer,art,kit,ground,false,false,points); const auto path=output/("resize-"+std::to_string(size[0])+"x"+std::to_string(size[1])+".png");
        renderer.render(frame,path); const auto display=renderer.display_info(); const auto after_resize=renderer.ground_forward_status(); Json letterbox;
        require(after_resize.width==display.drawable.width&&after_resize.height==display.drawable.height&&after_resize.frame_sets==2&&after_resize.candidate_live_textures==0,
                "Repeated resize status disagrees with the actual drawable or two-set contract");
        require(after_resize.target_generation==before_resize.target_generation+1&&after_resize.active_set_ids!=before_resize.active_set_ids,
                "Repeated resize did not atomically advance target generation and identities");
        require(after_resize.attachment_bytes==std::uint64_t(display.drawable.width)*display.drawable.height*2u*36u,
                "Repeated resize attachment byte ledger disagrees with the actual drawable");
        if(size==std::array<int,2>{800,600}) {
            require(display.drawable.width*3u==display.drawable.height*4u,
                    "Requested 800x600 control did not produce a 4:3 drawable; letterbox proof cannot be skipped");
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface);
            require(image!=nullptr,"Could not decode 4:3 resize capture");
            const auto resized_slope=logical_pixel(image.get(),points.slope),resized_flat=logical_pixel(image.get(),points.flat),resized_ui=logical_pixel(image.get(),points.ui);
            const auto upper=pixel(image.get(),image->w/2,20),lower=pixel(image.get(),image->w/2,image->h-20);
            // Composite applies exposure in linear light, then the standard SDR
            // transfer. This is an analytic oracle, not a value copied from readback.
            const std::array<int,4> encoded_clear{linear_to_srgb_byte(srgb_byte_to_linear(frame.clear.r)*frame.world_exposure),
                linear_to_srgb_byte(srgb_byte_to_linear(frame.clear.g)*frame.world_exposure),
                linear_to_srgb_byte(srgb_byte_to_linear(frame.clear.b)*frame.world_exposure),255};
            require(channels_near(resized_slope,slope[0],2)&&channels_near(resized_flat,flat[0],2),
                    "4:3 letterbox changed known ground samples beyond raster tolerance");
            require(resized_ui==ui[0],"4:3 letterbox changed the unchanged UI reference pixel");
            require(upper==encoded_clear&&lower==encoded_clear,"4:3 letterbox bands did not retain the encoded frame clear color");
            letterbox={{"slope",resized_slope},{"flat",resized_flat},{"ui",resized_ui},{"upper_band",upper},{"lower_band",lower},
                       {"expected_band",encoded_clear},{"ground_reference_tolerance",2}};
            letterbox_verified=true;
        }
        resize.push_back({{"requested_window_points",size},{"actual_drawable",{display.drawable.width,display.drawable.height}},
                          {"before",status_json(before_resize)},{"after",status_json(after_resize)},{"capture",path.filename().string()},
                          {"letterbox_samples",letterbox}});
    }
    require(letterbox_verified,"4:3 letterbox control did not execute");
    return {{"backend",renderer.device_info().backend},{"device",renderer.device_info().device},{"driver",renderer.device_info().driver},
            {"drawable",{drawable.width,drawable.height}},{"logical",{logical.width,logical.height}},
            {"initial_status",initial},{"telemetry",telemetry},{"samples",{{"slope",slope},{"flat",flat},{"window",window_sample},{"ui",ui},
             {"fixed_sun_emission_window",emission_window},{"fixed_sun_nonemissive_facade",emission_facade}}},
            {"fixed_sun_emission_delta_rgb",emission_delta},
            {"local_light_control",local_light_control},
            {"normal_control",{{"slope_normal",kit.slope_normal},{"flat_normal",{0,0,1}},
             {"sun_a",sun_a},{"sun_b",sun_b},{"a_flat_dot",dot(flat_normal,sun_a)},{"a_slope_dot",dot(kit.slope_normal,sun_a)},
             {"b_flat_dot",dot(flat_normal,sun_b)},{"b_slope_dot",dot(kit.slope_normal,sun_b)},
             {"ground_response",{{"roughness",0},{"wetness",0},{"shore",0},{"reveal",1}}}}},
            {"byte_exact_cross_slot_reuse",{{"A0_slot0_A1_slot1",true},{"B1_slot1_B0_slot0",true}}},{"presentation_modes",modes},
            {"letterbox_verified",letterbox_verified},{"repeated_resize",resize}};
}

Json resize_faults(const std::filesystem::path& runtime_assets,const std::filesystem::path& output) {
    Json results=Json::array();
    for(const auto attempt:{15u,21u,28u}) {
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow(("Ground fault "+std::to_string(attempt)).c_str(),640,360,SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);
        require(window!=nullptr,"Fault probe window unavailable"); SDL_SyncWindow(window.get());
        RendererOptions options; options.backend=Backend::Direct3D12; options.world_graph=WorldGraph::GroundForwardSlice;
        options.test_fail_ground_target_creation_at=attempt; options.collect_telemetry=true;
        Renderer2D renderer(window.get(),runtime_assets,options); renderer.set_logical_size(logical);
        const std::array<std::uint8_t,4> albedo{82,96,86,255}; const auto ground=renderer.create_sampled_texture({{1,1},SampledTextureFormat::Rgba8UnormSrgb},albedo);
        Frame2D frame; frame.logical_size=logical; frame.clear={8,12,18,255}; frame.linear_hdr_world=true;
        frame.ground_material_draws.push_back({ground,{}});
        append_ground_triangle(frame,{{Vec3{-1,-1,0},Vec3{1,-1,0},Vec3{0,1,0}}},{0,0,1},{320,180},40,1);
        std::array<std::vector<std::uint8_t>,2> before_bytes;
        for(std::size_t slot=0;slot<2;++slot) {
            const auto path=output/("resize-fault-"+std::to_string(attempt)+"-before-slot"+std::to_string(slot)+".png");
            renderer.render(frame,path); require(renderer.frame_telemetry().frame_slot==slot,"Fault precondition did not prime the expected active slot");
            before_bytes[slot]=read_bytes(path);
        }
        require(before_bytes[0]==before_bytes[1],"Initial active target sets did not render the same known frame");
        const auto before=renderer.ground_forward_status();
        require(before.creation_attempts==14&&before.frame_sets==2,
                "Fault renderer did not prime two active sets with fourteen target creations");
        require(SDL_SetWindowSize(window.get(),800,450),"Fault resize request failed"); SDL_SyncWindow(window.get()); SDL_PumpEvents();
        std::string diagnostic; try{renderer.render(frame);}catch(const std::runtime_error& e){diagnostic=e.what();}
        const auto failed=renderer.ground_forward_status();
        require(std::string_view(diagnostic).starts_with("Injected ground target creation failure: ")&&failed.creation_attempts==attempt,
                "Resize rejection was not the exact requested ground-target allocation fault");
        require(failed.active_set_ids==before.active_set_ids&&failed.width==before.width&&failed.height==before.height&&
                failed.target_generation==before.target_generation&&failed.candidate_live_textures==0,"Failed resize mutated active target sets or leaked candidate textures");
        require(SDL_SetWindowSize(window.get(),640,360),"Fault rollback size restore failed"); SDL_SyncWindow(window.get()); SDL_PumpEvents();
        std::array<std::vector<std::uint8_t>,2> rollback_bytes;
        for(std::size_t render_index=0;render_index<2;++render_index) {
            const auto path=output/("resize-fault-"+std::to_string(attempt)+"-rollback-render"+std::to_string(render_index)+".png");
            renderer.render(frame,path); rollback_bytes[renderer.frame_telemetry().frame_slot]=read_bytes(path);
        }
        require(rollback_bytes==before_bytes,"Known-frame pixels in one or both active sets changed after failed candidate rollback");
        const auto rolled_back=renderer.ground_forward_status();
        require(rolled_back.active_set_ids==before.active_set_ids&&rolled_back.target_generation==before.target_generation,
                "Rendering the rolled-back active sets changed their identity or generation");
        require(SDL_SetWindowSize(window.get(),800,450),"Fault recovery resize request failed"); SDL_SyncWindow(window.get()); SDL_PumpEvents();
        std::array<std::vector<std::uint8_t>,2> recovery_bytes; std::array<std::string,2> recovery_names;
        for(std::size_t render_index=0;render_index<2;++render_index) {
            const auto path=output/("resize-fault-"+std::to_string(attempt)+"-recovery-render"+std::to_string(render_index)+".png");
            renderer.render(frame,path); const auto slot=renderer.frame_telemetry().frame_slot; recovery_bytes[slot]=read_bytes(path); recovery_names[slot]=path.filename().string();
        }
        require(recovery_bytes[0]==recovery_bytes[1],"Freshly recovered target sets did not render the same known frame");
        const auto recovered=renderer.ground_forward_status();
        require(recovered.available&&recovered.target_generation>before.target_generation&&recovered.active_set_ids!=before.active_set_ids&&recovered.candidate_live_textures==0,
                "Subsequent resize recovery did not atomically install two fresh sets");
        results.push_back({{"failure_creation_attempt",attempt},{"before",status_json(before)},{"diagnostic",diagnostic},{"after_failure",status_json(failed)},
                           {"after_rollback_renders",status_json(rolled_back)},{"rollback_both_slots_byte_exact",true},
                           {"after_recovery",status_json(recovered)},{"recovery_both_slots_byte_exact",true},{"recovery_captures",recovery_names}});
    }
    return results;
}

int software_contract(const std::filesystem::path& runtime_assets) {
    GroundMaterialGpu ground; require(ground.response==std::array<float,4>{1,0,0,1}&&ground.flags==std::array<std::uint32_t,4>{1,0,0,0}&&
        ground.visibility==std::array<float,4>{1,1,0,0},"Ground material ABI defaults changed");
    SpriteMaterialDraw sprite; require(sprite.reveal==1.f,"Forward reveal default changed"); Frame2D frame; frame.linear_hdr_world=true; frame.ground_material_draws.push_back({}); frame.clear_commands();
    require(!frame.linear_hdr_world&&frame.ground_material_draws.empty()&&frame.sprite_material_draws.empty()&&frame.surface_vertices.empty(),"Frame clear contract changed");
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Ground graph software rejection",320,180,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(window!=nullptr,"Software contract window unavailable"); RendererOptions options; options.backend=Backend::Software; options.world_graph=WorldGraph::GroundForwardSlice;
    bool rejected=false; try{Renderer2D renderer(window.get(),runtime_assets,options);}
    catch(const std::invalid_argument& error){rejected=std::string_view(error.what())=="GroundForwardSlice requires the GPU backend";}
    require(rejected,"Software backend accepted the native ground/forward graph");
    std::cout<<"Ground/forward ABI defaults and software rejection contracts passed.\n"; return 0;
}
} // namespace

int main(int argc,char** argv) {
    try {
        require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization failed"); struct Quit{~Quit(){SDL_Quit();}} quit;
        const auto* base=SDL_GetBasePath(); require(base,"Runtime base path unavailable"); const auto runtime_assets=from_utf8(base)/"assets";
        if(argc==2&&std::string_view(argv[1])=="--software-contract")return software_contract(runtime_assets);
        require(argc==3,"Usage: GroundForwardProbe <fresh-kit-root> <NEW-output-root> | --software-contract");
        const auto kit_root=from_utf8(argv[1]),output=from_utf8(argv[2]); require(!std::filesystem::exists(output),"Output already exists; retained evidence is never reused or overwritten");
        Kit kit(kit_root); require(std::filesystem::create_directories(output),"Output was concurrently created; refusing to reuse it");
        const auto blend=rg16f_blend_control(runtime_assets);
        const auto position_precision=ground_position_precision_control(runtime_assets,output);
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Ribterra production ground/forward probe",logical.width,logical.height,SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);
        require(window!=nullptr,"Production probe window unavailable"); SDL_SyncWindow(window.get()); SDL_PumpEvents();
        const auto production=production_captures(window.get(),runtime_assets,output,kit); window.reset();
        for(const auto* control:{&blend,&position_precision})for(const auto* field:{"backend","device","driver"})
            require(control->at(field)==production.at(field),std::string("Independent GPU control selected a different production ")+field);
        const auto faults=resize_faults(runtime_assets,output); const auto executable=from_utf8(base)/"GroundForwardProbe.exe";
        write_json(output/"ground-forward-report.json",{{"schema",1},{"complete",true},{"build",RIBTERRA_PROBE_BUILD},{"platform",SDL_GetPlatform()},
            {"probe_executable_sha256",sha256(read_bytes(executable))},{"source_kit",{{"root",utf8(kit_root)},{"content_sha256",kit.assets.content_hash()},
             {"manifest_sha256",sha256(read_bytes(kit_root/"manifest.json"))},{"world_sha256",sha256(read_bytes(kit_root/"art/world.json"))},
             {"calibration_sha256",sha256(read_bytes(kit_root/"art/material/calibration.json"))},{"editable_master_path",utf8(kit.master_path)},
             {"editable_master_sha256",sha256(read_bytes(kit.master_path))}}},
            {"production_shader_manifest_sha256",sha256(read_bytes(runtime_assets/"shaders/manifest.json"))},{"production",production},{"rg16f_source_alpha_control",blend},
            {"ground_position_precision_control",position_precision},{"gpu_identity_matches_production",true},
            {"resize_faults",faults},{"claims",{"production GroundGBuffer/GroundLighting/WorldForward/Composite/UI routing","two-set byte-exact reuse","causal ground and cabin response","unchanged UI","transactional resize recovery","native RG16F source-alpha color blending"}},
            {"not_claimed",{"complete E01-E17 acceptance","Reflection","FoW secrecy","shadows or occlusion","atmosphere","Dynamic GameView","quality settings","GPU timing","FPS","desktop scan-out"}}});
        std::cout<<"Production ground/forward captures, two-set reuse, RG16F blending, and resize rollback passed; evidence retained in "<<utf8(output)<<'\n'; return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
