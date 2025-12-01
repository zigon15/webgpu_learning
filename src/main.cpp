#include "Application.hpp"

int main(int, char **) {
  Application app;

  if (!app.Initialize(640, 480)) {
    return 1;
  }

  // Warning: this is still not Emscripten-friendly, see below
  while (app.IsRunning()) {
    app.MainLoop();
  }

  app.Terminate();
  return 0;

  // for (int i = 0; i < 5; ++i) {
  //   std::cout << "Tick/Poll device..." << std::endl;
  //   wgpuDeviceTick(device);
  // }

  return 0;
}