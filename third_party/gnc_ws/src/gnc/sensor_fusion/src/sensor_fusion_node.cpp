#include "sensor_fusion/sensor_fusion_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {
constexpr double kPi = 3.14159265358979323846;

void extract_rpy(const geometry_msgs::msg::Quaternion& msg, double& roll, double& pitch, double& yaw)
{
    tf2::Quaternion q(msg.x, msg.y, msg.z, msg.w);
    tf2::Matrix3x3(q).getRPY(roll, pitch, yaw);
}

double safe_positive(double value, double fallback)
{
    return std::isfinite(value) && value > 1e-9 ? value : fallback;
}
}  // namespace

SensorFusionNode::SensorFusionNode()
    : Node("sensor_fusion_node"),
      X_(Eigen::VectorXd::Zero(STATE_SIZE)),
      P_(Eigen::MatrixXd::Identity(STATE_SIZE, STATE_SIZE)),
      Q_(Eigen::MatrixXd::Zero(STATE_SIZE, STATE_SIZE)),
      Q_base_(Eigen::MatrixXd::Zero(STATE_SIZE, STATE_SIZE)),
      I_(Eigen::MatrixXd::Identity(STATE_SIZE, STATE_SIZE))
{
    declare_parameter("ship_odom_topic", ship_odom_topic_);
    declare_parameter("gnss_topic", gnss_topic_);
    declare_parameter("imu_topic", imu_topic_);
    declare_parameter("heading_topic", heading_topic_);
    declare_parameter("cmd_tau_topic", cmd_tau_topic_);
    declare_parameter("output_odom_topic", output_odom_topic_);
    declare_parameter("output_status_topic", output_status_topic_);

    declare_parameter("use_ship_odom_measurement", use_ship_odom_measurement_);
    declare_parameter("use_gnss_measurement", use_gnss_measurement_);
    declare_parameter("use_gnss_velocity", use_gnss_velocity_);
    declare_parameter("use_gnss_heading", use_gnss_heading_);
    declare_parameter("use_imu_measurement", use_imu_measurement_);
    declare_parameter("use_imu_yaw", use_imu_yaw_);
    declare_parameter("use_heading_measurement", use_heading_measurement_);

    declare_parameter("q_pos", q_pos_);
    declare_parameter("q_roll", q_roll_);
    declare_parameter("q_psi", q_yaw_);
    declare_parameter("q_vel", q_vel_);
    declare_parameter("q_p", q_p_);
    declare_parameter("q_r", q_r_);

    declare_parameter("r_pos", r_odom_pos_);
    declare_parameter("r_gnss_pos", r_gnss_pos_);
    declare_parameter("r_roll", r_roll_);
    declare_parameter("r_psi", r_yaw_);
    declare_parameter("r_vel", r_vel_);
    declare_parameter("r_p", r_p_);
    declare_parameter("r_r", r_r_);

    declare_parameter("nom_mass_x", m_nom_x_);
    declare_parameter("nom_mass_y", m_nom_y_);
    declare_parameter("nom_mass_roll", m_nom_roll_);
    declare_parameter("nom_mass_psi", m_nom_yaw_);
    declare_parameter("nom_damp_x", d_nom_x_);
    declare_parameter("nom_damp_y", d_nom_y_);
    declare_parameter("nom_damp_roll", d_nom_roll_);
    declare_parameter("nom_damp_psi", d_nom_yaw_);

    declare_parameter("odom_chi2_threshold", odom_chi2_threshold_);
    declare_parameter("gnss_chi2_threshold", gnss_chi2_threshold_);
    declare_parameter("imu_chi2_threshold", imu_chi2_threshold_);
    declare_parameter("heading_chi2_threshold", heading_chi2_threshold_);
    declare_parameter("gnss_position_jump_gate_m", gnss_position_jump_gate_m_);
    declare_parameter("chi2_threshold", 12.59);
    declare_parameter("reject_outliers", reject_outliers_);

    declare_parameter("ship_odom_timeout_s", ship_odom_timeout_s_);
    declare_parameter("gnss_timeout_s", gnss_timeout_s_);
    declare_parameter("imu_timeout_s", imu_timeout_s_);
    declare_parameter("heading_timeout_s", heading_timeout_s_);
    declare_parameter("predict_rate_hz", predict_rate_hz_);
    declare_parameter("status_rate_hz", status_rate_hz_);

    get_parameters();
    setup_matrices();

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
        ship_odom_topic_, 20,
        std::bind(&SensorFusionNode::odom_callback, this, std::placeholders::_1));
    gnss_sub_ = create_subscription<nav_msgs::msg::Odometry>(
        gnss_topic_, 20,
        std::bind(&SensorFusionNode::gnss_callback, this, std::placeholders::_1));
    imu_sub_ = create_subscription<sensor_msgs::msg::Imu>(
        imu_topic_, 50,
        std::bind(&SensorFusionNode::imu_callback, this, std::placeholders::_1));
    heading_sub_ = create_subscription<std_msgs::msg::Float64>(
        heading_topic_, 20,
        std::bind(&SensorFusionNode::heading_callback, this, std::placeholders::_1));
    tau_cmd_sub_ = create_subscription<geometry_msgs::msg::WrenchStamped>(
        cmd_tau_topic_, 20,
        std::bind(&SensorFusionNode::tau_cmd_callback, this, std::placeholders::_1));

    odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(output_odom_topic_, 20);
    status_pub_ = create_publisher<std_msgs::msg::String>(output_status_topic_, 10);

    const auto predict_period = std::chrono::duration<double>(1.0 / safe_positive(predict_rate_hz_, 50.0));
    predict_timer_ = create_wall_timer(
        std::chrono::duration_cast<std::chrono::milliseconds>(predict_period),
        std::bind(&SensorFusionNode::predict_timer_callback, this));

    const auto status_period = std::chrono::duration<double>(1.0 / safe_positive(status_rate_hz_, 1.0));
    status_timer_ = create_wall_timer(
        std::chrono::duration_cast<std::chrono::milliseconds>(status_period),
        std::bind(&SensorFusionNode::status_timer_callback, this));

    RCLCPP_INFO(
        get_logger(),
        "sensor_fusion_node shadow mode started. output=%s status=%s state=[x,y,roll,yaw,u,v,p,r]",
        output_odom_topic_.c_str(),
        output_status_topic_.c_str());
}

