/*
 * Copyright 2016 The Cartographer Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sensor_bridge.h"

#include "src/top/msg_conversion.h"

#include "src/common/make_unique.h"

namespace sample_carto {

namespace top {


using sample_carto::transform::Rigid3d;

std::unique_ptr<::sample_carto::sensor::OdometryData> SensorBridge::ToOdometryData(const nav_msgs::Odometry::ConstPtr &msg)
{
  double time = msg->header.stamp.toSec();
  //TODO tf_pose (liu)
  transform::Rigid3d pose({baselink_to_laser_x_, baselink_to_laser_y_, 0}, transform::RollPitchYaw(0, 0, baselink_to_laser_theta_));
  return ::sample_carto::common::make_unique<::sample_carto::sensor::OdometryData>(::sample_carto::sensor::OdometryData{time, ToRigid3d(msg->pose.pose) * pose});
}

void SensorBridge::HandleLaserScanMessage(const sensor_msgs::LaserScan::ConstPtr &msg)
{
    auto scan_data = ToPointCloudWithIntensities(*msg);
    double timestamp = msg->header.stamp.toSec();
    Eigen::Vector3f origin = Eigen::Vector3f::Zero();
    global_trajectory_builder_ptr_->AddRangefinderData(timestamp, origin, scan_data.points);

}

void SensorBridge::HandleOdometryMessage(const nav_msgs::Odometry::ConstPtr& msg) 
{
  auto odom_data = ToOdometryData(msg);
  if (odom_data != nullptr) {
    global_trajectory_builder_ptr_->AddSensorData(*odom_data);
  }
}
}

}  // namespace sample_carto

