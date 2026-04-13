#pragma once

#include "editor/DebugUIState.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include "world/TerrainQueries.h"
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

struct DefaultEngineTerrainSurfaceSample {
  size_t terrainIndex = 0;
  glm::vec3 worldPosition{0.0f};
  glm::vec3 worldNormal{0.0f, 1.0f, 0.0f};
};

struct DefaultEngineCharacterControllerRuntimeContext {
  SceneDefinition &sceneDefinition;
  std::vector<SceneAssetInstance> &sceneAssets;
  DefaultDebugUISettings &debugUiSettings;
};

class DefaultEngineCharacterControllerRuntime {
public:
  static std::optional<DefaultEngineTerrainSurfaceSample>
  sampleTerrainSurfaceAtWorldPosition(
      const DefaultEngineCharacterControllerRuntimeContext &context,
      const glm::vec3 &worldPosition) {
    const size_t terrainCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    std::optional<DefaultEngineTerrainSurfaceSample> bestSample;
    float bestDistance = std::numeric_limits<float>::max();

    for (size_t index = 0; index < terrainCount; ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::Terrain ||
          !context.debugUiSettings.sceneObjects[index].visible) {
        continue;
      }

      const glm::mat4 terrainModel = AppSceneController::sceneTransformMatrix(
          context.debugUiSettings.sceneObjects[index].transform);
      const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
      const glm::vec3 localPosition =
          glm::vec3(inverseTerrainModel * glm::vec4(worldPosition, 1.0f));
      const TerrainConfig &terrainConfig = context.sceneAssets[index].terrainConfig;
      const float halfSizeX = terrainConfig.sizeX * 0.5f;
      const float halfSizeZ = terrainConfig.sizeZ * 0.5f;
      if (localPosition.x < -halfSizeX || localPosition.x > halfSizeX ||
          localPosition.z < -halfSizeZ || localPosition.z > halfSizeZ) {
        continue;
      }

      const float localHeight = TerrainGenerator::sampleHeight(
          terrainConfig, localPosition.x, localPosition.z);
      const glm::vec3 localSurface(localPosition.x, localHeight, localPosition.z);
      const glm::vec3 worldSurface =
          glm::vec3(terrainModel * glm::vec4(localSurface, 1.0f));
      const glm::vec3 localNormal =
          TerrainQueries::sampleLocalNormal(terrainConfig, localSurface.x,
                                            localSurface.z);
      const glm::vec3 worldNormal = glm::normalize(
          glm::transpose(glm::inverse(glm::mat3(terrainModel))) * localNormal);
      const float distance = std::abs(worldPosition.y - worldSurface.y);
      if (bestSample.has_value() && distance >= bestDistance) {
        continue;
      }

      bestSample = DefaultEngineTerrainSurfaceSample{
          .terrainIndex = index,
          .worldPosition = worldSurface,
          .worldNormal = worldNormal,
      };
      bestDistance = distance;
    }

    return bestSample;
  }

  static bool snapCharacterControllerToTerrain(
      DefaultEngineCharacterControllerRuntimeContext &context,
      size_t characterIndex) {
    if (characterIndex >= context.sceneAssets.size() ||
        characterIndex >= context.debugUiSettings.sceneObjects.size() ||
        context.sceneAssets[characterIndex].kind !=
            SceneAssetKind::CharacterController) {
      return false;
    }

    SceneAssetInstance &characterAsset = context.sceneAssets[characterIndex];
    SceneObject &characterObject =
        context.debugUiSettings.sceneObjects[characterIndex];
    const auto surfaceSample = sampleTerrainSurfaceAtWorldPosition(
        context, characterAsset.characterControllerState.position);
    if (!surfaceSample.has_value()) {
      return false;
    }

    const float supportOffset = characterAsset.characterControllerConfig.radius +
                                characterAsset.characterControllerConfig.halfHeight;
    const glm::vec3 snappedPosition =
        surfaceSample->worldPosition + glm::vec3(0.0f, supportOffset, 0.0f);
    const bool changed =
        !glm::all(glm::epsilonEqual(characterAsset.characterControllerState.position,
                                    snappedPosition, 1e-4f));
    characterAsset.characterControllerState.position = snappedPosition;
    characterAsset.transform.position = snappedPosition;
    characterObject.transform.position = snappedPosition;
    if (changed) {
      context.sceneDefinition.assets = context.sceneAssets;
    }
    return changed;
  }

  static void updateTerrainAnchors(
      DefaultEngineCharacterControllerRuntimeContext &context) {
    bool anyChanged = false;
    const size_t characterCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    for (size_t index = 0; index < characterCount; ++index) {
      anyChanged |= snapCharacterControllerToTerrain(context, index);
    }
    if (anyChanged) {
      context.sceneDefinition.assets = context.sceneAssets;
    }
  }
};