void SensorFusionNode::get_parameters()
{
    ship_odom_topic_ = get_parameter("ship_odom_topic").as_string();
    gnss_topic_ = get_parameter("gnss_topic").as_string();
    imu_topic_ = get_parameter("imu_topic").as_string();
    heading_topic_ = get_parameter("heading_topic").as_string();
    cmd_tau_topic_ = get_parameter("cmd_tau_topic").as_string();
    output_odom_topic_ = get_parameter("output_odom_topic").as_string();
    output_status_topic_ = get_parameter("output_status_topic").as_string();

    use_ship_odom_measurement_ = get_parameter("use_ship_odom_measurement").as_bool();
    use_gnss_measurement_ = get_parameter("use_gnss_measurement").as_bool();
    use_gnss_velocity_ = get_parameter("use_gnss_velocity").as_bool();
    use_gnss_heading_ = get_parameter("use_gnss_heading").as_bool();
    use_imu_measurement_ = get_parameter("use_imu_measurement").as_bool();
    use_imu_yaw_ = get_parameter("use_imu_yaw").as_bool();
    use_heading_measurement_ = get_parameter("use_heading_measurement").as_bool();

    q_pos_ = get_parameter("q_pos").as_double();
    q_roll_ = get_parameter("q_roll").as_double();
    q_yaw_ = get_parameter("q_psi").as_double();
    q_vel_ = get_parameter("q_vel").as_double();
    q_p_ = get_parameter("q_p").as_double();
    q_r_ = get_parameter("q_r").as_double();

    r_odom_pos_ = get_parameter("r_pos").as_double();
    r_gnss_pos_ = get_parameter("r_gnss_pos").as_double();
    r_roll_ = get_parameter("r_roll").as_double();
    r_yaw_ = get_parameter("r_psi").as_double();
    r_vel_ = get_parameter("r_vel").as_double();
    r_p_ = get_parameter("r_p").as_double();
    r_r_ = get_parameter("r_r").as_double();

    m_nom_x_ = get_parameter("nom_mass_x").as_double();
    m_nom_y_ = get_parameter("nom_mass_y").as_double();
    m_nom_roll_ = get_parameter("nom_mass_roll").as_double();
    m_nom_yaw_ = get_parameter("nom_mass_psi").as_double();
    d_nom_x_ = get_parameter("nom_damp_x").as_double();
    d_nom_y_ = get_parameter("nom_damp_y").as_double();
    d_nom_roll_ = get_parameter("nom_damp_roll").as_double();
    d_nom_yaw_ = get_parameter("nom_damp_psi").as_double();

    odom_chi2_threshold_ = get_parameter("odom_chi2_threshold").as_double();
    gnss_chi2_threshold_ = get_parameter("gnss_chi2_threshold").as_double();
    imu_chi2_threshold_ = get_parameter("imu_chi2_threshold").as_double();
    heading_chi2_threshold_ = get_parameter("heading_chi2_threshold").as_double();
    gnss_position_jump_gate_m_ = get_parameter("gnss_position_jump_gate_m").as_double();
    const double legacy_threshold = get_parameter("chi2_threshold").as_double();
    if (odom_chi2_threshold_ <= 0.0) {
        odom_chi2_threshold_ = legacy_threshold;
    }
    reject_outliers_ = get_parameter("reject_outliers").as_bool();

    ship_odom_timeout_s_ = get_parameter("ship_odom_timeout_s").as_double();
    gnss_timeout_s_ = get_parameter("gnss_timeout_s").as_double();
    imu_timeout_s_ = get_parameter("imu_timeout_s").as_double();
    heading_timeout_s_ = get_parameter("heading_timeout_s").as_double();
    predict_rate_hz_ = get_parameter("predict_rate_hz").as_double();
    status_rate_hz_ = get_parameter("status_rate_hz").as_double();
}

