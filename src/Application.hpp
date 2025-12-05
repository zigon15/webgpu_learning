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

#pragma once

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include "Scene.hpp"

// Forward declare
struct GLFWwindow;

class Application {
public:
  // A function called only once at the beginning. Returns false is init failed.
  bool onInit();

  // A function called at each frame, guaranteed never to be called before
  // `onInit`.
  void onFrame();

  // A function called only once at the very end.
  void onFinish();

  // A function that tells if the application is still running.
  bool isRunning();

  // A function called when the window is resized.
  void onResize();

private:
  bool initWindowAndDevice();
  void terminateWindowAndDevice();

  bool initSwapChain();
  void terminateSwapChain();

  bool initGui();                                     // called in onInit
  void terminateGui();                                // called in onFinish
  void updateGui(wgpu::RenderPassEncoder renderPass); // called in onFrame

  // Mouse events
  void onMouseMove(double xpos, double ypos);
  void onMouseButton(int button, int action, int mods);
  void onScroll(double xoffset, double yoffset);

private:
  // Window and Device
  GLFWwindow *m_window = nullptr;
  wgpu::Instance m_instance = nullptr;
  wgpu::Surface m_surface = nullptr;
  wgpu::Device m_device = nullptr;
  wgpu::Queue m_queue = nullptr;
  wgpu::TextureFormat m_swapChainFormat = wgpu::TextureFormat::Undefined;
  wgpu::TextureFormat m_depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
  // Keep the error callback alive
  std::unique_ptr<wgpu::ErrorCallback> m_errorCallbackHandle;

  int m_width = 0;
  int m_height = 0;

  // Swap Chain
  wgpu::SwapChain m_swapChain = nullptr;

  Scene m_sceneTop = {RESOURCE_DIR "/fourareen.obj",
                      RESOURCE_DIR "/fourareen2K_albedo.jpg"};
  Scene m_sceneBottom = {RESOURCE_DIR "/pyramid.obj", ""};
};