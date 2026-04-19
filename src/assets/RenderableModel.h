#pragma once

#include "animation/ModelAnimationState.h"
#include "assets/GltfModelAsset.h"
#include "assets/ModelAsset.h"
#include "assets/ObjModelAsset.h"
#include "assets/TerrainModelAsset.h"
#include "core/RenderPass.h"
#include "resources/FrameGeometryUniforms.h"
#include "resources/Mesh.h"
#include "resources/ModelMaterialSet.h"
#include "resources/Sampler.h"
#include "resources/SkinPaletteBindings.h"
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_inverse.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

class RenderableModel {
public:
  using MaterialOverrideFn =
      std::function<void(std::vector<ImportedMaterialData> &)>;

  void loadFromObj(const std::string &path, CommandContext &commandContext,
                   DeviceContext &deviceContext,
                   const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                   const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                   FrameGeometryUniforms &frameGeometryUniforms,
                   Sampler &sampler, uint32_t framesInFlight,
                   MaterialOverrideFn materialOverride = nullptr) {
    loadAsset<ObjModelAsset>(path, commandContext, deviceContext,
                             descriptorSetLayout, secondaryDescriptorSetLayout,
                             frameGeometryUniforms, sampler, framesInFlight,
                             std::move(materialOverride));
  }

  void loadFromGltf(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                    FrameGeometryUniforms &frameGeometryUniforms,
                    Sampler &sampler, uint32_t framesInFlight,
                    MaterialOverrideFn materialOverride = nullptr) {
    loadAsset<GltfModelAsset>(path, commandContext, deviceContext,
                              descriptorSetLayout, secondaryDescriptorSetLayout,
                              frameGeometryUniforms, sampler, framesInFlight,
                              std::move(materialOverride));
  }

  void loadFromFile(const std::string &path, CommandContext &commandContext,
                    DeviceContext &deviceContext,
                    const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                    const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                    FrameGeometryUniforms &frameGeometryUniforms,
                    Sampler &sampler, uint32_t framesInFlight,
                    MaterialOverrideFn materialOverride = nullptr) {
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".obj") {
      loadFromObj(path, commandContext, deviceContext, descriptorSetLayout,
                  secondaryDescriptorSetLayout, frameGeometryUniforms, sampler,
                  framesInFlight, std::move(materialOverride));
      return;
    }

    if (extension == ".gltf" || extension == ".glb") {
      loadFromGltf(path, commandContext, deviceContext, descriptorSetLayout,
                   secondaryDescriptorSetLayout, frameGeometryUniforms, sampler,
                   framesInFlight, std::move(materialOverride));
      return;
    }

