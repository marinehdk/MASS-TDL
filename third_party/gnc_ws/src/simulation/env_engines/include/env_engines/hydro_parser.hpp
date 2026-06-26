#ifndef ENV_ENGINES_HYDRO_PARSER_HPP
#define ENV_ENGINES_HYDRO_PARSER_HPP

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace env_engines {

struct DataPoint1D {
    double x = 0.0;
    std::vector<double> values;
};

struct DataPoint2D {
    double x = 0.0;
    double y = 0.0;
    std::vector<double> values;
};

class HydroParser {
public:
    HydroParser() = default;

    bool load_1d_csv(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        data_1d_.clear();
        std::string line;
        std::getline(file, line); // header

        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }
            std::stringstream ss(line);
            std::string cell;
            DataPoint1D pt;

            if (!std::getline(ss, cell, ',')) {
                continue;
            }
            pt.x = std::stod(cell);
            while (std::getline(ss, cell, ',')) {
                pt.values.push_back(std::stod(cell));
            }
            if (!pt.values.empty()) {
                data_1d_.push_back(pt);
            }
        }

        std::sort(data_1d_.begin(), data_1d_.end(),
                  [](const DataPoint1D& a, const DataPoint1D& b) {
                      return a.x < b.x;
                  });
        return !data_1d_.empty();
    }

    bool load_2d_csv(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        data_2d_.clear();
        std::string line;
        std::getline(file, line); // header

        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }
            std::stringstream ss(line);
            std::string cell;
            DataPoint2D pt;

            if (!std::getline(ss, cell, ',')) {
                continue;
            }
            pt.x = std::stod(cell);
            if (!std::getline(ss, cell, ',')) {
                continue;
            }
            pt.y = normalize_degrees(std::stod(cell));
            while (std::getline(ss, cell, ',')) {
                pt.values.push_back(std::stod(cell));
            }
            if (!pt.values.empty()) {
                data_2d_.push_back(pt);
            }
        }

        std::sort(data_2d_.begin(), data_2d_.end(),
                  [](const DataPoint2D& a, const DataPoint2D& b) {
                      if (std::abs(a.x - b.x) > 1.0e-9) {
                          return a.x < b.x;
                      }
                      return a.y < b.y;
                  });
        return !data_2d_.empty();
    }

    std::vector<double> interpolate_1d(double x_target) const {
        if (data_1d_.empty()) {
            return {};
        }
        const int n_vals = static_cast<int>(data_1d_[0].values.size());
        x_target = normalize_degrees(x_target);

        for (size_t i = 0; i + 1 < data_1d_.size(); ++i) {
            if (x_target >= data_1d_[i].x && x_target < data_1d_[i + 1].x) {
                const double denom = data_1d_[i + 1].x - data_1d_[i].x;
                const double t = denom > 1.0e-12 ? (x_target - data_1d_[i].x) / denom : 0.0;
                std::vector<double> res(n_vals, 0.0);
                for (int j = 0; j < n_vals; ++j) {
                    res[j] = lerp(data_1d_[i].values[j], data_1d_[i + 1].values[j], t);
                }
                return res;
            }
        }

        const auto& last = data_1d_.back();
        const auto& first = data_1d_.front();
        const double span = 360.0 - last.x + first.x;
        const double t = span > 1.0e-12 ? (x_target - last.x) / span : 0.0;
        std::vector<double> res(n_vals, 0.0);
        for (int j = 0; j < n_vals; ++j) {
            res[j] = lerp(last.values[j], first.values[j], std::clamp(t, 0.0, 1.0));
        }
        return res;
    }

    std::vector<double> interpolate_2d(double x_target, double y_target) const {
        if (data_2d_.empty()) {
            return {};
        }

        std::vector<double> xs;
        std::vector<double> ys;
        collect_axes(xs, ys);
        if (xs.empty() || ys.empty()) {
            return {};
        }

        x_target = std::clamp(x_target, xs.front(), xs.back());
        y_target = normalize_degrees(y_target);

        double x0 = xs.front();
        double x1 = xs.front();
        bracket_linear(xs, x_target, x0, x1);

        double y0 = ys.front();
        double y1 = ys.front();
        double y_eval = y_target;
        bracket_periodic_degrees(ys, y_target, y0, y1, y_eval);

        const auto q00 = values_at(x0, y0);
        const auto q10 = values_at(x1, y0);
        const auto q01 = values_at(x0, y1);
        const auto q11 = values_at(x1, y1);
        if (q00.empty() || q10.empty() || q01.empty() || q11.empty()) {
            return nearest_2d(x_target, y_target);
        }

        const size_t n_vals = q00.size();
        if (q10.size() != n_vals || q01.size() != n_vals || q11.size() != n_vals) {
            return nearest_2d(x_target, y_target);
        }

        const double tx = std::abs(x1 - x0) > 1.0e-12 ? (x_target - x0) / (x1 - x0) : 0.0;
        const double ty = std::abs(y1 - y0) > 1.0e-12 ? (y_eval - y0) / (y1 - y0) : 0.0;

        std::vector<double> out(n_vals, 0.0);
        for (size_t i = 0; i < n_vals; ++i) {
            const double a = lerp(q00[i], q10[i], std::clamp(tx, 0.0, 1.0));
            const double b = lerp(q01[i], q11[i], std::clamp(tx, 0.0, 1.0));
            out[i] = lerp(a, b, std::clamp(ty, 0.0, 1.0));
        }
        return out;
    }

    std::vector<double> nearest_2d(double x_target, double y_target) const {
        if (data_2d_.empty()) {
            return {};
        }

        y_target = normalize_degrees(y_target);
        double min_dist = 1.0e18;
        size_t best_idx = 0;

        for (size_t i = 0; i < data_2d_.size(); ++i) {
            const double dx = data_2d_[i].x - x_target;
            double dy = std::abs(data_2d_[i].y - y_target);
            if (dy > 180.0) {
                dy = 360.0 - dy;
            }
            const double dist = std::sqrt((dx * 100.0) * (dx * 100.0) + dy * dy);
            if (dist < min_dist) {
                min_dist = dist;
                best_idx = i;
            }
        }
        return data_2d_[best_idx].values;
    }

