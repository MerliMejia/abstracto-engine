#pragma once

#include "backend/AppWindow.h"
#include "editor/DebugUIState.h"
#include "passes/DebugOverlayPass.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include "resources/Mesh.h"
#include "world/Terrain.h"
#include "world/TerrainQueries.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct DefaultEngineInstancedObjectRuntimeContext {
  SceneDefinition &sceneDefinition;
  std::vector<SceneAssetInstance> &sceneAssets;
  DefaultDebugUISettings &debugUiSettings;
  AppWindow &window;
  DebugOverlayPass *debugOverlayPass = nullptr;
  TypedMesh<Vertex> &terrainBrushIndicatorMesh;
};

class DefaultEngineInstancedObjectRuntime {
public:
  static bool hasActivePaintTool(
      const DefaultEngineInstancedObjectRuntimeContext &context) {
    return activePaintObjectIndex(context).has_value();
  }

  static bool updateTerrainPaintTool(
      DefaultEngineInstancedObjectRuntimeContext &context, const glm::mat4 &view,
      const glm::mat4 &proj, float deltaSeconds) {
    const auto activeObjectIndex = activePaintObjectIndex(context);
    if (!activeObjectIndex.has_value()) {
      return false;
    }

    SceneAssetInstance &instancedObject =
        context.sceneAssets[*activeObjectIndex];
    updateBrushControls(context, instancedObject, deltaSeconds);
    const auto hit =
        raycastTerrainFromCursor(context, *activeObjectIndex, view, proj);
    applyBrushStroke(context, *activeObjectIndex, hit);
    updateToolOverlay(context, hit, instancedObject);
    return true;
  }

  static bool fillTerrain(DefaultEngineInstancedObjectRuntimeContext &context,
                          size_t instancedObjectIndex) {
    auto resolved = resolveInstancedObjectTerrain(context, instancedObjectIndex);
    if (!resolved.has_value()) {
      return false;
    }

    SceneAssetInstance &instancedObject =
        context.sceneAssets[instancedObjectIndex];
    const size_t terrainIndex = resolved->terrainIndex;
    const SceneAssetInstance &terrainAsset = context.sceneAssets[terrainIndex];
    const glm::mat4 terrainModel =
        terrainModelMatrix(context, terrainIndex);
    const glm::mat3 terrainNormalMatrix =
        glm::transpose(glm::inverse(glm::mat3(terrainModel)));

    const float halfSizeX = terrainAsset.terrainConfig.sizeX * 0.5f;
    const float halfSizeZ = terrainAsset.terrainConfig.sizeZ * 0.5f;
    const float spacing = std::max(instancedObject.instanceSpacing, 0.05f);
    const float jitterAmplitude =
        0.5f * spacing * glm::clamp(instancedObject.instanceJitter, 0.0f, 1.0f);

    std::vector<SceneTransform> generated;
    const float startX = halfSizeX <= spacing * 0.5f ? 0.0f : -halfSizeX + spacing * 0.5f;
    const float startZ = halfSizeZ <= spacing * 0.5f ? 0.0f : -halfSizeZ + spacing * 0.5f;

    for (float z = startZ; z <= halfSizeZ; z += spacing) {
      for (float x = startX; x <= halfSizeX; x += spacing) {
        const glm::vec2 jitter = {
            randomSignedUnit(instancedObject.instanceScatterSeed, x, z, 17.0f),
            randomSignedUnit(instancedObject.instanceScatterSeed, x, z, 53.0f)};
        const float localX = glm::clamp(x + jitter.x * jitterAmplitude,
                                        -halfSizeX, halfSizeX);
        const float localZ = glm::clamp(z + jitter.y * jitterAmplitude,
                                        -halfSizeZ, halfSizeZ);
        if (!passesSlopeFilter(instancedObject, terrainAsset.terrainConfig, localX,
                               localZ)) {
          continue;
        }

        generated.push_back(buildPlacementTransform(
            instancedObject, terrainAsset.terrainConfig, terrainModel,
            terrainNormalMatrix, localX, localZ));
      }
    }

    if (sameTransformList(instancedObject.instanceTransforms, generated)) {
      instancedObject.targetTerrainName = resolved->terrainName;
      return false;
    }

    instancedObject.targetTerrainName = resolved->terrainName;
    instancedObject.instanceTransforms = std::move(generated);
    context.sceneDefinition.assets = context.sceneAssets;
    return true;
  }

