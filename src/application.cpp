
#define WEBGPU_CPP_IMPLEMENTATION

#include "application.hpp"
#include <cassert>
#include <glfw3webgpu.h>
#include <iostream>
#include <vector>
#include <webgpu/webgpu.hpp>

// Avoid the "wgpu::" prefix in front of all WebGPU symbols
using namespace wgpu;

// We embbed the source of the shader module here
const char *shaderSource = R"(
@vertex
fn vs_main(@location(0) in_vertex_position: vec2f) -> @builtin(position) vec4f {
	return vec4f(in_vertex_position, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
	return vec4f(0.0, 0.4, 1.0, 1.0);
}
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
  renderPassColorAttachment.clearValue = WGPUColor{0.9, 0.1, 0.2, 1.0};
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
  renderPass.setVertexBuffer(0, vertexBuffer, 0, vertexBuffer.getSize());

  // We use the `vertexCount` variable instead of hard-coding the vertex count
  renderPass.draw(vertexCount, 1, 0, 0);

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

  // We use the extension mechanism to specify the WGSL part of the shader
  // module descriptor
  ShaderModuleWGSLDescriptor shaderCodeDesc;
  // Set the chained struct's header
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
  // Connect the chain
  shaderDesc.nextInChain = &shaderCodeDesc.chain;
  shaderCodeDesc.code = shaderSource;
  ShaderModule shaderModule = device.createShaderModule(shaderDesc);

  // Create the render pipeline
  RenderPipelineDescriptor pipelineDesc;

  // Configure the vertex pipeline
  // We use one vertex buffer
  VertexBufferLayout vertexBufferLayout;
  VertexAttribute positionAttrib;
  // == For each attribute, describe its layout, i.e., how to interpret the raw
  // data == Corresponds to @location(...)
  positionAttrib.shaderLocation = 0;
  // Means vec2f in the shader
  positionAttrib.format = VertexFormat::Float32x2;
  // Index of the first element
  positionAttrib.offset = 0;

  vertexBufferLayout.attributeCount = 1;
  vertexBufferLayout.attributes = &positionAttrib;

  // == Common to attributes from the same buffer ==
  vertexBufferLayout.arrayStride = 2 * sizeof(float);
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

  // We use at most 1 vertex attribute for now
  requiredLimits.limits.maxVertexAttributes = 1;
  // We should also tell that we use 1 vertex buffers
  requiredLimits.limits.maxVertexBuffers = 1;
  // Maximum size of a buffer is 6 vertices of 2 float each
  requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
  // Maximum stride between 2 consecutive vertices in the vertex buffer
  requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);

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
  // Vertex buffer data
  // There are 2 floats per vertex, one for x and one for y.
  std::vector<float> vertexData = {// Define a first triangle:
                                   -0.5, -0.5, +0.5, -0.5, +0.0, +0.5,

                                   // Add a second triangle:
                                   -0.55f, -0.5, -0.05f, +0.5, -0.55f, +0.5};
  vertexCount = static_cast<uint32_t>(vertexData.size() / 2);

  // Create vertex buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = vertexData.size() * sizeof(float);
  bufferDesc.usage =
      BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
  bufferDesc.mappedAtCreation = false;
  vertexBuffer = device.createBuffer(bufferDesc);

  // Upload geometry data to the buffer
  queue.writeBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);
}