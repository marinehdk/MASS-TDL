/**
 * @file wave_engine_node.cpp
 * @brief Wave load engine with separated first-order and drift loads.
 */

#include "env_engines/wave_engine_node.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <mutex>

namespace env_engines {

WaveEngineNode::WaveEngineNode()
    : EnvEngineBase("wave_engine_node", true)
{
    this->declare_parameter("Lpp", DEFAULT_WAVE_LPP);
    this->declare_parameter("Los", DEFAULT_WAVE_LOS);
    this->declare_parameter("B", DEFAULT_WAVE_B);
    this->declare_parameter("T", DEFAULT_WAVE_DRAFT);
    this->declare_parameter("bow_angle_rad", DEFAULT_WAVE_BOW_ANGLE);
    this->declare_parameter("C_WL_aft", DEFAULT_WAVE_C_WL_AFT);
    this->declare_parameter("xLos", DEFAULT_WAVE_XLOS);
    this->declare_parameter("Hs", DEFAULT_WAVE_HS);
    this->declare_parameter("Tz", DEFAULT_WAVE_TZ);
    this->declare_parameter("direction_rad", DEFAULT_WAVE_DIRECTION);
    this->declare_parameter("use_fixed_heading", false);
    this->declare_parameter("fixed_heading", 0.0);
    this->declare_parameter("water_density", 1025.0);
    this->declare_parameter("water_depth", DEFAULT_WAVE_WATER_DEPTH);
    this->declare_parameter("gravity", 9.81);
    this->declare_parameter("fk_scale_factor", 0.1);
    this->declare_parameter("qtf_csv_path", std::string(""));
    this->declare_parameter("drift.model", std::string("inferred"));
    this->declare_parameter("drift.inferred_surge_scale",
                            DEFAULT_INFERRED_DRIFT_SURGE_SCALE);
    this->declare_parameter("drift.inferred_sway_scale",
                            DEFAULT_INFERRED_DRIFT_SWAY_SCALE);
    this->declare_parameter("drift.inferred_yaw_lever_scale",
                            DEFAULT_INFERRED_DRIFT_YAW_LEVER_SCALE);
    this->declare_parameter("drift.inferred_roll_lever_scale",
                            DEFAULT_INFERRED_DRIFT_ROLL_LEVER_SCALE);
    this->declare_parameter("spreading_factor", DEFAULT_WAVE_SPREADING_FACTOR);
    this->declare_parameter("direction_components", DEFAULT_WAVE_DIRECTION_COMPONENTS);
    this->declare_parameter("wave_source_mode", std::string("auto"));
    this->declare_parameter("wave_direction_is_from", DEFAULT_WAVE_DIRECTION_IS_FROM);
    this->declare_parameter("wave_input_timeout_s", DEFAULT_WAVE_INPUT_TIMEOUT_S);

    this->declare_parameter("vessel.KG", 7.0);
    this->declare_parameter("vessel.GM_T", 1.5);
    this->declare_parameter("vessel.displacement_ton", 50000.0);

    this->declare_parameter("rao.damping_ratio_heave", 0.10);
    this->declare_parameter("rao.damping_ratio_roll", 0.15);
    this->declare_parameter("rao.cutoff_freq_surge", 0.25);
    this->declare_parameter("rao.cutoff_freq_sway", 0.30);
    this->declare_parameter("rao.cutoff_freq_yaw", 0.20);
    this->declare_parameter("rao.scale_max", 3.0);
    this->declare_parameter("rao.scale_max_roll", 5.0);
    this->declare_parameter("rao.surge_scale", 1.0);
    this->declare_parameter("rao.sway_scale", 1.0);
    this->declare_parameter("rao.roll_scale", 1.0);
    this->declare_parameter("rao.yaw_scale", 1.0);

    vessel_params_.Lpp = this->get_parameter("Lpp").as_double();
    vessel_params_.Los = this->get_parameter("Los").as_double();
    vessel_params_.B = this->get_parameter("B").as_double();
    vessel_params_.T = this->get_parameter("T").as_double();
    vessel_params_.bow_angle_rad = this->get_parameter("bow_angle_rad").as_double();
    vessel_params_.C_WL_aft = this->get_parameter("C_WL_aft").as_double();
    vessel_params_.xLos = this->get_parameter("xLos").as_double();
    vessel_params_.KG = this->get_parameter("vessel.KG").as_double();
    vessel_params_.GM_T = this->get_parameter("vessel.GM_T").as_double();
    vessel_params_.displacement_ton = this->get_parameter("vessel.displacement_ton").as_double();
    displacement_kg_ = vessel_params_.displacement_ton * 1000.0;

    param_env_.Hs = this->get_parameter("Hs").as_double();
    param_env_.Tz = this->get_parameter("Tz").as_double();
    param_env_.direction_rad = normalize_radians(this->get_parameter("direction_rad").as_double());

    use_fixed_heading_ = this->get_parameter("use_fixed_heading").as_bool();
    fixed_heading_ = this->get_parameter("fixed_heading").as_double();
    water_density_ = this->get_parameter("water_density").as_double();
    water_depth_ =
        std::max(MIN_WAVE_WATER_DEPTH, this->get_parameter("water_depth").as_double());
    gravity_ = this->get_parameter("gravity").as_double();
    fk_scale_factor_ = this->get_parameter("fk_scale_factor").as_double();
    qtf_csv_path_ = this->get_parameter("qtf_csv_path").as_string();
    inferred_drift_surge_scale_ = std::clamp(
        this->get_parameter("drift.inferred_surge_scale").as_double(), 0.0, 1.0);
    inferred_drift_sway_scale_ = std::clamp(
        this->get_parameter("drift.inferred_sway_scale").as_double(), 0.0, 1.0);
    inferred_drift_yaw_lever_scale_ = std::clamp(
        this->get_parameter("drift.inferred_yaw_lever_scale").as_double(), 0.0, 1.0);
    inferred_drift_roll_lever_scale_ = std::clamp(
        this->get_parameter("drift.inferred_roll_lever_scale").as_double(), 0.0, 1.0);
    spreading_factor_ =
        std::clamp(this->get_parameter("spreading_factor").as_double(), 0.0, 1.0);
    direction_components_ = static_cast<int>(
        std::max<int64_t>(1, this->get_parameter("direction_components").as_int()));
    wave_direction_is_from_ = this->get_parameter("wave_direction_is_from").as_bool();
    wave_input_timeout_s_ =
        std::max(0.1, this->get_parameter("wave_input_timeout_s").as_double());

    WaveSourceMode parsed_mode = WaveSourceMode::Auto;
    if (!parse_wave_source_mode(this->get_parameter("wave_source_mode").as_string(), parsed_mode)) {
        RCLCPP_WARN(this->get_logger(), "[WaveEngine] invalid wave_source_mode; fallback to auto");
    }
    wave_source_mode_ = parsed_mode;

    WaveDriftModel parsed_drift_model = WaveDriftModel::Inferred;
    if (!parse_wave_drift_model(this->get_parameter("drift.model").as_string(),
                                parsed_drift_model)) {
        RCLCPP_WARN(this->get_logger(), "[WaveEngine] invalid drift.model; fallback to inferred");
    }
    wave_drift_model_ = parsed_drift_model;

    if (wave_direction_is_from_) {
        param_env_.direction_rad = normalize_radians(param_env_.direction_rad + M_PI);
    }
    topic_env_ = param_env_;

    rao_damping_heave_ = this->get_parameter("rao.damping_ratio_heave").as_double();
    rao_damping_roll_ = this->get_parameter("rao.damping_ratio_roll").as_double();
    rao_cutoff_surge_ = this->get_parameter("rao.cutoff_freq_surge").as_double();
    rao_cutoff_sway_ = this->get_parameter("rao.cutoff_freq_sway").as_double();
    rao_cutoff_yaw_ = this->get_parameter("rao.cutoff_freq_yaw").as_double();
    rao_scale_max_ = this->get_parameter("rao.scale_max").as_double();
    rao_scale_max_roll_ = this->get_parameter("rao.scale_max_roll").as_double();
    rao_surge_scale_ = std::clamp(this->get_parameter("rao.surge_scale").as_double(), 0.0, 10.0);
    rao_sway_scale_ = std::clamp(this->get_parameter("rao.sway_scale").as_double(), 0.0, 10.0);
    rao_roll_scale_ = std::clamp(this->get_parameter("rao.roll_scale").as_double(), 0.0, 10.0);
    rao_yaw_scale_ = std::clamp(this->get_parameter("rao.yaw_scale").as_double(), 0.0, 10.0);

    param_callback_handle_ = this->add_on_set_parameters_callback(
        std::bind(&WaveEngineNode::parameter_callback, this, std::placeholders::_1));

    if (!validate_vessel_params() || !validate_env_params(param_env_, "parameters")) {
        RCLCPP_WARN(this->get_logger(), "[WaveEngine] invalid initial parameters detected");
    }

    raw_publisher_ =
        this->create_publisher<geometry_msgs::msg::WrenchStamped>("/env/wave/raw_load", 10);
    drift_publisher_ =
        this->create_publisher<geometry_msgs::msg::WrenchStamped>("/env/wave/drift_load", 10);

    env_sub_ = this->create_subscription<geometry_msgs::msg::Vector3>(
        "/env/wave_params", 10,
        std::bind(&WaveEngineNode::env_params_callback, this, std::placeholders::_1));
    vessel_sub_ = this->create_subscription<ship_interfaces::msg::VesselParams>(
        "/env/vessel_params", 10,
        std::bind(&WaveEngineNode::vessel_params_callback, this, std::placeholders::_1));
    heading_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/ship/heading", 10,
        std::bind(&WaveEngineNode::heading_callback, this, std::placeholders::_1));
    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&WaveEngineNode::odom_callback, this, std::placeholders::_1));

    timer_ = this->create_wall_timer(
        std::chrono::milliseconds(1000 / DEFAULT_TIMER_HZ),
        std::bind(&WaveEngineNode::main_calc_loop, this));

    last_log_time_ = last_update_time_ = this->now();

    load_qtf_csv();
    rebuild_wave_spectrum_locked(param_env_, water_depth_);
    update_heave_natural_freq();
    update_roll_natural_freq();

    RCLCPP_INFO(this->get_logger(),
                "[WaveEngine] started | source=%s drift_model=%s timeout=%.1fs direction=%s depth=%.1fm spread=%.2f dirs=%d",
                wave_source_mode_name(wave_source_mode_),
                wave_drift_model_name(wave_drift_model_),
                wave_input_timeout_s_,
                wave_direction_is_from_ ? "from" : "to",
                water_depth_, spreading_factor_, direction_components_);
    RCLCPP_INFO(this->get_logger(),
                "[WaveEngine] inferred drift ship basis Lpp=%.2fm B=%.2fm T=%.2fm disp=%.1ft scales[x=%.3f y=%.3f n=%.3f roll=%.3f]",
                vessel_params_.Lpp, vessel_params_.B, vessel_params_.T,
                vessel_params_.displacement_ton,
                inferred_drift_surge_scale_, inferred_drift_sway_scale_,
                inferred_drift_yaw_lever_scale_, inferred_drift_roll_lever_scale_);
    RCLCPP_INFO(this->get_logger(),
                "[WaveEngine] topics: /env/wave/raw_load and /env/wave/drift_load");
}

