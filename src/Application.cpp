
#define WEBGPU_CPP_IMPLEMENTATION
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED

#include "Application.hpp"
#include "ResourceManager.hpp"
#include <array>
#include <cassert>
#include <glfw3webgpu.h>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <webgpu/webgpu.hpp>

// Avoid the "wgpu::" prefix in front of all WebGPU symbols
using namespace wgpu;
using glm::mat4x4;
using glm::vec3;
using glm::vec4;

constexpr float PI = 3.14159265358979323846f;

//----- Class Functions -----//
bool Application::Initialize(uint32_t width, uint32_t height) {
  _width = width;
  _height = height;

  // Move the whole initialization here
  // Open window
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  window = glfwCreateWindow(_width, _height, "Learn WebGPU", nullptr, nullptr);

  Instance instance = wgpuCreateInstance(nullptr);
  surface = glfwGetWGPUSurface(instance, window);

  // Request adapter
  std::cout << "Requesting adapter..." << std::endl;
  WGPURequestAdapterOptions adapterOpts = {};
  adapterOpts.nextInChain = nullptr;
  adapterOpts.compatibleSurface = surface;
  Adapter adapter = instance.requestAdapter(adapterOpts);
  std::cout << "Got adapter: " << adapter << std::endl;
  instance.release();

  // Get device
  DeviceDescriptor deviceDesc = {};
  deviceDesc.label = "My Device";
  deviceDesc.requiredFeatureCount = 0;
  deviceDesc.requiredLimits = nullptr;
  deviceDesc.defaultQueue.nextInChain = nullptr;
  deviceDesc.defaultQueue.label = "The default queue";
  deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason,
                                     char const *message, void *) {
    std::cout << "Device lost: reason " << reason;
    if (message)
      std::cout << " (" << message << ")";
    std::cout << std::endl;
  };
  // Before adapter.requestDevice(deviceDesc)
  RequiredLimits requiredLimits = _GetRequiredLimits(adapter);
  deviceDesc.requiredLimits = &requiredLimits;
  device = adapter.requestDevice(deviceDesc);
  std::cout << "Got device: " << device << std::endl;
  // Get device limits
  SupportedLimits deviceSupportedLimits;
  device.getLimits(&deviceSupportedLimits);
  deviceLimits = deviceSupportedLimits.limits;

  uncapturedErrorCallbackHandle = device.setUncapturedErrorCallback(
      [](ErrorType type, char const *message) {
        std::cout << "Uncaptured device error: type " << type;
        if (message)
          std::cout << " (" << message << ")";
        std::cout << std::endl;
      });

  queue = device.getQueue();

  // Configure the surface
  SurfaceConfiguration config = {};

  // Configuration of the textures created for the underlying swap chain
  config.width = _width;
  config.height = _height;
  config.usage = TextureUsage::RenderAttachment;
  surfaceFormat = surface.getPreferredFormat(adapter);
  config.format = surfaceFormat;

  // And we do not need any particular view format:
  config.viewFormatCount = 0;
  config.viewFormats = nullptr;
  config.device = device;
  config.presentMode = PresentMode::Fifo;
  config.alphaMode = CompositeAlphaMode::Auto;

  surface.configure(config);

  // Release the adapter only after it has been fully utilized
  adapter.release();

  _InitializePipeline();
  _InitializeBuffers();
  _InitializeBindGroups();

  // Create the depth texture
  TextureFormat depthTextureFormat = TextureFormat::Depth24Plus;
  TextureDescriptor depthTextureDesc;
  depthTextureDesc.dimension = TextureDimension::_2D;
  depthTextureDesc.format = depthTextureFormat;
  depthTextureDesc.mipLevelCount = 1;
  depthTextureDesc.sampleCount = 1;
  depthTextureDesc.size = {_width, _height, 1};
  depthTextureDesc.usage = TextureUsage::RenderAttachment;
  depthTextureDesc.viewFormatCount = 1;
  depthTextureDesc.viewFormats = (WGPUTextureFormat *)&depthTextureFormat;
  depthTexture = device.createTexture(depthTextureDesc);
  std::cout << "Depth texture: " << depthTexture << std::endl;

  // Create the view of the depth texture manipulated by the rasterizer
  TextureViewDescriptor depthTextureViewDesc;
  depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
  depthTextureViewDesc.baseArrayLayer = 0;
  depthTextureViewDesc.arrayLayerCount = 1;
  depthTextureViewDesc.baseMipLevel = 0;
  depthTextureViewDesc.mipLevelCount = 1;
  depthTextureViewDesc.dimension = TextureViewDimension::_2D;
  depthTextureViewDesc.format = depthTextureFormat;
  depthTextureView = depthTexture.createView(depthTextureViewDesc);
  std::cout << "Depth texture view: " << depthTextureView << std::endl;

  return true;
}

