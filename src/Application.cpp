
#define WEBGPU_CPP_IMPLEMENTATION

#include "Application.hpp"
#include "ResourceManager.hpp"
#include <cassert>
#include <glfw3webgpu.h>
#include <iostream>
#include <vector>
#include <webgpu/webgpu.hpp>

// Avoid the "wgpu::" prefix in front of all WebGPU symbols
using namespace wgpu;

// We embbed the source of the shader module here
const char *shaderSource = R"(

)";

//----- Class Functions -----//
bool Application::Initialize() {
  // Move the whole initialization here
  // Open window
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

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
  config.width = 640;
  config.height = 480;
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

  return true;
}

void Application::Terminate() {
  pointBuffer.release();
  indexBuffer.release();
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
  renderPassDesc.depthStencilAttachment = nullptr;
  renderPassDesc.timestampWrites = nullptr;

  // Create the render pass and end it immediately (we only clear the screen but
  // do not draw anything)
  RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

  // Select which render pipeline to use
  renderPass.setPipeline(pipeline);

  // Set vertex buffer while encoding the render pass
  renderPass.setVertexBuffer(0, pointBuffer, 0, pointBuffer.getSize());

  // The second argument must correspond to the choice of uint16_t or uint32_t
  // we've done when creating the index buffer.
  renderPass.setIndexBuffer(indexBuffer, IndexFormat::Uint16, 0,
                            indexBuffer.getSize());

  // Replace `draw()` with `drawIndexed()` and `vertexCount` with `indexCount`
  // The extra argument is an offset within the index buffer.
  renderPass.drawIndexed(indexCount, 1, 0, 0, 0);

  renderPass.end();
  renderPass.release();

  // Finally encode and submit the render pass
  CommandBufferDescriptor cmdBufferDescriptor = {};
  cmdBufferDescriptor.label = "Command buffer";
  CommandBuffer command = encoder.finish(cmdBufferDescriptor);
  encoder.release();

  std::cout << "Submitting command..." << std::endl;
  queue.submit(1, &command);
  command.release();
  std::cout << "Command submitted." << std::endl;

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
  std::vector<VertexAttribute> vertexAttribs(2);

  // Describe the position attribute
  vertexAttribs[0].shaderLocation = 0; // @location(0)
  vertexAttribs[0].format = VertexFormat::Float32x2;
  vertexAttribs[0].offset = 0;

  // Describe the color attribute
  vertexAttribs[1].shaderLocation = 1;               // @location(1)
  vertexAttribs[1].format = VertexFormat::Float32x3; // different type!
  vertexAttribs[1].offset = 2 * sizeof(float);       // non null offset!

  vertexBufferLayout.attributeCount =
      static_cast<uint32_t>(vertexAttribs.size());
  vertexBufferLayout.attributes = vertexAttribs.data();

  vertexBufferLayout.arrayStride = 5 * sizeof(float);
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

  // We do not use stencil/depth testing for now
  pipelineDesc.depthStencil = nullptr;

  // Samples per pixel
  pipelineDesc.multisample.count = 1;

  // Default value for the mask, meaning "all bits on"
  pipelineDesc.multisample.mask = ~0u;

  // Default value as well (irrelevant for count = 1 anyways)
  pipelineDesc.multisample.alphaToCoverageEnabled = false;
  pipelineDesc.layout = nullptr;

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
  requiredLimits.limits.maxVertexAttributes = 2;
  //                                          ^ This was 1
  // We should also tell that we use 1 vertex buffers
  requiredLimits.limits.maxVertexBuffers = 1;
  // Maximum size of a buffer is 15 vertices of 5 float each
  requiredLimits.limits.maxBufferSize = 15 * 5 * sizeof(float);
  //                                        ^ This was a 2
  // Maximum stride between 2 consecutive vertices in the vertex buffer
  requiredLimits.limits.maxVertexBufferArrayStride = 5 * sizeof(float);
  //                                                 ^ This was a 2

  // There is a maximum of 3 float forwarded from vertex to fragment shader
  requiredLimits.limits.maxInterStageShaderComponents = 3;

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
  // 1. Load from disk into CPU-side vectors pointData and indexData
  // Define data vectors, but without filling them in
  std::vector<float> pointData;
  std::vector<uint16_t> indexData;

  // Here we use the new 'loadGeometry' function:
  bool success = ResourceManager::loadGeometry(RESOURCE_DIR "/webgpu.txt",
                                               pointData, indexData);

  // Check for errors
  if (!success) {
    std::cerr << "Could not load geometry!" << std::endl;
    exit(1);
  }

  // We now store the index count rather than the vertex count
  indexCount = static_cast<uint32_t>(indexData.size());

  // Create vertex buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = pointData.size() * sizeof(float);
  bufferDesc.usage =
      BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
  bufferDesc.mappedAtCreation = false;
  pointBuffer = device.createBuffer(bufferDesc);

  // Upload geometry data to the buffer
  queue.writeBuffer(pointBuffer, 0, pointData.data(), bufferDesc.size);

  // Create index buffer
  // (we reuse the bufferDesc initialized for the pointBuffer)
  bufferDesc.size = indexData.size() * sizeof(uint16_t);
  bufferDesc.size =
      (bufferDesc.size + 3) & ~3; // round up to the next multiple of 4
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
  indexBuffer = device.createBuffer(bufferDesc);

  queue.writeBuffer(indexBuffer, 0, indexData.data(), bufferDesc.size);
}