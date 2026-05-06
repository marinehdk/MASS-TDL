#include "m2_world_model/cpa_tcpa_calculator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

#include <Eigen/Cholesky>

namespace mass_l3::m2 {

namespace {

constexpr double kKnToMs = 0.514444;
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;
constexpr double kLatM = 111320.0;  // metres per degree latitude (approx)
constexpr double kEps = 1e-12;

/// Safe square root (avoid negative due to floating-point).
inline double safe_sqrt(double x) { return std::sqrt(std::max(x, 0.0)); }

/// Extract ENU position and velocity covariance from target snapshot.
/// Target covariance is 3x3 [lat_var, lon_var, heading_var].
/// Returns a 2x2 position covariance in metres^2 (ENU frame).
Eigen::Matrix2d target_pos_covariance_enu_(const TargetSnapshot& tgt) {
  double lat_rad = tgt.latitude_deg * kDegToRad;
  double lon_to_m = kLatM * std::cos(lat_rad);
  Eigen::Matrix2d sigma;
  sigma(0, 0) = tgt.covariance(1, 1) * lon_to_m * lon_to_m;  // lon var -> east var
  sigma(1, 1) = tgt.covariance(0, 0) * kLatM * kLatM;        // lat var -> north var
  sigma(0, 1) = 0.0;
  sigma(1, 0) = 0.0;
  return sigma;
}

/// Extract ENU position and velocity covariance from own-ship snapshot (6x6).
/// Assumes order: [east, north, up, ve, vn, vu].
std::pair<Eigen::Matrix2d, Eigen::Matrix2d>
own_ship_covariances_enu_(const OwnShipSnapshot& own) {
  Eigen::Matrix2d sigma_pos = own.covariance.block<2, 2>(0, 0);
  Eigen::Matrix2d sigma_vel = own.covariance.block<2, 2>(3, 3);
  return {sigma_pos, sigma_vel};
}

}  // namespace

CpaTcpaCalculator::CpaTcpaCalculator(Config cfg) : cfg_(std::move(cfg)) {}

std::optional<CpaResult>
CpaTcpaCalculator::compute(const OwnShipSnapshot& own_ship,
                           const TargetSnapshot& target,
                           OddZone odd_zone) const {
  // ── 1. Time alignment check ──
  auto dt_sec = std::chrono::duration<double>(own_ship.stamp - target.stamp).count();
  if (std::abs(dt_sec) > cfg_.max_align_dt_s) {
    return std::nullopt;
  }

  // ── 2. Coordinate transform (origin = own ship position) ──
  CoordTransform ct;
  ct.init(own_ship.latitude_deg, own_ship.longitude_deg);

  // Own ship at origin → pos = (0, 0)
  Eigen::Vector2d own_pos, own_vel;
  bool own_ok = ct.wgs84_to_enu(own_ship.latitude_deg, own_ship.longitude_deg,
                                own_ship.sog_kn, own_ship.cog_deg,
                                own_pos, own_vel);
  if (!own_ok) return std::nullopt;

  // Compute own-ship velocity from u_water / v_water + current (per spec §5.1.2)
  double heading_rad = own_ship.heading_deg * kDegToRad;
  double current_rad = own_ship.current_direction_deg * kDegToRad;
  double current_spd_ms = own_ship.current_speed_kn * kKnToMs;
  Eigen::Vector2d own_vel_water;
  own_vel_water(0) = own_ship.u_water * std::sin(heading_rad)
                   + own_ship.v_water * std::cos(heading_rad);
  own_vel_water(1) = own_ship.u_water * std::cos(heading_rad)
                   - own_ship.v_water * std::sin(heading_rad);
  Eigen::Vector2d own_vel_current;
  own_vel_current(0) = current_spd_ms * std::sin(current_rad);
  own_vel_current(1) = current_spd_ms * std::cos(current_rad);
  Eigen::Vector2d own_vel_total = own_vel_water + own_vel_current;

  // Target position & velocity (from SOG/COG)
  Eigen::Vector2d tgt_pos, tgt_vel;
  // Extrapolate target to own_ship time, then convert to ENU
  TargetSnapshot tgt_aligned = extrapolate_to_(target, own_ship.stamp);
  bool tgt_ok = ct.wgs84_to_enu(tgt_aligned.latitude_deg, tgt_aligned.longitude_deg,
                                tgt_aligned.sog_kn, tgt_aligned.cog_deg,
                                tgt_pos, tgt_vel);
  if (!tgt_ok) return std::nullopt;

  // ── 3. Relative state ──
  Eigen::Vector2d rel_pos = tgt_pos - own_pos;  // own_pos ~ (0,0)
  Eigen::Vector2d rel_vel = tgt_vel - own_vel_total;

  // ── 4. CPA / TCPA computation ──
  double rel_speed_sq = rel_vel.squaredNorm();
  double tcpa_s = 0.0;
  double cpa_m = 0.0;

  if (rel_speed_sq < kEps) {
    // Stationary relative motion
    cpa_m = rel_pos.norm();
  } else {
    tcpa_s = -rel_pos.dot(rel_vel) / rel_speed_sq;
    if (tcpa_s < 0.0) {
      // CPA lies in the past → use current distance
      cpa_m = rel_pos.norm();
      tcpa_s = 0.0;
    } else {
      cpa_m = (rel_pos + rel_vel * tcpa_s).norm();
    }
  }

  // ── 5. Uncertainty propagation ──
  Eigen::Matrix2d sigma_tgt_pos = target_pos_covariance_enu_(target);
  auto [sigma_own_pos, sigma_own_vel] = own_ship_covariances_enu_(own_ship);
  Eigen::Matrix2d sigma_rel = sigma_tgt_pos + sigma_own_pos;
  Eigen::Matrix2d sigma_rel_vel = sigma_own_vel;

  CpaUncertainty unc{0.0, 0.0};
  switch (cfg_.method) {
    case UncertaintyMethod::Linear:
      unc = propagate_linear_(rel_pos, rel_vel, sigma_rel, sigma_rel_vel);
      break;
    case UncertaintyMethod::MonteCarlo:
      unc = propagate_monte_carlo_(rel_pos, rel_vel, sigma_rel, sigma_rel_vel);
      break;
  }

  // ── 6. Safety factor ──
  double final_cpa = cpa_m * cfg_.safety_factor;
  if (odd_zone == OddZone::D) {
    final_cpa *= cfg_.odd_d_multiplier;
  }

  return CpaResult{final_cpa, tcpa_s, unc};
}

TargetSnapshot
CpaTcpaCalculator::extrapolate_to_(const TargetSnapshot& target,
                                   std::chrono::steady_clock::time_point tgt_time) const {
  auto dt = std::chrono::duration<double>(tgt_time - target.stamp).count();

  double speed_ms = target.sog_kn * kKnToMs;
  double cog_rad = target.cog_deg * kDegToRad;

  // ENU displacement
  double de = speed_ms * std::sin(cog_rad) * dt;
  double dn = speed_ms * std::cos(cog_rad) * dt;

  // Approximate lat/lon offset (small-angle valid for short dt)
  double lat_rad = target.latitude_deg * kDegToRad;
  double dlat = dn / kLatM;
  double dlon = de / (kLatM * std::cos(lat_rad));

  TargetSnapshot result = target;
  result.latitude_deg += dlat;
  result.longitude_deg += dlon;
  result.stamp = tgt_time;
  return result;
}

CpaUncertainty
CpaTcpaCalculator::propagate_linear_(const Eigen::Vector2d& rel_pos,
                                     const Eigen::Vector2d& rel_vel,
                                     const Eigen::Matrix2d& sigma_rel_pos,
                                     const Eigen::Matrix2d& sigma_rel_vel) const {
  double rel_speed_sq = rel_vel.squaredNorm();

  if (rel_speed_sq < kEps) {
    // Stationary: only position uncertainty contributes to CPA
    double cpa_var = (rel_pos.transpose() * sigma_rel_pos * rel_pos).value();
    cpa_var /= rel_pos.squaredNorm() + kEps;
    return {safe_sqrt(cpa_var), 0.0};
  }

  double tcpa = -rel_pos.dot(rel_vel) / rel_speed_sq;
  Eigen::Vector2d cpa_vec = rel_pos + rel_vel * tcpa;
  double cpa = cpa_vec.norm();

  // Jacobian of tcpa w.r.t. rel_pos
  Eigen::RowVector2d dtcpa_dpos = -rel_vel.transpose() / rel_speed_sq;

  // Jacobian of tcpa w.r.t. rel_vel
  // d/dtcp[a]/d(vel) = -(rel_pos^T + 2*tcpa*rel_vel^T) / |rel_vel|^2
  Eigen::RowVector2d dtcpa_dvel = -(rel_pos.transpose() + 2.0 * tcpa * rel_vel.transpose())
                                / rel_speed_sq;

  // Jacobian of cpa w.r.t. rel_pos
  Eigen::RowVector2d dcpa_dpos = Eigen::RowVector2d::Zero();
  Eigen::RowVector2d dcpa_dvel = Eigen::RowVector2d::Zero();

  if (cpa > kEps) {
    dcpa_dpos = cpa_vec.transpose() / cpa;
    dcpa_dvel = tcpa * cpa_vec.transpose() / cpa;
  }

  // Propagate: sigma_y = J * sigma_x * J^T  (first-order, ignoring cross-covariance)
  double cpa_pos_var = (dcpa_dpos * sigma_rel_pos * dcpa_dpos.transpose()).value();
  double cpa_vel_var = (dcpa_dvel * sigma_rel_vel * dcpa_dvel.transpose()).value();
  double cpa_var = cpa_pos_var + cpa_vel_var;
  double tcpa_pos_var = (dtcpa_dpos * sigma_rel_pos * dtcpa_dpos.transpose()).value();
  double tcpa_vel_var = (dtcpa_dvel * sigma_rel_vel * dtcpa_dvel.transpose()).value();
  double tcpa_var = tcpa_pos_var + tcpa_vel_var;

  return {safe_sqrt(cpa_var), safe_sqrt(tcpa_var)};
}

CpaUncertainty
CpaTcpaCalculator::propagate_monte_carlo_(const Eigen::Vector2d& rel_pos,
                                          const Eigen::Vector2d& rel_vel,
                                          const Eigen::Matrix2d& sigma_rel_pos,
                                          const Eigen::Matrix2d& sigma_rel_vel) const {
  // Cholesky decomposition for sampling
  Eigen::LLT<Eigen::Matrix2d> llt_pos(sigma_rel_pos);
  Eigen::LLT<Eigen::Matrix2d> llt_vel(sigma_rel_vel);
  Eigen::Matrix2d L_pos = llt_pos.matrixL();
  Eigen::Matrix2d L_vel = llt_vel.matrixL();

  std::vector<double> cpa_samples;
  cpa_samples.reserve(static_cast<std::size_t>(cfg_.monte_carlo_samples));

  std::mt19937 gen(42);  // fixed seed for deterministic results
  std::normal_distribution<double> dist(0.0, 1.0);

  for (std::int32_t i = 0; i < cfg_.monte_carlo_samples; ++i) {
    Eigen::Vector2d noise_pos, noise_vel;
    noise_pos(0) = dist(gen);
    noise_pos(1) = dist(gen);
    noise_vel(0) = dist(gen);
    noise_vel(1) = dist(gen);

    Eigen::Vector2d sampled_pos = rel_pos + L_pos * noise_pos;
    Eigen::Vector2d sampled_vel = rel_vel + L_vel * noise_vel;

    // Compute CPA for this sample
    double rs2 = sampled_vel.squaredNorm();
    double sample_cpa;
    if (rs2 < kEps) {
      sample_cpa = sampled_pos.norm();
    } else {
      double t = -sampled_pos.dot(sampled_vel) / rs2;
      if (t < 0.0) {
        sample_cpa = sampled_pos.norm();
      } else {
        sample_cpa = (sampled_pos + sampled_vel * t).norm();
      }
    }
    cpa_samples.push_back(sample_cpa);
  }

  // CPA standard deviation from samples
  double sum = std::accumulate(cpa_samples.begin(), cpa_samples.end(), 0.0);
  double mean = sum / static_cast<double>(cpa_samples.size());

  double variance = 0.0;
  for (double s : cpa_samples) {
    double dev = s - mean;
    variance += dev * dev;
  }
  variance /= static_cast<double>(cpa_samples.size());

  // TCPA sigma via analytical Jacobian (Monte Carlo for position uncertainties)
  double tcpa_sigma = 0.0;
  double rel_speed_sq = rel_vel.squaredNorm();
  if (rel_speed_sq > kEps) {
    Eigen::RowVector2d dtcpa_dpos = -rel_vel.transpose() / rel_speed_sq;
    double tcpa_var = (dtcpa_dpos * sigma_rel_pos * dtcpa_dpos.transpose()).value();
    tcpa_sigma = safe_sqrt(tcpa_var);
  }

  return {safe_sqrt(variance), tcpa_sigma};
}

}  // namespace mass_l3::m2
