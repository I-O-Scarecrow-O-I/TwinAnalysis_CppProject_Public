## 1. 文档目的
本文档定义 `twin-analysis-framework` 对下游模型封装 RPC 服务返回内容的统一要求，适用于：

+ `FmuPackagingService`（陈负责）
+ `AiModelPackagingService`（闵负责）

目标是统一下游服务的返回交付格式，确保本系统能够稳定完成：

+ 保存原始 RPC 结果
+ 解压和归档返回产物
+ 读取映射关系和告警信息
+ 提取生成的 C++ 包装类源码
+ 将包装结果继续集成到后续 C++ 项目框架

## 2. 返回形式
下游 RPC 服务必须采用“对象存储交付 + RPC 返回清单”的方式返回结果。

下游服务必须先生成一个标准结果包，并上传到 MinIO。

要求如下：

+ 返回包文件名建议：`<BlockName>_packaging_result.zip`
+ ZIP 内根目录固定为：`Packaging_Result/`
+ 所有 JSON 文件统一使用 `UTF-8`
+ 所有路径和文件名区分大小写

RPC 响应体不直接传输 ZIP 二进制流，而是只返回轻量结果清单。

RPC 响应体至少应返回：

+ `resultPackageUri`：结果包在对象存储中的访问地址
+ `resultPackageSha256`：结果包摘要，便于本系统校验
+ `storageType`：存储类型，建议固定为 `MINIO`

如需增强调试与按需读取能力，可额外返回细粒度 artifact 地址，但不替代标准结果包。

## 3. 标准目录结构
```latex
Packaging_Result/
├── Result_Metadata.json
├── Detected_Model_Meta.json
├── Mapping_Table.json
├── Unmatched_Items.json
├── Warnings.json
├── Compatibility_Checks.json
├── generated-src/
│   ├── include/
│   │   └── XxxWrapper.h
│   ├── src/
│   │   └── XxxWrapper.cpp
│   └── CMakeLists.txt
├── generated-docs/
│   ├── Interface_Summary.md
│   └── Binding_Report.md
└── raw-extract/
    ├── framework_parse_log.txt
    ├── modelDescription.xml
    └── onnx_io_dump.json
```

说明：

+ `generated-docs/` 为推荐项，可选
+ `raw-extract/` 为推荐项，可选，但建议保留
+ `modelDescription.xml` 仅适用于 FMU
+ `onnx_io_dump.json` 仅适用于 AI 模型

## 4. 必需文件与可选文件
### 4.1 必需文件
以下文件为通用强制要求：

+ `Packaging_Result/Result_Metadata.json`
+ `Packaging_Result/Detected_Model_Meta.json`
+ `Packaging_Result/Mapping_Table.json`
+ `Packaging_Result/Unmatched_Items.json`
+ `Packaging_Result/Warnings.json`
+ `Packaging_Result/Compatibility_Checks.json`
+ `Packaging_Result/generated-src/include/*.h`
+ `Packaging_Result/generated-src/src/*.cpp`

如果未生成可用包装类源码，则仍必须返回：

+ 上述所有 JSON 文件
+ `Result_Metadata.json.status = "FAILED"`

此时 `generated-src/` 可以为空目录，或仅包含失败说明文件。

### 4.2 可选文件
以下文件为推荐提供：

+ `Packaging_Result/generated-src/CMakeLists.txt`
+ `Packaging_Result/generated-docs/Interface_Summary.md`
+ `Packaging_Result/generated-docs/Binding_Report.md`
+ `Packaging_Result/raw-extract/framework_parse_log.txt`
+ `Packaging_Result/raw-extract/modelDescription.xml`
+ `Packaging_Result/raw-extract/onnx_io_dump.json`

## 5. 通用返回内容定义
说明：

+ 本节定义的是下游服务必须产出的标准结果包逻辑内容
+ 这些文件实际通过对象存储交付，不直接在 RPC 响应体中内联返回
+ 本系统以 `resultPackageUri` 指向的标准结果包作为最终接收对象

### 5.1 `Result_Metadata.json`
用于描述本次封装任务的总体状态，是本系统读取返回包时的首要入口文件。

示例：

```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-20260303-0001",
  "service": "FmuPackagingService",
  "status": "SUCCEEDED",
  "message": "FMU 解析完成，已生成包装类",
  "blockKey": "BlockDefinition:AircraftSysMLModel::20_Structure_BDD::TurbofanEngine",
  "blockName": "TurbofanEngine",
  "modelType": "FMU",
  "wrapperClassName": "twin.generated::TurbofanEngineWrapper",
  "generatedAt": "2026-03-03T16:00:00+08:00"
}
```

字段要求：

+ `protocolVersion`：协议版本
+ `taskId`：必须与请求中的 `taskId` 一致
+ `service`：服务名，取值为 `FmuPackagingService` 或 `AiModelPackagingService`
+ `status`：`SUCCEEDED`、`PARTIAL_SUCCESS`、`FAILED`
+ `message`：简要说明
+ `blockKey`：必须与请求中的 `block.blockKey` 一致
+ `blockName`：模块短名称
+ `modelType`：`FMU`、`ONNX`、`TensorFlow`、`PyTorch`
+ `wrapperClassName`：生成的包装类全限定名；失败时可为 `null`
+ `generatedAt`：生成时间

### 5.2 `Detected_Model_Meta.json`
用于描述从底层模型文件中实际解析出的元数据，是“真实模型接口”的权威描述。

该文件必须是结构化 JSON，不允许仅将关键信息写在日志中。

### 5.3 `Mapping_Table.json`
用于返回成功匹配的 SysML 接口与底层模型变量/张量之间的映射关系。

