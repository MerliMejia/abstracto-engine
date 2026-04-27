#pragma once

#include "passes/PbrPass.h"
#include "passes/TonemapPass.h"
#include "lighting/ImageBasedLightingTypes.h"
#include "assets/RenderableModel.h"
#include "SceneLightSet.h"
#include "SceneTypes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>

struct SceneAssetInstance;

enum class PresentedOutput : uint32_t {
  GBufferAlbedo = 0,
  GBufferNormal = 1,
  GBufferMaterial = 2,
  GBufferEmissive = 3,
  GBufferDepth = 4,
  GeometryPass = 5,
  PbrPass = 6,
  TonemapPass = 7,
  DirectionalShadow = 8,
  SpotShadow0 = 9,
  SpotShadow1 = 10,
  SpotShadow2 = 11,
};

enum class ViewportCameraMode : uint32_t {
  Debug = 0,
  SceneCamera = 1,
};

enum class EngineRuntimeState : uint32_t {
  Editing = 0,
  GamePlay = 1,
};

enum class EngineLogicState : uint32_t {
  Always = 0,
  GamePlay = 1,
};

struct DefaultDebugUISettings {
  EngineRuntimeState runtimeState = EngineRuntimeState::Editing;
  PresentedOutput presentedOutput = PresentedOutput::PbrPass;
  PbrDebugView pbrDebugView = PbrDebugView::Final;
  int selectedMaterialIndex = 0;
  int selectedObjectIndex = 0;
  int selectedLightIndex = -1;
  int selectedBoneIndex = -1;
  int selectedCharacterVisualObjectIndex = -1;
  int selectedAnimationObjectIndex = -1;
  int selectedAnimationIndex = -1;

  std::vector<SceneObject> sceneObjects{SceneObject{}};

  SceneLightSet sceneLights = SceneLightSet::showcaseLights();
  bool lightMarkersVisible = true;
  float lightMarkerScale = 0.35f;
  bool bonesVisible = true;
  float boneMarkerScale = 0.08f;
  bool showBoneWeights = false;
  bool shadowsEnabled = true;
  float directionalShadowExtent = 12.0f;
  float directionalShadowNearPlane = 0.1f;
  float directionalShadowFarPlane = 24.0f;

  float exposure = 1.0f;
  float autoExposureKey = 2.5f;
  float whitePoint = 2.716f;
  float gamma = 1.684f;
  bool autoExposureEnabled = true;
  TonemapOperator tonemapOperator = TonemapOperator::ACES;

  float environmentIntensity = 1.1f;
  float environmentBackgroundWeight = 1.0f;
  float environmentDiffuseWeight = 0.85f;
  float environmentSpecularWeight = 1.35f;
  float dielectricSpecularScale = 1.0f;
  float environmentRotationRadians = 0.0f;
  bool iblEnabled = false;
  bool skyboxVisible = false;
  bool syncSkySunToLight = true;
  ImageBasedLightingBakeSettings iblBakeSettings{};

  glm::vec3 cameraPosition = {0.0f, 1.4f, 4.5f};
  float cameraYawRadians = glm::radians(180.0f);
  float cameraPitchRadians = glm::radians(-12.0f);
  float cameraMoveSpeed = 2.5f;
  float cameraLookSensitivity = 0.0035f;
  float cameraFarPlane = 100.0f;
  ViewportCameraMode viewportCameraMode = ViewportCameraMode::Debug;
  int viewportSceneCameraIndex = -1;
  bool cameraLookActive = false;
  double cameraLastCursorX = 0.0;
  double cameraLastCursorY = 0.0;
};

static inline bool gamePlayRuntimeActive(
    const DefaultDebugUISettings &settings) {
  return settings.runtimeState == EngineRuntimeState::GamePlay;
}

static inline bool engineLogicEnabled(const DefaultDebugUISettings &settings,
                                      EngineLogicState logicState) {
  if (logicState == EngineLogicState::GamePlay) {
    return gamePlayRuntimeActive(settings);
  }
  return true;
}

