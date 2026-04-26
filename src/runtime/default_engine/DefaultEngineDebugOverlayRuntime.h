#pragma once

#include "assets/RenderableModel.h"
#include "editor/DebugUIState.h"
#include "passes/DebugOverlayPass.h"
#include "scene/SceneDefinition.h"
#include <algorithm>
#include <array>
#include <vector>

struct DefaultEngineDebugOverlayRuntimeContext {
  std::vector<SceneAssetInstance> &sceneAssets;
  std::vector<RenderableModel> &sceneAssetModels;
  DefaultDebugUISettings &debugUiSettings;
  DebugOverlayPass *debugOverlayPass = nullptr;
  TypedMesh<Vertex> &cameraGizmoMesh;
  TypedMesh<Vertex> &characterControllerRingMesh;
  TypedMesh<Vertex> &characterControllerVerticalLineMesh;
};

class DefaultEngineDebugOverlayRuntime {
public:
  static glm::vec3 debugPositionFromMatrix(const glm::mat4 &matrix) {
    return glm::vec3(matrix[3]);
  }

  static glm::vec3 safeDebugDirection(const glm::vec3 &direction,
                                      const glm::vec3 &fallback) {
    const float lengthSquared = glm::dot(direction, direction);
    return glm::normalize(lengthSquared > 1e-6f ? direction : fallback);
  }