double WaveEngineNode::normalize_radians(double angle)
{
    if (!std::isfinite(angle)) {
        return 0.0;
    }
    angle = std::fmod(angle, 2.0 * M_PI);
    if (angle < 0.0) {
        angle += 2.0 * M_PI;
    }
    return angle;
}

bool WaveEngineNode::parse_wave_source_mode(const std::string& mode, WaveSourceMode& parsed)
{
    std::string normalized = mode;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalized == "auto") {
        parsed = WaveSourceMode::Auto;
        return true;
    }
    if (normalized == "topic" || normalized == "sensor") {
        parsed = WaveSourceMode::Topic;
        return true;
    }
    if (normalized == "params" || normalized == "parameter" || normalized == "yaml") {
        parsed = WaveSourceMode::Params;
        return true;
    }
    return false;
}

const char* WaveEngineNode::wave_source_mode_name(WaveSourceMode mode)
{
    switch (mode) {
    case WaveSourceMode::Auto:
        return "auto";
    case WaveSourceMode::Topic:
        return "topic";
    case WaveSourceMode::Params:
        return "params";
    }
    return "unknown";
}

bool WaveEngineNode::parse_wave_drift_model(const std::string& mode, WaveDriftModel& parsed)
{
    std::string normalized = mode;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalized == "auto") {
        parsed = WaveDriftModel::Auto;
        return true;
    }
    if (normalized == "qtf_table" || normalized == "qtf" || normalized == "table") {
        parsed = WaveDriftModel::QtfTable;
        return true;
    }
    if (normalized == "inferred" || normalized == "semi_empirical" ||
        normalized == "semi-empirical" || normalized == "estimated") {
        parsed = WaveDriftModel::Inferred;
        return true;
    }
    return false;
}

