cmake_minimum_required(VERSION 3.28)
project(TwinAnalysis LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Feature toggles
option(BUILD_MICROSERVICE "Build the packaging HTTP microservice" ON)
option(TWIN_ENABLE_ONNX "Enable ONNX-based parsing/runtime support" ON)
option(TWIN_ENABLE_TORCH "Enable PyTorch packaging workflow support" ON)
option(TWIN_ENABLE_TF "Enable TensorFlow->ONNX packaging workflow support" ON)
option(TWIN_ENABLE_RESULT_UPLOAD "Enable uploading result zip via HTTP endpoint" ON)
option(TWIN_ENABLE_TESTS "Enable optional generated wrapper test targets" OFF)
option(TWIN_ENABLE_TEST_WRAPPER_ONNX "Build generated ONNX wrapper test target" OFF)
option(TWIN_ENABLE_TEST_WRAPPER_TORCH "Build generated PyTorch wrapper test target" OFF)

if(NOT TWIN_ENABLE_ONNX AND NOT TWIN_ENABLE_TORCH)
    message(FATAL_ERROR "At least one runtime must be enabled: TWIN_ENABLE_ONNX=ON or TWIN_ENABLE_TORCH=ON")
endif()

if(TWIN_ENABLE_TF AND NOT TWIN_ENABLE_ONNX)
    message(FATAL_ERROR "TWIN_ENABLE_TF requires TWIN_ENABLE_ONNX because TF workflow converts to ONNX and uses ONNX parsing/runtime")
endif()

if(TWIN_ENABLE_TEST_WRAPPER_ONNX AND NOT TWIN_ENABLE_TESTS)
    message(FATAL_ERROR "TWIN_ENABLE_TEST_WRAPPER_ONNX requires TWIN_ENABLE_TESTS=ON")
endif()
if(TWIN_ENABLE_TEST_WRAPPER_TORCH AND NOT TWIN_ENABLE_TESTS)
    message(FATAL_ERROR "TWIN_ENABLE_TEST_WRAPPER_TORCH requires TWIN_ENABLE_TESTS=ON")
endif()

# include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# ONNX Runtime discovery
if(TWIN_ENABLE_ONNX)
    if(NOT DEFINED ONNXRUNTIME_ROOT OR ONNXRUNTIME_ROOT STREQUAL "")
        if(DEFINED ENV{ONNXRUNTIME_ROOT} AND NOT "$ENV{ONNXRUNTIME_ROOT}" STREQUAL "")
            set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}" CACHE PATH "ONNX Runtime root directory")
        endif()
    endif()

    if(NOT DEFINED ONNXRUNTIME_ROOT OR ONNXRUNTIME_ROOT STREQUAL "")
        message(FATAL_ERROR "TWIN_ENABLE_ONNX=ON but ONNXRUNTIME_ROOT is not set. Provide -DONNXRUNTIME_ROOT=<path> or set ONNXRUNTIME_ROOT env var.")
    endif()

    # Some ORT bundles expose headers as include/onnxruntime_cxx_api.h, others as
    # include/onnxruntime/core/session/onnxruntime_cxx_api.h. Check both layouts.
    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h onnxruntime/core/session/onnxruntime_cxx_api.h
        PATHS
            "${ONNXRUNTIME_ROOT}/include"
            "${ONNXRUNTIME_ROOT}"
        NO_DEFAULT_PATH)

    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        PATHS
            "${ONNXRUNTIME_ROOT}/lib"
            "${ONNXRUNTIME_ROOT}/lib64"
            "${ONNXRUNTIME_ROOT}/bin"
        NO_DEFAULT_PATH)

    if(WIN32)
        find_file(ONNXRUNTIME_DLL
            NAMES onnxruntime.dll
            PATHS
                "${ONNXRUNTIME_ROOT}/lib"
                "${ONNXRUNTIME_ROOT}/bin"
            NO_DEFAULT_PATH)
    endif()

    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR
            "ONNX Runtime not found under ONNXRUNTIME_ROOT='${ONNXRUNTIME_ROOT}'.\n"
            "Expected include path containing onnxruntime_cxx_api.h and a library in lib/ or lib64/.")
    endif()

    add_library(onnxruntime UNKNOWN IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}")
    target_include_directories(onnxruntime INTERFACE "${ONNXRUNTIME_INCLUDE_DIR}")

    message(STATUS "[ONNX] ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}")
    message(STATUS "[ONNX] include=${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "[ONNX] library=${ONNXRUNTIME_LIBRARY}")
endif()

function(twin_apply_feature_definitions target_name)
    if(TWIN_ENABLE_ONNX)
        target_compile_definitions(${target_name} PRIVATE TWIN_ENABLE_ONNX=1)
    else()
        target_compile_definitions(${target_name} PRIVATE TWIN_ENABLE_ONNX=0)
    endif()

    if(TWIN_ENABLE_TORCH)
        target_compile_definitions(${target_name} PRIVATE TWIN_ENABLE_TORCH=1)
    else()
        target_compile_definitions(${target_name} PRIVATE TWIN_ENABLE_TORCH=0)
    endif()

    if(TWIN_ENABLE_TF)
        target_compile_definitions(${target_name} PRIVATE TWIN_ENABLE_TF=1)
    else()
        target_compile_definitions(${target_name} PRIVATE TWIN_ENABLE_TF=0)
    endif()
endfunction()

# Main executable (ONNX demo app)
if(TWIN_ENABLE_ONNX)
    add_executable(${PROJECT_NAME}
        src/main.cpp
        include/AI_Adapter.h
        include/IModelBlock.h
        include/ModelParser.h
        src/ModelParser.cpp
        src/AI_Adapter.cpp)
    target_link_libraries(${PROJECT_NAME} PRIVATE onnxruntime)
    twin_apply_feature_definitions(${PROJECT_NAME})
endif()

# Packaging CLI
set(TWIN_PACKAGING_CLI_SOURCES
    src/packaging_cli.cpp
    src/packaging/RequestLoader.cpp
    src/packaging/DetectedModelMetaBuilder.cpp
    src/packaging/BindingChecker.cpp
    src/packaging/WrapperGenerator.cpp
    src/packaging/PackagingResultWriter.cpp)

if(TWIN_ENABLE_ONNX)
    list(APPEND TWIN_PACKAGING_CLI_SOURCES src/ModelParser.cpp)
endif()

add_executable(AiModelPackagingCLI
    ${TWIN_PACKAGING_CLI_SOURCES}
    include/packaging/PackagingTypes.h
    include/packaging/JsonHelpers.h
    include/packaging/RequestLoader.h
    include/packaging/DetectedModelMetaBuilder.h
    include/packaging/BindingChecker.h
    include/packaging/WrapperGenerator.h
    include/packaging/PackagingResultWriter.h
    include/utils/ProcessRunner.h)

target_include_directories(AiModelPackagingCLI PRIVATE ${CMAKE_SOURCE_DIR}/include)
twin_apply_feature_definitions(AiModelPackagingCLI)

if(TWIN_ENABLE_ONNX)
    target_link_libraries(AiModelPackagingCLI PRIVATE onnxruntime)
endif()

# Optional generated wrapper tests
if(TWIN_ENABLE_TESTS)
    if(TWIN_ENABLE_TEST_WRAPPER_ONNX)
        if(NOT TWIN_ENABLE_ONNX)
            message(FATAL_ERROR "TWIN_ENABLE_TEST_WRAPPER_ONNX requires TWIN_ENABLE_ONNX=ON")
        endif()

        set(ONNX_OUT_DIR "packageResults_onnx" CACHE PATH "Output directory that contains Packaging_Result for ONNX")
        set(ONNX_WRAPPER_CLASS "AddModelWrapper" CACHE STRING "Generated ONNX wrapper class name (without extension)")

        set(_ONNX_SRC_DIR  "${CMAKE_SOURCE_DIR}/${ONNX_OUT_DIR}/Packaging_Result/generated-src")
        set(_ONNX_INC      "${_ONNX_SRC_DIR}/include")
        set(_ONNX_CPP      "${_ONNX_SRC_DIR}/src/${ONNX_WRAPPER_CLASS}.cpp")
        set(_ONNX_RT_CPP   "${_ONNX_SRC_DIR}/src/OnnxModelRuntime.cpp")
        set(_ONNX_TEST_CPP "${CMAKE_SOURCE_DIR}/src/generated_wrapper_test.cpp")

        if(EXISTS "${_ONNX_INC}" AND EXISTS "${_ONNX_CPP}" AND EXISTS "${_ONNX_RT_CPP}" AND EXISTS "${_ONNX_TEST_CPP}")
            add_executable(GeneratedWrapperTest
                "${_ONNX_TEST_CPP}"
                "${_ONNX_CPP}"
                "${_ONNX_RT_CPP}")
            target_include_directories(GeneratedWrapperTest PRIVATE
                ${CMAKE_SOURCE_DIR}/include
                "${_ONNX_INC}")
            target_compile_definitions(GeneratedWrapperTest PRIVATE
                "WRAPPER_HEADER=\"${ONNX_WRAPPER_CLASS}.h\""
                "WRAPPER_TYPE=twin::generated::${ONNX_WRAPPER_CLASS}"
                "PACKAGING_OUT_DIR_DEFAULT=\"${ONNX_OUT_DIR}\"")
            target_link_libraries(GeneratedWrapperTest PRIVATE onnxruntime)
            twin_apply_feature_definitions(GeneratedWrapperTest)
        else()
            message(WARNING "[ONNX WrapperTest] Skipped: missing generated files. Run AiModelPackagingCLI with --out ${ONNX_OUT_DIR} first.")
        endif()
    endif()

    if(TWIN_ENABLE_TEST_WRAPPER_TORCH)
        if(NOT TWIN_ENABLE_TORCH)
            message(FATAL_ERROR "TWIN_ENABLE_TEST_WRAPPER_TORCH requires TWIN_ENABLE_TORCH=ON")
        endif()

        if(NOT DEFINED LIBTORCH_ROOT OR LIBTORCH_ROOT STREQUAL "")
            if(DEFINED ENV{LIBTORCH_ROOT} AND NOT "$ENV{LIBTORCH_ROOT}" STREQUAL "")
                set(LIBTORCH_ROOT "$ENV{LIBTORCH_ROOT}" CACHE PATH "LibTorch root directory")
            endif()
        endif()

        if(DEFINED LIBTORCH_ROOT AND NOT LIBTORCH_ROOT STREQUAL "")
            list(APPEND CMAKE_PREFIX_PATH "${LIBTORCH_ROOT}")
        endif()

        find_package(Torch REQUIRED)

        set(PYTORCH_OUT_DIR "packageResults_pt" CACHE PATH "Output directory that contains Packaging_Result for PyTorch")
        set(PYTORCH_WRAPPER_CLASS "PyTorchAntiJammingWrapper" CACHE STRING "Generated PyTorch wrapper class name (without extension)")

        set(_PT_SRC_DIR  "${CMAKE_SOURCE_DIR}/${PYTORCH_OUT_DIR}/Packaging_Result/generated-src")
        set(_PT_INC      "${_PT_SRC_DIR}/include")
        set(_PT_CPP      "${_PT_SRC_DIR}/src/${PYTORCH_WRAPPER_CLASS}.cpp")
        set(_PT_RT_CPP   "${_PT_SRC_DIR}/src/TorchModelRuntime.cpp")
        set(_PT_TEST_CPP "${CMAKE_SOURCE_DIR}/src/generated_pytorch_wrapper_test.cpp")

        if(EXISTS "${_PT_INC}" AND EXISTS "${_PT_CPP}" AND EXISTS "${_PT_RT_CPP}" AND EXISTS "${_PT_TEST_CPP}")
            add_executable(GeneratedPyTorchWrapperTest
                "${_PT_TEST_CPP}"
                "${_PT_CPP}"
                "${_PT_RT_CPP}")
            target_include_directories(GeneratedPyTorchWrapperTest PRIVATE
                ${CMAKE_SOURCE_DIR}/include
                "${_PT_INC}"
                ${TORCH_INCLUDE_DIRS})
            target_compile_definitions(GeneratedPyTorchWrapperTest PRIVATE
                "WRAPPER_HEADER=\"${PYTORCH_WRAPPER_CLASS}.h\""
                "WRAPPER_TYPE=twin::generated::${PYTORCH_WRAPPER_CLASS}"
                "PACKAGING_OUT_DIR_DEFAULT=\"${PYTORCH_OUT_DIR}\"")
            target_link_libraries(GeneratedPyTorchWrapperTest PRIVATE ${TORCH_LIBRARIES})
            twin_apply_feature_definitions(GeneratedPyTorchWrapperTest)
        else()
            message(WARNING "[PyTorch WrapperTest] Skipped: missing generated files. Run AiModelPackagingCLI with --out ${PYTORCH_OUT_DIR} first.")
        endif()
    endif()
endif()

# Runtime DLL copy (Windows)
if(WIN32 AND TWIN_ENABLE_ONNX AND DEFINED ONNXRUNTIME_DLL AND EXISTS "${ONNXRUNTIME_DLL}")
    foreach(tgt ${PROJECT_NAME} AiModelPackagingCLI GeneratedWrapperTest)
        if(TARGET ${tgt})
            add_custom_command(TARGET ${tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ONNXRUNTIME_DLL}"
                    $<TARGET_FILE_DIR:${tgt}>)
        endif()
    endforeach()
endif()

if(WIN32 AND TARGET GeneratedPyTorchWrapperTest AND DEFINED LIBTORCH_ROOT AND EXISTS "${LIBTORCH_ROOT}/lib")
    file(GLOB LIBTORCH_DLLS "${LIBTORCH_ROOT}/lib/*.dll")
    foreach(DLL_FILE ${LIBTORCH_DLLS})
        add_custom_command(TARGET GeneratedPyTorchWrapperTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                $<TARGET_FILE_DIR:GeneratedPyTorchWrapperTest>)
    endforeach()
endif()

# Microservice target (runtime entrypoint)
if(BUILD_MICROSERVICE)
    # --------------------------
    # Microservice dependencies
    # --------------------------
    set(HTTPLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/microservice/cpp-httplib")
    if(NOT EXISTS "${HTTPLIB_INCLUDE_DIR}/httplib.h")
        message(FATAL_ERROR "httplib.h not found: ${HTTPLIB_INCLUDE_DIR}/httplib.h")
    endif()

    set(PICOSHA2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/microservice/picosha2")
    if(NOT EXISTS "${PICOSHA2_INCLUDE_DIR}/picosha2.h")
        message(FATAL_ERROR "picosha2.h not found: ${PICOSHA2_INCLUDE_DIR}/picosha2.h")
    endif()

    # --------------------------
    # minizip discovery (required for microservice ZIP)
    # --------------------------
    if(NOT DEFINED MINIZIP_TARGET)
        find_package(minizip-ng CONFIG QUIET)
        if(TARGET MINIZIP::minizip-ng)
            set(MINIZIP_TARGET MINIZIP::minizip-ng)
        endif()
    endif()

    if(NOT DEFINED MINIZIP_TARGET)
        find_package(minizip CONFIG QUIET)
        if(TARGET MINIZIP::minizip)
            set(MINIZIP_TARGET MINIZIP::minizip)
        endif()
    endif()

    if(NOT MINIZIP_TARGET)
        message(FATAL_ERROR "BUILD_MICROSERVICE=ON requires minizip-ng/minizip. Install it (vcpkg/system) or configure with -DBUILD_MICROSERVICE=OFF.")
    endif()

    # --------------------------
    # Microservice runtime config (cache variables)
    # --------------------------
    set(MICROSERVICE_HOST "0.0.0.0" CACHE STRING "Microservice listen host")
    set(MICROSERVICE_PORT "8080" CACHE STRING "Microservice listen port")

    if(WIN32)
        set(_default_cli_name "AiModelPackagingCLI.exe")
    else()
        set(_default_cli_name "AiModelPackagingCLI")
    endif()
    set(MICROSERVICE_CLI "${_default_cli_name}" CACHE STRING "CLI executable name or path")

    # --------------------------
    # ADP endpoints (confirmed by user)
    # --------------------------
    # 1) Upload API: http://10.95.210.240:8080/api/v1/files/upload
    set(ADP_UPLOAD_BASE_URL "http://10.95.210.240:8080" CACHE STRING "ADP upload API base URL")
    set(ADP_UPLOAD_PATH "/api/v1/files/upload" CACHE STRING "ADP upload API path")

    # 2) Callback API: http://10.95.210.240:33000/internal/module2/callbacks/wrapper-codegen
    set(ADP_CALLBACK_BASE_URL "http://10.95.210.240:33000" CACHE STRING "ADP callback base URL")
    set(ADP_CALLBACK_PATH "/internal/module2/callbacks/wrapper-codegen" CACHE STRING "ADP callback path")

    # --------------------------
    # Interface libraries for headers
    # --------------------------
    add_library(httplib INTERFACE)
    target_include_directories(httplib INTERFACE ${HTTPLIB_INCLUDE_DIR})

    add_library(picosha2 INTERFACE)
    target_include_directories(picosha2 INTERFACE ${PICOSHA2_INCLUDE_DIR})

    # --------------------------
    # Microservice executable
    # --------------------------
    add_executable(PackagingMicroservice
        src/microservice/PackagingService.cpp)

    target_include_directories(PackagingMicroservice PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${HTTPLIB_INCLUDE_DIR}
        ${PICOSHA2_INCLUDE_DIR})

    target_link_libraries(PackagingMicroservice PRIVATE
        httplib
        picosha2
        ${MINIZIP_TARGET})

    # --------------------------
    # libcurl for multipart upload (/api/v1/files/upload)
    # --------------------------
    if(TWIN_ENABLE_RESULT_UPLOAD)
        find_package(CURL QUIET)
        if(NOT CURL_FOUND)
            message(FATAL_ERROR "TWIN_ENABLE_RESULT_UPLOAD=ON requires libcurl. Install curl via vcpkg and configure CMAKE_TOOLCHAIN_FILE.")
        endif()
        message(STATUS "[Upload] CURL found: ${CURL_VERSION_STRING}")
        target_link_libraries(PackagingMicroservice PRIVATE CURL::libcurl)
        target_sources(PackagingMicroservice PRIVATE src/microservice/ResultUploader.cpp)
        target_compile_definitions(PackagingMicroservice PRIVATE TWIN_ENABLE_RESULT_UPLOAD=1)
    else()
        target_sources(PackagingMicroservice PRIVATE src/microservice/ResultUploader_stub.cpp) # 里面返回 ""
        target_compile_definitions(PackagingMicroservice PRIVATE TWIN_ENABLE_RESULT_UPLOAD=0)
    endif()

    # --------------------------
    # Compile-time config injection (no runtime args/env needed)
    # --------------------------
    target_compile_definitions(PackagingMicroservice PRIVATE
        PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
        MICROSERVICE_CLI="${MICROSERVICE_CLI}"
        MICROSERVICE_HOST="${MICROSERVICE_HOST}"
        MICROSERVICE_PORT="${MICROSERVICE_PORT}"
        TWIN_DEFAULT_UPLOAD_DIR_NAME="Upload_Result"
        TWIN_PACKAGING_CLI_PATH="$<TARGET_FILE:AiModelPackagingCLI>"

        TWIN_ADP_UPLOAD_BASE_URL="${ADP_UPLOAD_BASE_URL}"
        TWIN_ADP_UPLOAD_PATH="${ADP_UPLOAD_PATH}"

        TWIN_ADP_CALLBACK_BASE_URL="${ADP_CALLBACK_BASE_URL}"
        TWIN_ADP_CALLBACK_PATH="${ADP_CALLBACK_PATH}"

        TWIN_ENABLE_ADP_CALLBACK=1
    )

    twin_apply_feature_definitions(PackagingMicroservice)

    if(WIN32)
        target_link_libraries(PackagingMicroservice PRIVATE ws2_32 crypt32)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(PackagingMicroservice PRIVATE Threads::Threads)
    endif()
endif()