private:
    static double normalize_degrees(double angle) {
        if (!std::isfinite(angle)) {
            return 0.0;
        }
        angle = std::fmod(angle, 360.0);
        if (angle < 0.0) {
            angle += 360.0;
        }
        return angle;
    }

    static double lerp(double a, double b, double t) {
        return a + (b - a) * t;
    }

    static bool nearly_equal(double a, double b) {
        return std::abs(a - b) < 1.0e-9;
    }

    void collect_axes(std::vector<double>& xs, std::vector<double>& ys) const {
        xs.clear();
        ys.clear();
        for (const auto& pt : data_2d_) {
            xs.push_back(pt.x);
            ys.push_back(normalize_degrees(pt.y));
        }
        auto unique_sorted = [](std::vector<double>& values) {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end(),
                                     [](double a, double b) { return std::abs(a - b) < 1.0e-9; }),
                         values.end());
        };
        unique_sorted(xs);
        unique_sorted(ys);
    }

    static void bracket_linear(const std::vector<double>& axis, double target,
                               double& lower, double& upper) {
        lower = axis.front();
        upper = axis.front();
        if (target <= axis.front()) {
            return;
        }
        if (target >= axis.back()) {
            lower = axis.back();
            upper = axis.back();
            return;
        }
        for (size_t i = 0; i + 1 < axis.size(); ++i) {
            if (target >= axis[i] && target <= axis[i + 1]) {
                lower = axis[i];
                upper = axis[i + 1];
                return;
            }
        }
    }

    static void bracket_periodic_degrees(const std::vector<double>& axis, double target,
                                         double& lower, double& upper, double& eval_target) {
        lower = axis.front();
        upper = axis.front();
        eval_target = target;
        if (axis.size() == 1) {
            return;
        }
        for (size_t i = 0; i + 1 < axis.size(); ++i) {
            if (target >= axis[i] && target <= axis[i + 1]) {
                lower = axis[i];
                upper = axis[i + 1];
                return;
            }
        }
        lower = axis.back();
        upper = axis.front() + 360.0;
        eval_target = target < axis.front() ? target + 360.0 : target;
    }

    std::vector<double> values_at(double x, double y) const {
        const double y_norm = normalize_degrees(y);
        for (const auto& pt : data_2d_) {
            if (nearly_equal(pt.x, x) && nearly_equal(normalize_degrees(pt.y), y_norm)) {
                return pt.values;
            }
        }
        return {};
    }

    std::vector<DataPoint1D> data_1d_;
    std::vector<DataPoint2D> data_2d_;
};

} // namespace env_engines

#endif // ENV_ENGINES_HYDRO_PARSER_HPP