const char* WaveEngineNode::wave_drift_model_name(WaveDriftModel mode)
{
    switch (mode) {
    case WaveDriftModel::Auto:
        return "auto";
    case WaveDriftModel::QtfTable:
        return "qtf_table";
    case WaveDriftModel::Inferred:
        return "inferred";
    }
    return "unknown";
}

bool WaveEngineNode::is_topic_wave_fresh(const rclcpp::Time& now) const
{
    return topic_wave_received_ &&
           (now - last_wave_input_time_).seconds() <= wave_input_timeout_s_;
}

EnvParams WaveEngineNode::selected_env_locked(const rclcpp::Time& now) const
{
    const bool topic_fresh = is_topic_wave_fresh(now);
    if (wave_source_mode_ == WaveSourceMode::Params) {
        return param_env_;
    }
    if (wave_source_mode_ == WaveSourceMode::Topic) {
        if (topic_fresh) {
            return topic_env_;
        }
        EnvParams calm;
        calm.Hs = 0.0;
        calm.Tz = DEFAULT_WAVE_TZ;
        calm.direction_rad = 0.0;
        return calm;
    }
    return topic_fresh ? topic_env_ : param_env_;
}

void WaveEngineNode::update_heave_natural_freq()
{
    const double C_wp = 0.85;
    const double Awp = C_wp * vessel_params_.Lpp * vessel_params_.B;
    const double kz = water_density_ * gravity_ * Awp;
    const double m = std::max(displacement_kg_, 1.0e3);
    rao_omega_n_heave_ = std::sqrt(std::max(kz / m, 1.0e-8));
}

void WaveEngineNode::update_roll_natural_freq()
{
    const double C_kxx = 0.4;
    const double k_xx = C_kxx * vessel_params_.B;
    if (k_xx < 1.0e-3 || vessel_params_.GM_T < 1.0e-4) {
        rao_omega_n_roll_ = 0.4;
        return;
    }
    rao_omega_n_roll_ = std::sqrt(gravity_ * vessel_params_.GM_T / (k_xx * k_xx));
}

double WaveEngineNode::calc_rao_heave(double omega) const
{
    const double r = omega / std::max(rao_omega_n_heave_, 1.0e-4);
    const double d = std::sqrt((1.0 - r * r) * (1.0 - r * r) +
                               4.0 * rao_damping_heave_ * rao_damping_heave_ * r * r);
    return std::min(1.0 / std::max(d, 1.0e-6), rao_scale_max_);
}

double WaveEngineNode::calc_rao_dp(double omega, double cutoff) const
{
    const double r = omega / std::max(cutoff, 1.0e-4);
    return std::min(1.0 / std::sqrt(1.0 + r * r), rao_scale_max_);
}

double WaveEngineNode::calc_rao_roll(double omega) const
{
    const double r = omega / std::max(rao_omega_n_roll_, 1.0e-4);
    if (r < 0.5) {
        return 1.0;
    }
    if (r <= 2.0) {
        const double d = std::sqrt((1.0 - r * r) * (1.0 - r * r) +
                                   4.0 * rao_damping_roll_ * rao_damping_roll_ * r * r);
        return std::min(1.0 / std::max(d, 1.0e-6), rao_scale_max_roll_);
    }
    return std::min(2.0 / (r * r), rao_scale_max_roll_);
}

double WaveEngineNode::solve_wave_number(double omega, double water_depth) const
{
    if (!std::isfinite(omega) || omega <= 0.0 || gravity_ <= 0.0) {
        return 0.0;
    }

    const double depth = std::max(MIN_WAVE_WATER_DEPTH, water_depth);
    const double omega2 = omega * omega;
    const double deep_water_k = omega2 / gravity_;
    if (deep_water_k * depth > 30.0) {
        return deep_water_k;
    }

    double k = std::max(omega / std::sqrt(std::max(gravity_ * depth, 1.0e-9)), 1.0e-6);
    for (int i = 0; i < 20; ++i) {
        const double kh = k * depth;
        const double tanh_kh = std::tanh(kh);
        const double sech2_kh = 1.0 - tanh_kh * tanh_kh;
        const double f = gravity_ * k * tanh_kh - omega2;
        const double df = gravity_ * tanh_kh + gravity_ * k * depth * sech2_kh;
        if (std::abs(df) < 1.0e-12) {
            break;
        }
        const double step = f / df;
        k = std::max(1.0e-8, k - step);
        if (std::abs(step) < 1.0e-10) {
            break;
        }
    }
    return k;
}

