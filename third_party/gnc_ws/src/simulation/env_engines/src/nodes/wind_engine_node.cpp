/**
 * @file wind_engine_node.cpp
 * @brief 风力计算节点实现，基于OCIMF风载荷标准
 *
 * 修改记录 (2026-04-14):
 *   [MOD] 替换风力系数表 - 原实现使用 cos/sin 数学构造，无物理依据
 *         新实现基于 OCIMF 2010 标准推荐的工程系数
 *
 *   参考: OCIMF 2010 "Prediction of Wind Loads and Wind Effects on Floating Structures"
 *         ITTC 2011 Recommended Procedures 7.5-02
 *
 *   物理意义:
 *   - Cx: 纵向力系数，正值为顺风推力，负值为顶风阻力
 *   - Cy: 横向力系数，正值为右舷横风推力
 *   - Cn: 偏航力矩系数，正值为右转首摇力矩
 */

#include "env_engines/wind_engine_node.hpp"
#include "ship_interfaces/msg/vessel_params.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <shared_mutex>

namespace env_engines {

WindEngineNode::WindEngineNode()
    : EnvEngineBase("wind_engine_node", true),
      rng_(std::random_device{}()),
      phase_dist_(0.0, 2.0 * M_PI) {

    this->declare_parameter("u10", DEFAULT_U10);
    this->declare_parameter("z_center", DEFAULT_Z_CENTER);
    this->declare_parameter("Af", DEFAULT_AF);
    this->declare_parameter("Al", DEFAULT_AL);
    this->declare_parameter("Lpp", DEFAULT_LPP);
    this->declare_parameter("wind_roll_moment_arm", DEFAULT_WIND_ROLL_MOMENT_ARM);
    this->declare_parameter("wind_direction", DEFAULT_WIND_DIR);
    this->declare_parameter("wind_direction_is_from", DEFAULT_WIND_DIRECTION_IS_FROM);
    this->declare_parameter("wind_source_mode", std::string("auto"));
    this->declare_parameter("wind_input_timeout_s", DEFAULT_WIND_INPUT_TIMEOUT_S);
    this->declare_parameter("wind_input_filter_enabled", DEFAULT_WIND_INPUT_FILTER_ENABLED);
    this->declare_parameter("wind_speed_filter_tau_s", DEFAULT_WIND_SPEED_FILTER_TAU_S);
    this->declare_parameter("wind_speed_rate_limit_mps_s", DEFAULT_WIND_SPEED_RATE_LIMIT_MPS_S);
    this->declare_parameter("wind_direction_rate_limit_deg_s", DEFAULT_WIND_DIRECTION_RATE_LIMIT_DEG_S);
    this->declare_parameter("anemometer_height", 10.0);
    this->declare_parameter("air_density", DEFAULT_AIR_DENSITY);

    u10_ = this->get_parameter("u10").as_double();
    z_c_ = this->get_parameter("z_center").as_double();
    Af_ = this->get_parameter("Af").as_double();
    Al_ = this->get_parameter("Al").as_double();
    Lpp_ = this->get_parameter("Lpp").as_double();
    wind_roll_moment_arm_ = this->get_parameter("wind_roll_moment_arm").as_double();
    wind_direction_ = normalize_degrees(this->get_parameter("wind_direction").as_double());
    wind_direction_is_from_ = this->get_parameter("wind_direction_is_from").as_bool();
    WindSourceMode parsed_mode = WindSourceMode::Auto;
    if (!parse_wind_source_mode(this->get_parameter("wind_source_mode").as_string(), parsed_mode)) {
        RCLCPP_WARN(this->get_logger(), "[WindEngine] invalid wind_source_mode, fallback to auto");
    }
    wind_source_mode_ = parsed_mode;
    wind_input_timeout_s_ = std::max(0.1, this->get_parameter("wind_input_timeout_s").as_double());
    wind_input_filter_enabled_ = this->get_parameter("wind_input_filter_enabled").as_bool();
    wind_speed_filter_tau_s_ = std::max(0.1, this->get_parameter("wind_speed_filter_tau_s").as_double());
    wind_speed_rate_limit_mps_s_ = std::max(0.1, this->get_parameter("wind_speed_rate_limit_mps_s").as_double());
    wind_direction_rate_limit_deg_s_ = std::max(0.1, this->get_parameter("wind_direction_rate_limit_deg_s").as_double());
    anemometer_height_ = this->get_parameter("anemometer_height").as_double();
    air_density_ = this->get_parameter("air_density").as_double();
    explicit_u10_ = u10_;
    explicit_wind_direction_ = wind_direction_;
    explicit_u10_received_ = (wind_source_mode_ == WindSourceMode::U10) || (u10_ >= MIN_WIND_SPEED);

    param_cb_handle_ = this->add_on_set_parameters_callback(
        std::bind(&WindEngineNode::parameter_callback, this, std::placeholders::_1));

    if (!validate_and_update_params()) {
        RCLCPP_ERROR(this->get_logger(), "[WindEngine] 参数验证失败，使用默认值");
        u10_ = DEFAULT_U10;
        z_c_ = DEFAULT_Z_CENTER;
        Af_ = DEFAULT_AF;
        Al_ = DEFAULT_AL;
        Lpp_ = DEFAULT_LPP;
    }
    explicit_u10_ = u10_;
    explicit_wind_direction_ = wind_direction_;
    explicit_u10_received_ = (wind_source_mode_ == WindSourceMode::U10) || (u10_ >= MIN_WIND_SPEED);

    init_coefficient_tables();
    select_wind_source_locked();
    apply_wind_input_filter_locked(1.0 / DEFAULT_TIMER_HZ);
    update_wind_speed();
    init_spectrum();
    last_spectrum_u10_ = u10_;

    publisher_ = this->create_publisher<geometry_msgs::msg::WrenchStamped>("/env/wind_load", 10);

    wind_sub_ = this->create_subscription<geometry_msgs::msg::Vector3>(
        "/env/wind_params", 10,
        std::bind(&WindEngineNode::wind_params_callback, this, std::placeholders::_1));

    anemometer_sub_ = this->create_subscription<geometry_msgs::msg::Vector3>(
        "/env/anemometer_params", 10,
        std::bind(&WindEngineNode::anemometer_params_callback, this, std::placeholders::_1));

    vessel_sub_ = this->create_subscription<ship_interfaces::msg::VesselParams>(
        "/env/vessel_params", 10,
        std::bind(&WindEngineNode::vessel_params_callback, this, std::placeholders::_1));

    heading_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/ship/heading", 10,
        std::bind(&WindEngineNode::heading_callback, this, std::placeholders::_1));

    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&WindEngineNode::odom_callback, this, std::placeholders::_1));

    const auto timer_period = std::chrono::milliseconds(1000 / DEFAULT_TIMER_HZ);
    timer_ = this->create_wall_timer(timer_period,
        std::bind(&WindEngineNode::calculate_step, this));

    RCLCPP_INFO(this->get_logger(), "[WindEngine] 风力引擎启动 (OCIMF系数版)");
    RCLCPP_INFO(this->get_logger(),
        "[WindEngine] U10=%.2f m/s | Zc=%.2f m | direction=%.1f deg (%s) | mode=%s source=%s timeout=%.1fs",
        u10_, z_c_, wind_direction_, wind_direction_is_from_ ? "from" : "to",
        wind_source_mode_name(wind_source_mode_),
        effective_wind_source_name(effective_wind_source_),
        wind_input_timeout_s_);
}

