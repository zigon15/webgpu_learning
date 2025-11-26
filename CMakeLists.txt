cmake_minimum_required(VERSION 3.0...3.25)
project(
    LearnWebGPU 
    VERSION 0.1.0
    LANGUAGES CXX C 
)

add_executable(App src/main.cpp src/application.cpp)

set_target_properties(App PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)
target_compile_options(App PRIVATE -Wall -Wextra -pedantic)

# Add dependencies
add_subdirectory(lib/webgpu)
add_subdirectory(lib/glfw3webgpu)
target_link_libraries(App PRIVATE webgpu glfw glfw3webgpu)

# The application's binary must find wgpu.dll or libwgpu.so at runtime,
# so we automatically copy it (it's called WGPU_RUNTIME_LIB in general)
# next to the binary.
target_copy_webgpu_binaries(App)