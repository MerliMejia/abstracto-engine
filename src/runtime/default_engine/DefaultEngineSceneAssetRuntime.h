#pragma once

#include "../DefaultEngineConfig.h"
#include "assets/RenderableModel.h"
#include "backend/VulkanBackend.h"
#include "DefaultEngineTerrainRuntime.h"
#include "editor/DebugUIState.h"
#include "lighting/ImageBasedLightingTypes.h"
#include "resources/FrameGeometryUniforms.h"
#include "resources/Sampler.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

struct DefaultEngineSceneAssetRuntimeContext {
  const DefaultEngineConfig &engineConfig;
  SceneDefinition &sceneDefinition;
  std::vector<SceneAssetInstance> &sceneAssets;
  std::vector<RenderableModel> &sceneAssetModels;
  std::vector<DefaultEngineTerrainPaintState> &terrainPaintStates;
  DefaultDebugUISettings &debugUiSettings;
  VulkanBackend &backend;
  FrameGeometryUniforms &frameGeometryUniforms;
  Sampler &sampler;
};

class DefaultEngineSceneAssetRuntime {
public:
  static std::vector<SceneAssetInstance>
  resolvedSceneAssets(const SceneDefinition &sceneDefinition) {
    if (!sceneDefinition.assets.empty()) {
      return sceneDefinition.assets;
    }
    if (!sceneDefinition.modelPath.empty()) {
      return {SceneAssetInstance::fromAsset(sceneDefinition.modelPath)};
    }
    return {};
  }

  static void applyEngineConfigOverrides(const DefaultEngineConfig &engineConfig,
                                         DefaultDebugUISettings &settings) {
    if (engineConfig.skyboxVisible.has_value()) {
      settings.skyboxVisible = *engineConfig.skyboxVisible;
    }
  }

  static DefaultDebugUISettings
  buildBaseDebugUiSettings(const DefaultEngineConfig &engineConfig,
                           const SceneDefinition &sceneDefinition) {
    DefaultDebugUISettings settings;
    const std::vector<SceneAssetInstance> initialSceneAssets =
        resolvedSceneAssets(sceneDefinition);
    if (initialSceneAssets.empty()) {
      settings.sceneObjects.clear();
    } else {
      settings.sceneObjects.clear();
      settings.sceneObjects.reserve(initialSceneAssets.size());
      for (size_t index = 0; index < initialSceneAssets.size(); ++index) {
        const auto &sceneAsset = initialSceneAssets[index];
        settings.sceneObjects.push_back(SceneObject{
            .name = AppSceneController::sceneAssetName(sceneAsset, index),
            .transform = sceneAsset.transform,
            .visible = sceneAsset.visible,
        });
      }
    }
    settings.sceneLights = sceneDefinition.sceneLights;
    settings.iblBakeSettings.environmentHdrPath =
        resolvedDefaultEnvironmentHdrPath(engineConfig);
    if (engineConfig.configureSettings) {
      engineConfig.configureSettings(settings);
    }
    if (sceneDefinition.configureSettings) {
      sceneDefinition.configureSettings(settings);
    }
    applyEngineConfigOverrides(engineConfig, settings);
    clampSceneObjectSelection(settings);
    return settings;
  }

  static void syncSceneObjectsWithAssets(
      DefaultEngineSceneAssetRuntimeContext &context) {
    AppSceneController::syncSceneObjectsWithAssets(context.debugUiSettings,
                                                   context.sceneAssets);
    AppSceneController::applyObjectOverrides(
        context.debugUiSettings, context.sceneDefinition.objectOverrides);
  }