struct DefaultDebugUICallbacks {
  std::function<void()> syncProceduralSkySunWithLight;
  std::function<glm::vec3()> currentPrimaryDirectionalLightWorld;
  std::function<bool(size_t)> bucketPaintTerrainTexture;
  std::function<bool(size_t)> fillTerrainWithInstancedObject;
  std::function<bool(size_t)> reprojectTerrainInstancedObject;
};

class DefaultDebugCameraController {
public:
  explicit DefaultDebugCameraController(DefaultDebugUISettings &settings)
      : settings(settings) {}

  static DefaultDebugCameraController create(DefaultDebugUISettings &settings) {
    return DefaultDebugCameraController(settings);
  }

  void reset() {
    settings.cameraPosition = {0.0f, 1.4f, 4.5f};
    settings.cameraYawRadians = glm::radians(180.0f);
    settings.cameraPitchRadians = glm::radians(-12.0f);
    settings.cameraFarPlane = 100.0f;
    settings.cameraLookActive = false;
  }

  static glm::vec3 forwardFromAngles(float yawRadians, float pitchRadians) {
    const float cosPitch = std::cos(pitchRadians);
    return glm::normalize(glm::vec3(std::sin(yawRadians) * cosPitch,
                                    std::sin(pitchRadians),
                                    -std::cos(yawRadians) * cosPitch));
  }

  static glm::vec3 forwardFromSettings(const DefaultDebugUISettings &settings) {
    return forwardFromAngles(settings.cameraYawRadians,
                             settings.cameraPitchRadians);
  }

  static void anglesFromForward(const glm::vec3 &forward, float &yawRadians,
                                float &pitchRadians) {
    const glm::vec3 normalizedForward =
        glm::normalize(glm::length(forward) > 1e-6f
                           ? forward
                           : glm::vec3(0.0f, 0.0f, -1.0f));
    pitchRadians =
        std::asin(glm::clamp(normalizedForward.y, -1.0f, 1.0f));
    yawRadians = std::atan2(normalizedForward.x, -normalizedForward.z);
  }

  static SceneTransform sceneTransformFromPose(const glm::vec3 &position,
                                               float yawRadians,
                                               float pitchRadians) {
    return SceneTransform{
        .position = position,
        .rotationDegrees = {glm::degrees(pitchRadians),
                            glm::degrees(yawRadians), 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
    };
  }

  static SceneTransform
  sceneTransformFromSettings(const DefaultDebugUISettings &settings) {
    return sceneTransformFromPose(settings.cameraPosition,
                                  settings.cameraYawRadians,
                                  settings.cameraPitchRadians);
  }

  static void activateSceneCameraPreview(DefaultDebugUISettings &settings,
                                         int sceneCameraIndex) {
    settings.viewportCameraMode = ViewportCameraMode::SceneCamera;
    settings.viewportSceneCameraIndex = sceneCameraIndex;
    settings.cameraLookActive = false;
  }

  static void deactivateSceneCameraPreview(DefaultDebugUISettings &settings) {
    settings.viewportCameraMode = ViewportCameraMode::Debug;
    settings.viewportSceneCameraIndex = -1;
    settings.cameraLookActive = false;
  }

  static bool
  sceneCameraPreviewActive(const DefaultDebugUISettings &settings) {
    return settings.viewportCameraMode == ViewportCameraMode::SceneCamera &&
           settings.viewportSceneCameraIndex >= 0;
  }

  glm::vec3 currentForward() const { return forwardFromSettings(settings); }

  void update(float deltaSeconds, GLFWwindow *windowHandle) {
    updatePose(settings, settings.cameraPosition, settings.cameraYawRadians,
               settings.cameraPitchRadians, deltaSeconds, windowHandle);
  }

  static void updatePose(DefaultDebugUISettings &settings, glm::vec3 &position,
                         float &yawRadians, float &pitchRadians,
                         float deltaSeconds, GLFWwindow *windowHandle) {
    ImGuiIO &io = ImGui::GetIO();

    if (glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_RIGHT) ==
        GLFW_PRESS) {
      double cursorX = 0.0;
      double cursorY = 0.0;
      glfwGetCursorPos(windowHandle, &cursorX, &cursorY);

      if (!settings.cameraLookActive && !io.WantCaptureMouse) {
        settings.cameraLookActive = true;
        settings.cameraLastCursorX = cursorX;
        settings.cameraLastCursorY = cursorY;
      } else if (settings.cameraLookActive) {
        const double deltaX = cursorX - settings.cameraLastCursorX;
        const double deltaY = cursorY - settings.cameraLastCursorY;
        settings.cameraLastCursorX = cursorX;
        settings.cameraLastCursorY = cursorY;

        yawRadians +=
            static_cast<float>(deltaX) * settings.cameraLookSensitivity;
        pitchRadians -=
            static_cast<float>(deltaY) * settings.cameraLookSensitivity;
        pitchRadians = glm::clamp(pitchRadians, glm::radians(-89.0f),
                                  glm::radians(89.0f));
      }
    } else {
      settings.cameraLookActive = false;
    }

    if (io.WantCaptureKeyboard) {
      return;
    }

    const glm::vec3 forward = forwardFromAngles(yawRadians, pitchRadians);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float moveStep = settings.cameraMoveSpeed * deltaSeconds;

    if (glfwGetKey(windowHandle, GLFW_KEY_W) == GLFW_PRESS) {
      position += forward * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_S) == GLFW_PRESS) {
      position -= forward * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_D) == GLFW_PRESS) {
      position += right * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_A) == GLFW_PRESS) {
      position -= right * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_E) == GLFW_PRESS) {
      position += up * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_Q) == GLFW_PRESS) {
      position -= up * moveStep;
    }
  }

