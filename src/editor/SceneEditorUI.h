#pragma once

#include "DebugUIState.h"
#include "scene/AppSceneController.h"
#include "scene/SceneDefinition.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

struct SceneEditorUIResult {
  bool materialChanged = false;
  bool sceneAssetChanged = false;
  bool sceneGeometryChanged = false;
};

class SceneEditorUI {
public:
  explicit SceneEditorUI(DefaultDebugUIBindings bindings)
      : bindings(std::move(bindings)) {}

  SceneEditorUIResult build() {
    SceneEditorUIResult result = buildHierarchyPanel();
    if (result.sceneGeometryChanged) {
      return result;
    }

    const SceneEditorUIResult inspectorResult = buildInspectorPanel();
    result.materialChanged |= inspectorResult.materialChanged;
    result.sceneAssetChanged |= inspectorResult.sceneAssetChanged;
    result.sceneGeometryChanged |= inspectorResult.sceneGeometryChanged;
    return result;
  }

private:
  DefaultDebugUIBindings bindings;

  static bool isSupportedAssetPath(const std::filesystem::path &path) {
    const std::string extension = path.extension().string();
    return extension == ".obj" || extension == ".gltf" || extension == ".glb";
  }

  static bool isSupportedTerrainBrushTexturePath(
      const std::filesystem::path &path) {
    const std::string extension = path.extension().string();
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
  }

  static std::vector<std::filesystem::path> collectModelAssetPaths() {
    std::vector<std::filesystem::path> assetPaths;
    std::error_code errorCode;
    const std::filesystem::path assetRoot("assets/models");
    if (std::filesystem::exists(assetRoot, errorCode) &&
        std::filesystem::is_directory(assetRoot, errorCode)) {
      for (std::filesystem::recursive_directory_iterator iterator(assetRoot,
                                                                  errorCode),
           end;
           iterator != end && !errorCode; iterator.increment(errorCode)) {
        if (!iterator->is_regular_file(errorCode) ||
            !isSupportedAssetPath(iterator->path())) {
          continue;
        }
        assetPaths.push_back(iterator->path().lexically_normal());
      }
    }

    std::sort(assetPaths.begin(), assetPaths.end());
    return assetPaths;
  }

  static std::vector<std::filesystem::path> collectTerrainBrushTexturePaths() {
    std::vector<std::filesystem::path> texturePaths;
    std::error_code errorCode;
    const std::filesystem::path textureRoot("assets/textures");
    if (std::filesystem::exists(textureRoot, errorCode) &&
        std::filesystem::is_directory(textureRoot, errorCode)) {
      for (std::filesystem::recursive_directory_iterator iterator(textureRoot,
                                                                  errorCode),
           end;
           iterator != end && !errorCode; iterator.increment(errorCode)) {
        if (!iterator->is_regular_file(errorCode) ||
            !isSupportedTerrainBrushTexturePath(iterator->path())) {
          continue;
        }
        texturePaths.push_back(iterator->path().lexically_normal());
      }
    }

    std::sort(texturePaths.begin(), texturePaths.end());
    return texturePaths;
  }

  static TerrainConfig defaultTerrainConfig() {
    return TerrainConfig{
        .sizeX = 18.0f,
        .sizeZ = 18.0f,
        .xSegments = 32,
        .zSegments = 32,
        .uvScale = {6.0f, 6.0f},
        .heightScale = 0.0f,
        .noiseFrequency = 0.2f,
        .noiseOctaves = 4,
        .noisePersistence = 0.5f,
        .noiseLacunarity = 2.0f,
        .noiseSeed = 7,
    };
  }

  static SceneAssetInstance defaultCharacterControllerAsset() {
    SceneAssetInstance sceneAsset = SceneAssetInstance::makeCharacterController();
    sceneAsset.transform.position = {0.0f, 1.25f, 0.0f};
    sceneAsset.characterControllerState.position = sceneAsset.transform.position;
    return sceneAsset;
  }

  static SceneAssetInstance
  defaultCameraAsset(const DefaultDebugUISettings &settings) {
    return SceneAssetInstance::makeCamera(
        DefaultDebugCameraController::sceneTransformFromSettings(settings),
        SceneCameraConfig{.fieldOfViewDegrees = 45.0f,
                          .farPlane = settings.cameraFarPlane},
        "Camera");
  }

  SceneAssetInstance defaultInstancedObjectAsset() const {
    SceneAssetInstance sceneAsset = SceneAssetInstance::makeInstancedObject();
    for (size_t index = 0; index < bindings.sceneAssets.size(); ++index) {
      if (bindings.sceneAssets[index].kind != SceneAssetKind::Terrain) {
        continue;
      }
      sceneAsset.targetTerrainName =
          AppSceneController::sceneAssetName(bindings.sceneAssets[index], index);
      break;
    }
    return sceneAsset;
  }

  void activateSceneCameraPreviewIfApplicable(int index) {
    if (index < 0 ||
        static_cast<size_t>(index) >= bindings.sceneAssets.size()) {
      return;
    }
    if (bindings.sceneAssets[static_cast<size_t>(index)].kind !=
        SceneAssetKind::Camera) {
      return;
    }
    DefaultDebugCameraController::activateSceneCameraPreview(bindings.settings,
                                                             index);
  }

  void addSceneAsset(SceneAssetInstance sceneAsset) {
    auto &settings = bindings.settings;
    bindings.sceneAssets.push_back(std::move(sceneAsset));

    const size_t assetIndex = bindings.sceneAssets.size() - 1;
    const SceneAssetInstance &addedSceneAsset = bindings.sceneAssets[assetIndex];
    settings.sceneObjects.push_back(SceneObject{
        .name = AppSceneController::sceneAssetName(addedSceneAsset, assetIndex),
        .transform = addedSceneAsset.transform,
        .visible = addedSceneAsset.visible,
    });
    settings.selectedObjectIndex = static_cast<int>(assetIndex);
    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = -1;
    settings.selectedMaterialIndex = 0;
    activateSceneCameraPreviewIfApplicable(static_cast<int>(assetIndex));
  }

  void removeSelectedSceneAsset() {
    auto &settings = bindings.settings;
    if (settings.selectedObjectIndex < 0 ||
        static_cast<size_t>(settings.selectedObjectIndex) >=
            bindings.sceneAssets.size() ||
        static_cast<size_t>(settings.selectedObjectIndex) >=
            settings.sceneObjects.size()) {
      return;
    }

    const size_t selectedIndex = static_cast<size_t>(settings.selectedObjectIndex);
    if (DefaultDebugCameraController::sceneCameraPreviewActive(settings)) {
      if (settings.viewportSceneCameraIndex ==
          static_cast<int>(selectedIndex)) {
        DefaultDebugCameraController::deactivateSceneCameraPreview(settings);
      } else if (settings.viewportSceneCameraIndex >
                 static_cast<int>(selectedIndex)) {
        --settings.viewportSceneCameraIndex;
      }
    }
    bindings.sceneAssets.erase(bindings.sceneAssets.begin() +
                               static_cast<long>(selectedIndex));
    settings.sceneObjects.erase(settings.sceneObjects.begin() +
                                static_cast<long>(selectedIndex));

    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = -1;
    settings.selectedAnimationObjectIndex = -1;
    settings.selectedAnimationIndex = -1;
    settings.selectedMaterialIndex = 0;

    if (settings.sceneObjects.empty()) {
      settings.selectedObjectIndex = 0;
      return;
    }

    settings.selectedObjectIndex =
        std::clamp(settings.selectedObjectIndex, 0,
                   static_cast<int>(settings.sceneObjects.size()) - 1);
  }