double WaveEngineNode::encounter_frequency(
    double omega, double wave_number, double forward_speed, double relative_wave_dir) const
{
    if (!std::isfinite(omega) || !std::isfinite(wave_number) ||
        !std::isfinite(forward_speed) || !std::isfinite(relative_wave_dir)) {
        return omega;
    }
    return omega - wave_number * forward_speed * std::cos(relative_wave_dir);
}

InferredDriftCoefficients WaveEngineNode::calc_inferred_drift_coefficients(
    const WaveComponent& component, double relative_wave_dir,
    const VesselParams& vessel) const
{
    InferredDriftCoefficients coeffs;
    if (vessel.Lpp <= 0.1 || vessel.B <= 0.1 || component.k <= 0.0 ||
        !std::isfinite(relative_wave_dir)) {
        return coeffs;
    }

    const double L = std::max(vessel.Lpp, 0.1);
    const double B = std::max(vessel.B, 0.1);
    const double T = std::max(vessel.T, 0.1);
    const double KG = std::max(vessel.KG, T);
    const double k = std::max(component.k, 1.0e-8);
    const double kL = std::clamp(k * L, 0.0, 60.0);
    const double kT = std::clamp(k * T, 0.0, 60.0);

    const double long_wave_build_up = (kL * kL) / (1.0 + kL * kL);
    const double short_wave_decay = 1.0 / std::sqrt(1.0 + std::pow(kL / 8.0, 4.0));
    const double draft_participation = std::sqrt(std::max(0.0, 1.0 - std::exp(-2.0 * kT)));
    const double frequency_shape =
        std::clamp(long_wave_build_up * short_wave_decay *
                   std::max(0.25, draft_participation),
                   0.0, 1.5);

    const double c = std::cos(relative_wave_dir);
    const double s = std::sin(relative_wave_dir);
    const double c_abs = std::abs(c);
    const double s_abs = std::abs(s);

    const double surge_per_amp2 =
        water_density_ * gravity_ * B * inferred_drift_surge_scale_ * frequency_shape;
    const double sway_per_amp2 =
        water_density_ * gravity_ * L * inferred_drift_sway_scale_ * frequency_shape;
    const double yaw_lever = inferred_drift_yaw_lever_scale_ * L;
    const double roll_lever =
        inferred_drift_roll_lever_scale_ * std::max(0.1, KG - 0.33 * T);

    coeffs.Fx_N_per_m2 = surge_per_amp2 * c * c_abs;
    coeffs.Fy_N_per_m2 = sway_per_amp2 * s * s_abs;
    coeffs.Mz_Nm_per_m2 = sway_per_amp2 * yaw_lever * c * s_abs;
    coeffs.Mx_Nm_per_m2 = sway_per_amp2 * roll_lever * s * s_abs;
    return coeffs;
}

double WaveEngineNode::directional_spreading(double delta, double spread_factor) const
{
    const double factor = std::clamp(spread_factor, 0.0, 1.0);
    if (factor < 1.0e-6) {
        return std::abs(delta) < 1.0e-9 ? 1.0 : 0.0;
    }
    const double half_spread = M_PI * factor;
    if (std::abs(delta) > half_spread) {
        return 0.0;
    }
    return std::pow(std::cos(delta / 2.0), 2.0);
}

std::vector<DirectionalSample> WaveEngineNode::direction_samples() const
{
    if (spreading_factor_ < 1.0e-6 || direction_components_ <= 1) {
        return {{0.0, 1.0}};
    }

    int count = std::max(3, direction_components_);
    if (count % 2 == 0) {
        ++count;
    }
    const double half_spread = M_PI * std::clamp(spreading_factor_, 0.0, 1.0);
    std::vector<DirectionalSample> samples;
    samples.reserve(static_cast<size_t>(count));

    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double t = count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.5;
        const double offset = -half_spread + 2.0 * half_spread * t;
        const double weight = directional_spreading(offset, spreading_factor_);
        samples.push_back({offset, weight});
        sum += weight;
    }

    if (sum <= 1.0e-12) {
        return {{0.0, 1.0}};
    }
    for (auto& sample : samples) {
        sample.weight /= sum;
    }
    return samples;
}

void WaveEngineNode::calc_first_order_loads(
    double gamma, double sim_time, const VesselParams& vessel,
    const std::vector<WaveComponent>& components,
    double forward_speed,
    double& Fx_1st, double& Fy_1st, double& Mx_1st, double& Mz_1st) const
{
    Fx_1st = 0.0;
    Fy_1st = 0.0;
    Mx_1st = 0.0;
    Mz_1st = 0.0;
    if (components.empty()) {
        return;
    }

    const auto dir_samples = direction_samples();
    for (const auto& comp : components) {
        for (const auto& sample : dir_samples) {
            const double gamma_dir = gamma + sample.offset_rad;
            const double amp = comp.amplitude * std::sqrt(std::max(sample.weight, 0.0));
            const double k = comp.k;
            const double T = vessel.T;

            const double H_surge = std::abs(std::cos(gamma_dir));
            const double H_sway = std::abs(std::sin(gamma_dir));
            const double H_roll = H_sway;
            const double H_yaw = 0.5 * std::abs(std::sin(2.0 * gamma_dir));
            const double omega_e = encounter_frequency(comp.omega, k, forward_speed, gamma_dir);
            const double omega_rao = std::max(std::abs(omega_e), 1.0e-4);

            const double e_half = std::exp(-k * T / 2.0);
            const double f_FK_sway =
                amp * water_density_ * gravity_ * vessel.B * vessel.Lpp * k * e_half;

            const double I_depth_roll = (1.0 - std::exp(-k * T)) / std::max(k, 1.0e-8);
            const double f_FK_roll_raw = amp * water_density_ * gravity_ *
                                         (vessel.B * vessel.B / 4.0) *
                                         vessel.Lpp * I_depth_roll;
            const double f_FK_roll = f_FK_roll_raw * 0.03;

            const double rao_surge = calc_rao_dp(omega_rao, rao_cutoff_surge_);
            const double rao_sway = calc_rao_dp(omega_rao, rao_cutoff_sway_);
            const double rao_roll = calc_rao_roll(omega_rao);
            const double rao_yaw = calc_rao_dp(omega_rao, rao_cutoff_yaw_);

            const double phase_t = omega_e * sim_time + comp.phase;
            const double cos_pt = std::cos(phase_t);
            const double sin_pt = std::sin(phase_t);

            Fx_1st += rao_surge_scale_ * fk_scale_factor_ *
                       f_FK_sway * H_surge * cos_pt * rao_surge;
            Fy_1st += rao_sway_scale_ * fk_scale_factor_ *
                       f_FK_sway * H_sway * cos_pt * rao_sway;
            Mx_1st += rao_roll_scale_ * f_FK_roll * H_roll * cos_pt * rao_roll;
            Mz_1st += rao_yaw_scale_ * fk_scale_factor_ *
                       f_FK_sway * vessel.Lpp * H_yaw * sin_pt * rao_yaw;
        }
    }
}

