#include "Scene.hpp"
#include "ResourceManager.hpp"

#include <glm/ext.hpp>
#include <glm/gtx/polar_coordinates.hpp>
#include <iostream>

constexpr float PI = 3.14159265358979323846f;

using VertexAttributes = ResourceManager::VertexAttributes;
using namespace wgpu;

Scene::Scene(std::string objPath, std::string texturePath)
    : m_objPath(objPath), m_texturePath(texturePath) {}

bool Scene::onInit(wgpu::Device device, wgpu::Queue queue,
                   wgpu::TextureFormat swapChainFormat,
                   wgpu::TextureFormat depthTextureFormat, int width,
                   int height, int viewportX, int viewportY, int viewportWidth,
                   int viewportHeight) {
  m_device = device;
  m_queue = queue;
  m_swapChainFormat = swapChainFormat;
  m_depthTextureFormat = depthTextureFormat;
  m_width = viewportWidth;
  m_height = viewportHeight;
  m_viewportX = viewportX;
  m_viewportY = viewportY;

  std::cout << "Scene::onInit depthTextureFormat: " << (int)depthTextureFormat
            << std::endl;

  if (!initDepthBuffer(width, height))
    return false;

  if (!initBindGroupLayout())
    return false;
  if (!initRenderPipeline())
    return false;
  if (!initTexture())
    return false;
  if (!initGeometry())
    return false;
  if (!initUniforms())
    return false;
  if (!initLightingUniforms())
    return false;
  if (!initBindGroup())
    return false;

  return true;
}

void Scene::onFinish() {
  terminateBindGroup();
  terminateLightingUniforms();
  terminateUniforms();
  terminateGeometry();
  terminateTexture();
  terminateRenderPipeline();
  terminateBindGroupLayout();
}

void Scene::onFrame(wgpu::CommandEncoder encoder,
                    wgpu::TextureView renderTarget, wgpu::LoadOp loadOp,
                    float time) {
  updateLightingUniforms();
  updateDragInertia();

  // Update uniform buffer
  m_uniforms.time = time;
  m_queue.writeBuffer(m_uniformBuffer, offsetof(MyUniforms, time),
                      &m_uniforms.time, sizeof(MyUniforms::time));

  RenderPassDescriptor renderPassDesc{};
  RenderPassColorAttachment renderPassColorAttachment{};
  renderPassColorAttachment.view = renderTarget;
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = loadOp;
  renderPassColorAttachment.storeOp = StoreOp::Store;
  renderPassColorAttachment.clearValue = Color{0.05, 0.05, 0.05, 1.0};
  renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;

  RenderPassDepthStencilAttachment depthStencilAttachment{};
  depthStencilAttachment.view = m_depthTextureView;
  depthStencilAttachment.depthClearValue = 1.0f;
  depthStencilAttachment.depthLoadOp = LoadOp::Clear;
  depthStencilAttachment.depthStoreOp = StoreOp::Store;
  depthStencilAttachment.depthReadOnly = false;
  depthStencilAttachment.stencilClearValue = 0;
  depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
  depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
  depthStencilAttachment.stencilReadOnly = true;

  renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

  RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
  renderPass.setViewport((float)m_viewportX, (float)m_viewportY, (float)m_width,
                         (float)m_height, 0.0f, 1.0f);

  renderPass.setPipeline(m_pipeline);

  renderPass.setVertexBuffer(0, m_vertexBuffer, 0,
                             m_vertexCount * sizeof(VertexAttributes));

  // Set binding group
  renderPass.setBindGroup(0, m_bindGroup, 0, nullptr);

  renderPass.draw(m_vertexCount, 1, 0, 0);

  renderPass.end();
}

void Scene::onResize(int width, int height, int viewportX, int viewportY,
                     int viewportWidth, int viewportHeight) {
  m_width = viewportWidth;
  m_height = viewportHeight;
  m_viewportX = viewportX;
  m_viewportY = viewportY;

  terminateDepthBuffer();
  initDepthBuffer(width, height);

  updateProjectionMatrix();
}

