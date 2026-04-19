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
  glm::ivec2 chunkCoord{0, 0};
  glm::vec3 localCenter{0.0f};
  float localBoundingRadius = 0.0f;
  RenderableModel model;
};

struct DefaultEngineTerrainGrassRuntimeContext {
  std::vector<SceneAssetInstance> &sceneAssets;
  const DefaultDebugUISettings &debugUiSettings;
  std::vector<DefaultEngineTerrainGrassChunkState> &grassChunks;
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

          auto chunkAsset = std::make_unique<TerrainGrassChunkModelAsset>();
          const GeneratedGrassChunk chunkGeometry = buildChunkGeometry(
              terrainAsset.terrainConfig, grassConfig, chunkMinX, chunkMaxX,
              chunkMinZ, chunkMaxZ);
          if (chunkGeometry.indices.empty()) {
            continue;
          }

          chunkAsset->setChunkGeometry(
              std::move(chunkGeometry.vertices), std::move(chunkGeometry.indices),
              chunkGeometry.material,
              grassChunkLabel(grassAsset, chunkX, chunkZ));

          DefaultEngineTerrainGrassChunkState chunkState{
              .grassAssetIndex = grassAssetIndex,
              .terrainIndex = terrainIndex,
              .chunkCoord = {chunkX, chunkZ},
              .localCenter = chunkGeometry.localCenter,
              .localBoundingRadius = chunkGeometry.localBoundingRadius,
          };
          chunkState.model.loadFromAsset(
              std::move(chunkAsset), context.backend.commands(),
              context.backend.device(), sceneDescriptorSetLayout, nullptr,
              context.frameGeometryUniforms, context.sampler,
              DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
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
    for (auto &chunk : context.grassChunks) {
      if (chunk.grassAssetIndex >= context.sceneAssets.size() ||
          chunk.terrainIndex >= context.sceneAssets.size()) {
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

      const glm::vec3 toChunk = worldCenter - cameraPosition;
      const float forwardDistance = glm::dot(toChunk, normalizedCameraForward);
      if (forwardDistance < -worldRadius ||
          forwardDistance > drawDistance + worldRadius) {
        continue;
      }

      const auto chunkItems =
          chunk.model.buildRenderItems(deviceContext, geometryPass, {terrainModel});
      renderItems.insert(renderItems.end(), chunkItems.begin(), chunkItems.end());
    }
  }

private:
  struct ResolvedTerrainTarget {
    size_t terrainIndex = 0;
  };

  struct GeneratedGrassChunk {
    std::vector<ImportedGeometryVertex> vertices;
    std::vector<uint32_t> indices;
    ImportedMaterialData material;
    glm::vec3 localCenter{0.0f};
    float localBoundingRadius = 0.0f;
  };

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

  static std::string grassChunkLabel(const SceneAssetInstance &grassAsset,
                                     int chunkX, int chunkZ) {
    const std::string baseLabel =
        grassAsset.name.empty() ? "terrain_grass" : grassAsset.name;
    return baseLabel + "_chunk_" + std::to_string(chunkX) + "_" +
           std::to_string(chunkZ);
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

  static GeneratedGrassChunk buildChunkGeometry(
      const TerrainConfig &terrainConfig, const TerrainGrassConfig &grassConfig,
      float chunkMinX, float chunkMaxX, float chunkMinZ, float chunkMaxZ) {
    GeneratedGrassChunk chunk;
    chunk.material = ImportedMaterialData{
        .name = "Terrain Grass",
        .baseColorFactor = glm::vec4(1.0f),
        .metallicFactor = 0.0f,
        .roughnessFactor = 1.0f,
        .occlusionStrength = 1.0f,
    };

    const float density = std::max(grassConfig.density, 0.0f);
    if (density <= 1e-6f) {
      return chunk;
    }

    const float spacing = std::max(std::sqrt(1.0f / density), 0.15f);
    const float halfSizeX = terrainConfig.sizeX * 0.5f;
    const float halfSizeZ = terrainConfig.sizeZ * 0.5f;
    const float startX = gridStart(halfSizeX, spacing);
    const float startZ = gridStart(halfSizeZ, spacing);
    const float jitterAmplitude =
        0.5f * spacing * glm::clamp(grassConfig.placementJitter, 0.0f, 1.0f);
    const int minCellX = static_cast<int>(
        std::floor((chunkMinX - jitterAmplitude - startX) / spacing));
    const int maxCellX = static_cast<int>(
        std::ceil((chunkMaxX + jitterAmplitude - startX) / spacing));
    const int minCellZ = static_cast<int>(
        std::floor((chunkMinZ - jitterAmplitude - startZ) / spacing));
    const int maxCellZ = static_cast<int>(
        std::ceil((chunkMaxZ + jitterAmplitude - startZ) / spacing));

    const float centerX = 0.5f * (chunkMinX + chunkMaxX);
    const float centerZ = 0.5f * (chunkMinZ + chunkMaxZ);
    chunk.localCenter = glm::vec3(
        centerX, TerrainGenerator::sampleHeight(terrainConfig, centerX, centerZ),
        centerZ);
    chunk.localBoundingRadius =
        std::sqrt(std::pow(0.5f * (chunkMaxX - chunkMinX), 2.0f) +
                  std::pow(0.5f * (chunkMaxZ - chunkMinZ), 2.0f) +
                  std::pow(grassConfig.bladeHeightRange.y, 2.0f)) +
        grassConfig.clumpRadius;

    const uint32_t clumpSeed = grassConfig.scatterSeed;
    const uint32_t bladesPerClump =
        std::max(grassConfig.bladesPerClump, 1u);
    const bool includeMaxX = chunkMaxX >= halfSizeX;
    const bool includeMaxZ = chunkMaxZ >= halfSizeZ;

    for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
      for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
        const float nominalX = startX + static_cast<float>(cellX) * spacing;
        const float nominalZ = startZ + static_cast<float>(cellZ) * spacing;
        const float jitterX =
            randomSignedUnit(clumpSeed, nominalX, nominalZ, 17.0f);
        const float jitterZ =
            randomSignedUnit(clumpSeed, nominalX, nominalZ, 53.0f);
        const float localX = nominalX + jitterX * jitterAmplitude;
        const float localZ = nominalZ + jitterZ * jitterAmplitude;

        if (localX < -halfSizeX || localX > halfSizeX ||
            localZ < -halfSizeZ || localZ > halfSizeZ) {
          continue;
        }
        if (localX < chunkMinX || (!includeMaxX && localX >= chunkMaxX) ||
            (includeMaxX && localX > chunkMaxX) || localZ < chunkMinZ ||
            (!includeMaxZ && localZ >= chunkMaxZ) ||
            (includeMaxZ && localZ > chunkMaxZ)) {
          continue;
        }

        const glm::vec4 terrainColor =
            TerrainGenerator::sampleColor(terrainConfig, localX, localZ);

        const glm::vec3 localNormal =
            TerrainQueries::sampleLocalNormal(terrainConfig, localX, localZ);
        const float slopeCos = glm::clamp(
            glm::dot(safeNormalized(localNormal, glm::vec3(0.0f, 1.0f, 0.0f)),
                     glm::vec3(0.0f, 1.0f, 0.0f)),
            -1.0f, 1.0f);
        const float slopeDegrees = glm::degrees(std::acos(slopeCos));
        if (slopeDegrees > grassConfig.maxSlopeDegrees) {
          continue;
        }

        const float localHeight =
            TerrainGenerator::sampleHeight(terrainConfig, localX, localZ);
        const glm::vec3 rootPosition(localX, localHeight, localZ);
        const glm::vec3 up =
            safeNormalized(localNormal, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 fallbackForward =
            std::abs(glm::dot(up, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 clumpForward =
            safeNormalized(glm::cross(up, glm::cross(fallbackForward, up)),
                           glm::vec3(0.0f, 0.0f, 1.0f));
        const float baseYaw =
            glm::two_pi<float>() *
            randomUnit(clumpSeed, localX, localZ, 131.0f);

        for (uint32_t bladeIndex = 0; bladeIndex < bladesPerClump; ++bladeIndex) {
          const float bladeAlpha =
              static_cast<float>(bladeIndex) /
              static_cast<float>(bladesPerClump);
          const float bladeYaw =
              baseYaw + bladeAlpha * glm::two_pi<float>() +
              randomSignedUnit(clumpSeed, localX + bladeAlpha, localZ, 149.0f) *
                  0.3f;
          const glm::vec3 facingDirection =
              rotateAroundAxis(clumpForward, up, bladeYaw);
          const glm::vec3 clumpOffsetDirection = rotateAroundAxis(
              facingDirection, up,
              randomSignedUnit(clumpSeed, localX, localZ + bladeAlpha, 173.0f) *
                  0.4f);
          const float clumpOffsetAmount =
              grassConfig.clumpRadius *
              randomUnit(clumpSeed, localX + bladeAlpha, localZ, 191.0f);
          const glm::vec3 bladeRoot =
              rootPosition + clumpOffsetDirection * clumpOffsetAmount;
          const float bladeWidth = randomRange(
              grassConfig.bladeWidthRange.x, grassConfig.bladeWidthRange.y,
              randomUnit(clumpSeed, localX + bladeAlpha, localZ, 211.0f));
          const float bladeHeight = randomRange(
              grassConfig.bladeHeightRange.x, grassConfig.bladeHeightRange.y,
              randomUnit(clumpSeed, localX + bladeAlpha, localZ, 223.0f));
          const float leanRadians = glm::radians(randomUnit(
              clumpSeed, localX + bladeAlpha, localZ, 241.0f) *
                                                grassConfig.randomLeanDegrees);

          appendBlade(chunk.vertices, chunk.indices, bladeRoot, up, facingDirection,
                      terrainColor, bladeWidth, bladeHeight, leanRadians);
        }
      }
    }

    return chunk;
  }
};
