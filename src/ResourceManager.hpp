#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include "Application.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>

class ResourceManager {
public:
  /**
   * Load a file from `path` using our ad-hoc format and populate the
   * `pointData` and `indexData` vectors.
   */
  static bool loadGeometry(const std::filesystem::path &path,
                           std::vector<float> &pointData,
                           std::vector<uint16_t> &indexData, int dimensions);

  /**
   * Create a shader module for a given WebGPU `device` from a WGSL shader
   * source loaded from file `path`.
   */
  static wgpu::ShaderModule loadShaderModule(const std::filesystem::path &path,
                                             wgpu::Device device);

  static bool loadGeometryFromObj(const std::filesystem::path &path,
                                  std::vector<VertexAttributes> &vertexData);
};
#endif