该文件是后续自动集成、运行时绑定和人工排查的核心依据之一。

### 5.4 `Unmatched_Items.json`
用于返回未成功匹配的项目。

包括但不限于：

+ SysML 字段未匹配到底层模型变量
+ 甲方 `PortMapping` 指向的目标不存在
+ 模型存在额外变量/张量但未被使用

### 5.5 `Warnings.json`
用于返回所有非致命问题。

包括但不限于：

+ 类型不一致
+ 单位不一致
+ 方向不一致
+ 维度不一致
+ 数值精度降级

### 5.6 `Compatibility_Checks.json`
用于返回逐项适配性检查结果。

该文件是：

+ 闵的 AI 模型服务的核心输出之一
+ 陈的 FMU 服务的推荐输出

说明：

+ FMU 场景通常不按张量模型处理，`shape` 相关字段可为 `null`
+ AI 场景必须返回 `shape` 和 `precision` 相关检查结果

### 5.7 `generated-src/`
用于存放生成的 C++ 包装类源码。

目录要求：

+ 头文件放在 `generated-src/include/`
+ 源文件放在 `generated-src/src/`

最少要求：

+ 至少 1 个 `.h` 文件
+ 至少 1 个 `.cpp` 文件

建议：

+ 文件名与 `generationOptions.className` 保持一致
+ 如果生成多个辅助文件，也统一放在上述目录中

### 5.8 RPC 返回清单字段
RPC 响应体应返回一个轻量结果清单，用于告知本系统从对象存储读取标准结果包。

最少要求：

+ `storageType`
+ `resultPackageUri`
+ `resultPackageSha256`

推荐补充：

+ `metadataUri`
+ `mappingTableUri`
+ `warningsUri`
+ `compatibilityChecksUri`
+ `generatedSrcDirUri`

说明：

+ `resultPackageUri` 是主入口，本系统默认以它下载完整结果包并执行校验
+ 细粒度 URI 仅用于调试、定位或按需读取
+ 无论是否返回细粒度 URI，下游都必须产出完整的标准结果包

## 6. JSON 文件的最小字段要求
### 6.1 `Mapping_Table.json`
建议结构为数组。

示例：

```json
[
  {
    "sysmlPath": "p_fuel.massFlowRate",
    "targetName": "fmu_in_eng_fuel_flow",
    "targetKind": "scalar",
    "targetRole": "input",
    "targetIndex": null,
    "matchStatus": "MATCHED"
  }
]
```

最小字段：

+ `sysmlPath`
+ `targetName`
+ `targetKind`
+ `targetRole`
+ `targetIndex`
+ `matchStatus`

### 6.2 `Unmatched_Items.json`
建议结构为数组。

示例：

```json
[
  {
    "kind": "SYSML_FIELD",
    "sysmlPath": "p_sensor.pressure",
    "reason": "No matching target variable found"
  }
]
```

最小字段：

+ `kind`
+ `sysmlPath`
+ `reason`

### 6.3 `Warnings.json`
建议结构为数组。

示例：

```json
[
  {
    "code": "TYPE_MISMATCH",
    "severity": "WARN",
    "path": "p_cmdOut.pitch",
    "message": "期望 double，实际为 float32"
  }
]
```

最小字段：

+ `code`
+ `severity`
+ `path`
+ `message`

### 6.4 `Compatibility_Checks.json`
建议结构为数组。

示例：

```json
[
  {
    "sysmlPath": "p_cmdOut.pitch",
    "targetName": "action_vector",
    "targetIndex": [0],
    "expectedDirection": "output",
    "actualDirection": "output",
    "expectedType": "double",
    "actualType": "float32",
    "expectedShape": [1],
    "actualShape": [1, 3],
    "expectedPrecision": "float64",
    "actualPrecision": "float32",
    "directionResult": "PASS",
    "typeResult": "WARN",
    "shapeResult": "WARN",
    "precisionResult": "WARN",
    "messages": [
      "期望 double，实际 float32",
      "期望 shape [1]，实际 shape [1,3]"
    ]
  }
]
```

最小字段：

+ `sysmlPath`
+ `targetName`
+ `targetIndex`
+ `expectedDirection`
+ `actualDirection`
+ `expectedType`
+ `actualType`
+ `expectedShape`
+ `actualShape`
+ `expectedPrecision`
+ `actualPrecision`
+ `directionResult`
+ `typeResult`
+ `shapeResult`
+ `precisionResult`
+ `messages`

## 7. 陈（FMU）返回内容要求
陈的服务返回内容必须以 FMU 变量模型为中心。

### 7.1 `Detected_Model_Meta.json` 要求
建议结构：

```json
{
  "fmiVersion": "2.0",
  "modelName": "CFM56_Turbofan",
  "guid": "fmu-guid-xxxx",
  "variables": [
    {
      "name": "fmu_in_eng_fuel_flow",
      "valueReference": 1001,
      "dataType": "Real",
      "unit": "kg/s",
      "causality": "input",
      "variability": "continuous",
      "initial": "exact"
    }
  ]
}
```

最小要求：

+ `fmiVersion`
+ `modelName`
+ `variables`

`variables` 中每项最少包含：

+ `name`
+ `valueReference`
+ `dataType`
+ `causality`

### 7.2 `Compatibility_Checks.json` 要求
FMU 不要求按张量维度进行严格检查。

建议：

+ `expectedShape = null`
+ `actualShape = null`
+ `shapeResult = "N/A"`

FMU 应重点检查：

+ 输入输出方向
+ 类型兼容性
+ 单位兼容性
+ 精度兼容性

