#ifndef APPLICATION_HPP
#define APPLICATION_HPP

// #define WEBGPU_CPP_IMPLEMENTATION
#include <GLFW/glfw3.h>
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
  std::pair<wgpu::SurfaceTexture, wgpu::TextureView> _GetNextSurfaceViewData();
  void _InitializePipeline();

private:
  GLFWwindow *window;
  wgpu::Device device;
  wgpu::Queue queue;
  wgpu::Surface surface;
  std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
  wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
  wgpu::RenderPipeline pipeline;
};

#endif
