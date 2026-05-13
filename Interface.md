

## 上传内部文件


**接口地址**:`/api/v1/files/upload`


**请求方式**:`POST`


**请求数据类型**:`application/x-www-form-urlencoded,multipart/form-data`


**响应数据类型**:`application/json`


**接口描述**:<p>上传文件到后端内部存储，并返回文件标识、下载地址、校验值及存储信息。</p>



**请求参数**:


**请求参数**:


| 参数名称 | 参数说明 | 请求类型    | 是否必须 | 数据类型 | schema |
| -------- | -------- | ----- | -------- | -------- | ------ |
|file|待上传文件。|query|true|file||
|userId|用户 ID。|query|false|string||
|tags||query|false|string||
|metadata||query|false|string||


**响应状态**:


| 状态码 | 说明 | schema |
| -------- | -------- | ----- | 
|201|文件上传成功。|FileUploadResponse|
|400|请求参数非法或文件内容不符合要求。|FileUploadErrorResponse|
|500|文件保存或后端处理失败。|FileUploadErrorResponse|


**响应状态码-201**:


**响应参数**:


| 参数名称 | 参数说明 | 类型 | schema |
| -------- | -------- | ----- |----- | 
|fileId|文件唯一标识，用于后续下载。|string||
|filename|服务端保存的文件名。|string||
|fileSize|文件大小，单位字节。|integer(int64)|integer(int64)|
|contentType|文件 MIME 类型。|string||
|uploadTime|上传时间。|string||
|message|处理结果消息。|string||
|downloadUrl|文件下载地址。|string||
|md5Hash|文件 MD5 校验值。|string||
|extraInfo||FileUploadExtraInfo|FileUploadExtraInfo|
|&emsp;&emsp;minioStored|是否已同步保存到 MinIO。|boolean||
|&emsp;&emsp;storageType|存储类型。`local` 表示本地存储，`dual` 表示本地与 MinIO 双写。|string||
|&emsp;&emsp;gridfsStored|是否已保存到 GridFS。当前固定为 false。|boolean||


**响应示例**:
```javascript
{
	"fileId": "20260506123000123_8f3a2c4d.json",
	"filename": "20260506123000123_launch.json",
	"fileSize": 128,
	"contentType": "application/json",
	"uploadTime": "2026-05-06T12:30:00",
	"message": "File uploaded successfully",
	"downloadUrl": "http://10.95.210.240:8080/api/v1/files/download/20260506123000123_8f3a2c4d.json",
	"md5Hash": "e4b7dec1c3f18b0b4f6d1f7a8a3d2c11",
	"extraInfo": {
		"minioStored": true,
		"storageType": "dual",
		"gridfsStored": false
	}
}
```


**响应状态码-400**:


**响应参数**:


| 参数名称 | 参数说明 | 类型 | schema |
| -------- | -------- | ----- |----- | 
|message|错误消息。|string||


**响应示例**:
```javascript
{
	"message": "file is required"
}
```


**响应状态码-500**:


**响应参数**:


| 参数名称 | 参数说明 | 类型 | schema |
| -------- | -------- | ----- |----- | 
|message|错误消息。|string||


**响应示例**:
```javascript
{
	"message": "file is required"
}
```




## 模块2-包装类代码产物回调


**接口地址**:`/internal/module2/callbacks/wrapper-codegen`


**请求方式**:`POST`


**请求数据类型**:`application/x-www-form-urlencoded,application/json`


**响应数据类型**:`*/*`


**接口描述**:<p>模块2处理服务生成包装类完成后回调 ADP。ADP 接收 artifact_uri、artifact_type、artifact_sha256 和文件清单，随后下载产物并上传 Gitea。</p>



**请求示例**:


```javascript
{
  "taskId": 2051293181848457216,
  "status": "SUCCESS",
  "success": true,
  "projectName": "AircraftControlQt",
  "artifactUri": "http://localhost:8080/api/v1/codegen/artifacts/xxx:download",
  "artifactType": "zip",
  "artifactSha256": "xxxx",
  "mainClassName": "AircraftControl",
  "wrapperClass": "FlightControlWrapper",
  "modelType": "FMU",
  "runtime": "qt_cpp",
  "modelCount": 3,
  "fileCount": 12,
  "files": {},
  "warnings": {},
  "errorCode": "",
  "errorMessage": ""
}
```


**请求参数**:


**请求参数**:


