#pragma once

#include "../DefaultEngineConfig.h"
#include "assets/RenderableModel.h"
#include "assets/TerrainGrassChunkModelAsset.h"
#include "backend/VulkanBackend.h"
#include "resources/FrameGeometryUniforms.h"
#include "resources/Sampler.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include "world/Terrain.h"
#include "world/TerrainQueries.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct DefaultEngineTerrainGrassChunkState {
  size_t grassAssetIndex = 0;
  size_t terrainIndex = 0;
  size_t nearSharedModelIndex = 0;
  size_t midSharedModelIndex = 0;
  glm::ivec2 chunkCoord{0, 0};
  float chunkMinX = 0.0f;
  float chunkMaxX = 0.0f;
  float chunkMinZ = 0.0f;
  float chunkMaxZ = 0.0f;
  glm::vec3 localCenter{0.0f};
  float localBoundingRadius = 0.0f;
  std::vector<glm::mat4> instanceTransforms;
  bool instancesBuilt = false;
};

struct DefaultEngineTerrainGrassSharedModelState {
  size_t grassAssetIndex = 0;
  size_t terrainIndex = 0;
  RenderableModel model;
  std::vector<glm::mat4> visibleInstanceTransforms;
  uint64_t visibleInstanceSignature = 0;
  bool visibleInstanceTransformsValid = false;
};

struct DefaultEngineTerrainGrassRuntimeContext {
  std::vector<SceneAssetInstance> &sceneAssets;
  const DefaultDebugUISettings &debugUiSettings;
  std::vector<DefaultEngineTerrainGrassChunkState> &grassChunks;
  std::vector<DefaultEngineTerrainGrassSharedModelState> &sharedModels;
  VulkanBackend &backend;
  FrameGeometryUniforms &frameGeometryUniforms;
  Sampler &sampler;
};

