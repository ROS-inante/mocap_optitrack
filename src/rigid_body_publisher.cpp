/* 
 * Copyright (c) 2023, Alexander Junk
 * Copyright (c) 2018, Houston Mechatronics Inc., JD Yamokoski
 * Copyright (c) 2012, Clearpath Robotics, Inc., Alex Bencz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its 
 *    contributors may be used to endorse or promote products derived from 
 *    this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <mocap_optitrack/rigid_body_publisher.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Transform.h>
#include <tf2/LinearMath/Vector3.h>
#include <tf2/utils.h>

namespace mocap_optitrack
{

namespace utilities
{
  geometry_msgs::msg::PoseStamped getRosPose(RigidBody const& body, bool newCoordinates)
  {
    geometry_msgs::msg::PoseStamped poseStampedMsg;
    if (newCoordinates)
    {
      // Motive 1.7+ coordinate system
      poseStampedMsg.pose.position.x = body.pose.position.x;
      poseStampedMsg.pose.position.y = body.pose.position.y;
      poseStampedMsg.pose.position.z = body.pose.position.z;
  
      poseStampedMsg.pose.orientation.x = body.pose.orientation.x;
      poseStampedMsg.pose.orientation.y = body.pose.orientation.y;
      poseStampedMsg.pose.orientation.z = body.pose.orientation.z;
      poseStampedMsg.pose.orientation.w = body.pose.orientation.w;
    }
    else
    {
      // y & z axes are swapped in the Optitrack coordinate system
      poseStampedMsg.pose.position.x = body.pose.position.x;
      poseStampedMsg.pose.position.y = -body.pose.position.z;
      poseStampedMsg.pose.position.z = body.pose.position.y;
  
      poseStampedMsg.pose.orientation.x = body.pose.orientation.x;
      poseStampedMsg.pose.orientation.y = -body.pose.orientation.z;
      poseStampedMsg.pose.orientation.z = body.pose.orientation.y;
      poseStampedMsg.pose.orientation.w = body.pose.orientation.w;
    }

    return poseStampedMsg;
  }   
}

RigidBodyPublisher::RigidBodyPublisher(rclcpp::Node::SharedPtr &node, 
  Version const& natNetVersion,
  PublisherConfiguration const& config) :
    config(config), tfPublisher(node)
{
  if (config.publishPose)
    posePublisher = node->create_publisher<geometry_msgs::msg::PoseStamped>(config.poseTopicName, 1000);

  if (config.publishPose2d)
    pose2dPublisher = node->create_publisher<geometry_msgs::msg::Pose2D>(config.pose2dTopicName, 1000);

  if (config.publishOdom)
    odometry_publisher_ = node->create_publisher<nav_msgs::msg::Odometry>(config.odomTopicName, rclcpp::SystemDefaultsQoS());

  // Motive 1.7+ uses a new coordinate system
  useNewCoordinates = (natNetVersion >= Version("1.7"));
}

RigidBodyPublisher::~RigidBodyPublisher()
{

}

void RigidBodyPublisher::publish(rclcpp::Time const& time, RigidBody const& body)
{
  // don't do anything if no new data was provided
  if (!body.hasValidData())
  {
    return;
  }

  // NaN?
  if (body.pose.position.x != body.pose.position.x)
  {
    return;
  }

  geometry_msgs::msg::PoseStamped pose = utilities::getRosPose(body, useNewCoordinates);
  pose.header.stamp = time;

  if (config.publishPose)
  {
    pose.header.frame_id = config.parentFrameId;
    posePublisher->publish(pose);
  }

  tf2::Quaternion q(pose.pose.orientation.x,
                   pose.pose.orientation.y,
                   pose.pose.orientation.z,
                   pose.pose.orientation.w);

  // publish 2D pose
  if (config.publishPose2d)
  {
    geometry_msgs::msg::Pose2D pose2d;
    pose2d.x = pose.pose.position.x;
    pose2d.y = pose.pose.position.y;
    pose2d.theta = tf2::getYaw(q);
    pose2dPublisher->publish(pose2d);
  }

  if (config.publishOdom)
  {
    nav_msgs::msg::Odometry odometry_message;

    odometry_message.twist =
        geometry_msgs::msg::TwistWithCovariance(rosidl_runtime_cpp::MessageInitialization::ALL);

    odometry_message.header.frame_id = config.parentFrameId;
    odometry_message.child_frame_id = config.childFrameId;

    odometry_message.header.stamp = time;

    odometry_message.pose.pose.position.x = pose.pose.position.x;
    odometry_message.pose.pose.position.y = pose.pose.position.y;
    odometry_message.pose.pose.position.z = pose.pose.position.z;
    
    odometry_message.pose.pose.orientation.x = pose.pose.orientation.x;
    odometry_message.pose.pose.orientation.y = pose.pose.orientation.y;
    odometry_message.pose.pose.orientation.z = pose.pose.orientation.z;
    odometry_message.pose.pose.orientation.w = pose.pose.orientation.w;

    std::array<double, 6> pose_covariance_diagonal{0};

      constexpr size_t NUM_DIMENSIONS = 6;
      for (size_t index = 0; index < 6; ++index)
      {
          // 0, 7, 14, 21, 28, 35
          const size_t diagonal_index = NUM_DIMENSIONS * index + index;

          odometry_message.pose.covariance[diagonal_index] = 
              pose_covariance_diagonal[index];
      }

    odometry_publisher_->publish(odometry_message);
  }

  if (config.publishTf)
  {
    geometry_msgs::msg::TransformStamped transformStamped;
    transformStamped.header.frame_id = config.parentFrameId;
    transformStamped.header.stamp = time;
    transformStamped.child_frame_id = config.childFrameId;
    transformStamped.transform.rotation = pose.pose.orientation;
    transformStamped.transform.translation.x = pose.pose.position.x;
    transformStamped.transform.translation.y = pose.pose.position.y;
    transformStamped.transform.translation.z = pose.pose.position.z;

    tfPublisher.sendTransform(transformStamped);
  }
}


RigidBodyPublishDispatcher::RigidBodyPublishDispatcher(
  rclcpp::Node::SharedPtr &node, 
  Version const& natNetVersion,
  PublisherConfigurations const& configs)
{
  for (auto const& config : configs)
  {
    rigidBodyPublisherMap[config.rigidBodyId] = 
      RigidBodyPublisherPtr(new RigidBodyPublisher(node, natNetVersion, config));
  }
}

void RigidBodyPublishDispatcher::publish(
  rclcpp::Time const& time, 
  std::vector<RigidBody> const& rigidBodies)
{
  for (auto const& rigidBody : rigidBodies)
  {
    auto const& iter = rigidBodyPublisherMap.find(rigidBody.bodyId);

    if (iter != rigidBodyPublisherMap.end())
    {
      (*iter->second).publish(time, rigidBody);
    }
  }
}


} // namespace