WaveLoadsSeparated WaveEngineNode::calculateWaveDriftForces(
    const EnvParams& env, const VesselParams& vessel,
    const std::vector<WaveComponent>& components,
    double current_heading_deg, double forward_speed, double sim_time)
{
    WaveLoadsSeparated loads;
    if (vessel.Lpp < 0.1 || vessel.B < 0.1 || env.Hs <= 0.01 || components.empty()) {
        return loads;
    }

    const double gamma = std::atan2(
        std::sin(env.direction_rad - current_heading_deg * DEG_TO_RAD),
        std::cos(env.direction_rad - current_heading_deg * DEG_TO_RAD));

    double Fx_2nd = 0.0;
    double Fy_2nd = 0.0;
    double Mx_2nd = 0.0;
    double Mz_2nd = 0.0;

    const bool use_qtf_table =
        qtf_table_loaded_ &&
        (wave_drift_model_ == WaveDriftModel::QtfTable ||
         wave_drift_model_ == WaveDriftModel::Auto);
    const bool use_inferred =
        wave_drift_model_ == WaveDriftModel::Inferred ||
        (wave_drift_model_ == WaveDriftModel::Auto && !qtf_table_loaded_);
    const char* drift_source = use_qtf_table ? "qtf_table" :
                               (use_inferred ? "inferred" : "none");

    if (use_qtf_table) {
        const auto dir_samples = direction_samples();
        for (const auto& comp : components) {
            for (const auto& sample : dir_samples) {
                const double gamma_dir = gamma + sample.offset_rad;
                double gamma_deg = gamma_dir * RAD_TO_DEG;
                while (gamma_deg < 0.0) {
                    gamma_deg += 360.0;
                }
                while (gamma_deg >= 360.0) {
                    gamma_deg -= 360.0;
                }

                const std::vector<double> qtfs =
                    hydro_parser_.interpolate_2d(comp.omega, gamma_deg);
                const double amp_sq = comp.amplitude * comp.amplitude *
                                      std::max(sample.weight, 0.0);
                if (qtfs.size() >= 3) {
                    Fx_2nd += qtfs[0] * 1000.0 * amp_sq;
                    Fy_2nd += qtfs[1] * 1000.0 * amp_sq;
                    Mz_2nd += qtfs[2] * 1000.0 * amp_sq;
                }
                if (qtfs.size() >= 4) {
                    Mx_2nd += qtfs[3] * 1000.0 * amp_sq;
                }
            }
        }
    } else if (use_inferred) {
        const auto dir_samples = direction_samples();
        for (const auto& comp : components) {
            for (const auto& sample : dir_samples) {
                const double gamma_dir = gamma + sample.offset_rad;
                const double amp_sq = comp.amplitude * comp.amplitude *
                                      std::max(sample.weight, 0.0);
                const InferredDriftCoefficients coeffs =
                    calc_inferred_drift_coefficients(comp, gamma_dir, vessel);
                Fx_2nd += coeffs.Fx_N_per_m2 * amp_sq;
                Fy_2nd += coeffs.Fy_N_per_m2 * amp_sq;
                Mx_2nd += coeffs.Mx_Nm_per_m2 * amp_sq;
                Mz_2nd += coeffs.Mz_Nm_per_m2 * amp_sq;
            }
        }
    } else {
        RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 5000,
            "[WaveEngine] drift.model=qtf_table but no valid QTF is loaded; drift load is zero");
    }

    double Fx_1st = 0.0;
    double Fy_1st = 0.0;
    double Mx_1st = 0.0;
    double Mz_1st = 0.0;
    calc_first_order_loads(gamma, sim_time, vessel, components, forward_speed,
                           Fx_1st, Fy_1st, Mx_1st, Mz_1st);

    loads.Fx_1st = Fx_1st;
    loads.Fy_1st = Fy_1st;
    loads.Mx_1st = Mx_1st;
    loads.Mz_1st = Mz_1st;
    loads.Fx_2nd = Fx_2nd;
    loads.Fy_2nd = Fy_2nd;
    loads.Mx_2nd = Mx_2nd;
    loads.Mz_2nd = Mz_2nd;

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                         "[WaveEngine] rel=%.1f deg drift_source=%s | 1st Fx=%.0f Fy=%.0f | drift Fx=%.0f Fy=%.0f Mz=%.0f",
                         gamma * RAD_TO_DEG, drift_source, loads.Fx_1st, loads.Fy_1st,
                         loads.Fx_2nd, loads.Fy_2nd, loads.Mz_2nd);
    return loads;
}