  static bool reprojectTerrain(
      DefaultEngineInstancedObjectRuntimeContext &context,
      size_t instancedObjectIndex) {
    auto resolved = resolveInstancedObjectTerrain(context, instancedObjectIndex);
    if (!resolved.has_value()) {
      return false;
    }

    SceneAssetInstance &instancedObject =
        context.sceneAssets[instancedObjectIndex];
    if (instancedObject.instanceTransforms.empty()) {
      instancedObject.targetTerrainName = resolved->terrainName;
      context.sceneDefinition.assets = context.sceneAssets;
      return false;
    }

    const size_t terrainIndex = resolved->terrainIndex;
    const SceneAssetInstance &terrainAsset = context.sceneAssets[terrainIndex];
    const glm::mat4 terrainModel = terrainModelMatrix(context, terrainIndex);
    const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
    const glm::mat3 terrainNormalMatrix =
        glm::transpose(glm::inverse(glm::mat3(terrainModel)));

    bool changed = false;
    for (auto &instanceTransform : instancedObject.instanceTransforms) {
      const glm::vec3 worldPosition = instanceTransform.position;
      const glm::vec3 terrainLocalPosition =
          glm::vec3(inverseTerrainModel * glm::vec4(worldPosition, 1.0f));
      if (!insideTerrainBounds(terrainAsset.terrainConfig, terrainLocalPosition.x,
                               terrainLocalPosition.z)) {
        continue;
      }
      if (!passesSlopeFilter(instancedObject, terrainAsset.terrainConfig,
                             terrainLocalPosition.x, terrainLocalPosition.z)) {
        continue;
      }

      const SceneTransform projected =
          buildPlacementTransform(instancedObject, terrainAsset.terrainConfig,
                                  terrainModel, terrainNormalMatrix,
                                  terrainLocalPosition.x, terrainLocalPosition.z);
      if (sameTransform(projected, instanceTransform)) {
        continue;
      }
      instanceTransform = projected;
      changed = true;
    }

    instancedObject.targetTerrainName = resolved->terrainName;
    if (changed) {
      context.sceneDefinition.assets = context.sceneAssets;
    }
    return changed;
  }

private:
  struct ResolvedTerrainTarget {
    size_t terrainIndex = 0;
    std::string terrainName;
  };

  struct PaintHit {
    size_t instancedObjectIndex = 0;
    size_t terrainIndex = 0;
    glm::vec3 localPosition{0.0f};
    glm::vec3 worldPosition{0.0f};
    glm::vec3 worldNormal{0.0f, 1.0f, 0.0f};
  };

  static constexpr uint64_t cellKey(int x, int z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint32_t>(z);
  }

  static std::optional<size_t> activePaintObjectIndex(
      const DefaultEngineInstancedObjectRuntimeContext &context) {
    const int selectedIndex = context.debugUiSettings.selectedObjectIndex;
    if (selectedIndex < 0 ||
        static_cast<size_t>(selectedIndex) >= context.sceneAssets.size() ||
        static_cast<size_t>(selectedIndex) >=
            context.debugUiSettings.sceneObjects.size()) {
      return std::nullopt;
    }

    const size_t objectIndex = static_cast<size_t>(selectedIndex);
    const SceneAssetInstance &sceneAsset = context.sceneAssets[objectIndex];
    if (sceneAsset.kind != SceneAssetKind::InstancedObject ||
        !sceneAsset.instancePaintMode || sceneAsset.assetPath.empty() ||
        !context.debugUiSettings.sceneObjects[objectIndex].visible) {
      return std::nullopt;
    }

    auto resolved = resolveInstancedObjectTerrain(
        const_cast<DefaultEngineInstancedObjectRuntimeContext &>(context),
        objectIndex);
    if (!resolved.has_value() ||
        resolved->terrainIndex >= context.debugUiSettings.sceneObjects.size() ||
        !context.debugUiSettings.sceneObjects[resolved->terrainIndex].visible) {
      return std::nullopt;
    }

    return objectIndex;
  }

