#include <rclcpp/rclcpp.hpp>
#include <cmath>

#include <irobot_interfaces/msg/turtle_array.hpp>
#include <irobot_interfaces/msg/turtleinfo.hpp>
#include <irobot_interfaces/srv/kill_switch.hpp>

#include <geometry_msgs/msg/twist.hpp>
#include <turtlesim/msg/pose.hpp>

// @brief for testing purpose..
#include <geometry_msgs/msg/pose2_d.hpp>

/* @todo Work with the namespaces...
*/

class Hunter_Node : public rclcpp::Node 
{
public:
    Hunter_Node() : Node("hunter") 
    {
        RCLCPP_INFO(this->get_logger(), "Hunter_Node has been initiated...");

        // parameters
        this->declare_parameter<int>("kill_closest_turtle_first", false);
        this->declare_parameter<int>("hunter_freq", 1);
        this->declare_parameter<bool>("test_mode", 0);

        this->declare_parameter<float>("kp_linear", 1);
        this->declare_parameter<float>("kp_angular", 1);

        kp_linear                 = this->get_parameter("kp_linear").get_value<float>();
        kp_angular                = this->get_parameter("kp_angular").get_value<float>();

        test_mode                 = this->get_parameter("test_mode").get_value<bool>();
        kill_closest_turtle_first = this->get_parameter("kill_closest_turtle_first").get_value<int>();
        hunter_frequency          = this->get_parameter("hunter_freq").get_value<int>();
        
       // Subscribers and publishers
        pose_subscriber = this->create_subscription<turtlesim::msg::Pose>(
            "turtle1/pose", 10, std::bind(&Hunter_Node::pose_subscriber_callback, this, std::placeholders::_1));

        target_subscriber = this->create_subscription<irobot_interfaces::msg::TurtleArray>(
            "alive_turtles", 10, std::bind(&Hunter_Node::find_target_pray, this, std::placeholders::_1));

        if (test_mode) {
            test_vertex_subscriber = this->create_subscription<geometry_msgs::msg::Pose2D>(
                "test_vertex", 10, std::bind(&Hunter_Node::test_vertex_callback, this,  std::placeholders::_1));
        }

        cmd_vel_publisher = this->create_publisher<geometry_msgs::msg::Twist>("turtle1/cmd_vel", 10);

        // services
        kill_request_ = this->create_client<irobot_interfaces::srv::KillSwitch>("killer");
        
        // timer_callbacks
        timer_ = this->create_wall_timer(
            std::chrono::seconds(this->hunter_frequency),
            std::bind(&Hunter_Node::hunt, this));
        
    }

/** 
 *  @brief we have to use destructor to destroy all the threads when the node is exiting
 *
 *  @todo Learn about this topic, and understand how shit works...
 * 
*/

    // Destructor
    ~Hunter_Node() 
    {
        RCLCPP_INFO(this->get_logger(), "Hunter_Node is shutting down...");

        // Join all threads to ensure clean exit
        for (auto &thread : Kill_request_thread) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        RCLCPP_INFO(this->get_logger(), "All threads have been joined. Node shutdown complete.");
    }

private:

// functions

    double calculate_distance(double x1, double y1, double x2, double y2){
        return std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    }

    void target_reset(){
        if (current_target.locked) {
            current_target = Target();
        }
    }

    bool pose_setted_and_target_locked(){
        return (self_pose_ && current_target.locked);
    }

// Callbacks    

    void hunt(){
        test_mode ? test_ride() : hunt_pray_callback();
    }

    void pose_subscriber_callback(const turtlesim::msg::Pose::SharedPtr msg){
        self_pose_ = msg;
    }

    void test_vertex_callback(const geometry_msgs::msg::Pose2D::SharedPtr msg)
    {   
        //test_goal = nullptr;
        test_goal = msg;
    }

    void find_target_pray(const irobot_interfaces::msg::TurtleArray::SharedPtr msg){
        if(!msg->turtles.empty()) {
            if (kill_closest_turtle_first){
                auto closest_turtle          = current_target.info;
                auto closest_turtle_distance = current_target.distance;

                if (!current_target.locked) {
                    for (const auto& turtle : msg->turtles) {
                        double distance = calculate_distance(self_pose_->x, 
                                                             self_pose_->y, 
                                                             turtle.x, 
                                                             turtle.y);

                        // If closest_turtle_distance is unset (0.0) or a closer turtle is found
                        bool new_turtle_closer = (!closest_turtle || distance < closest_turtle_distance);

                        if (new_turtle_closer) {
                            closest_turtle_distance = distance;
                            closest_turtle          = std::make_shared<irobot_interfaces::msg::Turtleinfo>(turtle); 
                        }
                    }
                }
                else {
                    closest_turtle_distance = calculate_distance(self_pose_->x, 
                                                                 self_pose_->y, 
                                                                 current_target.info->x, 
                                                                 current_target.info->y);
                }

                // Update the target
                current_target.info     = closest_turtle;
                current_target.distance = closest_turtle_distance;

                // Compute the goal angle
                if (closest_turtle) {
                    double dist_x        = closest_turtle->x - self_pose_->x;
                    double dist_y        = closest_turtle->y - self_pose_->y;
                    current_target.theta = std::atan2(dist_y, dist_x);
                }

                current_target.locked = true;
            }

            else {
                current_target.info     = std::make_shared<irobot_interfaces::msg::Turtleinfo>(*msg->turtles.begin());
                current_target.distance = calculate_distance(self_pose_->x, 
                                                             self_pose_->y, 
                                                             current_target.info->x, 
                                                             current_target.info->y);
                current_target.theta    = std::atan2(current_target.info->y, current_target.info->x);
                current_target.locked   = true;
            }
        }
    }