void SensorFusionNode::setup_matrices()
{
    P_ = Eigen::MatrixXd::Identity(STATE_SIZE, STATE_SIZE);
    P_(IDX_X, IDX_X) = 25.0;
    P_(IDX_Y, IDX_Y) = 25.0;
    P_(IDX_ROLL, IDX_ROLL) = 0.05;
    P_(IDX_YAW, IDX_YAW) = 0.05;
    P_(IDX_U, IDX_U) = 4.0;
    P_(IDX_V, IDX_V) = 4.0;
    P_(IDX_P, IDX_P) = 0.25;
    P_(IDX_R, IDX_R) = 0.25;

    Q_base_ = Eigen::MatrixXd::Zero(STATE_SIZE, STATE_SIZE);
    Q_base_(IDX_X, IDX_X) = q_pos_;
    Q_base_(IDX_Y, IDX_Y) = q_pos_;
    Q_base_(IDX_ROLL, IDX_ROLL) = q_roll_;
    Q_base_(IDX_YAW, IDX_YAW) = q_yaw_;
    Q_base_(IDX_U, IDX_U) = q_vel_;
    Q_base_(IDX_V, IDX_V) = q_vel_;
    Q_base_(IDX_P, IDX_P) = q_p_;
    Q_base_(IDX_R, IDX_R) = q_r_;
    Q_ = Q_base_;
    I_ = Eigen::MatrixXd::Identity(STATE_SIZE, STATE_SIZE);

    sensor_health_["ship_odom"] = SensorHealth{};
    sensor_health_["gnss"] = SensorHealth{};
    sensor_health_["imu"] = SensorHealth{};
    sensor_health_["heading"] = SensorHealth{};
}

void SensorFusionNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    if (!use_ship_odom_measurement_) {
        return;
    }

    std::lock_guard<std::mutex> lock(ekf_mutex_);
    const rclcpp::Time stamp = stamp_or_now(msg->header.stamp, *this);

    if (!initialized_) {
        initialize_from_odometry(*msg, stamp, "ship_odom");
        return;
    }

    predict_to(stamp);

    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    extract_rpy(msg->pose.pose.orientation, roll, pitch, yaw);

    Eigen::VectorXd z(STATE_SIZE);
    z << msg->pose.pose.position.x,
        msg->pose.pose.position.y,
        roll,
        yaw,
        msg->twist.twist.linear.x,
        msg->twist.twist.linear.y,
        msg->twist.twist.angular.x,
        msg->twist.twist.angular.z;

    Eigen::VectorXd variance(STATE_SIZE);
    variance << r_odom_pos_, r_odom_pos_, r_roll_, r_yaw_, r_vel_, r_vel_, r_p_, r_r_;
    update_with_measurement("ship_odom", z, {IDX_X, IDX_Y, IDX_ROLL, IDX_YAW, IDX_U, IDX_V, IDX_P, IDX_R}, variance, odom_chi2_threshold_);
}

void SensorFusionNode::gnss_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    if (!use_gnss_measurement_) {
        return;
    }

    std::lock_guard<std::mutex> lock(ekf_mutex_);
    const rclcpp::Time stamp = stamp_or_now(msg->header.stamp, *this);

    if (!initialized_) {
        initialize_from_odometry(*msg, stamp, "gnss");
        return;
    }

    predict_to(stamp);

    const auto gnss_health_it = sensor_health_.find("gnss");
    const bool gnss_recent =
        gnss_health_it != sensor_health_.end()
        && gnss_health_it->second.last_stamp.nanoseconds() > 0
        && !gnss_health_it->second.timed_out
        && (stamp - gnss_health_it->second.last_stamp).seconds() <= safe_positive(gnss_timeout_s_, 1.0);
    const double position_jump = std::hypot(
        msg->pose.pose.position.x - X_(IDX_X),
        msg->pose.pose.position.y - X_(IDX_Y));
    if (gnss_recent
        && gnss_position_jump_gate_m_ > 0.0
        && std::isfinite(position_jump)
        && position_jump > gnss_position_jump_gate_m_) {
        mark_sensor_update("gnss", stamp, true, position_jump);
        RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 1000,
            "[FDE] reject GNSS position jump: innovation=%.2fm gate=%.2fm",
            position_jump, gnss_position_jump_gate_m_);
        return;
    }

    std::vector<int> indices{IDX_X, IDX_Y};
    std::vector<double> values{
        msg->pose.pose.position.x,
        msg->pose.pose.position.y};
    std::vector<double> variances{r_gnss_pos_, r_gnss_pos_};

    if (use_gnss_heading_) {
        double roll = 0.0;
        double pitch = 0.0;
        double yaw = 0.0;
        extract_rpy(msg->pose.pose.orientation, roll, pitch, yaw);
        indices.push_back(IDX_YAW);
        values.push_back(yaw);
        variances.push_back(r_yaw_);
    }

    if (use_gnss_velocity_) {
        indices.push_back(IDX_U);
        values.push_back(msg->twist.twist.linear.x);
        variances.push_back(r_vel_);
        indices.push_back(IDX_V);
        values.push_back(msg->twist.twist.linear.y);
        variances.push_back(r_vel_);
    }

    Eigen::VectorXd z(values.size());
    Eigen::VectorXd variance(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        z(static_cast<int>(i)) = values[i];
        variance(static_cast<int>(i)) = variances[i];
    }

    update_with_measurement("gnss", z, indices, variance, gnss_chi2_threshold_);
}

