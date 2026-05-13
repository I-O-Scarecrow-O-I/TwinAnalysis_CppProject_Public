# Deployment Guide (Windows & Linux)

This document describes how to set up a **reproducible**, **space-conscious**, and **feature-gated** build/runtime environment for `TwinAnalysis_CppProject` on both **Linux** and **Windows**.

The repository provides two runtime entrypoints:

- **CLI**: `AiModelPackagingCLI`  
  Packages model artifacts into a `Packaging_Result/` directory tree.
- **Microservice**: `PackagingMicroservice` (`src/microservice/PackagingService.cpp`)  
  HTTP API for packaging + optionally uploading a ZIP result and notifying downstream systems.

---

## Contents

- [1. Repository overview](#1-repository-overview)
- [2. Feature flags and dependency matrix](#2-feature-flags-and-dependency-matrix)
- [3. Recommended versions](#3-recommended-versions)
- [4. Linux deployment (Ubuntu)](#4-linux-deployment-ubuntu)
- [5. Windows deployment (VS2022 + vcpkg)](#5-windows-deployment-vs2022--vcpkg)
- [6. Running the CLI](#6-running-the-cli)
- [7. Running the microservice](#7-running-the-microservice)
- [8. Troubleshooting](#8-troubleshooting)
- [9. Clean uninstall / reclaim disk space](#9-clean-uninstall--reclaim-disk-space)

---

## 1. Repository overview

- Project root contains the main `CMakeLists.txt`.
- Output packages are written into `<out>/Packaging_Result/`.
- Example requests and outputs:
  - `requests/`
  - `packageResults_onnx/`, `packageResults_pt/`, `packageResults_ts/` (sample outputs)

---

## 2. Feature flags and dependency matrix

### 2.1 CMake options (build-time)

| Option | Default | Meaning |
|---|---:|---|
| `TWIN_ENABLE_ONNX` | `ON` | Enable ONNX parsing/runtime support |
| `TWIN_ENABLE_TORCH` | `ON` | Enable PyTorch workflow (uses Python for metadata extraction) |
| `TWIN_ENABLE_TF` | `ON` | Enable TensorFlow workflow (TF -> ONNX conversion; uses Python) |
| `TWIN_ENABLE_TESTS` | `OFF` | Enable optional generated wrapper tests |
| `TWIN_ENABLE_TEST_WRAPPER_ONNX` | `OFF` | Build ONNX generated-wrapper test |
| `TWIN_ENABLE_TEST_WRAPPER_TORCH` | `OFF` | Build PyTorch generated-wrapper test (requires LibTorch) |
| `BUILD_MICROSERVICE` | `OFF` | Build `PackagingMicroservice` |
| `TWIN_ENABLE_RESULT_UPLOAD` | `ON` (repo default) | Enable uploading ZIP result via HTTP upload API (microservice only) |

**Constraint:** at least one of `TWIN_ENABLE_ONNX` or `TWIN_ENABLE_TORCH` must be `ON`.

### 2.2 Environment variables (runtime)

| Variable | Required when | Purpose |
|---|---|---|
| `ONNXRUNTIME_ROOT` | `TWIN_ENABLE_ONNX=ON` | ONNX Runtime root directory (expects `include/` + `lib/` or `lib64/`) |
| `PT_PYTHON` | `TWIN_ENABLE_TORCH=ON` | Python executable with `torch` installed |
| `TF_PYTHON` | `TWIN_ENABLE_TF=ON` | Python executable with `tensorflow` + `tf2onnx` installed |
| `LIBTORCH_ROOT` | `TWIN_ENABLE_TEST_WRAPPER_TORCH=ON` | LibTorch root (or use `Torch_DIR`) |

---

## 3. Recommended versions

These versions are known-good recommendations for this repository. You can use newer versions, but keep the stack consistent.

### 3.1 C++ toolchain
- **CMake**: ≥ 3.28
- **C++**: C++17
- **Linux**: GCC 9+ or Clang 12+
- **Windows**: Visual Studio 2022 (MSVC v143)

### 3.2 ONNX Runtime
- **ONNX Runtime**: `1.23.2`
  - Linux: `onnxruntime-linux-x64-1.23.2`
  - Windows: `onnxruntime-win-x64-1.23.2`

### 3.3 Python (optional by workflow)
If you enable `TWIN_ENABLE_TORCH` and/or `TWIN_ENABLE_TF`, install Python environments:

- **Python**: 3.10 (recommended)
- **torch** (CPU): pick a CPU wheel compatible with your Python version
- **tensorflow-cpu**: TensorFlow 2.x compatible with your Python version
- **tf2onnx**: latest compatible with TF version

> Note: TF/PT are used for **metadata extraction / conversion**. Generated C++ runtime stubs may not implement inference for TF/PT; check `Warnings.json`.

### 3.4 Microservice system dependencies
- `minizip-ng` (for ZIP generation)
- `libcurl` (for multipart upload to upload API when `TWIN_ENABLE_RESULT_UPLOAD=ON`)

---

## 4. Linux deployment (Ubuntu)

### 4.1 Install build tools (minimal footprint)

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git pkg-config \
  python3 python3-venv \
  libcurl4-openssl-dev
```

> If you are not building the microservice or you disable upload, you may omit `libcurl4-openssl-dev`.

### 4.2 Get the repository

```bash
git clone https://github.com/I-O-Scarecrow-O-I/twin-analysis-CppProject_Public.git
cd TwinAnalysis_CppProject
```

### 4.3 Install ONNX Runtime (recommended: extract-only, no system install)

Download the `onnxruntime-linux-x64-1.23.2` release archive and extract it under a dedicated directory.

Example:

```bash
mkdir -p Code_Dependency
# Put onnxruntime-linux-x64-1.23.2.tgz into Code_Dependency manually or via wget/curl
tar -xzf Code_Dependency/onnxruntime-linux-x64-1.23.2.tgz -C Code_Dependency
export ONNXRUNTIME_ROOT="$PWD/Code_Dependency/onnxruntime-linux-x64-1.23.2"
```

> Keep `ONNXRUNTIME_ROOT` pointing to an **absolute path** to avoid confusion when the working directory changes.

### 4.4 (Optional) Python venv for Torch

Only required when `TWIN_ENABLE_TORCH=ON`.

```bash
python3 -m venv .venv_torch
source .venv_torch/bin/activate
pip install -U pip
pip install torch --index-url https://download.pytorch.org/whl/cpu
export PT_PYTHON="$PWD/.venv_torch/bin/python"
deactivate
```

### 4.5 (Optional) Python venv for TensorFlow + tf2onnx

Only required when `TWIN_ENABLE_TF=ON`.

```bash
python3 -m venv .venv_tf
source .venv_tf/bin/activate
pip install -U pip
pip install tensorflow-cpu tf2onnx onnx
export TF_PYTHON="$PWD/.venv_tf/bin/python"
deactivate
```

### 4.6 Install minizip-ng (required when `BUILD_MICROSERVICE=ON`)

You can build from source (your known command sequence), or use a package manager if available.
Your known build-from-source steps:

```bash
git clone https://github.com/zlib-ng/minizip-ng.git
cd minizip-ng
mkdir build && cd build
cmake .. \
    -DMZ_COMPAT=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local
make -j"$(nproc)"
sudo make install
```

Return to repo root after installing:

```bash
cd ../../TwinAnalysis_CppProject
```

### 4.7 Build (recommended: Ninja, out-of-source)

#### 4.7.1 Build the CLI only (ONNX-only example)

```bash
cmake -S . -B build-onnx -G Ninja \
  -DTWIN_ENABLE_ONNX=ON \
  -DTWIN_ENABLE_TORCH=OFF \
  -DTWIN_ENABLE_TF=OFF \
  -DBUILD_MICROSERVICE=OFF \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"

cmake --build build-onnx --target AiModelPackagingCLI
```

#### 4.7.2 Build microservice (requires minizip-ng + curl)

```bash
cmake -S . -B build-ms -G Ninja \
  -DTWIN_ENABLE_ONNX=ON \
  -DTWIN_ENABLE_TORCH=ON \
  -DTWIN_ENABLE_TF=ON \
  -DBUILD_MICROSERVICE=ON \
  -DTWIN_ENABLE_RESULT_UPLOAD=ON \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"

cmake --build build-ms --target PackagingMicroservice
```

---

## 5. Windows deployment (VS2022 + vcpkg)

### 5.1 Prerequisites
- Visual Studio 2022 (Desktop development with C++)
- CMake ≥ 3.28 (VS can provide it)
- Git for Windows
- (Recommended) Ninja

### 5.2 vcpkg install

Clone & bootstrap vcpkg (one-time):

```bat
cd /d O:\Code_dependency
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
```

Install dependencies (x64):

```bat
O:\Code_dependency\vcpkg\vcpkg.exe install curl:x64-windows
O:\Code_dependency\vcpkg\vcpkg.exe install minizip-ng:x64-windows
```

> If you prefer static CRT/links, use `x64-windows-static`, but ensure your whole project is consistent.

### 5.3 ONNX Runtime (Windows)
Download and extract:
- `onnxruntime-win-x64-1.23.2.zip`

Set:
```bat
set ONNXRUNTIME_ROOT=O:\Code_dependency\onnxruntime-win-x64-1.23.2
```

### 5.4 Configure and build with vcpkg toolchain (CMake CLI)

```bat
cmake -S . -B build-ms -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=O:\Code_dependency\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DTWIN_ENABLE_ONNX=ON ^
  -DTWIN_ENABLE_TORCH=ON ^
  -DTWIN_ENABLE_TF=ON ^
  -DBUILD_MICROSERVICE=ON ^
  -DTWIN_ENABLE_RESULT_UPLOAD=ON ^
  -DONNXRUNTIME_ROOT=%ONNXRUNTIME_ROOT%

cmake --build build-ms --target PackagingMicroservice
```

### 5.5 Visual Studio 2022 (CMake Project)
If you open the folder in Visual Studio:
- Ensure CMake is configured with the vcpkg toolchain file:
  - `CMAKE_TOOLCHAIN_FILE = O:/Code_dependency/vcpkg/scripts/buildsystems/vcpkg.cmake`

Then build targets:
- `AiModelPackagingCLI`
- `PackagingMicroservice`

---

## 6. Running the CLI

Example (Linux):

```bash
./build-onnx/AiModelPackagingCLI \
  --request requests/test-local-001.json \
  --model models/affine_savemodel.onnx \
  --out packageResults_onnx
```

Example (Windows PowerShell):

```powershell
.\build-onnx\AiModelPackagingCLI.exe `
  --request requests\test-local-001.json `
  --model models\affine_savemodel.onnx `
  --out packageResults_onnx
```

---

## 7. Running the microservice

### 7.1 Start server
Start `PackagingMicroservice` from the build output directory:

Linux:
```bash
./build-ms/PackagingMicroservice
```

Windows:
```bat
build-ms\PackagingMicroservice.exe
```

The microservice exposes:
- `POST /api/v1/packaging`

### 7.2 Upload + callback integration
The microservice can:
1) package and ZIP the runtime payload
2) upload the ZIP to Upload API (multipart/form-data, field `file`) when `TWIN_ENABLE_RESULT_UPLOAD=ON`
3) call wrapper-codegen callback with `artifactUri` pointing to the uploaded `downloadUrl`

> Upload and callback endpoints are currently injected at build time via CMake compile definitions.
> If you need to change endpoints without rebuilding, consider moving them into a runtime config file (future work).

### 7.3 Example request (Postman)
Your JSON request must include `taskId` (ADP expects it for tracking):

```json
{
  "taskId": "2051293181848457216",
  "block": { "blockName": "AffineTest" },
  "implementation": {
    "type": "ONNX",
    "fileUri": "http://<server>/models/affine_savemodel.onnx",
    "filename": "affine_savemodel.onnx",
    "contentSha256": ""
  },
  "generationOptions": { "className": "AffineTestWrapper" }
}
```

---


## 8. Clean uninstall / reclaim disk space

To reclaim space quickly:

- Remove build directories:
  ```bash
  rm -rf build-* 
  ```
- Remove Python venvs:
  ```bash
  rm -rf .venv_torch .venv_tf
  ```
- Remove extracted ONNX Runtime bundle:
  ```bash
  rm -rf Code_Dependency/onnxruntime-*
  ```

On Ubuntu, system packages can be removed (optional):
```bash
sudo apt-get remove --purge -y libcurl4-openssl-dev
sudo apt-get autoremove -y
```