    void hunt_pray_callback(){

        if (pose_setted_and_target_locked()) {

            // Define necessary shit
            auto twist_msg    = geometry_msgs::msg::Twist();

            // Calculate the difference between the current angle and the target angle
            double angle_diff = std::fmod(current_target.theta - self_pose_->theta + M_PI, 2 * M_PI) - M_PI;

            if (current_target.distance > 0.5) {
                // Set linear and angular velocities
                twist_msg.linear.x  = kp_linear * current_target.distance; // Proportional control for linear velocity
                twist_msg.angular.z = kp_angular * angle_diff; // Proportional control for angular velocity
            } else {
                twist_msg.linear.x  = 0.0;
                twist_msg.angular.z = 0.0;
                RCLCPP_INFO(this->get_logger(), "reached_target");
                Kill_request_thread.push_back(
                    std::thread(&Hunter_Node::send_kill_request, this, current_target.info->name));

            }

            // Publish the twist message
            cmd_vel_publisher->publish(twist_msg);
        }
    }

void test_ride() {

    // Check if `self_pose_` and `test_goal` are valid
    if (!self_pose_) {
        RCLCPP_ERROR(this->get_logger(), "self_pose_ is null. Skipping test_ride.");
        return;
    }

    if (!test_goal) {
        RCLCPP_ERROR(this->get_logger(), "test_goal is null. Skipping test_ride.");
        return;
    }

    auto twist_msg = geometry_msgs::msg::Twist();

    // Calculate the difference between the current angle and the target angle
    double angle_diff = std::fmod(test_goal->theta - self_pose_->theta + M_PI, 2 * M_PI) - M_PI;

    double distance = calculate_distance(self_pose_->x, 
                                         self_pose_->y, 
                                         test_goal->x, 
                                         test_goal->y);

    RCLCPP_INFO(this->get_logger(), "(x1: %.2f, y1: %.2f),\n%*s(x2: %.2f, y2: %.2f)", 
                                       self_pose_->x, self_pose_->y, 40, "", test_goal->x, test_goal->y);
    RCLCPP_INFO(this->get_logger(), "Computed_distance = %.2f", distance);

    if (distance > 0.5) {
        // Set linear and angular velocities
        twist_msg.linear.x  = kp_linear * distance; // Proportional control for linear velocity
        twist_msg.angular.z = kp_angular * angle_diff; // Proportional control for angular velocity
    } else {
        twist_msg.linear.x  = 0.0;
        twist_msg.angular.z = 0.0;
        RCLCPP_INFO(this->get_logger(), "Reached goal.");
    }

    // Publish the twist message
    cmd_vel_publisher->publish(twist_msg);
}


void send_kill_request(const std::string &turtle_name) {
    auto request_  = std::make_shared<irobot_interfaces::srv::KillSwitch::Request>();
    request_->name = turtle_name;

    // Send the request asynchronously
    auto future = kill_request_->async_send_request(request_);

    try{
        auto response = future.get();

        if (!response){
            RCLCPP_WARN(this->get_logger(), "target_turtle_escaped from the hunter");
        }
        else{
            RCLCPP_INFO(this->get_logger(), "%s, has been killed by hunter", turtle_name.c_str());
        }
    }

    catch (const std::exception &e)
        {
            RCLCPP_ERROR(this->get_logger(), "Service call failed.");
        }
    }

    // Declarations

    rclcpp::Subscription<geometry_msgs::msg::Pose2D>::SharedPtr test_vertex_subscriber;

    rclcpp::Subscription<turtlesim::msg::Pose>::SharedPtr pose_subscriber;
    rclcpp::Subscription<irobot_interfaces::msg::TurtleArray>::SharedPtr target_subscriber;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_publisher;
    

    rclcpp::Client<irobot_interfaces::srv::KillSwitch>::SharedPtr kill_request_;

    rclcpp::TimerBase::SharedPtr timer_;

    struct Target 
    {
        bool locked;
        double distance;
        double theta;
        std::shared_ptr<irobot_interfaces::msg::Turtleinfo> info;

        Target() : locked(false), distance(0.0), theta(0.0), info(nullptr) {}
    };

    Target current_target;

    bool test_mode;
    bool kill_closest_turtle_first;
    u_int hunter_frequency = 0;

    float kp_linear;
    float kp_angular;

    std::shared_ptr<turtlesim::msg::Pose> self_pose_      = nullptr;
    std::shared_ptr<geometry_msgs::msg::Pose2D> test_goal = nullptr;
    
    std::vector<std::thread> Kill_request_thread;

};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<Hunter_Node>(); 
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