bool WindEngineNode::validate_and_update_params() {
    bool valid = true;
    valid &= validate_nonnegative("u10", u10_);
    valid &= validate_positive("z_center", z_c_, 0.1);
    valid &= validate_positive("Af", Af_, 0.0);
    valid &= validate_positive("Al", Al_, 0.0);
    valid &= validate_positive("Lpp", Lpp_, 0.0);
    valid &= std::isfinite(wind_roll_moment_arm_) && wind_roll_moment_arm_ >= -1.0;
    valid &= validate_range("wind_direction", wind_direction_, 0.0, 360.0);
    valid &= validate_positive("air_density", air_density_, 0.0);
    valid &= std::isfinite(wind_input_timeout_s_) && validate_positive("wind_input_timeout_s", wind_input_timeout_s_, 0.0);
    valid &= std::isfinite(wind_speed_filter_tau_s_) && validate_positive("wind_speed_filter_tau_s", wind_speed_filter_tau_s_, 0.0);
    valid &= std::isfinite(wind_speed_rate_limit_mps_s_) && validate_positive("wind_speed_rate_limit_mps_s", wind_speed_rate_limit_mps_s_, 0.0);
    valid &= std::isfinite(wind_direction_rate_limit_deg_s_) && validate_positive("wind_direction_rate_limit_deg_s", wind_direction_rate_limit_deg_s_, 0.0);
    return valid;
}

double WindEngineNode::normalize_degrees(double angle) {
    if (!std::isfinite(angle)) {
        return 0.0;
    }
    angle = std::fmod(angle, 360.0);
    if (angle < 0.0) {
        angle += 360.0;
    }
    return angle;
}

double WindEngineNode::shortest_angle_delta_degrees(double target, double current) {
    double delta = normalize_degrees(target) - normalize_degrees(current);
    while (delta > 180.0) {
        delta -= 360.0;
    }
    while (delta < -180.0) {
        delta += 360.0;
    }
    return delta;
}

