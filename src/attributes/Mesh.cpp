#include "Mesh.hpp"

#include "tiny_obj_loader.h"
#include <iostream>

using namespace wgpu;

ZMesh::ZMesh(Device &rDevice, Queue &rQueue)
    : _rDevice(rDevice), _rQueue(rQueue), _vertexData{} {}

ZMesh::~ZMesh() { _vertexBuffer.release(); }

int ZMesh::init(const std::vector<VertexAttributes> &vertices) {
  _vertexData = vertices;

  _createVertexBuffer();
  return 0;
}

int ZMesh::init(const std::filesystem::path &objPath) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;

  std::string warn;
  std::string err;

  // Call the core loading procedure of TinyOBJLoader
  bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                              objPath.string().c_str());

  // Check errors
  if (!warn.empty()) {
    std::cout << warn << std::endl;
  }

  if (!err.empty()) {
    std::cerr << err << std::endl;
  }

  if (!ret) {
    return 1;
  }

  // Filling in vertexData
  _vertexData.clear();
  for (const auto &shape : shapes) {
    size_t offset = _vertexData.size();
    _vertexData.resize(offset + shape.mesh.indices.size());

    for (size_t i = 0; i < shape.mesh.indices.size(); ++i) {
      const tinyobj::index_t &idx = shape.mesh.indices[i];

      _vertexData[offset + i].position = {
          attrib.vertices[3 * idx.vertex_index + 0],
          -attrib.vertices[3 * idx.vertex_index + 2],
          attrib.vertices[3 * idx.vertex_index + 1]};

      _vertexData[offset + i].normal = {
          attrib.normals[3 * idx.normal_index + 0],
          -attrib.normals[3 * idx.normal_index + 2],
          attrib.normals[3 * idx.normal_index + 1]};

      _vertexData[offset + i].color = {attrib.colors[3 * idx.vertex_index + 0],
                                       attrib.colors[3 * idx.vertex_index + 1],
                                       attrib.colors[3 * idx.vertex_index + 2]};

      _vertexData[offset + i].uv = {
          attrib.texcoords[2 * idx.texcoord_index + 0],
          1 - attrib.texcoords[2 * idx.texcoord_index + 1]};
    }
  }

  _createVertexBuffer();
  return 0;
}

int ZMesh::render(RenderPassEncoder &rRenderPassEncoder) {
  rRenderPassEncoder.setVertexBuffer(
      0, _vertexBuffer, 0, _vertexData.size() * sizeof(VertexAttributes));
  rRenderPassEncoder.draw(_vertexData.size(), 1, 0, 0);

  return 0;
}

int ZMesh::_createVertexBuffer() {
  // Create vertex buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = _vertexData.size() * sizeof(VertexAttributes);
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
  bufferDesc.mappedAtCreation = false;
  _vertexBuffer = _rDevice.createBuffer(bufferDesc);
  _rQueue.writeBuffer(_vertexBuffer, 0, _vertexData.data(), bufferDesc.size);

  return 0;
}