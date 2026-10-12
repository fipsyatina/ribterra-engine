function(ribterra_compile_calibration_shader output_dir)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  find_program(RIBTERRA_CALIBRATION_SHADERCROSS shadercross
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/sdl3-shadercross"
          "${VCPKG_INSTALLED_DIR}/x64-windows/tools/sdl3-shadercross"
    NO_DEFAULT_PATH REQUIRED)
  find_program(RIBTERRA_CALIBRATION_DXC dxc
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/directx-dxc"
          "${VCPKG_INSTALLED_DIR}/x64-windows/tools/directx-dxc"
    NO_DEFAULT_PATH REQUIRED)
  set(source "${CMAKE_SOURCE_DIR}/shaders/material_calibration.frag.hlsl")
  set(binary "${output_dir}/material_calibration.frag.dxil")
  set(reflection "${output_dir}/material_calibration.frag.json")
  set(contract "${output_dir}/contract.json")
  add_custom_command(OUTPUT "${binary}" "${reflection}" "${contract}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
    COMMAND "${RIBTERRA_CALIBRATION_SHADERCROSS}" "${source}" -s HLSL -d DXIL -t fragment -e main -I "${CMAKE_SOURCE_DIR}/shaders" -o "${binary}"
    COMMAND "${RIBTERRA_CALIBRATION_SHADERCROSS}" "${source}" -s HLSL -d JSON -t fragment -e main -I "${CMAKE_SOURCE_DIR}/shaders" -o "${reflection}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/material_calibration_shader_contract.py"
      "${CMAKE_SOURCE_DIR}/shaders" "${output_dir}" "${RIBTERRA_CALIBRATION_DXC}" --write-contract
    DEPENDS "${source}" "${CMAKE_SOURCE_DIR}/shaders/material_sprite.frag.hlsl"
      "${CMAKE_SOURCE_DIR}/tests/material_calibration_shader_contract.py"
      "${RIBTERRA_CALIBRATION_SHADERCROSS}" "${RIBTERRA_CALIBRATION_DXC}" VERBATIM)
  add_custom_target(MaterialCalibrationShader DEPENDS "${binary}" "${reflection}" "${contract}")
  if(BUILD_TESTING)
    add_test(NAME material_calibration_shader_contract
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/material_calibration_shader_contract.py"
        "${CMAKE_SOURCE_DIR}/shaders" "${output_dir}" "${RIBTERRA_CALIBRATION_DXC}")
  endif()
endfunction()