bool Scene::initDepthBuffer(int width, int height) {
  // Create the depth texture
  TextureDescriptor depthTextureDesc;
  depthTextureDesc.dimension = TextureDimension::_2D;
  depthTextureDesc.format = m_depthTextureFormat;
  depthTextureDesc.mipLevelCount = 1;
  depthTextureDesc.sampleCount = 1;
  depthTextureDesc.size = {static_cast<uint32_t>(width),
                           static_cast<uint32_t>(height), 1};
  depthTextureDesc.usage = TextureUsage::RenderAttachment;
  depthTextureDesc.viewFormatCount = 1;
  depthTextureDesc.viewFormats = (WGPUTextureFormat *)&m_depthTextureFormat;
  m_depthTexture = m_device.createTexture(depthTextureDesc);
  std::cout << "Depth texture: " << m_depthTexture << std::endl;

  // Create the view of the depth texture manipulated by the rasterizer
  TextureViewDescriptor depthTextureViewDesc;
  depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
  depthTextureViewDesc.baseArrayLayer = 0;
  depthTextureViewDesc.arrayLayerCount = 1;
  depthTextureViewDesc.baseMipLevel = 0;
  depthTextureViewDesc.mipLevelCount = 1;
  depthTextureViewDesc.dimension = TextureViewDimension::_2D;
  depthTextureViewDesc.format = m_depthTextureFormat;
  m_depthTextureView = m_depthTexture.createView(depthTextureViewDesc);
  std::cout << "Depth texture view: " << m_depthTextureView << std::endl;

  return m_depthTextureView != nullptr;
}

void Scene::terminateDepthBuffer() {
  m_depthTextureView.release();
  m_depthTexture.destroy();
  m_depthTexture.release();
}

bool Scene::initRenderPipeline() {
  std::cout << "Creating shader module..." << std::endl;
  m_shaderModule =
      ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", m_device);
  std::cout << "Shader module: " << m_shaderModule << std::endl;

  std::cout << "Creating render pipeline..." << std::endl;
  RenderPipelineDescriptor pipelineDesc;

  // Vertex fetch
  std::vector<VertexAttribute> vertexAttribs(4);

  // Position attribute
  vertexAttribs[0].shaderLocation = 0;
  vertexAttribs[0].format = VertexFormat::Float32x3;
  vertexAttribs[0].offset = 0;

  // Normal attribute
  vertexAttribs[1].shaderLocation = 1;
  vertexAttribs[1].format = VertexFormat::Float32x3;
  vertexAttribs[1].offset = offsetof(VertexAttributes, normal);

  // Color attribute
  vertexAttribs[2].shaderLocation = 2;
  vertexAttribs[2].format = VertexFormat::Float32x3;
  vertexAttribs[2].offset = offsetof(VertexAttributes, color);

  // UV attribute
  vertexAttribs[3].shaderLocation = 3;
  vertexAttribs[3].format = VertexFormat::Float32x2;
  vertexAttribs[3].offset = offsetof(VertexAttributes, uv);

  VertexBufferLayout vertexBufferLayout;
  vertexBufferLayout.attributeCount = (uint32_t)vertexAttribs.size();
  vertexBufferLayout.attributes = vertexAttribs.data();
  vertexBufferLayout.arrayStride = sizeof(VertexAttributes);
  vertexBufferLayout.stepMode = VertexStepMode::Vertex;

  pipelineDesc.vertex.bufferCount = 1;
  pipelineDesc.vertex.buffers = &vertexBufferLayout;

  pipelineDesc.vertex.module = m_shaderModule;
  pipelineDesc.vertex.entryPoint = "vs_main";
  pipelineDesc.vertex.constantCount = 0;
  pipelineDesc.vertex.constants = nullptr;

  pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
  pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
  pipelineDesc.primitive.frontFace = FrontFace::CCW;
  pipelineDesc.primitive.cullMode = CullMode::None;

  FragmentState fragmentState;
  pipelineDesc.fragment = &fragmentState;
  fragmentState.module = m_shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.constantCount = 0;
  fragmentState.constants = nullptr;

  BlendState blendState;
  blendState.color.srcFactor = BlendFactor::SrcAlpha;
  blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
  blendState.color.operation = BlendOperation::Add;
  blendState.alpha.srcFactor = BlendFactor::Zero;
  blendState.alpha.dstFactor = BlendFactor::One;
  blendState.alpha.operation = BlendOperation::Add;

  ColorTargetState colorTarget;
  colorTarget.format = m_swapChainFormat;
  colorTarget.blend = &blendState;
  colorTarget.writeMask = ColorWriteMask::All;

  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  DepthStencilState depthStencilState = Default;
  depthStencilState.depthCompare = CompareFunction::Less;
  depthStencilState.depthWriteEnabled = true;
  depthStencilState.format = m_depthTextureFormat;
  depthStencilState.stencilReadMask = 0;
  depthStencilState.stencilWriteMask = 0;

  pipelineDesc.depthStencil = &depthStencilState;

  pipelineDesc.multisample.count = 1;
  pipelineDesc.multisample.mask = ~0u;
  pipelineDesc.multisample.alphaToCoverageEnabled = false;

  // Create the pipeline layout
  PipelineLayoutDescriptor layoutDesc{};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)&m_bindGroupLayout;
  PipelineLayout layout = m_device.createPipelineLayout(layoutDesc);
  pipelineDesc.layout = layout;

  m_pipeline = m_device.createRenderPipeline(pipelineDesc);
  std::cout << "Render pipeline: " << m_pipeline << std::endl;

  return m_pipeline != nullptr;
}

