#ifndef SENSOR_FUSION_NODE_HPP
#define SENSOR_FUSION_NODE_HPP

#include <Eigen/Dense>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/string.hpp>
#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

#include <map>
#include <mutex>
#include <string>
#include <vector>

class SensorFusionNode : public rclcpp::Node {
public:
    SensorFusionNode();
    ~SensorFusionNode() override = default;

private:
    enum StateIndex {
        IDX_X = 0,
        IDX_Y = 1,
        IDX_ROLL = 2,
        IDX_YAW = 3,
        IDX_U = 4,
        IDX_V = 5,
        IDX_P = 6,
        IDX_R = 7,
        STATE_SIZE = 8
    };

    struct SensorHealth {
        rclcpp::Time last_stamp{0, 0, RCL_ROS_TIME};
        int update_count{0};
        int reject_count{0};
        bool timed_out{false};
        bool fault{false};
        double last_mahalanobis{0.0};
    };

    void get_parameters();
    void setup_matrices();

    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);
    void gnss_callback(const nav_msgs::msg::Odometry::SharedPtr msg);
    void imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg);
    void heading_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void tau_cmd_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);

    void predict_timer_callback();
    void status_timer_callback();

    void initialize_from_odometry(
        const nav_msgs::msg::Odometry& msg,
        const rclcpp::Time& stamp,
        const std::string& source);
    void predict_to(const rclcpp::Time& stamp);
    void predict_step(double dt);
    bool update_with_measurement(
        const std::string& source,
        const Eigen::VectorXd& z,
        const std::vector<int>& state_indices,
        const Eigen::VectorXd& measurement_variance,
        double chi2_threshold);

    void publish_filtered_odom(const rclcpp::Time& stamp);
    void publish_status(const rclcpp::Time& stamp);
    void mark_sensor_update(
        const std::string& source,
        const rclcpp::Time& stamp,
        bool rejected,
        double mahalanobis_sq);
    void update_sensor_timeouts(const rclcpp::Time& stamp);

    static rclcpp::Time stamp_or_now(
        const builtin_interfaces::msg::Time& stamp,
        const rclcpp::Node& node);
    static double normalize_angle(double angle);

    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr gnss_sub_;
    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_sub_;
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr tau_cmd_sub_;
    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
    rclcpp::TimerBase::SharedPtr predict_timer_;
    rclcpp::TimerBase::SharedPtr status_timer_;

    std::mutex ekf_mutex_;
    rclcpp::Time last_predict_time_{0, 0, RCL_ROS_TIME};
    bool initialized_{false};

    Eigen::VectorXd X_;
    Eigen::MatrixXd P_;
    Eigen::MatrixXd Q_;
    Eigen::MatrixXd Q_base_;
    Eigen::MatrixXd I_;

    double q_pos_{0.01};
    double q_roll_{0.001};
    double q_yaw_{0.001};
    double q_vel_{0.1};
    double q_p_{0.01};
    double q_r_{0.01};

    double r_odom_pos_{5.0};
    double r_gnss_pos_{5.0};
    double r_roll_{0.05};
    double r_yaw_{0.1};
    double r_vel_{2.0};
    double r_p_{0.2};
    double r_r_{0.5};

    double m_nom_x_{1.1e6};
    double m_nom_y_{1.7e6};
    double m_nom_roll_{5.0e7};
    double m_nom_yaw_{1.9e8};
    double d_nom_x_{-10000.0};
    double d_nom_y_{-60000.0};
    double d_nom_roll_{-8.0e5};
    double d_nom_yaw_{-1600000.0};
    Eigen::Vector3d tau_cmd_{Eigen::Vector3d::Zero()};

    double odom_chi2_threshold_{20.09};
    double gnss_chi2_threshold_{9.21};
    double imu_chi2_threshold_{11.34};
    double heading_chi2_threshold_{6.63};
    double gnss_position_jump_gate_m_{20.0};
    bool reject_outliers_{true};
    double Q_base_scale_{1.0};

    bool use_ship_odom_measurement_{true};
    bool use_gnss_measurement_{true};
    bool use_gnss_velocity_{false};
    bool use_gnss_heading_{false};
    bool use_imu_measurement_{true};
    bool use_imu_yaw_{false};
    bool use_heading_measurement_{true};

    double ship_odom_timeout_s_{2.0};
    double gnss_timeout_s_{2.0};
    double imu_timeout_s_{1.0};
    double heading_timeout_s_{1.5};
    double predict_rate_hz_{50.0};
    double status_rate_hz_{1.0};

    std::string ship_odom_topic_{"/ship/odometry"};
    std::string gnss_topic_{"/mock/gnss/odometry"};
    std::string imu_topic_{"/mock/imu"};
    std::string heading_topic_{"/mock/heading"};
    std::string cmd_tau_topic_{"/cmd_tau"};
    std::string output_odom_topic_{"/ship/odom_filtered"};
    std::string output_status_topic_{"/navigation/fusion_status"};

    std::map<std::string, SensorHealth> sensor_health_;
};

#endif
