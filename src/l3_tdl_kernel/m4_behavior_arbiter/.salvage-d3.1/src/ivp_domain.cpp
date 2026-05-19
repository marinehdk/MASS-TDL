#include "m4_behavior_arbiter/ivp_domain.hpp"

#include <cmath>
#include <stdexcept>

namespace {

size_t validated_heading_size(double resolution_deg) {
  if (resolution_deg <= 0.0) {
    throw std::invalid_argument("IvPHeadingDomain: resolution_deg must be > 0");
  }
  return static_cast<size_t>(360.0 / resolution_deg);
}

size_t validated_speed_size(double min_kn, double max_kn, double resolution_kn) {
  if (resolution_kn <= 0.0) {
    throw std::invalid_argument("IvPSpeedDomain: resolution_kn must be > 0");
  }
  if (max_kn <= min_kn) {
    throw std::invalid_argument("IvPSpeedDomain: max_kn must be > min_kn");
  }
  return static_cast<size_t>((max_kn - min_kn) / resolution_kn) + 1U;
}

}  // namespace

namespace mass_l3::m4 {

IvPHeadingDomain::IvPHeadingDomain(double resolution_deg)
    : resolution_deg_(resolution_deg)
    , size_(validated_heading_size(resolution_deg))
{}

double IvPHeadingDomain::at(size_t i) const {
  if (i >= size_) {
    throw std::out_of_range("IvPHeadingDomain::at: index out of range");
  }
  return static_cast<double>(i) * resolution_deg_;
}

double IvPHeadingDomain::wrap(double psi_deg) const {
  double wrapped = std::fmod(psi_deg, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return wrapped;
}

IvPSpeedDomain::IvPSpeedDomain(double min_kn, double max_kn, double resolution_kn)
    : min_kn_(min_kn)
    , max_kn_(max_kn)
    , resolution_kn_(resolution_kn)
    , size_(validated_speed_size(min_kn, max_kn, resolution_kn))
{}

double IvPSpeedDomain::at(size_t i) const {
  if (i >= size_) {
    throw std::out_of_range("IvPSpeedDomain::at: index out of range");
  }
  return min_kn_ + static_cast<double>(i) * resolution_kn_;
}

}  // namespace mass_l3::m4