void WaveEngineNode::load_qtf_csv()
{
    if (qtf_csv_path_.empty()) {
        qtf_table_loaded_ = false;
        RCLCPP_WARN(this->get_logger(),
                    "[WaveEngine] qtf_csv_path is empty; QTF table unavailable, drift_model=%s",
                    wave_drift_model_name(wave_drift_model_));
        return;
    }

    RCLCPP_INFO(this->get_logger(), "[WaveEngine] loading QTF: %s", qtf_csv_path_.c_str());
    qtf_table_loaded_ = hydro_parser_.load_2d_csv(qtf_csv_path_);
    if (!qtf_table_loaded_) {
        RCLCPP_WARN(this->get_logger(),
                    "[WaveEngine] failed to load QTF; drift_model=%s",
                    wave_drift_model_name(wave_drift_model_));
    } else {
        RCLCPP_INFO(this->get_logger(), "[WaveEngine] QTF table loaded");
    }
}

void WaveEngineNode::rebuild_wave_spectrum_locked(const EnvParams& env, double water_depth)
{
    wave_components_.clear();
    spectrum_initialized_ = false;
    spectrum_hs_ = env.Hs;
    spectrum_tz_ = env.Tz;
    spectrum_depth_ = std::max(MIN_WAVE_WATER_DEPTH, water_depth);

    if (env.Hs <= 0.01 || env.Tz <= 0.1) {
        return;
    }

    const double Tp = env.Tz / 0.79;
    const double wp = 2.0 * M_PI / Tp;
    const double dw = (3.0 - 0.3) * wp / static_cast<double>(num_wave_components_);
    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<> phase_dist(0.0, 2.0 * M_PI);
    const double Ag = 1.0 - 0.287 * std::log(jonswap_gamma_);

    for (int i = 0; i < num_wave_components_; ++i) {
        const double omega = 0.3 * wp + (static_cast<double>(i) + 0.5) * dw;
        const double sigma = (omega <= wp) ? 0.07 : 0.09;
        const double eta = std::exp(-0.5 * std::pow((omega - wp) / (sigma * wp), 2.0));
        const double spm = (5.0 / 16.0) * std::pow(env.Hs, 2.0) *
                           std::pow(wp, 4.0) * std::pow(omega, -5.0) *
                           std::exp(-1.25 * std::pow(wp / omega, 4.0));
        const double spectrum = Ag * spm * std::pow(jonswap_gamma_, eta);

        WaveComponent component;
        component.amplitude = std::sqrt(std::max(0.0, 2.0 * spectrum * dw));
        component.omega = omega;
        component.k = solve_wave_number(omega, spectrum_depth_);
        component.phase = phase_dist(gen);
        wave_components_.push_back(component);
    }

    spectrum_initialized_ = true;
    RCLCPP_INFO(this->get_logger(),
                "[WaveEngine] JONSWAP rebuilt Hs=%.2f Tz=%.2f Tp=%.2f depth=%.1f N=%d",
                env.Hs, env.Tz, Tp, spectrum_depth_, num_wave_components_);
}

void WaveEngineNode::ensure_spectrum_for_env_locked(const EnvParams& env, double water_depth)
{
    if (!spectrum_initialized_ ||
        std::abs(spectrum_hs_ - env.Hs) > 0.05 ||
        std::abs(spectrum_tz_ - env.Tz) > 0.05 ||
        std::abs(spectrum_depth_ - water_depth) > 0.10) {
        rebuild_wave_spectrum_locked(env, water_depth);
    }
}

void WaveEngineNode::main_calc_loop()
{
    WaveLoadsSeparated result;
    bool ok = false;
    bool topic_fresh = false;
    EnvParams env;
    double forward_speed = 0.0;
    double water_depth = DEFAULT_WAVE_WATER_DEPTH;

    try {
        const rclcpp::Time now = this->now();
        double dt = (now - last_update_time_).seconds();
        if (dt <= 0.0 || dt > 1.0) {
            dt = 0.1;
        }
        last_update_time_ = now;

        VesselParams vessel;
        std::vector<WaveComponent> components;
        double heading = 0.0;
        double sim_time = 0.0;
        {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            topic_fresh = is_topic_wave_fresh(now);
            env = selected_env_locked(now);
            water_depth = water_depth_;
            ensure_spectrum_for_env_locked(env, water_depth);
            vessel = vessel_params_;
            components = wave_components_;
            heading = use_fixed_heading_ ? fixed_heading_ : ship_heading_;
            forward_speed = ship_u_;
            sim_time_ += dt;
            sim_time = sim_time_;
        }

        result = calculateWaveDriftForces(env, vessel, components, heading, forward_speed, sim_time);
        ok = true;
    } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "[WaveEngine] exception: %s", e.what());
    }

    if (!ok) {
        result = last_valid_result_;
    } else {
        last_valid_result_ = result;
    }

    auto raw_msg = geometry_msgs::msg::WrenchStamped();
    raw_msg.header.stamp = this->now();
    raw_msg.header.frame_id = "base_link";
    raw_msg.wrench.force.x = result.Fx_1st + result.Fx_2nd;
    raw_msg.wrench.force.y = result.Fy_1st + result.Fy_2nd;
    raw_msg.wrench.force.z = 0.0;
    raw_msg.wrench.torque.x = result.Mx_1st + result.Mx_2nd;
    raw_msg.wrench.torque.y = 0.0;
    raw_msg.wrench.torque.z = result.Mz_1st + result.Mz_2nd;
    raw_publisher_->publish(raw_msg);

    auto drift_msg = geometry_msgs::msg::WrenchStamped();
    drift_msg.header = raw_msg.header;
    drift_msg.wrench.force.x = result.Fx_2nd;
    drift_msg.wrench.force.y = result.Fy_2nd;
    drift_msg.wrench.force.z = 0.0;
    drift_msg.wrench.torque.x = result.Mx_2nd;
    drift_msg.wrench.torque.y = 0.0;
    drift_msg.wrench.torque.z = result.Mz_2nd;
    drift_publisher_->publish(drift_msg);

    ++calc_count_;
    const rclcpp::Time now = this->now();
    if ((now - last_log_time_).seconds() >= 1.0) {
        RCLCPP_INFO(this->get_logger(),
                    "[WaveEngine] source=%s drift_model=%s Hs=%.2f Tz=%.2f dir=%.1f deg U=%.2f depth=%.1f topic_fresh=%s",
                    wave_source_mode_name(wave_source_mode_),
                    wave_drift_model_name(wave_drift_model_),
                    env.Hs, env.Tz,
                    env.direction_rad * RAD_TO_DEG, forward_speed, water_depth,
                    topic_fresh ? "true" : "false");
        RCLCPP_INFO(this->get_logger(),
                    "[WaveEngine] 1st Fx=%.1f Fy=%.1f Mx=%.1f Mz=%.1f",
                    result.Fx_1st, result.Fy_1st, result.Mx_1st, result.Mz_1st);
        RCLCPP_INFO(this->get_logger(),
                    "[WaveEngine] drift Fx=%.1f Fy=%.1f Mx=%.1f Mz=%.1f",
                    result.Fx_2nd, result.Fy_2nd, result.Mx_2nd, result.Mz_2nd);
        last_log_time_ = now;
    }
}

