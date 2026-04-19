#pragma once

#include "DebugUIState.h"
#include "scene/SceneDefinition.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json.hpp>

class DebugSessionIO {
public:

using json = nlohmann::json;

static inline json vec3ToJson(const glm::vec3 &value) {
  return json::array({value.x, value.y, value.z});
}

static inline glm::vec3
vec3FromJson(const json &value,
             const glm::vec3 &fallback = glm::vec3(0.0f)) {
  if (!value.is_array() || value.size() != 3) {
    return fallback;
  }
  return glm::vec3(value[0].get<float>(), value[1].get<float>(),
                   value[2].get<float>());
}

static inline json vec4ToJson(const glm::vec4 &value) {
  return json::array({value.x, value.y, value.z, value.w});
}

static inline glm::vec4
vec4FromJson(const json &value,
             const glm::vec4 &fallback = glm::vec4(0.0f)) {
  if (!value.is_array() || value.size() != 4) {
    return fallback;
  }
  return glm::vec4(value[0].get<float>(), value[1].get<float>(),
                   value[2].get<float>(), value[3].get<float>());
}

static inline json sceneTransformToJson(const SceneTransform &transform) {
  return {
      {"position", vec3ToJson(transform.position)},
      {"rotationDegrees", vec3ToJson(transform.rotationDegrees)},
      {"scale", vec3ToJson(transform.scale)},
  };
}

static inline SceneTransform
sceneTransformFromJson(const json &value,
                       const SceneTransform &fallback = SceneTransform{}) {
  SceneTransform transform = fallback;
  transform.position =
      vec3FromJson(value.value("position", json::array()), transform.position);
  transform.rotationDegrees = vec3FromJson(
      value.value("rotationDegrees", json::array()), transform.rotationDegrees);
  transform.scale =
      vec3FromJson(value.value("scale", json::array()), transform.scale);
  return transform;
}

static inline json sceneObjectToJson(const SceneObject &object) {
  json value = sceneTransformToJson(object.transform);
  value["name"] = object.name;
  value["visible"] = object.visible;
  return value;
}

static inline SceneObject sceneObjectFromJson(const json &value) {
  SceneObject object;
  object.name = value.value("name", object.name);
  object.visible = value.value("visible", object.visible);
  object.transform = sceneTransformFromJson(value, object.transform);
  return object;
}

static inline json terrainConfigToJson(const TerrainConfig &config) {
  json heightOffsets = json::array();
  for (const float offset : config.heightOffsets) {
    heightOffsets.push_back(offset);
  }
  json vertexColors = json::array();
  for (const glm::vec4 &color : config.vertexColors) {
    vertexColors.push_back(vec4ToJson(color));
  }
  return {
      {"sizeX", config.sizeX},
      {"sizeZ", config.sizeZ},
      {"xSegments", config.xSegments},
      {"zSegments", config.zSegments},
      {"uvScale", json::array({config.uvScale.x, config.uvScale.y})},
      {"heightScale", config.heightScale},
      {"noiseFrequency", config.noiseFrequency},
      {"noiseOctaves", config.noiseOctaves},
      {"noisePersistence", config.noisePersistence},
      {"noiseLacunarity", config.noiseLacunarity},
      {"noiseSeed", config.noiseSeed},
      {"heightOffsets", std::move(heightOffsets)},
      {"vertexColors", std::move(vertexColors)},
  };
}

static inline TerrainConfig terrainConfigFromJson(const json &value) {
  TerrainConfig config;
  config.sizeX = value.value("sizeX", config.sizeX);
  config.sizeZ = value.value("sizeZ", config.sizeZ);
  config.xSegments = value.value("xSegments", config.xSegments);
  config.zSegments = value.value("zSegments", config.zSegments);
  if (value.contains("uvScale") && value["uvScale"].is_array() &&
      value["uvScale"].size() == 2) {
    config.uvScale =
        glm::vec2(value["uvScale"][0].get<float>(),
                  value["uvScale"][1].get<float>());
  }
  config.heightScale = value.value("heightScale", config.heightScale);
  config.noiseFrequency =
      value.value("noiseFrequency", config.noiseFrequency);
  config.noiseOctaves = value.value("noiseOctaves", config.noiseOctaves);
  config.noisePersistence =
      value.value("noisePersistence", config.noisePersistence);
  config.noiseLacunarity =
      value.value("noiseLacunarity", config.noiseLacunarity);
  config.noiseSeed = value.value("noiseSeed", config.noiseSeed);
  if (value.contains("heightOffsets") && value["heightOffsets"].is_array()) {
    config.heightOffsets.clear();
    config.heightOffsets.reserve(value["heightOffsets"].size());
    for (const auto &offsetValue : value["heightOffsets"]) {
      config.heightOffsets.push_back(offsetValue.get<float>());
    }
  }
  if (value.contains("vertexColors") && value["vertexColors"].is_array()) {
    config.vertexColors.clear();
    config.vertexColors.reserve(value["vertexColors"].size());
    for (const auto &colorValue : value["vertexColors"]) {
      config.vertexColors.push_back(
          vec4FromJson(colorValue, glm::vec4(1.0f)));
    }
  }
  return config;
}

static inline json
terrainMaterialOverrideToJson(const TerrainMaterialOverride &materialOverride) {
  return {
      {"name", materialOverride.name},
      {"baseColorFactor", vec4ToJson(materialOverride.baseColorFactor)},
      {"emissiveFactor", vec3ToJson(materialOverride.emissiveFactor)},
      {"metallicFactor", materialOverride.metallicFactor},
      {"roughnessFactor", materialOverride.roughnessFactor},
      {"occlusionStrength", materialOverride.occlusionStrength},
  };
}

static inline TerrainMaterialOverride
terrainMaterialOverrideFromJson(const json &value) {
  TerrainMaterialOverride materialOverride;
  materialOverride.name = value.value("name", materialOverride.name);
  materialOverride.baseColorFactor = vec4FromJson(
      value.value("baseColorFactor", json::array()),
      materialOverride.baseColorFactor);
  materialOverride.emissiveFactor = vec3FromJson(
      value.value("emissiveFactor", json::array()),
      materialOverride.emissiveFactor);
  materialOverride.metallicFactor = glm::clamp(
      value.value("metallicFactor", materialOverride.metallicFactor), 0.0f,
      1.0f);
  materialOverride.roughnessFactor = glm::clamp(
      value.value("roughnessFactor", materialOverride.roughnessFactor), 0.0f,
      1.0f);
  materialOverride.occlusionStrength = glm::clamp(
      value.value("occlusionStrength", materialOverride.occlusionStrength), 0.0f,
      1.0f);
  return materialOverride;
}

static inline json
characterControllerConfigToJson(const CharacterControllerConfig &config) {
  return {
      {"radius", config.radius},
      {"halfHeight", config.halfHeight},
      {"moveSpeed", config.moveSpeed},
      {"jumpSpeed", config.jumpSpeed},
      {"gravity", config.gravity},
      {"maxSlopeDegrees", config.maxSlopeDegrees},
      {"alignToGroundNormal", config.alignToGroundNormal},
      {"cameraFollow", config.cameraFollow},
      {"visualAssetPath", config.visualAssetPath},
  };
}

static inline CharacterControllerConfig
characterControllerConfigFromJson(const json &value) {
  CharacterControllerConfig config;
  config.radius = std::max(value.value("radius", config.radius), 0.01f);
  config.halfHeight =
      std::max(value.value("halfHeight", config.halfHeight), 0.01f);
  config.moveSpeed = std::max(value.value("moveSpeed", config.moveSpeed), 0.0f);
  config.jumpSpeed = value.value("jumpSpeed", config.jumpSpeed);
  config.gravity = std::max(value.value("gravity", config.gravity), 0.0f);
  config.maxSlopeDegrees = glm::clamp(
      value.value("maxSlopeDegrees", config.maxSlopeDegrees), 0.0f, 89.0f);
  config.alignToGroundNormal = value.value("alignToGroundNormal",
                                           config.alignToGroundNormal);
  config.cameraFollow = value.value("cameraFollow", config.cameraFollow);
  config.visualAssetPath =
      value.value("visualAssetPath", config.visualAssetPath);
  return config;
}

static inline json
characterControllerStateToJson(const CharacterControllerState &state) {
  return {
      {"position", vec3ToJson(state.position)},
      {"velocity", vec3ToJson(state.velocity)},
      {"yawRadians", state.yawRadians},
      {"grounded", state.grounded},
  };
}

static inline CharacterControllerState
characterControllerStateFromJson(const json &value) {
  CharacterControllerState state;
  state.position =
      vec3FromJson(value.value("position", json::array()), state.position);
  state.velocity =
      vec3FromJson(value.value("velocity", json::array()), state.velocity);
  state.yawRadians = value.value("yawRadians", state.yawRadians);
  state.grounded = value.value("grounded", state.grounded);
  return state;
}

static inline json cameraConfigToJson(const SceneCameraConfig &config) {
  return {
      {"fieldOfViewDegrees", config.fieldOfViewDegrees},
      {"farPlane", config.farPlane},
      {"free", config.free},
  };
}

static inline SceneCameraConfig cameraConfigFromJson(const json &value) {
  SceneCameraConfig config;
  config.fieldOfViewDegrees = glm::clamp(
      value.value("fieldOfViewDegrees", config.fieldOfViewDegrees), 10.0f,
      120.0f);
  config.farPlane = std::max(value.value("farPlane", config.farPlane), 1.0f);
  config.free = value.value("free", config.free);
  return config;
}

static inline json terrainGrassConfigToJson(const TerrainGrassConfig &config) {
  return {
      {"density", config.density},
      {"placementJitter", config.placementJitter},
      {"chunkSize", config.chunkSize},
      {"drawDistance", config.drawDistance},
      {"nearDistance", config.nearDistance},
      {"midDensityScale", config.midDensityScale},
      {"maxSlopeDegrees", config.maxSlopeDegrees},
      {"clumpRadius", config.clumpRadius},
      {"bladeHeightRange",
       json::array({config.bladeHeightRange.x, config.bladeHeightRange.y})},
      {"bladeWidthRange",
       json::array({config.bladeWidthRange.x, config.bladeWidthRange.y})},
      {"bladesPerClump", config.bladesPerClump},
      {"midBladesPerClump", config.midBladesPerClump},
      {"scatterSeed", config.scatterSeed},
      {"randomLeanDegrees", config.randomLeanDegrees},
  };
}

static inline TerrainGrassConfig terrainGrassConfigFromJson(const json &value) {
  TerrainGrassConfig config;
  config.density = std::max(value.value("density", config.density), 0.0f);
  config.placementJitter = glm::clamp(
      value.value("placementJitter", config.placementJitter), 0.0f, 1.0f);
  config.chunkSize = std::max(value.value("chunkSize", config.chunkSize), 1.0f);
  config.drawDistance =
      std::max(value.value("drawDistance", config.drawDistance), 0.0f);
  config.nearDistance = glm::clamp(
      value.value("nearDistance", config.nearDistance), 0.0f,
      config.drawDistance);
  config.midDensityScale = glm::clamp(
      value.value("midDensityScale", config.midDensityScale), 0.0f, 1.0f);
  config.maxSlopeDegrees = glm::clamp(
      value.value("maxSlopeDegrees", config.maxSlopeDegrees), 0.0f, 89.9f);
  config.clumpRadius =
      std::max(value.value("clumpRadius", config.clumpRadius), 0.0f);
  if (value.contains("bladeHeightRange") &&
      value["bladeHeightRange"].is_array() &&
      value["bladeHeightRange"].size() == 2) {
    config.bladeHeightRange = glm::vec2(
        value["bladeHeightRange"][0].get<float>(),
        value["bladeHeightRange"][1].get<float>());
  }
  if (value.contains("bladeWidthRange") &&
      value["bladeWidthRange"].is_array() &&
      value["bladeWidthRange"].size() == 2) {
    config.bladeWidthRange = glm::vec2(
        value["bladeWidthRange"][0].get<float>(),
        value["bladeWidthRange"][1].get<float>());
  }
  config.bladeHeightRange.x = std::max(config.bladeHeightRange.x, 0.01f);
  config.bladeHeightRange.y =
      std::max(config.bladeHeightRange.y, config.bladeHeightRange.x);
  config.bladeWidthRange.x = std::max(config.bladeWidthRange.x, 0.001f);
  config.bladeWidthRange.y =
      std::max(config.bladeWidthRange.y, config.bladeWidthRange.x);
  config.bladesPerClump = std::max(
      value.value("bladesPerClump", config.bladesPerClump), 1u);
  config.midBladesPerClump = std::max(
      value.value("midBladesPerClump", config.midBladesPerClump), 1u);
  config.midBladesPerClump =
      std::min(config.midBladesPerClump, config.bladesPerClump);
  config.scatterSeed = value.value("scatterSeed", config.scatterSeed);
  config.randomLeanDegrees = glm::clamp(
      value.value("randomLeanDegrees", config.randomLeanDegrees), 0.0f, 80.0f);
  return config;
}

static inline json sceneAssetToJson(const SceneAssetInstance &sceneAsset) {
  json value = sceneTransformToJson(sceneAsset.transform);
  value["kind"] = static_cast<uint32_t>(sceneAsset.kind);
  value["assetPath"] = sceneAsset.assetPath;
  value["name"] = sceneAsset.name;
  value["visible"] = sceneAsset.visible;
  value["renderPrimaryTransform"] = sceneAsset.renderPrimaryTransform;
  value["terrainWireframeVisible"] = sceneAsset.terrainWireframeVisible;
  value["terrainEditMode"] = sceneAsset.terrainEditMode;
  value["terrainBrushRadius"] = sceneAsset.terrainBrushRadius;
  value["terrainBrushLowerMode"] = sceneAsset.terrainBrushLowerMode;
  value["terrainBrushFlattenMode"] = sceneAsset.terrainBrushFlattenMode;
  value["terrainBrushColorPaintMode"] = sceneAsset.terrainBrushColorPaintMode;
  value["terrainBrushColor"] = vec4ToJson(sceneAsset.terrainBrushColor);
  value["terrainBrushTexturePaintMode"] =
      sceneAsset.terrainBrushTexturePaintMode;
  value["terrainBrushOpacity"] = sceneAsset.terrainBrushOpacity;
  value["terrainBrushTextureVariation"] =
      sceneAsset.terrainBrushTextureVariation;
  value["terrainPaintCanvasResolution"] =
      sceneAsset.terrainPaintCanvasResolution;
  value["terrainPaintCanvasPath"] = sceneAsset.terrainPaintCanvasPath;
  value["terrainBrushTexturePath"] = sceneAsset.terrainBrushTexturePath;
  if (!sceneAsset.instanceTransforms.empty()) {
    json instanceTransforms = json::array();
    for (const auto &instanceTransform : sceneAsset.instanceTransforms) {
      instanceTransforms.push_back(sceneTransformToJson(instanceTransform));
    }
    value["instanceTransforms"] = std::move(instanceTransforms);
  }
  if (!sceneAsset.targetTerrainName.empty()) {
    value["targetTerrainName"] = sceneAsset.targetTerrainName;
  }
  value["instanceSpacing"] = sceneAsset.instanceSpacing;
  value["instanceJitter"] = sceneAsset.instanceJitter;
  value["instanceScaleRange"] = json::array(
      {sceneAsset.instanceScaleRange.x, sceneAsset.instanceScaleRange.y});
  value["instanceScaleVerticalRange"] = json::array(
      {sceneAsset.instanceScaleVerticalRange.x,
       sceneAsset.instanceScaleVerticalRange.y});
  value["instanceAlignToTerrainNormal"] =
      sceneAsset.instanceAlignToTerrainNormal;
  value["instanceRandomYaw"] = sceneAsset.instanceRandomYaw;
  value["instanceYawRangeDegrees"] = sceneAsset.instanceYawRangeDegrees;
  value["instancePitchRangeDegrees"] = sceneAsset.instancePitchRangeDegrees;
  value["instanceRollRangeDegrees"] = sceneAsset.instanceRollRangeDegrees;
  value["instanceMaxSlopeDegrees"] = sceneAsset.instanceMaxSlopeDegrees;
  value["instanceHeightOffset"] = sceneAsset.instanceHeightOffset;
  value["instanceHeightJitter"] = sceneAsset.instanceHeightJitter;
  value["instanceScatterSeed"] = sceneAsset.instanceScatterSeed;
  value["instancePaintMode"] = sceneAsset.instancePaintMode;
  value["instanceEraseMode"] = sceneAsset.instanceEraseMode;
  value["instanceBrushRadius"] = sceneAsset.instanceBrushRadius;
  if (sceneAsset.kind == SceneAssetKind::Terrain) {
    value["terrainConfig"] = terrainConfigToJson(sceneAsset.terrainConfig);
    if (!sceneAsset.terrainMaterialOverrides.empty()) {
      json materialOverrides = json::array();
      for (const auto &materialOverride : sceneAsset.terrainMaterialOverrides) {
        materialOverrides.push_back(
            terrainMaterialOverrideToJson(materialOverride));
      }
      value["terrainMaterialOverrides"] = std::move(materialOverrides);
    }
  } else if (sceneAsset.kind == SceneAssetKind::CharacterController) {
    value["characterControllerConfig"] =
        characterControllerConfigToJson(sceneAsset.characterControllerConfig);
    value["characterControllerState"] =
        characterControllerStateToJson(sceneAsset.characterControllerState);
  } else if (sceneAsset.kind == SceneAssetKind::Camera) {
    value["cameraConfig"] = cameraConfigToJson(sceneAsset.cameraConfig);
  } else if (sceneAsset.kind == SceneAssetKind::TerrainGrass) {
    value["terrainGrassConfig"] =
        terrainGrassConfigToJson(sceneAsset.terrainGrassConfig);
  }
  return value;
}

static inline SceneAssetInstance sceneAssetFromJson(const json &value) {
  SceneAssetInstance sceneAsset;
  sceneAsset.kind = static_cast<SceneAssetKind>(
      value.value("kind", static_cast<uint32_t>(sceneAsset.kind)));
  sceneAsset.assetPath = value.value("assetPath", sceneAsset.assetPath);
  sceneAsset.name = value.value("name", sceneAsset.name);
  sceneAsset.visible = value.value("visible", sceneAsset.visible);
  sceneAsset.renderPrimaryTransform =
      value.value("renderPrimaryTransform", sceneAsset.renderPrimaryTransform);
  sceneAsset.transform = sceneTransformFromJson(value, sceneAsset.transform);
  if (value.contains("instanceTransforms") &&
      value["instanceTransforms"].is_array()) {
    sceneAsset.instanceTransforms.clear();
    sceneAsset.instanceTransforms.reserve(value["instanceTransforms"].size());
    for (const auto &instanceTransformValue : value["instanceTransforms"]) {
      sceneAsset.instanceTransforms.push_back(
          sceneTransformFromJson(instanceTransformValue));
    }
  }
  sceneAsset.targetTerrainName =
      value.value("targetTerrainName", sceneAsset.targetTerrainName);
  sceneAsset.instanceSpacing =
      std::max(value.value("instanceSpacing", sceneAsset.instanceSpacing), 0.05f);
  sceneAsset.instanceJitter =
      glm::clamp(value.value("instanceJitter", sceneAsset.instanceJitter), 0.0f,
                 1.0f);
  if (value.contains("instanceScaleRange") &&
      value["instanceScaleRange"].is_array() &&
      value["instanceScaleRange"].size() == 2) {
    sceneAsset.instanceScaleRange = glm::vec2(
        value["instanceScaleRange"][0].get<float>(),
        value["instanceScaleRange"][1].get<float>());
  }
  sceneAsset.instanceScaleRange.x =
      std::max(sceneAsset.instanceScaleRange.x, 0.01f);
  sceneAsset.instanceScaleRange.y =
      std::max(sceneAsset.instanceScaleRange.y, sceneAsset.instanceScaleRange.x);
  if (value.contains("instanceScaleVerticalRange") &&
      value["instanceScaleVerticalRange"].is_array() &&
      value["instanceScaleVerticalRange"].size() == 2) {
    sceneAsset.instanceScaleVerticalRange = glm::vec2(
        value["instanceScaleVerticalRange"][0].get<float>(),
        value["instanceScaleVerticalRange"][1].get<float>());
  } else {
    sceneAsset.instanceScaleVerticalRange = sceneAsset.instanceScaleRange;
  }
  sceneAsset.instanceScaleVerticalRange.x =
      std::max(sceneAsset.instanceScaleVerticalRange.x, 0.01f);
  sceneAsset.instanceScaleVerticalRange.y =
      std::max(sceneAsset.instanceScaleVerticalRange.y,
               sceneAsset.instanceScaleVerticalRange.x);
  sceneAsset.instanceAlignToTerrainNormal = value.value(
      "instanceAlignToTerrainNormal", sceneAsset.instanceAlignToTerrainNormal);
  sceneAsset.instanceRandomYaw =
      value.value("instanceRandomYaw", sceneAsset.instanceRandomYaw);
  sceneAsset.instanceYawRangeDegrees = glm::clamp(
      value.value("instanceYawRangeDegrees", sceneAsset.instanceYawRangeDegrees),
      0.0f, 360.0f);
  sceneAsset.instancePitchRangeDegrees = glm::clamp(
      value.value("instancePitchRangeDegrees",
                  sceneAsset.instancePitchRangeDegrees),
      0.0f, 180.0f);
  sceneAsset.instanceRollRangeDegrees = glm::clamp(
      value.value("instanceRollRangeDegrees", sceneAsset.instanceRollRangeDegrees),
      0.0f, 180.0f);
  sceneAsset.instanceMaxSlopeDegrees = glm::clamp(
      value.value("instanceMaxSlopeDegrees", sceneAsset.instanceMaxSlopeDegrees),
      0.0f, 89.9f);
  sceneAsset.instanceHeightOffset = value.value(
      "instanceHeightOffset", sceneAsset.instanceHeightOffset);
  sceneAsset.instanceHeightJitter = std::max(
      value.value("instanceHeightJitter", sceneAsset.instanceHeightJitter), 0.0f);
  sceneAsset.instanceScatterSeed =
      value.value("instanceScatterSeed", sceneAsset.instanceScatterSeed);
  sceneAsset.instancePaintMode =
      value.value("instancePaintMode", sceneAsset.instancePaintMode);
  sceneAsset.instanceEraseMode =
      value.value("instanceEraseMode", sceneAsset.instanceEraseMode);
  sceneAsset.instanceBrushRadius = std::max(
      value.value("instanceBrushRadius", sceneAsset.instanceBrushRadius), 0.05f);
  sceneAsset.terrainWireframeVisible = value.value(
      "terrainWireframeVisible", sceneAsset.terrainWireframeVisible);
  sceneAsset.terrainEditMode =
      value.value("terrainEditMode", sceneAsset.terrainEditMode);
  sceneAsset.terrainBrushRadius = std::max(
      value.value("terrainBrushRadius", sceneAsset.terrainBrushRadius), 0.05f);
  sceneAsset.terrainBrushLowerMode = value.value(
      "terrainBrushLowerMode", sceneAsset.terrainBrushLowerMode);
  sceneAsset.terrainBrushFlattenMode = value.value(
      "terrainBrushFlattenMode", sceneAsset.terrainBrushFlattenMode);
  sceneAsset.terrainBrushColorPaintMode = value.value(
      "terrainBrushColorPaintMode", sceneAsset.terrainBrushColorPaintMode);
  sceneAsset.terrainBrushColor = vec4FromJson(
      value.value("terrainBrushColor", json::array()),
      sceneAsset.terrainBrushColor);
  sceneAsset.terrainBrushTexturePaintMode = value.value(
      "terrainBrushTexturePaintMode", sceneAsset.terrainBrushTexturePaintMode);
  sceneAsset.terrainBrushOpacity =
      glm::clamp(value.value("terrainBrushOpacity", sceneAsset.terrainBrushOpacity),
                 0.0f, 1.0f);
  sceneAsset.terrainBrushTextureVariation = glm::clamp(
      value.value("terrainBrushTextureVariation",
                  sceneAsset.terrainBrushTextureVariation),
      0.0f, 1.0f);
  sceneAsset.terrainPaintCanvasResolution = std::max(
      value.value("terrainPaintCanvasResolution",
                  sceneAsset.terrainPaintCanvasResolution),
      64u);
  sceneAsset.terrainPaintCanvasPath = value.value(
      "terrainPaintCanvasPath", sceneAsset.terrainPaintCanvasPath);
  sceneAsset.terrainBrushTexturePath = value.value(
      "terrainBrushTexturePath", sceneAsset.terrainBrushTexturePath);
  if (sceneAsset.kind == SceneAssetKind::Terrain &&
      value.contains("terrainConfig") && value["terrainConfig"].is_object()) {
    sceneAsset.terrainConfig = terrainConfigFromJson(value["terrainConfig"]);
  }
  if (sceneAsset.kind == SceneAssetKind::Terrain &&
      value.contains("terrainMaterialOverrides") &&
      value["terrainMaterialOverrides"].is_array()) {
    sceneAsset.terrainMaterialOverrides.clear();
    sceneAsset.terrainMaterialOverrides.reserve(
        value["terrainMaterialOverrides"].size());
    for (const auto &materialOverrideValue :
         value["terrainMaterialOverrides"]) {
      sceneAsset.terrainMaterialOverrides.push_back(
          terrainMaterialOverrideFromJson(materialOverrideValue));
    }
  } else if (sceneAsset.kind == SceneAssetKind::CharacterController) {
    if (value.contains("characterControllerConfig") &&
        value["characterControllerConfig"].is_object()) {
      sceneAsset.characterControllerConfig =
          characterControllerConfigFromJson(value["characterControllerConfig"]);
    }
    if (value.contains("characterControllerState") &&
        value["characterControllerState"].is_object()) {
      sceneAsset.characterControllerState =
          characterControllerStateFromJson(value["characterControllerState"]);
    } else {
      sceneAsset.characterControllerState.position = sceneAsset.transform.position;
      sceneAsset.characterControllerState.yawRadians =
          glm::radians(sceneAsset.transform.rotationDegrees.y);
    }
    sceneAsset.transform.position = sceneAsset.characterControllerState.position;
    sceneAsset.transform.rotationDegrees.y =
        glm::degrees(sceneAsset.characterControllerState.yawRadians);
  } else if (sceneAsset.kind == SceneAssetKind::Camera &&
             value.contains("cameraConfig") &&
             value["cameraConfig"].is_object()) {
    sceneAsset.cameraConfig = cameraConfigFromJson(value["cameraConfig"]);
  } else if (sceneAsset.kind == SceneAssetKind::TerrainGrass &&
             value.contains("terrainGrassConfig") &&
             value["terrainGrassConfig"].is_object()) {
    sceneAsset.terrainGrassConfig =
        terrainGrassConfigFromJson(value["terrainGrassConfig"]);
  }
  return sceneAsset;
}

static inline json sceneLightToJson(const SceneLight &light) {
  return {
      {"name", light.name},
      {"type", static_cast<uint32_t>(light.type)},
      {"enabled", light.enabled},
      {"color", vec3ToJson(light.color)},
      {"power", light.power},
      {"exposure", light.exposure},
      {"position", vec3ToJson(light.position)},
      {"radius", light.radius},
      {"range", light.range},
      {"direction", vec3ToJson(light.direction)},
      {"innerConeAngleRadians", light.innerConeAngleRadians},
      {"outerConeAngleRadians", light.outerConeAngleRadians},
      {"castsShadow", light.castsShadow},
      {"shadowBias", light.shadowBias},
      {"shadowNormalBias", light.shadowNormalBias},
  };
}

static inline SceneLight sceneLightFromJson(const json &value) {
  SceneLight light;
  light.name = value.value("name", light.name);
  light.type = static_cast<SceneLightType>(
      value.value("type", static_cast<uint32_t>(light.type)));
  light.enabled = value.value("enabled", light.enabled);
  light.color = vec3FromJson(value.value("color", json::array()), light.color);
  light.power = std::max(
      value.value("power", value.value("intensity", light.power)), 0.0f);
  light.exposure = value.value("exposure", light.exposure);
  light.position =
      vec3FromJson(value.value("position", json::array()), light.position);
  light.radius = std::max(value.value("radius", light.radius), 0.0f);
  light.range = std::max(value.value("range", light.range), 0.01f);
  light.direction =
      vec3FromJson(value.value("direction", json::array()), light.direction);
  light.innerConeAngleRadians =
      value.value("innerConeAngleRadians", light.innerConeAngleRadians);
  light.outerConeAngleRadians = std::max(
      value.value("outerConeAngleRadians", light.outerConeAngleRadians),
      light.innerConeAngleRadians);
  const bool defaultCastsShadow = light.type == SceneLightType::Directional ||
                                  light.type == SceneLightType::Spot;
  light.castsShadow = value.value("castsShadow", defaultCastsShadow);
  light.shadowBias =
      std::max(value.value("shadowBias", light.shadowBias), 0.0f);
  light.shadowNormalBias =
      std::max(value.value("shadowNormalBias", light.shadowNormalBias), 0.0f);
  light.normalizeDirection();
  return light;
}

static inline json proceduralSkyToJson(const ProceduralSkySettings &sky) {
  return {
      {"zenithColor", vec3ToJson(sky.zenithColor)},
      {"horizonColor", vec3ToJson(sky.horizonColor)},
      {"groundColor", vec3ToJson(sky.groundColor)},
      {"sunColor", vec3ToJson(sky.sunColor)},
      {"sunIntensity", sky.sunIntensity},
      {"sunAngularRadius", sky.sunAngularRadius},
      {"sunGlow", sky.sunGlow},
      {"horizonGlow", sky.horizonGlow},
      {"skyExponent", sky.skyExponent},
      {"groundFalloff", sky.groundFalloff},
      {"sunAzimuthRadians", sky.sunAzimuthRadians},
      {"sunElevationRadians", sky.sunElevationRadians},
  };
}

static inline ProceduralSkySettings
proceduralSkyFromJson(const json &value) {
  ProceduralSkySettings sky;
  sky.zenithColor =
      vec3FromJson(value.value("zenithColor", json::array()), sky.zenithColor);
  sky.horizonColor = vec3FromJson(value.value("horizonColor", json::array()),
                                  sky.horizonColor);
  sky.groundColor =
      vec3FromJson(value.value("groundColor", json::array()), sky.groundColor);
  sky.sunColor =
      vec3FromJson(value.value("sunColor", json::array()), sky.sunColor);
  sky.sunIntensity = value.value("sunIntensity", sky.sunIntensity);
  sky.sunAngularRadius = value.value("sunAngularRadius", sky.sunAngularRadius);
  sky.sunGlow = value.value("sunGlow", sky.sunGlow);
  sky.horizonGlow = value.value("horizonGlow", sky.horizonGlow);
  sky.skyExponent = value.value("skyExponent", sky.skyExponent);
  sky.groundFalloff = value.value("groundFalloff", sky.groundFalloff);
  sky.sunAzimuthRadians =
      value.value("sunAzimuthRadians", sky.sunAzimuthRadians);
  sky.sunElevationRadians =
      value.value("sunElevationRadians", sky.sunElevationRadians);
  return sky;
}

static inline json
iblBakeSettingsToJson(const ImageBasedLightingBakeSettings &settings) {
  return {
      {"environmentResolution", settings.environmentResolution},
      {"irradianceResolution", settings.irradianceResolution},
      {"prefilteredResolution", settings.prefilteredResolution},
      {"brdfResolution", settings.brdfResolution},
      {"irradianceSamples", settings.irradianceSamples},
      {"prefilteredSamples", settings.prefilteredSamples},
      {"brdfSamples", settings.brdfSamples},
      {"environmentHdrPath", settings.environmentHdrPath},
      {"sky", proceduralSkyToJson(settings.sky)},
  };
}

static inline ImageBasedLightingBakeSettings
iblBakeSettingsFromJson(const json &value) {
  ImageBasedLightingBakeSettings settings;
  settings.environmentResolution =
      value.value("environmentResolution", settings.environmentResolution);
  settings.irradianceResolution =
      value.value("irradianceResolution", settings.irradianceResolution);
  settings.prefilteredResolution =
      value.value("prefilteredResolution", settings.prefilteredResolution);
  settings.brdfResolution =
      value.value("brdfResolution", settings.brdfResolution);
  settings.irradianceSamples =
      value.value("irradianceSamples", settings.irradianceSamples);
  settings.prefilteredSamples =
      value.value("prefilteredSamples", settings.prefilteredSamples);
  settings.brdfSamples = value.value("brdfSamples", settings.brdfSamples);
  settings.environmentHdrPath =
      value.value("environmentHdrPath", settings.environmentHdrPath);
  if (value.contains("sky") && value["sky"].is_object()) {
    settings.sky = proceduralSkyFromJson(value["sky"]);
  }
  return settings;
}

static inline json settingsToJson(const DefaultDebugUISettings &settings) {
  json sceneObjects = json::array();
  for (const auto &object : settings.sceneObjects) {
    sceneObjects.push_back(sceneObjectToJson(object));
  }

  json sceneLights = json::array();
  for (const auto &light : settings.sceneLights.lights()) {
    sceneLights.push_back(sceneLightToJson(light));
  }

  json value = {
      {"presentedOutput", static_cast<uint32_t>(settings.presentedOutput)},
      {"pbrDebugView", static_cast<uint32_t>(settings.pbrDebugView)},
      {"selectedMaterialIndex", settings.selectedMaterialIndex},
      {"selectedObjectIndex", settings.selectedObjectIndex},
      {"selectedLightIndex", settings.selectedLightIndex},
      {"selectedBoneIndex", settings.selectedBoneIndex},
      {"selectedAnimationObjectIndex", settings.selectedAnimationObjectIndex},
      {"selectedAnimationIndex", settings.selectedAnimationIndex},
      {"sceneObjects", sceneObjects},
      {"sceneLights", sceneLights},
      {"lightMarkersVisible", settings.lightMarkersVisible},
      {"lightMarkerScale", settings.lightMarkerScale},
      {"bonesVisible", settings.bonesVisible},
      {"boneMarkerScale", settings.boneMarkerScale},
      {"showBoneWeights", settings.showBoneWeights},
      {"shadowsEnabled", settings.shadowsEnabled},
      {"directionalShadowExtent", settings.directionalShadowExtent},
      {"directionalShadowNearPlane", settings.directionalShadowNearPlane},
      {"directionalShadowFarPlane", settings.directionalShadowFarPlane},
      {"exposure", settings.exposure},
      {"autoExposureKey", settings.autoExposureKey},
      {"whitePoint", settings.whitePoint},
      {"gamma", settings.gamma},
      {"autoExposureEnabled", settings.autoExposureEnabled},
      {"tonemapOperator", static_cast<uint32_t>(settings.tonemapOperator)},
      {"environmentIntensity", settings.environmentIntensity},
      {"environmentBackgroundWeight", settings.environmentBackgroundWeight},
      {"environmentDiffuseWeight", settings.environmentDiffuseWeight},
      {"environmentSpecularWeight", settings.environmentSpecularWeight},
      {"dielectricSpecularScale", settings.dielectricSpecularScale},
      {"environmentRotationRadians", settings.environmentRotationRadians},
      {"iblEnabled", settings.iblEnabled},
      {"skyboxVisible", settings.skyboxVisible},
      {"syncSkySunToLight", settings.syncSkySunToLight},
      {"iblBakeSettings", iblBakeSettingsToJson(settings.iblBakeSettings)},
      {"cameraPosition", vec3ToJson(settings.cameraPosition)},
      {"cameraYawRadians", settings.cameraYawRadians},
      {"cameraPitchRadians", settings.cameraPitchRadians},
      {"cameraMoveSpeed", settings.cameraMoveSpeed},
      {"cameraLookSensitivity", settings.cameraLookSensitivity},
      {"cameraFarPlane", settings.cameraFarPlane},
  };

  if (!settings.sceneObjects.empty()) {
    value["modelPosition"] =
        vec3ToJson(settings.sceneObjects.front().transform.position);
    value["modelRotationDegrees"] =
        vec3ToJson(settings.sceneObjects.front().transform.rotationDegrees);
    value["modelScale"] =
        vec3ToJson(settings.sceneObjects.front().transform.scale);
  }

  return value;
}

static inline json sessionToJson(const DefaultDebugUISettings &settings,
                                 const std::vector<SceneAssetInstance> &sceneAssets) {
  json value = settingsToJson(settings);
  json sceneAssetsJson = json::array();
  for (const auto &sceneAsset : sceneAssets) {
    sceneAssetsJson.push_back(sceneAssetToJson(sceneAsset));
  }
  value["sceneAssets"] = std::move(sceneAssetsJson);
  return value;
}

static inline DefaultDebugUISettings settingsFromJson(const json &value) {
  DefaultDebugUISettings settings;
  settings.presentedOutput = static_cast<PresentedOutput>(value.value(
      "presentedOutput", static_cast<uint32_t>(settings.presentedOutput)));
  settings.pbrDebugView = static_cast<PbrDebugView>(value.value(
      "pbrDebugView", static_cast<uint32_t>(settings.pbrDebugView)));
  settings.selectedMaterialIndex =
      value.value("selectedMaterialIndex", settings.selectedMaterialIndex);
  settings.selectedObjectIndex =
      value.value("selectedObjectIndex", settings.selectedObjectIndex);
  settings.selectedLightIndex =
      value.value("selectedLightIndex", settings.selectedLightIndex);
  settings.selectedBoneIndex =
      value.value("selectedBoneIndex", settings.selectedBoneIndex);
  settings.selectedAnimationObjectIndex = value.value(
      "selectedAnimationObjectIndex", settings.selectedAnimationObjectIndex);
  settings.selectedAnimationIndex =
      value.value("selectedAnimationIndex", settings.selectedAnimationIndex);

  settings.sceneObjects.clear();
  const bool hasSceneObjectsArray =
      value.contains("sceneObjects") && value["sceneObjects"].is_array();
  if (hasSceneObjectsArray) {
    for (const auto &objectValue : value["sceneObjects"]) {
      settings.sceneObjects.push_back(sceneObjectFromJson(objectValue));
    }
  }
  if (!hasSceneObjectsArray && settings.sceneObjects.empty() &&
      (value.contains("modelPosition") || value.contains("modelRotationDegrees") ||
       value.contains("modelScale"))) {
    SceneObject legacyObject;
    legacyObject.transform.position =
        vec3FromJson(value.value("modelPosition", json::array()),
                     legacyObject.transform.position);
    legacyObject.transform.rotationDegrees =
        vec3FromJson(value.value("modelRotationDegrees", json::array()),
                     legacyObject.transform.rotationDegrees);
    legacyObject.transform.scale = vec3FromJson(
        value.value("modelScale", json::array()), legacyObject.transform.scale);
    settings.sceneObjects.push_back(std::move(legacyObject));
  }
  clampSceneObjectSelection(settings);

  settings.sceneLights.clear();
  const bool hasSceneLightsArray =
      value.contains("sceneLights") && value["sceneLights"].is_array();
  if (hasSceneLightsArray) {
    for (const auto &lightValue : value["sceneLights"]) {
      settings.sceneLights.lights().push_back(sceneLightFromJson(lightValue));
    }
  }
  if (!hasSceneLightsArray && settings.sceneLights.empty()) {
    settings.sceneLights = SceneLightSet::showcaseLights();
  }

  settings.lightMarkersVisible =
      value.value("lightMarkersVisible", settings.lightMarkersVisible);
  settings.lightMarkerScale = std::max(
      value.value("lightMarkerScale", settings.lightMarkerScale), 0.01f);
  settings.bonesVisible = value.value("bonesVisible", settings.bonesVisible);
  settings.boneMarkerScale = std::max(
      value.value("boneMarkerScale", settings.boneMarkerScale), 0.005f);
  settings.showBoneWeights =
      value.value("showBoneWeights", settings.showBoneWeights);
  settings.shadowsEnabled =
      value.value("shadowsEnabled", settings.shadowsEnabled);
  settings.directionalShadowExtent = std::max(
      value.value("directionalShadowExtent", settings.directionalShadowExtent),
      0.5f);
  settings.directionalShadowNearPlane =
      std::max(value.value("directionalShadowNearPlane",
                           settings.directionalShadowNearPlane),
               0.01f);
  settings.directionalShadowFarPlane =
      std::max(value.value("directionalShadowFarPlane",
                           settings.directionalShadowFarPlane),
               settings.directionalShadowNearPlane + 0.5f);
  settings.exposure = value.value("exposure", settings.exposure);
  settings.autoExposureKey =
      value.value("autoExposureKey", settings.autoExposureKey);
  settings.whitePoint = value.value("whitePoint", settings.whitePoint);
  settings.gamma = value.value("gamma", settings.gamma);
  settings.autoExposureEnabled =
      value.value("autoExposureEnabled", settings.autoExposureEnabled);
  settings.tonemapOperator = static_cast<TonemapOperator>(value.value(
      "tonemapOperator", static_cast<uint32_t>(settings.tonemapOperator)));
  settings.environmentIntensity =
      value.value("environmentIntensity", settings.environmentIntensity);
  settings.environmentBackgroundWeight = value.value(
      "environmentBackgroundWeight", settings.environmentBackgroundWeight);
  settings.environmentDiffuseWeight = value.value(
      "environmentDiffuseWeight", settings.environmentDiffuseWeight);
  settings.environmentSpecularWeight = value.value(
      "environmentSpecularWeight", settings.environmentSpecularWeight);
  settings.dielectricSpecularScale =
      value.value("dielectricSpecularScale", settings.dielectricSpecularScale);
  settings.environmentRotationRadians = value.value(
      "environmentRotationRadians", settings.environmentRotationRadians);
  settings.iblEnabled = value.value("iblEnabled", settings.iblEnabled);
  settings.skyboxVisible = value.value("skyboxVisible", settings.skyboxVisible);
  settings.syncSkySunToLight =
      value.value("syncSkySunToLight", settings.syncSkySunToLight);
  if (value.contains("iblBakeSettings") &&
      value["iblBakeSettings"].is_object()) {
    settings.iblBakeSettings =
        iblBakeSettingsFromJson(value["iblBakeSettings"]);
  }
  settings.cameraPosition = vec3FromJson(
      value.value("cameraPosition", json::array()), settings.cameraPosition);
  settings.cameraYawRadians =
      value.value("cameraYawRadians", settings.cameraYawRadians);
  settings.cameraPitchRadians =
      value.value("cameraPitchRadians", settings.cameraPitchRadians);
  settings.cameraMoveSpeed =
      value.value("cameraMoveSpeed", settings.cameraMoveSpeed);
  settings.cameraLookSensitivity =
      value.value("cameraLookSensitivity", settings.cameraLookSensitivity);
  settings.cameraFarPlane =
      value.value("cameraFarPlane", settings.cameraFarPlane);
  settings.cameraLookActive = false;
  settings.cameraLastCursorX = 0.0;
  settings.cameraLastCursorY = 0.0;
  return settings;
}

static inline bool saveDebugSession(const std::filesystem::path &path,
                                    const DefaultDebugUISettings &settings,
                                    const std::vector<SceneAssetInstance> &sceneAssets) {
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
  }

  std::ofstream output(path);
  if (!output.is_open()) {
    return false;
  }

  output << sessionToJson(settings, sceneAssets).dump(2);
  return output.good();
}

static inline bool loadDebugSession(const std::filesystem::path &path,
                                    DefaultDebugUISettings &settings,
                                    std::vector<SceneAssetInstance> *sceneAssets = nullptr) {
  if (!std::filesystem::exists(path)) {
    return false;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  json parsed;
  input >> parsed;
  settings = settingsFromJson(parsed);
  if (sceneAssets != nullptr) {
    sceneAssets->clear();
    if (parsed.contains("sceneAssets") && parsed["sceneAssets"].is_array()) {
      for (const auto &sceneAssetValue : parsed["sceneAssets"]) {
        sceneAssets->push_back(sceneAssetFromJson(sceneAssetValue));
      }
    }
  }
  return true;
}

};
