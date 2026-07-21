// P1b-1a T8 -- Nomoto T,K identification: VDM zigzag simulator.
//
// NOT production code. test/external staging spike only.
//
// Runs two rudder zigzag maneuvers (10/10 and 20/20) on the REAL
// VesselDynamicsModel (4-DOF simplified MMG, RK4 step()) and writes the
// time series (t_s, psi_rad, r_rad_s, delta_rad, u_mps) to stdout as CSV.
// ident_nomoto.py then reads this CSV and least-squares fits the Nomoto
// first-order model  T*r_dot + r = K*delta.
//
// Heading convention (matches types.hpp):
//   psi_rad: 0 = north, positive = clockwise (starboard).
//   r_rad_s: positive = turn to starboard.
//   rudder_rad (Input.rudder_rad): positive = starboard deflection.
//
// Zigzag rule (brief): for a "+/+ threshold" zigzag, command rudder OPPOSITE
// to the current heading deviation once |psi| crosses the threshold. To start
// the first starboard swing from psi=0 we kick with +threshold rudder first.
//
// Link recipe is in run_ident.sh: only vessel_dynamics_model.cpp.o and
// capability_manifest.cpp.o are pulled from the static archive (the two objects
// with no rclcpp deps), so the ament/rclcpp link tarball is avoided.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "m5_tactical_planner/shared/capability_manifest.hpp"
#include "m5_tactical_planner/shared/vessel_dynamics_model.hpp"

namespace {

using mass_l3::m5::shared::CapabilityManifest;
using mass_l3::m5::shared::VesselDynamicsModel;

constexpr double kDtS = 0.5;  // brief: dt = 0.5 s

// One zigzag maneuver. threshold_rad is the heading deviation that flips the
// rudder (10 deg or 20 deg); rudder_amplitude_rad is the commanded rudder
// magnitude. Emits n_steps CSV rows (the initial t=0 row, then n_steps-1 steps).
void run_zigzag(const VesselDynamicsModel& vdm,
                double threshold_rad,
                double rudder_amplitude_rad,
                int n_steps,
                double rpm_rps,
                double u0_mps) {
  VesselDynamicsModel::State s{};
  s.u_mps = u0_mps;   // start at cruise; prop holds it roughly constant
  s.psi_rad = 0.0;
  s.r_rad_s = 0.0;
  s.v_mps = 0.0;
  s.x_m = 0.0;
  s.y_m = 0.0;
  s.t_s = 0.0;

  // Initial rudder kick in the +starboard direction to initiate the first
  // starboard swing (otherwise psi stays at 0 forever).
  double delta = rudder_amplitude_rad;

  for (int k = 0; k < n_steps; ++k) {
    // Emit current state BEFORE stepping (row k uses the rudder applied over
    // [t_k, t_k+dt]). This gives n_steps rows; the LAST row is the final state.
    std::printf("%.6f,%.9f,%.9f,%.9f,%.9f\n",
                s.t_s, s.psi_rad, s.r_rad_s, delta, s.u_mps);

    // Zigzag rudder logic (evaluated on current heading deviation).
    // psi >= +thr  -> command PORT rudder (-amp) to check the starboard swing.
    // psi <= -thr  -> command STARBOARD rudder (+amp) to check the port swing.
    if (s.psi_rad >= threshold_rad) {
      delta = -rudder_amplitude_rad;
    } else if (s.psi_rad <= -threshold_rad) {
      delta = rudder_amplitude_rad;
    }
    // else: hold the current delta (keeps the kick active until threshold hit).

    VesselDynamicsModel::Input cmd;
    cmd.rudder_rad = delta;
    cmd.rpm_rps = rpm_rps;
    s = vdm.step(s, cmd, kDtS);
  }
}

}  // namespace

int main(int argc, char** argv) {
  // Fixture path. Default is the in-repo test fixture; allow an override as
  // the first CLI arg for reproducibility from a different worktree root.
  std::string fixture_path =
      "src/l3_tdl_kernel/m5_tactical_planner/test/fixtures/fcb_capability_fixture.yaml";
  if (argc > 1) {
    fixture_path = argv[1];
  }

  CapabilityManifest manifest = CapabilityManifest::load_from_yaml(fixture_path);
  VesselDynamicsModel vdm(manifest);

  const double u0_mps = manifest.service_speed_mps();  // 18 kn = 9.2593 m/s
  // Steady-state prop speed that sustains u0 against quadratic drag.
  // F_prop = k_prop * n^2 (k_prop=500), R_hull = k_drag * u^2 (k_drag=100).
  // n = u * sqrt(k_drag/k_prop). See brief for derivation.
  constexpr double kProp = 500.0;
  constexpr double kDrag = 100.0;
  const double rpm_rps = u0_mps * std::sqrt(kDrag / kProp);  // ~4.141 rev/s

  constexpr double deg10 = 10.0 * M_PI / 180.0;  // 0.174533 rad
  constexpr double deg20 = 20.0 * M_PI / 180.0;  // 0.349066 rad

  // CSV header.
  std::printf("t_s,psi_rad,r_rad,delta_rad,u_mps\n");

  // 10/10 zigzag: 300 s @ dt=0.5 -> 600 steps.
  std::printf("# maneuver 10/10\n");
  run_zigzag(vdm, /*threshold_rad=*/deg10, /*rudder_amplitude_rad=*/deg10,
             /*n_steps=*/600, rpm_rps, u0_mps);

  // 20/20 zigzag: 400 s @ dt=0.5 -> 800 steps.
  std::printf("# maneuver 20/20\n");
  run_zigzag(vdm, /*threshold_rad=*/deg20, /*rudder_amplitude_rad=*/deg20,
             /*n_steps=*/800, rpm_rps, u0_mps);

  return 0;
}
