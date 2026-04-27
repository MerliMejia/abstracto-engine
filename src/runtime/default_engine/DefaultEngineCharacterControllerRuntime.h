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
  bool &toolMousePressed;
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

  static void applyStartPositions(
      DefaultEngineCharacterControllerRuntimeContext &context) {
    const size_t characterCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    bool anyChanged = false;
    for (size_t index = 0; index < characterCount; ++index) {
      if (context.sceneAssets[index].kind !=
          SceneAssetKind::CharacterController) {
        continue;
      }

      SceneAssetInstance &characterAsset = context.sceneAssets[index];
      if (!characterAsset.characterControllerConfig.useStartPosition) {
        continue;
      }

      characterAsset.characterControllerState.position =
          characterAsset.characterControllerConfig.startPosition;
      characterAsset.characterControllerState.velocity = glm::vec3(0.0f);
      characterAsset.transform.position =
          characterAsset.characterControllerConfig.startPosition;
      context.debugUiSettings.sceneObjects[index].transform.position =
          characterAsset.characterControllerConfig.startPosition;
      snapCharacterControllerToTerrain(context, index);
      anyChanged = true;
    }

    if (anyChanged) {
      context.sceneDefinition.assets = context.sceneAssets;
    }
  }

  static bool hasActiveMouseTool(
      const DefaultEngineCharacterControllerRuntimeContext &context) {
    const auto characterIndex = activeCharacterControllerIndex(context);
    if (!characterIndex.has_value()) {
      return false;
    }
    const CharacterControllerConfig &config =
        context.sceneAssets[*characterIndex].characterControllerConfig;
    return config.startPlacementMode || config.limitEditMode;
  }

  static void updateMouseTools(
      DefaultEngineCharacterControllerRuntimeContext &context,
      const glm::mat4 &view, const glm::mat4 &proj) {
    const auto characterIndex = activeCharacterControllerIndex(context);
    if (!characterIndex.has_value()) {
      context.toolMousePressed = false;
      return;
    }

    SceneAssetInstance &characterAsset = context.sceneAssets[*characterIndex];
    CharacterControllerConfig &config =
        characterAsset.characterControllerConfig;
    if (!config.startPlacementMode && !config.limitEditMode) {
      context.toolMousePressed = false;
      return;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse && !context.debugUiSettings.cameraLookActive) {
      return;
    }

    const bool leftMouseDown =
        glfwGetMouseButton(context.window.handle(), GLFW_MOUSE_BUTTON_LEFT) ==
        GLFW_PRESS;
    if (!leftMouseDown) {
      context.toolMousePressed = false;
      return;
    }
    if (context.toolMousePressed) {
      return;
    }
    context.toolMousePressed = true;

    const auto hit = raycastVisibleTerrainFromCursor(context, view, proj);
    if (!hit.has_value()) {
      return;
    }

    const float supportOffset = config.radius + config.halfHeight;
    const glm::vec3 controllerPosition =
        hit->worldPosition + glm::vec3(0.0f, supportOffset, 0.0f);
    if (config.startPlacementMode) {
      config.useStartPosition = true;
      config.startPosition = controllerPosition;
      characterAsset.characterControllerState.position = controllerPosition;
      characterAsset.characterControllerState.velocity = glm::vec3(0.0f);
      characterAsset.characterControllerState.grounded = true;
      characterAsset.transform.position = controllerPosition;
      context.debugUiSettings.sceneObjects[*characterIndex].transform.position =
          controllerPosition;
      snapCharacterControllerToTerrain(context, *characterIndex);
      config.startPlacementMode = false;
    } else if (config.limitEditMode) {
      config.limitPoints.push_back(hit->worldPosition);
    }
    context.sceneDefinition.assets = context.sceneAssets;
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

    const glm::vec3 previousPosition =
        characterAsset.characterControllerState.position;
    const glm::vec3 desiredPosition =
        previousPosition + moveDirection * moveStep;
    characterAsset.characterControllerState.position =
        constrainedByLimits(characterAsset, previousPosition, desiredPosition);
    characterAsset.characterControllerState.velocity =
        moveDirection * characterAsset.characterControllerConfig.moveSpeed;
    characterAsset.transform.position =
        characterAsset.characterControllerState.position;
    characterObject.transform = characterAsset.transform;
    snapCharacterControllerToTerrain(context, *characterIndex);
    context.sceneDefinition.assets = context.sceneAssets;
  }

