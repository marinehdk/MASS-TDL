/**
 * @file current_engine_node.cpp
 * @brief Current load engine for 4-DOF environmental loads.
 */

#include "env_engines/current_engine_node.hpp"

#include <chrono>
#include <mutex>
#include <vector>

namespace env_engines {

constexpr double MIN_CCX = -2.0;
constexpr double MAX_CCX =  1.0;
constexpr double MIN_CCY = -2.0;
constexpr double MAX_CCY =  2.0;
constexpr double MIN_CMZ = -1.0;
constexpr double MAX_CMZ =  1.0;
constexpr double MIN_CMX = -2.0;
constexpr double MAX_CMX =  2.0;

CurrentEngineNode::CurrentEngineNode()
    : EnvEngineBase("current_engine_node", true)
{
    this->declare_parameter("Lpp", DEFAULT_CURR_LPP);
    this->declare_parameter("B", DEFAULT_CURR_B);
    this->declare_parameter("T", DEFAULT_CURR_T);
    this->declare_parameter("d", DEFAULT_CURR_D);
    this->declare_parameter("v_tide_surf", DEFAULT_CURR_V_TIDE);
    this->declare_parameter("dir_tide", DEFAULT_CURR_DIR_TIDE);
    this->declare_parameter("v_wind_surf", DEFAULT_CURR_V_WIND);
    this->declare_parameter("dir_wind", DEFAULT_CURR_DIR_WIND);
    this->declare_parameter("v_circ_surf", DEFAULT_CURR_V_CIRC);
    this->declare_parameter("dir_circ", DEFAULT_CURR_DIR_CIRC);
    this->declare_parameter("water_density", 1025.0);
    this->declare_parameter("coeffs_csv_path", std::string(""));
    this->declare_parameter("vessel.KG", DEFAULT_CURR_T * 0.65);
    this->declare_parameter("current_roll_moment_arm", DEFAULT_CURRENT_ROLL_MOMENT_ARM);
    this->declare_parameter("current_direction_is_from", DEFAULT_CURRENT_DIRECTION_IS_FROM);
    this->declare_parameter("current_input_timeout_s", DEFAULT_CURRENT_INPUT_TIMEOUT_S);
    this->declare_parameter("subtract_calm_water_damping", true);

    Lpp_ = this->get_parameter("Lpp").as_double();
    B_ = this->get_parameter("B").as_double();
    T_ = this->get_parameter("T").as_double();
    d_ = this->get_parameter("d").as_double();
    param_current_.v_tide_surf = this->get_parameter("v_tide_surf").as_double();
    param_current_.dir_tide = this->get_parameter("dir_tide").as_double();
    param_current_.v_wind_surf = this->get_parameter("v_wind_surf").as_double();
    param_current_.dir_wind = this->get_parameter("dir_wind").as_double();
    param_current_.v_circ_surf = this->get_parameter("v_circ_surf").as_double();
    param_current_.dir_circ = this->get_parameter("dir_circ").as_double();
    water_density_ = this->get_parameter("water_density").as_double();
    coeffs_csv_path_ = this->get_parameter("coeffs_csv_path").as_string();
    KG_ = this->get_parameter("vessel.KG").as_double();
    current_roll_moment_arm_ = this->get_parameter("current_roll_moment_arm").as_double();
    current_direction_is_from_ = this->get_parameter("current_direction_is_from").as_bool();
    current_input_timeout_s_ =
        std::max(0.1, this->get_parameter("current_input_timeout_s").as_double());
    subtract_calm_water_damping_ =
        this->get_parameter("subtract_calm_water_damping").as_bool();

    normalize_component(param_current_.v_tide_surf, param_current_.dir_tide,
                        current_direction_is_from_);
    normalize_component(param_current_.v_wind_surf, param_current_.dir_wind,
                        current_direction_is_from_);
    normalize_component(param_current_.v_circ_surf, param_current_.dir_circ,
                        current_direction_is_from_);
    topic_current_ = param_current_;

    load_coefficient_csv();

    if (!validate_vessel_params() || !validate_current_params()) {
        RCLCPP_WARN(this->get_logger(),
                    "[CurrentEngine] invalid initial parameters detected");
    }

    if (T_ >= d_) {
        RCLCPP_ERROR(this->get_logger(),
                     "[CurrentEngine] draft %.2f >= depth %.2f", T_, d_);
    }

    publisher_ =
        this->create_publisher<geometry_msgs::msg::WrenchStamped>("/env/current_load", 10);

    current_sub_ = this->create_subscription<ship_interfaces::msg::OceanCurrents>(
        "/env/ocean_currents", 10,
        std::bind(&CurrentEngineNode::ocean_currents_callback, this, std::placeholders::_1));
    vessel_sub_ = this->create_subscription<ship_interfaces::msg::VesselParams>(
        "/env/vessel_params", 10,
        std::bind(&CurrentEngineNode::vessel_params_callback, this, std::placeholders::_1));
    heading_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/ship/heading", 10,
        std::bind(&CurrentEngineNode::heading_callback, this, std::placeholders::_1));
    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&CurrentEngineNode::odom_callback, this, std::placeholders::_1));

    timer_ = this->create_wall_timer(
        std::chrono::milliseconds(1000 / DEFAULT_TIMER_HZ),
        std::bind(&CurrentEngineNode::main_calc_loop, this));

    last_log_time_ = this->now();
    RCLCPP_INFO(this->get_logger(),
                "[CurrentEngine] started | Lpp=%.1f B=%.1f T=%.1f KG=%.2f",
                Lpp_, B_, T_, KG_);
    RCLCPP_INFO(this->get_logger(),
                "[CurrentEngine] current convention=%s | input timeout=%.1fs | "
                "subtract calm-water damping=%s",
                current_direction_is_from_ ? "from" : "to",
                current_input_timeout_s_,
                subtract_calm_water_damping_ ? "true" : "false");
    RCLCPP_INFO(this->get_logger(),
                "[CurrentEngine] coefficient limits: Ccx[%.1f,%.1f] Ccy[%.1f,%.1f] "
                "Cmz[%.1f,%.1f] Cmx[%.1f,%.1f]",
                MIN_CCX, MAX_CCX, MIN_CCY, MAX_CCY, MIN_CMZ, MAX_CMZ, MIN_CMX, MAX_CMX);
}

