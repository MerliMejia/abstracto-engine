#pragma once

#include "backend/AppWindow.h"
#include "editor/DebugUIState.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include "world/TerrainQueries.h"
#include <GLFW/glfw3.h>
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
  AppWindow &window;
  glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
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
      characterAsset.characterControllerState.grounded = false;
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
    characterAsset.characterControllerState.grounded = true;
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

  static void updateGamePlay(DefaultEngineCharacterControllerRuntimeContext &context,
                             float deltaSeconds) {
    if (!engineLogicEnabled(context.debugUiSettings,
                            EngineLogicState::GamePlay)) {
      return;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) {
      return;
    }

    const std::optional<size_t> characterIndex = activeCharacterControllerIndex(
        context);
    if (!characterIndex.has_value()) {
      return;
    }

    SceneAssetInstance &characterAsset = context.sceneAssets[*characterIndex];
    SceneObject &characterObject =
        context.debugUiSettings.sceneObjects[*characterIndex];
    glm::vec3 moveInput(0.0f);
    if (glfwGetKey(context.window.handle(), GLFW_KEY_W) == GLFW_PRESS) {
      moveInput.z += 1.0f;
    }
    if (glfwGetKey(context.window.handle(), GLFW_KEY_S) == GLFW_PRESS) {
      moveInput.z -= 1.0f;
    }
    if (glfwGetKey(context.window.handle(), GLFW_KEY_D) == GLFW_PRESS) {
      moveInput.x += 1.0f;
    }
    if (glfwGetKey(context.window.handle(), GLFW_KEY_A) == GLFW_PRESS) {
      moveInput.x -= 1.0f;
    }

    if (glm::length(moveInput) <= 1e-4f) {
      characterAsset.characterControllerState.velocity = glm::vec3(0.0f);
      characterAsset.characterControllerState.grounded =
          sampleTerrainSurfaceAtWorldPosition(
              context, characterAsset.characterControllerState.position)
              .has_value();
      context.sceneDefinition.assets = context.sceneAssets;
      return;
    }

    moveInput = glm::normalize(moveInput);
    glm::vec3 forward(context.cameraForward.x, 0.0f, context.cameraForward.z);
    if (glm::length(forward) <= 1e-4f) {
      forward = glm::vec3(std::sin(characterAsset.characterControllerState.yawRadians),
                          0.0f,
                          -std::cos(characterAsset.characterControllerState.yawRadians));
    }
    forward = glm::normalize(forward);
    const glm::vec3 right =
        glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 moveDirection =
        glm::normalize((right * moveInput.x) + (forward * moveInput.z));
    const float moveStep =
        characterAsset.characterControllerConfig.moveSpeed * deltaSeconds;

    characterAsset.characterControllerState.position += moveDirection * moveStep;
    characterAsset.characterControllerState.velocity =
        moveDirection * characterAsset.characterControllerConfig.moveSpeed;
    characterAsset.characterControllerState.yawRadians =
        std::atan2(moveDirection.x, -moveDirection.z);
    characterAsset.transform.position =
        characterAsset.characterControllerState.position;
    characterAsset.transform.rotationDegrees.y =
        glm::degrees(characterAsset.characterControllerState.yawRadians);
    characterObject.transform = characterAsset.transform;
    snapCharacterControllerToTerrain(context, *characterIndex);
    context.sceneDefinition.assets = context.sceneAssets;
  }

private:
  static std::optional<size_t> activeCharacterControllerIndex(
      const DefaultEngineCharacterControllerRuntimeContext &context) {
    const int selectedIndex = context.debugUiSettings.selectedObjectIndex;
    const size_t objectCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < objectCount) {
      const size_t index = static_cast<size_t>(selectedIndex);
      if (context.sceneAssets[index].kind == SceneAssetKind::CharacterController &&
          context.debugUiSettings.sceneObjects[index].visible) {
        return index;
      }
    }

    for (size_t index = 0; index < objectCount; ++index) {
      if (context.sceneAssets[index].kind == SceneAssetKind::CharacterController &&
          context.debugUiSettings.sceneObjects[index].visible) {
        return index;
      }
    }
    return std::nullopt;
  }
};