void SensorFusionNode::imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg)
{
    if (!use_imu_measurement_ || !initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(ekf_mutex_);
    const rclcpp::Time stamp = stamp_or_now(msg->header.stamp, *this);
    predict_to(stamp);

    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    extract_rpy(msg->orientation, roll, pitch, yaw);

    std::vector<int> indices{IDX_ROLL, IDX_P, IDX_R};
    std::vector<double> values{roll, msg->angular_velocity.x, msg->angular_velocity.z};
    std::vector<double> variances{r_roll_, r_p_, r_r_};

    if (use_imu_yaw_) {
        indices.push_back(IDX_YAW);
        values.push_back(yaw);
        variances.push_back(r_yaw_);
    }

    Eigen::VectorXd z(values.size());
    Eigen::VectorXd variance(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        z(static_cast<int>(i)) = values[i];
        variance(static_cast<int>(i)) = variances[i];
    }

    update_with_measurement("imu", z, indices, variance, imu_chi2_threshold_);
}

void SensorFusionNode::heading_callback(const std_msgs::msg::Float64::SharedPtr msg)
{
    if (!use_heading_measurement_ || !initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(ekf_mutex_);
    const rclcpp::Time stamp = now();
    predict_to(stamp);

    Eigen::VectorXd z(1);
    z << normalize_angle(msg->data);
    Eigen::VectorXd variance(1);
    variance << r_yaw_;
    update_with_measurement("heading", z, {IDX_YAW}, variance, heading_chi2_threshold_);
}

void SensorFusionNode::tau_cmd_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(ekf_mutex_);
    tau_cmd_ << msg->wrench.force.x, msg->wrench.force.y, msg->wrench.torque.z;
}

void SensorFusionNode::predict_timer_callback()
{
    std::lock_guard<std::mutex> lock(ekf_mutex_);
    if (!initialized_) {
        return;
    }
    const rclcpp::Time stamp = now();
    predict_to(stamp);
    publish_filtered_odom(stamp);
}

void SensorFusionNode::status_timer_callback()
{
    std::lock_guard<std::mutex> lock(ekf_mutex_);
    const rclcpp::Time stamp = now();
    update_sensor_timeouts(stamp);
    publish_status(stamp);
}

void SensorFusionNode::initialize_from_odometry(
    const nav_msgs::msg::Odometry& msg,
    const rclcpp::Time& stamp,
    const std::string& source)
{
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    extract_rpy(msg.pose.pose.orientation, roll, pitch, yaw);

    X_ << msg.pose.pose.position.x,
        msg.pose.pose.position.y,
        normalize_angle(roll),
        normalize_angle(yaw),
        msg.twist.twist.linear.x,
        msg.twist.twist.linear.y,
        msg.twist.twist.angular.x,
        msg.twist.twist.angular.z;

    last_predict_time_ = stamp;
    initialized_ = true;
    mark_sensor_update(source, stamp, false, 0.0);
    publish_filtered_odom(stamp);
    RCLCPP_INFO(
        get_logger(),
        "EKF initialized from %s: x=%.2f y=%.2f roll=%.2fdeg yaw=%.2fdeg",
        source.c_str(),
        X_(IDX_X),
        X_(IDX_Y),
        X_(IDX_ROLL) * 180.0 / kPi,
        X_(IDX_YAW) * 180.0 / kPi);
}

void SensorFusionNode::predict_to(const rclcpp::Time& stamp)
{
    if (!initialized_) {
        return;
    }

    double dt = (stamp - last_predict_time_).seconds();
    if (!std::isfinite(dt) || dt <= 0.0) {
        return;
    }

    const double max_step_s = 0.05;
    while (dt > max_step_s) {
        predict_step(max_step_s);
        dt -= max_step_s;
    }
    if (dt > 1e-6) {
        predict_step(dt);
    }
    last_predict_time_ = stamp;
}

void SensorFusionNode::predict_step(double dt)
{
    const double roll = X_(IDX_ROLL);
    const double yaw = X_(IDX_YAW);
    const double u = X_(IDX_U);
    const double v = X_(IDX_V);
    const double p = X_(IDX_P);
    const double r = X_(IDX_R);

    X_(IDX_X) += (u * std::cos(yaw) - v * std::sin(yaw)) * dt;
    X_(IDX_Y) += (u * std::sin(yaw) + v * std::cos(yaw)) * dt;
    X_(IDX_ROLL) = normalize_angle(roll + p * dt);
    X_(IDX_YAW) = normalize_angle(yaw + r * dt);

    const double u_dot = (tau_cmd_.x() + d_nom_x_ * u) / safe_positive(m_nom_x_, 1.0);
    const double v_dot = (tau_cmd_.y() + d_nom_y_ * v) / safe_positive(m_nom_y_, 1.0);
    const double p_dot = (d_nom_roll_ * p) / safe_positive(m_nom_roll_, 1.0);
    const double r_dot = (tau_cmd_.z() + d_nom_yaw_ * r) / safe_positive(m_nom_yaw_, 1.0);

    X_(IDX_U) += u_dot * dt;
    X_(IDX_V) += v_dot * dt;
    X_(IDX_P) += p_dot * dt;
    X_(IDX_R) += r_dot * dt;

    Eigen::MatrixXd F = Eigen::MatrixXd::Identity(STATE_SIZE, STATE_SIZE);
    F(IDX_X, IDX_YAW) = (-u * std::sin(yaw) - v * std::cos(yaw)) * dt;
    F(IDX_Y, IDX_YAW) = (u * std::cos(yaw) - v * std::sin(yaw)) * dt;
    F(IDX_X, IDX_U) = std::cos(yaw) * dt;
    F(IDX_Y, IDX_U) = std::sin(yaw) * dt;
    F(IDX_X, IDX_V) = -std::sin(yaw) * dt;
    F(IDX_Y, IDX_V) = std::cos(yaw) * dt;
    F(IDX_ROLL, IDX_P) = dt;
    F(IDX_YAW, IDX_R) = dt;
    F(IDX_U, IDX_U) = 1.0 + (d_nom_x_ / safe_positive(m_nom_x_, 1.0)) * dt;
    F(IDX_V, IDX_V) = 1.0 + (d_nom_y_ / safe_positive(m_nom_y_, 1.0)) * dt;
    F(IDX_P, IDX_P) = 1.0 + (d_nom_roll_ / safe_positive(m_nom_roll_, 1.0)) * dt;
    F(IDX_R, IDX_R) = 1.0 + (d_nom_yaw_ / safe_positive(m_nom_yaw_, 1.0)) * dt;

    P_ = F * P_ * F.transpose() + Q_;
}

bool SensorFusionNode::update_with_measurement(
    const std::string& source,
    const Eigen::VectorXd& z,
    const std::vector<int>& state_indices,
    const Eigen::VectorXd& measurement_variance,
    double chi2_threshold)
{
    const int measurement_size = static_cast<int>(state_indices.size());
    if (measurement_size == 0 || z.size() != measurement_size || measurement_variance.size() != measurement_size) {
        RCLCPP_WARN(get_logger(), "invalid measurement from %s", source.c_str());
        return false;
    }

    Eigen::MatrixXd H = Eigen::MatrixXd::Zero(measurement_size, STATE_SIZE);
    Eigen::MatrixXd R = Eigen::MatrixXd::Zero(measurement_size, measurement_size);
    for (int i = 0; i < measurement_size; ++i) {
        H(i, state_indices[static_cast<size_t>(i)]) = 1.0;
        R(i, i) = safe_positive(measurement_variance(i), 1.0);
    }

    Eigen::VectorXd residual = z - H * X_;
    for (int i = 0; i < measurement_size; ++i) {
        const int idx = state_indices[static_cast<size_t>(i)];
        if (idx == IDX_ROLL || idx == IDX_YAW) {
            residual(i) = normalize_angle(residual(i));
        }
    }

    Eigen::MatrixXd S = H * P_ * H.transpose() + R;
    Eigen::LDLT<Eigen::MatrixXd> ldlt(S);
    if (ldlt.info() != Eigen::Success) {
        mark_sensor_update(source, now(), true, std::numeric_limits<double>::infinity());
        RCLCPP_WARN(get_logger(), "innovation covariance decomposition failed for %s", source.c_str());
        return false;
    }

    const double mahalanobis_sq = residual.transpose() * ldlt.solve(residual);
    const bool rejected =
        reject_outliers_ && std::isfinite(mahalanobis_sq) && mahalanobis_sq > safe_positive(chi2_threshold, 1.0);

    if (rejected) {
        Q_base_scale_ = std::min(20.0, Q_base_scale_ * 1.15);
        Q_ = Q_base_ * Q_base_scale_;
        mark_sensor_update(source, now(), true, mahalanobis_sq);
        RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 1000,
            "[FDE] reject %s measurement: mahalanobis=%.2f threshold=%.2f q_scale=%.2f",
            source.c_str(), mahalanobis_sq, chi2_threshold, Q_base_scale_);
        return false;
    }

    const Eigen::MatrixXd PHt = P_ * H.transpose();
    const Eigen::MatrixXd K = PHt * ldlt.solve(Eigen::MatrixXd::Identity(measurement_size, measurement_size));
    X_ = X_ + K * residual;
    X_(IDX_ROLL) = normalize_angle(X_(IDX_ROLL));
    X_(IDX_YAW) = normalize_angle(X_(IDX_YAW));

    const Eigen::MatrixXd I_KH = I_ - K * H;
    P_ = I_KH * P_ * I_KH.transpose() + K * R * K.transpose();
    P_ = 0.5 * (P_ + P_.transpose());

    Q_base_scale_ = std::max(1.0, Q_base_scale_ * 0.99);
    Q_ = Q_base_ * Q_base_scale_;
    mark_sensor_update(source, now(), false, mahalanobis_sq);
    return true;
}