private:
  DefaultDebugUISettings &settings;
};

struct DefaultDebugUIResult {
  bool materialChanged = false;
  bool sceneAssetChanged = false;
  bool sceneGeometryChanged = false;
  bool iblBakeRequested = false;
  bool saveSessionRequested = false;
  bool reloadSessionRequested = false;
  bool resetSessionRequested = false;
};

struct DefaultDebugUIPerformanceStats {
  float fps = 0.0f;
  float frameTimeMs = 0.0f;
  uint32_t objectCount = 0;
  uint32_t lightCount = 0;
  uint32_t materialCount = 0;
  uint32_t vertexCount = 0;
  uint32_t triangleCount = 0;
  uint32_t drawCallCount = 0;
  uint32_t preparedDrawCallCount = 0;
  uint32_t sceneDrawCallCount = 0;
  uint32_t shadowDrawCallCount = 0;
};

struct DefaultDebugUIBindings {
  RenderableModel &sceneModel;
  std::vector<RenderableModel> &sceneModels;
  std::vector<SceneAssetInstance> &sceneAssets;
  DefaultDebugUISettings &settings;
  DefaultDebugUICallbacks callbacks;
  DefaultDebugUIPerformanceStats performanceStats;
  ImGuiID dockspaceId = 0;
};

inline void clampSceneObjectSelection(DefaultDebugUISettings &settings) {
  if (settings.sceneObjects.empty()) {
    settings.selectedObjectIndex = 0;
    settings.selectedCharacterVisualObjectIndex = -1;
    return;
  }
  settings.selectedObjectIndex =
      std::clamp(settings.selectedObjectIndex, 0,
                 static_cast<int>(settings.sceneObjects.size()) - 1);
  if (settings.selectedCharacterVisualObjectIndex >=
      static_cast<int>(settings.sceneObjects.size())) {
    settings.selectedCharacterVisualObjectIndex = -1;
  }
}

inline void ensureSceneObjects(DefaultDebugUISettings &settings) {
  if (settings.sceneObjects.empty()) {
    settings.sceneObjects.push_back(SceneObject{});
  }
  clampSceneObjectSelection(settings);
}
