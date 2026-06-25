#!/usr/bin/env python3
"""
船舶物理性能测试脚本

功能：
1. 订阅 /ship/odometry 话题
2. 实时计算总速度、加速度和回转半径
3. 基于船舶吨位评估运行状态
4. 每秒打印一次船舶运行体检报告
"""

import rclpy
from rclpy.node import Node
from nav_msgs.msg import Odometry
import numpy as np
import time


class ShipPhysicsTester(Node):
    def __init__(self):
        super().__init__('ship_physics_tester')
        
        # 订阅 odometry 话题
        self.odom_sub = self.create_subscription(
            Odometry,
            '/ship/odometry',
            self.odom_callback,
            10
        )
        
        # 船舶参数
        self.ship_mass = 1.6e7  # 16,000 吨
        self.ship_length = 150.0  # 船长 150m
        
        # 历史数据
        self.last_time = None
        self.last_speed = 0.0
        self.last_timestamp = None
        
        # 体检报告计时器
        self.report_timer = self.create_timer(1.0, self.print_report)
        
        # 当前状态
        self.current_speed = 0.0
        self.current_acceleration = 0.0
        self.current_radius = 0.0
        self.current_angular_velocity = 0.0
        
        self.get_logger().info('Ship physics tester initialized')
    
    def odom_callback(self, msg):
        """处理 odometry 消息，计算物理参数"""
        # 提取速度信息
        u = msg.twist.twist.linear.x
        v = msg.twist.twist.linear.y
        r = msg.twist.twist.angular.z
        
        # 计算总速度
        speed = np.sqrt(u**2 + v**2)
        self.current_speed = speed
        self.current_angular_velocity = r
        
        # 计算加速度
        current_time = time.time()
        if self.last_time is not None:
            time_diff = current_time - self.last_time
            if time_diff > 0:
                acceleration = (speed - self.last_speed) / time_diff
                self.current_acceleration = acceleration
                
                # 加速度评估
                if abs(acceleration) > 0.1:
                    self.get_logger().warn('Acceleration too high! Check mass or thrust.')
        
        # 计算回转半径
        if abs(r) > 1e-6:
            radius = speed / abs(r)
            self.current_radius = radius
            
            # 回转半径评估
            if radius < 150.0:
                self.get_logger().warn('Turning too sharp! Check rotational inertia.')
        else:
            self.current_radius = float('inf')
        
        # 速度评估
        if speed > 15.0:
            self.get_logger().warn('Speed exceeds realistic limits for bulk carriers.')
        
        # 更新历史数据
        self.last_speed = speed
        self.last_time = current_time
        self.last_timestamp = msg.header.stamp
    
    def print_report(self):
        """打印船舶运行体检报告"""
        if self.last_timestamp is not None:
            report = f"\n=== 船舶运行体检报告 ==="
            report += f"时间: {self.last_timestamp.sec}.{self.last_timestamp.nanosec // 1000000:03d}\n"
            report += f"总速度: {self.current_speed:.2f} m/s ({self.current_speed * 3.6:.1f} km/h)\n"
            report += f"加速度: {self.current_acceleration:.3f} m/s²\n"
            report += f"角速度: {self.current_angular_velocity:.3f} rad/s\n"
            
            if self.current_radius == float('inf'):
                report += f"回转半径: 直线航行\n"
            else:
                report += f"回转半径: {self.current_radius:.1f} m\n"
            
            # 评估结果
            report += "\n=== 评估结果 ===\n"
            
            # 速度评估
            if self.current_speed > 15.0:
                report += "❌ 速度超过散货船合理范围\n"
            elif self.current_speed > 12.0:
                report += "⚠️  速度接近合理范围上限\n"
            else:
                report += "✅ 速度在合理范围内\n"
            
            # 加速度评估
            if abs(self.current_acceleration) > 0.1:
                report += "❌ 加速度过高\n"
            else:
                report += "✅ 加速度在合理范围内\n"
            
            # 回转半径评估
            if self.current_radius != float('inf'):
                if self.current_radius < 150.0:
                    report += "❌ 回转半径过小\n"
                elif self.current_radius < 300.0:
                    report += "⚠️  回转半径接近合理范围下限\n"
                else:
                    report += "✅ 回转半径在合理范围内\n"
            else:
                report += "✅ 直线航行\n"
            
            self.get_logger().info(report)
        else:
            self.get_logger().info("等待 odometry 数据...")


def main(args=None):
    rclpy.init(args=args)
    node = ShipPhysicsTester()
    
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
