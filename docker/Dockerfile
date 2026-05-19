# docker/Dockerfile
# syntax=docker/dockerfile:1

############################
# Builder stage (Ubuntu 22.04)
############################
FROM ubuntu:22.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_BUILD_TYPE=Release

# Optional proxy args
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY
ENV http_proxy=${HTTP_PROXY}
ENV https_proxy=${HTTPS_PROXY}
ENV no_proxy=${NO_PROXY}

# System build tools + libcurl development package + python3 (for venv execution)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ninja-build \
    pkg-config \
    ca-certificates \
    libcurl4-openssl-dev \
    zlib1g-dev \
    libssl-dev \
    python3 \
    python3-venv \
 && rm -rf /var/lib/apt/lists/*

# 使用宿主机的时间
RUN ln -snf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime && echo Asia/Shanghai > /etc/timezone

# Install CMake from local script
COPY docker/third_party/cmake/cmake-install.sh /tmp/cmake-install.sh
RUN mkdir -p /opt/cmake \
 && sh /tmp/cmake-install.sh --skip-license --prefix=/opt/cmake \
 && rm /tmp/cmake-install.sh
ENV PATH="/opt/cmake/bin:${PATH}"

# Build and install minizip-ng from local source (disable PPMd to avoid git dependency)
COPY docker/third_party/minizip-ng /tmp/minizip-ng
RUN cmake -S /tmp/minizip-ng -B /tmp/minizip-ng/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DMZ_COMPAT=ON \
    -DMZ_ZIP=ON \
    -DMZ_UNZIP=ON \
    -DMZ_PPMD=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
 && cmake --build /tmp/minizip-ng/build \
 && cmake --install /tmp/minizip-ng/build \
 && rm -rf /tmp/minizip-ng

# ----------------------------
# 复制用户提供的虚拟环境 (PyTorch & TensorFlow)
# 注意：你需要提前将虚拟环境放在 docker/third_party/python_envs/ 下
# ----------------------------
COPY docker/third_party/python_envs/twin_pt310 /opt/python_envs/twin_pt310
COPY docker/third_party/python_envs/twin_tf310 /opt/python_envs/twin_tf310

# 设置正确的环境变量名（程序期望 PT_PYTHON 和 TF_PYTHON）
ENV PT_PYTHON=/opt/python_envs/twin_pt310/bin/python
ENV TF_PYTHON=/opt/python_envs/twin_tf310/bin/python

# Copy project source
WORKDIR /src
COPY . /src

# Verify ONNX Runtime bundle exists in build context
RUN test -d /src/docker/third_party/onnxruntime/include \
 && ( test -d /src/docker/third_party/onnxruntime/lib || test -d /src/docker/third_party/onnxruntime/lib64 )

# Configure & Build (Enable ONNX, Torch, TF, Microservice)
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DTWIN_ENABLE_ONNX=ON \
    -DTWIN_ENABLE_TORCH=ON \
    -DTWIN_ENABLE_TF=ON \
    -DBUILD_MICROSERVICE=ON \
    -DTWIN_ENABLE_RESULT_UPLOAD=ON \
    -DONNXRUNTIME_ROOT=/src/docker/third_party/onnxruntime \
    -DPROJECT_ROOT=/opt/twin/app \
 && cmake --build build --target AiModelPackagingCLI PackagingMicroservice

# Diagnostics
RUN echo "==== /src/build directory ====" && ls -lah /src/build
RUN echo "==== executables under /src/build ====" && find /src/build -maxdepth 4 -type f -executable -print


############################
# Runtime stage (Ubuntu 22.04)
############################
FROM ubuntu:22.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY
ENV http_proxy=${HTTP_PROXY}
ENV https_proxy=${HTTPS_PROXY}
ENV no_proxy=${NO_PROXY}

# Runtime system deps: libcurl4 + curl + zip + python3 (to run venv)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    curl \
    zip \
    unzip \
    python3 \
    python3-venv \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/twin/app
RUN mkdir -p /opt/twin/bin /opt/twin/app/models /opt/twin/app/requests /opt/twin/app/Upload_Result /opt/twin/work
COPY include /opt/twin/app/include
# Copy Python metadata extraction scripts
COPY tools /opt/twin/work/tools

# Copy compiled binaries
COPY --from=builder /src/build/AiModelPackagingCLI /opt/twin/bin/AiModelPackagingCLI
COPY --from=builder /src/build/PackagingMicroservice /opt/twin/bin/PackagingMicroservice

# Copy ONNX Runtime runtime libraries
COPY docker/third_party/onnxruntime /opt/onnxruntime
ENV ONNXRUNTIME_ROOT=/opt/onnxruntime
ENV LD_LIBRARY_PATH=/opt/onnxruntime/lib:/opt/onnxruntime/lib64:${LD_LIBRARY_PATH}

# 复制虚拟环境（从 builder 阶段）
COPY --from=builder /opt/python_envs /opt/python_envs

# 设置正确的环境变量（程序期望 PT_PYTHON 和 TF_PYTHON）
ENV PT_PYTHON=/opt/python_envs/twin_pt310/bin/python
ENV TF_PYTHON=/opt/python_envs/twin_tf310/bin/python

# Environment for microservice (CLI path and project root)
ENV PROJECT_ROOT=/opt/twin/app
ENV TWIN_PACKAGING_CLI_DOCKER_PATH=/opt/twin/bin/AiModelPackagingCLI

# Copy default configuration file (can be overridden by mount)
COPY config.json /opt/twin/app/config.json

EXPOSE 8080
CMD ["/opt/twin/bin/PackagingMicroservice"]