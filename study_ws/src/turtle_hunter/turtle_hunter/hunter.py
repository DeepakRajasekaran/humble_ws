#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from math import sqrt, pi, atan2

from turtlesim.msg import Pose
from geometry_msgs.msg import Twist
from irobot_interfaces.msg import TurtleArray
from irobot_interfaces.srv import KillSwitch


class Hunter_Node(Node):
    def __init__(self):
        super().__init__("hunter")
        self.get_logger().info("Hunter Node has been initiated...")

        self.pose_ = None
        self.target = None

        self.pose_subscriber_ = self.create_subscription(Pose, "turtle1/pose", self.pose_subscriber_callback, 10)
        self.cmd_vel_publisher_ = self.create_publisher(Twist, "turtle1/cmd_vel", 10)
        self.target_subscriber_ = self.create_subscription(TurtleArray, "alive_turtles", self.find_target_pray, 10)

        self.kill_request_ = self.create_client(KillSwitch, "killer")
        self.timer_ = self.create_timer(0.01, self.hunt_pray_callback)  # 100 Hz loop

    def pose_subscriber_callback(self, msg):
        self.pose_ = msg

    def find_target_pray(self, msg):
        if msg.turtles:
            self.target = msg.turtles[0]

    def hunt_pray_callback(self):
        if self.pose_ is None or self.target is None:
            return

        dist_x = self.target.x - self.pose_.x
        dist_y = self.target.y - self.pose_.y
        distance = sqrt((dist_x * dist_x) + (dist_y * dist_y))

        msg = Twist()
        kp_linear = 2.0
        kp_angular = 6.0

        if distance > 0.5:
            msg.linear.x = kp_linear * distance
            goal_theta = atan2(dist_y, dist_x)
            diff = (goal_theta - self.pose_.theta + pi) % (2 * pi) - pi
            msg.angular.z = kp_angular * diff
        else:
            msg.linear.x = 0.0
            msg.angular.z = 0.0
            self.send_kill_request(self.target.name)
            self.target = None

        self.cmd_vel_publisher_.publish(msg)

    def send_kill_request(self, turtle_name):
        if not self.kill_request_.wait_for_service(timeout_sec=1.0):
            self.get_logger().info("KillSwitch service unavailable. Retrying...")
            return

        request = KillSwitch.Request()
        request.name = turtle_name
        future = self.kill_request_.call_async(request)
        future.add_done_callback(self.kill_request_response_callback_)

    def kill_request_response_callback_(self, future):
        try:
            response = future.result()
            if not response.killed:
                self.get_logger().info("Pray escaped due to unknown reasons.")
        except Exception as e:
            self.get_logger().warn(f"Error occurred during kill request: {e}")


def main(args=None):
    rclpy.init(args=args)
    node = Hunter_Node()
    rclpy.spin(node)
    rclpy.shutdown()


if __name__ == "__main__":
    main()