double CurrentEngineNode::normalize_degrees(double angle)
{
    if (!std::isfinite(angle)) {
        return 0.0;
    }
    angle = std::fmod(angle, 360.0);
    if (angle < 0.0) {
        angle += 360.0;
    }
    return angle;
}

void CurrentEngineNode::normalize_component(
    double& speed, double& direction_deg, bool direction_is_from)
{
    if (!std::isfinite(speed)) {
        speed = 0.0;
    }
    if (!std::isfinite(direction_deg)) {
        direction_deg = 0.0;
    }
    if (speed < 0.0) {
        speed = -speed;
        direction_deg += 180.0;
    }
    if (direction_is_from) {
        direction_deg += 180.0;
    }
    direction_deg = normalize_degrees(direction_deg);
}

bool CurrentEngineNode::is_topic_current_fresh(const rclcpp::Time& now) const
{
    return current_topic_received_ &&
           (now - last_current_input_time_).seconds() <= current_input_timeout_s_;
}

CurrentComponents CurrentEngineNode::selected_current_locked(const rclcpp::Time& now) const
{
    return is_topic_current_fresh(now) ? topic_current_ : param_current_;
}

bool CurrentEngineNode::calculate_depth_averaged_currents(
    const CurrentComponents& current, double draft, double depth,
    double& v_t_avg, double& v_w_avg, double& v_c_avg)
{
    v_t_avg = 0.0;
    v_w_avg = 0.0;
    v_c_avg = 0.0;

    if (!std::isfinite(draft) || !std::isfinite(depth) ||
        draft < MIN_DRAFT || depth < MIN_DEPTH) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                              "[CurrentEngine] invalid draft/depth: T=%.2f d=%.2f",
                              draft, depth);
        return false;
    }

    const double safe_T = std::min(draft, depth * 0.95);
    if (draft >= depth) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] draft %.2f >= depth %.2f; using T=%.2f",
                             draft, depth, safe_T);
    }

    const double d17 = std::pow(depth, 1.0 / 7.0);
    const double d87 = std::pow(depth, 8.0 / 7.0);
    const double depth_minus_T = std::max(0.001, depth - safe_T);
    const double depth_minus_T87 = std::pow(depth_minus_T, 8.0 / 7.0);
    if (d17 > 1.0e-10 && safe_T > 1.0e-10) {
        v_t_avg = current.v_tide_surf * (7.0 / 8.0) *
                  (d87 - depth_minus_T87) / (safe_T * d17);
    }

    if (safe_T <= WIND_DRIFT_DEPTH) {
        const double v_w_keel = current.v_wind_surf *
            std::max(0.0, (WIND_DRIFT_DEPTH - safe_T) / WIND_DRIFT_DEPTH);
        v_w_avg = 0.5 * (current.v_wind_surf + v_w_keel);
    } else {
        v_w_avg = current.v_wind_surf * WIND_DRIFT_DEPTH / (2.0 * safe_T);
    }

    v_c_avg = current.v_circ_surf;
    return true;
}

