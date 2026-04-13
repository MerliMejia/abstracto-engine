#pragma once

#include "../DefaultEngineConfig.h"
#include "backend/VulkanBackend.h"
#include "editor/DebugSessionIO.h"
#include "lighting/ImageBasedLighting.h"
#include "scene/SceneDefinition.h"
#include "core/PassRenderer.h"
#include <filesystem>
#include <functional>
#include <iostream>
#include <vector>

struct DefaultEngineSessionRuntimeContext {
  const DefaultEngineConfig &engineConfig;
  SceneDefinition &sceneDefinition;
  DefaultDebugUISettings &debugUiSettings;
  VulkanBackend &backend;
  ImageBasedLighting &imageBasedLighting;
  PassRenderer &renderer;
};

class DefaultEngineSessionRuntime {
public:
  static void loadDebugSessionFromDisk(
      DefaultEngineSessionRuntimeContext &context,
      const std::filesystem::path &debugSessionPath,
      const std::function<void(DefaultDebugUISettings &)> &applyEngineConfigOverrides,
      const std::function<void()> &ensureDefaultEnvironmentPath) {
    if (!context.engineConfig.enableDebugSessionPersistence) {
      return;
    }

    try {
      std::vector<SceneAssetInstance> loadedSceneAssets;
      DebugSessionIO::loadDebugSession(debugSessionPath, context.debugUiSettings,
                                       &loadedSceneAssets);
      if (!loadedSceneAssets.empty()) {
        context.sceneDefinition.assets = std::move(loadedSceneAssets);
      }
      applyEngineConfigOverrides(context.debugUiSettings);
      ensureDefaultEnvironmentPath();
    } catch (const std::exception &e) {
      std::cerr << "Failed to load debug session: " << e.what() << std::endl;
    }
  }

  static void saveDebugSessionToDisk(
      DefaultEngineSessionRuntimeContext &context,
      const std::filesystem::path &debugSessionPath,
      const std::function<void()> &flushTerrainPaintMaterials,
      const std::function<void()> &saveTerrainPaintCanvasesToDisk,
      const std::function<std::vector<SceneAssetInstance>()> &persistedSceneAssets) {
    if (!context.engineConfig.enableDebugSessionPersistence) {
      return;
    }

    flushTerrainPaintMaterials();
    saveTerrainPaintCanvasesToDisk();
    if (!DebugSessionIO::saveDebugSession(debugSessionPath,
                                          context.debugUiSettings,
                                          persistedSceneAssets())) {
      std::cerr << "Failed to save debug session to " << debugSessionPath.string()
                << std::endl;
    }
  }

  static void applyLoadedDebugSettings(
      DefaultEngineSessionRuntimeContext &context,
      const std::function<void()> &ensureDefaultEnvironmentPath,
      const std::function<void()> &syncProceduralSkySunWithLight,
      const std::function<void()> &reloadSceneAssets) {
    ensureDefaultEnvironmentPath();
    context.backend.waitIdle();
    if (context.debugUiSettings.syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    context.imageBasedLighting.rebuild(context.backend.device(),
                                       context.backend.commands(),
                                       context.debugUiSettings.iblBakeSettings);
    context.renderer.recreate(context.backend.device(), context.backend.swapchain());
    reloadSceneAssets();
  }
};
