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

struct TerrainMaterialOverride {
  std::string name;
  glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
  float metallicFactor = 0.0f;
  float roughnessFactor = 1.0f;
  float occlusionStrength = 1.0f;
};

struct SceneAssetInstance {
  SceneAssetKind kind = SceneAssetKind::File;
  std::string assetPath;
  std::string name;
  SceneTransform transform{};
  bool visible = true;
  TerrainConfig terrainConfig{};
  bool terrainWireframeVisible = false;
  bool terrainEditMode = false;
  float terrainBrushRadius = 1.5f;
  bool terrainBrushLowerMode = false;
  bool terrainBrushFlattenMode = false;
  bool terrainBrushColorPaintMode = false;
  glm::vec4 terrainBrushColor{0.38f, 0.62f, 0.24f, 1.0f};
  bool terrainBrushTexturePaintMode = false;
  float terrainBrushOpacity = 1.0f;
  float terrainBrushTextureVariation = 0.0f;
  uint32_t terrainPaintCanvasResolution = 1024;
  std::string terrainPaintCanvasPath;
  std::string terrainBrushTexturePath = "assets/textures/viking_room.png";
  std::vector<TerrainMaterialOverride> terrainMaterialOverrides;

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
