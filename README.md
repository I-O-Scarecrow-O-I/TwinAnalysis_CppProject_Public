# AI Model Packaging Service

`AiModelPackagingCLI` packages model artifacts into a standard `Packaging_Result/` tree.

Supported workflows:

- **ONNX**: parse + wrapper generation + inference-capable ONNX runtime wrapper
- **PyTorch (TorchScript)**: parse + wrapper generation (metadata via Python)
- **TensorFlow**: convert to ONNX first, then ONNX parse + wrapper generation (conversion/meta via Python)

A microservice entrypoint is also available at `src/microservice/PackagingService.cpp` (`PackagingMicroservice` target).

---

## Documentation

- **Deployment / Setup (Windows & Linux)**: see [`DeploymentGuide.md`](DeploymentGuide.md)
---

## Quick start

### Build CLI (Linux example, ONNX-only)

```bash
export ONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-1.23.2

cmake -S . -B build-onnx -G Ninja \
  -DTWIN_ENABLE_ONNX=ON \
  -DTWIN_ENABLE_TORCH=OFF \
  -DTWIN_ENABLE_TF=OFF \
  -DBUILD_MICROSERVICE=OFF \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"

cmake --build build-onnx --target AiModelPackagingCLI
```

Run:

```bash
./build-onnx/AiModelPackagingCLI \
  --request requests/test-local-001.json \
  --model models/affine_savemodel.onnx \
  --out packageResults_onnx
```

---

## Dependencies

### Required by feature

| Feature | Required dependencies |
|---|---|
| `TWIN_ENABLE_ONNX=ON` | ONNX Runtime (`ONNXRUNTIME_ROOT`) |
| `TWIN_ENABLE_TORCH=ON` | Python env with `torch` for metadata extraction (`PT_PYTHON`) |
| `TWIN_ENABLE_TF=ON` | Python env with `tensorflow`, `tf2onnx` (`TF_PYTHON`) + ONNX enabled (`onnx` is typically installed transitively with tf2onnx) |
| `TWIN_ENABLE_TEST_WRAPPER_TORCH=ON` | LibTorch (`Torch_DIR` or `LIBTORCH_ROOT`) |
| `BUILD_MICROSERVICE=ON` | `minizip-ng`, `cpp-httplib`, `picosha2` (+ `libcurl` when result upload is enabled) |

---

## CMake options

| Option | Default | Description |
|---|---:|---|
| `TWIN_ENABLE_ONNX` | `ON` | Enable ONNX parsing/runtime support |
| `TWIN_ENABLE_TORCH` | `ON` | Enable PyTorch packaging workflow |
| `TWIN_ENABLE_TF` | `ON` | Enable TensorFlow->ONNX workflow (requires ONNX) |
| `TWIN_ENABLE_TESTS` | `OFF` | Enable optional generated wrapper test targets |
| `TWIN_ENABLE_TEST_WRAPPER_ONNX` | `OFF` | Build ONNX generated-wrapper test (requires tests+ONNX) |
| `TWIN_ENABLE_TEST_WRAPPER_TORCH` | `OFF` | Build PyTorch generated-wrapper test (requires tests+torch) |
| `BUILD_MICROSERVICE` | `OFF` | Build PackagingMicroservice target |
| `TWIN_ENABLE_RESULT_UPLOAD` | `ON` | (Microservice) upload ZIP to upload API and use returned `downloadUrl` as `artifactUri` |

Constraint: at least one of `TWIN_ENABLE_ONNX` or `TWIN_ENABLE_TORCH` must be `ON`.

---

## Environment variables

| Variable | Purpose |
|---|---|
| `ONNXRUNTIME_ROOT` | ONNX Runtime root (expects include/ and lib or lib64/) |
| `PT_PYTHON` | Python executable with `torch` installed |
| `TF_PYTHON` | Python executable with `tensorflow` + `tf2onnx` installed |
| `LIBTORCH_ROOT` | Optional LibTorch root for generated PyTorch wrapper test builds |
| `Torch_DIR` | Optional CMake package dir for `find_package(Torch)` |

---

## Build (Linux)

### ONNX-only

```bash
cmake -S . -B build-onnx -G Ninja \
  -DTWIN_ENABLE_ONNX=ON \
  -DTWIN_ENABLE_TORCH=OFF \
  -DTWIN_ENABLE_TF=OFF \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime

cmake --build build-onnx --target AiModelPackagingCLI
```

### Torch-only

```bash
cmake -S . -B build-torch -G Ninja \
  -DTWIN_ENABLE_ONNX=OFF \
  -DTWIN_ENABLE_TORCH=ON \
  -DTWIN_ENABLE_TF=OFF

cmake --build build-torch --target AiModelPackagingCLI
```

### Enable generated-wrapper tests (optional)

```bash
cmake -S . -B build-tests -G Ninja \
  -DTWIN_ENABLE_ONNX=ON \
  -DTWIN_ENABLE_TORCH=ON \
  -DTWIN_ENABLE_TESTS=ON \
  -DTWIN_ENABLE_TEST_WRAPPER_ONNX=ON \
  -DTWIN_ENABLE_TEST_WRAPPER_TORCH=ON \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime \
  -DLIBTORCH_ROOT=/path/to/libtorch

cmake --build build-tests
```

---

## Build (Windows)

```bat
cmake -S . -B build -G Ninja ^
  -DTWIN_ENABLE_ONNX=ON ^
  -DTWIN_ENABLE_TORCH=ON ^
  -DTWIN_ENABLE_TF=ON ^
  -DONNXRUNTIME_ROOT=O:/path/to/onnxruntime

cmake --build build --target AiModelPackagingCLI
```

Optional microservice build:

```bat
cmake -S . -B build-ms -G Ninja -DBUILD_MICROSERVICE=ON
cmake --build build-ms --target PackagingMicroservice
```

---

## CLI usage

```bash
./AiModelPackagingCLI \
  --request requests/request_xxx.json \
  --model models/xxx \
  --out packageResults_xxx
```

Optional:

```bash
--input-shape 1,2,10000
```

Output layout remains:

```text
Packaging_Result/
├── Result_Metadata.json
├── Detected_Model_Meta.json
├── Mapping_Table.json
├── Unmatched_Items.json
├── Warnings.json
├── Compatibility_Checks.json
├── generated-src/
├── generated-docs/
└── raw-extract/
```
