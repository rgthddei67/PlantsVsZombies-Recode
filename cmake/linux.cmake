# Linux 云端验证入口。Windows / Android 保持原有工具链和发布参数。
# 共用 Vulkan 源码仍参与编译；本入口部署 OpenGL GLSL，运行时使用 -Renderer=opengl。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(libopenmpt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(Threads REQUIRED)

find_path(PVZ_VOLK_INCLUDE_DIR volk.h REQUIRED)
find_library(PVZ_VOLK_LIBRARY NAMES volk REQUIRED)
find_path(PVZ_VMA_INCLUDE_DIR vma/vk_mem_alloc.h REQUIRED)
find_path(PVZ_VULKAN_INCLUDE_DIR vulkan/vulkan.h REQUIRED)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PlantVsZombies")
file(GLOB_RECURSE PVZ_LINUX_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
list(FILTER PVZ_LINUX_SOURCES EXCLUDE REGEX "/(GameMonitor|AttachmentSystem)\\.cpp$")
add_executable(PlantsVsZombies ${PVZ_LINUX_SOURCES})
target_include_directories(PlantsVsZombies PRIVATE "${SRC_DIR}")
target_include_directories(PlantsVsZombies SYSTEM PRIVATE
    "${PVZ_VOLK_INCLUDE_DIR}" "${PVZ_VMA_INCLUDE_DIR}" "${PVZ_VULKAN_INCLUDE_DIR}")
target_compile_definitions(PlantsVsZombies PRIVATE VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
target_compile_options(PlantsVsZombies PRIVATE -Wall -Wextra -Wno-unused-parameter)
# 沿用正式构建的 AutoTest 大型 JSON 翻译单元例外，避免无关的前端优化耗时。
set_source_files_properties(PlantVsZombies/Game/AutoTest/TestDriver.cpp
    PROPERTIES COMPILE_OPTIONS -O0)
target_link_libraries(PlantsVsZombies PRIVATE SDL2::SDL2
    SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf SDL2_mixer::SDL2_mixer
    libopenmpt::libopenmpt glm::glm nlohmann_json::nlohmann_json pugixml::pugixml
    "${PVZ_VOLK_LIBRARY}" Threads::Threads ${CMAKE_DL_LIBS})

add_custom_command(TARGET PlantsVsZombies POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${SRC_DIR}/Shader/opengl" "$<TARGET_FILE_DIR:PlantsVsZombies>/Shader/opengl"
    COMMAND ${CMAKE_COMMAND}
        -DRES_DIR=$<TARGET_FILE_DIR:PlantsVsZombies>/resources
        -DRES_PARENT=$<TARGET_FILE_DIR:PlantsVsZombies>
        -DOUT=$<TARGET_FILE_DIR:PlantsVsZombies>/resources/manifest.txt
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_manifest.cmake"
    COMMENT "Copying OpenGL shaders and generating Linux resource manifest")

include(CTest)
if(BUILD_TESTING)
    # 纯逻辑测试不依赖窗口或玩家存档，复用现有测试源码。
    function(pvz_linux_test target test_name)
        add_executable(${target} ${ARGN})
        target_include_directories(${target} PRIVATE "${SRC_DIR}")
        target_link_libraries(${target} PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()
    pvz_linux_test(SaveMigrationTests save-migration
        tests/SaveMigrationTests.cpp PlantVsZombies/SaveMigration.cpp)
    pvz_linux_test(SaveSchemaTests save-schema
        tests/SaveSchemaTests.cpp PlantVsZombies/SaveSchema.cpp)
    pvz_linux_test(MineGridTests mine-grid
        tests/MineGridTests.cpp PlantVsZombies/Game/Board/MineGrid.cpp)
    pvz_linux_test(ExcavationPlannerTests excavation-planner
        tests/ExcavationPlannerTests.cpp tests/MineStrategyTests.cpp
        PlantVsZombies/Game/AI/MineWaveFormation.cpp tests/MineExcavationTacticsTests.cpp
        PlantVsZombies/Game/AI/MineExcavationTactics.cpp
        PlantVsZombies/Game/Board/MineGrid.cpp PlantVsZombies/Game/Board/MineGridPlanner.cpp)
    pvz_linux_test(PlantDefenseMonteCarloTests plant-defense-monte-carlo
        tests/PlantDefenseMonteCarloTests.cpp PlantVsZombies/Game/AI/PlantDefenseMonteCarlo.cpp)
    pvz_linux_test(ColdStorageStrategyTests cold-storage-strategy
        tests/ColdStorageStrategyTests.cpp PlantVsZombies/Game/AI/ColdStoragePlanner.cpp
        PlantVsZombies/Game/AI/ColdStorageStrategy.cpp PlantVsZombies/Game/AI/ColdStorageSearch.cpp)
    pvz_linux_test(ThreadPoolTests thread-pool-generations tests/ThreadPoolTests.cpp)
    set_tests_properties(thread-pool-generations PROPERTIES TIMEOUT 45)
endif()