  void addLight(SceneLightType type) {
    auto &settings = bindings.settings;
    switch (type) {
    case SceneLightType::Directional:
      settings.sceneLights.addDirectional();
      break;
    case SceneLightType::Point:
      settings.sceneLights.addPoint();
      break;
    case SceneLightType::Spot:
      settings.sceneLights.addSpot();
      break;
    }

    settings.selectedLightIndex = static_cast<int>(settings.sceneLights.size()) - 1;
    settings.selectedBoneIndex = -1;
  }

  void buildAssetAddMenu(SceneEditorUIResult &result) {
    const std::vector<std::filesystem::path> assetPaths = collectModelAssetPaths();
    if (assetPaths.empty()) {
      ImGui::MenuItem("No model assets found", nullptr, false, false);
      return;
    }

    for (size_t index = 0; index < assetPaths.size(); ++index) {
      const std::string assetPath = assetPaths[index].generic_string();
      const std::string label =
          assetPaths[index].filename().string() + "##add_asset_" +
          std::to_string(index);
      if (!ImGui::MenuItem(label.c_str())) {
        continue;
      }

      addSceneAsset(SceneAssetInstance::fromAsset(assetPath));
      result.sceneAssetChanged = true;
      result.sceneGeometryChanged = true;
    }
  }

  void buildHierarchyMenuBar(SceneEditorUIResult &result) {
    if (!ImGui::BeginMenuBar()) {
      return;
    }

    if (ImGui::BeginMenu("Add")) {
      if (ImGui::BeginMenu("Asset")) {
        buildAssetAddMenu(result);
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Terrain")) {
        addSceneAsset(
            SceneAssetInstance::makeTerrain(defaultTerrainConfig(), "Terrain"));
        result.sceneAssetChanged = true;
        result.sceneGeometryChanged = true;
      }
      if (ImGui::MenuItem("Character Controller")) {
        addSceneAsset(defaultCharacterControllerAsset());
        result.sceneAssetChanged = true;
      }
      if (ImGui::MenuItem("Camera")) {
        addSceneAsset(defaultCameraAsset(bindings.settings));
        result.sceneAssetChanged = true;
      }
      if (ImGui::MenuItem("Instanced Object")) {
        addSceneAsset(defaultInstancedObjectAsset());
        result.sceneAssetChanged = true;
        result.sceneGeometryChanged = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Directional Light")) {
        addLight(SceneLightType::Directional);
      }
      if (ImGui::MenuItem("Point Light")) {
        addLight(SceneLightType::Point);
      }
      if (ImGui::MenuItem("Spot Light")) {
        addLight(SceneLightType::Spot);
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene")) {
      if (ImGui::MenuItem("Reset Lights")) {
        auto &settings = bindings.settings;
        settings.sceneLights = SceneLightSet::showcaseLights();
        settings.selectedLightIndex = settings.sceneLights.empty() ? -1 : 0;
        settings.selectedBoneIndex = -1;
      }
      ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
  }

  const SkeletonAssetData *currentSkeleton() const {
    return bindings.sceneModel.skeletonAsset();
  }

  std::unordered_set<int> currentDeformingBoneSet() const {
    std::unordered_set<int> deformingBones;
    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    const SkeletonAssetData *skeleton = currentSkeleton();
    if (asset == nullptr || skeleton == nullptr) {
      return deformingBones;
    }

    for (const auto &submesh : asset->submeshes()) {
      if (submesh.skinIndex < 0 ||
          static_cast<size_t>(submesh.skinIndex) >= skeleton->skins.size()) {
        continue;
      }

      const SkinData &skin =
          skeleton->skins[static_cast<size_t>(submesh.skinIndex)];
      for (const int jointNodeIndex : skin.jointNodeIndices) {
        deformingBones.insert(jointNodeIndex);
      }
    }

    return deformingBones;
  }

  static int nearestDeformingParentIndex(
      const SkeletonAssetData &skeleton,
      const std::unordered_set<int> &deformingBones, int nodeIndex) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= skeleton.nodes.size()) {
      return -1;
    }

    int parentIndex = skeleton.nodes[static_cast<size_t>(nodeIndex)].parentIndex;
    while (parentIndex >= 0) {
      if (deformingBones.contains(parentIndex)) {
        return parentIndex;
      }
      if (static_cast<size_t>(parentIndex) >= skeleton.nodes.size()) {
        return -1;
      }
      parentIndex = skeleton.nodes[static_cast<size_t>(parentIndex)].parentIndex;
    }
    return -1;
  }

  static std::vector<std::vector<int>>
  buildCompressedDeformingChildren(const SkeletonAssetData &skeleton,
                                   const std::unordered_set<int> &deformingBones) {
    std::vector<std::vector<int>> compressedChildren(skeleton.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < skeleton.nodes.size(); ++nodeIndex) {
      if (!deformingBones.contains(static_cast<int>(nodeIndex))) {
        continue;
      }

      const int parentIndex = nearestDeformingParentIndex(
          skeleton, deformingBones, static_cast<int>(nodeIndex));
      if (parentIndex >= 0) {
        compressedChildren[static_cast<size_t>(parentIndex)].push_back(
            static_cast<int>(nodeIndex));
      }
    }

    for (auto &children : compressedChildren) {
      std::sort(children.begin(), children.end());
      children.erase(std::unique(children.begin(), children.end()),
                     children.end());
    }

    return compressedChildren;
  }

  static void markCompressedBoneSubtreeVisited(
      std::vector<bool> &visited,
      const std::vector<std::vector<int>> &compressedChildren, int nodeIndex) {
    if (nodeIndex < 0 ||
        static_cast<size_t>(nodeIndex) >= compressedChildren.size() ||
        visited[static_cast<size_t>(nodeIndex)]) {
      return;
    }

    visited[static_cast<size_t>(nodeIndex)] = true;
    for (const int childIndex :
         compressedChildren[static_cast<size_t>(nodeIndex)]) {
      markCompressedBoneSubtreeVisited(visited, compressedChildren, childIndex);
    }
  }

  void clampBoneSelection(DefaultDebugUISettings &settings) const {
    const SkeletonAssetData *skeleton = currentSkeleton();
    if (skeleton == nullptr || skeleton->nodes.empty()) {
      settings.selectedBoneIndex = -1;
      return;
    }

    settings.selectedBoneIndex =
        std::clamp(settings.selectedBoneIndex, -1,
                   static_cast<int>(skeleton->nodes.size()) - 1);
  }

  void selectObject(int index) {
    auto &settings = bindings.settings;
    settings.selectedObjectIndex = index;
    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = -1;
    activateSceneCameraPreviewIfApplicable(index);

    if (bindings.sceneModel.modelAsset() == nullptr) {
      return;
    }
    if (!bindings.sceneModel.materials().empty()) {
      settings.selectedMaterialIndex = 0;
    }
  }

