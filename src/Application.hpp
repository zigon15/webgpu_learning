#ifndef APPLICATION_HPP
#define APPLICATION_HPP

// #define WEBGPU_CPP_IMPLEMENTATION
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED

#include <GLFW/glfw3.h>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <utility>
#include <webgpu/webgpu.hpp>

class Application {
public:
  // Initialize everything and return true if it went all right
  bool Initialize();

  // Uninitialize everything that was initialized
  void Terminate();

  // Draw a frame and handle events
  void MainLoop();

  // Return true as long as the main loop should keep on running
  bool IsRunning();

private:
  /**
   * The same structure as in the shader, replicated in C++
   * https://eliemichel.github.io/WebGPU-AutoLayout/
   */
  struct MyUniforms {
    // We add transform matrices
    glm::mat4x4 projectionMatrix;
    glm::mat4x4 viewMatrix;
    glm::mat4x4 modelMatrix;
    std::array<float, 4> color;
    float time;
    float _pad[3];
  };
  // Have the compiler check byte alignment
  static_assert(sizeof(MyUniforms) % 16 == 0);

  std::pair<wgpu::SurfaceTexture, wgpu::TextureView> _GetNextSurfaceViewData();
  void _InitializePipeline();
  wgpu::RequiredLimits _GetRequiredLimits(wgpu::Adapter adapter) const;
  void _InitializeBuffers();
  void _InitializeBindGroups();
  uint32_t _ceilToNextMultiple(uint32_t value, uint32_t step);

private:
  GLFWwindow *window;
  wgpu::Device device;
  wgpu::Queue queue;
  wgpu::Surface surface;
  std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
  wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
  wgpu::RenderPipeline pipeline;
  wgpu::Buffer pointBuffer;
  wgpu::Buffer indexBuffer;
  uint32_t indexCount;
  wgpu::Buffer uniformBuffer;
  wgpu::BindGroup bindGroup;
  wgpu::PipelineLayout layout;
  wgpu::BindGroupLayout bindGroupLayout;
  wgpu::Limits deviceLimits;
  uint32_t uniformStride;
  wgpu::Texture depthTexture;
  wgpu::TextureView depthTextureView;
  MyUniforms uniforms;
};

#endif
