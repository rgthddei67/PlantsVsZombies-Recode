# CMake 是本项目唯一构建系统。
# 默认构建见 CMakePresets.json：cmake --preset clang-release && cmake --build --preset clang-release
cmake_minimum_required(VERSION 3.29)

project(PlantsVsZombies LANGUAGES CXX)

# Android 由 NDK 构建共享库；Windows 预设与发布参数继续走原有路径。
if(ANDROID)
    include(cmake/android.cmake)
    return()
endif()

option(PVZ_ENABLE_LTO "为发布构建启用 Clang LTO" OFF)
option(PVZ_ENABLE_AVX2 "允许游戏目标生成 AVX/AVX2 指令" ON)
option(PVZ_ENABLE_PDB "为优化构建生成函数栈与源码行可读的精简 PDB" OFF)
option(PVZ_ENABLE_ASAN "启用仅供开发诊断的 Clang AddressSanitizer" OFF)
set(PVZ_SHARED_RUNTIME_ROOT "" CACHE PATH
    "共享 resources/font 的权威运行目录；留空表示当前构建持有实体目录")

if(PVZ_ENABLE_ASAN)
    if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "PVZ_ENABLE_ASAN 当前只支持 Windows clang-cl")
    endif()
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER}
                /clang:-print-file-name=clang_rt.asan_dynamic-x86_64.dll
        RESULT_VARIABLE _pvz_asan_runtime_result
        OUTPUT_VARIABLE PVZ_ASAN_RUNTIME_DLL
        ERROR_VARIABLE _pvz_asan_runtime_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _pvz_asan_runtime_result EQUAL 0 OR NOT EXISTS "${PVZ_ASAN_RUNTIME_DLL}")
        message(FATAL_ERROR
            "无法定位 clang-cl ASan runtime: ${PVZ_ASAN_RUNTIME_DLL}\n${_pvz_asan_runtime_error}")
    endif()
    get_filename_component(_pvz_asan_runtime_dir "${PVZ_ASAN_RUNTIME_DLL}" DIRECTORY)
    set(PVZ_ASAN_RUNTIME_LIB
        "${_pvz_asan_runtime_dir}/clang_rt.asan_dynamic-x86_64.lib")
    set(PVZ_ASAN_RUNTIME_THUNK
        "${_pvz_asan_runtime_dir}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
    if(NOT EXISTS "${PVZ_ASAN_RUNTIME_LIB}" OR NOT EXISTS "${PVZ_ASAN_RUNTIME_THUNK}")
        message(FATAL_ERROR
            "clang-cl ASan import library 不完整: ${_pvz_asan_runtime_dir}")
    endif()
endif()

# ASan 使用独立诊断预设：保留适量优化以接近正式生命周期，同时关闭内联折叠并保留帧指针。
# Runtime DLL 自动复制到游戏 EXE 旁边，避免依赖开发者命令行的 PATH。
function(pvz_enable_address_sanitizer target_name)
    if(NOT PVZ_ENABLE_ASAN)
        return()
    endif()
    # vcpkg 静态依赖按普通 Release ABI 构建；关闭 MSVC STL 额外容器注解以消除
    # annotate_* 的 /FAILIFMISMATCH，常规堆栈/堆 UAF 与越界插桩仍保留。
    target_compile_definitions(${target_name} PRIVATE
        PVZ_ADDRESS_SANITIZER=1
        _DISABLE_STL_ANNOTATION=1
    )
    target_compile_options(${target_name} PRIVATE /fsanitize=address /O1 /Oy-)
    # Ninja 直接调用 lld-link，不能传只由 clang-cl 驱动识别的 /fsanitize=address；
    # 显式链接动态 runtime，并整体保留初始化 thunk，等价于 clang-cl 的链接注入。
    target_link_libraries(${target_name} PRIVATE "${PVZ_ASAN_RUNTIME_LIB}")
    target_link_options(${target_name} PRIVATE
        "/WHOLEARCHIVE:${PVZ_ASAN_RUNTIME_THUNK}"
        /INCREMENTAL:NO
        /OPT:NOICF
    )
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PVZ_ASAN_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${target_name}>"
        COMMENT "Copying clang AddressSanitizer runtime for ${target_name}"
        VERBATIM
    )