    throw std::runtime_error("unsupported model format: " + extension);
  }

  void loadFromAsset(std::unique_ptr<ModelAsset> loadedAsset,
                     CommandContext &commandContext,
                     DeviceContext &deviceContext,
                     const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                     const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                     FrameGeometryUniforms &frameGeometryUniforms,
                     Sampler &sampler, uint32_t framesInFlight,
                     MaterialOverrideFn materialOverride = nullptr) {
    if (loadedAsset == nullptr) {
      throw std::runtime_error("cannot load a null model asset");
    }
    if (materialOverride) {
      materialOverride(loadedAsset->mutableMaterials());
    }

    initializeLoadedAsset(std::move(loadedAsset), commandContext,
                          deviceContext, descriptorSetLayout,
                          secondaryDescriptorSetLayout, frameGeometryUniforms,
                          sampler, framesInFlight);
  }

  void loadTerrain(const TerrainConfig &config, const std::string &sourceLabel,
                   CommandContext &commandContext,
                   DeviceContext &deviceContext,
                   const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                   const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                   FrameGeometryUniforms &frameGeometryUniforms,
                   Sampler &sampler, uint32_t framesInFlight,
                   MaterialOverrideFn materialOverride = nullptr) {
    auto terrainAsset = std::make_unique<TerrainModelAsset>();
    terrainAsset->setTerrain(config, sourceLabel);
    loadFromAsset(std::move(terrainAsset), commandContext, deviceContext,
                  descriptorSetLayout, secondaryDescriptorSetLayout,
                  frameGeometryUniforms, sampler, framesInFlight,
                  std::move(materialOverride));
  }

  std::vector<RenderItem>
  buildRenderItems(DeviceContext &deviceContext, const RenderPass *targetPass,
                   const std::vector<glm::mat4> &itemModelMatrices = {},
                   int selectedBoneNodeIndex = -1,
                   size_t instanceSlotOffset = 0,
                   bool instanceDataChanged = true) {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    if (framesInFlightValue == 0) {
      throw std::runtime_error("RenderableModel instance buffers are not initialized");
    }

    std::vector<RenderItem> items;
    std::vector<glm::mat4> defaultInstanceMatrices;
    const std::vector<glm::mat4> *instanceMatrices = &itemModelMatrices;
    if (itemModelMatrices.empty()) {
      defaultInstanceMatrices.push_back(glm::mat4(1.0f));
      instanceMatrices = &defaultInstanceMatrices;
    }
    const auto &submeshes = asset->submeshes();
    items.reserve(submeshes.empty() ? 1 : submeshes.size());

    if (submeshes.empty()) {
      auto &cachedInstanceBuffer =
          instanceBufferFor(targetPass, instanceSlotOffset);
      if (instanceDataChanged || !cachedInstanceBuffer.hasUploadedSource) {
        uploadInstanceDataIfChanged(deviceContext, cachedInstanceBuffer,
                                    framesInFlightValue, *instanceMatrices);
      }
      items.push_back(RenderItem{
          .mesh = &geometryMesh,
          .descriptorBindings = &materialSet.bindingsForMaterialIndex(-1),
          .secondaryDescriptorBindings = nullptr,
          .targetPass = targetPass,
          .instanceBuffer = cachedInstanceBuffer.buffer,
          .instanceCount = static_cast<uint32_t>(instanceMatrices->size()),
          .boneWeightJointIndex = -1,
          .boneWeightDebugEnabled = 0,
          .skinningEnabled = 0,
      });
      return items;
    }

    const SkeletonAssetData *runtimeSkeleton = skeletonAsset();
    for (size_t index = 0; index < submeshes.size(); ++index) {
      const auto &submesh = submeshes[index];
      int selectedJointIndex = -1;
      if (selectedBoneNodeIndex >= 0 && runtimeSkeleton != nullptr &&
          submesh.skinIndex >= 0 &&
          static_cast<size_t>(submesh.skinIndex) < runtimeSkeleton->skins.size()) {
        const auto &jointNodeIndices =
            runtimeSkeleton->skins[static_cast<size_t>(submesh.skinIndex)]
                .jointNodeIndices;
        const auto selectedJointIt =
            std::find(jointNodeIndices.begin(), jointNodeIndices.end(),
                      selectedBoneNodeIndex);
        if (selectedJointIt != jointNodeIndices.end()) {
          selectedJointIndex = static_cast<int>(
              std::distance(jointNodeIndices.begin(), selectedJointIt));
        }
      }

      auto &cachedInstanceBuffer =
          instanceBufferFor(targetPass, instanceSlotOffset + index + 1);
      if (instanceDataChanged || !cachedInstanceBuffer.hasUploadedSource) {
        uploadInstanceDataIfChanged(deviceContext, cachedInstanceBuffer,
                                    framesInFlightValue, *instanceMatrices,
                                    submesh.transform);
      }
      items.push_back(RenderItem{
          .mesh = &geometryMesh,
          .descriptorBindings =
              &materialSet.bindingsForMaterialIndex(submesh.materialIndex),
          .secondaryDescriptorBindings =
              index < skinPaletteBindings.size() && skinPaletteBindings[index].valid
                  ? &skinPaletteBindings[index].bindings
                  : nullptr,
          .targetPass = targetPass,
          .indexOffset = submesh.indexOffset,
          .indexCount = submesh.indexCount,
          .instanceBuffer = cachedInstanceBuffer.buffer,
          .instanceCount = static_cast<uint32_t>(instanceMatrices->size()),
          .boneWeightJointIndex = selectedJointIndex,
          .boneWeightDebugEnabled = selectedBoneNodeIndex >= 0 ? 1 : 0,
          .skinningEnabled =
              index < skinPaletteBindings.size() && skinPaletteBindings[index].valid
                  ? 1
                  : 0,
      });
    }

    return items;
  }

  std::vector<ImportedMaterialData> &mutableMaterials() {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    return asset->mutableMaterials();
  }

  const std::vector<ImportedMaterialData> &materials() const {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    return asset->materials();
  }

  void syncMaterialParameters() {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    materialSet.updateMaterialParameters(convertMaterials(asset->materials()));
  }

  void syncMaterialResources(CommandContext &commandContext,
                             DeviceContext &deviceContext) {
    if (asset == nullptr) {
      throw std::runtime_error("RenderableModel has no loaded asset");
    }
    if (descriptorSetLayoutRef == nullptr || frameGeometryUniformsRef == nullptr ||
        samplerRef == nullptr || framesInFlightValue == 0) {
      throw std::runtime_error("RenderableModel material context is not initialized");
    }

    materialSet.create(deviceContext, commandContext, *descriptorSetLayoutRef,
                       *frameGeometryUniformsRef, *samplerRef,
                       convertMaterials(asset->materials()),
                       framesInFlightValue);
  }

  bool canUpdatePaintCanvasTexture(size_t materialIndex, int width,
                                   int height) const {
    return materialSet.canUpdatePaintCanvasTexture(materialIndex, width, height);
  }

  bool recordPaintCanvasTextureUpdate(
      size_t materialIndex, DeviceContext &deviceContext,
      vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex,
      const uint8_t *rgbaPixels, int sourceWidth, int sourceHeight, int x, int y,
      int width, int height) {
    return materialSet.recordPaintCanvasTextureUpdate(
        materialIndex, deviceContext, commandBuffer, frameIndex, rgbaPixels,
        sourceWidth, sourceHeight, x, y, width, height);
  }

  bool recordTerrainGeometryUpdate(const TerrainConfig &config,
                                   DeviceContext &deviceContext,
                                   vk::raii::CommandBuffer &commandBuffer,
                                   uint32_t frameIndex) {
    if (asset == nullptr ||
        dynamic_cast<TerrainModelAsset *>(asset.get()) == nullptr) {
      return false;
    }

    const TerrainMeshData terrainMesh = TerrainGenerator::buildMesh(config);
    if (terrainMesh.vertices.size() != geometryMesh.vertexCount() ||
        terrainMesh.indices != geometryMesh.getIndices()) {
      return false;
    }

    std::vector<GeometryVertex> vertices;
    vertices.reserve(terrainMesh.vertices.size());
    for (const auto &vertex : terrainMesh.vertices) {
      vertices.push_back(GeometryVertex{
          .pos = vertex.position,
          .normal = vertex.normal,
          .texCoord = vertex.uv,
          .color = vertex.color,
      });
    }

    geometryMesh.setImportedVertices(std::move(vertices));
    return geometryMesh.recordVertexBufferUpdate(deviceContext, commandBuffer,
                                                frameIndex);
  }

  const ModelAsset *modelAsset() const { return asset.get(); }

  const SkeletonAssetData *skeletonAsset() const {
    return runtimeSkeleton.has_value() ? &*runtimeSkeleton : nullptr;
  }

  AnimationPlaybackState *mutableAnimationPlayback() {
    return animationState.mutablePlayback(skeletonAsset());
  }

  const AnimationPlaybackState *currentAnimationPlayback() const {
    return animationState.currentPlayback(skeletonAsset());
  }

  SkeletonPose *mutableSkeletonPose() {
    return animationState.mutablePose(skeletonAsset());
  }

  const SkeletonPose *currentSkeletonPose() const {
    return animationState.currentPose(skeletonAsset());
  }

  void resetSkeletonPose() { animationState.resetPose(skeletonAsset()); }

  bool hasSelectedAnimation() const {
    return animationState.hasSelectedAnimation(skeletonAsset());
  }

  const AnimationClipData *selectedAnimationClip() const {
    return animationState.selectedClip(skeletonAsset());
  }

  void selectSourceAnimation(int animationIndex) {
    animationState.selectSourceAnimation(skeletonAsset(), animationIndex);
  }

  void playSelectedAnimation() {
    animationState.playSelectedAnimation(skeletonAsset());
  }

  void pauseAnimationPlayback() { animationState.pauseAnimationPlayback(); }

  void resetSelectedAnimation() {
    animationState.resetSelectedAnimation(skeletonAsset());
  }

  void sampleSelectedAnimation() {
    animationState.sampleSelectedAnimation(skeletonAsset());
  }

  void updateAnimationPlayback(float deltaSeconds) {
    animationState.updatePlayback(skeletonAsset(), deltaSeconds);
  }

  void updateSkinPalettes(uint32_t frameIndex) {
    const SkeletonAssetData *runtimeSkeletonAsset = skeletonAsset();
    const SkeletonPose *pose = animationState.currentPose(runtimeSkeletonAsset);
    if (asset == nullptr || runtimeSkeletonAsset == nullptr || pose == nullptr) {
      return;
    }

    const auto &submeshes = asset->submeshes();
    for (size_t submeshIndex = 0; submeshIndex < submeshes.size();
         ++submeshIndex) {
      if (submeshIndex >= skinPaletteBindings.size() ||
          !skinPaletteBindings[submeshIndex].valid) {
        continue;
      }

      const auto &submesh = submeshes[submeshIndex];
      if (submesh.skinIndex < 0 ||
          static_cast<size_t>(submesh.skinIndex) >=
              runtimeSkeletonAsset->skins.size()) {
        continue;
      }

      const SkinData &skin =
          runtimeSkeletonAsset->skins[static_cast<size_t>(submesh.skinIndex)];
      if (skin.jointNodeIndices.size() > MAX_SKIN_JOINTS) {
        throw std::runtime_error("skin joint count exceeds MAX_SKIN_JOINTS");
      }

      SkinPaletteUniformData palette{};
      for (auto &jointMatrix : palette.joints) {
        jointMatrix = glm::mat4(1.0f);
      }

      glm::mat4 meshNodeWorld = submesh.transform;
      if (submesh.nodeIndex >= 0 &&
          static_cast<size_t>(submesh.nodeIndex) < runtimeSkeletonAsset->nodes.size()) {
        meshNodeWorld =
            pose->worldTransform(static_cast<size_t>(submesh.nodeIndex));
      }
      const glm::mat4 inverseMeshNodeWorld = glm::inverse(meshNodeWorld);

      for (size_t jointIndex = 0; jointIndex < skin.jointNodeIndices.size();
           ++jointIndex) {
        const int jointNodeIndex = skin.jointNodeIndices[jointIndex];
        if (jointNodeIndex < 0 ||
            static_cast<size_t>(jointNodeIndex) >= runtimeSkeletonAsset->nodes.size()) {
          continue;
        }

        const glm::mat4 jointWorld =
            pose->worldTransform(static_cast<size_t>(jointNodeIndex));
        palette.joints[jointIndex] =
            inverseMeshNodeWorld * jointWorld *
            skin.inverseBindMatrices[jointIndex];
      }

      skinPaletteBindings[submeshIndex].bindings.write(frameIndex, palette);
    }
  }

