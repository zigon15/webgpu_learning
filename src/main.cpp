#include <webgpu/webgpu.h>
#include <iostream>

int main (int, char**) {
  WGPUInstanceDescriptor desc = {};
  desc.nextInChain = nullptr;

  // We create the instance using this descriptor
  WGPUInstance instance = wgpuCreateInstance(&desc);

  // We can check whether there is actually an instance created
  if (!instance) {
    std::cerr << "@ERROR Could not initialize WebGPU!" << std::endl;
    return 1;
  }else{
    std::cout << "@INFO Initalized WebGPU" << std::endl;
  }

  // Display the object (WGPUInstance is a simple pointer, it may be
  // copied around without worrying about its size).
  std::cout << "WGPU instance: " << instance << std::endl;

  return 0;
}