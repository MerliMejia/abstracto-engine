#pragma once

#include "assets/RenderableModel.h"
#include "backend/AppWindow.h"
#include "backend/VulkanBackend.h"
#include "debug/TerrainDebugMeshes.h"
#include "editor/DebugUIState.h"
#include "passes/DebugOverlayPass.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include "world/TerrainEditing.h"
#include "world/TerrainQueries.h"
#include <stb_image.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

struct DefaultEngineTerrainPaintState {
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

struct DefaultEngineTerrainFlattenStroke {
  size_t terrainIndex = 0;
  float targetHeight = 0.0f;
};

struct DefaultEngineTerrainEditHit {
  size_t terrainIndex = 0;
  glm::vec3 localPosition{0.0f};
  glm::vec3 worldPosition{0.0f};
  glm::vec3 worldNormal{0.0f, 1.0f, 0.0f};
};

struct DefaultEngineTerrainRuntimeContext {
  SceneDefinition &sceneDefinition;
  std::vector<SceneAssetInstance> &sceneAssets;
  std::vector<RenderableModel> &sceneAssetModels;
  std::vector<DefaultEngineTerrainPaintState> &terrainPaintStates;
  std::optional<DefaultEngineTerrainFlattenStroke> &activeTerrainFlattenStroke;
  DefaultDebugUISettings &debugUiSettings;
  AppWindow &window;
  VulkanBackend &backend;
  DebugOverlayPass *debugOverlayPass = nullptr;
  TypedMesh<Vertex> &terrainWireframeMesh;
  TypedMesh<Vertex> &terrainBrushIndicatorMesh;
  int &activeTerrainWireframeIndex;
  std::optional<TerrainConfig> &activeTerrainWireframeConfig;
};

class DefaultEngineTerrainRuntime {
public:
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