void WaveEngineNode::env_params_callback(const geometry_msgs::msg::Vector3::SharedPtr msg)
{
    EnvParams incoming;
    incoming.Hs = msg->x;
    incoming.Tz = msg->y;
    incoming.direction_rad = normalize_radians(msg->z);

    bool direction_is_from = false;
    {
        std::shared_lock<std::shared_mutex> lk(params_mutex_);
        direction_is_from = wave_direction_is_from_;
    }
    if (direction_is_from) {
        incoming.direction_rad = normalize_radians(incoming.direction_rad + M_PI);
    }

    if (!validate_env_params(incoming, "topic")) {
        RCLCPP_WARN(this->get_logger(), "[WaveEngine] invalid /env/wave_params ignored");
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        topic_env_ = incoming;
        topic_wave_received_ = true;
        last_wave_input_time_ = this->now();
    }

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                         "[WaveEngine] topic input Hs=%.2f Tz=%.2f dir=%.1f deg",
                         incoming.Hs, incoming.Tz, incoming.direction_rad * RAD_TO_DEG);
}

void WaveEngineNode::vessel_params_callback(const ship_interfaces::msg::VesselParams::SharedPtr msg)
{
    if (!validate_positive("Lpp", msg->lpp, 0.0) ||
        !validate_positive("B", msg->b, 0.0)) {
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        vessel_params_.Lpp = msg->lpp;
        vessel_params_.B = msg->b;
        vessel_params_.bow_angle_rad = msg->bow_angle_rad;
        vessel_params_.Los = msg->los;
        vessel_params_.C_WL_aft = msg->c_wl_aft;
        vessel_params_.xLos = msg->x_los;
        vessel_params_.T = (msg->t >= 0.1) ? msg->t : DEFAULT_WAVE_DRAFT;
        if (msg->d > MIN_WAVE_WATER_DEPTH) {
            water_depth_ = msg->d;
        }
        if (msg->displacement_ton > 0.0) {
            vessel_params_.displacement_ton = msg->displacement_ton;
            displacement_kg_ = msg->displacement_ton * 1000.0;
        }
        if (msg->kg > 0.0) {
            vessel_params_.KG = msg->kg;
        }
        if (msg->gm_t > 0.0) {
            vessel_params_.GM_T = msg->gm_t;
        }
        update_heave_natural_freq();
        update_roll_natural_freq();
    }
}

void WaveEngineNode::heading_callback(const std_msgs::msg::Float64::SharedPtr msg)
{
    double heading = msg->data;
    while (heading < 0.0) {
        heading += 360.0;
    }
    while (heading >= 360.0) {
        heading -= 360.0;
    }
    std::unique_lock<std::shared_mutex> lk(params_mutex_);
    ship_heading_ = heading;
}

void WaveEngineNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    std::unique_lock<std::shared_mutex> lk(params_mutex_);
    ship_u_ = msg->twist.twist.linear.x;
    ship_v_ = msg->twist.twist.linear.y;
}

bool WaveEngineNode::validate_vessel_params()
{
    bool ok = true;
    ok &= validate_positive("Lpp", vessel_params_.Lpp, 0.0);
    ok &= validate_positive("B", vessel_params_.B, 0.0);
    ok &= validate_positive("T", vessel_params_.T, 0.0);
    ok &= validate_positive("water_density", water_density_, 0.0);
    ok &= validate_positive("water_depth", water_depth_, MIN_WAVE_WATER_DEPTH);
    ok &= validate_positive("gravity", gravity_, 0.0);
    ok &= std::isfinite(vessel_params_.GM_T);
    ok &= std::isfinite(vessel_params_.KG);
    return ok;
}

bool WaveEngineNode::validate_env_params(const EnvParams& env, const std::string& source)
{
    bool ok = true;
    ok &= std::isfinite(env.Hs) &&
          validate_range(source + ".Hs", env.Hs, MIN_WAVE_HS, MAX_WAVE_HS);
    ok &= std::isfinite(env.Tz) &&
          validate_range(source + ".Tz", env.Tz, MIN_WAVE_TZ, MAX_WAVE_TZ);
    ok &= std::isfinite(env.direction_rad);
    return ok;
}

