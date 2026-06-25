#!/usr/bin/env python3
"""
船舶可视化适配器节点

功能：
1. 订阅 /ship/odometry 话题
2. 发布 map -> base_link 的 TF 变换
3. 发布船体形状的 visualization_msgs/Marker 消息
4. 使 Foxglove 能够实时显示船舶模型
"""

import rclpy
from rclpy.node import Node
from nav_msgs.msg import Odometry
from geometry_msgs.msg import TransformStamped, Point
from visualization_msgs.msg import Marker
import tf2_ros
import numpy as np


class ShipVizAdapter(Node):
    def __init__(self):
        super().__init__('ship_viz_adapter')
        
        # 订阅 odometry 话题
        self.odom_sub = self.create_subscription(
            Odometry,
            '/ship/odometry',
            self.odom_callback,
            10
        )
        
        # TF 广播器
        self.tf_broadcaster = tf2_ros.TransformBroadcaster(self)
        
        # Marker 发布者
        self.marker_pub = self.create_publisher(
            Marker,
            '/ship/marker',
            10
        )
        
        # 发布初始 Marker
        self.publish_ship_marker()
        
        self.get_logger().info('Ship visualization adapter node initialized')
    
    def odom_callback(self, msg):
        """处理 odometry 消息，发布 TF 变换和 Marker"""
        # 创建 TF 变换
        t = TransformStamped()
        t.header.stamp = self.get_clock().now().to_msg()
        t.header.frame_id = 'map'
        t.child_frame_id = 'base_link'
        
        # 设置位置
        t.transform.translation.x = msg.pose.pose.position.x
        t.transform.translation.y = msg.pose.pose.position.y
        t.transform.translation.z = msg.pose.pose.position.z
        
        # 设置姿态
        t.transform.rotation = msg.pose.pose.orientation
        
        # 发布 TF 变换
        self.tf_broadcaster.sendTransform(t)
        
        # 同时发布 Marker
        self.publish_ship_marker()
    
    def publish_ship_marker(self):
        """发布船体形状的 Marker 消息"""
        marker = Marker()
        marker.header.frame_id = 'base_link'
        marker.header.stamp = self.get_clock().now().to_msg()
        marker.ns = 'ship_model'
        marker.id = 0
        marker.type = Marker.CUBE
        marker.action = Marker.ADD
        
        # 船体尺寸：长 50m，宽 12m，高 5m
        marker.scale.x = 50.0
        marker.scale.y = 12.0
        marker.scale.z = 5.0
        
        # 位置（相对于 base_link 坐标系）
        marker.pose.position.x = 0.0
        marker.pose.position.y = 0.0
        marker.pose.position.z = 2.5  # 船体中心在水面上 2.5m
        
        # 姿态（相对于 base_link 坐标系）
        marker.pose.orientation.x = 0.0
        marker.pose.orientation.y = 0.0
        marker.pose.orientation.z = 0.0
        marker.pose.orientation.w = 1.0
        
        # 颜色：蓝色船体，确保不透明
        marker.color.r = 0.0
        marker.color.g = 0.0
        marker.color.b = 1.0
        marker.color.a = 1.0
        
        # 发布 Marker
        self.marker_pub.publish(marker)
        self.get_logger().debug('Published ship marker')


def main(args=None):
    rclpy.init(args=args)
    node = ShipVizAdapter()
    
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