### 7.3 `raw-extract/` 要求
推荐提供：

+ `raw-extract/modelDescription.xml`
+ `raw-extract/framework_parse_log.txt`

## 8. 闵（AI）返回内容要求
闵的服务返回内容必须以张量接口和适配检查为中心。

### 8.1 `Detected_Model_Meta.json` 要求
建议结构：

```json
{
  "framework": "ONNX",
  "modelName": "AI_AutoPilot_Controller",
  "inputs": [
    {
      "name": "input_sensor",
      "dataType": "float32",
      "shape": [1, 8],
      "rank": 2
    }
  ],
  "outputs": [
    {
      "name": "action_vector",
      "dataType": "float32",
      "shape": [1, 3],
      "rank": 2
    }
  ],
  "layers": [
    {
      "name": "encoder_1",
      "opType": "MatMul"
    }
  ]
}
```

最小要求：

+ `framework`
+ `modelName`
+ `inputs`
+ `outputs`

`inputs` 和 `outputs` 中每项最少包含：

+ `name`
+ `dataType`
+ `shape`
+ `rank`

### 8.2 `Compatibility_Checks.json` 要求
AI 模型必须返回以下检查结果：

+ 方向差异检查
+ 数据类型差异检查
+ 维度差异检查
+ 数值精度差异检查

必须支持：

+ 张量整体映射
+ 张量元素映射（如 `action_vector[0]`）
+ 必要时的张量切片映射

### 8.3 `raw-extract/` 要求
推荐提供：

+ `raw-extract/onnx_io_dump.json`
+ `raw-extract/framework_parse_log.txt`

如果是 `TensorFlow` 或 `PyTorch`，可沿用：

+ `raw-extract/framework_parse_log.txt`

并将框架特定解析结果写入 `Detected_Model_Meta.json`。

## 9. 失败返回要求
即使封装失败，也必须返回标准结果包，并上传到对象存储。

失败时要求：

+ `Result_Metadata.json.status = "FAILED"`
+ `Result_Metadata.json.message` 明确说明失败原因
+ 保留已完成的解析结果
+ 保留 `Warnings.json`
+ 如果存在错误定位信息，应写入 `raw-extract/framework_parse_log.txt`
+ RPC 响应体仍必须返回 `resultPackageUri`

失败场景下建议：

+ `Mapping_Table.json` 返回空数组
+ `Unmatched_Items.json` 返回已识别的未匹配项
+ `Compatibility_Checks.json` 返回已完成检查的部分结果或空数组

## 10. 本系统的接收与校验规则
本系统在接收返回包后，建议按以下顺序校验：

1. 从 RPC 响应体读取 `resultPackageUri`
2. 根据 `resultPackageUri` 从 MinIO 下载结果包
3. 校验 ZIP 是否可解压
4. 校验根目录是否为 `Packaging_Result/`
5. 校验必需文件是否存在
6. 先读取 `Result_Metadata.json`
7. 校验 `taskId`、`blockKey`、`service` 是否与请求一致
8. 再读取 `Mapping_Table.json`、`Warnings.json`、`Compatibility_Checks.json`
9. 最后提取 `generated-src/` 中的 C++ 源码并归档

如果目录结构不符合规范，应判定为协议不兼容。

## 11. 与现有请求协议的关系
本文件定义的是下游 RPC 的返回交付规范，与请求协议文档配套使用：

+ [rpc-model-packaging-protocol.md](/Users/sky/Documents/项目工程/Twin%20analysis%20v2/twin-analysis-framework/rpc-model-packaging-protocol.md)

职责边界如下：

+ 请求协议定义：本系统向陈、闵发送什么
+ 返回内容规范定义：陈、闵必须返回什么

两份文档应同时作为联调基线使用。

## 1. 文档目的
本文档定义 `twin-analysis-framework` 与以下两个下游 RPC 服务之间的统一对接协议：

+ `FmuPackagingService`（陈负责）
+ `AiModelPackagingService`（闵负责）

目标是让下游服务能够接收：

+ 模型文件本体
+ 甲方返回的补齐后模型元数据
+ 本系统解析得到的 SysML 结构上下文

并输出：

+ C++ 包装类源码
+ 变量/张量映射关系表
+ 未匹配项清单
+ 告警与兼容性检查结果

## 2. 上游输入来源
本系统接收甲方交付物 ZIP 后，解压得到：

+ `Delivery_Package/Enriched_Model_Data.json`
+ `Delivery_Package/models/*`

RPC 请求必须基于以下两部分数据构造：

+ 甲方交付物中的 `Enriched_Model_Data.json`
+ 本系统本地解析得到的 SysML 四视图数据（重点是 BDD、IBD）

## 3. 路由规则
RPC 任务按 `Block` 粒度下发。

+ `ModelImplementation.type = "FMU"`：路由到 `FmuPackagingService`
+ `ModelImplementation.type = "ONNX"`：路由到 `AiModelPackagingService`
+ `ModelImplementation.type = "TensorFlow"`：路由到 `AiModelPackagingService`
+ `ModelImplementation.type = "PyTorch"`：路由到 `AiModelPackagingService`
+ `ModelImplementation.type = "Framework"`：不调用下游封装 RPC

## 4. 通用请求协议
请求名称：`PackagingTaskRequest`