bool CurrentEngineNode::calculate_horizontal_components(
    const CurrentComponents& current,
    double v_t_avg, double v_w_avg, double v_c_avg,
    double& vx, double& vy)
{
    const double tide_r = current.dir_tide * DEG_TO_RAD;
    const double wind_r = current.dir_wind * DEG_TO_RAD;
    const double circ_r = current.dir_circ * DEG_TO_RAD;

    vx = v_t_avg * std::cos(tide_r) +
         v_w_avg * std::cos(wind_r) +
         v_c_avg * std::cos(circ_r);
    vy = v_t_avg * std::sin(tide_r) +
         v_w_avg * std::sin(wind_r) +
         v_c_avg * std::sin(circ_r);
    return std::isfinite(vx) && std::isfinite(vy);
}

void CurrentEngineNode::load_coefficient_csv()
{
    RCLCPP_INFO(this->get_logger(),
                "[CurrentEngine] loading coefficients: %s", coeffs_csv_path_.c_str());
    coeff_table_loaded_ = hydro_parser_.load_1d_csv(coeffs_csv_path_);
    if (!coeff_table_loaded_) {
        RCLCPP_ERROR(this->get_logger(),
                     "[CurrentEngine] failed to load coefficients: %s",
                     coeffs_csv_path_.c_str());
    } else {
        RCLCPP_INFO(this->get_logger(), "[CurrentEngine] coefficient table loaded");
    }
}

void CurrentEngineNode::calculate_forces(
    double rel_flow_x, double rel_flow_y,
    double v_total_apparent, double apparent_curr_dir,
    double draft, double depth, double lpp, double beam,
    double kg, double water_density, double roll_moment_arm,
    geometry_msgs::msg::WrenchStamped& msg)
{
    msg.wrench = geometry_msgs::msg::Wrench();

    if (!std::isfinite(rel_flow_x) || !std::isfinite(rel_flow_y) ||
        !std::isfinite(v_total_apparent) || v_total_apparent < MIN_CURRENT_EPS) {
        return;
    }

    if (!coeff_table_loaded_) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] coefficient table unavailable; output zero load");
        return;
    }

    const double gamma = normalize_degrees(apparent_curr_dir);
    const std::vector<double> coeffs = hydro_parser_.interpolate_1d(gamma);
    if (coeffs.size() < 3) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] coefficient row has %zu columns; expected >= 3",
                             coeffs.size());
        return;
    }

    double Ccx = coeffs[0];
    double Ccy = coeffs[1];
    double Cmz = coeffs[2];
    double Cmx = 0.0;
    const bool has_cmx = coeffs.size() >= 4;
    if (has_cmx) {
        Cmx = coeffs[3];
    } else {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] CSV has no Cmx column; using Fy * roll arm");
    }

    if (Ccx < MIN_CCX || Ccx > MAX_CCX) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] Ccx %.3f outside [%.1f,%.1f]; clamping",
                             Ccx, MIN_CCX, MAX_CCX);
        Ccx = std::clamp(Ccx, MIN_CCX, MAX_CCX);
    }
    if (Ccy < MIN_CCY || Ccy > MAX_CCY) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] Ccy %.3f outside [%.1f,%.1f]; clamping",
                             Ccy, MIN_CCY, MAX_CCY);
        Ccy = std::clamp(Ccy, MIN_CCY, MAX_CCY);
    }
    if (Cmz < MIN_CMZ || Cmz > MAX_CMZ) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] Cmz %.3f outside [%.1f,%.1f]; clamping",
                             Cmz, MIN_CMZ, MAX_CMZ);
        Cmz = std::clamp(Cmz, MIN_CMZ, MAX_CMZ);
    }
    if (has_cmx && (Cmx < MIN_CMX || Cmx > MAX_CMX)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] Cmx %.3f outside [%.1f,%.1f]; clamping",
                             Cmx, MIN_CMX, MAX_CMX);
        Cmx = std::clamp(Cmx, MIN_CMX, MAX_CMX);
    }

    const double safe_T = std::min(draft, depth * 0.95);
    if (safe_T < MIN_DRAFT || lpp <= 0.0 || beam <= 0.0 || water_density <= 0.0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "[CurrentEngine] invalid force geometry; output zero load");
        return;
    }

    const double S_long = beam * safe_T;
    const double S_trans = lpp * safe_T;
    const double V2 = v_total_apparent * v_total_apparent;
    const double half_rho = 0.5 * water_density;

    const double Fx = half_rho * Ccx * S_long * V2;
    const double Fy = half_rho * Ccy * S_trans * V2;
    const double Mz = half_rho * Cmz * S_trans * lpp * V2;

    double Mx = 0.0;
    if (has_cmx) {
        Mx = half_rho * Cmx * S_trans * lpp * V2;
    } else {
        const double auto_arm = kg - safe_T / 3.0;
        if (auto_arm < 0.0) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                                 "[CurrentEngine] auto roll arm KG-T/3 is negative: %.3f m",
                                 auto_arm);
        }
        const double z_arm = (roll_moment_arm >= 0.0) ? roll_moment_arm : auto_arm;
        Mx = Fy * z_arm;
    }

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                         "[CurrentEngine] gamma=%.1f deg V=%.2f Ccx=%.3f Ccy=%.3f Cmz=%.3f",
                         gamma, v_total_apparent, Ccx, Ccy, Cmz);

    msg.wrench.force.x = Fx;
    msg.wrench.force.y = Fy;
    msg.wrench.torque.x = Mx;
    msg.wrench.torque.z = Mz;
}

