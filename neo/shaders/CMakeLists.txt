include(../compileshaders.cmake)

file(GLOB globalshaders "BRDF.inc.hlsl" "global_inc.hlsl" "vulkan.hlsli" "renderParmSet0.inc.hlsl" "renderParmSet1.inc.hlsl" "renderParmSet2.inc.hlsl" "renderParmSet3.inc.hlsl" "renderParmSet4.inc.hlsl" "renderParmSet5.inc.hlsl" "renderParmSet6.inc.hlsl" "renderParmSet7.inc.hlsl" "renderParmSet8.inc.hlsl" "renderParmSet9.inc.hlsl" "renderParmSet10.inc.hlsl" "renderParmSet11.inc.hlsl" "renderParmSet12.inc.hlsl" "renderParmSet13.inc.hlsl" "renderParmSet14.inc.hlsl")
file(GLOB builtins "builtin/*.hlsl")
file(GLOB debug "builtin/debug/*hlsl")
file(GLOB fog "builtin/fog/*hlsl")
file(GLOB legacy "builtin/legacy/*hlsl")
file(GLOB lighting "builtin/lighting/*hlsl")
file(GLOB post "builtin/post/*hlsl")
file(GLOB pathtracing "builtin/pathtracing/*hlsl")
file(GLOB ssao "builtin/SSAO/*hlsl")
file(GLOB ssgi "builtin/SSGI/*hlsl")
file(GLOB video "builtin/video/*hlsl")
file(GLOB vr "builtin/VR/*hlsl")
file(GLOB usershaders "*.hlsl")

list(REMOVE_ITEM usershaders ${globalshaders})

set(shaders ${globalshaders} ${builtins} ${debug} ${fog} ${legacy} ${lighting} ${post} ${pathtracing} ${ssao} ${ssgi} ${video} ${vr} ${usershaders})

source_group("builtin" FILES ${builtins})
source_group("builtin/debug" FILES ${debug})
source_group("builtin/fog" FILES ${fog})
source_group("builtin/legacy" FILES ${legacy})
source_group("builtin/lighting" FILES ${lighting})
source_group("builtin/post" FILES ${post})
source_group("builtin/pathtracing" FILES ${pathtracing})
source_group("builtin/SSAO" FILES ${ssao})
source_group("builtin/SSGI" FILES ${ssgi})
source_group("builtin/video" FILES ${video})
source_group("builtin/VR" FILES ${vr})
source_group("global" FILES ${globalshaders})

source_group("user" FILES ${usershaders})

if(STANDALONE)
    compile_shaders_all_platforms(
        TARGET Shaders
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg
        OUTPUT_BASE ${CMAKE_CURRENT_SOURCE_DIR}/../../content/renderprogs2
        SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES ${shaders}
    )
else()
    compile_shaders_all_platforms(
        TARGET Shaders
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg
        OUTPUT_BASE ${CMAKE_CURRENT_SOURCE_DIR}/../../base/renderprogs2 # This path scheme is a bit odd.
        SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES ${shaders}
    )
endif()

set(PATH_TRACING_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/builtin/pathtracing/pathtrace_smoke.rt.hlsl)
set(PATH_TRACING_SHADER_OUTPUT_BASE ${CMAKE_CURRENT_SOURCE_DIR}/../../base/renderprogs2)
set(PATH_TRACING_SHADER_OUTPUTS)

if(USE_DX12 AND USE_DXIL_ON_DX12)
    set(PATH_TRACING_DXIL_OUTPUT ${PATH_TRACING_SHADER_OUTPUT_BASE}/dxil/builtin/pathtracing/pathtrace_smoke.rt.bin)
    add_custom_command(
        OUTPUT ${PATH_TRACING_DXIL_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PATH_TRACING_SHADER_OUTPUT_BASE}/dxil/builtin/pathtracing
        COMMAND ${DXC_PATH} -T lib_6_6 -Fo ${PATH_TRACING_DXIL_OUTPUT} ${PATH_TRACING_SHADER_SOURCE}
        DEPENDS ${PATH_TRACING_SHADER_SOURCE}
        COMMENT "Compiling path tracing smoke RT shader library (DXIL)"
        VERBATIM)
    list(APPEND PATH_TRACING_SHADER_OUTPUTS ${PATH_TRACING_DXIL_OUTPUT})
endif()

if(USE_VULKAN)
    set(PATH_TRACING_SPIRV_OUTPUT ${PATH_TRACING_SHADER_OUTPUT_BASE}/spirv/builtin/pathtracing/pathtrace_smoke.rt.bin)
    add_custom_command(
        OUTPUT ${PATH_TRACING_SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PATH_TRACING_SHADER_OUTPUT_BASE}/spirv/builtin/pathtracing
        COMMAND ${DXC_SPIRV_PATH} -T lib_6_6 -spirv -fspv-target-env=vulkan1.2 -Fo ${PATH_TRACING_SPIRV_OUTPUT} ${PATH_TRACING_SHADER_SOURCE}
        DEPENDS ${PATH_TRACING_SHADER_SOURCE}
        COMMENT "Compiling path tracing smoke RT shader library (SPIR-V)"
        VERBATIM)
    list(APPEND PATH_TRACING_SHADER_OUTPUTS ${PATH_TRACING_SPIRV_OUTPUT})
endif()

if(PATH_TRACING_SHADER_OUTPUTS)
    add_custom_target(PathTracingShaders DEPENDS ${PATH_TRACING_SHADER_OUTPUTS})
    add_dependencies(Shaders PathTracingShaders)
endif()