```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-20260303-0001",
  "taskType": "MODEL_PACKAGING",
  "requestTime": "2026-03-03T15:30:00+08:00",
  "projectContext": {
    "tenantId": 1001,
    "projectId": 2001,
    "modelId": 3001,
    "modelVersionId": 4001,
    "traceId": "trace-uuid-xxxx"
  },
  "sourceArtifacts": {
    "deliveryArtifactId": 9001,
    "graphViewsArtifactId": 9002,
    "modelLibraryPayloadArtifactId": 9003
  },
  "block": {
    "blockKey": "BlockDefinition:AircraftSysMLModel::20_Structure_BDD::TurbofanEngine",
    "blockName": "TurbofanEngine",
    "packagePath": "AircraftSysMLModel::20_Structure_BDD",
    "description": null
  },
  "implementation": {
    "type": "FMU",
    "filename": "models/CFM56_Turbofan.fmu",
    "fileUri": "https://example.com/artifacts/models/CFM56_Turbofan.fmu",
    "contentSha256": "abc123...",
    "deliveryPackageRelativePath": "models/CFM56_Turbofan.fmu"
  },
  "initialValues": {
    "currentThrust": 0.0,
    "exhaustGasTemp": 15.0
  },
  "portMapping": {
    "p_fuel": {
      "massFlowRate": "fmu_in_eng_fuel_flow",
      "temperature": "fmu_in_eng_fuel_temp"
    },
    "p_throttle": {
      "throttlePosition": "fmu_in_eng_throttle_cmd"
    }
  },
  "expectedBindings": [
    {
      "sysmlPath": "p_fuel.massFlowRate",
      "target": {
        "name": "fmu_in_eng_fuel_flow",
        "kind": "scalar",
        "role": "input",
        "index": null,
        "slice": null
      },
      "expectedDirection": "input",
      "expectedType": "double",
      "expectedShape": [1],
      "expectedPrecision": "float64"
    }
  ],
  "typeSystem": {
    "primitiveTypes": {
      "Mass_kg": "double",
      "Speed_mps": "double"
    },
    "dataTypes": {
      "AircraftSysMLModel::40_CommonTypes::FlightControlCommand": {
        "pitch": "double",
        "roll": "double",
        "yaw": "double"
      }
    }
  },
  "sysmlInterface": {
    "ports": [
      {
        "portName": "p_fuel",
        "direction": "in",
        "fields": [
          {
            "name": "massFlowRate",
            "type": "Mass_kg"
          },
          {
            "name": "temperature",
            "type": "double"
          }
        ]
      }
    ],
    "valueProperties": [
      {
        "name": "currentThrust",
        "type": "double",
        "defaultValue": null
      }
    ],
    "connections": [
      {
        "fromBlock": "FuelSystem",
        "fromPort": "p_outFuel",
        "toBlock": "TurbofanEngine",
        "toPort": "p_fuel"
      }
    ]
  },
  "generationOptions": {
    "language": "C++",
    "cppStandard": "C++17",
    "namespace": "twin.generated",
    "className": "TurbofanEngineWrapper",
    "emitMappingTable": true,
    "emitWarnings": true,
    "emitUnmatchedList": true
  }
}
```

## 5. 通用请求字段说明
### 5.1 `projectContext`
用于链路追踪和结果回写。

+ `tenantId / projectId / modelId / modelVersionId`：系统内部标识
+ `traceId`：建议贯穿日志、RPC 调用和 artifact 追踪

### 5.2 `sourceArtifacts`
标识本次任务使用的源 artifact。

+ `deliveryArtifactId`：甲方交付物 artifact
+ `graphViewsArtifactId`：四视图 artifact
+ `modelLibraryPayloadArtifactId`：发送给甲方的载荷 artifact

### 5.3 `block`
标识当前封装任务所对应的 SysML 模块。

+ `blockKey`：必须与 `Enriched_Model_Data.json -> Blocks` 的 key 一致
+ `blockName`：便于日志和展示的短名称
+ `packagePath`：可选，SysML 包路径

### 5.4 `implementation`
底层模型文件信息。

+ `type`：`FMU`、`ONNX`、`TensorFlow`、`PyTorch`、`Framework`
+ `filename`：来自甲方交付物
+ `fileUri`：建议提供下载地址，便于大文件传输
+ `contentSha256`：建议提供，便于缓存与校验
+ `deliveryPackageRelativePath`：交付包内相对路径

### 5.5 `initialValues`
来源于：

+ `Enriched_Model_Data.json -> Blocks[blockKey].InitialValues`

### 5.6 `portMapping`
来源于：

+ `Enriched_Model_Data.json -> Blocks[blockKey].PortMapping`

这是运行时绑定的关键字段。

+ 第一层键：SysML 端口名
+ 第二层键：SysML 字段名
+ 第二层值：底层模型中的真实变量名或张量名

说明：

+ `portMapping` 保留甲方交付物中的原始映射格式
+ 对于 AI 模型中的张量索引、张量切片、多字段拼接等复杂情况，不能只依赖 `portMapping`
+ 因此必须结合 `expectedBindings` 一起使用

### 5.7 `expectedBindings`
这是面向下游自动校验和自动生成包装类的正式绑定定义，尤其用于闵负责的 AI 模型封装场景。

每一条记录表示：

+ 一个 SysML 字段
+ 它对应的底层目标变量或张量
+ 该字段期望的方向、类型、维度和精度

推荐字段说明：

+ `sysmlPath`：完整 SysML 路径，建议格式为 `端口名.字段名`
+ `target.name`：底层目标名称（变量名或张量名）
+ `target.kind`：目标类型，建议取值：
    - `scalar`
    - `tensor`
    - `tensor_element`
    - `tensor_slice`