  void selectBone(int index) {
    auto &settings = bindings.settings;
    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = index;
  }

  static const char *lightTypeLabel(SceneLightType type) {
    switch (type) {
    case SceneLightType::Directional:
      return "Directional";
    case SceneLightType::Point:
      return "Point";
    case SceneLightType::Spot:
      return "Spot";
    }
    return "Unknown";
  }

  static glm::vec3 directionFromAngles(float azimuthRadians,
                                       float elevationRadians) {
    const float cosElevation = std::cos(elevationRadians);
    return glm::normalize(glm::vec3(cosElevation * std::cos(azimuthRadians),
                                    cosElevation * std::sin(azimuthRadians),
                                    std::sin(elevationRadians)));
  }

  static void anglesFromDirection(const glm::vec3 &direction,
                                  float &azimuthRadians,
                                  float &elevationRadians) {
    const glm::vec3 normalizedDirection = glm::normalize(
        glm::length(direction) > 1e-6f ? direction
                                       : glm::vec3(0.0f, -1.0f, -1.0f));
    azimuthRadians = std::atan2(normalizedDirection.y, normalizedDirection.x);
    elevationRadians =
        std::asin(glm::clamp(normalizedDirection.z, -1.0f, 1.0f));
  }

  SceneEditorUIResult buildHierarchyPanel() {
    SceneEditorUIResult result;
    auto &settings = bindings.settings;
    clampSceneObjectSelection(settings);
    clampBoneSelection(settings);
    const auto &lights = settings.sceneLights.lights();

    ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_MenuBar);
    buildHierarchyMenuBar(result);

    ImGui::SeparatorText("Objects");
    if (settings.sceneObjects.empty()) {
      ImGui::TextUnformatted("No objects in the scene.");
    } else {
      for (int index = 0; index < static_cast<int>(settings.sceneObjects.size());
           ++index) {
        buildObjectHierarchyNode(index);
      }
    }

