# Offline Docker Deployment Config

## Image
- Name: twin/packaging:1.0.0
- OS: ubuntu:22.04
- Architecture: x86_64

## Services
- PackagingMicroservice:
  - Port: 8080 (container)
  - Host binding: 0.0.0.0 (recommended)
  - API:
    - POST /api/v1/packaging

## Bundled runtimes
- ONNX Runtime:
  - Path: /opt/onnxruntime
<!-- - PyTorch python env:
  - PT_PYTHON: /opt/venv_torch/bin/python
- TensorFlow python env:
  - TF_PYTHON: /opt/venv_tf/bin/python -->

## Data directory
- Container work dir: /opt/twin/work
- Recommended mount: -v $(pwd)/twin_work:/opt/twin/work

## Integration (upload + callback)
Your microservice may call:
- Upload API:  http://10.95.210.240:8080/api/v1/files/upload
- Callback:   http://10.95.210.240:33000/internal/module2/callbacks/wrapper-codegen

If you changed these endpoints in code via compile-time macros, rebuild the image.
