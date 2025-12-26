cmake_minimum_required(VERSION 3.13)
project(cospas_sarsat_decoder)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Module sources
file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

# ============================================================================
# SDR++ Source Directory Detection
# ============================================================================

# Try to find SDR++ source directory
if(NOT DEFINED SDRPP_SOURCE_DIR)
    # Check common locations
    if(EXISTS "${CMAKE_SOURCE_DIR}/../SDRPlusPlus/core/src")
        set(SDRPP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../SDRPlusPlus")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/../../SDRPlusPlus/core/src")
        set(SDRPP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../../SDRPlusPlus")
    elseif(EXISTS "C:/Dev/SDRPlusPlus/core/src")
        set(SDRPP_SOURCE_DIR "C:/Dev/SDRPlusPlus")
    elseif(EXISTS "$ENV{HOME}/SDRPlusPlus/core/src")
        set(SDRPP_SOURCE_DIR "$ENV{HOME}/SDRPlusPlus")
    elseif(EXISTS "$ENV{HOME}/Téléchargements/SDRPlusPlus/core/src")
        set(SDRPP_SOURCE_DIR "$ENV{HOME}/Téléchargements/SDRPlusPlus")
    endif()
endif()

if(NOT DEFINED SDRPP_BUILD_DIR)
    if(DEFINED SDRPP_SOURCE_DIR)
        set(SDRPP_BUILD_DIR "${SDRPP_SOURCE_DIR}/build")
    endif()
endif()

message(STATUS "SDRPP_SOURCE_DIR: ${SDRPP_SOURCE_DIR}")
message(STATUS "SDRPP_BUILD_DIR: ${SDRPP_BUILD_DIR}")

# ============================================================================
# Build Configuration
# ============================================================================

# Create shared library
add_library(cospas_sarsat_decoder SHARED ${SRC})

# No lib prefix on output
set_target_properties(cospas_sarsat_decoder PROPERTIES PREFIX "")

# Include directories
target_include_directories(cospas_sarsat_decoder PRIVATE 
    "src/"
)

# SDR++ headers
if(DEFINED SDRPP_SOURCE_DIR AND EXISTS "${SDRPP_SOURCE_DIR}/core/src")
    target_include_directories(cospas_sarsat_decoder PRIVATE 
        "${SDRPP_SOURCE_DIR}/core/src"
        "${SDRPP_SOURCE_DIR}/core/src/imgui"
    )
    message(STATUS "Found SDR++ headers at ${SDRPP_SOURCE_DIR}/core/src")
else()
    # Try system paths
    find_path(SDRPP_CORE_INCLUDE 
        NAMES module.h
        PATHS 
            /usr/include/sdrpp_core
            /usr/local/include/sdrpp_core
    )
    if(SDRPP_CORE_INCLUDE)
        target_include_directories(cospas_sarsat_decoder PRIVATE ${SDRPP_CORE_INCLUDE})
        message(STATUS "Found SDR++ headers at ${SDRPP_CORE_INCLUDE}")
    else()
        message(WARNING "SDR++ headers not found. Set SDRPP_SOURCE_DIR.")
    endif()
endif()

# ============================================================================
# Platform-specific Configuration
# ============================================================================

if(MSVC)
    # Windows / Visual Studio
    target_compile_options(cospas_sarsat_decoder PRIVATE 
        /O2 
        /EHsc 
        /D_USE_MATH_DEFINES
        /DNOMINMAX
    )
    
    # Link against SDR++ core (Windows)
    if(EXISTS "${SDRPP_BUILD_DIR}/Release/sdrpp_core.lib")
        target_link_libraries(cospas_sarsat_decoder PRIVATE 
            "${SDRPP_BUILD_DIR}/Release/sdrpp_core.lib"
        )
    elseif(EXISTS "${SDRPP_BUILD_DIR}/sdrpp_core.lib")
        target_link_libraries(cospas_sarsat_decoder PRIVATE 
            "${SDRPP_BUILD_DIR}/sdrpp_core.lib"
        )
    endif()
    
    # Output to .dll
    set_target_properties(cospas_sarsat_decoder PROPERTIES 
        SUFFIX ".dll"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    )
    
else()
    # Linux / GCC / Clang
    target_compile_options(cospas_sarsat_decoder PRIVATE 
        -O3 
        -fPIC
    )
    
    # Don't use -march=native for better compatibility
    # Users can add it manually if desired
    
    # Link against SDR++ core (Linux)
    if(EXISTS "${SDRPP_BUILD_DIR}/libsdrpp_core.so")
        target_link_libraries(cospas_sarsat_decoder PRIVATE 
            "${SDRPP_BUILD_DIR}/libsdrpp_core.so"
        )
    elseif(EXISTS "/usr/lib/libsdrpp_core.so")
        target_link_libraries(cospas_sarsat_decoder PRIVATE 
            "/usr/lib/libsdrpp_core.so"
        )
    endif()
    
    # Output to .so
    set_target_properties(cospas_sarsat_decoder PROPERTIES 
        SUFFIX ".so"
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Determine install path
if(NOT DEFINED SDRPP_PLUGIN_DIR)
    if(WIN32)
        set(SDRPP_PLUGIN_DIR "C:/Program Files/SDR++/plugins")
    else()
        set(SDRPP_PLUGIN_DIR "/usr/lib/sdrpp/plugins")
    endif()
endif()

install(TARGETS cospas_sarsat_decoder
    LIBRARY DESTINATION ${SDRPP_PLUGIN_DIR}
    RUNTIME DESTINATION ${SDRPP_PLUGIN_DIR}
)

message(STATUS "Plugin will be installed to: ${SDRPP_PLUGIN_DIR}")