    ImGui::SeparatorText("Lights");
    if (lights.empty()) {
      ImGui::TextUnformatted("No lights in the scene.");
    } else {
      for (int index = 0; index < static_cast<int>(lights.size()); ++index) {
        const SceneLight &light = lights[static_cast<size_t>(index)];
        std::string label =
            light.name + "##outliner_light_" + std::to_string(index);
        if (ImGui::Selectable(label.c_str(),
                              settings.selectedLightIndex == index)) {
          settings.selectedLightIndex = index;
          settings.selectedBoneIndex = -1;
        }
      }
    }
    ImGui::End();
    return result;
  }

  SceneEditorUIResult buildInspectorPanel() {
    auto &settings = bindings.settings;
    clampSceneObjectSelection(settings);
    clampBoneSelection(settings);

    ImGui::Begin("Inspector");
    SceneEditorUIResult result;
    if (settings.selectedLightIndex >= 0) {
      buildLightInspector();
    } else if (settings.selectedBoneIndex >= 0) {
      buildBoneInspector();
    } else if (!settings.sceneObjects.empty()) {
      result = buildObjectInspector();
    } else {
      ImGui::TextUnformatted("Nothing selected.");
    }
    ImGui::End();
    return result;
  }

  void buildObjectHierarchyNode(int index) {
    auto &settings = bindings.settings;
    const SceneObject &object = settings.sceneObjects[static_cast<size_t>(index)];
    const bool isSelectedObject =
        settings.selectedLightIndex < 0 && settings.selectedObjectIndex == index;
    const bool showSkeletonChild =
        isSelectedObject && currentSkeleton() != nullptr &&
        !currentSkeleton()->nodes.empty();

    const std::string label =
        (object.name.empty() ? "Scene Object " + std::to_string(index)
                             : object.name) +
        "##scene_object_" + std::to_string(index);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!showSkeletonChild) {
      flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (isSelectedObject && settings.selectedBoneIndex < 0) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (isSelectedObject) {
      flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
      selectObject(index);
    }

    if (open) {
      if (showSkeletonChild) {
        buildSkeletonHierarchyNode();
      }
      ImGui::TreePop();
    }
  }

  static std::vector<std::pair<size_t, std::string>>
  collectTerrainTargets(const std::vector<SceneAssetInstance> &sceneAssets) {
    std::vector<std::pair<size_t, std::string>> terrainTargets;
    terrainTargets.reserve(sceneAssets.size());
    for (size_t index = 0; index < sceneAssets.size(); ++index) {
      if (sceneAssets[index].kind != SceneAssetKind::Terrain) {
        continue;
      }
      terrainTargets.emplace_back(
          index, AppSceneController::sceneAssetName(sceneAssets[index], index));
    }
    return terrainTargets;
  }

  bool buildInstancedAssetPicker(SceneAssetInstance &sceneAsset) {
    const std::vector<std::filesystem::path> assetPaths = collectModelAssetPaths();
    if (assetPaths.empty()) {
      ImGui::TextUnformatted("No model assets found in assets/models");
      return false;
    }

    int selectedAssetIndex = -1;
    for (int index = 0; index < static_cast<int>(assetPaths.size()); ++index) {
      if (assetPaths[static_cast<size_t>(index)].generic_string() ==
          sceneAsset.assetPath) {
        selectedAssetIndex = index;
        break;
      }
    }

    const std::string previewLabel =
        selectedAssetIndex >= 0
            ? assetPaths[static_cast<size_t>(selectedAssetIndex)].filename().string()
            : std::string("<select asset>");
    bool changed = false;
    if (ImGui::BeginCombo("Instance Asset", previewLabel.c_str())) {
      for (int index = 0; index < static_cast<int>(assetPaths.size()); ++index) {
        const bool selected = index == selectedAssetIndex;
        const std::string label =
            assetPaths[static_cast<size_t>(index)].filename().string();
        if (ImGui::Selectable(label.c_str(), selected)) {
          sceneAsset.assetPath = assetPaths[static_cast<size_t>(index)].generic_string();
          changed = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    if (!sceneAsset.assetPath.empty()) {
      ImGui::TextWrapped("Source: %s", sceneAsset.assetPath.c_str());
    }
    return changed;
  }

  SceneEditorUIResult buildInstancedObjectInspector(size_t sceneAssetIndex,
                                                    SceneAssetInstance &sceneAsset) {
    SceneEditorUIResult result;
    bool liveReprojectRequested = false;
    ImGui::SeparatorText("Instanced Object");
    ImGui::Text("Placements: %zu", sceneAsset.instanceTransforms.size());
    ImGui::TextUnformatted("The root transform is not rendered for this asset.");

    result.sceneGeometryChanged |= buildInstancedAssetPicker(sceneAsset);

    const std::vector<std::pair<size_t, std::string>> terrainTargets =
        collectTerrainTargets(bindings.sceneAssets);
    if (terrainTargets.empty()) {
      ImGui::TextUnformatted("No terrain assets available.");
    } else {
      int selectedTerrainIndex = 0;
      bool targetFound = false;
      for (int index = 0; index < static_cast<int>(terrainTargets.size()); ++index) {
        if (terrainTargets[static_cast<size_t>(index)].second ==
            sceneAsset.targetTerrainName) {
          selectedTerrainIndex = index;
          targetFound = true;
          break;
        }
      }
      const std::string previewLabel =
          targetFound ? terrainTargets[static_cast<size_t>(selectedTerrainIndex)].second
                      : terrainTargets.front().second;
      if (ImGui::BeginCombo("Target Terrain", previewLabel.c_str())) {
        for (int index = 0; index < static_cast<int>(terrainTargets.size()); ++index) {
          const bool selected = index == selectedTerrainIndex;
          const std::string &label =
              terrainTargets[static_cast<size_t>(index)].second;
          if (ImGui::Selectable(label.c_str(), selected)) {
            sceneAsset.targetTerrainName = label;
            result.sceneAssetChanged = true;
            liveReprojectRequested = true;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      if (sceneAsset.targetTerrainName.empty()) {
        sceneAsset.targetTerrainName = terrainTargets.front().second;
        result.sceneAssetChanged = true;
      }
    }

    if (ImGui::DragFloat("Fill Spacing", &sceneAsset.instanceSpacing, 0.05f, 0.05f,
                         64.0f, "%.2f")) {
      sceneAsset.instanceSpacing =
          std::clamp(sceneAsset.instanceSpacing, 0.05f, 64.0f);
      result.sceneAssetChanged = true;
    }
    result.sceneAssetChanged |=
        ImGui::SliderFloat("Jitter", &sceneAsset.instanceJitter, 0.0f, 1.0f);
    if (ImGui::DragFloat2("Scale XZ Range", &sceneAsset.instanceScaleRange.x,
                          0.01f, 0.01f, 16.0f, "%.2f")) {
      sceneAsset.instanceScaleRange.x =
          std::max(sceneAsset.instanceScaleRange.x, 0.01f);
      sceneAsset.instanceScaleRange.y =
          std::max(sceneAsset.instanceScaleRange.y,
                   sceneAsset.instanceScaleRange.x);
      result.sceneAssetChanged = true;
      liveReprojectRequested = true;
    }
    if (ImGui::DragFloat2("Scale Y Range",
                          &sceneAsset.instanceScaleVerticalRange.x, 0.01f, 0.01f,
                          16.0f, "%.2f")) {
      sceneAsset.instanceScaleVerticalRange.x =
          std::max(sceneAsset.instanceScaleVerticalRange.x, 0.01f);
      sceneAsset.instanceScaleVerticalRange.y =
          std::max(sceneAsset.instanceScaleVerticalRange.y,
                   sceneAsset.instanceScaleVerticalRange.x);
      result.sceneAssetChanged = true;
      liveReprojectRequested = true;
    }
    const bool alignChanged = ImGui::Checkbox(
        "Align To Terrain Normal", &sceneAsset.instanceAlignToTerrainNormal);
    result.sceneAssetChanged |= alignChanged;
    liveReprojectRequested |= alignChanged;
    const bool randomYawChanged =
        ImGui::Checkbox("Random Yaw", &sceneAsset.instanceRandomYaw);
    result.sceneAssetChanged |= randomYawChanged;
    liveReprojectRequested |= randomYawChanged;
    const bool yawRangeChanged = ImGui::SliderFloat(
        "Yaw Range", &sceneAsset.instanceYawRangeDegrees, 0.0f, 360.0f,
        "%.0f deg");
    result.sceneAssetChanged |= yawRangeChanged;
    liveReprojectRequested |= yawRangeChanged;
    const bool pitchRangeChanged = ImGui::SliderFloat(
        "Pitch Range", &sceneAsset.instancePitchRangeDegrees, 0.0f, 180.0f,
        "%.0f deg");
    result.sceneAssetChanged |= pitchRangeChanged;
    liveReprojectRequested |= pitchRangeChanged;
    const bool rollRangeChanged = ImGui::SliderFloat(
        "Roll Range", &sceneAsset.instanceRollRangeDegrees, 0.0f, 180.0f,
        "%.0f deg");
    result.sceneAssetChanged |= rollRangeChanged;
    liveReprojectRequested |= rollRangeChanged;
    result.sceneAssetChanged |= ImGui::SliderFloat(
        "Max Slope", &sceneAsset.instanceMaxSlopeDegrees, 0.0f, 89.0f,
        "%.1f deg");
    const bool heightOffsetChanged = ImGui::DragFloat(
        "Height Offset", &sceneAsset.instanceHeightOffset, 0.005f, -4.0f, 4.0f,
        "%.3f");
    result.sceneAssetChanged |= heightOffsetChanged;
    liveReprojectRequested |= heightOffsetChanged;
    const bool heightJitterChanged = ImGui::DragFloat(
        "Height Jitter", &sceneAsset.instanceHeightJitter, 0.005f, 0.0f, 4.0f,
        "%.3f");
    if (heightJitterChanged) {
      sceneAsset.instanceHeightJitter =
          std::max(sceneAsset.instanceHeightJitter, 0.0f);
      result.sceneAssetChanged = true;
      liveReprojectRequested = true;
    }
    int scatterSeed = static_cast<int>(sceneAsset.instanceScatterSeed);
    if (ImGui::InputInt("Scatter Seed", &scatterSeed)) {
      sceneAsset.instanceScatterSeed = static_cast<uint32_t>(std::max(scatterSeed, 0));
      result.sceneAssetChanged = true;
      liveReprojectRequested = true;
    }
    if (ImGui::Checkbox("Paint Mode", &sceneAsset.instancePaintMode)) {
      if (!sceneAsset.instancePaintMode) {
        sceneAsset.instanceEraseMode = false;
      }
      result.sceneAssetChanged = true;
    }
    ImGui::BeginDisabled(!sceneAsset.instancePaintMode);
    result.sceneAssetChanged |=
        ImGui::Checkbox("Erase Mode", &sceneAsset.instanceEraseMode);
    ImGui::EndDisabled();
    if (ImGui::DragFloat("Brush Radius", &sceneAsset.instanceBrushRadius, 0.05f,
                         0.05f, 128.0f, "%.2f")) {
      sceneAsset.instanceBrushRadius =
          std::clamp(sceneAsset.instanceBrushRadius, 0.05f, 128.0f);
      result.sceneAssetChanged = true;
    }
    if (sceneAsset.instancePaintMode) {
      ImGui::TextUnformatted(
          sceneAsset.instanceEraseMode
              ? "Paint Tool: LMB erase, Up/Down radius, Left/Right toggles paint/erase."
              : "Paint Tool: LMB paint, Up/Down radius, Left/Right toggles paint/erase.");
    }
    ImGui::TextUnformatted(
        "Scale/orientation/height controls live-update existing placements. Spacing and jitter affect new fill/paint placements.");

    if (liveReprojectRequested && !sceneAsset.instanceTransforms.empty() &&
        bindings.callbacks.reprojectTerrainInstancedObject != nullptr) {
      result.sceneAssetChanged |=
          bindings.callbacks.reprojectTerrainInstancedObject(sceneAssetIndex);
    }

    const bool canScatter = !sceneAsset.assetPath.empty() && !terrainTargets.empty();
    ImGui::BeginDisabled(!canScatter);
    if (ImGui::Button("Fill Terrain") &&
        bindings.callbacks.fillTerrainWithInstancedObject != nullptr) {
      result.sceneAssetChanged |=
          bindings.callbacks.fillTerrainWithInstancedObject(sceneAssetIndex);
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Reproject") &&
        bindings.callbacks.reprojectTerrainInstancedObject != nullptr) {
      result.sceneAssetChanged |=
          bindings.callbacks.reprojectTerrainInstancedObject(sceneAssetIndex);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Clear Instances")) {
      sceneAsset.instanceTransforms.clear();
      result.sceneAssetChanged = true;
    }

    return result;
  }

  SceneEditorUIResult buildObjectInspector() {
    SceneEditorUIResult result;
    auto &settings = bindings.settings;
    if (settings.sceneObjects.empty()) {
      return result;
    }
    const size_t selectedIndex =
        static_cast<size_t>(settings.selectedObjectIndex);
    SceneObject &object = settings.sceneObjects[selectedIndex];
    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    SceneAssetInstance *sceneAsset =
        selectedIndex < bindings.sceneAssets.size()
            ? &bindings.sceneAssets[selectedIndex]
            : nullptr;

    ImGui::Text("Object: %s",
                object.name.empty() ? "<unnamed>" : object.name.c_str());
    if (ImGui::Button("Remove From Scene")) {
      removeSelectedSceneAsset();
      result.sceneAssetChanged = true;
      result.sceneGeometryChanged = true;
      return result;
    }
    if (sceneAsset != nullptr &&
        sceneAsset->kind == SceneAssetKind::InstancedObject) {
      const SceneEditorUIResult instancedResult =
          buildInstancedObjectInspector(selectedIndex, *sceneAsset);
      result.sceneAssetChanged |= instancedResult.sceneAssetChanged;
      result.sceneGeometryChanged |= instancedResult.sceneGeometryChanged;
      if (asset != nullptr) {
        ImGui::SeparatorText("Loaded Asset");
        const std::string assetPath = asset->path();
        ImGui::Text("Asset: %s",
                    std::filesystem::path(assetPath).filename().string().c_str());
      }
    } else if (asset != nullptr) {
      const std::string assetPath = asset->path();
      ImGui::Text("Asset: %s",
                  std::filesystem::path(assetPath).filename().string().c_str());
      if (!assetPath.empty()) {
        ImGui::TextWrapped("Source: %s", assetPath.c_str());
      }
      if (const auto *skeleton = bindings.sceneModel.skeletonAsset();
          skeleton != nullptr) {
        ImGui::SeparatorText("Skeleton");
        ImGui::Text("Nodes: %zu", skeleton->nodes.size());
        ImGui::Text("Skins: %zu", skeleton->skins.size());
        ImGui::Text("Animations: %zu", skeleton->animations.size());

      }
    } else {
      if (sceneAsset != nullptr &&
          sceneAsset->kind == SceneAssetKind::CharacterController) {
        ImGui::TextUnformatted("Asset: Character Controller");
      } else if (sceneAsset != nullptr &&
                 sceneAsset->kind == SceneAssetKind::Camera) {
        ImGui::TextUnformatted("Asset: Camera");
      } else if (sceneAsset != nullptr &&
                 sceneAsset->kind == SceneAssetKind::Terrain) {
        ImGui::TextUnformatted("Asset: Terrain");
      } else if (sceneAsset != nullptr &&
                 sceneAsset->kind == SceneAssetKind::InstancedObject) {
        ImGui::TextUnformatted("Asset: Instanced Object");
      } else {
        ImGui::TextUnformatted("Asset: Scene Model");
      }
    }

    if (sceneAsset == nullptr ||
        sceneAsset->kind != SceneAssetKind::InstancedObject) {
      ImGui::SeparatorText("Transform");
      bool transformChanged = false;
      transformChanged |=
          ImGui::DragFloat3("Position", &object.transform.position.x, 0.01f);
      transformChanged |= ImGui::SliderFloat3(
          "Rotation", &object.transform.rotationDegrees.x, -180.0f, 180.0f);
      transformChanged |=
          ImGui::DragFloat3("Scale", &object.transform.scale.x, 0.1f, 0.01f,
                            200.0f);
      result.sceneAssetChanged |= transformChanged;
    }
    result.sceneAssetChanged |= buildCameraInspector(selectedIndex, sceneAsset);

    const TerrainInspectorResult terrainResult =
        buildTerrainInspector(selectedIndex, sceneAsset);
    result.sceneAssetChanged |= terrainResult.assetChanged;
    result.sceneGeometryChanged |= terrainResult.geometryChanged;

    if (asset == nullptr) {
      return result;
    }

    auto &materials = bindings.sceneModel.mutableMaterials();
    if (materials.empty()) {
      return result;
    }

    settings.selectedMaterialIndex =
        std::clamp(settings.selectedMaterialIndex, 0,
                   static_cast<int>(materials.size()) - 1);

    ImGui::SeparatorText("Materials");
    if (ImGui::BeginListBox("Material Slots")) {
      for (int index = 0; index < static_cast<int>(materials.size()); ++index) {
        const bool selected = settings.selectedMaterialIndex == index;
        const char *label = materials[index].name.empty()
                                ? "<unnamed>"
                                : materials[index].name.c_str();
        if (ImGui::Selectable(label, selected)) {
          settings.selectedMaterialIndex = index;
        }
      }
      ImGui::EndListBox();
    }

    bool materialChanged = false;
    auto &material =
        materials[static_cast<size_t>(settings.selectedMaterialIndex)];
    ImGui::Text("Selected: %s",
                material.name.empty() ? "<unnamed>" : material.name.c_str());
    materialChanged |=
        ImGui::ColorEdit4("Base Color", &material.baseColorFactor.x);
    materialChanged |=
        ImGui::ColorEdit3("Emissive", &material.emissiveFactor.x);
    materialChanged |=
        ImGui::SliderFloat("Metallic", &material.metallicFactor, 0.0f, 1.0f);
    materialChanged |=
        ImGui::SliderFloat("Roughness", &material.roughnessFactor, 0.0f, 1.0f);
    materialChanged |= ImGui::SliderFloat(
        "Occlusion Strength", &material.occlusionStrength, 0.0f, 1.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Textures");
    ImGui::BulletText("Base Color: %s",
                      material.baseColorTexture.hasPath() ||
                              material.baseColorTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText(
        "Metallic/Roughness: %s",
        material.metallicRoughnessTexture.hasPath() ||
                material.metallicRoughnessTexture.hasEmbeddedRgba()
            ? "yes"
            : "no");
    ImGui::BulletText("Emissive: %s",
                      material.emissiveTexture.hasPath() ||
                              material.emissiveTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText("Occlusion: %s",
                      material.occlusionTexture.hasPath() ||
                              material.occlusionTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    result.materialChanged = materialChanged;
    return result;
  }

  bool buildCameraInspector(size_t sceneAssetIndex,
                            SceneAssetInstance *sceneAsset) {
    if (sceneAsset == nullptr || sceneAsset->kind != SceneAssetKind::Camera) {
      return false;
    }

    auto &settings = bindings.settings;
    bool changed = false;
    const bool previewActive =
        DefaultDebugCameraController::sceneCameraPreviewActive(settings) &&
        settings.viewportSceneCameraIndex == static_cast<int>(sceneAssetIndex);

    ImGui::SeparatorText("Camera");
    ImGui::TextUnformatted(previewActive ? "Viewport: Scene Camera"
                                         : "Viewport: Debug Camera");
    if (previewActive) {
      if (ImGui::Button("Return To Debug Camera")) {
        DefaultDebugCameraController::deactivateSceneCameraPreview(settings);
      }
    } else if (ImGui::Button("View Through Camera")) {
      DefaultDebugCameraController::activateSceneCameraPreview(
          settings, static_cast<int>(sceneAssetIndex));
    }
    ImGui::TextUnformatted("Press ESC to return to the debug camera.");

    if (ImGui::Button("Match Debug Camera")) {
      sceneAsset->transform =
          DefaultDebugCameraController::sceneTransformFromSettings(settings);
      sceneAsset->cameraConfig.farPlane = settings.cameraFarPlane;
      changed = true;
    }

    changed |= ImGui::SliderFloat("Field Of View",
                                  &sceneAsset->cameraConfig.fieldOfViewDegrees,
                                  10.0f, 120.0f, "%.1f deg");
    changed |= ImGui::SliderFloat("Camera Far Clip",
                                  &sceneAsset->cameraConfig.farPlane, 10.0f,
                                  500.0f, "%.1f");
    return changed;
  }

  struct TerrainInspectorResult {
    bool assetChanged = false;
    bool geometryChanged = false;
  };

  TerrainInspectorResult buildTerrainInspector(size_t sceneAssetIndex,
                                               SceneAssetInstance *sceneAsset) {
    TerrainInspectorResult result;
    if (sceneAsset == nullptr || sceneAsset->kind != SceneAssetKind::Terrain) {
      return result;
    }

    ImGui::SeparatorText("Terrain");

    result.assetChanged |=
        ImGui::Checkbox("Wireframe", &sceneAsset->terrainWireframeVisible);
    result.assetChanged |=
        ImGui::Checkbox("Edit Mode", &sceneAsset->terrainEditMode);
    result.assetChanged |=
        ImGui::Checkbox("Lower Mode", &sceneAsset->terrainBrushLowerMode);
    result.assetChanged |=
        ImGui::Checkbox("Flatten Mode", &sceneAsset->terrainBrushFlattenMode);
    result.assetChanged |= ImGui::Checkbox(
        "Color Paint Mode", &sceneAsset->terrainBrushColorPaintMode);
    result.assetChanged |=
        ImGui::ColorEdit4("Paint Color", &sceneAsset->terrainBrushColor.x);
    result.assetChanged |= ImGui::Checkbox(
        "Texture Paint Mode", &sceneAsset->terrainBrushTexturePaintMode);
    result.assetChanged |= ImGui::SliderFloat(
        "Texture Paint Opacity", &sceneAsset->terrainBrushOpacity, 0.0f, 1.0f);
    result.assetChanged |= ImGui::SliderFloat(
        "Texture Variation", &sceneAsset->terrainBrushTextureVariation, 0.0f,
        1.0f);
    if (sceneAsset->terrainBrushTexturePaintMode) {
      ImGui::TextUnformatted(
          "Lower opacity keeps the paint already on the terrain visible below.");
    }
    if (ImGui::DragFloat("Brush Radius", &sceneAsset->terrainBrushRadius, 0.05f,
                         0.05f, 128.0f, "%.2f")) {
      sceneAsset->terrainBrushRadius =
          std::clamp(sceneAsset->terrainBrushRadius, 0.05f, 128.0f);
      result.assetChanged = true;
    }
    int canvasResolution =
        static_cast<int>(sceneAsset->terrainPaintCanvasResolution);
    if (ImGui::SliderInt("Paint Resolution", &canvasResolution, 128, 2048)) {
      sceneAsset->terrainPaintCanvasResolution =
          static_cast<uint32_t>(std::max(canvasResolution, 128));
      sceneAsset->terrainPaintCanvasPath.clear();
      result.assetChanged = true;
    }
    const std::vector<std::filesystem::path> brushTexturePaths =
        collectTerrainBrushTexturePaths();
    if (brushTexturePaths.empty()) {
      ImGui::TextUnformatted("No brush textures found in assets/textures");
    } else {
      int selectedTextureIndex = 0;
      bool selectedTextureFound = false;
      for (int index = 0; index < static_cast<int>(brushTexturePaths.size());
           ++index) {
        if (brushTexturePaths[static_cast<size_t>(index)].generic_string() ==
            sceneAsset->terrainBrushTexturePath) {
          selectedTextureIndex = index;
          selectedTextureFound = true;
          break;
        }
      }

      const std::string previewLabel =
          selectedTextureFound
              ? brushTexturePaths[static_cast<size_t>(selectedTextureIndex)]
                    .filename()
                    .string()
              : std::string("<select texture>");
      if (ImGui::BeginCombo("Brush Texture", previewLabel.c_str())) {
        for (int index = 0; index < static_cast<int>(brushTexturePaths.size());
             ++index) {
          const bool selected = index == selectedTextureIndex;
          const std::string label =
              brushTexturePaths[static_cast<size_t>(index)].filename().string();
          if (ImGui::Selectable(label.c_str(), selected)) {
            sceneAsset->terrainBrushTexturePath =
                brushTexturePaths[static_cast<size_t>(index)].generic_string();
            result.assetChanged = true;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
    }
    if (ImGui::Button("Clear Paint Canvas")) {
      sceneAsset->terrainPaintCanvasPath.clear();
      result.assetChanged = true;
    }
    ImGui::BeginDisabled(sceneAsset->terrainBrushTexturePath.empty());
    if (ImGui::Button("Bucket Paint Texture") &&
        bindings.callbacks.bucketPaintTerrainTexture != nullptr) {
      result.assetChanged |=
          bindings.callbacks.bucketPaintTerrainTexture(sceneAssetIndex);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Bucket Paint")) {
      TerrainGenerator::ensureVertexColors(sceneAsset->terrainConfig);
      std::fill(sceneAsset->terrainConfig.vertexColors.begin(),
                sceneAsset->terrainConfig.vertexColors.end(),
                sceneAsset->terrainBrushColor);
      result.assetChanged = true;
      result.geometryChanged = true;
    }
    if (ImGui::Button("Reset Height Offsets")) {
      TerrainGenerator::ensureHeightOffsets(sceneAsset->terrainConfig);
      std::fill(sceneAsset->terrainConfig.heightOffsets.begin(),
                sceneAsset->terrainConfig.heightOffsets.end(), 0.0f);
      result.assetChanged = true;
      result.geometryChanged = true;
    }
    if (ImGui::Button("Reset Vertex Colors")) {
      TerrainGenerator::ensureVertexColors(sceneAsset->terrainConfig);
      std::fill(sceneAsset->terrainConfig.vertexColors.begin(),
                sceneAsset->terrainConfig.vertexColors.end(),
                glm::vec4(1.0f));
      result.assetChanged = true;
      result.geometryChanged = true;
    }

    int subdivisions =
        static_cast<int>(std::max(sceneAsset->terrainConfig.xSegments,
                                  sceneAsset->terrainConfig.zSegments));
    bool changed = ImGui::SliderInt("Subdivisions", &subdivisions, 1, 256);
    subdivisions = std::clamp(subdivisions, 1, 256);
    if (changed) {
      TerrainGenerator::resampleSurfaceLayers(
          sceneAsset->terrainConfig, static_cast<uint32_t>(subdivisions),
          static_cast<uint32_t>(subdivisions));
      result.assetChanged = true;
      result.geometryChanged = true;
    }

    float size[2] = {sceneAsset->terrainConfig.sizeX, sceneAsset->terrainConfig.sizeZ};
    if (ImGui::DragFloat2("Size", size, 0.1f, 0.1f, 500.0f)) {
      sceneAsset->terrainConfig.sizeX = std::max(size[0], 0.1f);
      sceneAsset->terrainConfig.sizeZ = std::max(size[1], 0.1f);
      result.assetChanged = true;
      result.geometryChanged = true;
    }

    return result;
  }

  void buildSkeletonHierarchyNode() {
    auto &settings = bindings.settings;
    const SkeletonAssetData *skeleton = currentSkeleton();
    if (skeleton == nullptr || skeleton->nodes.empty()) {
      return;
    }
    const std::unordered_set<int> deformingBones = currentDeformingBoneSet();
    if (deformingBones.empty()) {
      return;
    }
    const std::vector<std::vector<int>> compressedChildren =
        buildCompressedDeformingChildren(*skeleton, deformingBones);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (settings.selectedBoneIndex >= 0) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool open = ImGui::TreeNodeEx("Skeleton##object_skeleton", flags);
    if (ImGui::IsItemClicked()) {
      settings.selectedBoneIndex = -1;
      settings.selectedLightIndex = -1;
    }
    if (!open) {
      return;
    }

    std::vector<bool> visited(skeleton->nodes.size(), false);
    for (const int jointNodeIndex : deformingBones) {
      if (jointNodeIndex < 0 ||
          static_cast<size_t>(jointNodeIndex) >= skeleton->nodes.size()) {
        continue;
      }

      const SkeletonNode &jointNode =
          skeleton->nodes[static_cast<size_t>(jointNodeIndex)];
      if (nearestDeformingParentIndex(*skeleton, deformingBones,
                                      jointNodeIndex) >= 0) {
        continue;
      }
      markCompressedBoneSubtreeVisited(visited, compressedChildren,
                                       jointNodeIndex);
      buildBoneTreeNode(*skeleton, settings, deformingBones,
                        compressedChildren, jointNodeIndex);
    }

    for (size_t nodeIndex = 0; nodeIndex < skeleton->nodes.size(); ++nodeIndex) {
      if (deformingBones.contains(static_cast<int>(nodeIndex)) &&
          !visited[nodeIndex]) {
        markCompressedBoneSubtreeVisited(visited, compressedChildren,
                                         static_cast<int>(nodeIndex));
        buildBoneTreeNode(*skeleton, settings, deformingBones,
                          compressedChildren, static_cast<int>(nodeIndex));
      }
    }

    ImGui::TreePop();
  }

  void buildBoneTreeNode(const SkeletonAssetData &skeleton,
                         DefaultDebugUISettings &settings,
                         const std::unordered_set<int> &deformingBones,
                         const std::vector<std::vector<int>> &compressedChildren,
                         int nodeIndex) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= skeleton.nodes.size()) {
      return;
    }
    if (!deformingBones.contains(nodeIndex)) {
      return;
    }
    const SkeletonNode &node = skeleton.nodes[static_cast<size_t>(nodeIndex)];
    const bool selected = settings.selectedBoneIndex == nodeIndex;
    const auto &visualChildren =
        compressedChildren[static_cast<size_t>(nodeIndex)];
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (visualChildren.empty()) {
      flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (selected) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label =
        (node.name.empty() ? "Bone " + std::to_string(nodeIndex) : node.name) +
        "##bone_" + std::to_string(nodeIndex);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
      selectBone(nodeIndex);
    }

    if (open) {
      for (const int childIndex : visualChildren) {
        buildBoneTreeNode(skeleton, settings, deformingBones,
                          compressedChildren, childIndex);
      }
      ImGui::TreePop();
    }
  }

  void buildBoneInspector() {
    auto &settings = bindings.settings;
    const SkeletonAssetData *skeleton = currentSkeleton();
    SkeletonPose *pose = bindings.sceneModel.mutableSkeletonPose();
    if (skeleton == nullptr || skeleton->nodes.empty() ||
        pose == nullptr ||
        settings.selectedBoneIndex < 0 ||
        static_cast<size_t>(settings.selectedBoneIndex) >= skeleton->nodes.size()) {
      settings.selectedBoneIndex = -1;
      ImGui::TextUnformatted("No bone selected.");
      return;
    }

    const SkeletonNode &bone =
        skeleton->nodes[static_cast<size_t>(settings.selectedBoneIndex)];
    const std::unordered_set<int> deformingBones = currentDeformingBoneSet();
    const std::vector<std::vector<int>> compressedChildren =
        buildCompressedDeformingChildren(*skeleton, deformingBones);
    ImGui::Text("Bone: %s",
                bone.name.empty() ? "<unnamed>" : bone.name.c_str());
    ImGui::Text("Node Index: %d", settings.selectedBoneIndex);
    ImGui::Text("Parent Index: %d", bone.parentIndex);
    ImGui::Text("Child Count: %zu", bone.childIndices.size());

    int referencedSkinCount = 0;
    int jointReferenceCount = 0;
    for (const auto &skin : skeleton->skins) {
      bool skinContainsBone = false;
      for (const int jointNodeIndex : skin.jointNodeIndices) {
        if (jointNodeIndex != settings.selectedBoneIndex) {
          continue;
        }
        skinContainsBone = true;
        ++jointReferenceCount;
      }
      if (skinContainsBone) {
        ++referencedSkinCount;
      }
    }

    ImGui::SeparatorText("Bindings");
    ImGui::Text("Referenced By Skins: %d", referencedSkinCount);
    ImGui::Text("Joint References: %d", jointReferenceCount);
    ImGui::Text("Visual Parent Index: %d",
                nearestDeformingParentIndex(*skeleton, deformingBones,
                                            settings.selectedBoneIndex));

    ImGui::SeparatorText("Local Bind Transform");
    ImGui::Text("Translation: %.3f %.3f %.3f",
                bone.localBindTransform.translation.x,
                bone.localBindTransform.translation.y,
                bone.localBindTransform.translation.z);
    const glm::vec3 rotationDegrees =
        glm::degrees(glm::eulerAngles(bone.localBindTransform.rotation));
    ImGui::Text("Rotation (Euler): %.3f %.3f %.3f", rotationDegrees.x,
                rotationDegrees.y, rotationDegrees.z);
    ImGui::Text("Rotation (Quat): %.3f %.3f %.3f %.3f",
                bone.localBindTransform.rotation.x,
                bone.localBindTransform.rotation.y,
                bone.localBindTransform.rotation.z,
                bone.localBindTransform.rotation.w);
    ImGui::Text("Scale: %.3f %.3f %.3f", bone.localBindTransform.scale.x,
                bone.localBindTransform.scale.y, bone.localBindTransform.scale.z);

    NodeTransform &poseTransform =
        pose->localTransform(static_cast<size_t>(settings.selectedBoneIndex));
    glm::vec3 poseRotationDegrees =
        glm::degrees(glm::eulerAngles(poseTransform.rotation));
    bool poseChanged = false;

    ImGui::SeparatorText("Current Pose");
    ImGui::Text("Translation: %.3f %.3f %.3f", poseTransform.translation.x,
                poseTransform.translation.y, poseTransform.translation.z);
    poseChanged |= ImGui::SliderFloat3("Local Rotation",
                                       &poseRotationDegrees.x, -180.0f, 180.0f);
    ImGui::Text("Scale: %.3f %.3f %.3f", poseTransform.scale.x,
                poseTransform.scale.y, poseTransform.scale.z);

    if (poseChanged) {
      poseTransform.rotation =
          glm::normalize(glm::quat(glm::radians(poseRotationDegrees)));
      pose->recomputeWorldTransforms(*skeleton);
    }

    if (ImGui::Button("Reset Selected Bone")) {
      poseTransform = bone.localBindTransform;
      pose->recomputeWorldTransforms(*skeleton);
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Reset Skeleton Pose")) {
      bindings.sceneModel.resetSkeletonPose();
    }

    const glm::mat4 &worldTransform =
        pose->worldTransform(static_cast<size_t>(settings.selectedBoneIndex));
    ImGui::SeparatorText("World Pose");
    ImGui::Text("Position: %.3f %.3f %.3f", worldTransform[3].x,
                worldTransform[3].y, worldTransform[3].z);

    const auto &visualChildren =
        compressedChildren[static_cast<size_t>(settings.selectedBoneIndex)];
    if (!visualChildren.empty()) {
      ImGui::SeparatorText("Visual Children");
      for (const int childIndex : visualChildren) {
        if (childIndex < 0 ||
            static_cast<size_t>(childIndex) >= skeleton->nodes.size()) {
          continue;
        }
        const SkeletonNode &child =
            skeleton->nodes[static_cast<size_t>(childIndex)];
        ImGui::BulletText("%s [%d]",
                          child.name.empty() ? "<unnamed>" : child.name.c_str(),
                          childIndex);
      }
    }

    ImGui::SeparatorText("Animation");
    int animatedTrackCount = 0;
    for (const auto &animation : skeleton->animations) {
      for (const auto &track : animation.tracks) {
        if (track.targetNodeIndex == settings.selectedBoneIndex) {
          ++animatedTrackCount;
        }
      }
    }
    ImGui::Text("Animated Tracks: %d", animatedTrackCount);
  }

  void buildLightInspector() {
    auto &settings = bindings.settings;
    auto &lights = settings.sceneLights.lights();
    if (lights.empty()) {
      settings.selectedLightIndex = -1;
      ImGui::TextUnformatted("No lights in the scene.");
      return;
    }

    settings.selectedLightIndex = std::clamp(
        settings.selectedLightIndex, 0, static_cast<int>(lights.size()) - 1);
    SceneLight &light =
        lights[static_cast<size_t>(settings.selectedLightIndex)];

    ImGui::Text("Light: %s", light.name.c_str());
    ImGui::Text("Type: %s", lightTypeLabel(light.type));
    ImGui::Checkbox("Enabled", &light.enabled);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Power", &light.power, 0.1f, 0.0f, 10000.0f, "%.3f");
    light.power = std::max(light.power, 0.0f);
    ImGui::SliderFloat("Exposure", &light.exposure, -16.0f, 16.0f, "%.3f");

    ImGui::SeparatorText("Shadowing");
    if (light.type == SceneLightType::Directional ||
        light.type == SceneLightType::Spot) {
      ImGui::Checkbox("Casts Shadow", &light.castsShadow);
      ImGui::SliderFloat("Shadow Bias", &light.shadowBias, 0.0001f, 0.02f,
                         "%.4f");
      ImGui::SliderFloat("Shadow Normal Bias", &light.shadowNormalBias, 0.0f,
                         0.2f, "%.4f");
    } else {
      bool pointShadowDisabled = false;
      ImGui::BeginDisabled();
      ImGui::Checkbox("Casts Shadow", &pointShadowDisabled);
      ImGui::EndDisabled();
      light.castsShadow = false;
      ImGui::TextUnformatted("Point-light shadows are not implemented yet.");
    }

    if (light.type == SceneLightType::Directional) {
      ImGui::TextUnformatted("Directional lights use direction only.");
    }

    if (light.type == SceneLightType::Directional ||
        light.type == SceneLightType::Spot) {
      ImGui::SeparatorText("Direction");
      float azimuthRadians = 0.0f;
      float elevationRadians = 0.0f;
      anglesFromDirection(light.direction, azimuthRadians, elevationRadians);
      float azimuthDegrees = glm::degrees(azimuthRadians);
      float elevationDegrees = glm::degrees(elevationRadians);
      if (ImGui::SliderFloat("Azimuth", &azimuthDegrees, -180.0f, 180.0f)) {
        azimuthRadians = glm::radians(azimuthDegrees);
        light.direction = directionFromAngles(azimuthRadians, elevationRadians);
      }
      if (ImGui::SliderFloat("Elevation", &elevationDegrees, -89.0f, 89.0f)) {
        elevationRadians = glm::radians(elevationDegrees);
        light.direction = directionFromAngles(azimuthRadians, elevationRadians);
      }
      ImGui::Text("Direction: %.2f %.2f %.2f", light.direction.x,
                  light.direction.y, light.direction.z);
    }

    if (light.type == SceneLightType::Point ||
        light.type == SceneLightType::Spot) {
      ImGui::SeparatorText("Transform");
      ImGui::DragFloat3("Position", &light.position.x, 0.05f);
    }

    if (light.type == SceneLightType::Point) {
      ImGui::DragFloat("Radius", &light.radius, 0.01f, 0.0f, 25.0f, "%.3f m");
      light.radius = std::max(light.radius, 0.0f);
    }

    if (light.type == SceneLightType::Spot) {
      ImGui::SliderFloat("Range", &light.range, 0.5f, 25.0f);
      light.range = std::max(light.range, 0.01f);
      float innerDegrees = glm::degrees(light.innerConeAngleRadians);
      float outerDegrees = glm::degrees(light.outerConeAngleRadians);
      if (ImGui::SliderFloat("Inner Cone", &innerDegrees, 1.0f, 85.0f)) {
        light.innerConeAngleRadians = glm::radians(innerDegrees);
      }
      if (ImGui::SliderFloat("Outer Cone", &outerDegrees, 1.0f, 89.0f)) {
        light.outerConeAngleRadians = glm::radians(outerDegrees);
      }
      light.outerConeAngleRadians =
          std::max(light.outerConeAngleRadians, light.innerConeAngleRadians);
    }

    if (ImGui::Button("Remove From Scene")) {
      settings.sceneLights.remove(
          static_cast<size_t>(settings.selectedLightIndex));
      settings.selectedLightIndex =
          settings.sceneLights.empty()
              ? -1
              : std::clamp(settings.selectedLightIndex, 0,
                           static_cast<int>(settings.sceneLights.size()) - 1);
      settings.selectedBoneIndex = -1;
    }

    const glm::vec3 primaryDirection =
        bindings.callbacks.currentPrimaryDirectionalLightWorld();
    ImGui::SeparatorText("Primary Directional");
    ImGui::Text("Direction: %.2f %.2f %.2f", primaryDirection.x,
                primaryDirection.y, primaryDirection.z);
  }
};