void SensorFusionNode::publish_filtered_odom(const rclcpp::Time& stamp)
{
    nav_msgs::msg::Odometry msg;
    msg.header.stamp = stamp;
    msg.header.frame_id = "odom";
    msg.child_frame_id = "base_link";

    msg.pose.pose.position.x = X_(IDX_X);
    msg.pose.pose.position.y = X_(IDX_Y);
    msg.pose.pose.position.z = 0.0;

    tf2::Quaternion q;
    q.setRPY(X_(IDX_ROLL), 0.0, X_(IDX_YAW));
    msg.pose.pose.orientation.x = q.x();
    msg.pose.pose.orientation.y = q.y();
    msg.pose.pose.orientation.z = q.z();
    msg.pose.pose.orientation.w = q.w();

    msg.twist.twist.linear.x = X_(IDX_U);
    msg.twist.twist.linear.y = X_(IDX_V);
    msg.twist.twist.linear.z = 0.0;
    msg.twist.twist.angular.x = X_(IDX_P);
    msg.twist.twist.angular.y = 0.0;
    msg.twist.twist.angular.z = X_(IDX_R);

    msg.pose.covariance[0] = P_(IDX_X, IDX_X);
    msg.pose.covariance[7] = P_(IDX_Y, IDX_Y);
    msg.pose.covariance[21] = P_(IDX_ROLL, IDX_ROLL);
    msg.pose.covariance[35] = P_(IDX_YAW, IDX_YAW);
    msg.twist.covariance[0] = P_(IDX_U, IDX_U);
    msg.twist.covariance[7] = P_(IDX_V, IDX_V);
    msg.twist.covariance[21] = P_(IDX_P, IDX_P);
    msg.twist.covariance[35] = P_(IDX_R, IDX_R);

    odom_pub_->publish(msg);
}

