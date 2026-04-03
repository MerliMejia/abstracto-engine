#pragma once

#include "scene/SceneLightSet.h"
#include "scene/SceneTypes.h"
#include "engine/editor/DebugUIState.h"
#include "world/Terrain.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class SceneAssetKind {
  File = 0,
  Terrain = 1,
};

struct SceneObjectOverride {
  std::string name;
  SceneTransform transform{};
  bool overrideTransform = false;
  bool visible = true;
  bool overrideVisibility = false;
};

struct SceneAssetInstance {
  SceneAssetKind kind = SceneAssetKind::File;
  std::string assetPath;
  std::string name;
  SceneTransform transform{};
  bool visible = true;
  TerrainConfig terrainConfig{};
  bool terrainWireframeVisible = false;

  static SceneAssetInstance fromAsset(std::string assetPathValue) {
    return SceneAssetInstance{
        .kind = SceneAssetKind::File,
        .assetPath = std::move(assetPathValue),
    };
  }

  static SceneAssetInstance makeTerrain(TerrainConfig config,
                                        std::string nameValue = "Terrain") {
    return SceneAssetInstance{
        .kind = SceneAssetKind::Terrain,
        .name = std::move(nameValue),
        .terrainConfig = std::move(config),
    };
  }
};

struct SceneDefinition {
  std::vector<SceneAssetInstance> assets;
  std::string modelPath = "assets/models/night.glb";
  SceneLightSet sceneLights = SceneLightSet::showcaseLights();
  std::vector<SceneObjectOverride> objectOverrides;
  std::function<void(DefaultDebugUISettings &)> configureSettings;

  static SceneDefinition fromModel(const std::string &modelPathValue) {
    SceneDefinition definition;
    definition.assets.push_back(SceneAssetInstance::fromAsset(modelPathValue));
    definition.modelPath = modelPathValue;
    return definition;
  }

  static SceneDefinition empty() {
    SceneDefinition definition;
    definition.assets.clear();
    definition.modelPath.clear();
    definition.sceneLights.clear();
    return definition;
  }
};