| 参数名称 | 参数说明 | 请求类型    | 是否必须 | 数据类型 | schema |
| -------- | -------- | ----- | -------- | -------- | ------ |
|codeArtifactCallbackRequest|代码生成服务完成后回调 ADP 的产物信息。ADP 接收后下载 artifact_uri 并上传 Git。|body|true|CodeArtifactCallbackRequest|CodeArtifactCallbackRequest|
|&emsp;&emsp;taskId|ADP 任务 ID。模块1为模型接入任务 ID，模块2为包装类任务 ID。||false|integer(int64)||
|&emsp;&emsp;status|生成状态。SUCCESS 表示成功，其它状态按失败或处理中处理。||false|string||
|&emsp;&emsp;success|是否生成成功。若传入该字段，优先于 status 判断。||false|boolean||
|&emsp;&emsp;projectName|生成项目名。||false|string||
|&emsp;&emsp;artifactUri|代码产物压缩包下载地址。||false|string||
|&emsp;&emsp;artifactType|代码产物压缩包类型。当前 ADP Git 上传支持 zip。||false|string||
|&emsp;&emsp;artifactSha256|代码产物 SHA-256。||false|string||
|&emsp;&emsp;mainClassName|模块1主模型类名。||false|string||
|&emsp;&emsp;wrapperClass|模块2包装类名。||false|string||
|&emsp;&emsp;modelType|模块2模型类型。||false|string||
|&emsp;&emsp;runtime|目标运行时。||false|string||
|&emsp;&emsp;modelCount|模型数量。||false|integer(int32)||
|&emsp;&emsp;fileCount|文件数量。||false|integer(int32)||
|&emsp;&emsp;files|文件清单。||false|object||
|&emsp;&emsp;warnings|警告信息。||false|object||
|&emsp;&emsp;errorCode|错误码。||false|string||
|&emsp;&emsp;errorMessage|错误信息。||false|string||


**响应状态**:


| 状态码 | 说明 | schema |
| -------- | -------- | ----- | 
|200|ADP 已接收包装类产物并完成或触发 Git 交付。|Module2WrapperTaskVO|


**响应参数**:


| 参数名称 | 参数说明 | 类型 | schema |
| -------- | -------- | ----- |----- | 
|id|任务记录 ID|integer(int64)|integer(int64)|
|taskId|任务 ID，与 id 保持一致，便于外部系统识别。|integer(int64)|integer(int64)|
|taskName|任务名称|string||
|modelType|模型类型,可用值:FMU,AI_MODEL|string||
|modelName|模型名称|string||
|modelVersion|模型版本|string||
|runtime|目标运行时|string||
|strictMode|是否启用严格校验|boolean||
|sourceFileName|源文件名|string||
|sourceFileSize|源文件大小，单位字节|integer(int64)|integer(int64)|
|sourceFileSha256|源文件 SHA-256|string||
|status|任务状态：MODULE2_SUBMITTED / MODULE2_PROCESSING / MODULE2_CODEGEN_SUBMITTED / MODULE2_CODEGEN_READY / MODULE2_CODEGEN_FAILED / MODULE2_GIT_DELIVERING / MODULE2_GIT_DELIVERED / MODULE2_GIT_DELIVERY_FAILED|string||
|progress|任务进度，0-100|integer(int32)|integer(int32)|
|stage|当前阶段：SUBMITTED / PROCESSING / CODEGEN_SUBMITTED / CODEGEN_READY / CODEGEN_FAILED / GIT_DELIVERING / GIT_DELIVERED / GIT_DELIVERY_FAILED|string||
|message|当前阶段说明|string||
|errorMessage|错误信息，失败时返回|string||
|remark|备注|string||
|statusUrl|任务状态查询地址|string||
|codegenResult|模块2处理服务返回的代码生成结果，通常包含 project_name、wrapper_class、artifact_uri、artifact_sha256、files、warnings 等字段。|object||
|gitDeliveryResult|ADP 上传包装类代码到 Gitea 的结果，通常包含 repoId、repoName、branch、commitSha、cloneUrl、htmlUrl 等字段。|object||
|codegenReady|是否已生成包装类代码产物|boolean||
|gitDelivered|是否已上传包装类代码到 Git|boolean||
|createTime|创建时间|string(date-time)|string(date-time)|
|updateTime|更新时间|string(date-time)|string(date-time)|


**响应示例**:
```javascript
{
	"id": 20001,
	"taskId": 20001,
	"taskName": "flight-control-fmu-wrapper",
	"modelType": "FMU",
	"modelName": "flight-control",
	"modelVersion": "v1.0.0",
	"runtime": "qt_cpp",
	"strictMode": false,
	"sourceFileName": "flight-control.fmu",
	"sourceFileSize": 1048576,
	"sourceFileSha256": "",
	"status": "MODULE2_GIT_DELIVERED",
	"progress": 100,
	"stage": "GIT_DELIVERED",
	"message": "包装类代码已上传 Git",
	"errorMessage": "",
	"remark": "",
	"statusUrl": "/api/module2/wrapper-tasks/20001",
	"codegenResult": {},
	"gitDeliveryResult": {},
	"codegenReady": true,
	"gitDelivered": true,
	"createTime": "",
	"updateTime": ""
}
```