bool WindEngineNode::validate_nonnegative(const std::string& name, double value) const {
    if (!std::isfinite(value) || value < 0.0) {
        RCLCPP_ERROR(this->get_logger(), "[WindEngine] parameter %s = %.4f must be finite and >= 0",
            name.c_str(), value);
        return false;
    }
    return true;
}

bool WindEngineNode::parse_wind_source_mode(const std::string& mode, WindSourceMode& parsed) {
    std::string normalized = mode;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "auto") {
        parsed = WindSourceMode::Auto;
        return true;
    }
    if (normalized == "u10" || normalized == "explicit_u10") {
        parsed = WindSourceMode::U10;
        return true;
    }
    if (normalized == "anemometer" || normalized == "sensor") {
        parsed = WindSourceMode::Anemometer;
        return true;
    }
    return false;
}

const char* WindEngineNode::wind_source_mode_name(WindSourceMode mode) {
    switch (mode) {
        case WindSourceMode::Auto: return "auto";
        case WindSourceMode::U10: return "u10";
        case WindSourceMode::Anemometer: return "anemometer";
    }
    return "unknown";
}

const char* WindEngineNode::effective_wind_source_name(EffectiveWindSource source) {
    switch (source) {
        case EffectiveWindSource::None: return "none";
        case EffectiveWindSource::U10: return "u10";
        case EffectiveWindSource::Anemometer: return "anemometer";
    }
    return "unknown";
}

bool WindEngineNode::is_input_available(
    bool received, const rclcpp::Time& stamp, const rclcpp::Time& now) const {
    if (!received) {
        return false;
    }
    if (stamp.nanoseconds() == 0) {
        return true;
    }
    return (now - stamp).seconds() <= wind_input_timeout_s_;
}

bool WindEngineNode::select_wind_source_locked() {
    const auto now = this->now();
    const bool explicit_available = is_input_available(explicit_u10_received_, last_explicit_u10_time_, now);
    const bool anemometer_available = is_input_available(anemometer_received_, last_anemometer_time_, now);

    double selected_u10 = 0.0;
    double selected_direction = wind_direction_;
    bool selected_from_anemometer = false;
    EffectiveWindSource selected_source = EffectiveWindSource::None;

    const auto select_explicit = [&]() {
        selected_u10 = explicit_u10_;
        selected_direction = explicit_wind_direction_;
        selected_from_anemometer = false;
        selected_source = EffectiveWindSource::U10;
    };
    const auto select_anemometer = [&]() {
        selected_u10 = anemometer_u10_;
        selected_direction = anemometer_wind_direction_;
        selected_from_anemometer = true;
        selected_source = EffectiveWindSource::Anemometer;
    };

    if (wind_source_mode_ == WindSourceMode::U10) {
        if (explicit_available) {
            select_explicit();
        } else {
            selected_direction = explicit_wind_direction_;
        }
    } else if (wind_source_mode_ == WindSourceMode::Anemometer) {
        if (anemometer_available) {
            select_anemometer();
        } else {
            selected_direction = anemometer_wind_direction_;
        }
    } else {
        if (explicit_available) {
            select_explicit();
        } else if (anemometer_available) {
            select_anemometer();
        } else if (explicit_u10_received_) {
            selected_direction = explicit_wind_direction_;
        } else if (anemometer_received_) {
            selected_direction = anemometer_wind_direction_;
        }
    }

    const double selected_direction_norm = normalize_degrees(selected_direction);
    const bool changed =
        std::abs(target_u10_ - selected_u10) > 1.0e-9 ||
        std::abs(shortest_angle_delta_degrees(selected_direction_norm, target_wind_direction_)) > 1.0e-9 ||
        use_anemometer_ != selected_from_anemometer ||
        effective_wind_source_ != selected_source;

    target_u10_ = selected_u10;
    target_wind_direction_ = selected_direction_norm;
    if (!wind_input_filter_enabled_ || !wind_filter_initialized_) {
        u10_ = target_u10_;
        wind_direction_ = target_wind_direction_;
        wind_filter_initialized_ = true;
    }
    use_anemometer_ = selected_from_anemometer;
    effective_wind_source_ = selected_source;
    return changed;
}