+ `target.role`：`input` 或 `output`
+ `target.index`：当目标是张量元素时使用，例如 `[0]`
+ `target.slice`：当目标是张量切片时使用
+ `expectedDirection`：期望方向
+ `expectedType`：期望逻辑类型（例如 `double`、`int`、`bool`）
+ `expectedShape`：期望维度
+ `expectedPrecision`：期望精度，建议使用：
    - `float32`
    - `float64`
    - `int32`
    - `int64`

补充说明：

+ 对 FMU 场景，底层对象通常是标量变量，`expectedShape` 一般可为 `null`，也不作为必填检查项
+ 对 AI 场景，`expectedShape` 必须显式给出，并作为维度适配检查的依据

示例：

```json
[
  {
    "sysmlPath": "p_sensorOut.altitude",
    "target": {
      "name": "onnx_out_sensor_alt",
      "kind": "tensor",
      "role": "output",
      "index": null,
      "slice": null
    },
    "expectedDirection": "output",
    "expectedType": "double",
    "expectedShape": [1],
    "expectedPrecision": "float64"
  },
  {
    "sysmlPath": "p_cmdOut.pitch",
    "target": {
      "name": "action_vector",
      "kind": "tensor_element",
      "role": "output",
      "index": [0],
      "slice": null
    },
    "expectedDirection": "output",
    "expectedType": "double",
    "expectedShape": [1],
    "expectedPrecision": "float64"
  }
]
```

### 5.8 `typeSystem`
来源于：

+ `Enriched_Model_Data.json -> PrimitiveTypes`
+ `Enriched_Model_Data.json -> DataTypes`

用于生成 C++ 类型映射和复合结构体定义。

### 5.9 `sysmlInterface`
由本系统根据 SysML 四视图整理得到，至少应包含：

+ 端口名
+ 端口方向
+ 字段名和字段类型
+ valueProperties
+ 模块间连接关系

### 5.10 `generationOptions`
控制下游包装类生成行为。

建议默认值：

+ `language = "C++"`
+ `cppStandard = "C++17"`

## 6. 通用响应协议
响应名称：`PackagingTaskResponse`

```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-20260303-0001",
  "service": "FmuPackagingService",
  "status": "SUCCEEDED",
  "message": "Wrapper generated successfully",
  "detectedModelMeta": {},
  "mappingTable": [],
  "unmatchedItems": [],
  "warnings": [],
  "compatibilityChecks": [],
  "generatedArtifacts": {
    "headerFile": {
      "filename": "TurbofanEngineWrapper.h",
      "content": "/* ... */"
    },
    "sourceFile": {
      "filename": "TurbofanEngineWrapper.cpp",
      "content": "/* ... */"
    }
  },
  "runtimeContract": {
    "wrapperClassName": "twin.generated::TurbofanEngineWrapper",
    "requiredMethods": []
  }
}
```

## 7. 通用响应字段说明
### 7.1 `status`
允许值：

+ `SUCCEEDED`
+ `PARTIAL_SUCCESS`
+ `FAILED`

### 7.2 `detectedModelMeta`
表示下游服务从目标模型文件中实际解析出的元数据。

+ FMU 场景：变量信息、FMI 元数据
+ AI 场景：输入输出张量、shape、可选层级信息

### 7.3 `mappingTable`
表示 SysML 字段到真实模型变量/张量的匹配结果。

### 7.4 `unmatchedItems`
当有未匹配项时必须返回。

推荐结构：

```json
{
  "kind": "SYSML_FIELD",
  "sysmlPort": "p_fuel",
  "sysmlField": "massFlowRate",
  "reason": "No matching target symbol found"
}
```

### 7.5 `warnings`
表示非致命问题。

推荐结构：

```json
{
  "code": "TYPE_MISMATCH",
  "message": "Expected double but actual type is float32",
  "severity": "WARN",
  "path": "p_navOut.latitude"
}
```

### 7.6 `compatibilityChecks`
该字段用于返回下游服务基于 `expectedBindings` 执行的适配性检查结果。

尤其在 AI 模型场景中，闵的服务应通过该字段返回：

+ 数据类型差异
+ 维度差异
+ 数值精度差异
+ 输入/输出方向差异

推荐结构：

```json
{
  "sysmlPath": "p_cmdOut.pitch",
  "targetName": "action_vector",
  "targetIndex": [0],
  "actualDirection": "output",
  "expectedDirection": "output",
  "actualType": "float32",
  "expectedType": "double",
  "actualShape": [1, 3],
  "expectedShape": [1],
  "actualPrecision": "float32",
  "expectedPrecision": "float64",
  "directionResult": "OK",
  "typeResult": "WARN",
  "shapeResult": "WARN",
  "precisionResult": "WARN",
  "messages": [
    "期望 double，实际为 float32",
    "期望 shape [1]，实际为 [1,3]",
    "存在精度降级：float64 -> float32"
  ]
}
```

### 7.7 `generatedArtifacts`
至少应包含：

+ 头文件
+ 源文件

如果下游服务不直接返回内容，也可以返回 artifact 地址，但双方需提前统一。

### 7.8 `runtimeContract`
描述生成后的包装类集成契约。

+ `wrapperClassName`
+ `requiredMethods`

## 8. FMU 服务特化约定（陈）
服务名：`FmuPackagingService`

请求要求：

+ `implementation.type` 必须为 `FMU`

响应要求：

+ `detectedModelMeta` 必须包含：
    - `fmiVersion`
    - `modelName`
    - `guid`
    - `variables[]`

推荐变量结构：

```json
{
  "name": "fmu_in_eng_fuel_flow",
  "valueReference": 1001,
  "dataType": "Real",
  "unit": "kg/s",
  "causality": "input",
  "variability": "continuous",
  "initial": "exact"
}
```

生成的包装类建议至少实现：

