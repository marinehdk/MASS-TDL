#include "m2_world_model/cpa_tcpa_calculator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
  double dt_sec = (own_ship.stamp - target.stamp).seconds();
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

  // ── 3a. Static target detection ──
  // Targets below the speed threshold are treated as fixed obstacles:
  // CPA = current separation, TCPA = 0 (immediate risk based on current range).
  if (tgt_aligned.sog_kn * kKnToMs < cfg_.static_target_speed_mps) {
    double cpa_raw = rel_pos.norm();
    double final_cpa = cpa_raw * cfg_.safety_factor;
    if (odd_zone == OddZone::D) {
      final_cpa *= cfg_.odd_d_multiplier;
    }
    return CpaResult{final_cpa, 0.0, CpaUncertainty{0.0, 0.0}};
  }

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
    case UncertaintyMethod::UkfSigma:
      unc = propagate_ukf_(rel_pos, rel_vel, sigma_rel, sigma_rel_vel);
      break;
    case UncertaintyMethod::CeAdaptive:
      // Phase 3 placeholder — conservative defaults
      unc = {50.0, 10.0};
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
                                   TimePoint tgt_time) const {
  double dt = (tgt_time - target.stamp).seconds();

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

CpaUncertainty
CpaTcpaCalculator::propagate_ukf_(const Eigen::Vector2d& rel_pos,
                                   const Eigen::Vector2d& rel_vel,
                                   const Eigen::Matrix2d& sigma_rel_pos,
                                   const Eigen::Matrix2d& sigma_rel_vel) const {
  // ── Low-speed fallback: use position covariance directly ──
  double rel_speed = rel_vel.norm();
  if (rel_speed < cfg_.min_rel_speed_for_ukf_ms) {
    double cpa_var = (rel_pos.transpose() * sigma_rel_pos * rel_pos).value();
    cpa_var /= rel_pos.squaredNorm() + kEps;
    // Low-speed: CPA variance from position uncertainty, TCPA unbounded
    double cpa_sigma = safe_sqrt(cpa_var);
    return {cpa_sigma, std::numeric_limits<double>::infinity()};
  }

  constexpr std::size_t n = 4;              // state dimension
  constexpr std::size_t n_sig = 2 * n + 1;  // 9 sigma points

  // Assemble mean state and covariance
  Eigen::Vector4d x_mean;
  x_mean << rel_pos(0), rel_pos(1), rel_vel(0), rel_vel(1);

  Eigen::Matrix4d P = Eigen::Matrix4d::Zero();
  P.block<2, 2>(0, 0) = sigma_rel_pos;
  P.block<2, 2>(2, 2) = sigma_rel_vel;

  // UKF scaling parameters
  const double alpha = cfg_.ukf_alpha;
  const double beta = cfg_.ukf_beta;
  const double kappa = cfg_.ukf_kappa;
  const double lambda = alpha * alpha * (static_cast<double>(n) + kappa)
                      - static_cast<double>(n);
  const double n_plus_lambda = static_cast<double>(n) + lambda;

  // Cholesky factor of (n+λ)P
  Eigen::Matrix4d P_scaled = n_plus_lambda * P;
  Eigen::LLT<Eigen::Matrix4d> llt(P_scaled);
  if (llt.info() != Eigen::Success) {
    // Cholesky failed (P not positive-definite) → fall back to linear propagation
    return propagate_linear_(rel_pos, rel_vel, sigma_rel_pos, sigma_rel_vel);
  }
  Eigen::Matrix4d L = llt.matrixL();

  // Generate 2n+1 sigma points (columns of X_sig)
  Eigen::Matrix<double, n, n_sig> X_sig;
  X_sig.col(0) = x_mean;
  for (std::size_t i = 0; i < n; ++i) {
    X_sig.col(static_cast<Eigen::Index>(i + 1))       = x_mean + L.col(static_cast<Eigen::Index>(i));
    X_sig.col(static_cast<Eigen::Index>(i + 1 + n))   = x_mean - L.col(static_cast<Eigen::Index>(i));
  }

  // Weights
  double w_m0 = lambda / n_plus_lambda;
  double w_c0 = lambda / n_plus_lambda + (1.0 - alpha * alpha + beta);
  double w_i  = 1.0 / (2.0 * n_plus_lambda);

  // Propagate each sigma point through observation h(x) = [CPA, TCPA]^T
  constexpr std::size_t obs_dim = 2;
  Eigen::Matrix<double, obs_dim, n_sig> Y_sig;
  for (std::size_t i = 0; i < n_sig; ++i) {
    Eigen::Vector2d rp = X_sig.block<2, 1>(0, static_cast<Eigen::Index>(i));
    Eigen::Vector2d rv = X_sig.block<2, 1>(2, static_cast<Eigen::Index>(i));

    double rss = rv.squaredNorm();
    double cpa, tcpa;
    if (rss < kEps) {
      cpa = rp.norm();
      tcpa = 0.0;
    } else {
      tcpa = -rp.dot(rv) / rss;
      if (tcpa < 0.0) {
        cpa = rp.norm();
        tcpa = 0.0;
      } else {
        cpa = (rp + rv * tcpa).norm();
      }
    }
    Y_sig(0, static_cast<Eigen::Index>(i)) = cpa;
    Y_sig(1, static_cast<Eigen::Index>(i)) = tcpa;
  }

  // Weighted mean of observations
  Eigen::Vector2d y_mean = w_m0 * Y_sig.col(0);
  for (std::size_t i = 1; i < n_sig; ++i) {
    y_mean += w_i * Y_sig.col(static_cast<Eigen::Index>(i));
  }

  // Weighted observation covariance
  Eigen::Matrix2d P_yy = Eigen::Matrix2d::Zero();
  Eigen::Vector2d d0 = Y_sig.col(0) - y_mean;
  P_yy += w_c0 * d0 * d0.transpose();
  for (std::size_t i = 1; i < n_sig; ++i) {
    Eigen::Vector2d d = Y_sig.col(static_cast<Eigen::Index>(i)) - y_mean;
    P_yy += w_i * d * d.transpose();
  }

  return {safe_sqrt(P_yy(0, 0)), safe_sqrt(P_yy(1, 1))};
}

}  // namespace mass_l3::m2
