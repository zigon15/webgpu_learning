#pragma once

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>

class Scene {
public:
  struct LightingUniforms {
    std::array<glm::vec4, 2> directions;
    std::array<glm::vec4, 2> colors;
  };
  static_assert(sizeof(LightingUniforms) % 16 == 0);

  Scene(std::string objPath, std::string texturePath);

  bool onInit(wgpu::Device device, wgpu::Queue queue,
              wgpu::TextureFormat swapChainFormat,
              wgpu::TextureFormat depthTextureFormat, int width, int height,
              int viewportX, int viewportY, int viewportWidth,
              int viewportHeight);
  void onFinish();
  void onFrame(wgpu::CommandEncoder encoder, wgpu::TextureView renderTarget,
               wgpu::LoadOp loadOp, float time);
  void onResize(int width, int height, int viewportX, int viewportY,
                int viewportWidth, int viewportHeight);

  // Input events
  void onMouseMove(double xpos, double ypos);
  void onMouseButton(int button, int action, int mods, double xpos,
                     double ypos);
  void onScroll(double xoffset, double yoffset);

  // Getters for GUI
  void *getLightingUniformsPtr() { return &m_lightingUniforms; }
  LightingUniforms &getLightingUniforms() { return m_lightingUniforms; }
  bool &getLightingUniformsChanged() { return m_lightingUniformsChanged; }

  wgpu::TextureView getDepthTextureView() { return m_depthTextureView; }

private:
  bool initDepthBuffer(int width, int height);
  void terminateDepthBuffer();

  bool initRenderPipeline();
  void terminateRenderPipeline();

  bool initBindGroupLayout();
  void terminateBindGroupLayout();

  bool initTexture();
  void terminateTexture();

  bool initGeometry();
  void terminateGeometry();

  bool initUniforms();
  void terminateUniforms();

  bool initLightingUniforms();
  void terminateLightingUniforms();
  void updateLightingUniforms();

  bool initBindGroup();
  void terminateBindGroup();

  void updateProjectionMatrix();
  void updateViewMatrix();
  void updateDragInertia();

private:
  // (Just aliases to make notations lighter)
  using mat4x4 = glm::mat4x4;
  using vec4 = glm::vec4;
  using vec3 = glm::vec3;
  using vec2 = glm::vec2;

  struct MyUniforms {
    mat4x4 projectionMatrix;
    mat4x4 viewMatrix;
    mat4x4 modelMatrix;
    vec4 color;
    float time;
    float _pad[3];
  };
  static_assert(sizeof(MyUniforms) % 16 == 0);

private:
  struct CameraState {
    vec2 angles = {0.8f, 0.5f};
    float zoom = -1.2f;
  };

  struct DragState {
    bool active = false;
    vec2 startMouse;
    CameraState startCameraState;
    float sensitivity = 0.01f;
    float scrollSensitivity = 0.1f;
    vec2 velocity = {0.0, 0.0};
    vec2 previousDelta;
    float inertia = 0.9f;
  };

  wgpu::Device m_device = nullptr;
  wgpu::Queue m_queue = nullptr;
  wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
  wgpu::TextureFormat m_depthTextureFormat = wgpu::TextureFormat::Undefined;
  wgpu::Texture m_depthTexture = nullptr;
  wgpu::TextureView m_depthTextureView = nullptr;

  // Render Pipeline
  wgpu::BindGroupLayout m_bindGroupLayout = nullptr;
  wgpu::ShaderModule m_shaderModule = nullptr;
  wgpu::RenderPipeline m_pipeline = nullptr;

  // Texture
  wgpu::Sampler m_sampler = nullptr;
  wgpu::Texture m_texture = nullptr;
  wgpu::TextureView m_textureView = nullptr;

  // Geometry
  wgpu::Buffer m_vertexBuffer = nullptr;
  int m_vertexCount = 0;

  // Uniforms
  wgpu::Buffer m_uniformBuffer = nullptr;
  MyUniforms m_uniforms;

  wgpu::Buffer m_lightingUniformBuffer = nullptr;
  LightingUniforms m_lightingUniforms;
  bool m_lightingUniformsChanged = false;

  // Bind Group
  wgpu::BindGroup m_bindGroup = nullptr;

  CameraState m_cameraState;
  DragState m_drag;

  int m_width = 0;
  int m_height = 0;
  int m_viewportX = 0;
  int m_viewportY = 0;
  std::string m_objPath;
  std::string m_texturePath;
};