+ `initialize`
+ `doStep`
+ `setInput`
+ `getOutput`
+ `terminate`

## 9. AI 服务特化约定（闵）
服务名：`AiModelPackagingService`

请求要求：

+ `implementation.type` 必须是以下之一：
    - `ONNX`
    - `TensorFlow`
    - `PyTorch`

响应要求：

+ `detectedModelMeta` 应包含：
    - `framework`
    - `modelName`
    - `inputs[]`
    - `outputs[]`
    - 可选 `layers[]`

推荐张量结构：

```json
{
  "name": "onnx_in_pwr_volts",
  "dataType": "float32",
  "shape": [1, 1],
  "rank": 2
}
```

建议附加 AI 专用检查字段：

```json
{
  "compatibilityChecks": [
    {
      "sysmlPath": "p_cmdOut.pitch",
      "targetName": "action_vector",
      "targetIndex": [0],
      "actualDirection": "output",
      "expectedDirection": "output",
      "actualType": "float32",
      "expectedType": "double",
      "actualShape": [1, 3],
      "expectedShape": [1],
      "actualPrecision": "float32",
      "expectedPrecision": "float64",
      "directionResult": "OK",
      "typeResult": "WARN",
      "shapeResult": "WARN",
      "precisionResult": "WARN",
      "messages": [
        "期望 double，实际为 float32",
        "期望 shape [1]，实际为 [1,3]"
      ]
    }
  ]
}
```

说明：

+ 闵的服务必须基于 `expectedBindings` 执行自动检查
+ 当 `target.kind = "tensor_element"` 时，必须支持如 `action_vector[0]` 这样的张量元素映射
+ 当 `target.kind = "tensor_slice"` 时，必须支持切片级映射和校验

生成的包装类建议至少实现：

+ `initialize`
+ `preprocess`
+ `infer`
+ `extractOutputs`
+ `release`

## 10. 传输方式建议
推荐采用：

+ 请求体类型：`application/json`
+ 模型文件传输：通过 `implementation.fileUri`

这样可以避免在 RPC 请求体中传输大体积二进制文件。

如果后续双方协商需要直接上传文件，也可改为：

+ `multipart/form-data`

## 11. 下游服务的最小必需输入
如果缺少以下任一字段，下游服务应拒绝处理：

+ `block.blockKey`
+ `implementation.type`
+ `implementation.filename`
+ `implementation.fileUri` 或实际文件二进制
+ `portMapping`
+ `expectedBindings`
+ `sysmlInterface.ports`
+ `typeSystem.primitiveTypes`

补充说明：

+ 对 FMU 场景，`expectedBindings` 中的 `expectedShape` 可为空
+ 对 AI 场景，`expectedBindings` 必须完整提供 `expectedShape`

## 12. 调用方建议持久化内容
本系统建议保存：

+ 请求 JSON
+ 响应 JSON
+ compatibilityChecks
+ 生成的包装类源码
+ mapping table
+ unmatched items
+ warnings

以便后续集成、审计和问题追踪。

## 13. 当前职责边界
甲方交付物是以下信息的唯一权威来源：

+ `ModelImplementation`
+ `InitialValues`
+ `PortMapping`
+ `PrimitiveTypes`
+ `DataTypes`

因此：

+ 本系统不应再使用旧参数模板库参与参数补全
+ 陈和闵的 RPC 服务应以 `Enriched_Model_Data.json` 为唯一映射和配置依据

## 14. 错误处理规则
如果下游服务在生成可用包装类之前失败：

+ 返回 `status = "FAILED"`
+ 返回明确的 `message`
+ 尽可能在 `warnings` 中返回诊断信息

如果只有部分映射成功：

+ 返回 `status = "PARTIAL_SUCCESS"`
+ 同时返回 `mappingTable` 和 `unmatchedItems`

## 15. 协议版本
当前协议版本：

+ `1.0`

如果发生不兼容变更，必须升级 `protocolVersion`。

## 16. 完整示例
### 16.1 FMU 请求示例（陈）
说明：

+ FMU 变量通常是标量
+ 此场景下 `expectedShape` 不作为必填项，可传 `null`
+ 重点是变量名、值引用、单位、causality 匹配