rcl_interfaces::msg::SetParametersResult
WaveEngineNode::parameter_callback(const std::vector<rclcpp::Parameter>& parameters)
{
    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;

    for (const auto& p : parameters) {
        const std::string& name = p.get_name();
        if (name == "Hs") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_range("Hs", p.as_double(), MIN_WAVE_HS, MAX_WAVE_HS)) {
                result.successful = false;
                result.reason = "Hs must be a finite double in range";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            param_env_.Hs = p.as_double();
        } else if (name == "Tz") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                !validate_range("Tz", p.as_double(), MIN_WAVE_TZ, MAX_WAVE_TZ)) {
                result.successful = false;
                result.reason = "Tz must be a finite double in range";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            param_env_.Tz = p.as_double();
        } else if (name == "direction_rad") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE) {
                result.successful = false;
                result.reason = "direction_rad must be a double";
                continue;
            }
            double direction = normalize_radians(p.as_double());
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            if (wave_direction_is_from_) {
                direction = normalize_radians(direction + M_PI);
            }
            param_env_.direction_rad = direction;
        } else if (name == "wave_direction_is_from") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_BOOL) {
                result.successful = false;
                result.reason = "wave_direction_is_from must be bool";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wave_direction_is_from_ = p.as_bool();
        } else if (name == "wave_input_timeout_s") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                p.as_double() <= 0.0 || !std::isfinite(p.as_double())) {
                result.successful = false;
                result.reason = "wave_input_timeout_s must be finite and > 0";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wave_input_timeout_s_ = std::max(0.1, p.as_double());
        } else if (name == "wave_source_mode") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_STRING) {
                result.successful = false;
                result.reason = "wave_source_mode must be auto, topic, or params";
                continue;
            }
            WaveSourceMode parsed_mode;
            if (!parse_wave_source_mode(p.as_string(), parsed_mode)) {
                result.successful = false;
                result.reason = "wave_source_mode must be auto, topic, or params";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wave_source_mode_ = parsed_mode;
        } else if (name == "drift.model") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_STRING) {
                result.successful = false;
                result.reason = "drift.model must be inferred, qtf_table, or auto";
                continue;
            }
            WaveDriftModel parsed_model;
            if (!parse_wave_drift_model(p.as_string(), parsed_model)) {
                result.successful = false;
                result.reason = "drift.model must be inferred, qtf_table, or auto";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            wave_drift_model_ = parsed_model;
        } else if (name == "qtf_csv_path") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_STRING) {
                result.successful = false;
                result.reason = "qtf_csv_path must be a string";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            qtf_csv_path_ = p.as_string();
            load_qtf_csv();
        } else if (name == "drift.inferred_surge_scale") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            inferred_drift_surge_scale_ = std::clamp(p.as_double(), 0.0, 1.0);
        } else if (name == "drift.inferred_sway_scale") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            inferred_drift_sway_scale_ = std::clamp(p.as_double(), 0.0, 1.0);
        } else if (name == "drift.inferred_yaw_lever_scale") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            inferred_drift_yaw_lever_scale_ = std::clamp(p.as_double(), 0.0, 1.0);
        } else if (name == "drift.inferred_roll_lever_scale") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            inferred_drift_roll_lever_scale_ = std::clamp(p.as_double(), 0.0, 1.0);
        } else if (name == "use_fixed_heading") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            use_fixed_heading_ = p.as_bool();
        } else if (name == "fixed_heading") {
            double heading = p.as_double();
            while (heading < 0.0) {
                heading += 360.0;
            }
            while (heading >= 360.0) {
                heading -= 360.0;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            fixed_heading_ = heading;
        } else if (name == "spreading_factor") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            spreading_factor_ = std::clamp(p.as_double(), 0.0, 1.0);
        } else if (name == "direction_components") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            direction_components_ = static_cast<int>(std::max<int64_t>(1, p.as_int()));
        } else if (name == "water_depth") {
            if (p.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                p.as_double() <= MIN_WAVE_WATER_DEPTH || !std::isfinite(p.as_double())) {
                result.successful = false;
                result.reason = "water_depth must be finite and greater than MIN_WAVE_WATER_DEPTH";
                continue;
            }
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            water_depth_ = p.as_double();
        } else if (name == "rao.damping_ratio_heave") {
            rao_damping_heave_ = std::clamp(p.as_double(), 0.01, 0.50);
        } else if (name == "rao.damping_ratio_roll") {
            rao_damping_roll_ = std::clamp(p.as_double(), 0.01, 0.50);
        } else if (name == "rao.cutoff_freq_surge") {
            rao_cutoff_surge_ = std::max(p.as_double(), 0.01);
        } else if (name == "rao.cutoff_freq_sway") {
            rao_cutoff_sway_ = std::max(p.as_double(), 0.01);
        } else if (name == "rao.cutoff_freq_yaw") {
            rao_cutoff_yaw_ = std::max(p.as_double(), 0.01);
        } else if (name == "rao.scale_max") {
            rao_scale_max_ = std::max(p.as_double(), 1.0);
        } else if (name == "rao.scale_max_roll") {
            rao_scale_max_roll_ = std::max(p.as_double(), 1.0);
        } else if (name == "rao.surge_scale") {
            rao_surge_scale_ = std::clamp(p.as_double(), 0.0, 10.0);
        } else if (name == "rao.sway_scale") {
            rao_sway_scale_ = std::clamp(p.as_double(), 0.0, 10.0);
        } else if (name == "rao.roll_scale") {
            rao_roll_scale_ = std::clamp(p.as_double(), 0.0, 10.0);
        } else if (name == "rao.yaw_scale") {
            rao_yaw_scale_ = std::clamp(p.as_double(), 0.0, 10.0);
        } else if (name == "vessel.KG") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            vessel_params_.KG = std::max(p.as_double(), 0.1);
        } else if (name == "vessel.GM_T") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            vessel_params_.GM_T = std::max(p.as_double(), 0.01);
            update_roll_natural_freq();
        } else if (name == "vessel.displacement_ton") {
            std::unique_lock<std::shared_mutex> lk(params_mutex_);
            vessel_params_.displacement_ton = std::max(p.as_double(), 1.0);
            displacement_kg_ = vessel_params_.displacement_ton * 1000.0;
            update_heave_natural_freq();
        }
    }

    return result;
}

} // namespace env_engines

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<env_engines::WaveEngineNode>());
    rclcpp::shutdown();
    return 0;
}