  static void commitSceneAssetsFromSettings(
      DefaultEngineSceneAssetRuntimeContext &context,
      const std::function<bool(size_t)> &snapCharacterControllerToTerrain) {
    const size_t sceneObjectCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    for (size_t index = 0; index < sceneObjectCount; ++index) {
      context.sceneAssets[index].transform =
          context.debugUiSettings.sceneObjects[index].transform;
      context.sceneAssets[index].visible =
          context.debugUiSettings.sceneObjects[index].visible;
    }
    for (size_t index = 0; index < sceneObjectCount; ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::CharacterController) {
        continue;
      }
      context.sceneAssets[index].characterControllerState.position =
          context.sceneAssets[index].transform.position;
      context.sceneAssets[index].characterControllerState.yawRadians =
          glm::radians(context.sceneAssets[index].transform.rotationDegrees.y);
      snapCharacterControllerToTerrain(index);
    }
    DefaultEngineTerrainRuntime::syncTerrainMaterialOverridesInto(
        context.sceneAssets, context.sceneAssetModels);
    context.sceneDefinition.assets = context.sceneAssets;
  }

  static std::vector<SceneAssetInstance>
  persistedSceneAssets(const DefaultEngineSceneAssetRuntimeContext &context) {
    std::vector<SceneAssetInstance> persisted =
        !context.sceneAssets.empty() ? context.sceneAssets
                                     : context.sceneDefinition.assets;
    DefaultEngineTerrainRuntime::syncTerrainMaterialOverridesInto(
        persisted, context.sceneAssetModels);
    const size_t sceneObjectCount =
        std::min(persisted.size(), context.debugUiSettings.sceneObjects.size());
    for (size_t index = 0; index < sceneObjectCount; ++index) {
      persisted[index].transform =
          context.debugUiSettings.sceneObjects[index].transform;
      persisted[index].visible =
          context.debugUiSettings.sceneObjects[index].visible;
      if (persisted[index].kind == SceneAssetKind::CharacterController) {
        persisted[index].characterControllerState.position =
            persisted[index].transform.position;
        persisted[index].characterControllerState.yawRadians =
            glm::radians(persisted[index].transform.rotationDegrees.y);
      }
    }
    return persisted;
  }

  static void loadSceneAssetModel(
      DefaultEngineSceneAssetRuntimeContext &context, size_t index,
      const vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout,
      const vk::raii::DescriptorSetLayout *sceneSecondaryDescriptorSetLayout) {
    if (index >= context.sceneAssets.size() ||
        index >= context.sceneAssetModels.size()) {
      return;
    }

    SceneAssetInstance &sceneAsset = context.sceneAssets[index];
    if (sceneAsset.kind == SceneAssetKind::Terrain) {
      auto terrainContext = buildTerrainContext(context);
      DefaultEngineTerrainPaintState &paintState =
          DefaultEngineTerrainRuntime::ensureTerrainPaintState(terrainContext,
                                                               index);
      std::vector<ImportedMaterialData> existingMaterials;
      if (context.sceneAssetModels[index].modelAsset() != nullptr) {
        existingMaterials = context.sceneAssetModels[index].materials();
      }
      context.sceneAssetModels[index].loadTerrain(
          sceneAsset.terrainConfig,
          AppSceneController::sceneAssetName(sceneAsset, index),
          context.backend.commands(), context.backend.device(),
          sceneDescriptorSetLayout, sceneSecondaryDescriptorSetLayout,
          context.frameGeometryUniforms, context.sampler,
          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT,
          [&sceneAsset, &paintState, existingMaterials = std::move(existingMaterials)](
              std::vector<ImportedMaterialData> &materials) {
            if (materials.empty()) {
              return;
            }
            if (!existingMaterials.empty()) {
              const size_t materialCount =
                  std::min(existingMaterials.size(), materials.size());
              for (size_t materialIndex = 0; materialIndex < materialCount;
                   ++materialIndex) {
                materials[materialIndex] = existingMaterials[materialIndex];
              }
            } else if (!sceneAsset.terrainMaterialOverrides.empty()) {
              const size_t materialCount = std::min(
                  sceneAsset.terrainMaterialOverrides.size(), materials.size());
              for (size_t materialIndex = 0; materialIndex < materialCount;
                   ++materialIndex) {
                DefaultEngineTerrainRuntime::applyTerrainMaterialOverride(
                    materials[materialIndex],
                    sceneAsset.terrainMaterialOverrides[materialIndex]);
              }
            }
            DefaultEngineTerrainRuntime::applyTerrainPaintMaterial(
                materials.front(), sceneAsset, paintState);
          });
      paintState.materialDirty = false;
      paintState.lastUploadTime = std::chrono::steady_clock::now();
      return;
    }

    if (sceneAsset.assetPath.empty()) {
      return;
    }
    context.sceneAssetModels[index].loadFromFile(
        sceneAsset.assetPath, context.backend.commands(), context.backend.device(),
        sceneDescriptorSetLayout, sceneSecondaryDescriptorSetLayout,
        context.frameGeometryUniforms, context.sampler,
        DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
    if (context.engineConfig.onSceneAssetLoaded != nullptr &&
        context.sceneAssetModels[index].modelAsset() != nullptr) {
      context.engineConfig.onSceneAssetLoaded(
          sceneAsset, *context.sceneAssetModels[index].modelAsset());
    }
  }

  static void reloadTerrainAsset(
      DefaultEngineSceneAssetRuntimeContext &context, size_t terrainIndex,
      const vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout,
      const vk::raii::DescriptorSetLayout *sceneSecondaryDescriptorSetLayout,
      const std::function<void()> &rebuildSceneRenderItems) {
    if (terrainIndex >= context.sceneAssets.size() ||
        terrainIndex >= context.sceneAssetModels.size()) {
      return;
    }

    context.backend.waitIdle();
    loadSceneAssetModel(context, terrainIndex, sceneDescriptorSetLayout,
                        sceneSecondaryDescriptorSetLayout);
    context.sceneDefinition.assets = context.sceneAssets;
    syncSceneObjectsWithAssets(context);
    rebuildSceneRenderItems();
  }

  static void reloadSceneAssets(
      DefaultEngineSceneAssetRuntimeContext &context,
      const vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout,
      const vk::raii::DescriptorSetLayout *sceneSecondaryDescriptorSetLayout,
      const std::function<void()> &rebuildSceneRenderItems) {
    context.backend.waitIdle();
    std::unordered_map<std::string, DefaultEngineTerrainPaintState>
        previousPaintStates;
    for (size_t index = 0; index < context.sceneAssets.size() &&
                           index < context.terrainPaintStates.size();
         ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::Terrain ||
          context.sceneAssets[index].terrainPaintCanvasPath.empty()) {
        continue;
      }
      previousPaintStates.emplace(context.sceneAssets[index].terrainPaintCanvasPath,
                                  std::move(context.terrainPaintStates[index]));
    }

    context.sceneAssets = resolvedSceneAssets(context.sceneDefinition);
    context.terrainPaintStates.clear();
    context.terrainPaintStates.resize(context.sceneAssets.size());
    for (size_t index = 0; index < context.sceneAssets.size(); ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::Terrain ||
          context.sceneAssets[index].terrainPaintCanvasPath.empty()) {
        continue;
      }
      const auto previousStateIt = previousPaintStates.find(
          context.sceneAssets[index].terrainPaintCanvasPath);
      if (previousStateIt == previousPaintStates.end()) {
        continue;
      }
      context.terrainPaintStates[index] = std::move(previousStateIt->second);
    }

    context.sceneAssetModels.clear();
    context.sceneAssetModels.resize(context.sceneAssets.size());

    for (size_t index = 0; index < context.sceneAssets.size(); ++index) {
      loadSceneAssetModel(context, index, sceneDescriptorSetLayout,
                          sceneSecondaryDescriptorSetLayout);
    }
    syncSceneObjectsWithAssets(context);
    rebuildSceneRenderItems();
  }

