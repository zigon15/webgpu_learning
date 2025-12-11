#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <vector>
#include <webgpu/webgpu.hpp>

class ZMesh {
public:
  /**
   * A structure that describes the data layout in the vertex buffer,
   * used by loadGeometryFromObj and used it in `sizeof` and `offsetof`
   * when uploading data to the GPU.
   */
  struct VertexAttributes {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
  };

public:
  ZMesh(wgpu::Device &rDevice, wgpu::Queue &rQueue);
  ~ZMesh();

  int init(const std::vector<VertexAttributes> &vertices);
  int init(const std::filesystem::path &path);

  int render(wgpu::RenderPassEncoder &rRenderPassEncoder);

private:
 int _createVertexBuffer();

private:
  wgpu::Device &_rDevice;
  wgpu::Queue &_rQueue;
  std::vector<VertexAttributes> _vertexData;
  wgpu::Buffer _vertexBuffer = nullptr;
};