  static std::optional<ResolvedTerrainTarget>
  resolveInstancedObjectTerrain(DefaultEngineInstancedObjectRuntimeContext &context,
                                size_t instancedObjectIndex) {
    if (instancedObjectIndex >= context.sceneAssets.size()) {
      return std::nullopt;
    }

    SceneAssetInstance &instancedObject = context.sceneAssets[instancedObjectIndex];
    if (instancedObject.kind != SceneAssetKind::InstancedObject) {
      return std::nullopt;
    }

    std::optional<ResolvedTerrainTarget> firstTerrain;
    for (size_t index = 0; index < context.sceneAssets.size(); ++index) {
      const SceneAssetInstance &sceneAsset = context.sceneAssets[index];
      if (sceneAsset.kind != SceneAssetKind::Terrain) {
        continue;
      }

      const std::string terrainName =
          AppSceneController::sceneAssetName(sceneAsset, index);
      if (!firstTerrain.has_value()) {
        firstTerrain = ResolvedTerrainTarget{
            .terrainIndex = index,
            .terrainName = terrainName,
        };
      }
      if (!instancedObject.targetTerrainName.empty() &&
          instancedObject.targetTerrainName == terrainName) {
        return ResolvedTerrainTarget{
            .terrainIndex = index,
            .terrainName = terrainName,
        };
      }
    }

    return firstTerrain;
  }

