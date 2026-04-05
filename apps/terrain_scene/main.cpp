#include "engine/runtime/DefaultEngineApp.h"
#include <cstdlib>
#include <exception>
#include <glm/glm.hpp>
#include <iostream>

int main() {
  try {
    DefaultEngineConfig engineConfig{};
    engineConfig.windowTitle = "Terrain Scene";
    engineConfig.enableDebugSessionPersistence = true;
    engineConfig.restoreSessionOnStartup = true;
    engineConfig.debugSessionPath = "assets/debug/terrain_scene.json";
    engineConfig.defaultEnvironmentHdrPath = "assets/textures/autumn_field_puresky_4k.hdr";
    engineConfig.skyboxVisible = true;
    engineConfig.debugUiVisibleOnStartup = true;

    SceneDefinition scene = SceneDefinition::empty();
    scene.assets.push_back(SceneAssetInstance::makeTerrain(
        TerrainConfig{
            .sizeX = 18.0f,
            .sizeZ = 18.0f,
            .xSegments = 32,
            .zSegments = 32,
            .uvScale = {6.0f, 6.0f},
            .heightScale = 0.0f,
            .noiseFrequency = 0.2f,
            .noiseOctaves = 4,
            .noisePersistence = 0.5f,
            .noiseLacunarity = 2.0f,
            .noiseSeed = 7,
        },
        "Terrain"));

    scene.configureSettings = [](DefaultDebugUISettings &settings) {
      settings.cameraPosition = {0.0f, 6.0f, 12.0f};
      settings.cameraYawRadians = glm::radians(180.0f);
      settings.cameraPitchRadians = glm::radians(-24.0f);
      settings.cameraFarPlane = 160.0f;
      settings.iblEnabled = true;
      settings.skyboxVisible = true;
      settings.lightMarkersVisible = false;
    };

    DefaultEngineApp::create(engineConfig, scene).run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
