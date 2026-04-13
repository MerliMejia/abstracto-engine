#pragma once

#include "AppPerformanceStats.h"
#include "AppRendererSetup.h"
#include "DefaultEngineConfig.h"
#include "RendererSceneAdapters.h"
#include "SceneRenderItemBuilder.h"
#include "ShadowSystem.h"
#include "assets/RenderableModel.h"
#include "default_engine/DefaultEngineCharacterControllerRuntime.h"
#include "default_engine/DefaultEngineDebugOverlayRuntime.h"
#include "default_engine/DefaultEngineSceneAssetRuntime.h"
#include "default_engine/DefaultEngineSessionRuntime.h"
#include "default_engine/DefaultEngineTerrainRuntime.h"
#include "editor/DefaultDebugUI.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "core/PassRenderer.h"
#include "core/RenderPass.h"
#include "debug/CharacterControllerDebugMeshes.h"
#include "debug/DebugLightMeshes.h"
#include "debug/TerrainDebugMeshes.h"
#include "passes/ShadowPass.h"
#include "resources/FrameGeometryUniforms.h"
#include "resources/Sampler.h"
#include <stb_image.h>
#include "vulkan/vulkan.hpp"
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

class DefaultEngineApp {
public:
  explicit DefaultEngineApp(DefaultEngineConfig config = {},
                            SceneDefinition sceneDefinition = {})
      : engineConfig(std::move(config)),
        sceneDefinition(std::move(sceneDefinition)),
        backendConfig{.appName = engineConfig.windowTitle,
                      .maxFramesInFlight =
                          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT} {}

  static DefaultEngineApp create(DefaultEngineConfig config = {},
                                 SceneDefinition sceneDefinition = {}) {
    return DefaultEngineApp(std::move(config), std::move(sceneDefinition));
  }