void SensorFusionNode::publish_status(const rclcpp::Time& stamp)
{
    const auto has_fresh = [&](const std::string& source, bool enabled) {
        if (!enabled) {
            return false;
        }
        const auto it = sensor_health_.find(source);
        return it != sensor_health_.end()
            && it->second.update_count > 0
            && !it->second.timed_out;
    };

    const bool has_position_source =
        has_fresh("ship_odom", use_ship_odom_measurement_) || has_fresh("gnss", use_gnss_measurement_);

    bool fault_detected = false;
    for (const auto& item : sensor_health_) {
        fault_detected = fault_detected || item.second.fault || item.second.timed_out;
    }
    const bool degraded = initialized_ && !has_position_source;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "{";
    ss << "\"initialized\":" << (initialized_ ? "true" : "false") << ",";
    ss << "\"degraded\":" << (degraded ? "true" : "false") << ",";
    ss << "\"fault_detected\":" << (fault_detected ? "true" : "false") << ",";
    ss << "\"state_dim\":8,";
    ss << "\"x\":" << X_(IDX_X) << ",";
    ss << "\"y\":" << X_(IDX_Y) << ",";
    ss << "\"roll_deg\":" << X_(IDX_ROLL) * 180.0 / kPi << ",";
    ss << "\"yaw_deg\":" << X_(IDX_YAW) * 180.0 / kPi << ",";
    ss << "\"speed_mps\":" << std::hypot(X_(IDX_U), X_(IDX_V)) << ",";
    ss << "\"q_scale\":" << Q_base_scale_ << ",";
    ss << "\"sensors\":{";
    bool first = true;
    for (const auto& item : sensor_health_) {
        if (!first) {
            ss << ",";
        }
        first = false;
        double age = -1.0;
        if (item.second.last_stamp.nanoseconds() > 0) {
            age = (stamp - item.second.last_stamp).seconds();
        }
        ss << "\"" << item.first << "\":{";
        ss << "\"updates\":" << item.second.update_count << ",";
        ss << "\"rejects\":" << item.second.reject_count << ",";
        ss << "\"timed_out\":" << (item.second.timed_out ? "true" : "false") << ",";
        ss << "\"fault\":" << (item.second.fault ? "true" : "false") << ",";
        ss << "\"age_s\":" << age << ",";
        ss << "\"mahalanobis\":" << item.second.last_mahalanobis;
        ss << "}";
    }
    ss << "}}";

    std_msgs::msg::String msg;
    msg.data = ss.str();
    status_pub_->publish(msg);
}

