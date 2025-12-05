/**
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://github.com/eliemichel/LearnWebGPU
 *
 * MIT License
 * Copyright (c) 2022-2023 Elie Michel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "Application.hpp"
#include "ResourceManager.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>
#include <glfw3webgpu.h>
#include <glm/gtx/polar_coordinates.hpp>
#include <imgui.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

using namespace wgpu;
using VertexAttributes = ResourceManager::VertexAttributes;

constexpr float SIDEBAR_WIDTH = 100.0f;

namespace ImGui {
bool DragDirection(const char *label, glm::vec4 &direction) {
  glm::vec2 angles = glm::degrees(glm::polar(glm::vec3(direction)));
  bool changed = ImGui::DragFloat2(label, glm::value_ptr(angles));
  direction = glm::vec4(glm::euclidean(glm::radians(angles)), direction.w);
  return changed;
}
} // namespace ImGui

///////////////////////////////////////////////////////////////////////////////
// Public methods

bool Application::onInit() {
  if (!initWindowAndDevice())
    return false;
  if (!initSwapChain())
    return false;
  if (!initGui())
    return false;

  m_depthTextureFormat = wgpu::TextureFormat::Depth24Plus;

  if (!m_sceneTop.onInit(m_device, m_queue, m_swapChainFormat,
                         m_depthTextureFormat, m_width, m_height,
                         (int)SIDEBAR_WIDTH, 0, m_width - (int)SIDEBAR_WIDTH,
                         m_height / 2))
    return false;
  if (!m_sceneBottom.onInit(m_device, m_queue, m_swapChainFormat,
                            m_depthTextureFormat, m_width, m_height,
                            (int)SIDEBAR_WIDTH, m_height / 2,
                            m_width - (int)SIDEBAR_WIDTH, m_height / 2))
    return false;

  // Sync lighting uniforms
  m_sceneBottom.getLightingUniforms() = m_sceneTop.getLightingUniforms();
  m_sceneBottom.getLightingUniformsChanged() = true;

  return true;
}

void Application::onFrame() {

  glfwPollEvents();

  // Update uniform buffer
  // Moved to Scene

  TextureView nextTexture = m_swapChain.getCurrentTextureView();
  if (!nextTexture) {
    std::cerr << "Cannot acquire next swap chain texture" << std::endl;
    return;
  }

  CommandEncoderDescriptor commandEncoderDesc;
  commandEncoderDesc.label = "Command Encoder";
  CommandEncoder encoder = m_device.createCommandEncoder(commandEncoderDesc);

  // Render Top Scene
  m_sceneTop.onFrame(encoder, nextTexture, LoadOp::Clear, glfwGetTime());

  // Render Bottom Scene
  m_sceneBottom.onFrame(encoder, nextTexture, LoadOp::Load, glfwGetTime());

  // Render GUI
  {
    RenderPassDescriptor renderPassDesc{};
    RenderPassColorAttachment renderPassColorAttachment{};
    renderPassColorAttachment.view = nextTexture;
    renderPassColorAttachment.resolveTarget = nullptr;
    renderPassColorAttachment.loadOp = LoadOp::Load;
    renderPassColorAttachment.storeOp = StoreOp::Store;
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &renderPassColorAttachment;

    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    updateGui(renderPass);
    renderPass.end();
  }

  nextTexture.release();

  CommandBufferDescriptor cmdBufferDescriptor{};
  cmdBufferDescriptor.label = "Command buffer";
  CommandBuffer command = encoder.finish(cmdBufferDescriptor);
  encoder.release();
  m_queue.submit(command);
  command.release();

  m_swapChain.present();

  // Check for pending error callbacks
  m_device.tick();
}

void Application::onFinish() {
  terminateGui();
  m_sceneTop.onFinish();
  m_sceneBottom.onFinish();
  terminateSwapChain();
  terminateWindowAndDevice();
}

bool Application::isRunning() { return !glfwWindowShouldClose(m_window); }

///////////////////////////////////////////////////////////////////////////////
// Private methods

bool Application::initWindowAndDevice() {
  m_instance = createInstance(InstanceDescriptor{});
  if (!m_instance) {
    std::cerr << "Could not initialize WebGPU!" << std::endl;
    return false;
  }

  if (!glfwInit()) {
    std::cerr << "Could not initialize GLFW!" << std::endl;
    return false;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  m_window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);
  if (!m_window) {
    std::cerr << "Could not open window!" << std::endl;
    return false;
  }

  std::cout << "Requesting adapter..." << std::endl;
  m_surface = glfwGetWGPUSurface(m_instance, m_window);
  RequestAdapterOptions adapterOpts{};
  adapterOpts.compatibleSurface = m_surface;
  Adapter adapter = m_instance.requestAdapter(adapterOpts);
  std::cout << "Got adapter: " << adapter << std::endl;

  SupportedLimits supportedLimits;
  adapter.getLimits(&supportedLimits);

  std::cout << "Requesting device..." << std::endl;
  RequiredLimits requiredLimits = Default;
  requiredLimits.limits.maxVertexAttributes = 4;
  requiredLimits.limits.maxVertexBuffers = 1;
  requiredLimits.limits.maxBufferSize = 150000 * sizeof(VertexAttributes);
  requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);
  requiredLimits.limits.minStorageBufferOffsetAlignment =
      supportedLimits.limits.minStorageBufferOffsetAlignment;
  requiredLimits.limits.minUniformBufferOffsetAlignment =
      supportedLimits.limits.minUniformBufferOffsetAlignment;
  requiredLimits.limits.maxInterStageShaderComponents = 8;
  requiredLimits.limits.maxBindGroups = 2;
  requiredLimits.limits.maxUniformBuffersPerShaderStage = 2;
  requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
  // Allow textures up to 2K
  requiredLimits.limits.maxTextureDimension1D = 2048;
  requiredLimits.limits.maxTextureDimension2D = 2048;
  requiredLimits.limits.maxTextureArrayLayers = 1;
  requiredLimits.limits.maxSampledTexturesPerShaderStage = 1;
  requiredLimits.limits.maxSamplersPerShaderStage = 1;

  DeviceDescriptor deviceDesc;
  deviceDesc.label = "My Device";
  deviceDesc.requiredFeatureCount = 0;
  deviceDesc.requiredLimits = &requiredLimits;
  deviceDesc.defaultQueue.label = "The default queue";
  m_device = adapter.requestDevice(deviceDesc);
  std::cout << "Got device: " << m_device << std::endl;

  // Add an error callback for more debug info
  m_errorCallbackHandle = m_device.setUncapturedErrorCallback(
      [](ErrorType type, char const *message) {
        std::cout << "Device error: type " << type;
        if (message)
          std::cout << " (message: " << message << ")";
        std::cout << std::endl;
      });

  m_queue = m_device.getQueue();

#ifdef WEBGPU_BACKEND_WGPU
  m_swapChainFormat = m_surface.getPreferredFormat(adapter);
#else
  m_swapChainFormat = TextureFormat::BGRA8Unorm;
#endif

  adapter.release();

  // Set the user pointer to be "this"
  glfwSetWindowUserPointer(m_window, this);
  // Use a non-capturing lambda as resize callback
  glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow *window, int, int) {
    auto that =
        reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
    if (that != nullptr)
      that->onResize();
  });
  glfwSetCursorPosCallback(
      m_window, [](GLFWwindow *window, double xpos, double ypos) {
        auto that =
            reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
          that->onMouseMove(xpos, ypos);
      });
  glfwSetMouseButtonCallback(
      m_window, [](GLFWwindow *window, int button, int action, int mods) {
        auto that =
            reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
          that->onMouseButton(button, action, mods);
      });
  glfwSetScrollCallback(
      m_window, [](GLFWwindow *window, double xoffset, double yoffset) {
        auto that =
            reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
        if (that != nullptr)
          that->onScroll(xoffset, yoffset);
      });

  return m_device != nullptr;
}

void Application::terminateWindowAndDevice() {
  m_queue.release();
  m_device.release();
  m_surface.release();
  m_instance.release();

  glfwDestroyWindow(m_window);
  glfwTerminate();
}

bool Application::initSwapChain() {
  // Get the current size of the window's framebuffer:
  // Get the current size of the window's framebuffer:
  glfwGetFramebufferSize(m_window, &m_width, &m_height);

  std::cout << "Creating swapchain..." << std::endl;
  SwapChainDescriptor swapChainDesc;
  swapChainDesc.width = static_cast<uint32_t>(m_width);
  swapChainDesc.height = static_cast<uint32_t>(m_height);
  std::cout << "Swapchain format: " << (int)m_swapChainFormat << std::endl;
  swapChainDesc.usage = TextureUsage::RenderAttachment;
  swapChainDesc.format = m_swapChainFormat;
  swapChainDesc.presentMode = PresentMode::Fifo;
  m_swapChain = m_device.createSwapChain(m_surface, swapChainDesc);
  std::cout << "Swapchain: " << m_swapChain << std::endl;
  return m_swapChain != nullptr;
}

void Application::terminateSwapChain() { m_swapChain.release(); }

// Methods moved to Scene

void Application::onResize() {
  terminateSwapChain();
  initSwapChain();
  m_sceneTop.onResize(m_width, m_height, (int)SIDEBAR_WIDTH, 0,
                      m_width - (int)SIDEBAR_WIDTH, m_height / 2);
  m_sceneBottom.onResize(m_width, m_height, (int)SIDEBAR_WIDTH, m_height / 2,
                         m_width - (int)SIDEBAR_WIDTH, m_height / 2);
}

void Application::onMouseMove(double xpos, double ypos) {
  if (xpos > SIDEBAR_WIDTH) {
    if (ypos < m_height / 2.0) {
      m_sceneTop.onMouseMove(xpos - SIDEBAR_WIDTH, ypos);
    } else {
      m_sceneBottom.onMouseMove(xpos - SIDEBAR_WIDTH, ypos - m_height / 2.0);
    }
  }
}

void Application::onMouseButton(int button, int action, int mods) {
  if (ImGui::GetCurrentContext() != nullptr && action == GLFW_PRESS &&
      ImGui::GetIO().WantCaptureMouse) {
    return;
  }
  double xpos, ypos;
  glfwGetCursorPos(m_window, &xpos, &ypos);
  if (xpos > SIDEBAR_WIDTH) {
    if (ypos < m_height / 2.0) {
      m_sceneTop.onMouseButton(button, action, mods, xpos - SIDEBAR_WIDTH,
                               ypos);
    } else {
      m_sceneBottom.onMouseButton(button, action, mods, xpos - SIDEBAR_WIDTH,
                                  ypos - m_height / 2.0);
    }
  }
}

void Application::onScroll(double xoffset, double yoffset) {
  if (ImGui::GetCurrentContext() != nullptr &&
      ImGui::GetIO().WantCaptureMouse) {
    return;
  }
  double xpos, ypos;
  glfwGetCursorPos(m_window, &xpos, &ypos);
  if (xpos > SIDEBAR_WIDTH) {
    if (ypos < m_height / 2.0) {
      m_sceneTop.onScroll(xoffset, yoffset);
    } else {
      m_sceneBottom.onScroll(xoffset, yoffset);
    }
  }
}

bool Application::initGui() {
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOther(m_window, true);
  ImGui_ImplWGPU_Init(m_device, 3, m_swapChainFormat,
                      wgpu::TextureFormat::Undefined);
  return true;
}

void Application::terminateGui() {
  ImGui_ImplGlfw_Shutdown();
  ImGui_ImplWGPU_Shutdown();
}

void DrawRightSidebar() {
  // 1. Setup dimensions
  float width = SIDEBAR_WIDTH;

  // 2. Get the main viewport (the full OS window or screen)
  const ImGuiViewport *viewport = ImGui::GetMainViewport();

  // 3. Calculate Position
  // X = Start of work area + Total width - Panel width
  // Y = Start of work area (accounts for top menu bars)
  ImVec2 work_pos =
      viewport->WorkPos; // Use WorkPos to avoid covering menu bars
  ImVec2 work_size = viewport->WorkSize;

  ImVec2 pos = ImVec2(0, work_pos.y);
  ImVec2 size = ImVec2(width, work_size.y);

  // 4. Force Position and Size
  // ImGuiCond_Always ensures it snaps back if the user tries to move it
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);

  // 5. Set Window Flags to make it look like a static panel
  ImGuiWindowFlags window_flags = 0;
  window_flags |= ImGuiWindowFlags_NoTitleBar; // Remove blue header
  window_flags |= ImGuiWindowFlags_NoResize;   // User can't change size
  window_flags |= ImGuiWindowFlags_NoMove;     // User can't drag it
  window_flags |= ImGuiWindowFlags_NoCollapse; // User can't minimize it
  window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus; // Optional: keeps it
                                                          // in background layer

  // 6. Draw
  ImGui::Begin("RightStickyPanel", nullptr, window_flags);

  ImGui::Text("I am");
  ImGui::Text("Stuck!");
  ImGui::Button("Action", ImVec2(-1, 0)); // Button spans full width

  ImGui::End();
}

void Application::updateGui(RenderPassEncoder renderPass) {
  // Start the Dear ImGui frame
  ImGui_ImplWGPU_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Build our UI
  ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

  ImGui::Begin("Lighting");

  bool changed = false;
  ImGui::Begin("Lighting");
  changed = ImGui::ColorEdit3(
      "Color #0", (float *)&m_sceneTop.getLightingUniforms().colors[0]);
  changed =
      ImGui::ColorEdit3("Color #1",
                        (float *)&m_sceneTop.getLightingUniforms().colors[1]) ||
      changed;
  changed =
      ImGui::DragDirection("Direction #0",
                           m_sceneTop.getLightingUniforms().directions[0]) ||
      changed;
  changed =
      ImGui::DragDirection("Direction #1",
                           m_sceneTop.getLightingUniforms().directions[1]) ||
      changed;
  ImGui::End();

  if (changed) {
    m_sceneTop.getLightingUniformsChanged() = true;
    m_sceneBottom.getLightingUniforms() = m_sceneTop.getLightingUniforms();
    m_sceneBottom.getLightingUniformsChanged() = true;
  }
  ImGui::End();

  DrawRightSidebar();

  // Draw the UI
  ImGui::EndFrame();
  // Convert the UI defined above into low-level drawing commands
  ImGui::Render();
  // Execute the low-level drawing commands on the WebGPU backend
  ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
}

// Methods moved to Scene