  void run() {
    debugUiSettings = buildBaseDebugUiSettings();
    debugUiVisible = engineConfig.debugUiVisibleOnStartup.value_or(
        !isDebuggerAttached());
    if (engineConfig.enableDebugSessionPersistence &&
        engineConfig.restoreSessionOnStartup) {
      loadDebugSessionFromDisk();
    }
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  using TerrainPaintState = DefaultEngineTerrainPaintState;
  using TerrainEditHit = DefaultEngineTerrainEditHit;

  DefaultEngineConfig engineConfig;
  SceneDefinition sceneDefinition;
  AppWindow window;
  VulkanBackend backend;
  BackendConfig backendConfig;
  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  std::vector<SceneAssetInstance> sceneAssets;
  std::vector<RenderableModel> sceneAssetModels;
  RenderableModel emptyEditorModel;
  FullscreenMesh lightQuad;
  TypedMesh<Vertex> pointLightMarkerMesh;
  TypedMesh<Vertex> spotLightMarkerMesh;
  TypedMesh<Vertex> directionalLightMarkerMesh;
  TypedMesh<Vertex> boneSegmentMesh;
  TypedMesh<Vertex> boneJointMarkerMesh;
  TypedMesh<Vertex> characterControllerRingMesh;
  TypedMesh<Vertex> characterControllerVerticalLineMesh;
  TypedMesh<Vertex> terrainWireframeMesh;
  TypedMesh<Vertex> terrainBrushIndicatorMesh;
  FrameGeometryUniforms frameGeometryUniforms;
  Sampler sampler;
  ImageBasedLighting imageBasedLighting;
  GeometryPass *geometryPass = nullptr;
  ShadowPass *directionalShadowPass = nullptr;
  std::array<ShadowPass *, DEFAULT_ENGINE_MAX_SPOT_SHADOW_PASSES>
      spotShadowPasses{nullptr, nullptr, nullptr};
  PbrPass *pbrPass = nullptr;
  TonemapPass *tonemapPass = nullptr;
  DebugPresentPass *debugPresentPass = nullptr;
  DebugOverlayPass *debugOverlayPass = nullptr;
  ImGuiPass *imguiPass = nullptr;

  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  DefaultDebugUISettings debugUiSettings;
  float smoothedFrameTimeMs = 0.0f;
  bool debugUiVisible = true;
  bool debugUiToggleHeld = false;
  int activeTerrainWireframeIndex = -1;
  std::optional<TerrainConfig> activeTerrainWireframeConfig;
  std::optional<DefaultEngineTerrainFlattenStroke> activeTerrainFlattenStroke;
  std::vector<DefaultEngineTerrainPaintState> terrainPaintStates;

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  DefaultEngineSceneAssetRuntimeContext sceneAssetRuntimeContext() {
    return DefaultEngineSceneAssetRuntimeContext{
        .engineConfig = engineConfig,
        .sceneDefinition = sceneDefinition,
        .sceneAssets = sceneAssets,
        .sceneAssetModels = sceneAssetModels,
        .terrainPaintStates = terrainPaintStates,
        .debugUiSettings = debugUiSettings,
        .backend = backend,
        .frameGeometryUniforms = frameGeometryUniforms,
        .sampler = sampler,
    };
  }

  DefaultEngineTerrainRuntimeContext terrainRuntimeContext() {
    return DefaultEngineTerrainRuntimeContext{
        .sceneDefinition = sceneDefinition,
        .sceneAssets = sceneAssets,
        .sceneAssetModels = sceneAssetModels,
        .terrainPaintStates = terrainPaintStates,
        .activeTerrainFlattenStroke = activeTerrainFlattenStroke,
        .debugUiSettings = debugUiSettings,
        .window = window,
        .backend = backend,
        .debugOverlayPass = debugOverlayPass,
        .terrainWireframeMesh = terrainWireframeMesh,
        .terrainBrushIndicatorMesh = terrainBrushIndicatorMesh,
        .activeTerrainWireframeIndex = activeTerrainWireframeIndex,
        .activeTerrainWireframeConfig = activeTerrainWireframeConfig,
    };
  }

  DefaultEngineCharacterControllerRuntimeContext
  characterControllerRuntimeContext() {
    return DefaultEngineCharacterControllerRuntimeContext{
        .sceneDefinition = sceneDefinition,
        .sceneAssets = sceneAssets,
        .debugUiSettings = debugUiSettings,
    };
  }

  DefaultEngineDebugOverlayRuntimeContext debugOverlayRuntimeContext() {
    return DefaultEngineDebugOverlayRuntimeContext{
        .sceneAssets = sceneAssets,
        .sceneAssetModels = sceneAssetModels,
        .debugUiSettings = debugUiSettings,
        .debugOverlayPass = debugOverlayPass,
        .characterControllerRingMesh = characterControllerRingMesh,
        .characterControllerVerticalLineMesh = characterControllerVerticalLineMesh,
    };
  }

  DefaultEngineSessionRuntimeContext sessionRuntimeContext() {
    return DefaultEngineSessionRuntimeContext{
        .engineConfig = engineConfig,
        .sceneDefinition = sceneDefinition,
        .debugUiSettings = debugUiSettings,
        .backend = backend,
        .imageBasedLighting = imageBasedLighting,
        .renderer = renderer,
    };
  }

  static bool isDebuggerAttached() {
#if defined(__APPLE__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc info{};
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size != sizeof(info)) {
      return false;
    }
    return (info.kp_proc.p_flag & P_TRACED) != 0;
#elif defined(_WIN32)
    return IsDebuggerPresent() != 0;
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      if (line.rfind("TracerPid:", 0) != 0) {
        continue;
      }
      const size_t valueStart = line.find_first_not_of(" \t", 10);
      return valueStart != std::string::npos && line[valueStart] != '0';
    }
    return false;
#else
    return false;
#endif
  }

  void updateDebugUiToggle() {
    const bool togglePressed =
        glfwGetKey(window.handle(), GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
    if (togglePressed && !debugUiToggleHeld) {
      debugUiVisible = !debugUiVisible;
    }
    debugUiToggleHeld = togglePressed;
  }

  vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout() {
    if (geometryPass == nullptr ||
        geometryPass->descriptorSetLayout() == nullptr) {
      throw std::runtime_error(
          "GeometryPass descriptor set layout is not available");
    }
    return *geometryPass->descriptorSetLayout();
  }

  vk::raii::DescriptorSetLayout *sceneSecondaryDescriptorSetLayout() {
    return geometryPass == nullptr ? nullptr : geometryPass->descriptorSetLayout(1);
  }

  std::filesystem::path debugSessionPath() const {
    return resolvedDebugSessionPath(engineConfig);
  }

  static std::string sanitizePathFragment(std::string value) {
    return DefaultEngineTerrainRuntime::sanitizePathFragment(std::move(value));
  }

  std::filesystem::path defaultTerrainPaintCanvasPath(
      size_t terrainIndex, const SceneAssetInstance &sceneAsset) const {
    return DefaultEngineTerrainRuntime::defaultTerrainPaintCanvasPath(
        terrainIndex, sceneAsset);
  }

  static bool loadTerrainPaintCanvasFile(const std::filesystem::path &path,
                                         uint32_t width, uint32_t height,
                                         std::vector<uint8_t> &pixels) {
    return DefaultEngineTerrainRuntime::loadTerrainPaintCanvasFile(path, width,
                                                                   height, pixels);
  }

  static bool writeTerrainPaintCanvasFile(const std::filesystem::path &path,
                                          const std::vector<uint8_t> &pixels) {
    return DefaultEngineTerrainRuntime::writeTerrainPaintCanvasFile(path, pixels);
  }

  static glm::vec4 sampleTerrainBrushTexture(
      const DefaultEngineTerrainPaintState &paintState,
                                             float u, float v) {
    return DefaultEngineTerrainRuntime::sampleTerrainBrushTexture(paintState, u,
                                                                  v);
  }

  static float stableTerrainNoise(float x, float y) {
    return DefaultEngineTerrainRuntime::stableTerrainNoise(x, y);
  }

  static glm::vec2 variedTerrainBrushUv(float tileU, float tileV,
                                        float variation) {
    return DefaultEngineTerrainRuntime::variedTerrainBrushUv(tileU, tileV,
                                                             variation);
  }

  static float brushFalloff(float distance) {
    return DefaultEngineTerrainRuntime::brushFalloff(distance);
  }

  static void applyTerrainPaintMaterial(ImportedMaterialData &material,
                                        const SceneAssetInstance &sceneAsset,
                                        const DefaultEngineTerrainPaintState &paintState) {
    DefaultEngineTerrainRuntime::applyTerrainPaintMaterial(material, sceneAsset,
                                                           paintState);
  }

  static TerrainMaterialOverride
  terrainMaterialOverrideFromMaterial(const ImportedMaterialData &material) {
    return DefaultEngineTerrainRuntime::terrainMaterialOverrideFromMaterial(
        material);
  }

  static void applyTerrainMaterialOverride(
      ImportedMaterialData &material,
      const TerrainMaterialOverride &materialOverride) {
    DefaultEngineTerrainRuntime::applyTerrainMaterialOverride(material,
                                                              materialOverride);
  }

  static std::vector<TerrainMaterialOverride>
  terrainMaterialOverridesFromMaterials(
      const std::vector<ImportedMaterialData> &materials) {
    return DefaultEngineTerrainRuntime::terrainMaterialOverridesFromMaterials(
        materials);
  }

  void applyEngineConfigOverrides(DefaultDebugUISettings &settings) const {
    DefaultEngineSceneAssetRuntime::applyEngineConfigOverrides(engineConfig,
                                                               settings);
  }

  std::vector<SceneAssetInstance> resolvedSceneAssets() const {
    return DefaultEngineSceneAssetRuntime::resolvedSceneAssets(sceneDefinition);
  }

  RenderableModel &currentEditorModel() {
    if (debugUiSettings.selectedLightIndex >= 0 || sceneAssetModels.empty() ||
        debugUiSettings.sceneObjects.empty()) {
      return emptyEditorModel;
    }

    const size_t selectedIndex =
        static_cast<size_t>(debugUiSettings.selectedObjectIndex);
    if (selectedIndex >= sceneAssetModels.size()) {
      return emptyEditorModel;
    }
    return sceneAssetModels[selectedIndex];
  }

  uint32_t totalMaterialCount() const {
    uint32_t total = 0;
    for (const auto &sceneAssetModel : sceneAssetModels) {
      if (sceneAssetModel.modelAsset() == nullptr) {
        continue;
      }
      total += static_cast<uint32_t>(sceneAssetModel.materials().size());
    }
    return total;
  }

  uint32_t totalVertexCount() const {
    uint32_t total = 0;
    for (const auto &sceneAssetModel : sceneAssetModels) {
      const ModelAsset *asset = sceneAssetModel.modelAsset();
      if (asset == nullptr) {
        continue;
      }
      total += static_cast<uint32_t>(asset->mesh().vertexCount());
    }
    return total;
  }

  uint32_t totalTriangleCount() const {
    uint32_t total = 0;
    for (const auto &sceneAssetModel : sceneAssetModels) {
      const ModelAsset *asset = sceneAssetModel.modelAsset();
      if (asset == nullptr) {
        continue;
      }
      total += static_cast<uint32_t>(asset->mesh().indexData().size() / 3);
    }
    return total;
  }

  DefaultDebugUISettings buildBaseDebugUiSettings() const {
    return DefaultEngineSceneAssetRuntime::buildBaseDebugUiSettings(
        engineConfig, sceneDefinition);
  }

  void initWindow() {
    window.create(engineConfig.width, engineConfig.height,
                  engineConfig.windowTitle, true);
  }

  void syncProceduralSkySunWithLight() {
    const glm::vec3 sunDirection = -currentPrimaryDirectionalLightWorld();
    debugUiSettings.iblBakeSettings.sky.sunAzimuthRadians =
        std::atan2(sunDirection.y, sunDirection.x);
    debugUiSettings.iblBakeSettings.sky.sunElevationRadians =
        std::asin(glm::clamp(sunDirection.z, -1.0f, 1.0f));
  }

  void ensureDefaultEnvironmentPath() {
    if (debugUiSettings.iblBakeSettings.environmentHdrPath.empty()) {
      debugUiSettings.iblBakeSettings.environmentHdrPath =
          resolvedDefaultEnvironmentHdrPath(engineConfig);
    }
  }

  void syncSceneObjectsWithAssets() {
    auto context = sceneAssetRuntimeContext();
    DefaultEngineSceneAssetRuntime::syncSceneObjectsWithAssets(context);
  }

  void commitSceneAssetsFromSettings() {
    auto context = sceneAssetRuntimeContext();
    DefaultEngineSceneAssetRuntime::commitSceneAssetsFromSettings(
        context, [this](size_t index) {
          return snapCharacterControllerToTerrain(index);
        });
  }

  std::vector<SceneAssetInstance> persistedSceneAssets() const {
    return DefaultEngineSceneAssetRuntime::persistedSceneAssets(
        DefaultEngineSceneAssetRuntimeContext{
            .engineConfig = engineConfig,
            .sceneDefinition = const_cast<SceneDefinition &>(sceneDefinition),
            .sceneAssets = const_cast<std::vector<SceneAssetInstance> &>(
                sceneAssets),
            .sceneAssetModels =
                const_cast<std::vector<RenderableModel> &>(sceneAssetModels),
            .terrainPaintStates =
                const_cast<std::vector<DefaultEngineTerrainPaintState> &>(
                    terrainPaintStates),
            .debugUiSettings =
                const_cast<DefaultDebugUISettings &>(debugUiSettings),
            .backend = const_cast<VulkanBackend &>(backend),
            .frameGeometryUniforms =
                const_cast<FrameGeometryUniforms &>(frameGeometryUniforms),
            .sampler = const_cast<Sampler &>(sampler),
        });
  }

  void syncTerrainMaterialOverridesInto(
      std::vector<SceneAssetInstance> &assets) const {
    DefaultEngineTerrainRuntime::syncTerrainMaterialOverridesInto(
        assets, sceneAssetModels);
  }

  TerrainPaintState &ensureTerrainPaintState(size_t terrainIndex) {
    auto context = terrainRuntimeContext();
    return DefaultEngineTerrainRuntime::ensureTerrainPaintState(context,
                                                                terrainIndex);
  }

  static void endTerrainPaintStroke(TerrainPaintState &paintState) {
    DefaultEngineTerrainRuntime::endTerrainPaintStroke(paintState);
  }

  bool applyTerrainPaintStamp(size_t terrainIndex, const glm::vec2 &localPosition) {
    auto context = terrainRuntimeContext();
    return DefaultEngineTerrainRuntime::applyTerrainPaintStamp(
        context, terrainIndex, localPosition);
  }

  bool bucketPaintTerrainTexture(size_t terrainIndex) {
    auto context = terrainRuntimeContext();
    return DefaultEngineTerrainRuntime::bucketPaintTerrainTexture(context,
                                                                  terrainIndex);
  }

  bool eraseTerrainPaintCanvas(size_t terrainIndex, const glm::vec2 &localPosition,
                               float eraseStrength) {
    auto context = terrainRuntimeContext();
    return DefaultEngineTerrainRuntime::eraseTerrainPaintCanvas(
        context, terrainIndex, localPosition, eraseStrength);
  }

  void flushTerrainPaintMaterials(bool force = false) {
    auto context = terrainRuntimeContext();
    DefaultEngineTerrainRuntime::flushTerrainPaintMaterials(
        context, force, [this]() { rebuildSceneRenderItems(); });
  }

  void saveTerrainPaintCanvasesToDisk() {
    auto context = terrainRuntimeContext();
    DefaultEngineTerrainRuntime::saveTerrainPaintCanvasesToDisk(context);
  }

  bool snapCharacterControllerToTerrain(size_t characterIndex) {
    auto context = characterControllerRuntimeContext();
    return DefaultEngineCharacterControllerRuntime::snapCharacterControllerToTerrain(
        context, characterIndex);
  }

  void updateCharacterControllerTerrainAnchors() {
    auto context = characterControllerRuntimeContext();
    DefaultEngineCharacterControllerRuntime::updateTerrainAnchors(context);
  }

  std::optional<TerrainEditHit>
  raycastTerrainFromCursor(const glm::mat4 &view,
                           const glm::mat4 &proj) {
    auto context = terrainRuntimeContext();
    return DefaultEngineTerrainRuntime::raycastTerrainFromCursor(context, view,
                                                                 proj);
  }

  void updateTerrainWireframeOverlay() {
    auto context = terrainRuntimeContext();
    DefaultEngineTerrainRuntime::updateTerrainWireframeOverlay(context);
  }

  void loadSceneAssetModel(size_t index) {
    auto context = sceneAssetRuntimeContext();
    DefaultEngineSceneAssetRuntime::loadSceneAssetModel(
        context, index, sceneDescriptorSetLayout(),
        sceneSecondaryDescriptorSetLayout());
  }

  void reloadTerrainAsset(size_t terrainIndex) {
    auto context = sceneAssetRuntimeContext();
    DefaultEngineSceneAssetRuntime::reloadTerrainAsset(
        context, terrainIndex, sceneDescriptorSetLayout(),
        sceneSecondaryDescriptorSetLayout(),
        [this]() { rebuildSceneRenderItems(); });
  }

  void updateTerrainSculpting(const std::optional<TerrainEditHit> &hit,
                              float deltaSeconds) {
    auto context = terrainRuntimeContext();
    DefaultEngineTerrainRuntime::updateTerrainSculpting(
        context, hit, deltaSeconds,
        [this](size_t terrainIndex) { reloadTerrainAsset(terrainIndex); });
  }

  void updateTerrainEditOverlay(const glm::mat4 &view, const glm::mat4 &proj,
                                float deltaSeconds) {
    auto context = terrainRuntimeContext();
    DefaultEngineTerrainRuntime::updateTerrainEditOverlay(context, view, proj,
                                                          deltaSeconds);
  }

  void rebuildSceneRenderItems() {
    SceneRenderItemBuilder::rebuild(
        renderItems, sceneAssetModels, debugUiSettings, geometryPass,
        directionalShadowPass, spotShadowPasses, lightQuad, pbrPass,
        tonemapPass, debugPresentPass);
  }

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

  void updateCharacterControllerOverlay() {
    auto context = debugOverlayRuntimeContext();
    DefaultEngineDebugOverlayRuntime::updateCharacterControllerOverlay(context);
  }

  void updateBoneOverlay() {
    auto context = debugOverlayRuntimeContext();
    DefaultEngineDebugOverlayRuntime::updateBoneOverlay(context);
  }

  void reloadSceneAssets() {
    auto context = sceneAssetRuntimeContext();
    DefaultEngineSceneAssetRuntime::reloadSceneAssets(
        context, sceneDescriptorSetLayout(),
        sceneSecondaryDescriptorSetLayout(),
        [this]() { rebuildSceneRenderItems(); });
  }

  void loadDebugSessionFromDisk() {
    auto context = sessionRuntimeContext();
    DefaultEngineSessionRuntime::loadDebugSessionFromDisk(
        context, debugSessionPath(),
        [this](DefaultDebugUISettings &settings) {
          applyEngineConfigOverrides(settings);
        },
        [this]() { ensureDefaultEnvironmentPath(); });
  }

  void saveDebugSessionToDisk() {
    auto context = sessionRuntimeContext();
    DefaultEngineSessionRuntime::saveDebugSessionToDisk(
        context, debugSessionPath(),
        [this]() { flushTerrainPaintMaterials(true); },
        [this]() { saveTerrainPaintCanvasesToDisk(); },
        [this]() { return persistedSceneAssets(); });
  }

  void applyLoadedDebugSettings() {
    auto context = sessionRuntimeContext();
    DefaultEngineSessionRuntime::applyLoadedDebugSettings(
        context, [this]() { ensureDefaultEnvironmentPath(); },
        [this]() { syncProceduralSkySunWithLight(); },
        [this]() { reloadSceneAssets(); });
  }

  void initVulkan() {
    backend.initialize(window, backendConfig);
    ensureDefaultEnvironmentPath();

    sampler.create(deviceContext());
    AppRendererSetup::registerShadowPasses(
        renderer, directionalShadowPass, spotShadowPasses,
        DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT,
        DEFAULT_ENGINE_SHADOW_MAP_RESOLUTION, engineConfig.rendererAssetPath);

    lightQuad = buildFullscreenQuadMesh();
    lightQuad.createVertexBuffer(commandContext(), deviceContext());
    lightQuad.createIndexBuffer(commandContext(), deviceContext());

    pointLightMarkerMesh = buildPointLightMarkerMesh();
    pointLightMarkerMesh.createVertexBuffer(commandContext(), deviceContext());
    pointLightMarkerMesh.createIndexBuffer(commandContext(), deviceContext());

    spotLightMarkerMesh = buildSpotLightMarkerMesh();
    spotLightMarkerMesh.createVertexBuffer(commandContext(), deviceContext());
    spotLightMarkerMesh.createIndexBuffer(commandContext(), deviceContext());

    directionalLightMarkerMesh = buildDirectionalLightMarkerMesh();
    directionalLightMarkerMesh.createVertexBuffer(commandContext(),
                                                  deviceContext());
    directionalLightMarkerMesh.createIndexBuffer(commandContext(),
                                                 deviceContext());

    boneSegmentMesh = buildBoneSegmentMesh();
    boneSegmentMesh.createVertexBuffer(commandContext(), deviceContext());
    boneSegmentMesh.createIndexBuffer(commandContext(), deviceContext());

    boneJointMarkerMesh = buildBoneJointMarkerMesh();
    boneJointMarkerMesh.createVertexBuffer(commandContext(), deviceContext());
    boneJointMarkerMesh.createIndexBuffer(commandContext(), deviceContext());

    characterControllerRingMesh = buildCharacterControllerRingMesh();
    characterControllerRingMesh.createVertexBuffer(commandContext(),
                                                   deviceContext());
    characterControllerRingMesh.createIndexBuffer(commandContext(),
                                                  deviceContext());

    characterControllerVerticalLineMesh =
        buildCharacterControllerVerticalLineMesh();
    characterControllerVerticalLineMesh.createVertexBuffer(commandContext(),
                                                          deviceContext());
    characterControllerVerticalLineMesh.createIndexBuffer(commandContext(),
                                                         deviceContext());

    terrainBrushIndicatorMesh = buildTerrainBrushIndicatorMesh();
    terrainBrushIndicatorMesh.createVertexBuffer(commandContext(),
                                                deviceContext());
    terrainBrushIndicatorMesh.createIndexBuffer(commandContext(),
                                               deviceContext());

    if (debugUiSettings.syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    imageBasedLighting.create(deviceContext(), commandContext(),
                              debugUiSettings.iblBakeSettings);
    AppRendererSetup::registerMainPasses(
        renderer, geometryPass, pbrPass, tonemapPass, debugPresentPass,
        debugOverlayPass, imguiPass, window, backend.instance(),
        commandContext(), debugUiSettings, imageBasedLighting,
        directionalShadowPass, spotShadowPasses, pointLightMarkerMesh,
        spotLightMarkerMesh, directionalLightMarkerMesh, boneSegmentMesh,
        boneJointMarkerMesh, DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT,
        DEFAULT_ENGINE_CAMERA_NEAR_PLANE, engineConfig.rendererAssetPath);

    renderer.initialize(deviceContext(), swapchainContext());

    frameGeometryUniforms.create(deviceContext(),
                                 DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
    reloadSceneAssets();
  }

  glm::vec3 currentPrimaryDirectionalLightWorld() const {
    const int directionalIndex =
        debugUiSettings.sceneLights.firstDirectionalLightIndex();
    if (directionalIndex < 0) {
      return glm::normalize(glm::vec3(-0.55f, -0.25f, -1.0f));
    }
    return debugUiSettings.sceneLights
        .lights()[static_cast<size_t>(directionalIndex)]
        .direction;
  }

  glm::vec3 estimatedSceneLightRadiance() const {
    glm::vec3 radiance(0.0f);
    for (const auto &light : debugUiSettings.sceneLights.lights()) {
      if (!light.enabled) {
        continue;
      }
      radiance += light.color * light.radianceScale();
    }
    return radiance;
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(window);

    if (!frameState.has_value()) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = std::min(
        std::chrono::duration<float>(now - lastFrameTime).count(), 0.1f);
    lastFrameTime = now;
    const float frameTimeMs = deltaSeconds * 1000.0f;
    if (smoothedFrameTimeMs == 0.0f) {
      smoothedFrameTimeMs = frameTimeMs;
    } else {
      smoothedFrameTimeMs =
          smoothedFrameTimeMs + (frameTimeMs - smoothedFrameTimeMs) * 0.1f;
    }
    const float smoothedFps =
        smoothedFrameTimeMs > 0.0f ? 1000.0f / smoothedFrameTimeMs : 0.0f;

    updateDebugUiToggle();

    if (imguiPass != nullptr) {
      const uint32_t activeShadowPasses = ShadowSystem::activeShadowPassCount(
          debugUiSettings, pbrPass, directionalShadowPass, spotShadowPasses);
      imguiPass->beginFrame(debugUiVisible);
      DefaultDebugUIResult uiResult;
      if (debugUiVisible) {
        RenderableModel &editorModel = currentEditorModel();
        DefaultDebugUI defaultDebugUi = DefaultDebugUI::create(
            editorModel, sceneAssetModels, sceneAssets, debugUiSettings,
            DefaultDebugUICallbacks{
                .syncProceduralSkySunWithLight =
                    [this]() { syncProceduralSkySunWithLight(); },
                .currentPrimaryDirectionalLightWorld =
                    [this]() { return currentPrimaryDirectionalLightWorld(); },
                .bucketPaintTerrainTexture =
                    [this](size_t terrainIndex) {
                      return bucketPaintTerrainTexture(terrainIndex);
                    },
            },
            AppPerformanceStats::build(
                smoothedFps, smoothedFrameTimeMs, debugUiSettings,
                totalMaterialCount(), totalVertexCount(), totalTriangleCount(),
                renderItems, geometryPass, pbrPass, tonemapPass,
                debugPresentPass, activeShadowPasses),
            imguiPass->dockspaceId());
        uiResult = defaultDebugUi.build();
        if (uiResult.materialChanged && editorModel.modelAsset() != nullptr) {
          editorModel.syncMaterialParameters();
          syncTerrainMaterialOverridesInto(sceneAssets);
          sceneDefinition.assets = sceneAssets;
        }
      }
      imguiPass->endFrame();
      if (uiResult.sceneAssetChanged || uiResult.sceneGeometryChanged) {
        commitSceneAssetsFromSettings();
      }
      if (uiResult.sceneGeometryChanged) {
        reloadSceneAssets();
      }
      if (uiResult.saveSessionRequested) {
        saveDebugSessionToDisk();
      }
      if (uiResult.reloadSessionRequested) {
        loadDebugSessionFromDisk();
        applyLoadedDebugSettings();
      }
      if (uiResult.resetSessionRequested) {
        debugUiSettings = buildBaseDebugUiSettings();
        applyLoadedDebugSettings();
      }
      if (debugPresentPass != nullptr) {
        debugPresentPass->setSelectedOutput(
            static_cast<uint32_t>(debugUiSettings.presentedOutput));
        debugPresentPass->setClipPlanes(DEFAULT_ENGINE_CAMERA_NEAR_PLANE,
                                        debugUiSettings.cameraFarPlane);
      }
      if (uiResult.iblBakeRequested) {
        backend.waitIdle();
        if (debugUiSettings.syncSkySunToLight) {
          syncProceduralSkySunWithLight();
        }
        imageBasedLighting.rebuild(deviceContext(), commandContext(),
                                   debugUiSettings.iblBakeSettings);
        renderer.recreate(deviceContext(), swapchainContext());
      }
    }

    DefaultDebugCameraController cameraController =
        DefaultDebugCameraController::create(debugUiSettings);
    cameraController.update(deltaSeconds, window.handle());
    for (auto &sceneAssetModel : sceneAssetModels) {
      sceneAssetModel.updateAnimationPlayback(deltaSeconds);
    }
    rebuildSceneRenderItems();
    for (auto &sceneAssetModel : sceneAssetModels) {
      sceneAssetModel.updateSkinPalettes(frameState->frameIndex);
    }

    GeometryUniformData geometryUniformData{};
    geometryUniformData.model = glm::mat4(1.0f);
    geometryUniformData.modelNormal =
        glm::transpose(glm::inverse(geometryUniformData.model));

    geometryUniformData.view = glm::lookAt(
        debugUiSettings.cameraPosition,
        debugUiSettings.cameraPosition +
            DefaultDebugCameraController::forwardFromSettings(debugUiSettings),
        glm::vec3(0.0f, 1.0f, 0.0f));

    geometryUniformData.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        DEFAULT_ENGINE_CAMERA_NEAR_PLANE, debugUiSettings.cameraFarPlane);
    geometryUniformData.proj[1][1] *= -1.0f;

    frameGeometryUniforms.write(frameState->frameIndex, geometryUniformData);

    if (pbrPass != nullptr) {
      pbrPass->setCamera(geometryUniformData.proj, geometryUniformData.view);
      pbrPass->setLights(
          RendererSceneAdapters::buildPbrLightInputs(debugUiSettings.sceneLights));
      pbrPass->clearLightShadows();
      pbrPass->setEnvironmentControls(
          debugUiSettings.environmentRotationRadians,
          debugUiSettings.environmentIntensity *
              debugUiSettings.environmentBackgroundWeight,
          debugUiSettings.environmentIntensity *
              debugUiSettings.environmentDiffuseWeight,
          debugUiSettings.environmentIntensity *
              debugUiSettings.environmentSpecularWeight,
          debugUiSettings.iblEnabled, debugUiSettings.skyboxVisible);
      pbrPass->setDielectricSpecularScale(
          debugUiSettings.dielectricSpecularScale);
      pbrPass->setDebugView(debugUiSettings.pbrDebugView);
      ShadowSystem::configure(
          debugUiSettings,
          AppSceneController::sceneObjectsAnchor(debugUiSettings), pbrPass,
          directionalShadowPass, spotShadowPasses);
    }
    if (debugOverlayPass != nullptr) {
      debugOverlayPass->setCamera(geometryUniformData.view,
                                  geometryUniformData.proj);
      debugOverlayPass->setMarkersVisible(debugUiSettings.lightMarkersVisible);
      debugOverlayPass->setMarkerScale(debugUiSettings.lightMarkerScale);
      debugOverlayPass->setLightMarkers(RendererSceneAdapters::buildDebugLightMarkers(
          debugUiSettings.sceneLights,
          AppSceneController::sceneObjectsAnchor(debugUiSettings),
          debugUiSettings.lightMarkerScale));
    }
    updateTerrainSculpting(
        raycastTerrainFromCursor(geometryUniformData.view, geometryUniformData.proj),
        deltaSeconds);
    flushTerrainPaintMaterials();
    updateCharacterControllerTerrainAnchors();
    updateTerrainWireframeOverlay();
    updateTerrainEditOverlay(geometryUniformData.view, geometryUniformData.proj,
                             deltaSeconds);
    updateCharacterControllerOverlay();
    updateBoneOverlay();
    if (tonemapPass != nullptr) {
      const glm::vec3 lightRadiance = estimatedSceneLightRadiance();
      const float lightLuminance =
          glm::dot(lightRadiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
      const float resolvedExposure =
          debugUiSettings.autoExposureEnabled
              ? glm::clamp(debugUiSettings.autoExposureKey /
                               std::max(lightLuminance, 0.001f),
                           0.05f, 8.0f)
              : debugUiSettings.exposure;
      tonemapPass->setExposure(resolvedExposure);
      tonemapPass->setWhitePoint(debugUiSettings.whitePoint);
      tonemapPass->setGamma(debugUiSettings.gamma);
      tonemapPass->setOperator(debugUiSettings.tonemapOperator);
    }

    auto &commandBuffer =
        backend.commands().commandBuffer(frameState->frameIndex);
    commandBuffer.begin({});
    RenderPassContext context{.commandBuffer = commandBuffer,
                              .swapchainContext = swapchainContext(),
                              .frameIndex = frameState->frameIndex,
                              .imageIndex = frameState->imageIndex};
    renderer.record(context, renderItems);
    commandBuffer.end();

    bool shouldRecreate = backend.endFrame(*frameState, window);
    if (shouldRecreate) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
    }
  }

  void mainLoop() {
    while (!window.shouldClose()) {
      window.pollEvents();
      drawFrame();
    }
    backend.waitIdle();
  }

  void cleanup() { window.destroy(); }
};
