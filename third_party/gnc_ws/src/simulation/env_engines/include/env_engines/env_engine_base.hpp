#ifndef ENV_ENGINES_ENV_ENGINE_BASE_HPP
#define ENV_ENGINES_ENV_ENGINE_BASE_HPP

#include "rclcpp/rclcpp.hpp"
#include <sched.h>
#include <sys/resource.h>
#include <string>

namespace env_engines {

// 物理常数定义
constexpr double RHO_WATER = 1026.0;      // 海水密度 kg/m^3
constexpr double RHO_AIR = 1.225;         // 空气密度 kg/m^3
constexpr double G = 9.81;                // 重力加速度 m/s^2
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

// 实时优先级配置
constexpr int REALTIME_PRIORITY = 80;
constexpr int DEFAULT_TIMER_HZ = 10;      // 默认定时器频率

/**
 * @brief 环境引擎基类
 * 
 * 提供所有环境载荷计算节点的公共功能：
 * - 实时优先级设置
 * - 参数验证工具
 * - 通用日志格式
 */
class EnvEngineBase : public rclcpp::Node {
public:
    /**
     * @brief 构造函数
     * @param node_name 节点名称
     * @param enable_realtime 是否启用实时优先级（默认true）
     */
    explicit EnvEngineBase(const std::string& node_name, bool enable_realtime = true);

protected:
    /**
     * @brief 设置实时优先级 (SCHED_FIFO)
     * @return 是否设置成功
     */
    bool set_realtime_priority();

    /**
     * @brief 验证参数是否为正数
     * @param name 参数名称（用于日志）
     * @param value 参数值
     * @param min_value 最小允许值（默认0.0）
     * @return 是否验证通过
     */
    bool validate_positive(const std::string& name, double value, double min_value = 0.0);

    /**
     * @brief 验证参数是否在范围内
     * @param name 参数名称
     * @param value 参数值
     * @param min_val 最小值
     * @param max_val 最大值
     * @return 是否验证通过
     */
    bool validate_range(const std::string& name, double value, double min_val, double max_val);

    /**
     * @brief 安全除法，避免除零
     * @param numerator 分子
     * @param denominator 分母
     * @param default_value 除零时的默认值
     * @return 计算结果
     */
    static double safe_divide(double numerator, double denominator, double default_value = 0.0);

    /**
     * @brief 角度归一化到 [0, 2π)
     * @param angle_rad 角度（弧度）
     * @return 归一化后的角度
     */
    static double normalize_angle(double angle_rad);

    /**
     * @brief 角度折叠到 [0, π]（用于对称处理）
     * @param angle_rad 角度（弧度）
     * @return 折叠后的角度
     */
    static double fold_angle(double angle_rad);

    /**
     * @brief 获取带节点前缀的日志头
     * @return 格式化的日志前缀
     */
    std::string get_log_prefix() const;

    // 节点状态标志
    bool is_initialized_ = false;
    bool realtime_enabled_ = false;
};

} // namespace env_engines

#endif // ENV_ENGINES_ENV_ENGINE_BASE_HPP