void Application::Terminate() {
  bindGroup.release();
  layout.release();
  bindGroupLayout.release();
  uniformBuffer.release();
  vertexBuffer.release();
  pipeline.release();
  surface.unconfigure();
  queue.release();
  surface.release();
  device.release();

  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::MainLoop() {
  glfwPollEvents();

  // Update uniform buffer
  uniforms.time =
      static_cast<float>(glfwGetTime()); // glfwGetTime returns a double
  // Only update the 1-st float of the buffer
  queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, time), &uniforms.time,
                    sizeof(MyUniforms::time));

  // Update view matrix
  float angle1 = uniforms.time;
  mat4x4 S = glm::scale(mat4x4(1.0), vec3(0.3f));
  mat4x4 T1 = glm::translate(mat4x4(1.0), vec3(0.5, 0.0, 0.0));
  mat4x4 R1 = glm::rotate(mat4x4(1.0), angle1, vec3(0.0, 0.0, 1.0));
  uniforms.modelMatrix = R1 * T1 * S;
  queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, modelMatrix),
                    &uniforms.modelMatrix, sizeof(MyUniforms::modelMatrix));

  // Get the next target texture view
  auto [surfaceTexture, targetView] = _GetNextSurfaceViewData();
  if (!targetView)
    return;

  // Create a command encoder for the draw call
  CommandEncoderDescriptor encoderDesc = {};
  encoderDesc.label = "My command encoder";
  CommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

  // Create the render pass that clears the screen with our color
  RenderPassDescriptor renderPassDesc = {};

  // The attachment part of the render pass descriptor describes the target
  // texture of the pass
  RenderPassColorAttachment renderPassColorAttachment = {};
  renderPassColorAttachment.view = targetView;
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = LoadOp::Clear;
  renderPassColorAttachment.storeOp = StoreOp::Store;
  renderPassColorAttachment.clearValue = WGPUColor{0.05, 0.05, 0.05, 1.0};
  renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;

  // We now add a depth/stencil attachment:
  RenderPassDepthStencilAttachment depthStencilAttachment;
  // The view of the depth texture
  depthStencilAttachment.view = depthTextureView;
  // The initial value of the depth buffer, meaning "far"
  depthStencilAttachment.depthClearValue = 1.0f;
  // Operation settings comparable to the color attachment
  depthStencilAttachment.depthLoadOp = LoadOp::Clear;
  depthStencilAttachment.depthStoreOp = StoreOp::Store;
  // we could turn off writing to the depth buffer globally here
  depthStencilAttachment.depthReadOnly = false;
  // Stencil setup, mandatory but unused
  depthStencilAttachment.stencilClearValue = 0;
  depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
  depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
  depthStencilAttachment.stencilReadOnly = true;
  renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

  renderPassDesc.timestampWrites = nullptr;

  // Create the render pass and end it immediately (we only clear the screen but
  // do not draw anything)
  RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

  // Select which render pipeline to use
  renderPass.setPipeline(pipeline);

  // Set vertex buffer while encoding the render pass
  renderPass.setVertexBuffer(0, vertexBuffer, 0,
                             indexCount * sizeof(VertexAttributes));

  uint32_t dynamicOffset = 0;

  // Set binding group
  dynamicOffset = 0 * uniformStride;
  renderPass.setBindGroup(0, bindGroup, 1, &dynamicOffset);
  renderPass.draw(indexCount, 1, 0, 0);

  renderPass.end();
  renderPass.release();

  // Finally encode and submit the render pass
  CommandBufferDescriptor cmdBufferDescriptor = {};
  cmdBufferDescriptor.label = "Command buffer";
  CommandBuffer command = encoder.finish(cmdBufferDescriptor);
  encoder.release();

  // std::cout << "Submitting command..." << std::endl;
  queue.submit(1, &command);
  command.release();
  // std::cout << "Command submitted." << std::endl;

  // At the end of the frame
  targetView.release();

  wgpuSurfacePresent(surface);
  wgpuDeviceTick(device);
}