bool WindEngineNode::apply_wind_input_filter_locked(double dt) {
    if (!std::isfinite(dt) || dt <= 0.0) {
        dt = 1.0 / DEFAULT_TIMER_HZ;
    }

    if (!wind_input_filter_enabled_) {
        const bool changed =
            std::abs(u10_ - target_u10_) > 1.0e-9 ||
            std::abs(shortest_angle_delta_degrees(target_wind_direction_, wind_direction_)) > 1.0e-9;
        u10_ = target_u10_;
        wind_direction_ = target_wind_direction_;
        wind_filter_initialized_ = true;
        return changed;
    }

    if (!wind_filter_initialized_) {
        u10_ = target_u10_;
        wind_direction_ = target_wind_direction_;
        wind_filter_initialized_ = true;
        return true;
    }

    const double tau = std::max(0.1, wind_speed_filter_tau_s_);
    const double alpha = std::clamp(dt / tau, 0.0, 1.0);
    const double speed_step = (target_u10_ - u10_) * alpha;
    const double max_speed_step = std::max(0.0, wind_speed_rate_limit_mps_s_) * dt;
    const double limited_speed_step = std::clamp(speed_step, -max_speed_step, max_speed_step);

    const double direction_delta = shortest_angle_delta_degrees(target_wind_direction_, wind_direction_);
    const double max_dir_step = std::max(0.0, wind_direction_rate_limit_deg_s_) * dt;
    const double limited_dir_step = std::clamp(direction_delta, -max_dir_step, max_dir_step);

    const bool changed =
        std::abs(limited_speed_step) > 1.0e-6 ||
        std::abs(limited_dir_step) > 1.0e-4;
    u10_ = std::max(0.0, u10_ + limited_speed_step);
    wind_direction_ = normalize_degrees(wind_direction_ + limited_dir_step);
    return changed;
}

void WindEngineNode::init_coefficient_tables() {
    // OCIMF 2010 风力系数表
    // 角度: 0°=顶风(船首来), 90°=右舷横风, 180°=顺风
    const std::array<WindCoeffEntry, WIND_COEFF_TABLE_SIZE> ocimpf_table = { {
        {  0.0, -0.60,  0.00,  0.000},
        { 10.0, -0.58,  0.06,  0.010},
        { 20.0, -0.50,  0.18,  0.030},
        { 30.0, -0.40,  0.30,  0.040},
        { 40.0, -0.28,  0.42,  0.055},
        { 50.0, -0.14,  0.54,  0.065},
        { 60.0,  0.00,  0.65,  0.070},
        { 70.0,  0.10,  0.72,  0.068},
        { 80.0,  0.17,  0.77,  0.040},
        { 90.0,  0.20,  0.80,  0.000},
        {100.0,  0.17,  0.77, -0.040},
        {110.0,  0.10,  0.72, -0.068},
        {120.0,  0.00,  0.65, -0.070},
        {130.0, -0.14,  0.54, -0.065},
        {140.0, -0.28,  0.42, -0.055},
        {150.0, -0.40,  0.30, -0.040},
        {160.0, -0.50,  0.18, -0.030},
        {170.0, -0.58,  0.06, -0.010},
        {180.0, -0.50,  0.00,  0.000},
        {190.0, -0.42, -0.06,  0.010},
        {200.0, -0.32, -0.18,  0.030},
        {210.0, -0.25, -0.30,  0.040},
        {220.0, -0.14, -0.42,  0.055},
        {230.0,  0.00, -0.54,  0.065},
        {240.0,  0.14, -0.65,  0.070},
        {250.0,  0.22, -0.72,  0.068},
        {260.0,  0.28, -0.77,  0.040},
        {270.0,  0.35, -0.80,  0.000},
        {280.0,  0.28, -0.77, -0.040},
        {290.0,  0.22, -0.72, -0.068},
        {300.0,  0.14, -0.65, -0.070},
        {310.0,  0.00, -0.54, -0.065},
        {320.0, -0.14, -0.42, -0.055},
        {330.0, -0.25, -0.30, -0.040},
        {340.0, -0.32, -0.18, -0.030},
        {350.0, -0.45, -0.06, -0.010},
    } };

    coeff_table_ = ocimpf_table;

    RCLCPP_INFO(this->get_logger(),
        "[WindEngine] 风力系数表已加载 (OCIMF 2010, %zu 点)",
        WIND_COEFF_TABLE_SIZE);
}

void WindEngineNode::update_wind_speed() {
    if (u10_ < MIN_WIND_SPEED) {
        u_avg_z_ = 0.0;
        target_sigma_ = 0.0;
        for (auto& c : components_) {
            c.amplitude = 0.0;
        }
        return;
    }

    u_avg_z_ = u10_ * std::pow(z_c_ / 10.0, 0.12);
    double I_U = 0.06 * (1.0 + 0.043 * u10_) * std::pow(z_c_ / 10.0, -0.22);
    target_sigma_ = u_avg_z_ * I_U;
}

