cmake_minimum_required(VERSION 3.30)

project(spacerabbit_package_consumer LANGUAGES CXX)

if(NOT DEFINED SPACERABBIT_TEST_LINK_TARGET)
  message(FATAL_ERROR "SPACERABBIT_TEST_LINK_TARGET must be provided.")
endif()

find_package(SpaceRabbit CONFIG REQUIRED)

add_executable(spacerabbit_package_consumer main.cpp)
target_compile_features(spacerabbit_package_consumer PRIVATE cxx_std_23)
set_target_properties(
  spacerabbit_package_consumer
  PROPERTIES
    CXX_EXTENSIONS OFF
)
target_link_libraries(
  spacerabbit_package_consumer
  PRIVATE
    "${SPACERABBIT_TEST_LINK_TARGET}"
)