void Scene::terminateRenderPipeline() {
  m_pipeline.release();
  m_shaderModule.release();
}

bool Scene::initTexture() {
  // Create a sampler
  SamplerDescriptor samplerDesc;
  samplerDesc.addressModeU = AddressMode::Repeat;
  samplerDesc.addressModeV = AddressMode::Repeat;
  samplerDesc.addressModeW = AddressMode::Repeat;
  samplerDesc.magFilter = FilterMode::Linear;
  samplerDesc.minFilter = FilterMode::Linear;
  samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
  samplerDesc.lodMinClamp = 0.0f;
  samplerDesc.lodMaxClamp = 8.0f;
  samplerDesc.compare = CompareFunction::Undefined;
  samplerDesc.maxAnisotropy = 1;
  m_sampler = m_device.createSampler(samplerDesc);

  // Create a texture
  if (m_texturePath.empty()) {
    // Create a dummy 1x1 white texture
    TextureDescriptor textureDesc;
    textureDesc.dimension = TextureDimension::_2D;
    textureDesc.format = TextureFormat::RGBA8Unorm;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;
    textureDesc.size = {1, 1, 1};
    textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats = nullptr;
    m_texture = m_device.createTexture(textureDesc);

    TextureViewDescriptor textureViewDesc;
    textureViewDesc.aspect = TextureAspect::All;
    textureViewDesc.baseArrayLayer = 0;
    textureViewDesc.arrayLayerCount = 1;
    textureViewDesc.baseMipLevel = 0;
    textureViewDesc.mipLevelCount = 1;
    textureViewDesc.dimension = TextureViewDimension::_2D;
    textureViewDesc.format = TextureFormat::RGBA8Unorm;
    m_textureView = m_texture.createView(textureViewDesc);

    // Upload white color
    std::array<uint8_t, 4> white = {255, 255, 255, 255};
    ImageCopyTexture destination;
    destination.texture = m_texture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = TextureAspect::All;

    TextureDataLayout source;
    source.offset = 0;
    source.bytesPerRow = 4;
    source.rowsPerImage = 1;

    m_queue.writeTexture(destination, white.data(), white.size(), source,
                         textureDesc.size);

  } else {
    m_texture = ResourceManager::loadTexture(m_texturePath.c_str(), m_device,
                                             &m_textureView);
    if (!m_texture) {
      std::cerr << "Could not load texture: " << m_texturePath << std::endl;
      return false;
    }
  }
  std::cout << "Texture: " << m_texture << std::endl;
  std::cout << "Texture view: " << m_textureView << std::endl;

  return m_textureView != nullptr;
}

void Scene::terminateTexture() {
  m_textureView.release();
  m_texture.destroy();
  m_texture.release();
  m_sampler.release();
}

bool Scene::initGeometry() {
  // Load mesh data from OBJ file
  std::vector<VertexAttributes> vertexData;
  bool success =
      ResourceManager::loadGeometryFromObj(m_objPath.c_str(), vertexData);
  if (!success) {
    std::cerr << "Could not load geometry!" << std::endl;
    return false;
  }

  // Create vertex buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = vertexData.size() * sizeof(VertexAttributes);
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
  bufferDesc.mappedAtCreation = false;
  m_vertexBuffer = m_device.createBuffer(bufferDesc);
  m_queue.writeBuffer(m_vertexBuffer, 0, vertexData.data(), bufferDesc.size);

  m_vertexCount = static_cast<int>(vertexData.size());

  return m_vertexBuffer != nullptr;
}