void WindEngineNode::init_spectrum() {
    const double df = 2.0 / static_cast<double>(WIND_SPECTRUM_COMPONENTS);
    const double L_scale = 100.0 * std::pow(z_c_ / 10.0, 0.3);
    double total_variance = 0.0;

    for (size_t i = 0; i < WIND_SPECTRUM_COMPONENTS; ++i) {
        double freq = 0.05 + i * df;
        double f_tilde = (freq * L_scale) / std::max(u10_, MIN_WIND_SPEED);

        double S_f = (320.0 * std::pow(u10_, 2) * std::pow(z_c_ / 10.0, 0.45)) /
                     std::pow(1.0 + 1.5 * f_tilde, 5.0 / 3.0);

        double energy_i = S_f * df;
        total_variance += energy_i;

        components_[i].freq = freq;
        components_[i].amplitude = std::sqrt(2.0 * energy_i);
        components_[i].phase = phase_dist_(rng_);
    }

    double current_sigma = std::sqrt(total_variance);
    if (current_sigma > 1e-10 && target_sigma_ > 1e-10) {
        double K = target_sigma_ / current_sigma;
        for (auto& c : components_) {
            c.amplitude *= K;
        }
    }

    RCLCPP_INFO(this->get_logger(),
        "[WindEngine] 频谱初始化完成 | 目标σ: %.4f m/s", target_sigma_);
}

std::tuple<double, double, double> WindEngineNode::interpolate_coeff(double angle,
    const std::array<WindCoeffEntry, WIND_COEFF_TABLE_SIZE>& table) {

    while (angle < 0.0) angle += 360.0;
    while (angle >= 360.0) angle -= 360.0;

    if (angle >= 350.0) {
        const auto& last  = table[table.size() - 1];
        const auto& first = table[0];
        double t = (angle - last.angle) / (360.0 - last.angle);
        return std::make_tuple(
            last.cx + t * (first.cx - last.cx),
            last.cy + t * (first.cy - last.cy),
            last.cn + t * (first.cn - last.cn));
    }

    for (size_t i = 0; i < table.size() - 1; ++i) {
        if (angle >= table[i].angle && angle < table[i + 1].angle) {
            double t = (angle - table[i].angle) / (table[i + 1].angle - table[i].angle);
            return std::make_tuple(
                table[i].cx + t * (table[i + 1].cx - table[i].cx),
                table[i].cy + t * (table[i + 1].cy - table[i].cy),
                table[i].cn + t * (table[i + 1].cn - table[i].cn));
        }
    }

    return std::make_tuple(0.0, 0.0, 0.0);
}