class DefaultEngineTerrainGrassRuntime {
public:
  static void reloadGrass(
      DefaultEngineTerrainGrassRuntimeContext &context,
      const vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout) {
    context.grassChunks.clear();
    context.sharedModels.clear();

    for (size_t grassAssetIndex = 0; grassAssetIndex < context.sceneAssets.size();
         ++grassAssetIndex) {
      const SceneAssetInstance &grassAsset = context.sceneAssets[grassAssetIndex];
      if (grassAsset.kind != SceneAssetKind::TerrainGrass) {
        continue;
      }

      const auto resolvedTerrain =
          resolveTargetTerrain(context.sceneAssets, grassAsset);
      if (!resolvedTerrain.has_value()) {
        continue;
      }

      const size_t terrainIndex = resolvedTerrain->terrainIndex;
      const SceneAssetInstance &terrainAsset = context.sceneAssets[terrainIndex];
      const TerrainGrassConfig &grassConfig = grassAsset.terrainGrassConfig;
      if (std::max(grassConfig.density, 0.0f) <= 1e-6f) {
        continue;
      }

      auto nearSharedAsset = buildSharedGrassAsset(
          grassAsset, grassConfig, grassConfig.bladesPerClump, 1.0f, 1.0f,
          1.0f, "");
      DefaultEngineTerrainGrassSharedModelState nearSharedModel{
          .grassAssetIndex = grassAssetIndex,
          .terrainIndex = terrainIndex,
      };
      nearSharedModel.model.loadFromAsset(
          std::move(nearSharedAsset), context.backend.commands(),
          context.backend.device(), sceneDescriptorSetLayout, nullptr,
          context.frameGeometryUniforms, context.sampler,
          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
      const size_t nearSharedModelIndex = context.sharedModels.size();
      context.sharedModels.push_back(std::move(nearSharedModel));

      const uint32_t midBladesPerClump =
          std::min(std::max(grassConfig.midBladesPerClump, 1u),
                   std::max(grassConfig.bladesPerClump, 1u));
      auto midSharedAsset = buildSharedGrassAsset(
          grassAsset, grassConfig, midBladesPerClump, 1.1f, 2.4f, 0.45f,
          "_mid_lod");
      DefaultEngineTerrainGrassSharedModelState midSharedModel{
          .grassAssetIndex = grassAssetIndex,
          .terrainIndex = terrainIndex,
      };
      midSharedModel.model.loadFromAsset(
          std::move(midSharedAsset), context.backend.commands(),
          context.backend.device(), sceneDescriptorSetLayout, nullptr,
          context.frameGeometryUniforms, context.sampler,
          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
      const size_t midSharedModelIndex = context.sharedModels.size();
      context.sharedModels.push_back(std::move(midSharedModel));

      const float resolvedChunkSize = std::max(grassConfig.chunkSize, 1.0f);
      const float halfSizeX = terrainAsset.terrainConfig.sizeX * 0.5f;
      const float halfSizeZ = terrainAsset.terrainConfig.sizeZ * 0.5f;
      const int chunkCountX = std::max(
          static_cast<int>(std::ceil(terrainAsset.terrainConfig.sizeX /
                                     resolvedChunkSize)),
          1);
      const int chunkCountZ = std::max(
          static_cast<int>(std::ceil(terrainAsset.terrainConfig.sizeZ /
                                     resolvedChunkSize)),
          1);

      for (int chunkZ = 0; chunkZ < chunkCountZ; ++chunkZ) {
        const float chunkMinZ =
            -halfSizeZ + static_cast<float>(chunkZ) * resolvedChunkSize;
        const float chunkMaxZ =
            std::min(chunkMinZ + resolvedChunkSize, halfSizeZ);
        for (int chunkX = 0; chunkX < chunkCountX; ++chunkX) {
          const float chunkMinX =
              -halfSizeX + static_cast<float>(chunkX) * resolvedChunkSize;
          const float chunkMaxX =
              std::min(chunkMinX + resolvedChunkSize, halfSizeX);

          const glm::vec3 localCenter =
              chunkLocalCenter(terrainAsset.terrainConfig, chunkMinX, chunkMaxX,
                               chunkMinZ, chunkMaxZ);
          const float localBoundingRadius =
              chunkBoundingRadius(grassConfig, chunkMinX, chunkMaxX, chunkMinZ,
                                  chunkMaxZ);

          DefaultEngineTerrainGrassChunkState chunkState{
              .grassAssetIndex = grassAssetIndex,
              .terrainIndex = terrainIndex,
              .nearSharedModelIndex = nearSharedModelIndex,
              .midSharedModelIndex = midSharedModelIndex,
              .chunkCoord = {chunkX, chunkZ},
              .chunkMinX = chunkMinX,
              .chunkMaxX = chunkMaxX,
              .chunkMinZ = chunkMinZ,
              .chunkMaxZ = chunkMaxZ,
              .localCenter = localCenter,
              .localBoundingRadius = localBoundingRadius,
          };
          context.grassChunks.push_back(std::move(chunkState));
        }
      }
    }
  }

  static void appendVisibleRenderItems(
      DefaultEngineTerrainGrassRuntimeContext &context,
      std::vector<RenderItem> &renderItems, DeviceContext &deviceContext,
      const RenderPass *geometryPass, const glm::vec3 &cameraPosition,
      const glm::vec3 &cameraForward) {
    if (geometryPass == nullptr) {
      return;
    }

    const glm::vec3 normalizedCameraForward =
        safeNormalized(cameraForward, glm::vec3(0.0f, 0.0f, -1.0f));
    const uint64_t visibleBatchSignature =
        grassVisibleBatchSignature(context, cameraPosition, normalizedCameraForward);
    if (visibleBatchesCached(context, visibleBatchSignature)) {
      appendSharedModelRenderItems(context, renderItems, deviceContext,
                                   geometryPass, false);
      return;
    }

    for (auto &sharedModel : context.sharedModels) {
      sharedModel.visibleInstanceTransforms.clear();
      sharedModel.visibleInstanceTransformsValid = false;
    }

    for (auto &chunk : context.grassChunks) {
      if (chunk.grassAssetIndex >= context.sceneAssets.size() ||
          chunk.terrainIndex >= context.sceneAssets.size() ||
          chunk.nearSharedModelIndex >= context.sharedModels.size() ||
          chunk.midSharedModelIndex >= context.sharedModels.size()) {
        continue;
      }

      const SceneAssetInstance &grassAsset =
          context.sceneAssets[chunk.grassAssetIndex];
      const SceneAssetInstance &terrainAsset =
          context.sceneAssets[chunk.terrainIndex];
      if (grassAsset.kind != SceneAssetKind::TerrainGrass ||
          terrainAsset.kind != SceneAssetKind::Terrain) {
        continue;
      }
      if (!sceneObjectVisible(context, chunk.grassAssetIndex) ||
          !sceneObjectVisible(context, chunk.terrainIndex)) {
        continue;
      }

      const glm::mat4 terrainModel =
          terrainModelMatrix(context, chunk.terrainIndex);
      const glm::vec3 worldCenter =
          glm::vec3(terrainModel * glm::vec4(chunk.localCenter, 1.0f));
      const float maxScale = maxModelScale(terrainModel);
      const float worldRadius = chunk.localBoundingRadius * maxScale;
      const float drawDistance =
          std::max(grassAsset.terrainGrassConfig.drawDistance, 0.0f);
      if (drawDistance <= 1e-6f) {
        continue;
      }
      const float nearDistance = glm::clamp(
          grassAsset.terrainGrassConfig.nearDistance, 0.0f, drawDistance);

      const glm::vec3 toChunk = worldCenter - cameraPosition;
      const float horizontalDistanceToChunk =
          glm::length(glm::vec2(toChunk.x, toChunk.z));
      if (horizontalDistanceToChunk > drawDistance + worldRadius) {
        continue;
      }
      const float forwardDistance = glm::dot(toChunk, normalizedCameraForward);
      if (forwardDistance < -worldRadius ||
          forwardDistance > drawDistance + worldRadius) {
        continue;
      }

      ensureChunkInstances(context, chunk);
      if (chunk.instanceTransforms.empty()) {
        continue;
      }

      const bool useNearLod =
          horizontalDistanceToChunk <= nearDistance + worldRadius;
      if (!useNearLod && grassAsset.terrainGrassConfig.midDensityScale <= 1e-6f) {
        continue;
      }
      const size_t targetSharedModelIndex =
          useNearLod ? chunk.nearSharedModelIndex : chunk.midSharedModelIndex;
      auto &visibleInstanceTransforms =
          context.sharedModels[targetSharedModelIndex].visibleInstanceTransforms;
      visibleInstanceTransforms.reserve(visibleInstanceTransforms.size() +
                                        chunk.instanceTransforms.size());
      for (const auto &instanceTransform : chunk.instanceTransforms) {
        if (!useNearLod &&
            !shouldKeepMidInstance(grassAsset.terrainGrassConfig,
                                   instanceTransform)) {
          continue;
        }
        visibleInstanceTransforms.push_back(terrainModel * instanceTransform);
      }
    }

    for (auto &sharedModel : context.sharedModels) {
      sharedModel.visibleInstanceSignature = visibleBatchSignature;
      sharedModel.visibleInstanceTransformsValid = true;
    }
    appendSharedModelRenderItems(context, renderItems, deviceContext,
                                 geometryPass, true);
  }

private:
  struct ResolvedTerrainTarget {
    size_t terrainIndex = 0;
  };

  static void appendSharedModelRenderItems(
      DefaultEngineTerrainGrassRuntimeContext &context,
      std::vector<RenderItem> &renderItems, DeviceContext &deviceContext,
      const RenderPass *geometryPass, bool instanceDataChanged) {
    for (size_t sharedModelIndex = 0; sharedModelIndex < context.sharedModels.size();
         ++sharedModelIndex) {
      auto &sharedModel = context.sharedModels[sharedModelIndex];
      if (sharedModel.visibleInstanceTransforms.empty()) {
        continue;
      }

      const auto grassItems = sharedModel.model.buildRenderItems(
          deviceContext, geometryPass, sharedModel.visibleInstanceTransforms, -1,
          sharedModelIndex * 16u, instanceDataChanged);
      renderItems.insert(renderItems.end(), grassItems.begin(), grassItems.end());
    }
  }

  static bool visibleBatchesCached(
      const DefaultEngineTerrainGrassRuntimeContext &context,
      uint64_t visibleBatchSignature) {
    if (context.sharedModels.empty()) {
      return false;
    }
    for (const auto &sharedModel : context.sharedModels) {
      if (!sharedModel.visibleInstanceTransformsValid ||
          sharedModel.visibleInstanceSignature != visibleBatchSignature) {
        return false;
      }
    }
    return true;
  }

  static uint64_t grassVisibleBatchSignature(
      const DefaultEngineTerrainGrassRuntimeContext &context,
      const glm::vec3 &cameraPosition, const glm::vec3 &cameraForward) {
    const float cacheCellSize = grassVisibilityCacheCellSize(context);
    uint64_t hash = 1469598103934665603ull;
    hash = hashInteger(hash, quantizedValue(cameraPosition.x, cacheCellSize));
    hash = hashInteger(hash, quantizedValue(cameraPosition.z, cacheCellSize));
    hash = hashInteger(hash, quantizedCameraYaw(cameraForward));

    for (size_t index = 0; index < context.sceneAssets.size(); ++index) {
      const SceneAssetInstance &sceneAsset = context.sceneAssets[index];
      if (sceneAsset.kind != SceneAssetKind::TerrainGrass &&
          sceneAsset.kind != SceneAssetKind::Terrain) {
        continue;
      }

      hash = hashInteger(hash, static_cast<int64_t>(index));
      hash = hashInteger(hash,
                         static_cast<int64_t>(sceneAsset.kind));
      hash = hashInteger(hash, sceneObjectVisible(context, index) ? 1 : 0);
      if (sceneAsset.kind == SceneAssetKind::TerrainGrass) {
        const TerrainGrassConfig &grassConfig = sceneAsset.terrainGrassConfig;
        hash = hashQuantizedFloat(hash, grassConfig.drawDistance);
        hash = hashQuantizedFloat(hash, grassConfig.nearDistance);
        hash = hashQuantizedFloat(hash, grassConfig.midDensityScale);
        hash = hashQuantizedFloat(hash, grassConfig.density);
        hash = hashQuantizedFloat(hash, grassConfig.chunkSize);
        hash = hashInteger(hash, grassConfig.bladesPerClump);
        hash = hashInteger(hash, grassConfig.midBladesPerClump);
        hash = hashInteger(hash, grassConfig.scatterSeed);
      }
      if (sceneAsset.kind == SceneAssetKind::Terrain) {
        const glm::mat4 modelMatrix = terrainModelMatrix(context, index);
        for (int column = 0; column < 4; ++column) {
          for (int row = 0; row < 4; ++row) {
            hash = hashQuantizedFloat(hash, modelMatrix[column][row]);
          }
        }
      }
    }
    return hash;
  }

  static float grassVisibilityCacheCellSize(
      const DefaultEngineTerrainGrassRuntimeContext &context) {
    float cellSize = 8.0f;
    for (const auto &sceneAsset : context.sceneAssets) {
      if (sceneAsset.kind == SceneAssetKind::TerrainGrass) {
        cellSize = std::max(cellSize, sceneAsset.terrainGrassConfig.chunkSize);
      }
    }
    return std::max(cellSize, 1.0f);
  }

  static int64_t quantizedValue(float value, float cellSize) {
    return static_cast<int64_t>(std::floor(value / std::max(cellSize, 1.0f)));
  }

  static int64_t quantizedCameraYaw(const glm::vec3 &cameraForward) {
    const float yaw = std::atan2(cameraForward.x, cameraForward.z);
    const float normalizedYaw =
        (yaw + glm::pi<float>()) / glm::two_pi<float>();
    return static_cast<int64_t>(std::floor(normalizedYaw * 16.0f));
  }

  static uint64_t hashInteger(uint64_t hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
  }

  static uint64_t hashQuantizedFloat(uint64_t hash, float value,
                                     float scale = 1000.0f) {
    return hashInteger(hash, static_cast<uint64_t>(
                                 std::llround(value * scale)));
  }

  static std::unique_ptr<TerrainGrassChunkModelAsset>
  buildSharedGrassAsset(const SceneAssetInstance &grassAsset,
                        const TerrainGrassConfig &grassConfig,
                        uint32_t bladesPerClumpValue, float bladeHeightScale,
                        float bladeWidthScale, float clumpRadiusScale,
                        const std::string &labelSuffix) {
    std::vector<ImportedGeometryVertex> vertices;
    std::vector<uint32_t> indices;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const uint32_t bladesPerClump = std::max(bladesPerClumpValue, 1u);
    const uint32_t seed = grassConfig.scatterSeed;

    for (uint32_t bladeIndex = 0; bladeIndex < bladesPerClump; ++bladeIndex) {
      const float bladeAlpha =
          static_cast<float>(bladeIndex) / static_cast<float>(bladesPerClump);
      const float yaw = bladeAlpha * glm::two_pi<float>() +
                        randomSignedUnit(seed, bladeAlpha, 0.0f, 11.0f) * 0.35f;
      const glm::vec3 forward =
          glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
      const glm::vec3 offsetDirection =
          glm::vec3(std::sin(yaw + 1.7f), 0.0f, std::cos(yaw + 1.7f));
      const float offsetAmount =
          grassConfig.clumpRadius * std::max(clumpRadiusScale, 0.0f) *
          randomUnit(seed, bladeAlpha, 0.0f, 23.0f);
      const glm::vec3 rootPosition = offsetDirection * offsetAmount;
      const float width =
          randomRange(grassConfig.bladeWidthRange.x,
                      grassConfig.bladeWidthRange.y,
                      randomUnit(seed, bladeAlpha, 0.0f, 31.0f)) *
          std::max(bladeWidthScale, 0.01f);
      const float height =
          randomRange(grassConfig.bladeHeightRange.x,
                      grassConfig.bladeHeightRange.y,
                      randomUnit(seed, bladeAlpha, 0.0f, 37.0f)) *
          std::max(bladeHeightScale, 0.01f);
      const float leanRadians =
          glm::radians(randomUnit(seed, bladeAlpha, 0.0f, 41.0f) *
                       grassConfig.randomLeanDegrees);
      appendBlade(vertices, indices, rootPosition, up, forward,
                  glm::vec4(0.42f, 0.72f, 0.24f, 1.0f), width, height,
                  leanRadians);
    }

    auto asset = std::make_unique<TerrainGrassChunkModelAsset>();
    asset->setChunkGeometry(
        std::move(vertices), std::move(indices),
        ImportedMaterialData{
            .name = "Terrain Grass",
            .baseColorFactor = glm::vec4(1.0f),
            .metallicFactor = 0.0f,
            .roughnessFactor = 1.0f,
            .occlusionStrength = 1.0f,
        },
        (grassAsset.name.empty() ? "terrain_grass" : grassAsset.name) +
            labelSuffix);
    return asset;
  }

  static glm::vec3 chunkLocalCenter(const TerrainConfig &terrainConfig,
                                    float chunkMinX, float chunkMaxX,
                                    float chunkMinZ, float chunkMaxZ) {
    const float centerX = 0.5f * (chunkMinX + chunkMaxX);
    const float centerZ = 0.5f * (chunkMinZ + chunkMaxZ);
    return glm::vec3(
        centerX, TerrainGenerator::sampleHeight(terrainConfig, centerX, centerZ),
        centerZ);
  }

  static float chunkBoundingRadius(const TerrainGrassConfig &grassConfig,
                                   float chunkMinX, float chunkMaxX,
                                   float chunkMinZ, float chunkMaxZ) {
    return std::sqrt(std::pow(0.5f * (chunkMaxX - chunkMinX), 2.0f) +
                     std::pow(0.5f * (chunkMaxZ - chunkMinZ), 2.0f) +
                     std::pow(grassConfig.bladeHeightRange.y, 2.0f)) +
           grassConfig.clumpRadius;
  }

  static std::optional<ResolvedTerrainTarget>
  resolveTargetTerrain(const std::vector<SceneAssetInstance> &sceneAssets,
                       const SceneAssetInstance &grassAsset) {
    std::optional<ResolvedTerrainTarget> firstTerrain;
    for (size_t index = 0; index < sceneAssets.size(); ++index) {
      if (sceneAssets[index].kind != SceneAssetKind::Terrain) {
        continue;
      }

      if (!firstTerrain.has_value()) {
        firstTerrain = ResolvedTerrainTarget{.terrainIndex = index};
      }

      if (!grassAsset.targetTerrainName.empty() &&
          AppSceneController::sceneAssetName(sceneAssets[index], index) ==
              grassAsset.targetTerrainName) {
        return ResolvedTerrainTarget{.terrainIndex = index};
      }
    }
    return firstTerrain;
  }

  static bool sceneObjectVisible(
      const DefaultEngineTerrainGrassRuntimeContext &context, size_t index) {
    if (index < context.debugUiSettings.sceneObjects.size()) {
      return context.debugUiSettings.sceneObjects[index].visible;
    }
    return context.sceneAssets[index].visible;
  }

  static glm::mat4 terrainModelMatrix(
      const DefaultEngineTerrainGrassRuntimeContext &context,
      size_t terrainIndex) {
    if (terrainIndex < context.debugUiSettings.sceneObjects.size()) {
      return AppSceneController::sceneTransformMatrix(
          context.debugUiSettings.sceneObjects[terrainIndex].transform);
    }
    return AppSceneController::sceneTransformMatrix(
        context.sceneAssets[terrainIndex].transform);
  }

  static float maxModelScale(const glm::mat4 &modelMatrix) {
    return std::max(
        {glm::length(glm::vec3(modelMatrix[0])),
         glm::length(glm::vec3(modelMatrix[1])),
         glm::length(glm::vec3(modelMatrix[2])), 1.0f});
  }

  static float gridStart(float halfSize, float spacing) {
    return halfSize <= spacing * 0.5f ? 0.0f : -halfSize + spacing * 0.5f;
  }

  static float randomUnit(uint32_t seed, float x, float z, float salt) {
    const float value =
        std::sin(static_cast<float>(seed) * 17.0f + x * 12.9898f +
                 z * 78.233f + salt * 37.719f) *
        43758.5453f;
    return glm::fract(value);
  }

  static float randomSignedUnit(uint32_t seed, float x, float z, float salt) {
    return randomUnit(seed, x, z, salt) * 2.0f - 1.0f;
  }

  static float randomRange(float minValue, float maxValue, float alpha) {
    return glm::mix(minValue, std::max(maxValue, minValue), alpha);
  }

  static glm::vec3 rotateAroundAxis(const glm::vec3 &vector,
                                    const glm::vec3 &axis, float radians) {
    return glm::vec3(
        glm::rotate(glm::mat4(1.0f), radians, glm::normalize(axis)) *
        glm::vec4(vector, 0.0f));
  }

  static glm::mat4 grassInstanceTransform(const glm::vec3 &position,
                                          const glm::vec3 &normal,
                                          float yawRadians, float scale) {
    const glm::vec3 up =
        safeNormalized(normal, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 fallbackForward =
        std::abs(glm::dot(up, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 baseForward =
        safeNormalized(glm::cross(up, glm::cross(fallbackForward, up)),
                       glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 forward = rotateAroundAxis(baseForward, up, yawRadians);
    const glm::vec3 right =
        safeNormalized(glm::cross(up, forward), glm::vec3(1.0f, 0.0f, 0.0f));

    glm::mat4 transform(1.0f);
    transform[0] = glm::vec4(right * scale, 0.0f);
    transform[1] = glm::vec4(up * scale, 0.0f);
    transform[2] = glm::vec4(forward * scale, 0.0f);
    transform[3] = glm::vec4(position, 1.0f);
    return transform;
  }

  static bool shouldKeepMidInstance(const TerrainGrassConfig &grassConfig,
                                    const glm::mat4 &instanceTransform) {
    const float densityScale =
        glm::clamp(grassConfig.midDensityScale, 0.0f, 1.0f);
    if (densityScale >= 1.0f) {
      return true;
    }
    const glm::vec3 position = glm::vec3(instanceTransform[3]);
    return randomUnit(grassConfig.scatterSeed, position.x, position.z, 397.0f) <=
           densityScale;
  }

  static void ensureChunkInstances(
      DefaultEngineTerrainGrassRuntimeContext &context,
      DefaultEngineTerrainGrassChunkState &chunk) {
    if (chunk.instancesBuilt || chunk.grassAssetIndex >= context.sceneAssets.size() ||
        chunk.terrainIndex >= context.sceneAssets.size()) {
      return;
    }

    chunk.instancesBuilt = true;
    const SceneAssetInstance &grassAsset =
        context.sceneAssets[chunk.grassAssetIndex];
    const SceneAssetInstance &terrainAsset =
        context.sceneAssets[chunk.terrainIndex];
    const TerrainConfig &terrainConfig = terrainAsset.terrainConfig;
    const TerrainGrassConfig &grassConfig = grassAsset.terrainGrassConfig;
    const float density = std::max(grassConfig.density, 0.0f);
    if (density <= 1e-6f) {
      return;
    }

    const float spacing = std::max(std::sqrt(1.0f / density), 0.15f);
    const float halfSizeX = terrainConfig.sizeX * 0.5f;
    const float halfSizeZ = terrainConfig.sizeZ * 0.5f;
    const float startX = gridStart(halfSizeX, spacing);
    const float startZ = gridStart(halfSizeZ, spacing);
    const float jitterAmplitude =
        0.5f * spacing * glm::clamp(grassConfig.placementJitter, 0.0f, 1.0f);
    const int minCellX = static_cast<int>(
        std::floor((chunk.chunkMinX - jitterAmplitude - startX) / spacing));
    const int maxCellX = static_cast<int>(
        std::ceil((chunk.chunkMaxX + jitterAmplitude - startX) / spacing));
    const int minCellZ = static_cast<int>(
        std::floor((chunk.chunkMinZ - jitterAmplitude - startZ) / spacing));
    const int maxCellZ = static_cast<int>(
        std::ceil((chunk.chunkMaxZ + jitterAmplitude - startZ) / spacing));
    const bool includeMaxX = chunk.chunkMaxX >= halfSizeX;
    const bool includeMaxZ = chunk.chunkMaxZ >= halfSizeZ;
    const uint32_t seed = grassConfig.scatterSeed;

    const size_t estimatedCount =
        static_cast<size_t>(std::max(maxCellX - minCellX + 1, 0)) *
        static_cast<size_t>(std::max(maxCellZ - minCellZ + 1, 0));
    chunk.instanceTransforms.reserve(estimatedCount);

    for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
      for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
        const float nominalX = startX + static_cast<float>(cellX) * spacing;
        const float nominalZ = startZ + static_cast<float>(cellZ) * spacing;
        const float jitterX = randomSignedUnit(seed, nominalX, nominalZ, 17.0f);
        const float jitterZ = randomSignedUnit(seed, nominalX, nominalZ, 53.0f);
        const float localX = nominalX + jitterX * jitterAmplitude;
        const float localZ = nominalZ + jitterZ * jitterAmplitude;

        if (localX < -halfSizeX || localX > halfSizeX ||
            localZ < -halfSizeZ || localZ > halfSizeZ) {
          continue;
        }
        if (localX < chunk.chunkMinX ||
            (!includeMaxX && localX >= chunk.chunkMaxX) ||
            (includeMaxX && localX > chunk.chunkMaxX) ||
            localZ < chunk.chunkMinZ ||
            (!includeMaxZ && localZ >= chunk.chunkMaxZ) ||
            (includeMaxZ && localZ > chunk.chunkMaxZ)) {
          continue;
        }

        const glm::vec3 normal =
            TerrainQueries::sampleLocalNormal(terrainConfig, localX, localZ);
        const float slopeCos = glm::clamp(
            glm::dot(safeNormalized(normal, glm::vec3(0.0f, 1.0f, 0.0f)),
                     glm::vec3(0.0f, 1.0f, 0.0f)),
            -1.0f, 1.0f);
        if (glm::degrees(std::acos(slopeCos)) > grassConfig.maxSlopeDegrees) {
          continue;
        }

        const float localHeight =
            TerrainGenerator::sampleHeight(terrainConfig, localX, localZ);
        const float yaw = glm::two_pi<float>() *
                          randomUnit(seed, localX, localZ, 131.0f);
        const float scale =
            glm::mix(0.85f, 1.15f,
                     randomUnit(seed, localX, localZ, 277.0f));
        chunk.instanceTransforms.push_back(grassInstanceTransform(
            glm::vec3(localX, localHeight, localZ), normal, yaw, scale));
      }
    }
  }

  static glm::vec3 safeNormalized(const glm::vec3 &value,
                                  const glm::vec3 &fallback) {
    return glm::length(value) > 1e-6f ? glm::normalize(value) : fallback;
  }

  static void appendTriangle(std::vector<ImportedGeometryVertex> &vertices,
                             std::vector<uint32_t> &indices,
                             const glm::vec3 &a, const glm::vec3 &b,
                             const glm::vec3 &c, const glm::vec3 &normal,
                             const glm::vec4 &colorA,
                             const glm::vec4 &colorB,
                             const glm::vec4 &colorC) {
    const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    vertices.push_back(ImportedGeometryVertex{
        .pos = a,
        .normal = normal,
        .texCoord = {0.0f, 1.0f},
        .color = colorA,
    });
    vertices.push_back(ImportedGeometryVertex{
        .pos = b,
        .normal = normal,
        .texCoord = {1.0f, 1.0f},
        .color = colorB,
    });
    vertices.push_back(ImportedGeometryVertex{
        .pos = c,
        .normal = normal,
        .texCoord = {0.5f, 0.0f},
        .color = colorC,
    });
    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 2);
  }

  static void appendBlade(std::vector<ImportedGeometryVertex> &vertices,
                          std::vector<uint32_t> &indices,
                          const glm::vec3 &rootPosition,
                          const glm::vec3 &up, const glm::vec3 &forward,
                          const glm::vec4 &terrainColor, float width,
                          float height, float leanRadians) {
    const glm::vec3 widthDirection =
        safeNormalized(glm::cross(up, forward), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 leanDirection =
        rotateAroundAxis(forward, up, leanRadians * 0.35f);
    const glm::vec3 tipPosition =
        rootPosition + up * height + leanDirection * (height * std::tan(leanRadians));
    const glm::vec3 baseLeft = rootPosition - widthDirection * (width * 0.5f);
    const glm::vec3 baseRight = rootPosition + widthDirection * (width * 0.5f);
    const glm::vec4 bladeColor = terrainColor;
    const glm::vec3 frontNormal = safeNormalized(
        glm::cross(baseRight - baseLeft, tipPosition - baseLeft), up);

    appendTriangle(vertices, indices, baseLeft, baseRight, tipPosition,
                   frontNormal, bladeColor, bladeColor, bladeColor);
    appendTriangle(vertices, indices, baseRight, baseLeft, tipPosition,
                   -frontNormal, bladeColor, bladeColor, bladeColor);
  }

};