endfunction()

# 非发布预设通过 NTFS 目录联接复用发布版的 resources/font。联接只在首次配置时创建，
# 不复制资源；Shader、存档与 AutoTest 输出仍保留在各自 build/<preset>/ 中。
if(PVZ_SHARED_RUNTIME_ROOT)
    execute_process(
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ensure_runtime_asset_links.ps1
                -CanonicalRoot ${PVZ_SHARED_RUNTIME_ROOT}
                -BuildRoot ${CMAKE_BINARY_DIR}
        RESULT_VARIABLE _asset_link_result
        OUTPUT_VARIABLE _asset_link_output
        ERROR_VARIABLE _asset_link_error
    )
    if(NOT _asset_link_result EQUAL 0)
        message(FATAL_ERROR
            "共享运行资源联接失败:\n${_asset_link_output}${_asset_link_error}")
    endif()
    string(STRIP "${_asset_link_output}" _asset_link_output)
    if(_asset_link_output)
        message(STATUS "${_asset_link_output}")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 与 vcxproj 一致：静态运行时 /MT（Release）、/MTd（Debug），匹配 x64-windows-static triplet
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- 依赖（vcpkg manifest + 系统 Vulkan SDK）----
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(libopenmpt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
set(VOLK_PULL_IN_VULKAN OFF)
find_package(volk CONFIG REQUIRED)

# 当前 MSVC STL 会直接引用 Windows 8+ API。YY-Thunks 把这些入口改为运行时探测，
# 在 Windows 7 上走等价旧 API，同时在新系统继续调用原生实现。
if(WIN32)
    find_package(yy-thunks CONFIG REQUIRED)

    add_library(pvz_win7_compat INTERFACE)
    if(CMAKE_LINKER_TYPE STREQUAL "LLD")
        # LLD 需要 YY-Thunks 的整套替代 import libraries，并要求其先于 WinSDK 搜索。
        target_link_directories(pvz_win7_compat BEFORE INTERFACE
            "${YY_THUNKS_WIN7_X64_LIBRARY_DIR}"
        )
    else()
        # MSVC link.exe 直接链接官方聚合对象；用 LLD 链接该对象会产生重复符号。
        target_link_libraries(pvz_win7_compat INTERFACE
            "${YY_THUNKS_WIN7_X64_OBJECT}"
        )
    endif()
    target_link_options(pvz_win7_compat INTERFACE /SUBSYSTEM:CONSOLE,6.01)

    get_filename_component(_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(PVZ_LLVM_READOBJ
        NAMES llvm-readobj.exe llvm-readobj
        HINTS
            "${_compiler_bin_dir}"
            "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/x64/bin"
        REQUIRED
    )

    # 每个 Windows 可执行目标链接后都与 Win7 x64 官方导出表核对，防止依赖更新
    # 再次引入只能在 Windows 8+ 解析的静态入口。
    function(pvz_assert_win7_imports target_name)
        if(PVZ_ENABLE_ASAN AND "${target_name}" STREQUAL "PlantsVsZombies")
            # Windows ASan runtime 最低支持 Windows 8.1；该预设不属于 Win7 发布矩阵。
            message(STATUS "Skipping Win7 import audit for ASan target ${target_name}")
            return()
        endif()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_win7_imports.ps1"
                    -LlvmReadObj "${PVZ_LLVM_READOBJ}"
                    -Binary "$<TARGET_FILE:${target_name}>"
                    -Win7ExportMap "${YY_THUNKS_WIN7_X64_EXPORT_MAP}"
            COMMENT "Checking ${target_name} imports against Windows 7 x64"
            VERBATIM
        )
    endfunction()
endif()

# FindVulkan 可能优先命中 vcpkg 的 vulkan-headers；VMA 仍由项目约定的 Vulkan SDK 提供，
# 因此不能把它的位置间接绑定到 Vulkan_INCLUDE_DIR。
set(VULKAN_SDK_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
set(VMA_HEADER "${VULKAN_SDK_INCLUDE_DIR}/vma/vk_mem_alloc.h")
if(NOT EXISTS "${VMA_HEADER}")
    message(FATAL_ERROR "Vulkan SDK 缺少 VMA 头文件: ${VMA_HEADER}")
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/PlantVsZombies)

# 自动收集源文件：CONFIGURE_DEPENDS 让 Ninja 在每次构建前检查 glob 结果，
# 新增/删除 .cpp 自动触发重新 configure，无需手动维护清单。
# 代价：vcxproj 之外多出的"野" .cpp 也会被编进来，须在下方排除名单里登记。
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)
# 排除名单：在磁盘上但不参与编译的文件（与 vcxproj 保持一致）
list(REMOVE_ITEM SOURCES
    ${SRC_DIR}/Reanimation/AttachmentSystem.cpp
)