void WindEngineNode::calculate_step() {
    auto start_time = std::chrono::steady_clock::now();

    double u_total = u_avg_z_;
    double apparent_wind_speed = 0.0;
    double apparent_wind_dir = 0.0;
    geometry_msgs::msg::WrenchStamped msg;

    try {
        sim_time_ += 1.0 / DEFAULT_TIMER_HZ;

        {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            const bool source_changed = select_wind_source_locked();
            const bool filtered_changed = apply_wind_input_filter_locked(1.0 / DEFAULT_TIMER_HZ);
            const bool need_spectrum_refresh =
                source_changed ||
                std::abs(u10_ - last_spectrum_u10_) >= 0.25 ||
                (u10_ < MIN_WIND_SPEED && last_spectrum_u10_ >= MIN_WIND_SPEED) ||
                (u10_ >= MIN_WIND_SPEED && last_spectrum_u10_ < MIN_WIND_SPEED);
            if (filtered_changed || source_changed) {
                update_wind_speed();
                if (need_spectrum_refresh) {
                    init_spectrum();
                    last_spectrum_u10_ = u10_;
                }
            }
        }

        double delta_u = 0.0;
        {
            std::shared_lock<std::shared_mutex> lk(params_mutex_);
            for (const auto& c : components_) {
                delta_u += c.amplitude * std::cos(2.0 * M_PI * c.freq * sim_time_ + c.phase);
            }
            u_total = u_avg_z_ + delta_u;
            double u_min = u_avg_z_ * (1.0 - MAX_WIND_VARIATION);
            double u_max = u_avg_z_ * (1.0 + MAX_WIND_VARIATION);
            u_total = std::clamp(u_total, u_min, u_max);
        }

        double current_heading = 0.0;
        double local_Af = 0.0, local_Al = 0.0, local_Lpp = 0.0, local_z_c = 0.0;
        double local_roll_arm_param = 0.0;
        double local_u = 0.0, local_v = 0.0;
        double current_wind_direction = 0.0;
        bool direction_is_from = false;
        {
            std::shared_lock<std::shared_mutex> lk(params_mutex_);
            current_heading = ship_heading_;
            local_Af  = Af_;
            local_Al  = Al_;
            local_Lpp = Lpp_;
            local_z_c = z_c_;
            local_roll_arm_param = wind_roll_moment_arm_;
            local_u = current_u_;
            local_v = current_v_;
            current_wind_direction = wind_direction_;
            direction_is_from = wind_direction_is_from_;
        }

        double heading_rad = current_heading * DEG_TO_RAD;
        double wind_dir_rad = current_wind_direction * DEG_TO_RAD;
        if (direction_is_from) {
            wind_dir_rad += M_PI;
        }
        double U_w_x = u_total * std::cos(wind_dir_rad - heading_rad);
        double U_w_y = u_total * std::sin(wind_dir_rad - heading_rad);

        double U_rw_x = U_w_x - local_u;
        double U_rw_y = U_w_y - local_v;

        apparent_wind_speed = std::sqrt(U_rw_x * U_rw_x + U_rw_y * U_rw_y);
        apparent_wind_dir = std::atan2(U_rw_y, U_rw_x) * RAD_TO_DEG;

        if (!std::isfinite(apparent_wind_speed) || !std::isfinite(apparent_wind_dir)) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[WindEngine] 检测到NaN/Inf! 跳过本次计算");
            apparent_wind_speed = 0.0;
            apparent_wind_dir = 0.0;
        }

        while (apparent_wind_dir < 0.0)   apparent_wind_dir += 360.0;
        while (apparent_wind_dir >= 360.0) apparent_wind_dir -= 360.0;

        double q = 0.5 * air_density_ * apparent_wind_speed * apparent_wind_speed;

        auto [Cx, Cy, Cn] = interpolate_coeff(apparent_wind_dir, coeff_table_);

        msg.header.stamp = this->now();
        msg.header.frame_id = "base_link";

        msg.wrench.force.x  = q * Cx * local_Af;
        msg.wrench.force.y  = q * Cy * local_Al;
        const double roll_arm = (local_roll_arm_param < 0.0) ? local_z_c : local_roll_arm_param;
        msg.wrench.torque.x = -msg.wrench.force.y * roll_arm;
        msg.wrench.torque.z = q * Cn * local_Al * local_Lpp;

        publisher_->publish(msg);

        calc_count_++;
        min_wind_observed_ = std::min(min_wind_observed_, u_total);
        max_wind_observed_ = std::max(max_wind_observed_, u_total);

    } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "[WindEngine] 计算异常: %s", e.what());
    } catch (...) {
        RCLCPP_ERROR(this->get_logger(), "[WindEngine] 未知计算异常");
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;

    min_calc_time_ = std::min(min_calc_time_, duration);
    max_calc_time_ = std::max(max_calc_time_, duration);
    avg_calc_time_ = (avg_calc_time_ * calc_time_count_ + duration) / (calc_time_count_ + 1);
    calc_time_count_++;

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
        "[WindEngine] U=%.2f m/s | apparent=%.1f deg | Fx=%.1f N | Fy=%.1f N | Mx=%.1f Nm | Mz=%.1f Nm",
        apparent_wind_speed, apparent_wind_dir,
        msg.wrench.force.x, msg.wrench.force.y, msg.wrench.torque.x, msg.wrench.torque.z);

    if (calc_count_ % 100 == 0) {
        RCLCPP_INFO(this->get_logger(),
            "[WindEngine] 计算时间: min=%.2fms max=%.2fms avg=%.2fms",
            min_calc_time_, max_calc_time_, avg_calc_time_);
    }
}

void WindEngineNode::wind_params_callback(const geometry_msgs::msg::Vector3::SharedPtr msg) {
    double new_u10 = msg->x;

    if (!validate_nonnegative("u10 (callback)", new_u10)) {
        RCLCPP_WARN(this->get_logger(), "[WindEngine] 收到无效风力参数，忽略更新");
        return;
    }

    double new_direction = normalize_degrees(msg->y);
    if (!std::isfinite(msg->y)) {
        RCLCPP_WARN(this->get_logger(), "[WindEngine] 收到无效风向参数，忽略更新");
        return;
    }
    double new_anemometer_height = anemometer_height_;
    if (std::isfinite(msg->z) && msg->z > 0.1) {
        new_anemometer_height = msg->z;
    }

    EffectiveWindSource effective_source = EffectiveWindSource::None;
    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        explicit_u10_ = new_u10;
        explicit_wind_direction_ = new_direction;
        explicit_u10_received_ = true;
        last_explicit_u10_time_ = this->now();
        anemometer_height_ = new_anemometer_height;
        if (select_wind_source_locked() && !wind_input_filter_enabled_) {
            update_wind_speed();
            init_spectrum();
            last_spectrum_u10_ = u10_;
        }
        effective_source = effective_wind_source_;
    }

    RCLCPP_INFO(this->get_logger(),
        "[WindEngine] U10 input: U10=%.2f direction=%.1f height=%.1f effective=%s",
        new_u10, new_direction, new_anemometer_height, effective_wind_source_name(effective_source));
}