```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-fmu-0001",
  "taskType": "MODEL_PACKAGING",
  "requestTime": "2026-03-03T16:00:00+08:00",
  "projectContext": {
    "tenantId": 1001,
    "projectId": 2001,
    "modelId": 3001,
    "modelVersionId": 4001,
    "traceId": "trace-fmu-0001"
  },
  "sourceArtifacts": {
    "deliveryArtifactId": 9001,
    "graphViewsArtifactId": 9002,
    "modelLibraryPayloadArtifactId": 9003
  },
  "block": {
    "blockKey": "BlockDefinition:AircraftSysMLModel::20_Structure_BDD::TurbofanEngine",
    "blockName": "TurbofanEngine",
    "packagePath": "AircraftSysMLModel::20_Structure_BDD",
    "description": "涡扇发动机模块"
  },
  "implementation": {
    "type": "FMU",
    "filename": "models/CFM56_Turbofan.fmu",
    "fileUri": "https://example.com/artifacts/models/CFM56_Turbofan.fmu",
    "contentSha256": "fmu-sha256-xxxx",
    "deliveryPackageRelativePath": "models/CFM56_Turbofan.fmu"
  },
  "initialValues": {
    "currentThrust": 0.0,
    "exhaustGasTemp": 15.0
  },
  "portMapping": {
    "p_fuel": {
      "massFlowRate": "fmu_in_eng_fuel_flow",
      "temperature": "fmu_in_eng_fuel_temp"
    },
    "p_throttle": {
      "throttlePosition": "fmu_in_eng_throttle_cmd"
    },
    "p_engineOut": {
      "thrust": "fmu_out_eng_thrust"
    }
  },
  "expectedBindings": [
    {
      "sysmlPath": "p_fuel.massFlowRate",
      "target": {
        "name": "fmu_in_eng_fuel_flow",
        "kind": "scalar",
        "role": "input",
        "index": null,
        "slice": null
      },
      "expectedDirection": "input",
      "expectedType": "double",
      "expectedShape": null,
      "expectedPrecision": "float64"
    },
    {
      "sysmlPath": "p_engineOut.thrust",
      "target": {
        "name": "fmu_out_eng_thrust",
        "kind": "scalar",
        "role": "output",
        "index": null,
        "slice": null
      },
      "expectedDirection": "output",
      "expectedType": "double",
      "expectedShape": null,
      "expectedPrecision": "float64"
    }
  ],
  "typeSystem": {
    "primitiveTypes": {
      "Mass_kg": "double",
      "Force_N": "double"
    },
    "dataTypes": {}
  },
  "sysmlInterface": {
    "ports": [
      {
        "portName": "p_fuel",
        "direction": "in",
        "fields": [
          {
            "name": "massFlowRate",
            "type": "Mass_kg"
          },
          {
            "name": "temperature",
            "type": "double"
          }
        ]
      },
      {
        "portName": "p_engineOut",
        "direction": "out",
        "fields": [
          {
            "name": "thrust",
            "type": "Force_N"
          }
        ]
      }
    ],
    "valueProperties": [
      {
        "name": "currentThrust",
        "type": "double",
        "defaultValue": 0.0
      }
    ],
    "connections": []
  },
  "generationOptions": {
    "language": "C++",
    "cppStandard": "C++17",
    "namespace": "twin.generated",
    "className": "TurbofanEngineWrapper",
    "emitMappingTable": true,
    "emitWarnings": true,
    "emitUnmatchedList": true
  }
}
```

### 16.2 FMU 响应示例（陈）
```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-fmu-0001",
  "service": "FmuPackagingService",
  "status": "SUCCEEDED",
  "message": "FMU 解析完成，已生成包装类",
  "detectedModelMeta": {
    "fmiVersion": "2.0",
    "modelName": "CFM56_Turbofan",
    "guid": "fmu-guid-xxxx",
    "variables": [
      {
        "name": "fmu_in_eng_fuel_flow",
        "valueReference": 1001,
        "dataType": "Real",
        "unit": "kg/s",
        "causality": "input",
        "variability": "continuous",
        "initial": "exact"
      },
      {
        "name": "fmu_out_eng_thrust",
        "valueReference": 2001,
        "dataType": "Real",
        "unit": "N",
        "causality": "output",
        "variability": "continuous",
        "initial": "calculated"
      }
    ]
  },
  "mappingTable": [
    {
      "sysmlPath": "p_fuel.massFlowRate",
      "targetVariable": "fmu_in_eng_fuel_flow",
      "targetValueReference": 1001,
      "matchStatus": "MATCHED"
    },
    {
      "sysmlPath": "p_engineOut.thrust",
      "targetVariable": "fmu_out_eng_thrust",
      "targetValueReference": 2001,
      "matchStatus": "MATCHED"
    }
  ],
  "unmatchedItems": [],
  "warnings": [],
  "compatibilityChecks": [
    {
      "sysmlPath": "p_engineOut.thrust",
      "targetName": "fmu_out_eng_thrust",
      "actualDirection": "output",
      "expectedDirection": "output",
      "actualType": "Real",
      "expectedType": "double",
      "actualShape": null,
      "expectedShape": null,
      "actualPrecision": "float64",
      "expectedPrecision": "float64",
      "directionResult": "OK",
      "typeResult": "OK",
      "shapeResult": "N/A",
      "precisionResult": "OK",
      "messages": []
    }
  ],
  "generatedArtifacts": {
    "headerFile": {
      "filename": "TurbofanEngineWrapper.h",
      "content": "/* ... */"
    },
    "sourceFile": {
      "filename": "TurbofanEngineWrapper.cpp",
      "content": "/* ... */"
    }
  },
  "runtimeContract": {
    "wrapperClassName": "twin.generated::TurbofanEngineWrapper",
    "requiredMethods": [
      "initialize",
      "doStep",
      "setInput",
      "getOutput",
      "terminate"
    ]
  }
}
```

### 16.3 AI 请求示例（闵）
说明：

+ AI 模型必须显式提供 `expectedShape`
+ 支持张量元素映射，例如 `action_vector[0]`

