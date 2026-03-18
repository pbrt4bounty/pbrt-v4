
cmake_minimum_required (VERSION 3.12)

if (MSVC)
  add_definitions(/D_CRT_SECURE_NO_WARNINGS /Dstrdup=_strdup
    /wd4005 /wd4018 /wd4100 /wd4101 /wd4127 /wd4146 /wd4232 /wd4242 /wd4244 /wd4245 /wd4267 /wd4305 /wd4309 
    /wd4310 /wd4334 /wd4456 /wd4464 /wd4668 /wd4701 /wd4703 /wd4711 /wd4756 /wd4820 /wd5045 /wd5250)
endif ()
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang") # mute some Clang 16 warnings
  add_definitions(
    -Wno-deprecated-coroutine #-Wno-deprecated-non-prototype # zlib
    -Wno-tautological-constant-out-of-range-compare # Ptex
    -Wno-deprecated-declarations # utils, string
    -Wno-shift-negative-value
  ) 
endif()

###########################################################################
# stb

set (STB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/stb PARENT_SCOPE)

###########################################################################
# filesystem

set (FILESYSTEM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/filesystem PARENT_SCOPE)

###########################################################################
# libdeflate

add_subdirectory (libdeflate)

set (LIBDEFLATE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/libdeflate PARENT_SCOPE)
set (LIBDEFLATE_LIBRARIES deflate::deflate PARENT_SCOPE)

set_property (TARGET deflate PROPERTY FOLDER "ext")

###########################################################################
# zlib