bool Application::IsRunning() { return !glfwWindowShouldClose(window); }

std::pair<SurfaceTexture, TextureView> Application::_GetNextSurfaceViewData() {
  // Get the next surface texture
  SurfaceTexture surfaceTexture;
  surface.getCurrentTexture(&surfaceTexture);
  if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success) {
    return {surfaceTexture, nullptr};
  }
  Texture texture = surfaceTexture.texture;

  // Create a view for this surface texture
  TextureViewDescriptor viewDescriptor;
  viewDescriptor.label = "Surface texture view";
  viewDescriptor.format = texture.getFormat();
  viewDescriptor.dimension = TextureViewDimension::_2D;
  viewDescriptor.baseMipLevel = 0;
  viewDescriptor.mipLevelCount = 1;
  viewDescriptor.baseArrayLayer = 0;
  viewDescriptor.arrayLayerCount = 1;
  viewDescriptor.aspect = TextureAspect::All;
  TextureView targetView = texture.createView(viewDescriptor);

  texture.release();
  return {surfaceTexture, targetView};
}

void Application::_InitializePipeline() {
  // Load the shader module
  ShaderModuleDescriptor shaderDesc;
  // shaderDesc.hintCount = 0;
  // shaderDesc.hints = nullptr;

  std::cout << "Creating shader module..." << std::endl;
  ShaderModule shaderModule =
      ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", device);
  std::cout << "Shader module: " << shaderModule << std::endl;

  // Check for errors
  if (shaderModule == nullptr) {
    std::cerr << "Could not load shader!" << std::endl;
    exit(1);
  }

  // Create the render pipeline
  RenderPipelineDescriptor pipelineDesc;

  // Configure the vertex pipeline
  // We use one vertex buffer
  VertexBufferLayout vertexBufferLayout;
  // We now have 2 attributes
  std::vector<VertexAttribute> vertexAttribs(3);

  // Position attribute
  vertexAttribs[0].shaderLocation = 0;
  vertexAttribs[0].format = VertexFormat::Float32x3;
  vertexAttribs[0].offset = offsetof(VertexAttributes, position);

  // Normal attribute
  vertexAttribs[1].shaderLocation = 1;
  vertexAttribs[1].format = VertexFormat::Float32x3;
  vertexAttribs[1].offset = offsetof(VertexAttributes, normal);

  // Color attribute
  vertexAttribs[2].shaderLocation = 2;
  vertexAttribs[2].format = VertexFormat::Float32x3;
  vertexAttribs[2].offset = offsetof(VertexAttributes, color);

  vertexBufferLayout.attributeCount =
      static_cast<uint32_t>(vertexAttribs.size());
  vertexBufferLayout.attributes = vertexAttribs.data();

  vertexBufferLayout.arrayStride = sizeof(VertexAttributes);
  vertexBufferLayout.stepMode = VertexStepMode::Vertex;

  pipelineDesc.vertex.bufferCount = 1;
  pipelineDesc.vertex.buffers = &vertexBufferLayout;

  // NB: We define the 'shaderModule' in the second part of this chapter.
  // Here we tell that the programmable vertex shader stage is described
  // by the function called 'vs_main' in that module.
  pipelineDesc.vertex.module = shaderModule;
  pipelineDesc.vertex.entryPoint = "vs_main";
  pipelineDesc.vertex.constantCount = 0;
  pipelineDesc.vertex.constants = nullptr;

  // Each sequence of 3 vertices is considered as a triangle
  pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;

  // We'll see later how to specify the order in which vertices should be
  // connected. When not specified, vertices are considered sequentially.
  pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;

  // The face orientation is defined by assuming that when looking
  // from the front of the face, its corner vertices are enumerated
  // in the counter-clockwise (CCW) order.
  pipelineDesc.primitive.frontFace = FrontFace::CCW;

  // But the face orientation does not matter much because we do not
  // cull (i.e. "hide") the faces pointing away from us (which is often
  // used for optimization).
  pipelineDesc.primitive.cullMode = CullMode::None;

  // We tell that the programmable fragment shader stage is described
  // by the function called 'fs_main' in the shader module.
  FragmentState fragmentState;
  fragmentState.module = shaderModule;
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
  colorTarget.format = surfaceFormat;
  colorTarget.blend = &blendState;
  colorTarget.writeMask =
      ColorWriteMask::All; // We could write to only some of the color channels.

  // We have only one target because our render pass has only one output color
  // attachment.
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;
  pipelineDesc.fragment = &fragmentState;

  DepthStencilState depthStencilState = Default;
  depthStencilState.depthCompare = CompareFunction::Less;
  depthStencilState.depthWriteEnabled = true;
  // Store the format in a variable as later parts of the code depend on it
  TextureFormat depthTextureFormat = TextureFormat::Depth24Plus;
  depthStencilState.format = depthTextureFormat;
  // Deactivate the stencil alltogether
  depthStencilState.stencilReadMask = 0;
  depthStencilState.stencilWriteMask = 0;
  // Setup depth state
  pipelineDesc.depthStencil = &depthStencilState;

  // Samples per pixel
  pipelineDesc.multisample.count = 1;

  // Default value for the mask, meaning "all bits on"
  pipelineDesc.multisample.mask = ~0u;

  // Default value as well (irrelevant for count = 1 anyways)
  pipelineDesc.multisample.alphaToCoverageEnabled = false;

  // Define binding layout (don't forget to = Default)
  BindGroupLayoutEntry bindingLayout = Default;
  // The binding index as used in the @binding attribute in the shader
  bindingLayout.binding = 0;
  // The stage that needs to access this resource
  bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
  bindingLayout.buffer.type = BufferBindingType::Uniform;
  bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);
  bindingLayout.buffer.hasDynamicOffset = true;

  // Create a bind group layout
  BindGroupLayoutDescriptor bindGroupLayoutDesc{};
  bindGroupLayoutDesc.entryCount = 1;
  bindGroupLayoutDesc.entries = &bindingLayout;
  bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

  // Create the pipeline layout
  PipelineLayoutDescriptor layoutDesc{};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout *)&bindGroupLayout;
  layout = device.createPipelineLayout(layoutDesc);

  pipelineDesc.layout = layout;

  pipeline = device.createRenderPipeline(pipelineDesc);

  // We no longer need to access the shader module
  shaderModule.release();
}