  static void updateBrushControls(
      DefaultEngineInstancedObjectRuntimeContext &context,
      SceneAssetInstance &instancedObject, float deltaSeconds) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) {
      return;
    }

    bool changed = false;
    const float radiusStep = std::max(deltaSeconds * 4.0f, 0.02f);
    if (glfwGetKey(context.window.handle(), GLFW_KEY_UP) == GLFW_PRESS) {
      instancedObject.instanceBrushRadius =
          std::min(instancedObject.instanceBrushRadius + radiusStep, 128.0f);
      changed = true;
    }
    if (glfwGetKey(context.window.handle(), GLFW_KEY_DOWN) == GLFW_PRESS) {
      instancedObject.instanceBrushRadius =
          std::max(instancedObject.instanceBrushRadius - radiusStep, 0.05f);
      changed = true;
    }
    if (glfwGetKey(context.window.handle(), GLFW_KEY_LEFT) == GLFW_PRESS &&
        instancedObject.instanceEraseMode) {
      instancedObject.instanceEraseMode = false;
      changed = true;
    }
    if (glfwGetKey(context.window.handle(), GLFW_KEY_RIGHT) == GLFW_PRESS &&
        !instancedObject.instanceEraseMode) {
      instancedObject.instanceEraseMode = true;
      changed = true;
    }

    if (changed) {
      context.sceneDefinition.assets = context.sceneAssets;
    }
  }

  static float brushRadius(const SceneAssetInstance &instancedObject) {
    return std::max(instancedObject.instanceBrushRadius, 0.05f);
  }

  static float gridStart(float halfSize, float spacing) {
    return halfSize <= spacing * 0.5f ? 0.0f : -halfSize + spacing * 0.5f;
  }

  static std::optional<PaintHit> raycastTerrainFromCursor(
      DefaultEngineInstancedObjectRuntimeContext &context,
      size_t instancedObjectIndex, const glm::mat4 &view,
      const glm::mat4 &proj) {
    auto resolved = resolveInstancedObjectTerrain(context, instancedObjectIndex);
    if (!resolved.has_value()) {
      return std::nullopt;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse && !context.debugUiSettings.cameraLookActive) {
      return std::nullopt;
    }

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

    const glm::vec3 rayOrigin = context.debugUiSettings.cameraPosition;
    const glm::vec3 rayNear = glm::vec3(nearWorld) / nearWorld.w;
    const glm::vec3 rayFar = glm::vec3(farWorld) / farWorld.w;
    const glm::vec3 rayDirection = glm::normalize(rayFar - rayNear);

    const SceneAssetInstance &terrainAsset =
        context.sceneAssets[resolved->terrainIndex];
    const glm::mat4 terrainModel =
        terrainModelMatrix(context, resolved->terrainIndex);
    const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
    const glm::vec3 localOrigin =
        glm::vec3(inverseTerrainModel * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 localDirection = glm::normalize(
        glm::vec3(inverseTerrainModel * glm::vec4(rayDirection, 0.0f)));
    const auto localHit =
        TerrainQueries::raycastLocalSurface(terrainAsset.terrainConfig,
                                            localOrigin, localDirection);
    if (!localHit.has_value()) {
      return std::nullopt;
    }

    const glm::vec3 worldPoint =
        glm::vec3(terrainModel * glm::vec4(localHit->position, 1.0f));
    const glm::vec3 worldNormal = glm::normalize(
        glm::transpose(glm::inverse(glm::mat3(terrainModel))) * localHit->normal);
    return PaintHit{
        .instancedObjectIndex = instancedObjectIndex,
        .terrainIndex = resolved->terrainIndex,
        .localPosition = localHit->position,
        .worldPosition = worldPoint,
        .worldNormal = worldNormal,
    };
  }

  static glm::mat4 terrainModelMatrix(
      const DefaultEngineInstancedObjectRuntimeContext &context,
      size_t terrainIndex) {
    if (terrainIndex < context.debugUiSettings.sceneObjects.size()) {
      return AppSceneController::sceneTransformMatrix(
          context.debugUiSettings.sceneObjects[terrainIndex].transform);
    }
    return AppSceneController::sceneTransformMatrix(
        context.sceneAssets[terrainIndex].transform);
  }

  static bool insideTerrainBounds(const TerrainConfig &config, float x, float z) {
    const float halfSizeX = config.sizeX * 0.5f;
    const float halfSizeZ = config.sizeZ * 0.5f;
    return x >= -halfSizeX && x <= halfSizeX && z >= -halfSizeZ &&
           z <= halfSizeZ;
  }

  static int nearestCellIndex(float coordinate, float start, float spacing) {
    return static_cast<int>(std::llround((coordinate - start) / spacing));
  }

  static float cellCoordinate(int cellIndex, float start, float spacing) {
    return start + static_cast<float>(cellIndex) * spacing;
  }

  static bool passesSlopeFilter(const SceneAssetInstance &instancedObject,
                                const TerrainConfig &terrainConfig, float x,
                                float z) {
    const glm::vec3 normal = TerrainQueries::sampleLocalNormal(terrainConfig, x, z);
    const float slopeCos =
        glm::clamp(glm::dot(glm::normalize(normal), glm::vec3(0.0f, 1.0f, 0.0f)),
                   -1.0f, 1.0f);
    const float slopeDegrees = glm::degrees(std::acos(slopeCos));
    return slopeDegrees <= instancedObject.instanceMaxSlopeDegrees;
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

  static float centeredRandomValue(uint32_t seed, float x, float z, float salt,
                                   float range) {
    return randomSignedUnit(seed, x, z, salt) * range * 0.5f;
  }

  static glm::mat4 localEulerRotation(float pitchRadians, float yawRadians,
                                      float rollRadians) {
    glm::mat4 rotation(1.0f);
    rotation = glm::rotate(rotation, pitchRadians, glm::vec3(1.0f, 0.0f, 0.0f));
    rotation = glm::rotate(rotation, yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
    rotation = glm::rotate(rotation, rollRadians, glm::vec3(0.0f, 0.0f, 1.0f));
    return rotation;
  }

  static bool applyPaintBrush(DefaultEngineInstancedObjectRuntimeContext &context,
                              size_t instancedObjectIndex,
                              const PaintHit &hit) {
    SceneAssetInstance &instancedObject =
        context.sceneAssets[instancedObjectIndex];
    const SceneAssetInstance &terrainAsset = context.sceneAssets[hit.terrainIndex];
    const glm::mat4 terrainModel = terrainModelMatrix(context, hit.terrainIndex);
    const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
    const glm::mat3 terrainNormalMatrix =
        glm::transpose(glm::inverse(glm::mat3(terrainModel)));
    const float spacing = std::max(instancedObject.instanceSpacing, 0.05f);
    const float halfSizeX = terrainAsset.terrainConfig.sizeX * 0.5f;
    const float halfSizeZ = terrainAsset.terrainConfig.sizeZ * 0.5f;
    const float startX = gridStart(halfSizeX, spacing);
    const float startZ = gridStart(halfSizeZ, spacing);
    const float radius = brushRadius(instancedObject);
    const float jitterAmplitude =
        0.5f * spacing * glm::clamp(instancedObject.instanceJitter, 0.0f, 1.0f);

    std::unordered_set<uint64_t> occupiedCells;
    occupiedCells.reserve(instancedObject.instanceTransforms.size());
    for (const auto &instanceTransform : instancedObject.instanceTransforms) {
      const glm::vec3 terrainLocalPosition =
          glm::vec3(inverseTerrainModel *
                    glm::vec4(instanceTransform.position, 1.0f));
      if (!insideTerrainBounds(terrainAsset.terrainConfig, terrainLocalPosition.x,
                               terrainLocalPosition.z)) {
        continue;
      }
      occupiedCells.insert(cellKey(
          nearestCellIndex(terrainLocalPosition.x, startX, spacing),
          nearestCellIndex(terrainLocalPosition.z, startZ, spacing)));
    }

    const int minCellX = static_cast<int>(
        std::floor((hit.localPosition.x - radius - startX) / spacing));
    const int maxCellX = static_cast<int>(
        std::ceil((hit.localPosition.x + radius - startX) / spacing));
    const int minCellZ = static_cast<int>(
        std::floor((hit.localPosition.z - radius - startZ) / spacing));
    const int maxCellZ = static_cast<int>(
        std::ceil((hit.localPosition.z + radius - startZ) / spacing));

    bool changed = false;
    for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
      for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
        const uint64_t key = cellKey(cellX, cellZ);
        if (occupiedCells.contains(key)) {
          continue;
        }

        const float nominalX = cellCoordinate(cellX, startX, spacing);
        const float nominalZ = cellCoordinate(cellZ, startZ, spacing);
        if (!insideTerrainBounds(terrainAsset.terrainConfig, nominalX, nominalZ)) {
          continue;
        }

        const glm::vec2 jitter = {
            randomSignedUnit(instancedObject.instanceScatterSeed, nominalX,
                             nominalZ, 17.0f),
            randomSignedUnit(instancedObject.instanceScatterSeed, nominalX,
                             nominalZ, 53.0f)};
        const float localX = glm::clamp(nominalX + jitter.x * jitterAmplitude,
                                        -halfSizeX, halfSizeX);
        const float localZ = glm::clamp(nominalZ + jitter.y * jitterAmplitude,
                                        -halfSizeZ, halfSizeZ);
        const glm::vec2 offset(localX - hit.localPosition.x,
                               localZ - hit.localPosition.z);
        if (glm::dot(offset, offset) > radius * radius ||
            !passesSlopeFilter(instancedObject, terrainAsset.terrainConfig, localX,
                               localZ)) {
          continue;
        }

        instancedObject.instanceTransforms.push_back(buildPlacementTransform(
            instancedObject, terrainAsset.terrainConfig, terrainModel,
            terrainNormalMatrix, localX, localZ));
        occupiedCells.insert(key);
        changed = true;
      }
    }

    if (changed) {
      context.sceneDefinition.assets = context.sceneAssets;
    }
    return changed;
  }

  static bool erasePaintBrush(DefaultEngineInstancedObjectRuntimeContext &context,
                              size_t instancedObjectIndex,
                              const PaintHit &hit) {
    SceneAssetInstance &instancedObject =
        context.sceneAssets[instancedObjectIndex];
    if (instancedObject.instanceTransforms.empty()) {
      return false;
    }

    const glm::mat4 terrainModel = terrainModelMatrix(context, hit.terrainIndex);
    const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
    const float radiusSquared =
        brushRadius(instancedObject) * brushRadius(instancedObject);
    const size_t originalCount = instancedObject.instanceTransforms.size();
    instancedObject.instanceTransforms.erase(
        std::remove_if(instancedObject.instanceTransforms.begin(),
                       instancedObject.instanceTransforms.end(),
                       [&inverseTerrainModel, &hit, radiusSquared](
                           const SceneTransform &instanceTransform) {
                         const glm::vec3 localPosition =
                             glm::vec3(inverseTerrainModel *
                                       glm::vec4(instanceTransform.position, 1.0f));
                         const glm::vec2 offset(localPosition.x - hit.localPosition.x,
                                                localPosition.z - hit.localPosition.z);
                         return glm::dot(offset, offset) <= radiusSquared;
                       }),
        instancedObject.instanceTransforms.end());

    if (instancedObject.instanceTransforms.size() == originalCount) {
      return false;
    }

    context.sceneDefinition.assets = context.sceneAssets;
    return true;
  }

  static void applyBrushStroke(
      DefaultEngineInstancedObjectRuntimeContext &context,
      size_t instancedObjectIndex, const std::optional<PaintHit> &hit) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse ||
        glfwGetMouseButton(context.window.handle(), GLFW_MOUSE_BUTTON_LEFT) !=
            GLFW_PRESS ||
        !hit.has_value()) {
      return;
    }

    SceneAssetInstance &instancedObject =
        context.sceneAssets[instancedObjectIndex];
    if (instancedObject.instanceEraseMode) {
      erasePaintBrush(context, instancedObjectIndex, *hit);
      return;
    }
    applyPaintBrush(context, instancedObjectIndex, *hit);
  }

  static void updateToolOverlay(
      DefaultEngineInstancedObjectRuntimeContext &context,
      const std::optional<PaintHit> &hit,
      const SceneAssetInstance &instancedObject) {
    if (context.debugOverlayPass == nullptr || !hit.has_value()) {
      if (context.debugOverlayPass != nullptr) {
        context.debugOverlayPass->setToolVisible(false);
        context.debugOverlayPass->setToolMarkers({});
      }
      return;
    }

    const float radius = brushRadius(instancedObject);
    const float lineHeight = std::max(radius * 0.75f, 0.6f);
    context.debugOverlayPass->setToolMarkerMesh(context.terrainBrushIndicatorMesh);
    context.debugOverlayPass->setToolMarkers({DebugOverlayInstance{
        .model = alignedTransform(hit->worldPosition + hit->worldNormal * 0.01f,
                                  hit->worldNormal, {radius, lineHeight, radius},
                                  0.0f, 0.0f, 0.0f, 0.0f),
        .color = instancedObject.instanceEraseMode
                     ? glm::vec4(0.92f, 0.28f, 0.22f, 1.0f)
                     : glm::vec4(0.22f, 0.88f, 0.36f, 1.0f),
    }});
    context.debugOverlayPass->setToolVisible(true);
  }

  static bool sameTransform(const SceneTransform &lhs,
                            const SceneTransform &rhs) {
    return glm::all(glm::equal(lhs.position, rhs.position)) &&
           glm::all(glm::equal(lhs.rotationDegrees, rhs.rotationDegrees)) &&
           glm::all(glm::equal(lhs.scale, rhs.scale));
  }

  static bool sameTransformList(const std::vector<SceneTransform> &lhs,
                                const std::vector<SceneTransform> &rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    for (size_t index = 0; index < lhs.size(); ++index) {
      if (!sameTransform(lhs[index], rhs[index])) {
        return false;
      }
    }
    return true;
  }

  static glm::mat4 alignedTransform(const glm::vec3 &position,
                                    const glm::vec3 &normal,
                                    const glm::vec3 &scale,
                                    float pitchRadians,
                                    float yawRadians,
                                    float rollRadians,
                                    float heightOffset) {
    const glm::vec3 up = glm::normalize(
        glm::length(normal) > 1e-6f ? normal : glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 fallbackForward =
        std::abs(glm::dot(up, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(fallbackForward, up));
    const glm::vec3 forward = glm::normalize(glm::cross(up, right));
    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(right, 0.0f);
    basis[1] = glm::vec4(up, 0.0f);
    basis[2] = glm::vec4(forward, 0.0f);
    basis[3] = glm::vec4(position + up * heightOffset, 1.0f);
    return basis * localEulerRotation(pitchRadians, yawRadians, rollRadians) *
           glm::scale(glm::mat4(1.0f), scale);
  }

  static SceneTransform buildPlacementTransform(
      const SceneAssetInstance &instancedObject, const TerrainConfig &terrainConfig,
      const glm::mat4 &terrainModel, const glm::mat3 &terrainNormalMatrix,
      float localX, float localZ) {
    const float localHeight =
        TerrainGenerator::sampleHeight(terrainConfig, localX, localZ);
    const glm::vec3 localNormal =
        TerrainQueries::sampleLocalNormal(terrainConfig, localX, localZ);
    const glm::vec3 worldPosition =
        glm::vec3(terrainModel * glm::vec4(localX, localHeight, localZ, 1.0f));
    const glm::vec3 worldNormal =
        glm::normalize(terrainNormalMatrix * localNormal);
    const float scaleHorizontal = randomRange(
        instancedObject.instanceScaleRange.x,
        instancedObject.instanceScaleRange.y,
        randomUnit(instancedObject.instanceScatterSeed, localX, localZ, 211.0f));
    const float scaleVertical = randomRange(
        instancedObject.instanceScaleVerticalRange.x,
        instancedObject.instanceScaleVerticalRange.y,
        randomUnit(instancedObject.instanceScatterSeed, localX, localZ, 223.0f));
    const glm::vec3 scale(scaleHorizontal, scaleVertical, scaleHorizontal);
    const float yawRadians = instancedObject.instanceRandomYaw
                                 ? glm::radians(centeredRandomValue(
                                       instancedObject.instanceScatterSeed, localX,
                                       localZ, 101.0f,
                                       instancedObject.instanceYawRangeDegrees))
                                 : 0.0f;
    const float pitchRadians = glm::radians(centeredRandomValue(
        instancedObject.instanceScatterSeed, localX, localZ, 131.0f,
        instancedObject.instancePitchRangeDegrees));
    const float rollRadians = glm::radians(centeredRandomValue(
        instancedObject.instanceScatterSeed, localX, localZ, 151.0f,
        instancedObject.instanceRollRangeDegrees));
    const float heightJitter = centeredRandomValue(
        instancedObject.instanceScatterSeed, localX, localZ, 181.0f,
        instancedObject.instanceHeightJitter);
    const float heightOffset =
        instancedObject.instanceHeightOffset + heightJitter;

    if (instancedObject.instanceAlignToTerrainNormal) {
      return AppSceneController::sceneTransformFromMatrix(alignedTransform(
          worldPosition, worldNormal, scale, pitchRadians, yawRadians,
          rollRadians, heightOffset));
    }

    return SceneTransform{
        .position = worldPosition +
                    glm::vec3(0.0f, heightOffset, 0.0f),
        .rotationDegrees = {glm::degrees(pitchRadians), glm::degrees(yawRadians),
                            glm::degrees(rollRadians)},
        .scale = scale,
    };
  }
};