add_executable(PlantsVsZombies ${SOURCES} "PlantVsZombies/Game/Zombie/DoorZombie.cpp" "PlantVsZombies/Game/Plant/HypnoShroom.h" "PlantVsZombies/Game/Zombie/FootballZombie.h" "PlantVsZombies/Game/Zombie/FootballZombie.cpp" )

target_include_directories(PlantsVsZombies PRIVATE ${SRC_DIR})
# Vulkan SDK 的 VMA 是第三方头；标为 SYSTEM，避免其 nullability 注解噪声破坏项目零警告基线。
target_include_directories(PlantsVsZombies SYSTEM PRIVATE
    ${Vulkan_INCLUDE_DIR}
    ${VULKAN_SDK_INCLUDE_DIR}
)

target_compile_definitions(PlantsVsZombies PRIVATE
    _CONSOLE
    VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:
        _HAS_ITERATOR_DEBUGGING=0 _SCL_SECURE_NO_WARNINGS>
)

# /utf-8 必须：源文件里有中文 UI 字符串（cl 与 clang-cl 都接受）
# 注：不需要 /MP——那是 MSBuild 单进程多文件的旗标，Ninja 本身按翻译单元并行调度
target_compile_options(PlantsVsZombies PRIVATE /utf-8 /W3 /sdl /EHsc)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Release 与 Playtest 使用相同的运行期优化；只有正式发布预设额外启用 LTO。
    target_compile_options(PlantsVsZombies PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:
            /O2                     # MSVC 风格，clang-cl 原生识别，不加 /clang:
            /fp:fast                # 同上，等价于 -ffast-math
            /clang:-fvectorize      # 每个 clang 专属选项单独传递
            /clang:-fomit-frame-pointer
        >
    )
    if(PVZ_ENABLE_AVX2)
        target_compile_options(PlantsVsZombies PRIVATE
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/arch:AVX2>
        )
    endif()
    if(PVZ_ENABLE_LTO)
        target_compile_options(PlantsVsZombies PRIVATE $<$<CONFIG:Release>:-flto>)
    endif()
    # TestDriver 只在 AutoTest 模式执行重逻辑；Release 保持正式 ABI 与 LTO，
    # 但跳过这个超大 JSON 命令翻译单元的前端优化，缩短日常完整构建尾部耗时。
    set_property(SOURCE PlantVsZombies/Game/AutoTest/TestDriver.cpp APPEND PROPERTY
        COMPILE_OPTIONS $<$<CONFIG:Release>:/Od>
    )
    target_link_options(PlantsVsZombies PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF /OPT:ICF>
    )