void CurrentEngineNode::main_calc_loop()
{
    CurrentComponents current;
    double local_hdg = 0.0;
    double local_u = 0.0;
    double local_v = 0.0;
    double local_T = 0.0;
    double local_d = 0.0;
    double local_Lpp = 0.0;
    double local_B = 0.0;
    double local_KG = 0.0;
    double local_rho = 0.0;
    double local_roll_arm = 0.0;
    bool local_subtract = true;
    bool topic_fresh = false;
    const rclcpp::Time now = this->now();

    {
        std::shared_lock<std::shared_mutex> lk(params_mutex_);
        topic_fresh = is_topic_current_fresh(now);
        current = selected_current_locked(now);
        local_hdg = ship_heading_;
        local_u = current_u_;
        local_v = current_v_;
        local_T = T_;
        local_d = d_;
        local_Lpp = Lpp_;
        local_B = B_;
        local_KG = KG_;
        local_rho = water_density_;
        local_roll_arm = current_roll_moment_arm_;
        local_subtract = subtract_calm_water_damping_;
    }

    double v_t = 0.0;
    double v_w = 0.0;
    double v_c = 0.0;
    if (!calculate_depth_averaged_currents(current, local_T, local_d, v_t, v_w, v_c)) {
        return;
    }

    double vx = 0.0;
    double vy = 0.0;
    if (!calculate_horizontal_components(current, v_t, v_w, v_c, vx, vy)) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                              "[CurrentEngine] current vector synthesis failed");
        return;
    }

    const double hdg_r = local_hdg * DEG_TO_RAD;
    const double v_x_ship = vx * std::cos(hdg_r) + vy * std::sin(hdg_r);
    const double v_y_ship = -vx * std::sin(hdg_r) + vy * std::cos(hdg_r);

    // This is the current velocity relative to the hull in body axes.
    // Fossen-style hydrodynamic damping typically uses nu_r = nu - nu_c;
    // the coefficient table here is keyed by the opposite apparent inflow.
    const double rel_flow_x = v_x_ship - local_u;
    const double rel_flow_y = v_y_ship - local_v;
    const double v_app = std::sqrt(rel_flow_x * rel_flow_x + rel_flow_y * rel_flow_y);
    const double app_dir = normalize_degrees(std::atan2(rel_flow_y, rel_flow_x) * RAD_TO_DEG);

    auto msg = geometry_msgs::msg::WrenchStamped();
    msg.header.stamp = now;
    msg.header.frame_id = "base_link";
    calculate_forces(rel_flow_x, rel_flow_y, v_app, app_dir,
                     local_T, local_d, local_Lpp, local_B, local_KG,
                     local_rho, local_roll_arm, msg);

    if (local_subtract) {
        const double calm_rel_flow_x = -local_u;
        const double calm_rel_flow_y = -local_v;
        const double calm_app =
            std::sqrt(calm_rel_flow_x * calm_rel_flow_x + calm_rel_flow_y * calm_rel_flow_y);
        const double calm_dir =
            normalize_degrees(std::atan2(calm_rel_flow_y, calm_rel_flow_x) * RAD_TO_DEG);

        auto calm_msg = geometry_msgs::msg::WrenchStamped();
        calm_msg.header = msg.header;
        calculate_forces(calm_rel_flow_x, calm_rel_flow_y, calm_app, calm_dir,
                         local_T, local_d, local_Lpp, local_B, local_KG,
                         local_rho, local_roll_arm, calm_msg);

        msg.wrench.force.x -= calm_msg.wrench.force.x;
        msg.wrench.force.y -= calm_msg.wrench.force.y;
        msg.wrench.force.z -= calm_msg.wrench.force.z;
        msg.wrench.torque.x -= calm_msg.wrench.torque.x;
        msg.wrench.torque.y -= calm_msg.wrench.torque.y;
        msg.wrench.torque.z -= calm_msg.wrench.torque.z;
    }

    publisher_->publish(msg);
    ++calc_count_;

    if ((now - last_log_time_).seconds() >= 1.0) {
        const double global_speed = std::sqrt(vx * vx + vy * vy);
        const double global_dir = normalize_degrees(std::atan2(vy, vx) * RAD_TO_DEG);
        double body_dir = global_dir - local_hdg;
        while (body_dir < -180.0) {
            body_dir += 360.0;
        }
        while (body_dir >= 180.0) {
            body_dir -= 360.0;
        }
        RCLCPP_INFO(this->get_logger(),
                    "[CurrentEngine] source=%s V=%.2f m/s global=%.1f deg body=%.1f deg",
                    topic_fresh ? "topic" : "params", global_speed, global_dir, body_dir);
        RCLCPP_INFO(this->get_logger(),
                    "[CurrentEngine] components tide=%.2f@%.1f wind=%.2f@%.1f circ=%.2f@%.1f",
                    current.v_tide_surf, current.dir_tide,
                    current.v_wind_surf, current.dir_wind,
                    current.v_circ_surf, current.dir_circ);
        RCLCPP_INFO(this->get_logger(),
                    "[CurrentEngine] Fx=%.1f N Fy=%.1f N Mx=%.1f N*m Mz=%.1f N*m",
                    msg.wrench.force.x, msg.wrench.force.y,
                    msg.wrench.torque.x, msg.wrench.torque.z);
        last_log_time_ = now;
    }
}