void WindEngineNode::anemometer_params_callback(const geometry_msgs::msg::Vector3::SharedPtr msg) {
    double wind_speed = msg->x;
    double wind_direction = normalize_degrees(msg->y);
    double measured_height = (std::isfinite(msg->z) && msg->z > 0.1) ? msg->z : anemometer_height_;

    if (!validate_nonnegative("anemometer speed", wind_speed) ||
        !std::isfinite(msg->y) ||
        !validate_positive("anemometer height", measured_height, 0.1)) {
        RCLCPP_WARN(this->get_logger(), "[WindEngine] 收到无效风速仪数据，忽略更新");
        return;
    }

    double estimated_u10 = wind_speed * std::pow(10.0 / measured_height, 0.12);
    EffectiveWindSource effective_source = EffectiveWindSource::None;
    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        anemometer_wind_speed_ = wind_speed;
        anemometer_wind_direction_ = wind_direction;
        anemometer_height_ = measured_height;
        anemometer_u10_ = estimated_u10;
        anemometer_received_ = true;
        last_anemometer_time_ = this->now();
        if (select_wind_source_locked() && !wind_input_filter_enabled_) {
            update_wind_speed();
            init_spectrum();
            last_spectrum_u10_ = u10_;
        }
        effective_source = effective_wind_source_;
    }

    RCLCPP_INFO(this->get_logger(),
        "[WindEngine] anemometer input: measured=%.2f m/s at %.2f m -> U10=%.2f m/s direction=%.1f effective=%s",
        wind_speed, measured_height, estimated_u10, wind_direction, effective_wind_source_name(effective_source));
}

void WindEngineNode::vessel_params_callback(const ship_interfaces::msg::VesselParams::SharedPtr msg) {
    double new_Lpp = msg->lpp;
    double new_Af = msg->af;
    double new_Al = msg->al;
    double new_anemometer_height = msg->anemometer_height;
    double new_z_center = msg->z_center;

    if (!validate_positive("Lpp", new_Lpp, 0.0) ||
        !validate_positive("Af", new_Af, 0.0) ||
        !validate_positive("Al", new_Al, 0.0) ||
        !validate_positive("anemometer_height", new_anemometer_height, 0.1) ||
        !validate_positive("z_center", new_z_center, 0.1)) {
        RCLCPP_WARN(this->get_logger(), "[WindEngine] 收到无效船舶参数，忽略更新");
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        Lpp_ = new_Lpp;
        Af_  = new_Af;
        Al_  = new_Al;
        anemometer_height_ = new_anemometer_height;
        z_c_ = new_z_center;
        if (anemometer_received_) {
            anemometer_u10_ = anemometer_wind_speed_ * std::pow(10.0 / anemometer_height_, 0.12);
        }
        if (select_wind_source_locked() && !wind_input_filter_enabled_) {
            update_wind_speed();
            init_spectrum();
            last_spectrum_u10_ = u10_;
        } else {
            update_wind_speed();
            init_spectrum();
            last_spectrum_u10_ = u10_;
        }
    }

    RCLCPP_INFO(this->get_logger(),
        "[WindEngine] 更新船舶参数: Lpp=%.2f Af=%.2f Al=%.2f z_center=%.2f",
        Lpp_, Af_, Al_, z_c_);
}

void WindEngineNode::heading_callback(const std_msgs::msg::Float64::SharedPtr msg) {
    double heading = msg->data;
    while (heading < 0.0)    heading += 360.0;
    while (heading >= 360.0) heading -= 360.0;
    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        ship_heading_ = heading;
    }
}

void WindEngineNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg) {
    double u = msg->twist.twist.linear.x;
    double v = msg->twist.twist.linear.y;

    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        current_u_ = u;
        current_v_ = v;
    }
}

