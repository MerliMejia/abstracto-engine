cmake_minimum_required(VERSION 3.20)
project(abstracto_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET abstracto_renderer)
  add_subdirectory(systems/abstracto build/abstracto)
endif()
if(NOT TARGET abstracto_scene)
  add_subdirectory(systems/abstracto-scene build/abstracto-scene)
endif()
if(NOT TARGET abstracto_animation)
  add_subdirectory(systems/abstracto-animation build/abstracto-animation)
endif()
if(NOT TARGET abstracto_assets)
  add_subdirectory(systems/abstracto-assets build/abstracto-assets)
endif()

add_library(abstracto_engine INTERFACE)
target_include_directories(abstracto_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  abstracto_engine
  INTERFACE
    abstracto_renderer
    abstracto_scene
    abstracto_animation
    abstracto_assets
)

add_executable(AbstractoEngine apps/tree_scene/main.cpp)
target_include_directories(AbstractoEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(AbstractoEngine PRIVATE abstracto_engine)