void CurrentEngineNode::ocean_currents_callback(
    const ship_interfaces::msg::OceanCurrents::SharedPtr msg)
{
    CurrentComponents incoming;
    incoming.v_tide_surf = msg->v_tide;
    incoming.dir_tide = msg->dir_tide;
    incoming.v_wind_surf = msg->v_wind;
    incoming.dir_wind = msg->dir_wind;
    incoming.v_circ_surf = msg->v_circ;
    incoming.dir_circ = msg->dir_circ;

    bool direction_is_from = false;
    {
        std::shared_lock<std::shared_mutex> lk(params_mutex_);
        direction_is_from = current_direction_is_from_;
    }

    normalize_component(incoming.v_tide_surf, incoming.dir_tide, direction_is_from);
    normalize_component(incoming.v_wind_surf, incoming.dir_wind, direction_is_from);
    normalize_component(incoming.v_circ_surf, incoming.dir_circ, direction_is_from);

    if (!validate_current_params(incoming, "topic")) {
        RCLCPP_WARN(this->get_logger(), "[CurrentEngine] invalid /env/ocean_currents ignored");
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        topic_current_ = incoming;
        current_topic_received_ = true;
        last_current_input_time_ = this->now();
    }

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                         "[CurrentEngine] topic input tide=%.2f@%.1f wind=%.2f@%.1f circ=%.2f@%.1f",
                         incoming.v_tide_surf, incoming.dir_tide,
                         incoming.v_wind_surf, incoming.dir_wind,
                         incoming.v_circ_surf, incoming.dir_circ);
}

