#pragma once

#include "AppPerformanceStats.h"
#include "AppRendererSetup.h"
#include "DefaultEngineConfig.h"
#include "RendererSceneAdapters.h"
#include "SceneRenderItemBuilder.h"
#include "ShadowSystem.h"
#include "engine/assets/RenderableModel.h"
#include "engine/editor/DebugSessionIO.h"
#include "engine/editor/DefaultDebugUI.h"
#include "engine/scene/AppSceneController.h"
#include "engine/scene/SceneDefinition.h"
#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "core/PassRenderer.h"
#include "core/RenderPass.h"
#include "debug/DebugLightMeshes.h"
#include "engine/debug/TerrainDebugMeshes.h"
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
#include <fstream>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unordered_map>
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
  struct TerrainPaintState {
    std::vector<uint8_t> canvasPixels;
    uint32_t canvasWidth = 0;
    uint32_t canvasHeight = 0;
    std::vector<uint8_t> brushPixels;
    int brushWidth = 0;
    int brushHeight = 0;
    std::string loadedBrushTexturePath;
    glm::vec2 loadedUvScale{1.0f, 1.0f};
    bool materialDirty = false;
    bool canvasDirty = false;
    bool strokeActive = false;
    std::vector<uint8_t> strokeBasePixels;
    std::vector<uint8_t> strokePixels;
    std::chrono::steady_clock::time_point lastUploadTime =
        std::chrono::steady_clock::now();
  };
  struct TerrainFlattenStroke {
    size_t terrainIndex = 0;
    float targetHeight = 0.0f;
  };
  std::optional<TerrainFlattenStroke> activeTerrainFlattenStroke;
  std::vector<TerrainPaintState> terrainPaintStates;

  struct TerrainEditHit {
    size_t terrainIndex = 0;
    glm::vec3 localPosition{0.0f};
    glm::vec3 worldPosition{0.0f};
    glm::vec3 worldNormal{0.0f, 1.0f, 0.0f};
  };

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

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
    for (char &character : value) {
      if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
        continue;
      }
      character = '_';
    }

    value.erase(std::unique(value.begin(), value.end(),
                            [](char lhs, char rhs) {
                              return lhs == '_' && rhs == '_';
                            }),
                value.end());
    while (!value.empty() && value.front() == '_') {
      value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '_') {
      value.pop_back();
    }
    if (value.empty()) {
      return "terrain";
    }
    return value;
  }

  std::filesystem::path defaultTerrainPaintCanvasPath(
      size_t terrainIndex, const SceneAssetInstance &sceneAsset) const {
    const std::string baseName =
        sceneAsset.name.empty() ? "terrain" : sceneAsset.name;
    return std::filesystem::path("assets") / "debug" /
           (sanitizePathFragment(baseName) + "_paint_" +
            std::to_string(terrainIndex) + ".rgba");
  }

  static bool loadTerrainPaintCanvasFile(const std::filesystem::path &path,
                                         uint32_t width, uint32_t height,
                                         std::vector<uint8_t> &pixels) {
    if (path.empty() || !std::filesystem::exists(path)) {
      return false;
    }

    const size_t expectedSize =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }

    std::vector<uint8_t> loaded(expectedSize, 0);
    file.read(reinterpret_cast<char *>(loaded.data()),
              static_cast<std::streamsize>(expectedSize));
    if (!file || static_cast<size_t>(file.gcount()) != expectedSize) {
      return false;
    }

    pixels = std::move(loaded);
    return true;
  }

  static bool writeTerrainPaintCanvasFile(const std::filesystem::path &path,
                                          const std::vector<uint8_t> &pixels) {
    if (path.empty() || pixels.empty()) {
      return false;
    }

    std::error_code errorCode;
    const auto parentPath = path.parent_path();
    if (!parentPath.empty()) {
      std::filesystem::create_directories(parentPath, errorCode);
      if (errorCode) {
        return false;
      }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      return false;
    }

    file.write(reinterpret_cast<const char *>(pixels.data()),
               static_cast<std::streamsize>(pixels.size()));
    return static_cast<bool>(file);
  }

  static glm::vec4 sampleTerrainBrushTexture(const TerrainPaintState &paintState,
                                             float u, float v) {
    if (paintState.brushPixels.empty() || paintState.brushWidth <= 0 ||
        paintState.brushHeight <= 0) {
      return glm::vec4(0.0f);
    }

    const int x = std::clamp(static_cast<int>(
                                 std::floor(glm::clamp(u, 0.0f, 1.0f) *
                                            static_cast<float>(paintState.brushWidth))),
                             0, paintState.brushWidth - 1);
    const int y = std::clamp(static_cast<int>(
                                 std::floor(glm::clamp(v, 0.0f, 1.0f) *
                                            static_cast<float>(paintState.brushHeight))),
                             0, paintState.brushHeight - 1);
    const size_t pixelIndex =
        (static_cast<size_t>(y) * static_cast<size_t>(paintState.brushWidth) +
         static_cast<size_t>(x)) *
        4u;
    return glm::vec4(
        static_cast<float>(paintState.brushPixels[pixelIndex + 0]) / 255.0f,
        static_cast<float>(paintState.brushPixels[pixelIndex + 1]) / 255.0f,
        static_cast<float>(paintState.brushPixels[pixelIndex + 2]) / 255.0f,
        static_cast<float>(paintState.brushPixels[pixelIndex + 3]) / 255.0f);
  }

  static float stableTerrainNoise(float x, float y) {
    return glm::fract(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f);
  }

  static glm::vec2 variedTerrainBrushUv(float tileU, float tileV,
                                        float variation) {
    const glm::vec2 baseUv(glm::fract(tileU), glm::fract(tileV));
    if (variation <= 1e-6f) {
      return baseUv;
    }

    const float cellX = std::floor(tileU);
    const float cellY = std::floor(tileV);
    const float rotateNoise = stableTerrainNoise(cellX, cellY);
    const float mirrorNoise = stableTerrainNoise(cellX + 19.0f, cellY + 47.0f);
    const float offsetNoiseX =
        stableTerrainNoise(cellX + 101.0f, cellY + 13.0f) * 2.0f - 1.0f;
    const float offsetNoiseY =
        stableTerrainNoise(cellX + 73.0f, cellY + 151.0f) * 2.0f - 1.0f;

    glm::vec2 transformedUv = baseUv;
    const int rotationIndex = static_cast<int>(std::floor(rotateNoise * 4.0f)) % 4;
    const glm::vec2 centered = transformedUv - glm::vec2(0.5f);
    switch (rotationIndex) {
    case 1:
      transformedUv = glm::vec2(-centered.y, centered.x) + glm::vec2(0.5f);
      break;
    case 2:
      transformedUv = glm::vec2(-centered.x, -centered.y) + glm::vec2(0.5f);
      break;
    case 3:
      transformedUv = glm::vec2(centered.y, -centered.x) + glm::vec2(0.5f);
      break;
    default:
      break;
    }

    if (mirrorNoise > 0.5f) {
      transformedUv.x = 1.0f - transformedUv.x;
    }
    if (mirrorNoise < 0.25f) {
      transformedUv.y = 1.0f - transformedUv.y;
    }

    const glm::vec2 offset =
        glm::vec2(offsetNoiseX, offsetNoiseY) * (0.18f * variation);
    transformedUv = glm::fract(transformedUv + offset);
    return glm::mix(baseUv, transformedUv, variation);
  }

  static float brushFalloff(float distance) {
    const float t = glm::clamp(1.0f - distance, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  static void applyTerrainPaintMaterial(ImportedMaterialData &material,
                                        const SceneAssetInstance &sceneAsset,
                                        const TerrainPaintState &paintState) {
    material.paintCanvasTexture = ImportedTextureSource{
        .resolvedPath = {},
        .rgba = paintState.canvasPixels,
        .width = static_cast<int>(paintState.canvasWidth),
        .height = static_cast<int>(paintState.canvasHeight),
    };
    material.paintCanvasUvScale = sceneAsset.terrainConfig.uvScale;
  }

  static TerrainMaterialOverride
  terrainMaterialOverrideFromMaterial(const ImportedMaterialData &material) {
    return TerrainMaterialOverride{
        .name = material.name,
        .baseColorFactor = material.baseColorFactor,
        .emissiveFactor = material.emissiveFactor,
        .metallicFactor = material.metallicFactor,
        .roughnessFactor = material.roughnessFactor,
        .occlusionStrength = material.occlusionStrength,
    };
  }

  static void applyTerrainMaterialOverride(
      ImportedMaterialData &material,
      const TerrainMaterialOverride &materialOverride) {
    material.name = materialOverride.name;
    material.baseColorFactor = materialOverride.baseColorFactor;
    material.emissiveFactor = materialOverride.emissiveFactor;
    material.metallicFactor = materialOverride.metallicFactor;
    material.roughnessFactor = materialOverride.roughnessFactor;
    material.occlusionStrength = materialOverride.occlusionStrength;
  }

  static std::vector<TerrainMaterialOverride>
  terrainMaterialOverridesFromMaterials(
      const std::vector<ImportedMaterialData> &materials) {
    std::vector<TerrainMaterialOverride> overrides;
    overrides.reserve(materials.size());
    for (const auto &material : materials) {
      overrides.push_back(terrainMaterialOverrideFromMaterial(material));
    }
    return overrides;
  }

  void applyEngineConfigOverrides(DefaultDebugUISettings &settings) const {
    if (engineConfig.skyboxVisible.has_value()) {
      settings.skyboxVisible = *engineConfig.skyboxVisible;
    }
  }

  std::vector<SceneAssetInstance> resolvedSceneAssets() const {
    if (!sceneDefinition.assets.empty()) {
      return sceneDefinition.assets;
    }
    if (!sceneDefinition.modelPath.empty()) {
      return {SceneAssetInstance::fromAsset(sceneDefinition.modelPath)};
    }
    return {};
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
    DefaultDebugUISettings settings;
    const std::vector<SceneAssetInstance> initialSceneAssets =
        resolvedSceneAssets();
    if (initialSceneAssets.empty()) {
      settings.sceneObjects.clear();
    } else {
      settings.sceneObjects.clear();
      settings.sceneObjects.reserve(initialSceneAssets.size());
      for (size_t index = 0; index < initialSceneAssets.size(); ++index) {
        const auto &sceneAsset = initialSceneAssets[index];
        settings.sceneObjects.push_back(SceneObject{
            .name = AppSceneController::sceneAssetName(sceneAsset, index),
            .transform = sceneAsset.transform,
            .visible = sceneAsset.visible,
        });
      }
    }
    settings.sceneLights = sceneDefinition.sceneLights;
    settings.iblBakeSettings.environmentHdrPath =
        resolvedDefaultEnvironmentHdrPath(engineConfig);
    if (engineConfig.configureSettings) {
      engineConfig.configureSettings(settings);
    }
    if (sceneDefinition.configureSettings) {
      sceneDefinition.configureSettings(settings);
    }
    applyEngineConfigOverrides(settings);
    clampSceneObjectSelection(settings);
    return settings;
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
    AppSceneController::syncSceneObjectsWithAssets(debugUiSettings,
                                                   sceneAssets);
    AppSceneController::applyObjectOverrides(debugUiSettings,
                                             sceneDefinition.objectOverrides);
  }

  void commitSceneAssetsFromSettings() {
    const size_t sceneObjectCount =
        std::min(sceneAssets.size(), debugUiSettings.sceneObjects.size());
    for (size_t index = 0; index < sceneObjectCount; ++index) {
      sceneAssets[index].transform = debugUiSettings.sceneObjects[index].transform;
      sceneAssets[index].visible = debugUiSettings.sceneObjects[index].visible;
    }
    syncTerrainMaterialOverridesInto(sceneAssets);
    sceneDefinition.assets = sceneAssets;
  }

  std::vector<SceneAssetInstance> persistedSceneAssets() const {
    std::vector<SceneAssetInstance> persisted =
        !sceneAssets.empty() ? sceneAssets : sceneDefinition.assets;
    syncTerrainMaterialOverridesInto(persisted);
    const size_t sceneObjectCount =
        std::min(persisted.size(), debugUiSettings.sceneObjects.size());
    for (size_t index = 0; index < sceneObjectCount; ++index) {
      persisted[index].transform = debugUiSettings.sceneObjects[index].transform;
      persisted[index].visible = debugUiSettings.sceneObjects[index].visible;
    }
    return persisted;
  }

  void syncTerrainMaterialOverridesInto(
      std::vector<SceneAssetInstance> &assets) const {
    const size_t assetCount = std::min(assets.size(), sceneAssetModels.size());
    for (size_t index = 0; index < assetCount; ++index) {
      if (assets[index].kind != SceneAssetKind::Terrain ||
          sceneAssetModels[index].modelAsset() == nullptr) {
        continue;
      }
      assets[index].terrainMaterialOverrides =
          terrainMaterialOverridesFromMaterials(sceneAssetModels[index].materials());
    }
  }

  TerrainPaintState &ensureTerrainPaintState(size_t terrainIndex) {
    if (terrainPaintStates.size() < sceneAssets.size()) {
      terrainPaintStates.resize(sceneAssets.size());
    }

    TerrainPaintState &paintState = terrainPaintStates[terrainIndex];
    SceneAssetInstance &sceneAsset = sceneAssets[terrainIndex];
    if (sceneAsset.kind != SceneAssetKind::Terrain) {
      return paintState;
    }

    sceneAsset.terrainPaintCanvasResolution =
        std::clamp(sceneAsset.terrainPaintCanvasResolution, 128u, 2048u);
    const uint32_t canvasResolution = sceneAsset.terrainPaintCanvasResolution;
    const size_t expectedCanvasSize =
        static_cast<size_t>(canvasResolution) *
        static_cast<size_t>(canvasResolution) * 4u;

    if (sceneAsset.terrainPaintCanvasPath.empty()) {
      sceneAsset.terrainPaintCanvasPath =
          defaultTerrainPaintCanvasPath(terrainIndex, sceneAsset).string();
      paintState.canvasPixels.assign(expectedCanvasSize, 0);
      paintState.canvasWidth = canvasResolution;
      paintState.canvasHeight = canvasResolution;
      paintState.strokeActive = false;
      paintState.strokeBasePixels.clear();
      paintState.strokePixels.clear();
      paintState.materialDirty = true;
      paintState.canvasDirty = true;
    } else if (paintState.canvasWidth != canvasResolution ||
               paintState.canvasHeight != canvasResolution ||
               paintState.canvasPixels.size() != expectedCanvasSize) {
      if (!loadTerrainPaintCanvasFile(sceneAsset.terrainPaintCanvasPath,
                                      canvasResolution, canvasResolution,
                                      paintState.canvasPixels)) {
        paintState.canvasPixels.assign(expectedCanvasSize, 0);
        paintState.canvasDirty = true;
      }
      paintState.canvasWidth = canvasResolution;
      paintState.canvasHeight = canvasResolution;
      paintState.strokeActive = false;
      paintState.strokeBasePixels.clear();
      paintState.strokePixels.clear();
      paintState.materialDirty = true;
    }

    if (paintState.loadedBrushTexturePath != sceneAsset.terrainBrushTexturePath) {
      paintState.brushPixels.clear();
      paintState.brushWidth = 0;
      paintState.brushHeight = 0;
      paintState.loadedBrushTexturePath = sceneAsset.terrainBrushTexturePath;

      if (!sceneAsset.terrainBrushTexturePath.empty()) {
        int brushWidth = 0;
        int brushHeight = 0;
        int brushChannels = 0;
        stbi_uc *pixels = stbi_load(sceneAsset.terrainBrushTexturePath.c_str(),
                                    &brushWidth, &brushHeight, &brushChannels,
                                    STBI_rgb_alpha);
        if (pixels != nullptr && brushWidth > 0 && brushHeight > 0) {
          const size_t brushSize =
              static_cast<size_t>(brushWidth) * static_cast<size_t>(brushHeight) *
              4u;
          paintState.brushPixels.assign(pixels, pixels + brushSize);
          paintState.brushWidth = brushWidth;
          paintState.brushHeight = brushHeight;
          stbi_image_free(pixels);
        } else {
          std::cerr << "Failed to load terrain brush texture: "
                    << sceneAsset.terrainBrushTexturePath << std::endl;
        }
      }
    }

    if (paintState.loadedUvScale != sceneAsset.terrainConfig.uvScale) {
      paintState.loadedUvScale = sceneAsset.terrainConfig.uvScale;
      paintState.materialDirty = true;
    }

    return paintState;
  }

  static void endTerrainPaintStroke(TerrainPaintState &paintState) {
    if (!paintState.strokeActive) {
      return;
    }
    paintState.strokeActive = false;
    paintState.strokeBasePixels.clear();
    paintState.strokePixels.clear();
  }

  bool applyTerrainPaintStamp(size_t terrainIndex, const glm::vec2 &localPosition) {
    if (terrainIndex >= sceneAssets.size()) {
      return false;
    }

    SceneAssetInstance &sceneAsset = sceneAssets[terrainIndex];
    TerrainPaintState &paintState = ensureTerrainPaintState(terrainIndex);
    if (sceneAsset.kind != SceneAssetKind::Terrain || paintState.canvasPixels.empty() ||
        paintState.brushPixels.empty()) {
      return false;
    }

    if (!paintState.strokeActive) {
      paintState.strokeActive = true;
      paintState.strokeBasePixels = paintState.canvasPixels;
      paintState.strokePixels.assign(paintState.canvasPixels.size(), 0);
    }

    const float sizeX = std::max(sceneAsset.terrainConfig.sizeX, 1e-6f);
    const float sizeZ = std::max(sceneAsset.terrainConfig.sizeZ, 1e-6f);
    const float radius = std::max(sceneAsset.terrainBrushRadius, 1e-4f);
    const float radiusU = radius / sizeX;
    const float radiusV = radius / sizeZ;
    const float centerU =
        glm::clamp(localPosition.x / sizeX + 0.5f, 0.0f, 1.0f);
    const float centerV =
        glm::clamp(localPosition.y / sizeZ + 0.5f, 0.0f, 1.0f);

    const int minX = std::max(
        0, static_cast<int>(std::floor((centerU - radiusU) *
                                       static_cast<float>(paintState.canvasWidth))));
    const int maxX = std::min(
        static_cast<int>(paintState.canvasWidth) - 1,
        static_cast<int>(std::ceil((centerU + radiusU) *
                                   static_cast<float>(paintState.canvasWidth))));
    const int minY = std::max(
        0, static_cast<int>(std::floor((centerV - radiusV) *
                                       static_cast<float>(paintState.canvasHeight))));
    const int maxY = std::min(
        static_cast<int>(paintState.canvasHeight) - 1,
        static_cast<int>(std::ceil((centerV + radiusV) *
                                   static_cast<float>(paintState.canvasHeight))));

    bool changed = false;
    for (int y = minY; y <= maxY; ++y) {
      const float canvasV =
          (static_cast<float>(y) + 0.5f) / static_cast<float>(paintState.canvasHeight);
      const float normalizedV = (canvasV - centerV) / std::max(radiusV, 1e-6f);
      for (int x = minX; x <= maxX; ++x) {
        const float canvasU =
            (static_cast<float>(x) + 0.5f) /
            static_cast<float>(paintState.canvasWidth);
        const float normalizedU =
            (canvasU - centerU) / std::max(radiusU, 1e-6f);
        const float distance =
            std::sqrt(normalizedU * normalizedU + normalizedV * normalizedV);
        if (distance > 1.0f) {
          continue;
        }

        const float tileU =
            canvasU * std::max(sceneAsset.terrainConfig.uvScale.x, 1e-4f);
        const float tileV =
            canvasV * std::max(sceneAsset.terrainConfig.uvScale.y, 1e-4f);
        const glm::vec2 variedBrushUv = variedTerrainBrushUv(
            tileU, tileV, sceneAsset.terrainBrushTextureVariation);
        const glm::vec4 brushSample = sampleTerrainBrushTexture(
            paintState, variedBrushUv.x, variedBrushUv.y);
        const float sourceAlpha = glm::clamp(
            brushSample.a * sceneAsset.terrainBrushOpacity * brushFalloff(distance),
            0.0f, 1.0f);
        if (sourceAlpha <= 1e-5f) {
          continue;
        }

        const size_t pixelIndex =
            (static_cast<size_t>(y) * static_cast<size_t>(paintState.canvasWidth) +
             static_cast<size_t>(x)) *
            4u;
        const float existingStrokeAlpha =
            static_cast<float>(paintState.strokePixels[pixelIndex + 3]) / 255.0f;
        if (sourceAlpha <= existingStrokeAlpha + 1e-5f) {
          continue;
        }

        const glm::vec3 brushColor(brushSample.r, brushSample.g, brushSample.b);
        paintState.strokePixels[pixelIndex + 0] =
            static_cast<uint8_t>(glm::clamp(brushColor.r, 0.0f, 1.0f) * 255.0f +
                                 0.5f);
        paintState.strokePixels[pixelIndex + 1] =
            static_cast<uint8_t>(glm::clamp(brushColor.g, 0.0f, 1.0f) * 255.0f +
                                 0.5f);
        paintState.strokePixels[pixelIndex + 2] =
            static_cast<uint8_t>(glm::clamp(brushColor.b, 0.0f, 1.0f) * 255.0f +
                                 0.5f);
        paintState.strokePixels[pixelIndex + 3] =
            static_cast<uint8_t>(sourceAlpha * 255.0f + 0.5f);

        const glm::vec4 destination(
            static_cast<float>(paintState.strokeBasePixels[pixelIndex + 0]) / 255.0f,
            static_cast<float>(paintState.strokeBasePixels[pixelIndex + 1]) / 255.0f,
            static_cast<float>(paintState.strokeBasePixels[pixelIndex + 2]) / 255.0f,
            static_cast<float>(paintState.strokeBasePixels[pixelIndex + 3]) / 255.0f);
        const float outAlpha = sourceAlpha + destination.a * (1.0f - sourceAlpha);
        glm::vec3 outColor(0.0f);
        if (outAlpha > 1e-5f) {
          const glm::vec3 destinationColor(destination.r, destination.g,
                                           destination.b);
          outColor =
              (brushColor * sourceAlpha +
               destinationColor * destination.a * (1.0f - sourceAlpha)) /
              outAlpha;
        }

        const auto toByte = [](float value) {
          return static_cast<uint8_t>(glm::clamp(value, 0.0f, 1.0f) * 255.0f +
                                      0.5f);
        };
        const uint8_t nextR = toByte(outColor.r);
        const uint8_t nextG = toByte(outColor.g);
        const uint8_t nextB = toByte(outColor.b);
        const uint8_t nextA = toByte(outAlpha);
        if (paintState.canvasPixels[pixelIndex + 0] == nextR &&
            paintState.canvasPixels[pixelIndex + 1] == nextG &&
            paintState.canvasPixels[pixelIndex + 2] == nextB &&
            paintState.canvasPixels[pixelIndex + 3] == nextA) {
          continue;
        }

        paintState.canvasPixels[pixelIndex + 0] = nextR;
        paintState.canvasPixels[pixelIndex + 1] = nextG;
        paintState.canvasPixels[pixelIndex + 2] = nextB;
        paintState.canvasPixels[pixelIndex + 3] = nextA;
        changed = true;
      }
    }

    if (changed) {
      paintState.materialDirty = true;
      paintState.canvasDirty = true;
    }
    return changed;
  }

  bool eraseTerrainPaintCanvas(size_t terrainIndex, const glm::vec2 &localPosition,
                               float eraseStrength) {
    if (terrainIndex >= sceneAssets.size()) {
      return false;
    }

    SceneAssetInstance &sceneAsset = sceneAssets[terrainIndex];
    TerrainPaintState &paintState = ensureTerrainPaintState(terrainIndex);
    if (sceneAsset.kind != SceneAssetKind::Terrain || paintState.canvasPixels.empty() ||
        eraseStrength <= 1e-6f) {
      return false;
    }

    endTerrainPaintStroke(paintState);

    const float sizeX = std::max(sceneAsset.terrainConfig.sizeX, 1e-6f);
    const float sizeZ = std::max(sceneAsset.terrainConfig.sizeZ, 1e-6f);
    const float radius = std::max(sceneAsset.terrainBrushRadius, 1e-4f);
    const float radiusU = radius / sizeX;
    const float radiusV = radius / sizeZ;
    const float centerU =
        glm::clamp(localPosition.x / sizeX + 0.5f, 0.0f, 1.0f);
    const float centerV =
        glm::clamp(localPosition.y / sizeZ + 0.5f, 0.0f, 1.0f);

    const int minX = std::max(
        0, static_cast<int>(std::floor((centerU - radiusU) *
                                       static_cast<float>(paintState.canvasWidth))));
    const int maxX = std::min(
        static_cast<int>(paintState.canvasWidth) - 1,
        static_cast<int>(std::ceil((centerU + radiusU) *
                                   static_cast<float>(paintState.canvasWidth))));
    const int minY = std::max(
        0, static_cast<int>(std::floor((centerV - radiusV) *
                                       static_cast<float>(paintState.canvasHeight))));
    const int maxY = std::min(
        static_cast<int>(paintState.canvasHeight) - 1,
        static_cast<int>(std::ceil((centerV + radiusV) *
                                   static_cast<float>(paintState.canvasHeight))));

    bool changed = false;
    for (int y = minY; y <= maxY; ++y) {
      const float canvasV =
          (static_cast<float>(y) + 0.5f) / static_cast<float>(paintState.canvasHeight);
      const float normalizedV = (canvasV - centerV) / std::max(radiusV, 1e-6f);
      for (int x = minX; x <= maxX; ++x) {
        const float canvasU =
            (static_cast<float>(x) + 0.5f) /
            static_cast<float>(paintState.canvasWidth);
        const float normalizedU =
            (canvasU - centerU) / std::max(radiusU, 1e-6f);
        const float distance =
            std::sqrt(normalizedU * normalizedU + normalizedV * normalizedV);
        if (distance > 1.0f) {
          continue;
        }

        const float eraseAmount = glm::clamp(
            eraseStrength * brushFalloff(distance), 0.0f, 1.0f);
        if (eraseAmount <= 1e-6f) {
          continue;
        }

        const size_t pixelIndex =
            (static_cast<size_t>(y) * static_cast<size_t>(paintState.canvasWidth) +
             static_cast<size_t>(x)) *
            4u;
        const float currentAlpha =
            static_cast<float>(paintState.canvasPixels[pixelIndex + 3]) / 255.0f;
        const float nextAlpha =
            glm::clamp(currentAlpha - eraseAmount, 0.0f, 1.0f);
        const uint8_t nextAlphaByte =
            static_cast<uint8_t>(nextAlpha * 255.0f + 0.5f);
        if (paintState.canvasPixels[pixelIndex + 3] == nextAlphaByte) {
          continue;
        }

        paintState.canvasPixels[pixelIndex + 3] = nextAlphaByte;
        changed = true;
      }
    }

    if (changed) {
      paintState.materialDirty = true;
      paintState.canvasDirty = true;
    }
    return changed;
  }

  void flushTerrainPaintMaterials(bool force = false) {
    if (terrainPaintStates.empty()) {
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool leftMouseDown =
        glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    std::vector<size_t> dirtyTerrainIndices;
    for (size_t terrainIndex = 0; terrainIndex < sceneAssets.size() &&
                                 terrainIndex < terrainPaintStates.size();
         ++terrainIndex) {
      if (sceneAssets[terrainIndex].kind != SceneAssetKind::Terrain) {
        continue;
      }

      TerrainPaintState &paintState = ensureTerrainPaintState(terrainIndex);
      if (!paintState.materialDirty || sceneAssetModels.size() <= terrainIndex ||
          sceneAssetModels[terrainIndex].modelAsset() == nullptr) {
        continue;
      }

      const float secondsSinceUpload =
          std::chrono::duration<float>(now - paintState.lastUploadTime).count();
      if (!force && leftMouseDown && secondsSinceUpload < 0.08f) {
        continue;
      }

      dirtyTerrainIndices.push_back(terrainIndex);
    }

    if (dirtyTerrainIndices.empty()) {
      return;
    }

    backend.waitIdle();
    bool renderItemsChanged = false;
    for (const size_t terrainIndex : dirtyTerrainIndices) {
      TerrainPaintState &paintState = terrainPaintStates[terrainIndex];
      auto &materials = sceneAssetModels[terrainIndex].mutableMaterials();
      if (materials.empty()) {
        continue;
      }

      applyTerrainPaintMaterial(materials.front(), sceneAssets[terrainIndex],
                                paintState);
      sceneAssetModels[terrainIndex].syncMaterialResources(commandContext(),
                                                           deviceContext());
      paintState.materialDirty = false;
      paintState.lastUploadTime = now;
      renderItemsChanged = true;
    }

    if (renderItemsChanged) {
      rebuildSceneRenderItems();
    }
  }

  void saveTerrainPaintCanvasesToDisk() {
    for (size_t terrainIndex = 0; terrainIndex < sceneAssets.size() &&
                                 terrainIndex < terrainPaintStates.size();
         ++terrainIndex) {
      if (sceneAssets[terrainIndex].kind != SceneAssetKind::Terrain) {
        continue;
      }

      TerrainPaintState &paintState = ensureTerrainPaintState(terrainIndex);
      if (!paintState.canvasDirty ||
          sceneAssets[terrainIndex].terrainPaintCanvasPath.empty()) {
        continue;
      }

      if (!writeTerrainPaintCanvasFile(
              sceneAssets[terrainIndex].terrainPaintCanvasPath,
              paintState.canvasPixels)) {
        std::cerr << "Failed to save terrain paint canvas to "
                  << sceneAssets[terrainIndex].terrainPaintCanvasPath << std::endl;
        continue;
      }
      paintState.canvasDirty = false;
    }
  }

  static bool sameTerrainConfig(const TerrainConfig &lhs,
                                const TerrainConfig &rhs) {
    return lhs.sizeX == rhs.sizeX && lhs.sizeZ == rhs.sizeZ &&
           lhs.xSegments == rhs.xSegments && lhs.zSegments == rhs.zSegments &&
           lhs.uvScale == rhs.uvScale && lhs.heightScale == rhs.heightScale &&
           lhs.noiseFrequency == rhs.noiseFrequency &&
           lhs.noiseOctaves == rhs.noiseOctaves &&
           lhs.noisePersistence == rhs.noisePersistence &&
           lhs.noiseLacunarity == rhs.noiseLacunarity &&
           lhs.noiseSeed == rhs.noiseSeed &&
           lhs.heightOffsets == rhs.heightOffsets;
  }

  static glm::mat4 basisTransform(const glm::vec3 &position,
                                  const glm::vec3 &normal,
                                  const glm::vec3 &scale) {
    const glm::vec3 up = glm::normalize(
        glm::length(normal) > 1e-6f ? normal : glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 fallbackTangent =
        std::abs(glm::dot(up, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.98f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(fallbackTangent, up));
    const glm::vec3 bitangent = glm::normalize(glm::cross(up, tangent));

    glm::mat4 transform(1.0f);
    transform[0] = glm::vec4(tangent * scale.x, 0.0f);
    transform[1] = glm::vec4(up * scale.y, 0.0f);
    transform[2] = glm::vec4(bitangent * scale.z, 0.0f);
    transform[3] = glm::vec4(position, 1.0f);
    return transform;
  }

  static glm::vec3 terrainLocalNormal(const TerrainConfig &config, float x,
                                      float z) {
    const float epsilon =
        std::max(std::min(config.sizeX, config.sizeZ) / 512.0f, 0.01f);
    const float heightLeft =
        TerrainGenerator::sampleHeight(config, x - epsilon, z);
    const float heightRight =
        TerrainGenerator::sampleHeight(config, x + epsilon, z);
    const float heightBack =
        TerrainGenerator::sampleHeight(config, x, z - epsilon);
    const float heightFront =
        TerrainGenerator::sampleHeight(config, x, z + epsilon);
    return glm::normalize(glm::vec3(heightLeft - heightRight, epsilon * 2.0f,
                                    heightBack - heightFront));
  }

  static std::optional<glm::vec2>
  intersectRayAabb(const glm::vec3 &origin, const glm::vec3 &direction,
                   const glm::vec3 &boundsMin, const glm::vec3 &boundsMax) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
      if (std::abs(direction[axis]) <= 1e-6f) {
        if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis]) {
          return std::nullopt;
        }
        continue;
      }

      const float invDirection = 1.0f / direction[axis];
      float t0 = (boundsMin[axis] - origin[axis]) * invDirection;
      float t1 = (boundsMax[axis] - origin[axis]) * invDirection;
      if (t0 > t1) {
        std::swap(t0, t1);
      }

      tMin = std::max(tMin, t0);
      tMax = std::min(tMax, t1);
      if (tMin > tMax) {
        return std::nullopt;
      }
    }

    return glm::vec2(tMin, tMax);
  }

  std::optional<TerrainEditHit>
  raycastTerrainFromCursor(const glm::mat4 &view,
                           const glm::mat4 &proj) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse && !debugUiSettings.cameraLookActive) {
      return std::nullopt;
    }

    int activeTerrainIndex = -1;
    const int selectedIndex = debugUiSettings.selectedObjectIndex;
    if (selectedIndex >= 0 &&
        static_cast<size_t>(selectedIndex) < sceneAssets.size() &&
        static_cast<size_t>(selectedIndex) < debugUiSettings.sceneObjects.size() &&
        sceneAssets[static_cast<size_t>(selectedIndex)].kind ==
            SceneAssetKind::Terrain &&
        sceneAssets[static_cast<size_t>(selectedIndex)].terrainEditMode &&
        debugUiSettings.sceneObjects[static_cast<size_t>(selectedIndex)].visible) {
      activeTerrainIndex = selectedIndex;
    } else {
      for (size_t index = 0;
           index < sceneAssets.size() &&
           index < debugUiSettings.sceneObjects.size(); ++index) {
        if (sceneAssets[index].kind != SceneAssetKind::Terrain ||
            !sceneAssets[index].terrainEditMode ||
            !debugUiSettings.sceneObjects[index].visible) {
          continue;
        }
        activeTerrainIndex = static_cast<int>(index);
        break;
      }
    }

    if (activeTerrainIndex < 0) {
      return std::nullopt;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.handle(), &cursorX, &cursorY);

    const WindowSize windowSize = window.windowSize();
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

    const glm::vec3 rayOrigin = debugUiSettings.cameraPosition;
    const glm::vec3 rayNear = glm::vec3(nearWorld) / nearWorld.w;
    const glm::vec3 rayFar = glm::vec3(farWorld) / farWorld.w;
    const glm::vec3 rayDirection = glm::normalize(rayFar - rayNear);

    const size_t terrainIndex = static_cast<size_t>(activeTerrainIndex);
    const SceneAssetInstance &terrainAsset = sceneAssets[terrainIndex];
    const glm::mat4 terrainModel =
        AppSceneController::sceneTransformMatrix(
            debugUiSettings.sceneObjects[terrainIndex].transform);
    const glm::mat4 inverseTerrainModel = glm::inverse(terrainModel);
    const glm::vec3 localOrigin =
        glm::vec3(inverseTerrainModel * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 localDirection = glm::normalize(glm::vec3(
        inverseTerrainModel * glm::vec4(rayDirection, 0.0f)));

    const float heightBound = std::max(
                                  std::max(std::abs(terrainAsset.terrainConfig.heightScale),
                                           TerrainGenerator::maxHeightOffsetMagnitude(
                                               terrainAsset.terrainConfig)),
                                  0.5f) +
                              1.0f;
    const auto hitRange = intersectRayAabb(
        localOrigin, localDirection,
        {-terrainAsset.terrainConfig.sizeX * 0.5f, -heightBound,
         -terrainAsset.terrainConfig.sizeZ * 0.5f},
        {terrainAsset.terrainConfig.sizeX * 0.5f, heightBound,
         terrainAsset.terrainConfig.sizeZ * 0.5f});
    if (!hitRange.has_value()) {
      return std::nullopt;
    }

    const float startT = std::max(hitRange->x, 0.0f);
    const float endT = hitRange->y;
    const uint32_t stepCount = std::max(
        terrainAsset.terrainConfig.xSegments + terrainAsset.terrainConfig.zSegments,
        64u);
    float previousT = startT;
    glm::vec3 previousPoint = localOrigin + localDirection * previousT;
    float previousDelta =
        previousPoint.y - TerrainGenerator::sampleHeight(
                              terrainAsset.terrainConfig, previousPoint.x,
                              previousPoint.z);

    for (uint32_t step = 1; step <= stepCount; ++step) {
      const float alpha =
          static_cast<float>(step) / static_cast<float>(stepCount);
      const float currentT = glm::mix(startT, endT, alpha);
      const glm::vec3 currentPoint = localOrigin + localDirection * currentT;
      const float currentDelta =
          currentPoint.y - TerrainGenerator::sampleHeight(
                               terrainAsset.terrainConfig, currentPoint.x,
                               currentPoint.z);
      const bool crossedSurface =
          (previousDelta >= 0.0f && currentDelta <= 0.0f) ||
          std::abs(currentDelta) <= 1e-4f;
      if (!crossedSurface) {
        previousT = currentT;
        previousPoint = currentPoint;
        previousDelta = currentDelta;
        continue;
      }

      float lowT = previousT;
      float highT = currentT;
      for (int iteration = 0; iteration < 10; ++iteration) {
        const float midT = 0.5f * (lowT + highT);
        const glm::vec3 midPoint = localOrigin + localDirection * midT;
        const float midDelta =
            midPoint.y - TerrainGenerator::sampleHeight(
                             terrainAsset.terrainConfig, midPoint.x, midPoint.z);
        if (midDelta > 0.0f) {
          lowT = midT;
        } else {
          highT = midT;
        }
      }

      const float hitT = 0.5f * (lowT + highT);
      const glm::vec3 localHit = localOrigin + localDirection * hitT;
      const float hitHeight = TerrainGenerator::sampleHeight(
          terrainAsset.terrainConfig, localHit.x, localHit.z);
      const glm::vec3 localPoint(localHit.x, hitHeight, localHit.z);
      const glm::vec3 localNormal =
          terrainLocalNormal(terrainAsset.terrainConfig, localPoint.x,
                             localPoint.z);
      const glm::vec3 worldPoint =
          glm::vec3(terrainModel * glm::vec4(localPoint, 1.0f));
      const glm::vec3 worldNormal = glm::normalize(
          glm::transpose(glm::inverse(glm::mat3(terrainModel))) * localNormal);
      return TerrainEditHit{
          .terrainIndex = terrainIndex,
          .localPosition = localPoint,
          .worldPosition = worldPoint,
          .worldNormal = worldNormal,
      };
    }

    return std::nullopt;
  }

  void updateTerrainWireframeOverlay() {
    if (debugOverlayPass == nullptr) {
      return;
    }

    const size_t terrainCount =
        std::min(sceneAssets.size(), debugUiSettings.sceneObjects.size());
    int wireframeTerrainIndex = -1;
    for (size_t index = 0; index < terrainCount; ++index) {
      if (sceneAssets[index].kind != SceneAssetKind::Terrain ||
          !sceneAssets[index].terrainWireframeVisible ||
          !debugUiSettings.sceneObjects[index].visible) {
        continue;
      }
      wireframeTerrainIndex = static_cast<int>(index);
      break;
    }

    if (wireframeTerrainIndex < 0) {
      activeTerrainWireframeIndex = -1;
      activeTerrainWireframeConfig.reset();
      debugOverlayPass->setCustomVisible(false);
      debugOverlayPass->setCustomSegments({});
      debugOverlayPass->setCustomMarkers({});
      return;
    }

    const TerrainConfig &terrainConfig =
        sceneAssets[static_cast<size_t>(wireframeTerrainIndex)].terrainConfig;
    if (activeTerrainWireframeIndex != wireframeTerrainIndex ||
        !activeTerrainWireframeConfig.has_value() ||
        !sameTerrainConfig(*activeTerrainWireframeConfig, terrainConfig)) {
      terrainWireframeMesh = buildTerrainWireframeMesh(terrainConfig);
      terrainWireframeMesh.createVertexBuffer(commandContext(), deviceContext());
      terrainWireframeMesh.createIndexBuffer(commandContext(), deviceContext());
      activeTerrainWireframeIndex = wireframeTerrainIndex;
      activeTerrainWireframeConfig = terrainConfig;
    }

    debugOverlayPass->setCustomMarkerMesh(terrainWireframeMesh);
    debugOverlayPass->setCustomSegments({});
    debugOverlayPass->setCustomMarkers({DebugOverlayInstance{
        .model = AppSceneController::sceneTransformMatrix(
            debugUiSettings
                .sceneObjects[static_cast<size_t>(wireframeTerrainIndex)]
                .transform),
        .color = {0.08f, 0.08f, 0.08f, 1.0f},
    }});
    debugOverlayPass->setCustomVisible(true);
  }

  void loadSceneAssetModel(size_t index) {
    if (index >= sceneAssets.size() || index >= sceneAssetModels.size()) {
      return;
    }

    SceneAssetInstance &sceneAsset = sceneAssets[index];
    if (sceneAsset.kind == SceneAssetKind::Terrain) {
      TerrainPaintState &paintState = ensureTerrainPaintState(index);
      std::vector<ImportedMaterialData> existingMaterials;
      if (sceneAssetModels[index].modelAsset() != nullptr) {
        existingMaterials = sceneAssetModels[index].materials();
      }
      sceneAssetModels[index].loadTerrain(
          sceneAsset.terrainConfig,
          AppSceneController::sceneAssetName(sceneAsset, index),
          commandContext(), deviceContext(), sceneDescriptorSetLayout(),
          sceneSecondaryDescriptorSetLayout(), frameGeometryUniforms, sampler,
          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT,
          [&sceneAsset, &paintState, existingMaterials = std::move(existingMaterials)](
              std::vector<ImportedMaterialData> &materials) {
            if (materials.empty()) {
              return;
            }
            if (!existingMaterials.empty()) {
              const size_t materialCount =
                  std::min(existingMaterials.size(), materials.size());
              for (size_t materialIndex = 0; materialIndex < materialCount;
                   ++materialIndex) {
                materials[materialIndex] = existingMaterials[materialIndex];
              }
            } else if (!sceneAsset.terrainMaterialOverrides.empty()) {
              const size_t materialCount = std::min(
                  sceneAsset.terrainMaterialOverrides.size(), materials.size());
              for (size_t materialIndex = 0; materialIndex < materialCount;
                   ++materialIndex) {
                applyTerrainMaterialOverride(
                    materials[materialIndex],
                    sceneAsset.terrainMaterialOverrides[materialIndex]);
              }
            }
            applyTerrainPaintMaterial(materials.front(), sceneAsset, paintState);
          });
      paintState.materialDirty = false;
      paintState.lastUploadTime = std::chrono::steady_clock::now();
      return;
    }

    if (sceneAsset.assetPath.empty()) {
      return;
    }
    sceneAssetModels[index].loadFromFile(
        sceneAsset.assetPath, commandContext(), deviceContext(),
        sceneDescriptorSetLayout(), sceneSecondaryDescriptorSetLayout(),
        frameGeometryUniforms, sampler, DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
    if (engineConfig.onSceneAssetLoaded != nullptr &&
        sceneAssetModels[index].modelAsset() != nullptr) {
      engineConfig.onSceneAssetLoaded(sceneAsset,
                                      *sceneAssetModels[index].modelAsset());
    }
  }

  void reloadTerrainAsset(size_t terrainIndex) {
    if (terrainIndex >= sceneAssets.size() || terrainIndex >= sceneAssetModels.size()) {
      return;
    }

    backend.waitIdle();
    loadSceneAssetModel(terrainIndex);
    sceneDefinition.assets = sceneAssets;
    syncSceneObjectsWithAssets();
    rebuildSceneRenderItems();
  }

  void updateTerrainSculpting(const std::optional<TerrainEditHit> &hit,
                              float deltaSeconds) {
    ImGuiIO &io = ImGui::GetIO();
    const bool leftMouseDown =
        glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (io.WantCaptureMouse) {
      activeTerrainFlattenStroke.reset();
      for (auto &paintState : terrainPaintStates) {
        endTerrainPaintStroke(paintState);
      }
      return;
    }

    if (!leftMouseDown) {
      activeTerrainFlattenStroke.reset();
      for (auto &paintState : terrainPaintStates) {
        endTerrainPaintStroke(paintState);
      }
      return;
    }

    if (!hit.has_value()) {
      for (auto &paintState : terrainPaintStates) {
        endTerrainPaintStroke(paintState);
      }
      return;
    }

    SceneAssetInstance &terrainAsset = sceneAssets[hit->terrainIndex];
    const float sculptSpeedPerSecond = 2.4f;
    const float colorBlendPerSecond = 5.0f;
    const float brushStep =
        sculptSpeedPerSecond * std::max(deltaSeconds, 1.0f / 240.0f);
    bool changed = false;
    if (terrainAsset.terrainBrushTexturePaintMode) {
      activeTerrainFlattenStroke.reset();
      changed = applyTerrainPaintStamp(
          hit->terrainIndex, {hit->localPosition.x, hit->localPosition.z});
    } else if (terrainAsset.terrainBrushColorPaintMode) {
      activeTerrainFlattenStroke.reset();
      endTerrainPaintStroke(ensureTerrainPaintState(hit->terrainIndex));
      const float colorBlend =
          colorBlendPerSecond * std::max(deltaSeconds, 1.0f / 240.0f);
      changed = TerrainGenerator::applyColorBrush(
          terrainAsset.terrainConfig,
          {hit->localPosition.x, hit->localPosition.z},
          terrainAsset.terrainBrushRadius, terrainAsset.terrainBrushColor,
          colorBlend);
      changed |= eraseTerrainPaintCanvas(
          hit->terrainIndex, {hit->localPosition.x, hit->localPosition.z},
          colorBlend);
    } else if (terrainAsset.terrainBrushFlattenMode) {
      endTerrainPaintStroke(ensureTerrainPaintState(hit->terrainIndex));
      if (!activeTerrainFlattenStroke.has_value() ||
          activeTerrainFlattenStroke->terrainIndex != hit->terrainIndex) {
        activeTerrainFlattenStroke = TerrainFlattenStroke{
            .terrainIndex = hit->terrainIndex,
            .targetHeight = hit->localPosition.y,
        };
      }
      changed = TerrainGenerator::applyFlattenBrush(
          terrainAsset.terrainConfig,
          {hit->localPosition.x, hit->localPosition.z},
          terrainAsset.terrainBrushRadius,
          activeTerrainFlattenStroke->targetHeight, brushStep);
    } else {
      activeTerrainFlattenStroke.reset();
      endTerrainPaintStroke(ensureTerrainPaintState(hit->terrainIndex));
      const float heightStep =
          (terrainAsset.terrainBrushLowerMode ? -1.0f : 1.0f) * brushStep;
      changed = TerrainGenerator::applyBrush(
          terrainAsset.terrainConfig,
          {hit->localPosition.x, hit->localPosition.z},
          terrainAsset.terrainBrushRadius, heightStep);
    }

    if (!changed) {
      return;
    }

    if (!terrainAsset.terrainBrushTexturePaintMode) {
      reloadTerrainAsset(hit->terrainIndex);
    }
  }

  void updateTerrainEditOverlay(const glm::mat4 &view, const glm::mat4 &proj,
                                float deltaSeconds) {
    if (debugOverlayPass == nullptr) {
      return;
    }

    const auto hit = raycastTerrainFromCursor(view, proj);
    if (!hit.has_value()) {
      debugOverlayPass->setToolVisible(false);
      debugOverlayPass->setToolMarkers({});
      return;
    }

    SceneAssetInstance &terrainAsset = sceneAssets[hit->terrainIndex];
    ImGuiIO &io = ImGui::GetIO();
    bool radiusChanged = false;
    bool brushModeChanged = false;
    if (!io.WantCaptureKeyboard) {
      const float radiusStep = std::max(deltaSeconds * 4.0f, 0.02f);
      if (glfwGetKey(window.handle(), GLFW_KEY_UP) == GLFW_PRESS) {
        terrainAsset.terrainBrushRadius = std::min(
            terrainAsset.terrainBrushRadius + radiusStep, 128.0f);
        radiusChanged = true;
      }
      if (glfwGetKey(window.handle(), GLFW_KEY_DOWN) == GLFW_PRESS) {
        terrainAsset.terrainBrushRadius = std::max(
            terrainAsset.terrainBrushRadius - radiusStep, 0.05f);
        radiusChanged = true;
      }
      if (glfwGetKey(window.handle(), GLFW_KEY_LEFT) == GLFW_PRESS &&
          !terrainAsset.terrainBrushLowerMode) {
        terrainAsset.terrainBrushLowerMode = true;
        brushModeChanged = true;
      }
      if (glfwGetKey(window.handle(), GLFW_KEY_RIGHT) == GLFW_PRESS &&
          terrainAsset.terrainBrushLowerMode) {
        terrainAsset.terrainBrushLowerMode = false;
        brushModeChanged = true;
      }
    }

    if (radiusChanged || brushModeChanged) {
      sceneDefinition.assets = sceneAssets;
    }

    const float lineHeight =
        std::max(terrainAsset.terrainBrushRadius * 0.75f, 0.6f);
    const glm::vec4 brushColor =
        terrainAsset.terrainBrushTexturePaintMode
            ? glm::vec4(1.0f, 1.0f, 1.0f, 0.92f)
            : (terrainAsset.terrainBrushColorPaintMode
                   ? terrainAsset.terrainBrushColor
            : (terrainAsset.terrainBrushFlattenMode
            ? glm::vec4(0.22f, 0.72f, 0.96f, 1.0f)
            : (terrainAsset.terrainBrushLowerMode
                   ? glm::vec4(0.92f, 0.28f, 0.22f, 1.0f)
                   : glm::vec4(0.95f, 0.85f, 0.2f, 1.0f))));
    debugOverlayPass->setToolMarkerMesh(terrainBrushIndicatorMesh);
    debugOverlayPass->setToolMarkers({DebugOverlayInstance{
        .model = basisTransform(
            hit->worldPosition + hit->worldNormal * 0.01f, hit->worldNormal,
            {terrainAsset.terrainBrushRadius, lineHeight,
             terrainAsset.terrainBrushRadius}),
        .color = brushColor,
    }});
    debugOverlayPass->setToolVisible(true);
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

  void updateBoneOverlay() {
    if (debugOverlayPass == nullptr) {
      return;
    }

    std::vector<DebugOverlayInstance> boneSegments;
    std::vector<DebugOverlayInstance> boneMarkers;

    if (debugUiSettings.bonesVisible) {
      const size_t objectCount =
          std::min(sceneAssetModels.size(), debugUiSettings.sceneObjects.size());
      for (size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
        if (!debugUiSettings.sceneObjects[objectIndex].visible) {
          continue;
        }

        const RenderableModel &model = sceneAssetModels[objectIndex];
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
            debugUiSettings.sceneObjects[objectIndex].transform);
        const int selectedBoneIndex =
            static_cast<int>(objectIndex) == debugUiSettings.selectedObjectIndex
                ? debugUiSettings.selectedBoneIndex
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
              debugUiSettings.boneMarkerScale *
              (static_cast<int>(nodeIndex) == selectedBoneIndex ? 1.45f : 1.0f);

          boneMarkers.push_back(DebugOverlayInstance{
              .model = glm::translate(glm::mat4(1.0f), jointPosition) *
                       glm::scale(glm::mat4(1.0f), glm::vec3(jointScale)),
              .color = color,
          });

          const int parentIndex =
              skeleton->nodes[nodeIndex].parentIndex;
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
              debugUiSettings.boneMarkerScale * 0.8f, length * 0.045f);

          boneSegments.push_back(DebugOverlayInstance{
              .model = debugOrientationTransform(
                  parentPosition, direction, segmentRadius, segmentRadius,
                  length),
              .color = color,
          });
        }
      }
    }

    debugOverlayPass->setBonesVisible(debugUiSettings.bonesVisible);
    debugOverlayPass->setBoneSegments(std::move(boneSegments));
    debugOverlayPass->setBoneMarkers(std::move(boneMarkers));
  }

  void reloadSceneAssets() {
    backend.waitIdle();
    std::unordered_map<std::string, TerrainPaintState> previousPaintStates;
    for (size_t index = 0; index < sceneAssets.size() &&
                         index < terrainPaintStates.size();
         ++index) {
      if (sceneAssets[index].kind != SceneAssetKind::Terrain ||
          sceneAssets[index].terrainPaintCanvasPath.empty()) {
        continue;
      }
      previousPaintStates.emplace(sceneAssets[index].terrainPaintCanvasPath,
                                  std::move(terrainPaintStates[index]));
    }

    sceneAssets = resolvedSceneAssets();
    terrainPaintStates.clear();
    terrainPaintStates.resize(sceneAssets.size());
    for (size_t index = 0; index < sceneAssets.size(); ++index) {
      if (sceneAssets[index].kind != SceneAssetKind::Terrain ||
          sceneAssets[index].terrainPaintCanvasPath.empty()) {
        continue;
      }
      const auto previousStateIt =
          previousPaintStates.find(sceneAssets[index].terrainPaintCanvasPath);
      if (previousStateIt == previousPaintStates.end()) {
        continue;
      }
      terrainPaintStates[index] = std::move(previousStateIt->second);
    }

    sceneAssetModels.clear();
    sceneAssetModels.resize(sceneAssets.size());

    for (size_t index = 0; index < sceneAssets.size(); ++index) {
      loadSceneAssetModel(index);
    }
    syncSceneObjectsWithAssets();
    rebuildSceneRenderItems();
  }

  void loadDebugSessionFromDisk() {
    if (!engineConfig.enableDebugSessionPersistence) {
      return;
    }

    try {
      std::vector<SceneAssetInstance> loadedSceneAssets;
      DebugSessionIO::loadDebugSession(debugSessionPath(), debugUiSettings,
                                       &loadedSceneAssets);
      if (!loadedSceneAssets.empty()) {
        sceneDefinition.assets = std::move(loadedSceneAssets);
      }
      applyEngineConfigOverrides(debugUiSettings);
      ensureDefaultEnvironmentPath();
    } catch (const std::exception &e) {
      std::cerr << "Failed to load debug session: " << e.what() << std::endl;
    }
  }

  void saveDebugSessionToDisk() {
    if (!engineConfig.enableDebugSessionPersistence) {
      return;
    }

    flushTerrainPaintMaterials(true);
    saveTerrainPaintCanvasesToDisk();
    if (!DebugSessionIO::saveDebugSession(debugSessionPath(),
                                          debugUiSettings,
                                          persistedSceneAssets())) {
      std::cerr << "Failed to save debug session to "
                << debugSessionPath().string() << std::endl;
    }
  }

  void applyLoadedDebugSettings() {
    ensureDefaultEnvironmentPath();
    backend.waitIdle();
    if (debugUiSettings.syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    imageBasedLighting.rebuild(deviceContext(), commandContext(),
                               debugUiSettings.iblBakeSettings);
    renderer.recreate(deviceContext(), swapchainContext());
    reloadSceneAssets();
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
    updateTerrainWireframeOverlay();
    updateTerrainEditOverlay(geometryUniformData.view, geometryUniformData.proj,
                             deltaSeconds);
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