  static glm::mat4 debugOrientationTransform(const glm::vec3 &position,
                                             const glm::vec3 &direction,
                                             float scaleX, float scaleY,
                                             float scaleZ) {
    const glm::vec3 forward =
        safeDebugDirection(direction, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 worldUp =
        std::abs(glm::dot(forward, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
    const glm::vec3 up = glm::normalize(glm::cross(forward, right));

    glm::mat4 transform(1.0f);
    transform[0] = glm::vec4(right * scaleX, 0.0f);
    transform[1] = glm::vec4(up * scaleY, 0.0f);
    transform[2] = glm::vec4(forward * scaleZ, 0.0f);
    transform[3] = glm::vec4(position, 1.0f);
    return transform;
  }

  static glm::mat4 debugLineSegmentTransform(const glm::vec3 &start,
                                             const glm::vec3 &end) {
    const glm::vec3 midpoint = (start + end) * 0.5f;
    const glm::vec3 segment = end - start;
    const float length = glm::length(segment);
    if (length <= 1e-5f) {
      return glm::scale(glm::translate(glm::mat4(1.0f), midpoint),
                        glm::vec3(1.0f, 0.0f, 1.0f));
    }

    const glm::vec3 up = glm::normalize(segment);
    const glm::vec3 fallbackForward =
        std::abs(glm::dot(up, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(fallbackForward, up));
    const glm::vec3 forward = glm::normalize(glm::cross(up, right));

    glm::mat4 transform(1.0f);
    transform[0] = glm::vec4(right, 0.0f);
    transform[1] = glm::vec4(up * (length * 0.5f), 0.0f);
    transform[2] = glm::vec4(forward, 0.0f);
    transform[3] = glm::vec4(midpoint, 1.0f);
    return transform;
  }

  static glm::vec4 boneDebugColor(const SkeletonAssetData &skeleton,
                                  int nodeIndex, int selectedBoneIndex) {
    if (nodeIndex == selectedBoneIndex) {
      return {1.0f, 0.66f, 0.12f, 1.0f};
    }
    if (selectedBoneIndex >= 0 &&
        static_cast<size_t>(selectedBoneIndex) < skeleton.nodes.size()) {
      const SkeletonNode &selectedNode =
          skeleton.nodes[static_cast<size_t>(selectedBoneIndex)];
      if (selectedNode.parentIndex == nodeIndex) {
        return {0.98f, 0.28f, 0.68f, 1.0f};
      }
      if (std::find(selectedNode.childIndices.begin(),
                    selectedNode.childIndices.end(),
                    nodeIndex) != selectedNode.childIndices.end()) {
        return {0.48f, 1.0f, 0.34f, 1.0f};
      }
    }
    return {0.18f, 0.86f, 1.0f, 1.0f};
  }

  static void updateCharacterControllerOverlay(
      DefaultEngineDebugOverlayRuntimeContext &context) {
    if (context.debugOverlayPass == nullptr) {
      return;
    }

    std::vector<DebugOverlayInstance> characterMarkers;
    std::vector<DebugOverlayInstance> characterSegments;
    const size_t objectCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    characterMarkers.reserve(objectCount * 2);
    characterSegments.reserve(objectCount * 4);

    for (size_t index = 0; index < objectCount; ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::CharacterController ||
          !context.debugUiSettings.sceneObjects[index].visible) {
        continue;
      }

      const SceneAssetInstance &sceneAsset = context.sceneAssets[index];
      const glm::vec3 position = sceneAsset.characterControllerState.position;
      const float radius = sceneAsset.characterControllerConfig.radius;
      const float halfHeight = sceneAsset.characterControllerConfig.halfHeight;
      const float yawRadians = sceneAsset.characterControllerState.yawRadians;
      const glm::mat4 yawRotation = glm::rotate(
          glm::mat4(1.0f), yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
      const glm::vec4 color =
          static_cast<int>(index) == context.debugUiSettings.selectedObjectIndex &&
                  context.debugUiSettings.selectedLightIndex < 0 &&
                  context.debugUiSettings.selectedBoneIndex < 0
              ? glm::vec4(1.0f, 0.66f, 0.12f, 1.0f)
              : glm::vec4(0.16f, 0.92f, 1.0f, 1.0f);
      const glm::vec4 startColor(0.32f, 1.0f, 0.38f, 1.0f);
      const glm::vec4 limitColor(1.0f, 0.22f, 0.16f, 1.0f);

      for (const float yOffset : {halfHeight, -halfHeight}) {
        characterMarkers.push_back(DebugOverlayInstance{
            .model = glm::translate(glm::mat4(1.0f),
                                    position + glm::vec3(0.0f, yOffset, 0.0f)) *
                     yawRotation *
                     glm::scale(glm::mat4(1.0f),
                                glm::vec3(radius, 1.0f, radius)),
            .color = color,
        });
      }

      const std::array<glm::vec3, 4> sideOffsets = {
          glm::vec3(radius, 0.0f, 0.0f),
          glm::vec3(-radius, 0.0f, 0.0f),
          glm::vec3(0.0f, 0.0f, radius),
          glm::vec3(0.0f, 0.0f, -radius),
      };
      for (const glm::vec3 &sideOffset : sideOffsets) {
        const glm::vec3 worldOffset =
            glm::vec3(yawRotation * glm::vec4(sideOffset, 0.0f));
        characterSegments.push_back(DebugOverlayInstance{
            .model = glm::translate(glm::mat4(1.0f), position + worldOffset) *
                     glm::scale(glm::mat4(1.0f),
                                glm::vec3(1.0f, halfHeight, 1.0f)),
            .color = color,
        });
      }

      if (sceneAsset.characterControllerConfig.useStartPosition) {
        characterMarkers.push_back(DebugOverlayInstance{
            .model =
                glm::translate(glm::mat4(1.0f),
                               sceneAsset.characterControllerConfig.startPosition +
                                   glm::vec3(0.0f, 0.03f, 0.0f)) *
                glm::scale(glm::mat4(1.0f),
                           glm::vec3(radius * 1.35f, 1.0f, radius * 1.35f)),
            .color = startColor,
        });
      }

      const std::vector<glm::vec3> &limitPoints =
          sceneAsset.characterControllerConfig.limitPoints;
      for (size_t pointIndex = 0; pointIndex < limitPoints.size(); ++pointIndex) {
        characterMarkers.push_back(DebugOverlayInstance{
            .model =
                glm::translate(glm::mat4(1.0f),
                               limitPoints[pointIndex] +
                                   glm::vec3(0.0f, 0.04f, 0.0f)) *
                glm::scale(glm::mat4(1.0f),
                           glm::vec3(radius * 0.55f, 1.0f, radius * 0.55f)),
            .color = limitColor,
        });
        if (pointIndex + 1 >= limitPoints.size()) {
          continue;
        }
        characterSegments.push_back(DebugOverlayInstance{
            .model = debugLineSegmentTransform(
                limitPoints[pointIndex] + glm::vec3(0.0f, 0.06f, 0.0f),
                limitPoints[pointIndex + 1] + glm::vec3(0.0f, 0.06f, 0.0f)),
            .color = limitColor,
        });
      }
    }

    context.debugOverlayPass->setCharacterMarkerMesh(
        context.characterControllerRingMesh);
    context.debugOverlayPass->setCharacterSegmentMesh(
        context.characterControllerVerticalLineMesh);
    context.debugOverlayPass->setCharacterMarkers(std::move(characterMarkers));
    context.debugOverlayPass->setCharacterSegments(std::move(characterSegments));
    context.debugOverlayPass->setCharacterVisible(true);
  }

  static void
  updateSceneCameraOverlay(DefaultEngineDebugOverlayRuntimeContext &context) {
    if (context.debugOverlayPass == nullptr) {
      return;
    }

    std::vector<DebugOverlayInstance> cameraMarkers;
    const size_t objectCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    cameraMarkers.reserve(objectCount);

    for (size_t index = 0; index < objectCount; ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::Camera ||
          !context.debugUiSettings.sceneObjects[index].visible) {
        continue;
      }

      const SceneObject &sceneObject =
          context.debugUiSettings.sceneObjects[index];
      const bool selected =
          static_cast<int>(index) == context.debugUiSettings.selectedObjectIndex &&
          context.debugUiSettings.selectedLightIndex < 0 &&
          context.debugUiSettings.selectedBoneIndex < 0;
      const bool previewed =
          DefaultDebugCameraController::sceneCameraPreviewActive(
              context.debugUiSettings) &&
          context.debugUiSettings.viewportSceneCameraIndex ==
              static_cast<int>(index);
      const bool gamePlayRendered =
          gamePlayRuntimeActive(context.debugUiSettings) &&
          index == activeGamePlayCameraIndex(context);
      if (previewed || gamePlayRendered) {
        continue;
      }
      const glm::vec4 color =
          selected ? glm::vec4(1.0f, 0.46f, 0.12f, 1.0f)
                   : glm::vec4(0.2f, 0.9f, 1.0f, 1.0f);
      const float scale = selected ? 0.4f : 0.34f;

      cameraMarkers.push_back(DebugOverlayInstance{
          .model = debugOrientationTransform(
              sceneObject.transform.position,
              AppSceneController::forwardFromSceneTransform(
                  sceneObject.transform),
              scale, scale, scale),
          .color = color,
      });
    }

    context.debugOverlayPass->setSceneCameraMarkerMesh(context.cameraGizmoMesh);
    const bool hasCameraMarkers = !cameraMarkers.empty();
    context.debugOverlayPass->setSceneCameraMarkers(std::move(cameraMarkers));
    context.debugOverlayPass->setSceneCameraVisible(hasCameraMarkers);
  }

  static size_t activeGamePlayCameraIndex(
      const DefaultEngineDebugOverlayRuntimeContext &context) {
    const size_t objectCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    const int selectedIndex = context.debugUiSettings.selectedObjectIndex;
    if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < objectCount) {
      const size_t index = static_cast<size_t>(selectedIndex);
      if (context.sceneAssets[index].kind == SceneAssetKind::Camera &&
          context.debugUiSettings.sceneObjects[index].visible) {
        return index;
      }
    }

    for (size_t index = 0; index < objectCount; ++index) {
      if (context.sceneAssets[index].kind == SceneAssetKind::Camera &&
          context.debugUiSettings.sceneObjects[index].visible) {
        return index;
      }
    }
    return objectCount;
  }

  static void updateBoneOverlay(DefaultEngineDebugOverlayRuntimeContext &context) {
    if (context.debugOverlayPass == nullptr) {
      return;
    }

    std::vector<DebugOverlayInstance> boneSegments;
    std::vector<DebugOverlayInstance> boneMarkers;

    if (context.debugUiSettings.bonesVisible) {
      const size_t objectCount =
          std::min(context.sceneAssetModels.size(),
                   context.debugUiSettings.sceneObjects.size());
      for (size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
        if (!context.debugUiSettings.sceneObjects[objectIndex].visible) {
          continue;
        }

        const RenderableModel &model = context.sceneAssetModels[objectIndex];
        const ModelAsset *asset = model.modelAsset();
        const SkeletonPose *pose = model.currentSkeletonPose();
        const SkeletonAssetData *skeleton = model.skeletonAsset();
        if (asset == nullptr || pose == nullptr || skeleton == nullptr ||
            skeleton->nodes.empty()) {
          continue;
        }

        std::vector<bool> deformingNodes(skeleton->nodes.size(), false);
        for (const auto &submesh : asset->submeshes()) {
          if (submesh.skinIndex < 0 ||
              static_cast<size_t>(submesh.skinIndex) >= skeleton->skins.size()) {
            continue;
          }
          for (const int jointNodeIndex :
               skeleton->skins[static_cast<size_t>(submesh.skinIndex)]
                   .jointNodeIndices) {
            if (jointNodeIndex >= 0 &&
                static_cast<size_t>(jointNodeIndex) < deformingNodes.size()) {
              deformingNodes[static_cast<size_t>(jointNodeIndex)] = true;
            }
          }
        }

        const glm::mat4 objectMatrix = AppSceneController::sceneTransformMatrix(
            context.debugUiSettings.sceneObjects[objectIndex].transform);
        const int selectedBoneIndex =
            static_cast<int>(objectIndex) ==
                    context.debugUiSettings.selectedObjectIndex
                ? context.debugUiSettings.selectedBoneIndex
                : -1;

        for (size_t nodeIndex = 0; nodeIndex < skeleton->nodes.size();
             ++nodeIndex) {
          if (!deformingNodes[nodeIndex]) {
            continue;
          }

          const glm::mat4 worldMatrix =
              objectMatrix * pose->worldTransform(nodeIndex);
          const glm::vec3 jointPosition = debugPositionFromMatrix(worldMatrix);
          const glm::vec4 color = boneDebugColor(
              *skeleton, static_cast<int>(nodeIndex), selectedBoneIndex);
          const float jointScale =
              context.debugUiSettings.boneMarkerScale *
              (static_cast<int>(nodeIndex) == selectedBoneIndex ? 1.45f : 1.0f);

          boneMarkers.push_back(DebugOverlayInstance{
              .model = glm::translate(glm::mat4(1.0f), jointPosition) *
                       glm::scale(glm::mat4(1.0f), glm::vec3(jointScale)),
              .color = color,
          });

          const int parentIndex = skeleton->nodes[nodeIndex].parentIndex;
          if (parentIndex < 0 ||
              static_cast<size_t>(parentIndex) >= deformingNodes.size() ||
              !deformingNodes[static_cast<size_t>(parentIndex)]) {
            continue;
          }

          const glm::vec3 parentPosition = debugPositionFromMatrix(
              objectMatrix * pose->worldTransform(parentIndex));
          const glm::vec3 direction = jointPosition - parentPosition;
          const float length = glm::length(direction);
          if (length <= 1e-5f) {
            continue;
          }

          const float segmentRadius = std::max(
              context.debugUiSettings.boneMarkerScale * 0.8f, length * 0.045f);

          boneSegments.push_back(DebugOverlayInstance{
              .model = debugOrientationTransform(
                  parentPosition, direction, segmentRadius, segmentRadius,
                  length),
              .color = color,
          });
        }
      }
    }

    context.debugOverlayPass->setBonesVisible(context.debugUiSettings.bonesVisible);
    context.debugOverlayPass->setBoneSegments(std::move(boneSegments));
    context.debugOverlayPass->setBoneMarkers(std::move(boneMarkers));
  }
};