RequiredLimits Application::_GetRequiredLimits(wgpu::Adapter adapter) const {
  // Get adapter supported limits, in case we need them
  SupportedLimits supportedLimits;
  adapter.getLimits(&supportedLimits);

  // Don't forget to = Default
  RequiredLimits requiredLimits = Default;

  // We use at most 2 vertex attributes
  requiredLimits.limits.maxVertexAttributes = 3;
  // We should also tell that we use 1 vertex buffers
  requiredLimits.limits.maxVertexBuffers = 1;
  // Maximum size of a buffer is 15 vertices of 5 float each
  requiredLimits.limits.maxBufferSize = 10000 * sizeof(VertexAttributes);
  // Maximum stride between 2 consecutive vertices in the vertex buffer
  requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);

  // There is a maximum of 3 float forwarded from vertex to fragment shader
  requiredLimits.limits.maxInterStageShaderComponents = 6;

  // We use at most 1 bind group for now
  requiredLimits.limits.maxBindGroups = 1;
  // We use at most 1 uniform buffer per stage
  requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
  // Uniform structs have a size of maximum 16 float (more than what we need)
  requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);

  requiredLimits.limits.maxDynamicUniformBuffersPerPipelineLayout = 1;

  // These two limits are different because they are "minimum" limits,
  // they are the only ones we are may forward from the adapter's supported
  // limits.
  requiredLimits.limits.minUniformBufferOffsetAlignment =
      supportedLimits.limits.minUniformBufferOffsetAlignment;
  requiredLimits.limits.minStorageBufferOffsetAlignment =
      supportedLimits.limits.minStorageBufferOffsetAlignment;

  return requiredLimits;
}