find_package (ZLIB)
if (NOT ZLIB_FOUND)
  # Build zlib
  message(STATUS "Zlib not found; building it from scratch.")
  set (ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL " " FORCE)
  set (ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
  add_subdirectory (zlib)

  # this make openexr and Ptex happy about Zlib...
  set (ZLIB_LIBRARY zlibstatic)
  set (ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)
  set (ZLIB_FOUND TRUE)

  set_property (TARGET zlibstatic PROPERTY FOLDER "ext")

  add_library (ZLIB::ZLIB ALIAS zlibstatic)
  include_directories(${ZLIB_INCLUDE_DIR})  # yuck, but so openexr/ptex can find zlib.h...
endif ()

set (ZLIB_INCLUDE_DIR ${ZLIB_INCLUDE_DIR} PARENT_SCOPE)
set (ZLIB_LIBRARY ${ZLIB_LIBRARY} PARENT_SCOPE)

###########################################################################
# openexr

find_package(OpenEXR)

###########################################################################
# ptex

set (PTEX_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)

set (CMAKE_MACOSX_RPATH 1)
if (WIN32)
  add_definitions (/DPTEX_STATIC)
endif ()

add_subdirectory (ptex)

set_property (TARGET Ptex_static ptxinfo halftest ftest rtest wtest PROPERTY FOLDER "ext/ptex")

set (PTEX_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/ptex/src/ptex PARENT_SCOPE)

###########################################################################
# double-conversion

add_subdirectory (double-conversion)

set (DOUBLE_CONVERSION_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/double-conversion PARENT_SCOPE)

set_property (TARGET double-conversion cctest PROPERTY FOLDER "ext")

###########################################################################
# nanovdb

set (NANOVDB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/openvdb/nanovdb PARENT_SCOPE)

###########################################################################
# FLIP

set (FLIP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/flip PARENT_SCOPE)

add_library (flip_lib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/flip/flip.cpp)

set_property (TARGET flip_lib PROPERTY FOLDER "ext")

###########################################################################
# utf8proc

set (UTF8PROC_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/utf8proc PARENT_SCOPE)

add_subdirectory (utf8proc)

###########################################################################
# qoi

set (QOI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/qoi PARENT_SCOPE)

###########################################################################
# glfw / glad

set (GLFW_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/glfw/include PARENT_SCOPE)
set (GLAD_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include PARENT_SCOPE)

set (GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set (GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set (GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set (GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory (glfw)
add_subdirectory (glad)

set_property (TARGET glfw PROPERTY FOLDER "ext")
set_property (TARGET glad PROPERTY FOLDER "ext")

###########################################################################
# TBB
if (PBRT_WITH_PATH_GUIDING OR PBRT_WITH_OIDN)
  # allow to use a pre-build TBB
  find_package(TBB 2021.13)
  if (TBB_FOUND)
    set(TBB_ROOT ${TBB_ROOT} PARENT_SCOPE)
    set(TBB_DIR ${TBB_ROOT} PARENT_SCOPE)
    set(TBB_LIBRARIES tbb PARENT_SCOPE)
  else()
    message (STATUS "TBB not found; building it from scratch.")
include(ExternalProject)
    set(TBB_ARGS "")

    set(TBB_TEST OFF CACHE BOOL "" FORCE)
    set(TBB_STRICT OFF CACHE BOOL "" FORCE)

    set(TBB_ARGS ${TBB_ARGS}
      -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DTBB_TEST=OFF
      -DTBB_STRICT=OFF
    )

    if(APPLE)
      set(CMAKE_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
      set(CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
      set(TBB_ARGS ${TBB_ARGS}
          -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
          -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
      )
    endif()

    ExternalProject_Add(TBB
      PREFIX tbb
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tbb
      CMAKE_ARGS
        ${TBB_ARGS}
      BUILD_ALWAYS OFF
    )
    set(TBB_LIBRARIES tbb PARENT_SCOPE) # set when not found..
  endif()
endif()

###########################################################################
# Open Image Denoise
set(WITH_METAL_DEVICE OFF CACHE BOOL "" FORCE)
set(WITH_CUDA_DEVICE OFF CACHE BOOL "" FORCE)
set(WITH_RT_FILTERS OFF CACHE BOOL "" FORCE)

if (PBRT_WITH_OIDN)
include(ExternalProject)
  find_package (OpenImageDenoise 2.3.0)

  if (OPENIMAGEDENOISE_FOUND)
    set(OIDN_FOUND 1 PARENT_SCOPE)
    set(OIDN_LIBRARIES ${OPENIMAGEDENOISE_LIBRARIES} PARENT_SCOPE)
    set(OIDN_INCLUDE_DIR ${OPENIMAGEDENOISE_INCLUDE_DIRS} PARENT_SCOPE)
  else ()
    message (STATUS "Open Image Denoise not found; building it from scratch.")
    message(STATUS "ISPC_EXECUTABLE = ${ISPC_EXECUTABLE}")
    
    if(NOT ISPC_EXECUTABLE)
      set(GET_ISPC ON)
      include(get_ispc)
    endif()
    set(OIDN_ARGS "")

    set(ISPC_EXECUTABLE ${ISPC_EXECUTABLE})
    set(OIDN_APPS OFF)

    message(STATUS "TBB_ROOT = ${TBB_ROOT}")
    message(STATUS "TBB_DIR = ${TBB_DIR}")

    set(TBB_SRC ${CMAKE_INSTALL_PREFIX})
    if(NOT "${TBB_ROOT}" STREQUAL "")
      set(TBB_SRC ${TBB_ROOT})
    endif()
    
    set(OIDN_ARGS ${OIDN_ARGS}
      -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DISPC_EXECUTABLE=${ISPC_EXECUTABLE}
      -DOIDN_APPS=OFF
      -DTBB_ROOT=${TBB_SRC}
      -DOIDN_FILTER_RT=${WITH_RT_FILTERS}
      -DOIDN_FILTER_RTLIGHTMAP=${WITH_RT_FILTERS}
    )
    
    if(APPLE AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
      set(OIDN_DEVICE_METAL ${WITH_METAL_DEVICE})
      set(OIDN_ARGS ${OIDN_ARGS}
        -DOIDN_DEVICE_METAL=${WITH_METAL_DEVICE}
      )
    endif()

    if(NOT APPLE AND NOT "${PBRT_OPTIX7_PATH}" STREQUAL "")
      if(NOT CUDAToolkit_FOUND)
        find_package (CUDAToolkit ${PBRT_CUDA_TOOL_KIT} REQUIRED)
      endif() 
      if(CUDAToolkit_FOUND AND WITH_CUDA_DEVICE)
        message(STATUS "Enabling CUDA Device for OIDN")
        set(OIDN_DEVICE_CUDA ON)
        set(OIDN_ARGS ${OIDN_ARGS} -DOIDN_DEVICE_CUDA=ON)
      else()
        message(STATUS "CUDA not found or WITH_CUDA_DEVICE is off; disabling Cuda device for OIDN.")
      endif()
    endif()

    ExternalProject_Add(OIDN
      PREFIX oidn
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/oidn
      BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/oidn
      CMAKE_ARGS
        ${OIDN_ARGS}
      BUILD_ALWAYS OFF
    )

    ExternalProject_Add_StepDependencies(OIDN
    configure
      TBB
      $<$<BOOL:${GET_ISPC}>:ispc>
    )
    
    set (OIDN_INCLUDE_DIR
      ${CMAKE_CURRENT_SOURCE_DIR}/oidn/include
      PARENT_SCOPE
    )

    set (OIDN_LIBRARIES
      openpgl
      ${TBB_LIBRARIES}
      PARENT_SCOPE
    )
  endif()

endif()

###########################################################################
# Open PGL
if (PBRT_WITH_PATH_GUIDING)
  if(LOCAL_BUILD AND MSVC)
    # for now, we use only with MSVC
    set(OPENPGL_LIBRARIES ${OPENPGL_LIBRARY} PARENT_SCOPE)
    set(OPENPGL_INCLUDE_DIR ${OPENPGL_INCLUDE} PARENT_SCOPE)

  else()
    find_package (openpgl CONFIG)

    if (openpgl_FOUND)
      message (STATUS "Found OpenPGL version ${OPENPGL_VERSION}")
      get_target_property(OPENPGL_LIBRARIES openpgl::openpgl LOCATION)
      get_target_property(OPENPGL_INCLUDE_DIR openpgl::openpgl INTERFACE_INCLUDE_DIRECTORIES)
      set(OPENPGL_LIBRARIES ${OPENPGL_LIBRARIES} tbb PARENT_SCOPE)
      set(OPENPGL_INCLUDE_DIR ${OPENPGL_INCLUDE_DIR} PARENT_SCOPE)
    else ()
      message (STATUS "OpenPGL not found; building it from scratch.")

      if(NOT "${TBB_ROOT}" STREQUAL "")
        set(TBB_SRC ${TBB_ROOT})
      endif()

      set(OPENPGL_ARGS ${OPENPGL_ARGS}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DOPENPGL_BUILD_STATIC=OFF
        -DOPENPGL_TBB_ROOT=${TBB_SRC}
        -DOPENPGL_EF_RADIANCE_CACHES=ON
        -DOPENPGL_DIRECTION_COMPRESSION=ON
        -DOPENPGL_RADIANCE_COMPRESSION=ON
        -DOPENPGL_EF_IMAGE_SPACE_GUIDING_BUFFER=${PBRT_WITH_OIDN}
        -DOPENPGL_EF_VSP_GUIDING=ON
      )

      ExternalProject_Add(OpenPGL
        PREFIX openpgl
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openpgl
        CMAKE_ARGS
          ${OPENPGL_ARGS}
        BUILD_ALWAYS ON
      )

      if (PBRT_WITH_OIDN)
        if (NOT OPENIMAGEDENOISE_FOUND)
          ExternalProject_Add_StepDependencies(OpenPGL configure TBB OIDN )
        else()
          ExternalProject_Add_StepDependencies(OpenPGL configure TBB )
        endif()
      else()
        ExternalProject_Add_StepDependencies(OpenPGL configure TBB )
      endif()

      #set(BUILD_STATIC ON CACHE BOOL "" FORCE)
      #add_subdirectory (openpgl/superbuild)
      #set_property (TARGET ep_openpgl PROPERTY FOLDER "ext/openpgl")
      set (OPENPGL_DEP True PARENT_SCOPE)
      set (OPENPGL_INCLUDE_DIR
        ${CMAKE_INSTALL_PREFIX}/include
        PARENT_SCOPE
      )

      set (OPENPGL_LIBRARIES
        openpgl
        ${TBB_LIBRARIES}
        PARENT_SCOPE
      )
    endif()
  endif()

endif()

if (PBRT_WITH_OPENPBR)
  set (GLM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glm PARENT_SCOPE)
  set (OPENPBR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openpbr PARENT_SCOPE)
endif ()