  static std::filesystem::path
  defaultTerrainPaintCanvasPath(size_t terrainIndex,
                                const SceneAssetInstance &sceneAsset) {
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

  static glm::vec4
  sampleTerrainBrushTexture(const DefaultEngineTerrainPaintState &paintState,
                            float u, float v) {
    if (paintState.brushPixels.empty() || paintState.brushWidth <= 0 ||
        paintState.brushHeight <= 0) {
      return glm::vec4(0.0f);
    }

    const int x = std::clamp(
        static_cast<int>(std::floor(glm::clamp(u, 0.0f, 1.0f) *
                                    static_cast<float>(paintState.brushWidth))),
        0, paintState.brushWidth - 1);
    const int y = std::clamp(
        static_cast<int>(std::floor(glm::clamp(v, 0.0f, 1.0f) *
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
    const int rotationIndex =
        static_cast<int>(std::floor(rotateNoise * 4.0f)) % 4;
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

  static void applyTerrainPaintMaterial(
      ImportedMaterialData &material, const SceneAssetInstance &sceneAsset,
      const DefaultEngineTerrainPaintState &paintState) {
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

  static void syncTerrainMaterialOverridesInto(
      std::vector<SceneAssetInstance> &assets,
      const std::vector<RenderableModel> &sceneAssetModels) {
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

  static DefaultEngineTerrainPaintState &
  ensureTerrainPaintState(DefaultEngineTerrainRuntimeContext &context,
                          size_t terrainIndex) {
    if (context.terrainPaintStates.size() < context.sceneAssets.size()) {
      context.terrainPaintStates.resize(context.sceneAssets.size());
    }

    DefaultEngineTerrainPaintState &paintState =
        context.terrainPaintStates[terrainIndex];
    SceneAssetInstance &sceneAsset = context.sceneAssets[terrainIndex];
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

  static void endTerrainPaintStroke(DefaultEngineTerrainPaintState &paintState) {
    if (!paintState.strokeActive) {
      return;
    }
    paintState.strokeActive = false;
    paintState.strokeBasePixels.clear();
    paintState.strokePixels.clear();
  }

  static bool applyTerrainPaintStamp(DefaultEngineTerrainRuntimeContext &context,
                                     size_t terrainIndex,
                                     const glm::vec2 &localPosition) {
    if (terrainIndex >= context.sceneAssets.size()) {
      return false;
    }

    SceneAssetInstance &sceneAsset = context.sceneAssets[terrainIndex];
    DefaultEngineTerrainPaintState &paintState =
        ensureTerrainPaintState(context, terrainIndex);
    if (sceneAsset.kind != SceneAssetKind::Terrain ||
        paintState.canvasPixels.empty() || paintState.brushPixels.empty()) {
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
          (static_cast<float>(y) + 0.5f) /
          static_cast<float>(paintState.canvasHeight);
      const float normalizedV = (canvasV - centerV) / std::max(radiusV, 1e-6f);
      for (int x = minX; x <= maxX; ++x) {
        const float canvasU =
            (static_cast<float>(x) + 0.5f) /
            static_cast<float>(paintState.canvasWidth);
        const float normalizedU = (canvasU - centerU) / std::max(radiusU, 1e-6f);
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
            static_cast<float>(paintState.strokeBasePixels[pixelIndex + 3]) /
                255.0f);
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

  static bool bucketPaintTerrainTexture(DefaultEngineTerrainRuntimeContext &context,
                                        size_t terrainIndex) {
    if (terrainIndex >= context.sceneAssets.size()) {
      return false;
    }

    SceneAssetInstance &sceneAsset = context.sceneAssets[terrainIndex];
    DefaultEngineTerrainPaintState &paintState =
        ensureTerrainPaintState(context, terrainIndex);
    if (sceneAsset.kind != SceneAssetKind::Terrain ||
        paintState.canvasPixels.empty() || paintState.brushPixels.empty() ||
        paintState.canvasWidth == 0 || paintState.canvasHeight == 0) {
      return false;
    }

    endTerrainPaintStroke(paintState);

    const auto toByte = [](float value) {
      return static_cast<uint8_t>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    const float tiledUvScaleX =
        std::max(sceneAsset.terrainConfig.uvScale.x, 1e-4f);
    const float tiledUvScaleY =
        std::max(sceneAsset.terrainConfig.uvScale.y, 1e-4f);
    const float opacity = glm::clamp(sceneAsset.terrainBrushOpacity, 0.0f, 1.0f);

    bool changed = false;
    for (uint32_t y = 0; y < paintState.canvasHeight; ++y) {
      const float canvasV =
          (static_cast<float>(y) + 0.5f) /
          static_cast<float>(paintState.canvasHeight);
      for (uint32_t x = 0; x < paintState.canvasWidth; ++x) {
        const float canvasU =
            (static_cast<float>(x) + 0.5f) /
            static_cast<float>(paintState.canvasWidth);
        const glm::vec2 variedBrushUv = variedTerrainBrushUv(
            canvasU * tiledUvScaleX, canvasV * tiledUvScaleY,
            sceneAsset.terrainBrushTextureVariation);
        const glm::vec4 brushSample = sampleTerrainBrushTexture(
            paintState, variedBrushUv.x, variedBrushUv.y);
        const size_t pixelIndex =
            (static_cast<size_t>(y) * static_cast<size_t>(paintState.canvasWidth) +
             static_cast<size_t>(x)) *
            4u;
        const uint8_t nextR = toByte(brushSample.r);
        const uint8_t nextG = toByte(brushSample.g);
        const uint8_t nextB = toByte(brushSample.b);
        const uint8_t nextA = toByte(brushSample.a * opacity);
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

  static bool eraseTerrainPaintCanvas(DefaultEngineTerrainRuntimeContext &context,
                                      size_t terrainIndex,
                                      const glm::vec2 &localPosition,
                                      float eraseStrength) {
    if (terrainIndex >= context.sceneAssets.size()) {
      return false;
    }

    SceneAssetInstance &sceneAsset = context.sceneAssets[terrainIndex];
    DefaultEngineTerrainPaintState &paintState =
        ensureTerrainPaintState(context, terrainIndex);
    if (sceneAsset.kind != SceneAssetKind::Terrain ||
        paintState.canvasPixels.empty() || eraseStrength <= 1e-6f) {
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
          (static_cast<float>(y) + 0.5f) /
          static_cast<float>(paintState.canvasHeight);
      const float normalizedV = (canvasV - centerV) / std::max(radiusV, 1e-6f);
      for (int x = minX; x <= maxX; ++x) {
        const float canvasU =
            (static_cast<float>(x) + 0.5f) /
            static_cast<float>(paintState.canvasWidth);
        const float normalizedU = (canvasU - centerU) / std::max(radiusU, 1e-6f);
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

  static void flushTerrainPaintMaterials(
      DefaultEngineTerrainRuntimeContext &context, bool force,
      const std::function<void()> &rebuildSceneRenderItems) {
    if (context.terrainPaintStates.empty()) {
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool leftMouseDown =
        glfwGetMouseButton(context.window.handle(), GLFW_MOUSE_BUTTON_LEFT) ==
        GLFW_PRESS;
    std::vector<size_t> dirtyTerrainIndices;
    for (size_t terrainIndex = 0; terrainIndex < context.sceneAssets.size() &&
                                 terrainIndex < context.terrainPaintStates.size();
         ++terrainIndex) {
      if (context.sceneAssets[terrainIndex].kind != SceneAssetKind::Terrain) {
        continue;
      }

      DefaultEngineTerrainPaintState &paintState =
          ensureTerrainPaintState(context, terrainIndex);
      if (!paintState.materialDirty ||
          context.sceneAssetModels.size() <= terrainIndex ||
          context.sceneAssetModels[terrainIndex].modelAsset() == nullptr) {
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

    context.backend.waitIdle();
    bool renderItemsChanged = false;
    for (const size_t terrainIndex : dirtyTerrainIndices) {
      DefaultEngineTerrainPaintState &paintState =
          context.terrainPaintStates[terrainIndex];
      auto &materials = context.sceneAssetModels[terrainIndex].mutableMaterials();
      if (materials.empty()) {
        continue;
      }

      applyTerrainPaintMaterial(materials.front(), context.sceneAssets[terrainIndex],
                                paintState);
      context.sceneAssetModels[terrainIndex].syncMaterialResources(
          context.backend.commands(), context.backend.device());
      paintState.materialDirty = false;
      paintState.lastUploadTime = now;
      renderItemsChanged = true;
    }

    if (renderItemsChanged) {
      rebuildSceneRenderItems();
    }
  }

  static void saveTerrainPaintCanvasesToDisk(
      DefaultEngineTerrainRuntimeContext &context) {
    for (size_t terrainIndex = 0; terrainIndex < context.sceneAssets.size() &&
                                 terrainIndex < context.terrainPaintStates.size();
         ++terrainIndex) {
      if (context.sceneAssets[terrainIndex].kind != SceneAssetKind::Terrain) {
        continue;
      }

      DefaultEngineTerrainPaintState &paintState =
          ensureTerrainPaintState(context, terrainIndex);
      if (!paintState.canvasDirty ||
          context.sceneAssets[terrainIndex].terrainPaintCanvasPath.empty()) {
        continue;
      }

      if (!writeTerrainPaintCanvasFile(
              context.sceneAssets[terrainIndex].terrainPaintCanvasPath,
              paintState.canvasPixels)) {
        std::cerr << "Failed to save terrain paint canvas to "
                  << context.sceneAssets[terrainIndex].terrainPaintCanvasPath
                  << std::endl;
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
    return TerrainQueries::sampleLocalNormal(config, x, z);
  }

  static std::optional<glm::vec2>
  intersectRayAabb(const glm::vec3 &origin, const glm::vec3 &direction,
                   const glm::vec3 &boundsMin, const glm::vec3 &boundsMax) {
    return TerrainQueries::intersectRayAabb(origin, direction, boundsMin,
                                            boundsMax);
  }

  static std::optional<DefaultEngineTerrainEditHit>
  raycastTerrainFromCursor(DefaultEngineTerrainRuntimeContext &context,
                           const glm::mat4 &view, const glm::mat4 &proj) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse && !context.debugUiSettings.cameraLookActive) {
      return std::nullopt;
    }

    int activeTerrainIndex = -1;
    const int selectedIndex = context.debugUiSettings.selectedObjectIndex;
    if (selectedIndex >= 0 &&
        static_cast<size_t>(selectedIndex) < context.sceneAssets.size() &&
        static_cast<size_t>(selectedIndex) <
            context.debugUiSettings.sceneObjects.size() &&
        context.sceneAssets[static_cast<size_t>(selectedIndex)].kind ==
            SceneAssetKind::Terrain &&
        context.sceneAssets[static_cast<size_t>(selectedIndex)].terrainEditMode &&
        context.debugUiSettings.sceneObjects[static_cast<size_t>(selectedIndex)]
            .visible) {
      activeTerrainIndex = selectedIndex;
    } else {
      for (size_t index = 0;
           index < context.sceneAssets.size() &&
           index < context.debugUiSettings.sceneObjects.size();
           ++index) {
        if (context.sceneAssets[index].kind != SceneAssetKind::Terrain ||
            !context.sceneAssets[index].terrainEditMode ||
            !context.debugUiSettings.sceneObjects[index].visible) {
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

    const size_t terrainIndex = static_cast<size_t>(activeTerrainIndex);
    const SceneAssetInstance &terrainAsset = context.sceneAssets[terrainIndex];
    const glm::mat4 terrainModel = AppSceneController::sceneTransformMatrix(
        context.debugUiSettings.sceneObjects[terrainIndex].transform);
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
    return DefaultEngineTerrainEditHit{
        .terrainIndex = terrainIndex,
        .localPosition = localHit->position,
        .worldPosition = worldPoint,
        .worldNormal = worldNormal,
    };
  }

  static void updateTerrainWireframeOverlay(
      DefaultEngineTerrainRuntimeContext &context) {
    if (context.debugOverlayPass == nullptr) {
      return;
    }

    const size_t terrainCount =
        std::min(context.sceneAssets.size(),
                 context.debugUiSettings.sceneObjects.size());
    int wireframeTerrainIndex = -1;
    for (size_t index = 0; index < terrainCount; ++index) {
      if (context.sceneAssets[index].kind != SceneAssetKind::Terrain ||
          !context.sceneAssets[index].terrainWireframeVisible ||
          !context.debugUiSettings.sceneObjects[index].visible) {
        continue;
      }
      wireframeTerrainIndex = static_cast<int>(index);
      break;
    }

    if (wireframeTerrainIndex < 0) {
      context.activeTerrainWireframeIndex = -1;
      context.activeTerrainWireframeConfig.reset();
      context.debugOverlayPass->setCustomVisible(false);
      context.debugOverlayPass->setCustomSegments({});
      context.debugOverlayPass->setCustomMarkers({});
      return;
    }

    const TerrainConfig &terrainConfig =
        context.sceneAssets[static_cast<size_t>(wireframeTerrainIndex)]
            .terrainConfig;
    if (context.activeTerrainWireframeIndex != wireframeTerrainIndex ||
        !context.activeTerrainWireframeConfig.has_value() ||
        !sameTerrainConfig(*context.activeTerrainWireframeConfig, terrainConfig)) {
      context.terrainWireframeMesh = buildTerrainWireframeMesh(terrainConfig);
      context.terrainWireframeMesh.createVertexBuffer(context.backend.commands(),
                                                     context.backend.device());
      context.terrainWireframeMesh.createIndexBuffer(context.backend.commands(),
                                                    context.backend.device());
      context.activeTerrainWireframeIndex = wireframeTerrainIndex;
      context.activeTerrainWireframeConfig = terrainConfig;
    }

    context.debugOverlayPass->setCustomMarkerMesh(context.terrainWireframeMesh);
    context.debugOverlayPass->setCustomSegments({});
    context.debugOverlayPass->setCustomMarkers({DebugOverlayInstance{
        .model = AppSceneController::sceneTransformMatrix(
            context.debugUiSettings
                .sceneObjects[static_cast<size_t>(wireframeTerrainIndex)]
                .transform),
        .color = {0.08f, 0.08f, 0.08f, 1.0f},
    }});
    context.debugOverlayPass->setCustomVisible(true);
  }

  static void updateTerrainSculpting(
      DefaultEngineTerrainRuntimeContext &context,
      const std::optional<DefaultEngineTerrainEditHit> &hit, float deltaSeconds,
      const std::function<void(size_t)> &reloadTerrainAsset) {
    ImGuiIO &io = ImGui::GetIO();
    const bool leftMouseDown =
        glfwGetMouseButton(context.window.handle(), GLFW_MOUSE_BUTTON_LEFT) ==
        GLFW_PRESS;
    if (io.WantCaptureMouse) {
      context.activeTerrainFlattenStroke.reset();
      for (auto &paintState : context.terrainPaintStates) {
        endTerrainPaintStroke(paintState);
      }
      return;
    }

    if (!leftMouseDown) {
      context.activeTerrainFlattenStroke.reset();
      for (auto &paintState : context.terrainPaintStates) {
        endTerrainPaintStroke(paintState);
      }
      return;
    }

    if (!hit.has_value()) {
      for (auto &paintState : context.terrainPaintStates) {
        endTerrainPaintStroke(paintState);
      }
      return;
    }

    SceneAssetInstance &terrainAsset = context.sceneAssets[hit->terrainIndex];
    const float sculptSpeedPerSecond = 2.4f;
    const float colorBlendPerSecond = 5.0f;
    const float brushStep =
        sculptSpeedPerSecond * std::max(deltaSeconds, 1.0f / 240.0f);
    bool changed = false;
    if (terrainAsset.terrainBrushTexturePaintMode) {
      context.activeTerrainFlattenStroke.reset();
      changed = applyTerrainPaintStamp(
          context, hit->terrainIndex, {hit->localPosition.x, hit->localPosition.z});
    } else if (terrainAsset.terrainBrushColorPaintMode) {
      context.activeTerrainFlattenStroke.reset();
      endTerrainPaintStroke(ensureTerrainPaintState(context, hit->terrainIndex));
      const float colorBlend =
          colorBlendPerSecond * std::max(deltaSeconds, 1.0f / 240.0f);
      changed = TerrainEditing::applyColorBrush(
          terrainAsset.terrainConfig, {hit->localPosition.x, hit->localPosition.z},
          terrainAsset.terrainBrushRadius, terrainAsset.terrainBrushColor,
          colorBlend);
      changed |= eraseTerrainPaintCanvas(
          context, hit->terrainIndex, {hit->localPosition.x, hit->localPosition.z},
          colorBlend);
    } else if (terrainAsset.terrainBrushFlattenMode) {
      endTerrainPaintStroke(ensureTerrainPaintState(context, hit->terrainIndex));
      if (!context.activeTerrainFlattenStroke.has_value() ||
          context.activeTerrainFlattenStroke->terrainIndex != hit->terrainIndex) {
        context.activeTerrainFlattenStroke = DefaultEngineTerrainFlattenStroke{
            .terrainIndex = hit->terrainIndex,
            .targetHeight = hit->localPosition.y,
        };
      }
      changed = TerrainEditing::applyFlattenBrush(
          terrainAsset.terrainConfig, {hit->localPosition.x, hit->localPosition.z},
          terrainAsset.terrainBrushRadius,
          context.activeTerrainFlattenStroke->targetHeight, brushStep);
    } else {
      context.activeTerrainFlattenStroke.reset();
      endTerrainPaintStroke(ensureTerrainPaintState(context, hit->terrainIndex));
      const float heightStep =
          (terrainAsset.terrainBrushLowerMode ? -1.0f : 1.0f) * brushStep;
      changed = TerrainEditing::applyHeightBrush(
          terrainAsset.terrainConfig, {hit->localPosition.x, hit->localPosition.z},
          terrainAsset.terrainBrushRadius, heightStep);
    }

    if (!changed) {
      return;
    }

    if (!terrainAsset.terrainBrushTexturePaintMode) {
      reloadTerrainAsset(hit->terrainIndex);
    }
  }

  static void updateTerrainEditOverlay(DefaultEngineTerrainRuntimeContext &context,
                                       const glm::mat4 &view,
                                       const glm::mat4 &proj,
                                       float deltaSeconds) {
    if (context.debugOverlayPass == nullptr) {
      return;
    }

    const auto hit = raycastTerrainFromCursor(context, view, proj);
    if (!hit.has_value()) {
      context.debugOverlayPass->setToolVisible(false);
      context.debugOverlayPass->setToolMarkers({});
      return;
    }

    SceneAssetInstance &terrainAsset = context.sceneAssets[hit->terrainIndex];
    ImGuiIO &io = ImGui::GetIO();
    bool radiusChanged = false;
    bool brushModeChanged = false;
    if (!io.WantCaptureKeyboard) {
      const float radiusStep = std::max(deltaSeconds * 4.0f, 0.02f);
      if (glfwGetKey(context.window.handle(), GLFW_KEY_UP) == GLFW_PRESS) {
        terrainAsset.terrainBrushRadius =
            std::min(terrainAsset.terrainBrushRadius + radiusStep, 128.0f);
        radiusChanged = true;
      }
      if (glfwGetKey(context.window.handle(), GLFW_KEY_DOWN) == GLFW_PRESS) {
        terrainAsset.terrainBrushRadius =
            std::max(terrainAsset.terrainBrushRadius - radiusStep, 0.05f);
        radiusChanged = true;
      }
      if (glfwGetKey(context.window.handle(), GLFW_KEY_LEFT) == GLFW_PRESS &&
          !terrainAsset.terrainBrushLowerMode) {
        terrainAsset.terrainBrushLowerMode = true;
        brushModeChanged = true;
      }
      if (glfwGetKey(context.window.handle(), GLFW_KEY_RIGHT) == GLFW_PRESS &&
          terrainAsset.terrainBrushLowerMode) {
        terrainAsset.terrainBrushLowerMode = false;
        brushModeChanged = true;
      }
    }

    if (radiusChanged || brushModeChanged) {
      context.sceneDefinition.assets = context.sceneAssets;
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
    context.debugOverlayPass->setToolMarkerMesh(context.terrainBrushIndicatorMesh);
    context.debugOverlayPass->setToolMarkers({DebugOverlayInstance{
        .model = basisTransform(
            hit->worldPosition + hit->worldNormal * 0.01f, hit->worldNormal,
            {terrainAsset.terrainBrushRadius, lineHeight,
             terrainAsset.terrainBrushRadius}),
        .color = brushColor,
    }});
    context.debugOverlayPass->setToolVisible(true);
  }
};