void CurrentEngineNode::vessel_params_callback(
    const ship_interfaces::msg::VesselParams::SharedPtr msg)
{
    if (!validate_positive("Lpp", msg->lpp, 0.0) ||
        !validate_positive("B", msg->b, 0.0) ||
        !validate_positive("T", msg->t, MIN_DRAFT) ||
        !validate_positive("d", msg->d, MIN_DEPTH)) {
        RCLCPP_WARN(this->get_logger(), "[CurrentEngine] invalid vessel parameters ignored");
        return;
    }
    if (msg->t >= msg->d) {
        RCLCPP_WARN(this->get_logger(),
                    "[CurrentEngine] vessel parameters ignored: T %.2f >= d %.2f",
                    msg->t, msg->d);
        return;
    }

    double kg_for_log = msg->kg;
    {
        std::unique_lock<std::shared_mutex> lk(params_mutex_);
        Lpp_ = msg->lpp;
        B_ = msg->b;
        T_ = msg->t;
        d_ = msg->d;
        if (msg->kg > 0.0) {
            KG_ = msg->kg;
        }
        kg_for_log = KG_;
    }

    RCLCPP_INFO(this->get_logger(),
                "[CurrentEngine] vessel update Lpp=%.1f B=%.1f T=%.1f d=%.1f KG=%.2f",
                msg->lpp, msg->b, msg->t, msg->d, kg_for_log);
}

void CurrentEngineNode::heading_callback(const std_msgs::msg::Float64::SharedPtr msg)
{
    std::unique_lock<std::shared_mutex> lk(params_mutex_);
    ship_heading_ = normalize_degrees(msg->data);
}

void CurrentEngineNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    std::unique_lock<std::shared_mutex> lk(params_mutex_);
    current_u_ = msg->twist.twist.linear.x;
    current_v_ = msg->twist.twist.linear.y;
}

bool CurrentEngineNode::validate_vessel_params()
{
    bool ok = true;
    ok &= validate_positive("Lpp", Lpp_, 0.0);
    ok &= validate_positive("B", B_, 0.0);
    ok &= validate_positive("T", T_, MIN_DRAFT);
    ok &= validate_positive("d", d_, MIN_DEPTH);
    ok &= validate_positive("water_density", water_density_, 0.0);
    ok &= std::isfinite(KG_);
    ok &= std::isfinite(current_roll_moment_arm_);
    ok &= std::isfinite(current_input_timeout_s_) &&
          validate_positive("current_input_timeout_s", current_input_timeout_s_, 0.0);
    if (ok && T_ >= d_) {
        RCLCPP_ERROR(this->get_logger(), "[CurrentEngine] invalid vessel geometry: T >= d");
        ok = false;
    }
    return ok;
}

bool CurrentEngineNode::validate_current_params()
{
    return validate_current_params(param_current_, "parameters");
}

bool CurrentEngineNode::validate_current_params(
    const CurrentComponents& current, const std::string& source)
{
    bool ok = true;
    ok &= std::isfinite(current.v_tide_surf) &&
          validate_range(source + ".v_tide", current.v_tide_surf, 0.0, MAX_CURRENT_SPEED);
    ok &= std::isfinite(current.v_wind_surf) &&
          validate_range(source + ".v_wind", current.v_wind_surf, 0.0, MAX_CURRENT_SPEED);
    ok &= std::isfinite(current.v_circ_surf) &&
          validate_range(source + ".v_circ", current.v_circ_surf, 0.0, MAX_CURRENT_SPEED);
    ok &= std::isfinite(current.dir_tide) &&
          validate_range(source + ".dir_tide", current.dir_tide, 0.0, 360.0);
    ok &= std::isfinite(current.dir_wind) &&
          validate_range(source + ".dir_wind", current.dir_wind, 0.0, 360.0);
    ok &= std::isfinite(current.dir_circ) &&
          validate_range(source + ".dir_circ", current.dir_circ, 0.0, 360.0);
    return ok;
}

} // namespace env_engines

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<env_engines::CurrentEngineNode>());
    rclcpp::shutdown();
    return 0;
}
