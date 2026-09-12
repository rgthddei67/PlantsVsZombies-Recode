# Android 首版共享库。保留共用 Graphics 的 Vulkan 实现以避免复制绘制层，
# 但启动固定 GLES，Volk 仅动态取函数，不链接或加载 Android Vulkan loader。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(libopenmpt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
set(VOLK_PULL_IN_VULKAN OFF)
find_package(volk CONFIG REQUIRED)
set(PVZ_VMA_INCLUDE_DIR "" CACHE PATH "含 vma/vk_mem_alloc.h 的目录")
if(NOT EXISTS "${PVZ_VMA_INCLUDE_DIR}/vma/vk_mem_alloc.h")
    message(FATAL_ERROR "Set PVZ_VMA_INCLUDE_DIR to the VMA include root")
endif()
file(GLOB_RECURSE PVZ_ANDROID_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/PlantVsZombies/*.cpp")
list(FILTER PVZ_ANDROID_SOURCES EXCLUDE REGEX "/(GameMonitor|AttachmentSystem)\\.cpp$")
add_library(main SHARED ${PVZ_ANDROID_SOURCES})
target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PlantVsZombies")
target_include_directories(main SYSTEM PRIVATE "${PVZ_VMA_INCLUDE_DIR}")
target_compile_definitions(main PRIVATE VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
target_compile_options(main PRIVATE -Wall -Wno-unused-parameter)
# Android Release 明确使用 Full LTO；编译与链接必须同时启用，避免只生成 bitcode 却未做链接时优化。
target_compile_options(main PRIVATE $<$<CONFIG:Release>:-flto=full>)
target_link_options(main PRIVATE $<$<CONFIG:Release>:-flto=full>)
set_source_files_properties(PlantVsZombies/Game/AutoTest/TestDriver.cpp
    PROPERTIES COMPILE_OPTIONS -O0)
target_link_libraries(main PRIVATE SDL2::SDL2
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    libopenmpt::libopenmpt glm::glm nlohmann_json::nlohmann_json pugixml::pugixml
    volk::volk android log dl)
target_link_options(main PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