void Application::_InitializeBuffers() {
  // 1. Load from disk into CPU-side vectors vertexData and indexData
  // Define data vectors, but without filling them in
  std::vector<VertexAttributes> vertexData;

  // Here we use the new 'loadGeometry' function:
  bool success = ResourceManager::loadGeometryFromObj(
      RESOURCE_DIR "/mammoth.obj", vertexData);

  // Check for errors
  if (!success) {
    std::cerr << "Could not load geometry!" << std::endl;
    exit(1);
  }

  // Create vertex buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = vertexData.size() * sizeof(VertexAttributes);
  bufferDesc.usage =
      BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
  bufferDesc.mappedAtCreation = false;
  vertexBuffer = device.createBuffer(bufferDesc);
  indexCount = static_cast<int>(vertexData.size());

  // Upload geometry data to the buffer
  queue.writeBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);

  // Create uniform buffer (reusing bufferDesc from other buffer creations)
  // Subtility
  uniformStride = _ceilToNextMultiple(
      (uint32_t)sizeof(MyUniforms),
      (uint32_t)deviceLimits.minUniformBufferOffsetAlignment);
  // The buffer will contain 2 values for the uniforms plus the space in between
  // (NB: stride = sizeof(MyUniforms) + spacing)
  bufferDesc.size = uniformStride + sizeof(MyUniforms);
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
  bufferDesc.mappedAtCreation = false;
  uniformBuffer = device.createBuffer(bufferDesc);

  // Arbitrary time
  float angle1 = 2.0f;
  // Rotate the view point
  float angle2 = 3.0f * PI / 4.0f;
  float ratio = static_cast<float>(_width) / _height;
  float focalLength = 2.0;
  float near = 0.01f;
  float far = 100.0f;
  vec3 focalPoint(0.0, 0.0, -2.0);

  // Option C: A different way of using GLM extensions
  mat4x4 M(1.0);
  M = glm::rotate(M, angle1, vec3(0.0, 0.0, 1.0));
  M = glm::translate(M, vec3(0.5, 0.0, 0.0));
  M = glm::scale(M, vec3(0.3f));
  uniforms.modelMatrix = M;

  mat4x4 V(1.0);
  V = glm::translate(V, -focalPoint);
  V = glm::rotate(V, -angle2, vec3(1.0, 0.0, 0.0));
  uniforms.viewMatrix = V;

  float fov = 2 * glm::atan(1 / focalLength);
  uniforms.projectionMatrix = glm::perspective(fov, ratio, near, far);

  uniforms.time = 1.0f;
  uniforms.color = {0.0f, 1.0f, 0.4f, 1.0f};
  queue.writeBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));

  // Upload second value
  // uniforms.time = -1.0f;
  // uniforms.color = {1.0f, 1.0f, 1.0f, 0.5f};
  // queue.writeBuffer(uniformBuffer, uniformStride, &uniforms,
  //                   sizeof(MyUniforms));
  //                               ^^^^^^^^^^^^^ beware of the non-null offset!
}

void Application::_InitializeBindGroups() {
  // Create a binding
  BindGroupEntry binding{};
  // The index of the binding (the entries in bindGroupDesc can be in any order)
  binding.binding = 0;
  // The buffer it is actually bound to
  binding.buffer = uniformBuffer;
  // We can specify an offset within the buffer, so that a single buffer can
  // hold multiple uniform blocks.
  binding.offset = 0;
  // And we specify again the size of the buffer.
  binding.size = sizeof(MyUniforms);

  // A bind group contains one or multiple bindings
  BindGroupDescriptor bindGroupDesc{};
  bindGroupDesc.layout = bindGroupLayout;
  // There must be as many bindings as declared in the layout!
  bindGroupDesc.entryCount = 1;
  bindGroupDesc.entries = &binding;
  bindGroup = device.createBindGroup(bindGroupDesc);
}

/** Round 'value' up to the next multiplier of 'step' */
uint32_t Application::_ceilToNextMultiple(uint32_t value, uint32_t step) {
  uint32_t divide_and_ceil = value / step + (value % step == 0 ? 0 : 1);
  return step * divide_and_ceil;
}