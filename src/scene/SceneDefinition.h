#pragma once

#include "SceneLightSet.h"
#include "SceneTypes.h"
#include "editor/DebugUIState.h"
#include "world/Terrain.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class SceneAssetKind {
  File = 0,
  Terrain = 1,
  CharacterController = 2,
  Camera = 3,
  InstancedObject = 4,
};

struct CharacterControllerConfig {
  float radius = 0.35f;
  float halfHeight = 0.55f;
  float moveSpeed = 3.5f;
  float jumpSpeed = 4.5f;
  float gravity = 18.0f;
  float maxSlopeDegrees = 45.0f;
  bool alignToGroundNormal = true;
  bool cameraFollow = true;
  std::string visualAssetPath;
};

struct CharacterControllerState {
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  float yawRadians = 0.0f;
  bool grounded = false;
};

struct SceneCameraConfig {
  float fieldOfViewDegrees = 45.0f;
  float farPlane = 100.0f;
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
  bool renderPrimaryTransform = true;
  std::vector<SceneTransform> instanceTransforms;
  std::string targetTerrainName;
  float instanceSpacing = 1.0f;
  float instanceJitter = 0.35f;
  glm::vec2 instanceScaleRange{1.0f, 1.0f};
  glm::vec2 instanceScaleVerticalRange{1.0f, 1.0f};
  bool instanceAlignToTerrainNormal = true;
  bool instanceRandomYaw = true;
  float instanceYawRangeDegrees = 360.0f;
  float instancePitchRangeDegrees = 0.0f;
  float instanceRollRangeDegrees = 0.0f;
  float instanceMaxSlopeDegrees = 45.0f;
  float instanceHeightOffset = 0.0f;
  float instanceHeightJitter = 0.0f;
  uint32_t instanceScatterSeed = 1;
  bool instancePaintMode = false;
  bool instanceEraseMode = false;
  float instanceBrushRadius = 2.0f;
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
  CharacterControllerConfig characterControllerConfig{};
  CharacterControllerState characterControllerState{};
  SceneCameraConfig cameraConfig{};

  static SceneAssetInstance fromAsset(std::string assetPathValue) {
    return SceneAssetInstance{
        .kind = SceneAssetKind::File,
        .assetPath = std::move(assetPathValue),
    };
  }

  static SceneAssetInstance
  makeInstancedObject(std::string nameValue = "Instanced Object") {
    return SceneAssetInstance{
        .kind = SceneAssetKind::InstancedObject,
        .name = std::move(nameValue),
        .renderPrimaryTransform = false,
        .instanceSpacing = 1.0f,
        .instanceJitter = 0.35f,
        .instanceScaleRange = {0.9f, 1.1f},
        .instanceScaleVerticalRange = {0.9f, 1.1f},
        .instanceAlignToTerrainNormal = true,
        .instanceRandomYaw = true,
        .instanceYawRangeDegrees = 360.0f,
        .instancePitchRangeDegrees = 0.0f,
        .instanceRollRangeDegrees = 0.0f,
        .instanceMaxSlopeDegrees = 45.0f,
        .instanceHeightOffset = 0.0f,
        .instanceHeightJitter = 0.0f,
        .instanceScatterSeed = 1,
        .instancePaintMode = false,
        .instanceEraseMode = false,
        .instanceBrushRadius = 2.0f,
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

  static SceneAssetInstance
  makeCharacterController(std::string nameValue = "Character Controller") {
    SceneAssetInstance sceneAsset{
        .kind = SceneAssetKind::CharacterController,
        .name = std::move(nameValue),
    };
    sceneAsset.transform.position = sceneAsset.characterControllerState.position;
    return sceneAsset;
  }

  static SceneAssetInstance makeCamera(
      SceneTransform transformValue = {}, SceneCameraConfig cameraConfigValue = {},
      std::string nameValue = "Camera") {
    return SceneAssetInstance{
        .kind = SceneAssetKind::Camera,
        .name = std::move(nameValue),
        .transform = std::move(transformValue),
        .cameraConfig = std::move(cameraConfigValue),
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
