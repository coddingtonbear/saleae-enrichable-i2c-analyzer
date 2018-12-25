cmake_minimum_required (VERSION 3.11)
project(i2c_analyzer)

# custom CMake Modules are located in the cmake directory.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ExternalAnalyzerSDK)

set(SOURCES 
src/EnrichableI2cAnalyzer.cpp
src/EnrichableI2cAnalyzer.h
src/EnrichableI2cAnalyzerResults.cpp
src/EnrichableI2cAnalyzerResults.h
src/EnrichableI2cAnalyzerSettings.cpp
src/EnrichableI2cAnalyzerSettings.h
src/EnrichableI2cSimulationDataGenerator.cpp
src/EnrichableI2cSimulationDataGenerator.h
)

add_analyzer_plugin(i2c_analyzer SOURCES ${SOURCES})