void SensorFusionNode::mark_sensor_update(
    const std::string& source,
    const rclcpp::Time& stamp,
    bool rejected,
    double mahalanobis_sq)
{
    auto& health = sensor_health_[source];
    health.last_stamp = stamp;
    health.last_mahalanobis = mahalanobis_sq;
    health.timed_out = false;
    if (rejected) {
        health.reject_count += 1;
        health.fault = true;
    } else {
        health.update_count += 1;
    }
}

void SensorFusionNode::update_sensor_timeouts(const rclcpp::Time& stamp)
{
    const auto mark_timeout = [&](const std::string& source, bool enabled, double timeout_s) {
        if (!enabled) {
            return;
        }
        auto& health = sensor_health_[source];
        if (!initialized_ || health.last_stamp.nanoseconds() <= 0) {
            return;
        }
        const double age = (stamp - health.last_stamp).seconds();
        if (std::isfinite(age) && age > safe_positive(timeout_s, 1.0)) {
            health.timed_out = true;
            health.fault = true;
        }
    };

    mark_timeout("ship_odom", use_ship_odom_measurement_, ship_odom_timeout_s_);
    mark_timeout("gnss", use_gnss_measurement_, gnss_timeout_s_);
    mark_timeout("imu", use_imu_measurement_, imu_timeout_s_);
    mark_timeout("heading", use_heading_measurement_, heading_timeout_s_);
}

rclcpp::Time SensorFusionNode::stamp_or_now(
    const builtin_interfaces::msg::Time& stamp,
    const rclcpp::Node& node)
{
    const rclcpp::Time candidate(stamp);
    if (candidate.nanoseconds() <= 0) {
        return node.now();
    }
    return candidate;
}

double SensorFusionNode::normalize_angle(double angle)
{
    while (angle > kPi) {
        angle -= 2.0 * kPi;
    }
    while (angle < -kPi) {
        angle += 2.0 * kPi;
    }
    return angle;
}

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SensorFusionNode>());
    rclcpp::shutdown();
    return 0;
}