else()
    # 对应旧 Release|x64：MaxSpeed + fast-math + LTCG；AVX2 由发布预设显式选择。
    target_compile_options(PlantsVsZombies PRIVATE
        $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /Oy /fp:fast /GL>
    )
    if(PVZ_ENABLE_AVX2)
        target_compile_options(PlantsVsZombies PRIVATE $<$<CONFIG:Release>:/arch:AVX2>)
    endif()
    target_link_options(PlantsVsZombies PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

if(PVZ_ENABLE_PDB)
    # 只生成符号化回溯所需的函数/行表，不写变量与类型；GHASH 缩短 LLD 合并时间。
    # 信息最终进入外置 PDB；EXE 仅保留定位记录，且不泄露本机构建绝对路径。
    target_compile_options(PlantsVsZombies PRIVATE /Z7 -gline-tables-only -gcodeview-ghash)
    target_link_options(PlantsVsZombies PRIVATE /DEBUG:GHASH /PDBALTPATH:PlantsVsZombies.pdb)
    set_target_properties(PlantsVsZombies PROPERTIES
        PDB_NAME "PlantsVsZombies"
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

pvz_enable_address_sanitizer(PlantsVsZombies)

target_link_options(PlantsVsZombies PRIVATE /STACK:4194304,65536)

target_link_libraries(PlantsVsZombies PRIVATE
    $<$<PLATFORM_ID:Windows>:pvz_win7_compat>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    libopenmpt::libopenmpt
    glm::glm
    nlohmann_json::nlohmann_json
    pugixml::pugixml
    volk::volk
    version setupapi imm32 winmm shell32 ole32 legacy_stdio_definitions
)

if(WIN32)
    pvz_assert_win7_imports(PlantsVsZombies)
endif()

# 存档迁移是纯文件系统逻辑，单独目标可在不启动游戏、不接触真实玩家目录的情况下验证。
include(CTest)
if(BUILD_TESTING)
    add_executable(SaveMigrationTests
        tests/SaveMigrationTests.cpp
        PlantVsZombies/SaveMigration.cpp
     )
    target_include_directories(SaveMigrationTests PRIVATE ${SRC_DIR})
    target_compile_options(SaveMigrationTests PRIVATE /utf-8 /W3 /sdl /EHsc)
    target_link_libraries(SaveMigrationTests PRIVATE $<$<PLATFORM_ID:Windows>:pvz_win7_compat>)
    if(WIN32)
        pvz_assert_win7_imports(SaveMigrationTests)
    endif()
    add_test(NAME save-migration COMMAND SaveMigrationTests)

    add_executable(SaveSchemaTests
        tests/SaveSchemaTests.cpp
        PlantVsZombies/SaveSchema.cpp
     )
    target_include_directories(SaveSchemaTests PRIVATE ${SRC_DIR})
    target_compile_options(SaveSchemaTests PRIVATE /utf-8 /W3 /sdl /EHsc)
    target_link_libraries(SaveSchemaTests PRIVATE
        $<$<PLATFORM_ID:Windows>:pvz_win7_compat>
        nlohmann_json::nlohmann_json
    )
    if(WIN32)
        pvz_assert_win7_imports(SaveSchemaTests)
    endif()
    add_test(NAME save-schema COMMAND SaveSchemaTests)

    add_executable(MineGridTests tests/MineGridTests.cpp
        PlantVsZombies/Game/Board/MineGrid.cpp)
    target_include_directories(MineGridTests PRIVATE ${SRC_DIR})
    target_compile_options(MineGridTests PRIVATE /utf-8 /W3 /sdl /EHsc)
    target_link_libraries(MineGridTests PRIVATE $<$<PLATFORM_ID:Windows>:pvz_win7_compat>)
    if(WIN32)
        pvz_assert_win7_imports(MineGridTests)
    endif()
    add_test(NAME mine-grid COMMAND MineGridTests)

    add_executable(ExcavationPlannerTests tests/ExcavationPlannerTests.cpp
		tests/MineStrategyTests.cpp PlantVsZombies/Game/AI/MineWaveFormation.cpp
        tests/MineExcavationTacticsTests.cpp PlantVsZombies/Game/AI/MineExcavationTactics.cpp
        PlantVsZombies/Game/Board/MineGrid.cpp PlantVsZombies/Game/Board/MineGridPlanner.cpp)
    target_include_directories(ExcavationPlannerTests PRIVATE ${SRC_DIR})
    target_compile_options(ExcavationPlannerTests PRIVATE /utf-8 /W3 /sdl /EHsc)
    target_link_libraries(ExcavationPlannerTests PRIVATE $<$<PLATFORM_ID:Windows>:pvz_win7_compat>)
    if(WIN32)
        pvz_assert_win7_imports(ExcavationPlannerTests)
    endif()
    add_test(NAME excavation-planner COMMAND ExcavationPlannerTests)

    # 共享蒙特卡洛器保持纯数值边界，单测无需启动 SDL 或构造 Board。
    add_executable(PlantDefenseMonteCarloTests
        tests/PlantDefenseMonteCarloTests.cpp
        PlantVsZombies/Game/AI/PlantDefenseMonteCarlo.cpp
    )
    target_include_directories(PlantDefenseMonteCarloTests PRIVATE ${SRC_DIR})
    target_compile_options(PlantDefenseMonteCarloTests PRIVATE /utf-8 /W3 /sdl /EHsc)
    target_link_libraries(PlantDefenseMonteCarloTests PRIVATE
        $<$<PLATFORM_ID:Windows>:pvz_win7_compat>
    )
    if(WIN32)
        pvz_assert_win7_imports(PlantDefenseMonteCarloTests)
    endif()
    add_test(NAME plant-defense-monte-carlo COMMAND PlantDefenseMonteCarloTests)
endif()

# ---- GLSL → SPIR-V（复刻 vcxproj 的 CompileShaders Target，增量编译）----
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin REQUIRED)

set(SHADER_SOURCES
    batch.vert.glsl:vert
    batch.frag.glsl:frag
    batch_colorize.frag.glsl:frag
    batch_less_colorize.frag.glsl:frag
    pool.vert.glsl:vert
    pool.frag.glsl:frag
    reanim_inst.vert.glsl:vert
    reanim_inst.frag.glsl:frag
    reanim_inst_colorize.frag.glsl:frag
    reanim_inst_less_colorize.frag.glsl:frag
)

set(SPV_OUTPUTS "")
foreach(entry IN LISTS SHADER_SOURCES)
    string(REPLACE ":" ";" parts ${entry})
    list(GET parts 0 shader)
    list(GET parts 1 stage)
    # 只剥 .glsl（NAME_WE 会把 batch.vert.glsl 剥成 batch，与 batch.frag 撞名）
    string(REPLACE ".glsl" "" name ${shader})
    # 与旧工程一致：spv 落在源码树 Shader/spv/，所有 preset 共享同一份产物
    set(spv ${SRC_DIR}/Shader/spv/${name}.spv)
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SRC_DIR}/Shader/spv
        COMMAND ${GLSLC} -fshader-stage=${stage} --target-env=vulkan1.2 -O
                ${SRC_DIR}/Shader/${shader} -o ${spv}
        DEPENDS ${SRC_DIR}/Shader/${shader}
        COMMENT "[glslc] Shader/${shader} -> Shader/spv/${name}.spv"
    )
    list(APPEND SPV_OUTPUTS ${spv})
