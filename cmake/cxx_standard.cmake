# 三个平台及测试目标共用标准；不允许 CMake 静默降级，也不关闭 char8_t。
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 当前仍使用头文件，不因切换标准触发全树 module 扫描或额外的扫描器依赖。
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# SDL/JSON/日志接口仍使用 UTF-8 char 字节；普通窄字符串必须跨平台产生相同编码。
if(MSVC)
    set(_pvz_utf8_options /utf-8)
else()
    set(_pvz_utf8_options -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
add_compile_options(${_pvz_utf8_options})

# 仅编译/链接，不运行交叉编译产物；让编码或语言模式错误在配置阶段直接报告。
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
string(JOIN " " CMAKE_REQUIRED_FLAGS ${_pvz_utf8_options})
check_cxx_source_compiles([=[
    #include <filesystem>
    #include <string_view>
    #include <type_traits>
    #if defined(_MSVC_LANG)
    static_assert(_MSVC_LANG > 202002L);
    #else
    static_assert(__cplusplus > 202002L);
    #endif
    static_assert(__cpp_char8_t >= 201811L);
    constexpr std::string_view text = "中文";
    static_assert(text.size() == 6);
    static_assert(static_cast<unsigned char>(text[0]) == 0xE4
        && static_cast<unsigned char>(text[1]) == 0xB8
        && static_cast<unsigned char>(text[2]) == 0xAD
        && static_cast<unsigned char>(text[3]) == 0xE6
        && static_cast<unsigned char>(text[4]) == 0x96
        && static_cast<unsigned char>(text[5]) == 0x87);
    static_assert(std::is_same_v<decltype(std::filesystem::path{}.u8string()), std::u8string>);
    int main() { return 0; }
]=] PVZ_CXX23_UTF8_SUPPORTED)
cmake_pop_check_state()
if(NOT PVZ_CXX23_UTF8_SUPPORTED)
    message(FATAL_ERROR "PvZ requires C++23, native char8_t, and UTF-8 narrow literals. Check the compiler/standard library and CMake configure log.")
endif()