private:
  static std::optional<DefaultEngineTerrainSurfaceSample>
  raycastVisibleTerrainFromCursor(
      const DefaultEngineCharacterControllerRuntimeContext &context,
      const glm::mat4 &view, const glm::mat4 &proj) {
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(context.window.handle(), &cursorX, &cursorY);

    const WindowSize windowSize = context.window.windowSize();
    if (windowSize.width == 0 || windowSize.height == 0) {
      return std::nullopt;
    }

    const float ndcX =
        static_cast<float>((cursorX / static_cast<double>(windowSize.width)) *
                               2.0 -
                           1.0);
    const float ndcY =
        static_cast<float>((cursorY / static_cast<double>(windowSize.height)) *
                               2.0 -
                           1.0);
    const glm::mat4 inverseViewProj = glm::inverse(proj * view);
    const glm::vec4 nearWorld =
        inverseViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farWorld =
        inverseViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearWorld.w) <= 1e-6f || std::abs(farWorld.w) <= 1e-6f) {
      return std::nullopt;
    }

    const glm::vec3 rayNear = glm::vec3(nearWorld) / nearWorld.w;
    const glm::vec3 rayFar = glm::vec3(farWorld) / farWorld.w;
    const glm::vec3 rayDirection = glm::normalize(rayFar - rayNear);
    const glm::vec3 rayOrigin = context.debugUiSettings.cameraPosition;

    const size_t terrainCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    std::optional<DefaultEngineTerrainSurfaceSample> bestHit;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t index = 0; index < terrainCount; ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::Terrain ||
          !context.debugUiSettings.sceneObjects[index].visible) {
        continue;
      }

      const glm::mat4 terrainModel = AppSceneController::sceneTransformMatrix(
          context.debugUiSettings.sceneObjects[index].transform);
      const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
      const glm::vec3 localOrigin =
          glm::vec3(inverseTerrainModel * glm::vec4(rayOrigin, 1.0f));
      const glm::vec3 localDirection = glm::normalize(
          glm::vec3(inverseTerrainModel * glm::vec4(rayDirection, 0.0f)));
      const auto localHit =
          TerrainQueries::raycastLocalSurface(context.sceneAssets[index]
                                                  .terrainConfig,
                                              localOrigin, localDirection);
      if (!localHit.has_value()) {
        continue;
      }

      const glm::vec3 worldPoint =
          glm::vec3(terrainModel * glm::vec4(localHit->position, 1.0f));
      const float distance = glm::length(worldPoint - rayOrigin);
      if (bestHit.has_value() && distance >= bestDistance) {
        continue;
      }

      const glm::vec3 worldNormal = glm::normalize(
          glm::transpose(glm::inverse(glm::mat3(terrainModel))) *
          localHit->normal);
      bestHit = DefaultEngineTerrainSurfaceSample{
          .terrainIndex = index,
          .worldPosition = worldPoint,
          .worldNormal = worldNormal,
      };
      bestDistance = distance;
    }
    return bestHit;
  }

  static float cross2(const glm::vec2 &lhs, const glm::vec2 &rhs) {
    return lhs.x * rhs.y - lhs.y * rhs.x;
  }

  static bool segmentsIntersect(const glm::vec2 &a0, const glm::vec2 &a1,
                                const glm::vec2 &b0, const glm::vec2 &b1) {
    const glm::vec2 a = a1 - a0;
    const glm::vec2 b = b1 - b0;
    const float denominator = cross2(a, b);
    if (std::abs(denominator) <= 1e-6f) {
      return false;
    }

    const glm::vec2 offset = b0 - a0;
    const float t = cross2(offset, b) / denominator;
    const float u = cross2(offset, a) / denominator;
    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
  }

  static glm::vec3 constrainedByLimits(const SceneAssetInstance &characterAsset,
                                       const glm::vec3 &previousPosition,
                                       const glm::vec3 &desiredPosition) {
    const std::vector<glm::vec3> &limitPoints =
        characterAsset.characterControllerConfig.limitPoints;
    if (limitPoints.size() < 2) {
      return desiredPosition;
    }

    const glm::vec2 previous(previousPosition.x, previousPosition.z);
    const glm::vec2 desired(desiredPosition.x, desiredPosition.z);
    for (size_t index = 0; index + 1 < limitPoints.size(); ++index) {
      const glm::vec2 limitStart(limitPoints[index].x, limitPoints[index].z);
      const glm::vec2 limitEnd(limitPoints[index + 1].x,
                               limitPoints[index + 1].z);
      if (segmentsIntersect(previous, desired, limitStart, limitEnd)) {
        return previousPosition;
      }
    }
    return desiredPosition;
  }

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