endforeach()

add_custom_target(CompileShaders DEPENDS ${SPV_OUTPUTS})
add_dependencies(PlantsVsZombies CompileShaders)

# 只拷 Shader 到输出目录；resources/font 由 clang-release 持有，其他预设通过目录联接共享。
# 注意：copy_directory 拷的是"目录内容"，目标必须写全 Shader/spv（游戏按 Shader/spv/*.spv 加载）
add_custom_command(TARGET PlantsVsZombies POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${SRC_DIR}/Shader/spv $<TARGET_FILE_DIR:PlantsVsZombies>/Shader/spv
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${SRC_DIR}/Shader/opengl $<TARGET_FILE_DIR:PlantsVsZombies>/Shader/opengl
    COMMENT "Copying Vulkan SPIR-V and OpenGL GLSL shaders to output directory"
)

# ---- 生成资源清单 resources/manifest.txt（取代运行时 std::filesystem 目录枚举）----
# 直接 glob 紧挨 exe 的真实资源目录（非发布预设会经目录联接看到同一份实体资源），
# 每次构建重生成 = 零过期窗口。运行时 FileManager::ListResourceFiles 经 SDL_RWops 读它，
# 使资源目录列举在 Android(APK) 可用。详见
# docs/superpowers/specs/2026-06-25-resource-manifest-enumeration-design.md
add_custom_command(TARGET PlantsVsZombies POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DRES_DIR=$<TARGET_FILE_DIR:PlantsVsZombies>/resources
            -DRES_PARENT=$<TARGET_FILE_DIR:PlantsVsZombies>
            -DOUT=$<TARGET_FILE_DIR:PlantsVsZombies>/resources/manifest.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_manifest.cmake
    COMMENT "Generating resources/manifest.txt"
)