private:
  static DefaultEngineTerrainRuntimeContext
  buildTerrainContext(DefaultEngineSceneAssetRuntimeContext &context) {
    static TypedMesh<Vertex> unusedWireframeMesh;
    static TypedMesh<Vertex> unusedBrushMesh;
    static int unusedWireframeIndex = -1;
    static std::optional<TerrainConfig> unusedWireframeConfig;
    static std::optional<DefaultEngineTerrainFlattenStroke> unusedFlattenStroke;
    static AppWindow *unusedWindow = nullptr;

    if (unusedWindow == nullptr) {
      static AppWindow dummyWindow;
      unusedWindow = &dummyWindow;
    }

    return DefaultEngineTerrainRuntimeContext{
        .sceneDefinition = context.sceneDefinition,
        .sceneAssets = context.sceneAssets,
        .sceneAssetModels = context.sceneAssetModels,
        .terrainPaintStates = context.terrainPaintStates,
        .activeTerrainFlattenStroke = unusedFlattenStroke,
        .debugUiSettings = context.debugUiSettings,
        .window = *unusedWindow,
        .backend = context.backend,
        .debugOverlayPass = nullptr,
        .terrainWireframeMesh = unusedWireframeMesh,
        .terrainBrushIndicatorMesh = unusedBrushMesh,
        .activeTerrainWireframeIndex = unusedWireframeIndex,
        .activeTerrainWireframeConfig = unusedWireframeConfig,
    };
  }
};