rcl_interfaces::msg::SetParametersResult WindEngineNode::parameter_callback(
    const std::vector<rclcpp::Parameter>& parameters) {

    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;
    bool need_spectrum_update = false;
    bool need_source_update = false;

    for (const auto& param : parameters) {
        const auto& name = param.get_name();

        if (name == "wind_direction") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !std::isfinite(param.as_double())) {
                result.successful = false;
                result.reason = "wind_direction must be a finite double";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            explicit_wind_direction_ = normalize_degrees(param.as_double());
            need_source_update = true;
        } else if (name == "wind_direction_is_from") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_BOOL) {
                result.successful = false;
                result.reason = "wind_direction_is_from must be bool";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_direction_is_from_ = param.as_bool();
        } else if (name == "wind_source_mode") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_STRING) {
                result.successful = false;
                result.reason = "wind_source_mode must be a string: auto, u10, or anemometer";
                break;
            }
            WindSourceMode parsed_mode = WindSourceMode::Auto;
            if (!parse_wind_source_mode(param.as_string(), parsed_mode)) {
                result.successful = false;
                result.reason = "wind_source_mode must be auto, u10, or anemometer";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_source_mode_ = parsed_mode;
            if (wind_source_mode_ == WindSourceMode::U10) {
                explicit_u10_received_ = true;
            }
            need_source_update = true;
        } else if (name == "wind_input_timeout_s") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !std::isfinite(param.as_double()) || param.as_double() <= 0.0) {
                result.successful = false;
                result.reason = "wind_input_timeout_s must be finite and > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_input_timeout_s_ = std::max(0.1, param.as_double());
            need_source_update = true;
        } else if (name == "wind_input_filter_enabled") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_BOOL) {
                result.successful = false;
                result.reason = "wind_input_filter_enabled must be bool";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_input_filter_enabled_ = param.as_bool();
            need_source_update = true;
        } else if (name == "wind_speed_filter_tau_s") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !std::isfinite(param.as_double()) || param.as_double() <= 0.0) {
                result.successful = false;
                result.reason = "wind_speed_filter_tau_s must be finite and > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_speed_filter_tau_s_ = std::max(0.1, param.as_double());
        } else if (name == "wind_speed_rate_limit_mps_s") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !std::isfinite(param.as_double()) || param.as_double() <= 0.0) {
                result.successful = false;
                result.reason = "wind_speed_rate_limit_mps_s must be finite and > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_speed_rate_limit_mps_s_ = std::max(0.1, param.as_double());
        } else if (name == "wind_direction_rate_limit_deg_s") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !std::isfinite(param.as_double()) || param.as_double() <= 0.0) {
                result.successful = false;
                result.reason = "wind_direction_rate_limit_deg_s must be finite and > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_direction_rate_limit_deg_s_ = std::max(0.1, param.as_double());
        } else if (name == "u10") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_nonnegative("u10", param.as_double())) {
                result.successful = false;
                result.reason = "u10 must be finite and >= 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            explicit_u10_ = param.as_double();
            explicit_u10_received_ = true;
            last_explicit_u10_time_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
            need_source_update = true;
        } else if (name == "z_center") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_positive("z_center", param.as_double(), 0.1)) {
                result.successful = false;
                result.reason = "z_center must be > 0.1";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            z_c_ = param.as_double();
            need_spectrum_update = true;
        } else if (name == "anemometer_height") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_positive("anemometer_height", param.as_double(), 0.1)) {
                result.successful = false;
                result.reason = "anemometer_height must be > 0.1";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            anemometer_height_ = param.as_double();
            if (anemometer_received_) {
                anemometer_u10_ = anemometer_wind_speed_ * std::pow(10.0 / anemometer_height_, 0.12);
                need_source_update = true;
            }
        } else if (name == "Af") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_positive("Af", param.as_double(), 0.0)) {
                result.successful = false;
                result.reason = "Af must be > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            Af_ = param.as_double();
        } else if (name == "Al") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_positive("Al", param.as_double(), 0.0)) {
                result.successful = false;
                result.reason = "Al must be > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            Al_ = param.as_double();
        } else if (name == "Lpp") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_positive("Lpp", param.as_double(), 0.0)) {
                result.successful = false;
                result.reason = "Lpp must be > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            Lpp_ = param.as_double();
        } else if (name == "wind_roll_moment_arm") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !std::isfinite(param.as_double()) || param.as_double() < -1.0) {
                result.successful = false;
                result.reason = "wind_roll_moment_arm must be finite and >= -1";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wind_roll_moment_arm_ = param.as_double();
        } else if (name == "air_density") {
            if (param.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_positive("air_density", param.as_double(), 0.0)) {
                result.successful = false;
                result.reason = "air_density must be > 0";
                break;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            air_density_ = param.as_double();
        }
    }

    if (result.successful && need_source_update) {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        if (select_wind_source_locked() && !wind_input_filter_enabled_) {
            need_spectrum_update = true;
        }
    }

    if (result.successful && need_spectrum_update) {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        apply_wind_input_filter_locked(1.0 / DEFAULT_TIMER_HZ);
        update_wind_speed();
        init_spectrum();
        last_spectrum_u10_ = u10_;
    }

    return result;
}

} // namespace env_engines

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<env_engines::WindEngineNode>());
    rclcpp::shutdown();
    return 0;
}
