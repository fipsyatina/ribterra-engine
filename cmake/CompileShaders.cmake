function(ribterra_compile_shaders output_dir)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  find_program(RIBTERRA_SHADERCROSS shadercross
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/sdl3-shadercross"
          "${VCPKG_INSTALLED_DIR}/x64-windows/tools/sdl3-shadercross"
    NO_DEFAULT_PATH REQUIRED)
  find_program(RIBTERRA_DXC dxc
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/directx-dxc"
          "${VCPKG_INSTALLED_DIR}/x64-windows/tools/directx-dxc"
    NO_DEFAULT_PATH REQUIRED)
  set(shader_outputs)
  # Track the entire bounded source tree: shader_contract follows recursive
  # includes, so a newly nested include must also invalidate compiled assets.
  # CONFIGURE_DEPENDS notices new leaf files without requiring manual lists.
  file(GLOB_RECURSE shader_source_dependencies CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shaders/*")
  foreach(program sprite surface_lit surface_hdr world_linear hdr_composite material_sprite
                  ground_gbuffer ground_lighting material_forward ground_contact
                  ground_contact_gbuffer ground_contact_lighting ground_sky_lighting material_sky_forward
                  ground_shadow_lighting material_shadow_forward sky_shadow_depth
                  water_ground_gbuffer water_ground_lighting water_reflection
                  atmosphere_ground_lighting atmosphere_material_forward atmosphere_water_reflection)
    foreach(stage vert frag)
      if(stage STREQUAL "vert")
        set(stage_name vertex)
      else()
        set(stage_name fragment)
      endif()
      set(source "${CMAKE_SOURCE_DIR}/shaders/${program}.${stage}.hlsl")
      set(binary "${output_dir}/${program}.${stage}.dxil")
      set(reflection "${output_dir}/${program}.${stage}.json")
      set(shared_sources)
      if(program STREQUAL "surface_hdr")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/surface_lit.${stage}.hlsl")
      elseif((program STREQUAL "world_linear" OR program STREQUAL "material_sprite" OR
              program STREQUAL "material_forward") AND stage STREQUAL "vert")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/sprite.vert.hlsl")
      elseif(program STREQUAL "ground_gbuffer" AND stage STREQUAL "vert")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/surface_lit.vert.hlsl")
      elseif(program STREQUAL "ground_lighting" AND stage STREQUAL "vert")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/hdr_composite.vert.hlsl")
      elseif(program STREQUAL "ground_contact" AND stage STREQUAL "vert")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/hdr_composite.vert.hlsl")
      elseif(program STREQUAL "ground_contact_gbuffer")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/ground_gbuffer.${stage}.hlsl")
      elseif(program STREQUAL "ground_contact_lighting")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/ground_lighting.${stage}.hlsl")
      elseif(program STREQUAL "water_ground_gbuffer")
        if(stage STREQUAL "vert")
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/ground_contact_gbuffer.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/ground_gbuffer.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/surface_lit.vert.hlsl")
        else()
          list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/ground_gbuffer.frag.hlsl")
        endif()
      elseif(program STREQUAL "water_ground_lighting")
        if(stage STREQUAL "vert")
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/ground_lighting.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/hdr_composite.vert.hlsl")
        else()
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/sky_shadow.hlsli"
            "${CMAKE_SOURCE_DIR}/shaders/ground_lighting.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/local_occlusion.hlsli")
        endif()
      elseif(program STREQUAL "water_reflection" AND stage STREQUAL "vert")
        list(APPEND shared_sources
          "${CMAKE_SOURCE_DIR}/shaders/material_shadow_forward.vert.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/material_sky_forward.vert.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/material_forward.vert.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/sprite.vert.hlsl")
      elseif(program STREQUAL "water_reflection" AND stage STREQUAL "frag")
        list(APPEND shared_sources
          "${CMAKE_SOURCE_DIR}/shaders/sky_shadow.hlsli"
          "${CMAKE_SOURCE_DIR}/shaders/local_occlusion.hlsli"
          "${CMAKE_SOURCE_DIR}/shaders/material_sprite.frag.hlsl")
      elseif(program STREQUAL "atmosphere_ground_lighting")
        if(stage STREQUAL "vert")
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/water_ground_lighting.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/ground_lighting.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/hdr_composite.vert.hlsl")
        else()
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/water_ground_lighting.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/sky_shadow.hlsli"
            "${CMAKE_SOURCE_DIR}/shaders/ground_lighting.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/local_occlusion.hlsli")
        endif()
      elseif(program STREQUAL "atmosphere_material_forward")
        if(stage STREQUAL "vert")
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/material_shadow_forward.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/material_sky_forward.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/material_forward.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/sprite.vert.hlsl")
        else()
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/material_shadow_forward.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/sky_shadow.hlsli"
            "${CMAKE_SOURCE_DIR}/shaders/material_sky_forward.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/material_forward.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/local_occlusion.hlsli"
            "${CMAKE_SOURCE_DIR}/shaders/material_sprite.frag.hlsl")
        endif()
      elseif(program STREQUAL "atmosphere_water_reflection")
        if(stage STREQUAL "vert")
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/water_reflection.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/material_shadow_forward.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/material_sky_forward.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/material_forward.vert.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/sprite.vert.hlsl")
        else()
          list(APPEND shared_sources
            "${CMAKE_SOURCE_DIR}/shaders/water_reflection.frag.hlsl"
            "${CMAKE_SOURCE_DIR}/shaders/sky_shadow.hlsli"
            "${CMAKE_SOURCE_DIR}/shaders/local_occlusion.hlsli"
            "${CMAKE_SOURCE_DIR}/shaders/material_sprite.frag.hlsl")
        endif()
      elseif(program STREQUAL "material_forward" AND stage STREQUAL "frag")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/material_sprite.frag.hlsl")
      endif()
      if(stage STREQUAL "frag" AND program MATCHES
         "^(material_sprite|ground_lighting|material_forward|ground_contact_lighting|ground_sky_lighting|material_sky_forward|ground_shadow_lighting|material_shadow_forward|water_ground_lighting|water_reflection|atmosphere_ground_lighting|atmosphere_material_forward|atmosphere_water_reflection)$")
        # source_dependencies intentionally binds guarded includes too: a
        # shared-kernel edit must invalidate both legacy and opt-in variants.
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/atmosphere.hlsli")
      endif()
      if((program STREQUAL "world_linear" OR program STREQUAL "hdr_composite") AND stage STREQUAL "frag")
        list(APPEND shared_sources "${CMAKE_SOURCE_DIR}/shaders/color_transfer.hlsli")
      endif()
      add_custom_command(OUTPUT "${binary}" "${reflection}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
        COMMAND "${RIBTERRA_SHADERCROSS}" "${source}" -s HLSL -d DXIL -t "${stage_name}" -e main -I "${CMAKE_SOURCE_DIR}/shaders" -o "${binary}"
        COMMAND "${RIBTERRA_SHADERCROSS}" "${source}" -s HLSL -d JSON -t "${stage_name}" -e main -I "${CMAKE_SOURCE_DIR}/shaders" -o "${reflection}"
        DEPENDS "${source}" ${shared_sources} ${shader_source_dependencies}
          "${RIBTERRA_SHADERCROSS}" VERBATIM)
      list(APPEND shader_outputs "${binary}" "${reflection}")
    endforeach()
  endforeach()
  set(manifest "${output_dir}/manifest.json")
  add_custom_command(OUTPUT "${manifest}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/shader_contract.py"
      "${CMAKE_SOURCE_DIR}/shaders" "${output_dir}" --dxc "${RIBTERRA_DXC}" --shadercross "${RIBTERRA_SHADERCROSS}" --write-manifest
    DEPENDS ${shader_outputs} ${shader_source_dependencies}
      "${CMAKE_SOURCE_DIR}/tests/shader_contract.py" "${CMAKE_SOURCE_DIR}/vcpkg.json" "${RIBTERRA_DXC}" VERBATIM)
  add_custom_target(RibterraShaders DEPENDS "${manifest}")
  if(BUILD_TESTING)
    add_test(NAME shader_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/shader_contract.py"
      "${CMAKE_SOURCE_DIR}/shaders" "${output_dir}" --dxc "${RIBTERRA_DXC}" --shadercross "${RIBTERRA_SHADERCROSS}")
    add_test(NAME shader_negative COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/shader_negative.py"
      "${CMAKE_SOURCE_DIR}/shaders" "${output_dir}" "${RIBTERRA_DXC}" "${RIBTERRA_SHADERCROSS}")
  endif()
endfunction()