```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-ai-0001",
  "taskType": "MODEL_PACKAGING",
  "requestTime": "2026-03-03T16:10:00+08:00",
  "projectContext": {
    "tenantId": 1001,
    "projectId": 2001,
    "modelId": 3001,
    "modelVersionId": 4001,
    "traceId": "trace-ai-0001"
  },
  "sourceArtifacts": {
    "deliveryArtifactId": 9001,
    "graphViewsArtifactId": 9002,
    "modelLibraryPayloadArtifactId": 9003
  },
  "block": {
    "blockKey": "BlockDefinition:AircraftSysMLModel::20_Structure_BDD::AvionicsSystem",
    "blockName": "AvionicsSystem",
    "packagePath": "AircraftSysMLModel::20_Structure_BDD",
    "description": "航电智能代理模块"
  },
  "implementation": {
    "type": "ONNX",
    "filename": "models/AI_AutoPilot_Controller.onnx",
    "fileUri": "https://example.com/artifacts/models/AI_AutoPilot_Controller.onnx",
    "contentSha256": "onnx-sha256-xxxx",
    "deliveryPackageRelativePath": "models/AI_AutoPilot_Controller.onnx"
  },
  "initialValues": {},
  "portMapping": {
    "p_powerIn": {
      "voltage": "onnx_in_pwr_volts",
      "current": "onnx_in_pwr_amps"
    },
    "p_cmdOut": {
      "pitch": "action_vector[0]",
      "roll": "action_vector[1]",
      "yaw": "action_vector[2]"
    }
  },
  "expectedBindings": [
    {
      "sysmlPath": "p_powerIn.voltage",
      "target": {
        "name": "onnx_in_pwr_volts",
        "kind": "tensor",
        "role": "input",
        "index": null,
        "slice": null
      },
      "expectedDirection": "input",
      "expectedType": "double",
      "expectedShape": [1],
      "expectedPrecision": "float64"
    },
    {
      "sysmlPath": "p_cmdOut.pitch",
      "target": {
        "name": "action_vector",
        "kind": "tensor_element",
        "role": "output",
        "index": [0],
        "slice": null
      },
      "expectedDirection": "output",
      "expectedType": "double",
      "expectedShape": [1],
      "expectedPrecision": "float64"
    }
  ],
  "typeSystem": {
    "primitiveTypes": {
      "Voltage_V": "double",
      "Current_A": "double"
    },
    "dataTypes": {
      "AircraftSysMLModel::40_CommonTypes::FlightControlCommand": {
        "pitch": "double",
        "roll": "double",
        "yaw": "double"
      }
    }
  },
  "sysmlInterface": {
    "ports": [
      {
        "portName": "p_powerIn",
        "direction": "in",
        "fields": [
          {
            "name": "voltage",
            "type": "Voltage_V"
          },
          {
            "name": "current",
            "type": "Current_A"
          }
        ]
      },
      {
        "portName": "p_cmdOut",
        "direction": "out",
        "fields": [
          {
            "name": "pitch",
            "type": "double"
          },
          {
            "name": "roll",
            "type": "double"
          },
          {
            "name": "yaw",
            "type": "double"
          }
        ]
      }
    ],
    "valueProperties": [],
    "connections": []
  },
  "generationOptions": {
    "language": "C++",
    "cppStandard": "C++17",
    "namespace": "twin.generated",
    "className": "AvionicsSystemWrapper",
    "emitMappingTable": true,
    "emitWarnings": true,
    "emitUnmatchedList": true
  }
}
```

### 16.4 AI 响应示例（闵）
```json
{
  "protocolVersion": "1.0",
  "taskId": "pkg-task-ai-0001",
  "service": "AiModelPackagingService",
  "status": "PARTIAL_SUCCESS",
  "message": "模型解析完成，已生成包装类，存在维度与精度警告",
  "detectedModelMeta": {
    "framework": "ONNX",
    "modelName": "AI_AutoPilot_Controller",
    "inputs": [
      {
        "name": "onnx_in_pwr_volts",
        "dataType": "float32",
        "shape": [1, 1],
        "rank": 2
      }
    ],
    "outputs": [
      {
        "name": "action_vector",
        "dataType": "float32",
        "shape": [1, 3],
        "rank": 2
      }
    ],
    "layers": [
      {
        "name": "encoder_1",
        "opType": "MatMul"
      },
      {
        "name": "relu_1",
        "opType": "Relu"
      }
    ]
  },
  "mappingTable": [
    {
      "sysmlPath": "p_powerIn.voltage",
      "targetTensor": "onnx_in_pwr_volts",
      "tensorRole": "input",
      "matchStatus": "MATCHED"
    },
    {
      "sysmlPath": "p_cmdOut.pitch",
      "targetTensor": "action_vector",
      "targetIndex": [0],
      "tensorRole": "output",
      "matchStatus": "MATCHED"
    }
  ],
  "unmatchedItems": [],
  "warnings": [
    {
      "code": "SHAPE_MISMATCH",
      "message": "期望 shape [1]，实际输出张量 shape 为 [1,3]",
      "severity": "WARN",
      "path": "p_cmdOut.pitch"
    },
    {
      "code": "PRECISION_DOWNCAST",
      "message": "期望 float64，实际为 float32",
      "severity": "WARN",
      "path": "p_cmdOut.pitch"
    }
  ],
  "compatibilityChecks": [
    {
      "sysmlPath": "p_cmdOut.pitch",
      "targetName": "action_vector",
      "targetIndex": [0],
      "actualDirection": "output",
      "expectedDirection": "output",
      "actualType": "float32",
      "expectedType": "double",
      "actualShape": [1, 3],
      "expectedShape": [1],
      "actualPrecision": "float32",
      "expectedPrecision": "float64",
      "directionResult": "OK",
      "typeResult": "WARN",
      "shapeResult": "WARN",
      "precisionResult": "WARN",
      "messages": [
        "期望 double，实际为 float32",
        "期望 shape [1]，实际为 [1,3]",
        "存在精度降级：float64 -> float32"
      ]
    }
  ],
  "generatedArtifacts": {
    "headerFile": {
      "filename": "AvionicsSystemWrapper.h",
      "content": "/* ... */"
    },
    "sourceFile": {
      "filename": "AvionicsSystemWrapper.cpp",
      "content": "/* ... */"
    }
  },
  "runtimeContract": {
    "wrapperClassName": "twin.generated::AvionicsSystemWrapper",
    "requiredMethods": [
      "initialize",
      "preprocess",
      "infer",
      "extractOutputs",
      "release"
    ]
  }
}
```