private:
  struct SubmeshSkinPaletteResource {
    SkinPaletteBindings bindings;
    bool valid = false;
  };

  struct CachedInstanceBuffer {
    const RenderPass *targetPass = nullptr;
    size_t slot = 0;
    std::shared_ptr<FrameInstanceBuffer> buffer;
    uint64_t sourceHash = 0;
    size_t sourceCount = 0;
    bool hasUploadedSource = false;
  };

  static uint64_t hashBytes(const void *data, size_t byteCount,
                            uint64_t seed = 1469598103934665603ull) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    uint64_t hash = seed;
    for (size_t index = 0; index < byteCount; ++index) {
      hash ^= static_cast<uint64_t>(bytes[index]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  static uint64_t hashInstanceSource(const std::vector<glm::mat4> &instanceMatrices,
                                     const glm::mat4 &localTransform) {
    uint64_t hash = hashBytes(&localTransform, sizeof(glm::mat4));
    for (const auto &instanceMatrix : instanceMatrices) {
      hash = hashBytes(&instanceMatrix, sizeof(glm::mat4), hash);
    }
    return hash;
  }

  static std::vector<RenderInstanceData>
  buildInstanceData(const std::vector<glm::mat4> &instanceMatrices,
                    const glm::mat4 &localTransform = glm::mat4(1.0f)) {
    std::vector<RenderInstanceData> instances;
    instances.reserve(instanceMatrices.size());
    for (const auto &instanceMatrix : instanceMatrices) {
      const glm::mat4 modelMatrix = instanceMatrix * localTransform;
      const glm::mat4 modelNormalMatrix = glm::inverseTranspose(modelMatrix);
      instances.push_back(RenderInstanceData{
          .model0 = modelMatrix[0],
          .model1 = modelMatrix[1],
          .model2 = modelMatrix[2],
          .model3 = modelMatrix[3],
          .modelNormal0 = modelNormalMatrix[0],
          .modelNormal1 = modelNormalMatrix[1],
          .modelNormal2 = modelNormalMatrix[2],
          .modelNormal3 = modelNormalMatrix[3],
      });
    }
    return instances;
  }

  CachedInstanceBuffer &
  instanceBufferFor(const RenderPass *targetPass, size_t slot) {
    for (auto &entry : instanceBuffers) {
      if (entry.targetPass == targetPass && entry.slot == slot) {
        return entry;
      }
    }

    auto buffer = std::make_shared<FrameInstanceBuffer>();
    instanceBuffers.push_back(CachedInstanceBuffer{
        .targetPass = targetPass,
        .slot = slot,
        .buffer = buffer,
    });
    return instanceBuffers.back();
  }

  void uploadInstanceDataIfChanged(DeviceContext &deviceContext,
                                   CachedInstanceBuffer &cachedBuffer,
                                   uint32_t framesInFlight,
                                   const std::vector<glm::mat4> &instanceMatrices,
                                   const glm::mat4 &localTransform =
                                       glm::mat4(1.0f)) {
    const uint64_t sourceHash =
        hashInstanceSource(instanceMatrices, localTransform);
    if (cachedBuffer.hasUploadedSource &&
        cachedBuffer.sourceHash == sourceHash &&
        cachedBuffer.sourceCount == instanceMatrices.size()) {
      return;
    }

    cachedBuffer.buffer->write(deviceContext, framesInFlight,
                               buildInstanceData(instanceMatrices,
                                                 localTransform));
    cachedBuffer.sourceHash = sourceHash;
    cachedBuffer.sourceCount = instanceMatrices.size();
    cachedBuffer.hasUploadedSource = true;
  }

  static GeometryVertex convertVertex(const ImportedGeometryVertex &vertex) {
    return GeometryVertex{
        .pos = vertex.pos,
        .normal = vertex.normal,
        .texCoord = vertex.texCoord,
        .color = vertex.color,
        .tangent = vertex.tangent,
        .jointIndices = vertex.jointIndices,
        .jointWeights = vertex.jointWeights,
    };
  }

  static ModelMaterialData::TextureSource
  convertTextureSource(const ImportedTextureSource &source) {
    return ModelMaterialData::TextureSource{
        .resolvedPath = source.resolvedPath,
        .rgba = source.rgba,
        .width = source.width,
        .height = source.height,
    };
  }

  static ModelMaterialData convertMaterial(const ImportedMaterialData &material) {
    return ModelMaterialData{
        .name = material.name,
        .baseColorFactor = material.baseColorFactor,
        .baseColorTexture = convertTextureSource(material.baseColorTexture),
        .metallicRoughnessTexture =
            convertTextureSource(material.metallicRoughnessTexture),
        .emissiveTexture = convertTextureSource(material.emissiveTexture),
        .occlusionTexture = convertTextureSource(material.occlusionTexture),
        .paintCanvasTexture = convertTextureSource(material.paintCanvasTexture),
        .metallicFactor = material.metallicFactor,
        .roughnessFactor = material.roughnessFactor,
        .emissiveFactor = material.emissiveFactor,
        .occlusionStrength = material.occlusionStrength,
        .paintCanvasUvScale = material.paintCanvasUvScale,
        .raw = material.raw,
        .hasObjMaterial = material.hasObjMaterial,
    };
  }

  static ModelSubmesh convertSubmesh(const ImportedModelSubmesh &submesh) {
    return ModelSubmesh{
        .name = submesh.name,
        .indexOffset = submesh.indexOffset,
        .indexCount = submesh.indexCount,
        .materialIndex = submesh.materialIndex,
        .shapeIndex = submesh.shapeIndex,
        .transform = submesh.transform,
        .nodeIndex = submesh.nodeIndex,
        .skinIndex = submesh.skinIndex,
    };
  }

  static NodeTransform convertNodeTransform(const ImportedNodeTransform &transform) {
    return NodeTransform{
        .translation = transform.translation,
        .rotation = transform.rotation,
        .scale = transform.scale,
    };
  }

  static AnimationTargetPath convertTargetPath(ImportedAnimationTargetPath path) {
    switch (path) {
    case ImportedAnimationTargetPath::Translation:
      return AnimationTargetPath::Translation;
    case ImportedAnimationTargetPath::Rotation:
      return AnimationTargetPath::Rotation;
    case ImportedAnimationTargetPath::Scale:
      return AnimationTargetPath::Scale;
    }
    throw std::runtime_error("unknown imported animation target path");
  }

  static AnimationInterpolation
  convertInterpolation(ImportedAnimationInterpolation interpolation) {
    switch (interpolation) {
    case ImportedAnimationInterpolation::Linear:
      return AnimationInterpolation::Linear;
    case ImportedAnimationInterpolation::Step:
      return AnimationInterpolation::Step;
    case ImportedAnimationInterpolation::CubicSpline:
      return AnimationInterpolation::CubicSpline;
    }
    throw std::runtime_error("unknown imported animation interpolation");
  }

  static SkeletonAssetData
  convertSkeleton(const ImportedSkeletonData &skeleton) {
    SkeletonAssetData runtimeSkeleton;
    runtimeSkeleton.sceneRootNodeIndices = skeleton.sceneRootNodeIndices;
    runtimeSkeleton.nodes.reserve(skeleton.nodes.size());
    runtimeSkeleton.skins.reserve(skeleton.skins.size());
    runtimeSkeleton.animations.reserve(skeleton.animations.size());

    for (const auto &node : skeleton.nodes) {
      runtimeSkeleton.nodes.push_back(SkeletonNode{
          .name = node.name,
          .parentIndex = node.parentIndex,
          .childIndices = node.childIndices,
          .localBindTransform = convertNodeTransform(node.localBindTransform),
      });
    }

    for (const auto &skin : skeleton.skins) {
      runtimeSkeleton.skins.push_back(SkinData{
          .name = skin.name,
          .skeletonRootNodeIndex = skin.skeletonRootNodeIndex,
          .jointNodeIndices = skin.jointNodeIndices,
          .inverseBindMatrices = skin.inverseBindMatrices,
      });
    }

    for (const auto &clip : skeleton.animations) {
      AnimationClipData runtimeClip;
      runtimeClip.name = clip.name;
      runtimeClip.durationSeconds = clip.durationSeconds;
      runtimeClip.tracks.reserve(clip.tracks.size());

      for (const auto &track : clip.tracks) {
        runtimeClip.tracks.push_back(NodeAnimationTrack{
            .targetNodeIndex = track.targetNodeIndex,
            .targetPath = convertTargetPath(track.targetPath),
            .interpolation = convertInterpolation(track.interpolation),
            .timesSeconds = track.timesSeconds,
            .vec3InTangents = track.vec3InTangents,
            .vec3Values = track.vec3Values,
            .vec3OutTangents = track.vec3OutTangents,
            .quatInTangents = track.quatInTangents,
            .quatValues = track.quatValues,
            .quatOutTangents = track.quatOutTangents,
        });
      }

      runtimeSkeleton.animations.push_back(std::move(runtimeClip));
    }

    return runtimeSkeleton;
  }

  static std::vector<ModelMaterialData>
  convertMaterials(const std::vector<ImportedMaterialData> &materials) {
    std::vector<ModelMaterialData> converted;
    converted.reserve(materials.size());
    for (const auto &material : materials) {
      converted.push_back(convertMaterial(material));
    }
    return converted;
  }

  static std::vector<ModelSubmesh>
  convertSubmeshes(const std::vector<ImportedModelSubmesh> &submeshes) {
    std::vector<ModelSubmesh> converted;
    converted.reserve(submeshes.size());
    for (const auto &submesh : submeshes) {
      converted.push_back(convertSubmesh(submesh));
    }
    return converted;
  }

  static std::vector<GeometryVertex>
  convertVertices(const std::vector<ImportedGeometryVertex> &vertices) {
    std::vector<GeometryVertex> converted;
    converted.reserve(vertices.size());
    for (const auto &vertex : vertices) {
      converted.push_back(convertVertex(vertex));
    }
    return converted;
  }

  template <typename TAsset>
  void loadAsset(const std::string &path, CommandContext &commandContext,
                 DeviceContext &deviceContext,
                 const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                 const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
                 FrameGeometryUniforms &frameGeometryUniforms, Sampler &sampler,
                 uint32_t framesInFlight,
                 const MaterialOverrideFn &materialOverride = nullptr) {
    auto loadedAsset = std::make_unique<TAsset>();
    loadedAsset->load(path);
    if (materialOverride) {
      materialOverride(loadedAsset->mutableMaterials());
    }

    initializeLoadedAsset(std::move(loadedAsset), commandContext,
                          deviceContext, descriptorSetLayout,
                          secondaryDescriptorSetLayout, frameGeometryUniforms,
                          sampler, framesInFlight);
  }

  void initializeLoadedAsset(
      std::unique_ptr<ModelAsset> loadedAsset, CommandContext &commandContext,
      DeviceContext &deviceContext,
      const vk::raii::DescriptorSetLayout &descriptorSetLayout,
      const vk::raii::DescriptorSetLayout *secondaryDescriptorSetLayout,
      FrameGeometryUniforms &frameGeometryUniforms, Sampler &sampler,
      uint32_t framesInFlight) {
    if (loadedAsset == nullptr) {
      throw std::runtime_error("cannot initialize a null model asset");
    }

    geometryMesh.setImportedGeometry(
        convertVertices(loadedAsset->mesh().vertexData()),
        loadedAsset->mesh().indexData(),
        convertSubmeshes(loadedAsset->submeshes()),
        convertMaterials(loadedAsset->materials()));
    geometryMesh.createVertexBuffer(commandContext, deviceContext);
    geometryMesh.createIndexBuffer(commandContext, deviceContext);

    materialSet.create(deviceContext, commandContext, descriptorSetLayout,
                       frameGeometryUniforms, sampler,
                       convertMaterials(loadedAsset->materials()),
                       framesInFlight);
    descriptorSetLayoutRef = &descriptorSetLayout;
    frameGeometryUniformsRef = &frameGeometryUniforms;
    samplerRef = &sampler;
    framesInFlightValue = framesInFlight;
    instanceBuffers.clear();

    if (const ImportedSkeletonData *loadedSkeleton = loadedAsset->skeletonAsset();
        loadedSkeleton != nullptr && !loadedSkeleton->nodes.empty()) {
      runtimeSkeleton = convertSkeleton(*loadedSkeleton);
      animationState.reset(&*runtimeSkeleton);
      skinPaletteBindings.clear();
      skinPaletteBindings.resize(loadedAsset->submeshes().size());
      if (secondaryDescriptorSetLayout != nullptr) {
        for (size_t submeshIndex = 0;
             submeshIndex < loadedAsset->submeshes().size(); ++submeshIndex) {
          if (loadedAsset->submeshes()[submeshIndex].skinIndex < 0) {
            continue;
          }
          skinPaletteBindings[submeshIndex].bindings.create(
              deviceContext, *secondaryDescriptorSetLayout, framesInFlight);
          skinPaletteBindings[submeshIndex].valid = true;
        }
      }
    } else {
      runtimeSkeleton.reset();
      animationState.clear();
      skinPaletteBindings.clear();
    }

    asset = std::move(loadedAsset);
  }

  std::unique_ptr<ModelAsset> asset;
  ImportedGeometryMesh geometryMesh;
  ModelMaterialSet materialSet;
  const vk::raii::DescriptorSetLayout *descriptorSetLayoutRef = nullptr;
  FrameGeometryUniforms *frameGeometryUniformsRef = nullptr;
  Sampler *samplerRef = nullptr;
  uint32_t framesInFlightValue = 0;
  std::optional<SkeletonAssetData> runtimeSkeleton;
  ModelAnimationState animationState;
  std::vector<SubmeshSkinPaletteResource> skinPaletteBindings;
  std::vector<CachedInstanceBuffer> instanceBuffers;
};