void Scene::terminateGeometry() {
  m_vertexBuffer.destroy();
  m_vertexBuffer.release();
  m_vertexCount = 0;
}

bool Scene::initUniforms() {
  // Create uniform buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = sizeof(MyUniforms);
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
  bufferDesc.mappedAtCreation = false;
  m_uniformBuffer = m_device.createBuffer(bufferDesc);

  // Upload the initial value of the uniforms
  m_uniforms.modelMatrix = mat4x4(1.0);
  m_uniforms.viewMatrix =
      glm::lookAt(vec3(-2.0f, -3.0f, 2.0f), vec3(0.0f), vec3(0, 0, 1));
  m_uniforms.projectionMatrix = mat4x4(1.0);
  m_uniforms.time = 1.0f;
  m_uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
  m_queue.writeBuffer(m_uniformBuffer, 0, &m_uniforms, sizeof(MyUniforms));

  updateViewMatrix();
  updateProjectionMatrix();
  return m_uniformBuffer != nullptr;
}

void Scene::terminateUniforms() {
  m_uniformBuffer.destroy();
  m_uniformBuffer.release();
}

bool Scene::initBindGroupLayout() {
  std::vector<BindGroupLayoutEntry> bindingLayoutEntries(4, Default);

  // The uniform buffer binding that we already had
  BindGroupLayoutEntry &bindingLayout = bindingLayoutEntries[0];
  bindingLayout.binding = 0;
  bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
  bindingLayout.buffer.type = BufferBindingType::Uniform;
  bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

  // The texture binding
  BindGroupLayoutEntry &textureBindingLayout = bindingLayoutEntries[1];
  textureBindingLayout.binding = 1;
  textureBindingLayout.visibility = ShaderStage::Fragment;
  textureBindingLayout.texture.sampleType = TextureSampleType::Float;
  textureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

  // The texture sampler binding
  BindGroupLayoutEntry &samplerBindingLayout = bindingLayoutEntries[2];
  samplerBindingLayout.binding = 2;
  samplerBindingLayout.visibility = ShaderStage::Fragment;
  samplerBindingLayout.sampler.type = SamplerBindingType::Filtering;

  // The lighting uniform buffer binding
  BindGroupLayoutEntry &lightingUniformLayout = bindingLayoutEntries[3];
  lightingUniformLayout.binding = 3;
  lightingUniformLayout.visibility = ShaderStage::Fragment;
  lightingUniformLayout.buffer.type = BufferBindingType::Uniform;
  lightingUniformLayout.buffer.minBindingSize = sizeof(LightingUniforms);

  // Create a bind group layout
  BindGroupLayoutDescriptor bindGroupLayoutDesc{};
  bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
  bindGroupLayoutDesc.entries = bindingLayoutEntries.data();
  m_bindGroupLayout = m_device.createBindGroupLayout(bindGroupLayoutDesc);

  return m_bindGroupLayout != nullptr;
}

void Scene::terminateBindGroupLayout() { m_bindGroupLayout.release(); }

bool Scene::initBindGroup() {
  // Create a binding
  std::vector<BindGroupEntry> bindings(4);

  bindings[0].binding = 0;
  bindings[0].buffer = m_uniformBuffer;
  bindings[0].offset = 0;
  bindings[0].size = sizeof(MyUniforms);

  bindings[1].binding = 1;
  bindings[1].textureView = m_textureView;

  bindings[2].binding = 2;
  bindings[2].sampler = m_sampler;

  bindings[3].binding = 3;
  bindings[3].buffer = m_lightingUniformBuffer;
  bindings[3].offset = 0;
  bindings[3].size = sizeof(LightingUniforms);

  BindGroupDescriptor bindGroupDesc;
  bindGroupDesc.layout = m_bindGroupLayout;
  bindGroupDesc.entryCount = (uint32_t)bindings.size();
  bindGroupDesc.entries = bindings.data();
  m_bindGroup = m_device.createBindGroup(bindGroupDesc);

  return m_bindGroup != nullptr;
}

void Scene::terminateBindGroup() { m_bindGroup.release(); }

