#ifndef THRUST_ALLOCATION_PGD_SOLVER_HPP
#define THRUST_ALLOCATION_PGD_SOLVER_HPP

#include <Eigen/Dense>
#include <vector>
#include <cmath>

namespace thrust_allocation {

/**
 * @brief 投影约束结构体（对应原代码中的 ProjectionConstraint）
 */
struct ProjectionConstraint {
    int type;          // 1 = 方位（两列同界）, 0 = 固定/舵（单列）
    int idx_start;     // 约束对应的起始变量索引
    double min_bound;  // 下限 (kN 或 rad)
    double max_bound;  // 上限 (kN 或 rad)
};

/**
 * @brief PGD (Projected Gradient Descent) 求解器
 *
 * 求解带 box 约束的二次规划：
 *   min: 0.5 * x^T * H * x + c^T * x
 *   s.t. x_i in [min_i, max_i]
 *
 * 算法：投影梯度下降 + Barzilai-Borwein 步长
 * 收敛速度比固定步长快，适用于中小规模问题
 */
class PGDSolver {
public:
    /**
     * @brief 求解 QP
     * @param H 海森矩阵 (n×n)，需正定
     * @param c 线性项向量 (n)
     * @param constraints 投影约束列表
     * @param x_init 初始解 (n)，通常为上一时刻的解
     * @param max_iter 最大迭代次数
     * @param tol 收敛容忍度（梯度范数）
     * @return Eigen::VectorXd 最优解
     */
    static Eigen::VectorXd solve(
        const Eigen::MatrixXd& H,
        const Eigen::VectorXd& c,
        const std::vector<ProjectionConstraint>& constraints,
        const Eigen::VectorXd& x_init,
        int max_iter = 2000,  // [FIX] improved convergence
        double tol = 1e-6
    ) {
        Eigen::VectorXd x = x_init;
        Eigen::VectorXd x_prev = x;

        // 投影初始解到可行域
        project(x, constraints);

        // 【修复1】初始步长用 H 最大对角元估算，而不是固定 0.01
        // 最优步长 ≈ 1/λ_max(H)，H对角元是特征值的近似上界
        // 你的 H 对角元在 10000~16400 量级，最优步长约 6e-5
        // 原来固定 0.01 比最优步长大 160 倍，导致每次都触发线搜索回退
        const double alpha_max = 1.0 / (H.diagonal().maxCoeff() + 1e-10);
        double alpha = alpha_max;

        for (int iter = 0; iter < max_iter; ++iter) {
            // 梯度 = H * x + c
            Eigen::VectorXd grad = H * x + c;

            // 投影梯度下降方向
            Eigen::VectorXd d = -grad;

            // 【修复2】Armijo 线搜索条件修正
            // 原代码：f_x_new > f_x + c1 * alpha * grad.dot(x_new - x)
            //   问题：x_new 经过投影后，x_new-x 方向可能与 grad 同向，
            //         导致右侧变大，条件永远成立，alpha 被缩到最小值
            // 修复：用 -c1 * alpha * ||grad||² 作为下降量下界（标准充分下降条件）
            double beta = 0.5;
            double c1 = 1e-4;
            double grad_sq = grad.squaredNorm();

            Eigen::VectorXd x_new = x + alpha * d;
            project(x_new, constraints);

            double f_x     = 0.5 * x.dot(H * x) + c.dot(x);
            double f_x_new = 0.5 * x_new.dot(H * x_new) + c.dot(x_new);

            int ls_iter = 0;
            while (f_x_new > f_x - c1 * alpha * grad_sq && ls_iter < 20) {
                alpha *= beta;
                x_new = x + alpha * d;
                project(x_new, constraints);
                f_x_new = 0.5 * x_new.dot(H * x_new) + c.dot(x_new);
                ++ls_iter;
            }

            x_prev = x;
            x = x_new;

            // 【修复3】BB步长上界保护
            // 原代码上界 1e6，而最优步长约 6e-5，相差 100亿倍
            // BB 步长偶尔跳到 1e6 会让下一步直接发散，再由线搜索缩回来，
            // 浪费大量迭代次数（每次发散都需要 20 次线搜索回退）
            // 修复：上界限制为 10 * alpha_max（允许一定超调但不发散）
            Eigen::VectorXd s = x - x_prev;
            Eigen::VectorXd y = (H * x + c) - (H * x_prev + c);  // = H * s（更高效）
            if (s.norm() > 1e-12 && y.norm() > 1e-12) {
                double sTy = s.dot(y);
                if (sTy > 0) {
                    double alpha_bb = std::abs(s.dot(s) / sTy);
                    // 上界：最多允许 10 倍最优步长，防止发散
                    // 下界：1e-8 防止步长退化到零
                    alpha = std::clamp(alpha_bb, 1e-8, alpha_max * 10.0);
                }
                // sTy <= 0 说明曲率为负（数值问题），保持当前步长不更新
            }

            // 收敛判断：投影梯度范数
            // proj_grad = x - P[x - grad]，在最优解处为零向量
            Eigen::VectorXd proj_grad = x - project_vector(x - grad, constraints);
            if (proj_grad.norm() < tol) {
                break;
            }
        }

        return x;
    }

private:
    /**
     * @brief 投影单变量到其约束范围
     */
    static double project_single(double val, const std::vector<ProjectionConstraint>& constraints, int var_idx) {
        double lo = -1e10;
        double hi =  1e10;

        for (const auto& cons : constraints) {
            if (cons.type == 1) {
                // 方位推进器：约束 idx_start 和 idx_start+1
                if (var_idx == cons.idx_start || var_idx == cons.idx_start + 1) {
                    lo = std::max(lo, cons.min_bound);
                    hi = std::min(hi, cons.max_bound);
                }
            } else {
                // 固定/舵：约束 idx_start
                if (var_idx == cons.idx_start) {
                    lo = std::max(lo, cons.min_bound);
                    hi = std::min(hi, cons.max_bound);
                }
            }
        }
        return std::clamp(val, lo, hi);
    }

    /**
     * @brief 投影整个向量到可行域（逐元素投影）
     */
    static void project(Eigen::VectorXd& x, const std::vector<ProjectionConstraint>& constraints) {
        for (int i = 0; i < x.size(); ++i) {
            x(i) = project_single(x(i), constraints, i);
        }
    }

    /**
     * @brief 返回投影后的新向量（不修改原向量，用于收敛判断）
     * 原名 project_single(VectorXd) 与标量版重名，改为 project_vector 消除歧义
     */
    static Eigen::VectorXd project_vector(
        const Eigen::VectorXd& x,
        const std::vector<ProjectionConstraint>& constraints)
    {
        Eigen::VectorXd result = x;
        for (int i = 0; i < result.size(); ++i) {
            result(i) = project_single(result(i), constraints, i);
        }
        return result;
    }
};

}  // namespace thrust_allocation

#endif  // THRUST_ALLOCATION_PGD_SOLVER_HPP