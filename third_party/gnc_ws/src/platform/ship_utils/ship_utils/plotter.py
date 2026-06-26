#!/usr/bin/env python3
"""
Plotter for ship simulation data
"""

import argparse
import csv
import matplotlib.pyplot as plt
import numpy as np

def plot_ship_trajectory(input_file, output_file):
    """Plot ship trajectory from log file"""
    print(f"Plotting ship trajectory from: {input_file}")
    
    # 读取数据
    timestamps = []
    x_positions = []
    y_positions = []
    headings = []
    
    with open(input_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            timestamps.append(row['timestamp'])
            x_positions.append(float(row['x']))
            y_positions.append(float(row['y']))
            headings.append(float(row['heading']))
    
    # 创建图形
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    
    # 绘制轨迹
    ax1.plot(x_positions, y_positions, 'b-', label='Ship Trajectory')
    ax1.set_xlabel('X Position (m)')
    ax1.set_ylabel('Y Position (m)')
    ax1.set_title('Ship Trajectory')
    ax1.legend()
    ax1.grid(True)
    
    # 绘制航向
    ax2.plot(range(len(headings)), headings, 'r-', label='Heading')
    ax2.set_xlabel('Time Step')
    ax2.set_ylabel('Heading (rad)')
    ax2.set_title('Ship Heading')
    ax2.legend()
    ax2.grid(True)
    
    # 保存图形
    plt.tight_layout()
    plt.savefig(output_file)
    print(f"Plot saved to: {output_file}")

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='Plot ship simulation data')
    parser.add_argument('input_file', help='Input log file path')
    parser.add_argument('output_file', help='Output plot file path')
    args = parser.parse_args()
    
    plot_ship_trajectory(args.input_file, args.output_file)

if __name__ == '__main__':
    main()