void Scene::updateProjectionMatrix() {
  if (m_width <= 0 || m_height <= 0)
    return;
  float ratio = m_width / (float)m_height;
  m_uniforms.projectionMatrix =
      glm::perspective(45 * PI / 180, ratio, 0.01f, 100.0f);
  m_queue.writeBuffer(m_uniformBuffer, offsetof(MyUniforms, projectionMatrix),
                      &m_uniforms.projectionMatrix,
                      sizeof(MyUniforms::projectionMatrix));
}

void Scene::updateViewMatrix() {
  float cx = cos(m_cameraState.angles.x);
  float sx = sin(m_cameraState.angles.x);
  float cy = cos(m_cameraState.angles.y);
  float sy = sin(m_cameraState.angles.y);
  vec3 position = vec3(cx * cy, sx * cy, sy) * std::exp(-m_cameraState.zoom);
  m_uniforms.viewMatrix = glm::lookAt(position, vec3(0.0f), vec3(0, 0, 1));
  m_queue.writeBuffer(m_uniformBuffer, offsetof(MyUniforms, viewMatrix),
                      &m_uniforms.viewMatrix, sizeof(MyUniforms::viewMatrix));
}

void Scene::onMouseMove(double xpos, double ypos) {
  if (m_drag.active) {
    vec2 currentMouse = vec2(-(float)xpos, (float)ypos);
    vec2 delta = (currentMouse - m_drag.startMouse) * m_drag.sensitivity;
    m_cameraState.angles = m_drag.startCameraState.angles + delta;
    // Clamp to avoid going too far when orbitting up/down
    m_cameraState.angles.y =
        glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
    updateViewMatrix();

    // Inertia
    m_drag.velocity = delta - m_drag.previousDelta;
    m_drag.previousDelta = delta;
  }
}

void Scene::onMouseButton(int button, int action, int /* modifiers */,
                          double xpos, double ypos) {
  if (button == 0) { // GLFW_MOUSE_BUTTON_LEFT
    switch (action) {
    case 1: // GLFW_PRESS
      m_drag.active = true;
      m_drag.startMouse = vec2(-(float)xpos, (float)ypos);
      m_drag.startCameraState = m_cameraState;
      break;
    case 0: // GLFW_RELEASE
      m_drag.active = false;
      break;
    }
  }
}

void Scene::onScroll(double /* xoffset */, double yoffset) {
  m_cameraState.zoom += m_drag.scrollSensitivity * static_cast<float>(yoffset);
  m_cameraState.zoom = glm::clamp(m_cameraState.zoom, -2.0f, 2.0f);
  updateViewMatrix();
}

void Scene::updateDragInertia() {
  constexpr float eps = 1e-4f;
  // Apply inertia only when the user released the click.
  if (!m_drag.active) {
    // Avoid updating the matrix when the velocity is no longer noticeable
    if (std::abs(m_drag.velocity.x) < eps &&
        std::abs(m_drag.velocity.y) < eps) {
      return;
    }
    m_cameraState.angles += m_drag.velocity;
    m_cameraState.angles.y =
        glm::clamp(m_cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
    // Dampen the velocity so that it decreases exponentially and stops
    // after a few frames.
    m_drag.velocity *= m_drag.inertia;
    updateViewMatrix();
  }
}

bool Scene::initLightingUniforms() {
  // Create uniform buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = sizeof(LightingUniforms);
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
  bufferDesc.mappedAtCreation = false;
  m_lightingUniformBuffer = m_device.createBuffer(bufferDesc);

  // Initial values
  m_lightingUniforms.directions[0] = {0.5f, -0.9f, 0.1f, 0.0f};
  m_lightingUniforms.directions[1] = {0.2f, 0.4f, 0.3f, 0.0f};
  m_lightingUniforms.colors[0] = {1.0f, 0.9f, 0.6f, 1.0f};
  m_lightingUniforms.colors[1] = {0.6f, 0.9f, 1.0f, 1.0f};

  m_lightingUniformsChanged = true;
  updateLightingUniforms();

  return m_lightingUniformBuffer != nullptr;
}

void Scene::terminateLightingUniforms() {
  m_lightingUniformBuffer.destroy();
  m_lightingUniformBuffer.release();
}

void Scene::updateLightingUniforms() {
  if (m_lightingUniformsChanged) {
    m_queue.writeBuffer(m_lightingUniformBuffer, 0, &m_lightingUniforms,
                        sizeof(LightingUniforms));
    m_lightingUniformsChanged = false;
  }
}
