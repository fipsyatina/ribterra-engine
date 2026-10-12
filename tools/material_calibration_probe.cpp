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
#include <limits>
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
constexpr Point screen_base{640.f, 500.f};

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path from_utf8(std::string_view value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.good(), "Could not read " + utf8(path));
    const auto size = input.tellg();
    require(size >= 0 && size <= 128 * 1024 * 1024, "Calibration input is too large");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(result.data()), size);
    require(input.good(), "Incomplete input read");
    return result;
}
Json parse_json(std::span<const std::uint8_t> bytes) { return Json::parse(bytes.begin(), bytes.end()); }
void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    require(!std::filesystem::exists(path), "Refusing to overwrite " + utf8(path));
    std::ofstream output(path, std::ios::binary);
    require(output.good(), "Could not write " + utf8(path));
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.flush(); output.close(); require(output.good(), "Incomplete output write");
}
void write_json(const std::filesystem::path& path, const Json& value) {
    const auto text = value.dump(2) + '\n';
    write_bytes(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
Vec3 add(Vec3 a, Vec3 b) { for (std::size_t i=0;i<3;++i) a[i]+=b[i]; return a; }
Vec3 subtract(Vec3 a, Vec3 b) { for (std::size_t i=0;i<3;++i) a[i]-=b[i]; return a; }
Vec3 times(Vec3 a, float scale) { for (auto& value:a) value*=scale; return a; }
float dot(Vec3 a, Vec3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 cross(Vec3 a,Vec3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
float length(Vec3 value) { return std::sqrt(dot(value,value)); }
Vec3 normalized(Vec3 value) {
    const float size=length(value); require(std::isfinite(size)&&size>1.e-8f,"Non-normalizable vector");
    return times(value,1.f/size);
}
float maximum_error(Vec3 a, Vec3 b) {
    float result=0.f; for(std::size_t i=0;i<3;++i) result=std::max(result,std::abs(a[i]-b[i]));
    return result;
}
float half_float(std::uint16_t bits) {
    const auto mantissa=bits&1023u, exponent=(bits>>10u)&31u;
    float value{};
    if(exponent==0) value=std::ldexp(static_cast<float>(mantissa),-24);
    else if(exponent==31) value=mantissa?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
    else value=std::ldexp(static_cast<float>(mantissa+1024u),static_cast<int>(exponent)-25);
    return bits&0x8000u ? -value : value;
}
std::array<float,4> float_pixel(std::span<const std::uint8_t> bytes, int width, int x, int y) {
    const auto offset=(static_cast<std::size_t>(y)*width+x)*8;
    require(offset+8<=bytes.size(),"Float pixel outside readback");
    std::array<float,4> result{};
    for(std::size_t i=0;i<4;++i) {
        result[i]=half_float(static_cast<std::uint16_t>(bytes[offset+i*2] | bytes[offset+i*2+1]<<8u));
        require(std::isfinite(result[i]),"Non-finite GPU calibration output");
    }
    return result;
}

struct Kit {
    AssetCatalog assets;
    Json metadata,world;
    int width{},height{};
    Point pivot{};
    float eppp{}, depth_min{},depth_max{};
    std::array<std::vector<std::uint8_t>,5> bytes;
    std::array<std::string,5> hashes;
    explicit Kit(const std::filesystem::path& root):assets(root) {
        assets.verify();
        metadata=parse_json(assets.read_path("art/material/calibration.json"));
        world=parse_json(assets.read_path("art/world.json"));
        require(metadata.at("schema")==1 && world.at("schema")==2,"Unsupported calibration schema");
        width=metadata.at("source_size").at(0).get<int>(); height=metadata.at("source_size").at(1).get<int>();
        pivot={metadata.at("pivot_px").at(0).get<float>(),metadata.at("pivot_px").at(1).get<float>()};
        eppp=metadata.at("export_pixels_per_reference_pixel").get<float>();
        depth_min=metadata.at("depth_range").at(0).get<float>(); depth_max=metadata.at("depth_range").at(1).get<float>();
        require(width>0&&height>0&&width<=2048&&height<=2048&&eppp>0&&depth_min<depth_max,
                "Invalid master geometry profile");
        require(metadata.at("probes").size()>=12,"Insufficient geometry controls");
        constexpr std::array names{"albedo","normal_material","camera_depth","emission_mask","surface_mask"};
        constexpr std::array formats{"rgba8_unorm_srgb","rgba8_unorm","r16_unorm","r8_unorm","rgba8_unorm"};
        constexpr std::array<std::size_t,5> sizes{4,4,2,1,4};
        const auto& material=world.at("materials").at(0);
        require(material.at("source_hash")==metadata.at("master_sha256"),"Master provenance disagreement");
        for(std::size_t i=0;i<bytes.size();++i) {
            const auto& channel=material.at("channels").at(names[i]);
            require(channel.at("rect")==Json::array({0,0,width,height}),"Calibration expects aligned full-page channels");
            bool found=false;
            for(const auto& texture:world.at("textures")) if(texture.at("id")==channel.at("texture")) {
                require(texture.at("width")==width&&texture.at("height")==height&&texture.at("format")==formats[i],
                        "Calibration typed channel mismatch");
                bytes[i]=assets.read_path(texture.at("path").get<std::string>());
                hashes[i]=sha256(bytes[i]);
                require(bytes[i].size()==static_cast<std::size_t>(width)*height*sizes[i]&&
                        hashes[i]==texture.at("sha256").get<std::string>(),"Calibration raw channel hash/bytes mismatch");
                found=true; break;
            }
            require(found,"Missing calibration texture");
        }
    }
};

struct Command {
    SDL_GPUCommandBuffer* value{};
    explicit Command(SDL_GPUDevice* gpu):value(SDL_AcquireGPUCommandBuffer(gpu)) { require(value,"GPU command acquisition failed"); }
    ~Command() { if(value) SDL_CancelGPUCommandBuffer(value); }
    SDL_GPUFence* submit() {
        auto* command=value; value=nullptr;
        auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
        require(fence,"GPU diagnostic submission failed"); return fence;
    }
};

// Owns only probe resources. Never installs these MRT targets into Renderer2D.
struct DiagnosticGpu {
    SDL_GPUDevice* gpu{};
    SDL_GPUGraphicsPipeline* pipeline{};
    SDL_GPUSampler* sampler{};
    SDL_GPUBuffer* vertices{};
    std::array<SDL_GPUTexture*,5> maps{};
    std::array<SDL_GPUTexture*,2> targets{};
    std::array<SDL_GPUTransferBuffer*,2> downloads{};
    std::vector<SDL_GPUTransferBuffer*> uploads;
    std::vector<SDL_GPUShader*> shaders;
    int width{},height{},pitch{};
    ~DiagnosticGpu() {
        if(!gpu) return;
        SDL_WaitForGPUIdle(gpu);
        if(pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu,pipeline);
        for(auto* shader:shaders) SDL_ReleaseGPUShader(gpu,shader);
        if(vertices) SDL_ReleaseGPUBuffer(gpu,vertices);
        for(auto* texture:maps) if(texture) SDL_ReleaseGPUTexture(gpu,texture);
        for(auto* texture:targets) if(texture) SDL_ReleaseGPUTexture(gpu,texture);
        for(auto* buffer:downloads) if(buffer) SDL_ReleaseGPUTransferBuffer(gpu,buffer);
        for(auto* buffer:uploads) SDL_ReleaseGPUTransferBuffer(gpu,buffer);
        if(sampler) SDL_ReleaseGPUSampler(gpu,sampler);
        SDL_DestroyGPUDevice(gpu);
    }
    SDL_GPUShader* shader(const std::filesystem::path& path, SDL_GPUShaderStage stage,
                          std::uint32_t samplers,std::uint32_t uniforms) {
        const auto bytes=read_bytes(path);
        SDL_GPUShaderCreateInfo info{}; info.code=bytes.data(); info.code_size=bytes.size();
        info.entrypoint="main"; info.format=SDL_GPU_SHADERFORMAT_DXIL; info.stage=stage;
        info.num_samplers=samplers; info.num_uniform_buffers=uniforms;
        auto* value=SDL_CreateGPUShader(gpu,&info); require(value,"Calibration shader creation failed");
        shaders.push_back(value); return value;
    }
    void wait(SDL_GPUFence* fence) {
        const std::array fences{fence};
        const bool complete=SDL_WaitForGPUFences(gpu,true,fences.data(),1);
        SDL_ReleaseGPUFence(gpu,fence); require(complete,"Diagnostic GPU fence wait failed");
    }
    void initialize(const Kit& kit,const std::filesystem::path& assets,
                    const std::filesystem::path& shader_root,int w,int h) {
        width=w; height=h; pitch=(width*8+255)&~255;
        uploads.reserve(maps.size()+6);
        shaders.reserve(2);
        gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12"); require(gpu,"D3D12 diagnostic device unavailable");
        require(std::string_view(SDL_GetGPUDeviceDriver(gpu))=="direct3d12","Diagnostic backend is not D3D12");
        constexpr auto float_format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        require(SDL_GPUTextureSupportsFormat(gpu,float_format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET),
                "Direct float P/N render target is unsupported");
        for(std::size_t i=0;i<2;++i) {
            SDL_GPUTextureCreateInfo info{}; info.type=SDL_GPU_TEXTURETYPE_2D; info.format=float_format;
            info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; info.width=width; info.height=height; info.layer_count_or_depth=1;
            info.num_levels=1; info.sample_count=SDL_GPU_SAMPLECOUNT_1;
            targets[i]=SDL_CreateGPUTexture(gpu,&info); require(targets[i],"Float target allocation failed");
            SDL_GPUTransferBufferCreateInfo transfer{}; transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
            transfer.size=pitch*height; downloads[i]=SDL_CreateGPUTransferBuffer(gpu,&transfer);
            require(downloads[i],"Float readback allocation failed");
        }
        auto* vertex_shader=shader(assets/"shaders/sprite.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX,0,1);
        auto* fragment_shader=shader(shader_root/"material_calibration.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT,5,2);
        const SDL_GPUVertexBufferDescription stream{0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
        const std::array attributes{
            SDL_GPUVertexAttribute{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,position)},
            SDL_GPUVertexAttribute{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,uv)},
            SDL_GPUVertexAttribute{2,0,SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,offsetof(Vertex,color)}};
        std::array<SDL_GPUColorTargetDescription,2> colors{};
        for(auto& color:colors) color.format=float_format;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader=vertex_shader; info.fragment_shader=fragment_shader;
        info.vertex_input_state={&stream,1,attributes.data(),3};
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL; info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions=colors.data(); info.target_info.num_color_targets=2;
        pipeline=SDL_CreateGPUGraphicsPipeline(gpu,&info); require(pipeline,"P/N MRT pipeline creation failed");
        SDL_GPUSamplerCreateInfo sample{}; sample.min_filter=sample.mag_filter=SDL_GPU_FILTER_LINEAR;
        sample.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sample.address_mode_u=sample.address_mode_v=sample.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler=SDL_CreateGPUSampler(gpu,&sample); require(sampler,"Calibration sampler creation failed");
        SDL_GPUBufferCreateInfo buffer{}; buffer.usage=SDL_GPU_BUFFERUSAGE_VERTEX; buffer.size=6*sizeof(Vertex);
        vertices=SDL_CreateGPUBuffer(gpu,&buffer); require(vertices,"Calibration vertex buffer allocation failed");
        constexpr std::array formats{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
                                     SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREFORMAT_R16_UNORM,
                                     SDL_GPU_TEXTUREFORMAT_R8_UNORM,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
        std::array<SDL_GPUTransferBuffer*,5> map_uploads{};
        for(std::size_t i=0;i<maps.size();++i) {
            require(SDL_GPUTextureSupportsFormat(gpu,formats[i],SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER),
                    "Required material data format is unsupported");
            SDL_GPUTextureCreateInfo texture{}; texture.type=SDL_GPU_TEXTURETYPE_2D; texture.format=formats[i];
            texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER; texture.width=kit.width; texture.height=kit.height;
            texture.layer_count_or_depth=1; texture.num_levels=1; texture.sample_count=SDL_GPU_SAMPLECOUNT_1;
            maps[i]=SDL_CreateGPUTexture(gpu,&texture); require(maps[i],"Diagnostic material texture creation failed");
            SDL_GPUTransferBufferCreateInfo transfer{}; transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transfer.size=static_cast<Uint32>(kit.bytes[i].size());
            auto* upload=SDL_CreateGPUTransferBuffer(gpu,&transfer); require(upload,"Diagnostic texture upload buffer failed");
            uploads.push_back(upload);
            map_uploads[i]=upload;
            auto* memory=SDL_MapGPUTransferBuffer(gpu,upload,false); require(memory,"Diagnostic upload map failed");
            std::memcpy(memory,kit.bytes[i].data(),kit.bytes[i].size()); SDL_UnmapGPUTransferBuffer(gpu,upload);
        }
        // Finish fallible allocation/mapping before starting a pass, so unwind can cancel a closed command buffer.
        Command command(gpu); auto* copy=SDL_BeginGPUCopyPass(command.value); require(copy,"Calibration upload pass failed");
        for(std::size_t i=0;i<maps.size();++i) {
            const SDL_GPUTextureTransferInfo source{map_uploads[i],0,static_cast<Uint32>(kit.width),static_cast<Uint32>(kit.height)};
            const SDL_GPUTextureRegion destination{maps[i],0,0,0,0,0,static_cast<Uint32>(kit.width),static_cast<Uint32>(kit.height),1};
            SDL_UploadToGPUTexture(copy,&source,&destination,false);
        }
        SDL_EndGPUCopyPass(copy); wait(command.submit());
    }
    std::array<std::vector<std::uint8_t>,2> render(const Kit& kit,Point ground,Vec3 anchor,float zoom,float scale) {
        const float factor=zoom*scale/kit.eppp;
        const Rect rect{ground.x-kit.pivot.x*factor,ground.y-kit.pivot.y*factor,
                        kit.width*factor,kit.height*factor};
        constexpr Color white{255,255,255,255};
        const std::array<Vertex,6> quad{{{{rect.x,rect.y},{0,0},white},{{rect.x+rect.w,rect.y},{1,0},white},
                                      {{rect.x+rect.w,rect.y+rect.h},{1,1},white},
                                      {{rect.x,rect.y},{0,0},white},{{rect.x+rect.w,rect.y+rect.h},{1,1},white},
                                      {{rect.x,rect.y+rect.h},{0,1},white}}};
        SDL_GPUTransferBufferCreateInfo transfer{}; transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transfer.size=sizeof(quad);
        auto* upload=SDL_CreateGPUTransferBuffer(gpu,&transfer); require(upload,"Diagnostic vertex transfer failed"); uploads.push_back(upload);
        auto* memory=SDL_MapGPUTransferBuffer(gpu,upload,false); require(memory,"Diagnostic vertex map failed");
        std::memcpy(memory,quad.data(),sizeof(quad)); SDL_UnmapGPUTransferBuffer(gpu,upload);
        Command command(gpu); auto* copy=SDL_BeginGPUCopyPass(command.value); require(copy,"Diagnostic copy pass failed");
        const SDL_GPUTransferBufferLocation source{upload,0}; const SDL_GPUBufferRegion destination{vertices,0,sizeof(quad)};
        SDL_UploadToGPUBuffer(copy,&source,&destination,true); SDL_EndGPUCopyPass(copy);
        std::array<SDL_GPUColorTargetInfo,2> colors{};
        for(std::size_t i=0;i<2;++i) {colors[i].texture=targets[i]; colors[i].load_op=SDL_GPU_LOADOP_CLEAR; colors[i].store_op=SDL_GPU_STOREOP_STORE;}
        auto* pass=SDL_BeginGPURenderPass(command.value,colors.data(),2,nullptr); require(pass,"P/N render pass failed");
        SDL_BindGPUGraphicsPipeline(pass,pipeline);
        const SDL_GPUViewport viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1}; SDL_SetGPUViewport(pass,&viewport);
        const SDL_GPUBufferBinding binding{vertices,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
        const std::array<float,4> screen{static_cast<float>(logical.width),static_cast<float>(logical.height),0,0};
        SDL_PushGPUVertexUniformData(command.value,0,screen.data(),sizeof(screen));
        SurfaceLightingFrame lighting{}; lighting.ambient={0,0,0,0};
        SDL_PushGPUFragmentUniformData(command.value,0,&lighting,sizeof(lighting));
        SpriteSurfaceGpu surface{};
        for(auto& region:surface.uv_regions) region={0,0,1,1};
        surface.anchor_export={anchor[0],anchor[1],anchor[2],kit.eppp};
        surface.source_size_pivot={static_cast<float>(kit.width),static_cast<float>(kit.height),kit.pivot.x,kit.pivot.y};
        surface.depth_scale={kit.depth_min,kit.depth_max,scale,0};
        SDL_PushGPUFragmentUniformData(command.value,1,&surface,sizeof(surface));
        std::array<SDL_GPUTextureSamplerBinding,5> textures{};
        for(std::size_t i=0;i<textures.size();++i) textures[i]={maps[i],sampler};
        SDL_BindGPUFragmentSamplers(pass,0,textures.data(),5); SDL_DrawGPUPrimitives(pass,6,1,0,0); SDL_EndGPURenderPass(pass);
        copy=SDL_BeginGPUCopyPass(command.value); require(copy,"P/N download pass failed");
        for(std::size_t i=0;i<2;++i) {
            const SDL_GPUTextureRegion image{targets[i],0,0,0,0,0,static_cast<Uint32>(width),static_cast<Uint32>(height),1};
            const SDL_GPUTextureTransferInfo target{downloads[i],0,static_cast<Uint32>(pitch/8),static_cast<Uint32>(height)};
            SDL_DownloadFromGPUTexture(copy,&image,&target);
        }
        SDL_EndGPUCopyPass(copy); wait(command.submit());
        std::array<std::vector<std::uint8_t>,2> result;
        for(std::size_t i=0;i<2;++i) {
            result[i].resize(static_cast<std::size_t>(width)*height*8);
            auto* data=static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(gpu,downloads[i],false)); require(data,"P/N download map failed");
            for(int row=0;row<height;++row) std::memcpy(result[i].data()+static_cast<std::size_t>(row)*width*8,
                                                    data+static_cast<std::size_t>(row)*pitch,width*8);
            SDL_UnmapGPUTransferBuffer(gpu,downloads[i]);
        }
        return result;
    }
};

struct Sample {
    int x{},y{};
    Point source_pixel{},logical_pixel{};
    Vec3 local{},normal{},world{};
};
Sample sample_geometry(const Kit& kit,const Json& probe,Point ground,Vec3 anchor,float zoom,float scale,int width,int height) {
    const Point source{probe.at("source_pixel").at(0).get<float>(),probe.at("source_pixel").at(1).get<float>()};
    const auto offset=screen_offset_from_source_pixel(source,kit.pivot,kit.eppp,zoom*scale);
    Sample result;
    result.x=static_cast<int>(std::floor((ground.x+offset.x)*width/logical.width));
    result.y=static_cast<int>(std::floor((ground.y+offset.y)*height/logical.height));
    require(result.x>=0&&result.x<width&&result.y>=0&&result.y<height,"Geometry control is outside drawable");
    result.logical_pixel={(result.x+.5f)*logical.width/width,(result.y+.5f)*logical.height/height};
    const Point local_screen{(result.logical_pixel.x-ground.x)/(zoom*scale),
                             (result.logical_pixel.y-ground.y)/(zoom*scale)};
    result.source_pixel=source_pixel_from_reference_offset(local_screen,kit.pivot,kit.eppp);
    const auto triangle=probe.at("triangle_vertices").get<std::array<Vec3,3>>();
    result.normal=normalized(cross(subtract(triangle[1],triangle[0]),subtract(triangle[2],triangle[0])));
    require(maximum_error(result.normal,probe.at("normal").get<Vec3>())<1.e-5f,
            "Calibration metadata normal disagrees with editable geometry winding");
    const auto point=probe.at("position").get<Vec3>();
    const auto expected_center=project_render_xyz(point);
    const auto source_center=reference_offset_from_source_pixel(source,kit.pivot,kit.eppp);
    require(std::abs(expected_center.x-source_center.x)<.001f&&std::abs(expected_center.y-source_center.y)<.001f,
            "Exporter projection disagrees with the canonical C++ geometry basis");
    require(std::abs(dot(result.normal,subtract(point,triangle[0])))<1.e-5f,
            "Calibration source point does not lie on its geometry triangle");
    const auto ray_origin=reconstruct_render_xyz(local_screen,0.f);
    constexpr Vec3 ray_direction{camera_depth_axis,camera_depth_axis,camera_depth_axis};
    const float divisor=dot(result.normal,ray_direction); require(std::abs(divisor)>.01f,"Edge-on probe plane");
    const float depth=dot(result.normal,subtract(triangle[0],ray_origin))/divisor;
    result.local=add(ray_origin,times(ray_direction,depth)); result.world=add(anchor,times(result.local,scale));
    const auto a=project_render_xyz(triangle[0]),b=project_render_xyz(triangle[1]),c=project_render_xyz(triangle[2]);
    const float denominator=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
    require(std::abs(denominator)>1.e-6f,"Degenerate control triangle projection");
    const float w0=((b.y-c.y)*(local_screen.x-c.x)+(c.x-b.x)*(local_screen.y-c.y))/denominator;
    const float w1=((c.y-a.y)*(local_screen.x-c.x)+(a.x-c.x)*(local_screen.y-c.y))/denominator;
    require(w0>0.f&&w1>0.f&&w0+w1<1.f,"Actual physical sample left its source geometry triangle");
    require(probe.at("boundary_clearance_px").get<float>()>=8.f,"Control is too close to a surface discontinuity");
    return result;
}
void save_normal_preview(std::span<const std::uint8_t> raw,int width,int height,const std::filesystem::path& path) {
    require(!std::filesystem::exists(path),"Preview would overwrite a file");
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    require(surface!=nullptr,"Could not create decoded normal preview");
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto pixel=float_pixel(raw,width,x,y);
        auto* target=static_cast<std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4;
        for(std::size_t c=0;c<3;++c) target[c]=pixel[3]>.99f ? static_cast<std::uint8_t>(std::lround(std::clamp(pixel[c]*.5f+.5f,0.f,1.f)*255.f)):0;
        target[3]=255;
    }
    require(SDL_SavePNG(surface.get(),utf8(path).c_str()),"Could not save decoded normal preview");
}
Json diagnostic_matrix(const Kit& kit,const std::filesystem::path& assets,const std::filesystem::path& shader_root,
                       const std::filesystem::path& output,int width,int height) {
    const auto shader_contract=parse_json(read_bytes(shader_root/"contract.json"));
    require(shader_contract.at("binary_sha256").get<std::string>()==sha256(read_bytes(shader_root/"material_calibration.frag.dxil")),
            "Stale diagnostic shader binary hash");
    DiagnosticGpu gpu; gpu.initialize(kit,assets,shader_root,width,height);
    Json cases=Json::array(); float max_position=0,max_angle=0,max_screen=0,max_normal_length=0;
    for(bool panned:{false,true}) for(float zoom:{.62f,1.30f,3.f}) {
        const Vec3 camera=panned?Vec3{10.25f,-6.125f,0}:Vec3{};
        const Vec3 world_anchor=panned?Vec3{10.375f,-6.5f,render_z_from_legacy_elevation(4.f)}:Vec3{};
        const auto anchor=camera_relative_render_xyz(world_anchor,camera);
        const auto anchor_screen=project_render_xyz(anchor);
        const Point ground{screen_base.x+anchor_screen.x*zoom,screen_base.y+anchor_screen.y*zoom};
        const float scale=panned?1.15f:1.f;
        const auto name=std::string(panned?"panned-elevated-":"origin-")+std::to_string(static_cast<int>(std::lround(zoom*100)));
        const auto raw=gpu.render(kit,ground,anchor,zoom,scale);
        write_bytes(output/(name+"-position-rgba16f.bin"),raw[0]);
        write_bytes(output/(name+"-normal-rgba16f.bin"),raw[1]);
        save_normal_preview(raw[1],width,height,output/(name+"-decoded-normal-preview.png"));
        Json samples=Json::array();
        for(const auto& probe:kit.metadata.at("probes")) {
            const auto sample=sample_geometry(kit,probe,ground,anchor,zoom,scale,width,height);
            const auto p=float_pixel(raw[0],width,sample.x,sample.y),n=float_pixel(raw[1],width,sample.x,sample.y);
            require(p[3]>.99f&&n[3]>.99f,"Geometry control is not fully covered");
            const Vec3 position{p[0],p[1],p[2]},normal{n[0],n[1],n[2]};
            const float error=maximum_error(position,sample.world);
            const float angle=std::acos(std::clamp(dot(normalized(normal),sample.normal),-1.f,1.f))*180.f/3.14159265358979323846f;
            const float normal_length_error=std::abs(length(normal)-1.f);
            const auto expected_screen=project_render_xyz(sample.world),actual_screen=project_render_xyz(position);
            const float screen_error=std::max(std::abs(actual_screen.x-expected_screen.x)*zoom*width/logical.width,
                                               std::abs(actual_screen.y-expected_screen.y)*zoom*height/logical.height);
            const auto id=probe.at("id").get<std::string>();
            require(error<=.02f,"P error exceeds .02 tile at "+name+"/"+id+" value="+std::to_string(error));
            require(angle<=1.5f&&normal_length_error<=.001f,"N tolerance exceeded at "+name+"/"+id);
            require(screen_error<=1.f,"GPU P reprojection exceeds 1 native pixel at "+name+"/"+id);
            max_position=std::max(max_position,error); max_angle=std::max(max_angle,angle);
            max_screen=std::max(max_screen,screen_error); max_normal_length=std::max(max_normal_length,normal_length_error);
            samples.push_back({{"id",id},{"physical_pixel",{sample.x,sample.y}},
                               {"actual_source_pixel",{sample.source_pixel.x,sample.source_pixel.y}},
                               {"expected_camera_relative_position",sample.world},{"geometry_normal",sample.normal},
                               {"gpu_camera_relative_position",p},{"gpu_normal",n},{"position_error",error},
                               {"normal_angle_degrees",angle},{"normal_length_error",normal_length_error},
                               {"native_reprojection_error_px",screen_error}});
        }
        cases.push_back({{"id",name},{"zoom",zoom},{"geometry_scale",scale},{"camera_xyz",camera},
                         {"world_anchor",world_anchor},{"camera_relative_anchor",anchor},
                         {"screen_ground",{ground.x,ground.y}},{"samples",samples}});
    }
    const auto props=SDL_GetGPUDeviceProperties(gpu.gpu);
    return {{"scope","separate probe MRT/resources, unchanged production material functions; not production frame graph"},
            {"backend",SDL_GetGPUDeviceDriver(gpu.gpu)},
            {"device",SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,"")},
            {"format","R16G16B16A16_FLOAT; tightly packed little-endian readback"},
            {"size",{width,height}},{"shader_contract",shader_contract},
            {"vertex_shader_sha256",sha256(read_bytes(assets/"shaders/sprite.vert.dxil"))},
            {"position_tolerance_tile",.02f},{"normal_tolerance_degrees",1.5f},{"normal_length_tolerance",.001f},
            {"reprojection_tolerance_px",1.f},{"max_position_error",max_position},
            {"max_normal_angle_degrees",max_angle},{"max_normal_length_error",max_normal_length},
            {"max_native_reprojection_error_px",max_screen},{"cases",cases}};
}

float decode_srgb(int encoded) {
    const float value=encoded/255.f; return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
}
int encode_srgb(float linear) {
    linear=std::clamp(linear,0.f,1.f);
    const float value=linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1.f/2.4f)-.055f;
    return static_cast<int>(std::lround(std::clamp(value,0.f,1.f)*255.f));
}
Vec3 quantized_normal(const Kit& kit,Point source) {
    const int x=std::clamp(static_cast<int>(source.x),0,kit.width-1),y=std::clamp(static_cast<int>(source.y),0,kit.height-1);
    const auto offset=(static_cast<std::size_t>(y)*kit.width+x)*4;
    const float u=kit.bytes[1][offset]/255.f*2.f-1.f,v=kit.bytes[1][offset+1]/255.f*2.f-1.f;
    Vec3 result{u,v,1.f-std::abs(u)-std::abs(v)};
    if(result[2]<0) { result[0]=(1.f-std::abs(v))*(u>=0?1.f:-1.f); result[1]=(1.f-std::abs(u))*(v>=0?1.f:-1.f); }
    return normalized(result);
}
std::array<int,4> lighting_expected(const Kit& kit,const Json& probe,const Sample& sample,
                                    const SurfaceLightingFrame& lighting,bool emission_on) {
    const auto bytes=probe.at("albedo").get<std::array<int,4>>();
    const auto normal=quantized_normal(kit,sample.source_pixel);
    Vec3 diffuse{lighting.ambient[0]*std::lround(probe.at("ao").get<float>()*255.f)/255.f,
                 lighting.ambient[1]*std::lround(probe.at("ao").get<float>()*255.f)/255.f,
                 lighting.ambient[2]*std::lround(probe.at("ao").get<float>()*255.f)/255.f};
    const auto sun=normalized({lighting.sun_direction[0],lighting.sun_direction[1],lighting.sun_direction[2]});
    for(std::size_t c=0;c<3;++c) diffuse[c]+=lighting.sun_color_intensity[c]*lighting.sun_color_intensity[3]*std::max(dot(normal,sun),0.f);
    for(std::size_t i=0;i<lighting.counts[0];++i) {
        const auto& light=lighting.lights[i];
        const auto to_source=subtract({light.anchor_radius[0],light.anchor_radius[1],light.anchor_radius[2]},sample.world);
        const float radial=std::clamp(1.f-dot(to_source,to_source)/(light.anchor_radius[3]*light.anchor_radius[3]),0.f,1.f);
        const float attenuation=std::pow(radial,light.height_falloff_reveal[1])*light.height_falloff_reveal[2];
        const float facing=std::max(dot(normal,normalized(to_source)),0.f);
        for(std::size_t c=0;c<3;++c) diffuse[c]+=light.color_intensity[c]*light.color_intensity[3]*attenuation*facing;
    }
    const auto emission=kit.metadata.at("emission_radiance").get<std::array<float,4>>();
    std::array<int,4> result{0,0,0,255};
    for(std::size_t c=0;c<3;++c) result[c]=encode_srgb(decode_srgb(bytes[c])*diffuse[c]+
        (emission_on?emission[c]*emission[3]*probe.at("emission_mask").get<float>():0.f));
    return result;
}
Json production_lighting_matrix(const Kit& kit,SDL_Window* window,const std::filesystem::path& assets,
                                const std::filesystem::path& output,int width,int height) {
    RendererOptions options; options.backend=Backend::Direct3D12;
    Renderer2D renderer(window,assets,options); GameArt art(renderer); art.load(kit.assets);
    require(renderer.device_info().backend=="direct3d12"&&!renderer.device_info().software,"Production calibration requires native D3D12");
    require(art.contains_lit(kit.metadata.at("sprite_id_off").get<std::string>()),"Real geometry kit did not install");
    Json cases=Json::array();
    for(float zoom:{.62f,1.3f,3.f}) for(const auto* state:{"sun","point-x-positive","point-x-negative","emission-off","emission-on"}) {
        const bool emission_on=std::string_view(state)=="emission-on";
        Frame2D frame; frame.logical_size=logical; frame.clear={0,0,0,255}; frame.linear_hdr_world=true;
        frame.surface_lighting.ambient={0,0,0,0}; frame.surface_lighting.sun_color_intensity={0,0,0,0};
        auto& lighting=frame.surface_lighting;
        if(std::string_view(state)=="sun") {
            lighting.ambient={.03f,.03f,.03f,0}; const auto direction=normalized({1.f,.2f,1.f});
            lighting.sun_direction={direction[0],direction[1],direction[2],0}; lighting.sun_color_intensity={1.f,1.f,1.f,1.f};
        }
        if(std::string_view(state).starts_with("point")) {
            lighting.counts[0]=1; auto& lamp=lighting.lights[0];
            lamp.anchor_radius={std::string_view(state)=="point-x-positive"?6.f:-4.f,1.f,4.f,12.f};
            lamp.color_intensity={1.f,.7f,.3f,1.f};
        }
        art.lit_sprite(frame,kit.metadata.at(emission_on?"sprite_id_on":"sprite_id_off").get<std::string>(),screen_base,{0,0,0},zoom);
        // Isolate diffuse/emission oracle; wet specular belongs to a later control.
        frame.sprite_material_draws.front().surface.response[1]=0.f;
        const auto name=std::string("production-")+state+"-"+std::to_string(static_cast<int>(std::lround(zoom*100)));
        const auto path=output/(name+".png"); require(!std::filesystem::exists(path),"Production capture would overwrite");
        renderer.render(frame,path);
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> capture(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface);
        require(capture!=nullptr&&capture->w==width&&capture->h==height,"Production capture dimensions changed");
        Json samples=Json::array();
        for(const auto& probe:kit.metadata.at("probes")) {
            const auto sample=sample_geometry(kit,probe,screen_base,{0,0,0},zoom,1.f,width,height);
            Uint8 r{},g{},b{},a{}; require(SDL_ReadSurfacePixel(capture.get(),sample.x,sample.y,&r,&g,&b,&a),"Capture pixel is unavailable");
            const std::array<int,4> actual{r,g,b,a};
            const auto expected=lighting_expected(kit,probe,sample,lighting,emission_on);
            for(std::size_t c=0;c<4;++c) require(std::abs(actual[c]-expected[c])<=4,
                "Production material color mismatch at "+name+"/"+probe.at("id").get<std::string>()+
                " channel="+std::to_string(c)+" actual="+std::to_string(actual[c])+" expected="+std::to_string(expected[c]));
            samples.push_back({{"id",probe.at("id")},{"physical_pixel",{sample.x,sample.y}},{"actual_rgba",actual},{"expected_rgba",expected}});
        }
        cases.push_back({{"id",name},{"zoom",zoom},{"exposure",frame.world_exposure},{"specular_strength",0},
                         {"ambient",lighting.ambient},{"sun_direction",lighting.sun_direction},
                         {"sun_color_intensity",lighting.sun_color_intensity},{"point_anchor_radius",lighting.lights[0].anchor_radius},
                         {"point_color_intensity",lighting.lights[0].color_intensity},{"active_points",lighting.counts[0]},
                         {"emission_on",emission_on},{"samples",samples}});
    }
    return {{"backend",renderer.device_info().backend},{"device",renderer.device_info().device},
            {"driver",renderer.device_info().driver},{"captures",15},{"rgba_tolerance",4},
            {"scope","actual GameArt/Renderer2D diffuse and emission, not Dynamic GameView or GPU P proof"},
            {"albedo_sha256_off",kit.hashes[0]},{"albedo_sha256_on",kit.hashes[0]},
            {"cases",cases}};
}
} // namespace

int main(int argc,char** argv) {
    try {
        require(argc==4,"Usage: MaterialCalibrationProbe <kit-root> <NEW-output-root> <diagnostic-shader-dir>");
        const auto kit_root=from_utf8(argv[1]),output=from_utf8(argv[2]),shader_root=from_utf8(argv[3]);
        require(!std::filesystem::exists(output),"Output already exists; previous evidence is preserved");
        Kit kit(kit_root);
        require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization failed");
        struct Quit {~Quit(){SDL_Quit();}} quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra geometry material calibration",logical.width,logical.height,SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);
        require(window!=nullptr,"Calibration window unavailable"); SDL_PumpEvents();
        int width{},height{}; require(SDL_GetWindowSizeInPixels(window.get(),&width,&height),"Drawable dimensions unavailable");
        require(width>0&&height>0&&width*static_cast<int>(logical.height)==height*static_cast<int>(logical.width),
                "Calibration requires a 16:9 drawable for exact logical/physical oracle mapping");
        require(std::filesystem::create_directories(output),"Output was concurrently created; refusing to reuse it");
        const auto* base_path=SDL_GetBasePath(); require(base_path!=nullptr,"Runtime base path unavailable");
        const auto assets=from_utf8(base_path)/"assets";
        const auto diagnostic=diagnostic_matrix(kit,assets,shader_root,output,width,height);
        const auto production=production_lighting_matrix(kit,window.get(),assets,output,width,height);
        require(diagnostic.at("device")==production.at("device"),"Direct diagnostic and production selected different adapters");
        write_json(output/"material-calibration-report.json",{{"schema",1},{"build",RIBTERRA_PROBE_BUILD},
            {"probe_executable_sha256",sha256(read_bytes(from_utf8(base_path)/"MaterialCalibrationProbe.exe"))},
            {"production_shader_manifest_sha256",sha256(read_bytes(assets/"shaders/manifest.json"))},
            {"complete",true},{"calibration_metadata",kit.metadata},{"asset_content_sha256",kit.assets.content_hash()},
            {"direct_float_readback",diagnostic},{"production_material",production},
            {"not_claimed",{"concept-ready art","complete neutral kit","Dynamic GameView","shadows","occlusion","atmosphere","performance","E01-E17 acceptance","desktop scan-out"}}});
        std::cout<<"Direct P/N geometry controls and production lighting passed; evidence retained in "<<utf8(output)<<'\n';
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
