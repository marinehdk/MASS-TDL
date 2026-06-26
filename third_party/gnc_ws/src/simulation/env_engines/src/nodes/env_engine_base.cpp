/**
 * @file env_engine_base.cpp
 * @brief 环境引擎基类实现
 * 
 * 实现EnvEngineBase类的核心功能，包括实时优先级设置、参数验证
 * 工具函数和角度处理等公共功能。
 */

#include "env_engines/env_engine_base.hpp"
#include <cmath>
#include <sys/mman.h>
#include <sched.h>

namespace env_engines {

/**
 * @brief 构造函数
 * @param node_name 节点名称
 * @param enable_realtime 是否启用实时优先级
 * 
 * 初始化节点并根据参数决定是否设置实时优先级
 */
EnvEngineBase::EnvEngineBase(const std::string& node_name, bool enable_realtime)
    : Node(node_name), realtime_enabled_(enable_realtime) {
    
    if (enable_realtime) {
        realtime_enabled_ = set_realtime_priority();
    }
}

/**
 * @brief 设置实时优先级
 * @return 是否设置成功
 * 
 * 设置SCHED_FIFO调度策略、锁定内存页面、设置CPU亲和性，
 * 以确保实时性能。即使设置失败也会返回false，但不会影响节点运行。
 */
bool EnvEngineBase::set_realtime_priority() {
    struct sched_param param;
    param.sched_priority = REALTIME_PRIORITY;
    
    // 设置SCHED_FIFO调度策略
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        RCLCPP_WARN(this->get_logger(), 
            "[%s] 无法设置实时优先级(SCHED_FIFO:%d)，请检查权限或RT内核",
            this->get_name(), REALTIME_PRIORITY);
        return false;
    }
    
    // 禁用内存分页交换，确保实时性
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        RCLCPP_WARN(this->get_logger(),
            "[%s] 无法锁定内存页面，实时性能可能受影响",
            this->get_name());
    }
    
    // 设置 CPU 亲和性（绑定到第一个核心）
    // cpu_set_t cpuset;
    // CPU_ZERO(&cpuset);
    // CPU_SET(0, &cpuset);
    // if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
    //     RCLCPP_WARN(this->get_logger(),
    //         "[%s] 无法设置 CPU 亲和性", this->get_name());
    // }
    
    RCLCPP_INFO(this->get_logger(), "[%s] 实时优先级已设置 (SCHED_FIFO:%d)",
        this->get_name(), REALTIME_PRIORITY);
    return true;
}

/**
 * @brief 验证参数是否为正数
 * @param name 参数名称
 * @param value 参数值
 * @param min_value 最小允许值
 * @return 是否验证通过
 * 
 * 检查参数是否大于指定的最小值，用于确保物理参数的有效性
 */
bool EnvEngineBase::validate_positive(const std::string& name, double value, double min_value) {
    if (value <= min_value) {
        RCLCPP_ERROR(this->get_logger(), "[%s] 参数 %s = %.4f 必须 > %.4f",
            this->get_name(), name.c_str(), value, min_value);
        return false;
    }
    return true;
}

/**
 * @brief 验证参数是否在范围内
 * @param name 参数名称
 * @param value 参数值
 * @param min_val 最小值
 * @param max_val 最大值
 * @return 是否验证通过
 * 
 * 检查参数是否在指定的范围内，用于确保参数的合理性
 */
bool EnvEngineBase::validate_range(const std::string& name, double value, double min_val, double max_val) {
    if (value < min_val || value > max_val) {
        RCLCPP_ERROR(this->get_logger(), "[%s] 参数 %s = %.4f 超出范围 [%.4f, %.4f]",
            this->get_name(), name.c_str(), value, min_val, max_val);
        return false;
    }
    return true;
}

/**
 * @brief 安全除法，避免除零
 * @param numerator 分子
 * @param denominator 分母
 * @param default_value 除零时的默认值
 * @return 计算结果
 * 
 * 当分母接近零时返回默认值，否则执行正常除法
 */
double EnvEngineBase::safe_divide(double numerator, double denominator, double default_value) {
    if (std::abs(denominator) < 1e-10) {
        return default_value;
    }
    return numerator / denominator;
}

/**
 * @brief 角度归一化到 [0, 2π)
 * @param angle_rad 角度（弧度）
 * @return 归一化后的角度
 * 
 * 将任意角度归一化到0到2π之间，便于角度计算和比较
 */
double EnvEngineBase::normalize_angle(double angle_rad) {
    double normalized = std::fmod(angle_rad, 2.0 * M_PI);
    if (normalized < 0) {
        normalized += 2.0 * M_PI;
    }
    return normalized;
}

/**
 * @brief 角度折叠到 [0, π]（用于对称处理）
 * @param angle_rad 角度（弧度）
 * @return 折叠后的角度
 * 
 * 将角度折叠到0到π之间，用于处理对称物理模型（如波浪力）
 */
double EnvEngineBase::fold_angle(double angle_rad) {
    double normalized = normalize_angle(angle_rad);
    return (normalized <= M_PI) ? normalized : (2.0 * M_PI - normalized);
}

/**
 * @brief 获取带节点前缀的日志头
 * @return 格式化的日志前缀
 * 
 * 生成包含节点名称的日志前缀，用于统一日志格式
 */
std::string EnvEngineBase::get_log_prefix() const {
    return std::string("[") + this->get_name() + "]";
}

} // namespace env_engines
