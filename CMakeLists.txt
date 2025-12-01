cmake_minimum_required(VERSION 3.0...3.25)
project(
    LearnWebGPU 
    VERSION 0.1.0
    LANGUAGES CXX C 
)

add_executable(App 
    src/main.cpp 
    src/Application.cpp 
    src/ResourceManager.cpp
    src/implementations.cpp
)

set_target_properties(App PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)
target_compile_options(App PRIVATE -Wall -Wextra -pedantic)

# Configure GLFW to use Wayland and disable X11
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)

# Add dependencies
add_subdirectory(lib/webgpu)
add_subdirectory(lib/glfw3webgpu)
add_subdirectory(lib/glm)
target_link_libraries(App PRIVATE webgpu glfw glfw3webgpu)

# In dev mode, we load resources from the source tree, so that when we
# dynamically edit resources (like shaders), these are correctly
# versionned.
target_compile_definitions(App PRIVATE
    RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_include_directories(App PRIVATE . ./lib/tinyObjLoader ./lib/stbImage)


# The application's binary must find wgpu.dll or libwgpu.so at runtime,
# so we automatically copy it (it's called WGPU_RUNTIME_LIB in general)
# next to the binary.
target_copy_webgpu_binaries(App)