#ifndef MASS_L3_M5_MID_MPC_NODE_HPP_
#define MASS_L3_M5_MID_MPC_NODE_HPP_

#include <memory>
#include <optional>

#include "rclcpp/rclcpp.hpp"

#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp"

namespace mass_l3::m5::mid_mpc {

class MidMpcNode : public rclcpp::Node {
 public:
  struct Config {
    MidMpcNlpFormulation::Config nlp;
    MidMpcSolver::IpoptOptions   ipopt;
    MidMpcWaypointGenerator::Config waypoint;
    // [TBD-HAZID] Default FCB home port latitude [deg WGS84].
    // Replace with dynamic own-ship state in Phase E2.
    double own_ship_lat_deg{30.0};
    // [TBD-HAZID] Default FCB home port longitude [deg WGS84].
    double own_ship_lon_deg{122.0};
  };

  explicit MidMpcNode(const Config& cfg = Config{});

 private:
  MidMpcNlpFormulation        formulation_;
  MidMpcSolver                solver_;
  MidMpcWaypointGenerator     wp_gen_;
  std::optional<MidMpcSolution> last_solution_;

  l3_msgs::msg::OwnShipState::SharedPtr                     own_ship_state_;
  l3_external_msgs::msg::TrackedTargetArray::SharedPtr       tracked_targets_;
  l3_msgs::msg::BehaviorPlan::SharedPtr                      behavior_plan_;
  l3_msgs::msg::COLREGsConstraint::SharedPtr                 colregs_constraint_;
  l3_external_msgs::msg::PlannedRoute::SharedPtr             planned_route_;
  l3_external_msgs::msg::SpeedProfile::SharedPtr             speed_profile_;

  rclcpp::Publisher<l3_msgs::msg::AvoidancePlan>::SharedPtr  pub_avoidance_plan_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr     pub_asdr_record_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr        pub_sat_data_;

  rclcpp::Subscription<l3_msgs::msg::OwnShipState>::SharedPtr           sub_own_ship_;
  rclcpp::Subscription<l3_external_msgs::msg::TrackedTargetArray>::SharedPtr sub_targets_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr           sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr      sub_colregs_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr  sub_route_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr  sub_speed_;

  rclcpp::TimerBase::SharedPtr solve_timer_;

  void on_solve_cycle_();
  [[nodiscard]] bool has_required_inputs_() const noexcept;
  [[nodiscard]] MidMpcInput assemble_input_() const;
  void publish_outputs_(const MidMpcSolution& sol,
                        const l3_msgs::msg::AvoidancePlan& plan);
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_NODE_HPP_