# ---- 生成第三方许可证汇总 THIRD-PARTY-LICENSES.txt（紧挨 exe，随发布包分发）----
# 全树静态链接库均为 zlib/BSD/MIT 宽松证（零 copyleft），合规义务仅"随二进制分发物
# 附上版权与许可证声明"——本文件即满足。直接归集 vcpkg 的 <triplet>/share/*/copyright，
# 并从 Vulkan SDK 的 VMA 头文件提取原始 MIT 声明，每次构建重生成 = 零过期窗口。
# 仓库根另有一份提交快照供 GitHub 浏览；
# 依赖变动时本产物自动反映，根快照需重跑生成器刷新（见 cmake/gen_third_party_licenses.cmake）。
add_custom_command(TARGET PlantsVsZombies POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSHARE_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share
            -DVMA_HEADER=${VMA_HEADER}
            -DOUT=$<TARGET_FILE_DIR:PlantsVsZombies>/THIRD-PARTY-LICENSES.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_third_party_licenses.cmake
    COMMENT "Generating THIRD-PARTY-LICENSES.txt"
)

# ---- 头文件守卫：自动装 pre-commit 钩子 + configure 期 lint ----
# 迁离 .sln 后 VS 模板不再自动塞 #pragma once，这里在提交关口兜底并即时反馈。
# BOM-aware：只查 token 是否存在（不锚 ^），故 UTF-8 BOM 前缀不会误判。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_program(GIT_EXECUTABLE git)
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} config core.hooksPath .githooks
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE _hookcfg OUTPUT_QUIET ERROR_QUIET
        )
        if(_hookcfg EQUAL 0)
            message(STATUS "git core.hooksPath -> .githooks（头文件守卫 pre-commit 已启用）")
        endif()
    endif()
endif()

file(GLOB_RECURSE _ALL_HEADERS CONFIGURE_DEPENDS ${SRC_DIR}/*.h)
set(_UNGUARDED "")
foreach(_h IN LISTS _ALL_HEADERS)
    file(READ "${_h}" _head LIMIT 512)
    if(NOT _head MATCHES "#pragma once" AND NOT _head MATCHES "#ifndef")
        list(APPEND _UNGUARDED "${_h}")
    endif()
endforeach()
if(_UNGUARDED)
    string(REPLACE ";" "\n    " _UNGUARDED_MSG "${_UNGUARDED}")
    message(WARNING "以下头文件缺少守卫(#pragma once / #ifndef)，请补：\n    ${_UNGUARDED_MSG}")
endif()
