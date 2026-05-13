/**
 * @file IModelBlock.h
 * @brief 统一模型架构接口定义 (Universal Architecture Interface Contract - V2.0)
 * @author Architect Team (Module 1)
 */
#pragma once
#include <string>
#include <vector>
#include <map>

 // -------------------------------------------------------------------------
 // 1. 标准状态码定义
 // -------------------------------------------------------------------------
enum class ModelStatus {
    OK = 0, WARNING = 1, ERROR = 2, FATAL = 3
};

// -------------------------------------------------------------------------
// 2. 统一模型接口基类 (V2.0)
// -------------------------------------------------------------------------
class IModelBlock {
public:
    virtual ~IModelBlock() = default;

    // =================================================================
    // A. 生命周期接口
    // =================================================================
    virtual ModelStatus init() = 0;
    virtual ModelStatus configure(const std::string& configData) = 0;
    virtual ModelStatus step(double time, double stepSize) = 0;
    virtual ModelStatus reset() = 0;
    virtual ModelStatus terminate() = 0;

    // =================================================================
    // B. 数据交互接口 - 实数
    // =================================================================
    virtual void setRealInput(const std::string& portName, double value) = 0;
    virtual double getRealOutput(const std::string& portName) const = 0;

    // =================================================================
    // C. 数据交互接口 - 整型与枚举
    // =================================================================
    virtual void setIntInput(const std::string& portName, int value) = 0;
    virtual int getIntOutput(const std::string& portName) const = 0;

    // =================================================================
    // D. 数据交互接口 - 布尔型
    // =================================================================
    virtual void setBoolInput(const std::string& portName, bool value) = 0;
    virtual bool getBoolOutput(const std::string& portName) const = 0;

    // =================================================================
    // E. 数据交互接口 - 字符串
    // =================================================================
    virtual void setStringInput(const std::string& portName, const std::string& value) = 0;
    virtual std::string getStringOutput(const std::string& portName) const = 0;

    // =================================================================
    // F. 诊断与监控接口
    // =================================================================
    virtual std::string getBlockName() const = 0;
    virtual std::string getLastError() const = 0;
    virtual std::vector<std::string> getPortList() const = 0;

    // =================================================================
    // G. 新增：数据交互接口 - 张量/图像 (Tensor/Image)   ←←← 关键扩展
    // 支持复杂多维输入（如图像、序列、特征图等）
    // =================================================================
    virtual void setTensorInput(const std::string& portName, const std::vector<float>& tensorData) = 0;
};