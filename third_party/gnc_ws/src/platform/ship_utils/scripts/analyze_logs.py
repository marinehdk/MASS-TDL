#!/usr/bin/env python3
"""
数据分析脚本：分析船舶动力学节点生成的 CSV 日志文件

功能：
1. 读取 ship_dynamics_node 生成的 CSV 日志文件
2. 使用 pandas 加载数据
3. 计算轨迹的曲率半径 R = √(u²+v²) / r
4. 根据参数 d_u0 和 d_u1 以及输入的推进力，计算理论终点速度 u_max
5. 绘制 2x2 的图表进行可视化
6. 通过命令行参数接受最新的 CSV 文件路径
"""

import argparse
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import fsolve


def calculate_u_max(thrust, d_u0, d_u1):
    """
    计算理论终点速度 u_max
    解方程：F = d_u0 * u + d_u1 * u²
    """
    def equation(u):
        return d_u0 * u + d_u1 * u**2 - thrust
    
    # 初始猜测值
    initial_guess = 1.0
    u_max = fsolve(equation, initial_guess)[0]
    return u_max


def analyze_logs(csv_file, d_u0=10000.0, d_u1=500.0, thrust=50000.0):
    """
    分析日志文件并生成可视化图表
    """
    # 加载数据
    print(f"加载日志文件: {csv_file}")
    df = pd.read_csv(csv_file)
    
    # 计算轨迹的曲率半径
    df['speed'] = np.sqrt(df['u']**2 + df['v']**2)
    df['curvature_radius'] = df['speed'] / np.abs(df['r'].replace(0, np.finfo(float).eps))
    
    # 计算理论终点速度
    u_max = calculate_u_max(thrust, d_u0, d_u1)
    print(f"理论终点速度 u_max: {u_max:.2f} m/s")
    
    # 2x2 图表
    fig, axs = plt.subplots(2, 2, figsize=(15, 12))
    
    # 左上：X-Y 轨迹图（大地坐标系）
    axs[0, 0].plot(df['x'], df['y'], 'b-', linewidth=2)
    axs[0, 0].set_title('船舶轨迹 (X-Y)')
    axs[0, 0].set_xlabel('X 坐标 (m)')
    axs[0, 0].set_ylabel('Y 坐标 (m)')
    axs[0, 0].grid(True)
    axs[0, 0].axis('equal')
    
    # 右上：u, v, r 随时间变化的曲线
    axs[0, 1].plot(df['time'], df['u'], 'r-', label='u (前进速度)')
    axs[0, 1].plot(df['time'], df['v'], 'g-', label='v (横向速度)')
    axs[0, 1].plot(df['time'], df['r'], 'b-', label='r (偏航角速度)')
    axs[0, 1].set_title('速度随时间变化')
    axs[0, 1].set_xlabel('时间 (s)')
    axs[0, 1].set_ylabel('速度 (m/s) / 角速度 (rad/s)')
    axs[0, 1].legend()
    axs[0, 1].grid(True)
    
    # 左下：当前速度与理论收敛速度的对比
    axs[1, 0].plot(df['time'], df['u'], 'r-', label='当前速度 u')
    axs[1, 0].axhline(y=u_max, color='g', linestyle='--', label=f'理论终点速度 u_max = {u_max:.2f}')
    axs[1, 0].set_title('速度收敛对比')
    axs[1, 0].set_xlabel('时间 (s)')
    axs[1, 0].set_ylabel('速度 (m/s)')
    axs[1, 0].legend()
    axs[1, 0].grid(True)
    
    # 右下：航向角 ψ 的变化曲线
    axs[1, 1].plot(df['time'], df['psi'], 'k-', linewidth=2)
    axs[1, 1].set_title('航向角 ψ 变化')
    axs[1, 1].set_xlabel('时间 (s)')
    axs[1, 1].set_ylabel('航向角 (rad)')
    axs[1, 1].grid(True)
    
    # 调整布局
    plt.tight_layout()
    
    # 保存图表
    output_file = csv_file.replace('.csv', '_analysis.png')
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"分析图表已保存到: {output_file}")
    
    # 显示图表
    plt.show()


def main():
    parser = argparse.ArgumentParser(description='分析船舶动力学节点日志文件')
    parser.add_argument('csv_file', type=str, help='CSV 日志文件路径')
    parser.add_argument('--d_u0', type=float, default=10000.0, help='阻力系数 d_u0')
    parser.add_argument('--d_u1', type=float, default=500.0, help='阻力系数 d_u1')
    parser.add_argument('--thrust', type=float, default=50000.0, help='推进力 (N)')
    
    args = parser.parse_args()
    
    analyze_logs(args.csv_file, args.d_u0, args.d_u1, args.thrust)


if __name__ == '__main__':
    main()
