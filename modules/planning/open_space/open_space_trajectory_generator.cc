/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

/**
 * @file
 **/

#include "modules/planning/open_space/open_space_trajectory_generator.h"

#include <cmath>
#include <fstream>
#include <utility>

#include "cyber/common/log.h"
#include "cyber/task/task.h"
#include "modules/common/util/string_tokenizer.h"
#include "modules/planning/common/planning_gflags.h"

namespace apollo {
namespace planning {

using apollo::common::ErrorCode;
using apollo::common::Status;
using apollo::common::TrajectoryPoint;
using apollo::common::VehicleState;
using apollo::common::math::Box2d;
using apollo::common::math::Vec2d;

Status OpenSpaceTrajectoryGenerator::Init(
    const PlannerOpenSpaceConfig& planner_open_space_config) {
  AINFO << "In OpenSpaceTrajectoryGenerator::Init()";

  // nominal sampling time
  ts_ = planner_open_space_config.delta_t();

  // load vehicle configuration
  double front_to_center = vehicle_param_.front_edge_to_center();
  double back_to_center = vehicle_param_.back_edge_to_center();
  double left_to_center = vehicle_param_.left_edge_to_center();
  double right_to_center = vehicle_param_.right_edge_to_center();
  ego_.resize(4, 1);
  ego_ << front_to_center, right_to_center, back_to_center, left_to_center;

  // initialize warm start class pointer
  warm_start_.reset(new HybridAStar(planner_open_space_config));

  // initialize dual variable warm start class pointer
  dual_variable_warm_start_.reset(
      new DualVariableWarmStartProblem(planner_open_space_config));

  // initialize distance approach class pointer
  distance_approach_.reset(
      new DistanceApproachProblem(planner_open_space_config));

  return Status::OK();
}

apollo::common::Status OpenSpaceTrajectoryGenerator::Plan(
    const VehicleState& vehicle_state, const std::vector<double>& XYbounds,
    const double rotate_angle, const Vec2d& translate_origin,
    const std::vector<double>& end_pose, std::size_t obstacles_num,
    const Eigen::MatrixXd& obstacles_edges_num,
    const Eigen::MatrixXd& obstacles_A, const Eigen::MatrixXd& obstacles_b,
    ThreadSafeIndexedObstacles* obstalce_list) {
  // initial state
  init_state_ = vehicle_state;
  init_x_ = init_state_.x();
  init_y_ = init_state_.y();
  init_phi_ = init_state_.heading();
  init_v_ = init_state_.linear_velocity();
  // rotate and scale the state according to the origin point defined in
  // frame

  init_x_ = init_x_ - translate_origin.x();
  init_y_ = init_y_ - translate_origin.y();
  double tmp_x = init_x_;
  init_x_ =
      init_x_ * std::cos(-rotate_angle) - init_y_ * std::sin(-rotate_angle);
  init_y_ = tmp_x * std::sin(-rotate_angle) + init_y_ * std::cos(-rotate_angle);

  // TODO(Jinyun) how to initial input not decided yet
  init_steer_ = 0;
  init_a_ = 0;
  Eigen::MatrixXd x0(4, 1);
  x0 << init_x_, init_y_, init_phi_, init_v_;
  Eigen::MatrixXd last_time_u(2, 1);
  last_time_u << init_steer_, init_a_;

  // final state
  Eigen::MatrixXd xF(4, 1);
  xF << end_pose[0], end_pose[1], end_pose[2], end_pose[3];

  // planning bound
  XYbounds_ = XYbounds;

  ADEBUG << "Start forming state warm start problem with configs setting : "
         << planner_open_space_config_.warm_start_config().ShortDebugString();

  // Warm Start (initial velocity is assumed to be 0 for now)
  Result result;

  if (warm_start_->Plan(x0(0, 0), x0(1, 0), x0(2, 0), xF(0, 0), xF(1, 0),
                        xF(2, 0), XYbounds_, obstalce_list, &result)) {
    ADEBUG << "State warm start problem solved successfully!";
  } else {
    return Status(ErrorCode::PLANNING_ERROR,
                  "State warm start problem failed to solve");
  }
  // load Warm Start result(horizon is the "N", not the size of step points)
  horizon_ = result.x.size() - 1;
  Eigen::MatrixXd xWS = Eigen::MatrixXd::Zero(4, horizon_ + 1);
  Eigen::MatrixXd uWS = Eigen::MatrixXd::Zero(2, horizon_);
  Eigen::VectorXd x = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(
      result.x.data(), horizon_ + 1);
  Eigen::VectorXd y = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(
      result.y.data(), horizon_ + 1);
  Eigen::VectorXd phi = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(
      result.phi.data(), horizon_ + 1);
  Eigen::VectorXd v = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(
      result.v.data(), horizon_ + 1);
  Eigen::VectorXd steer = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(
      result.steer.data(), horizon_);
  Eigen::VectorXd a =
      Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(result.a.data(), horizon_);
  xWS.row(0) = x;
  xWS.row(1) = y;
  xWS.row(2) = phi;
  xWS.row(3) = v;
  uWS.row(0) = steer;
  uWS.row(1) = a;

  // Step 8 : Formulate distance approach problem
  // solution from distance approach
  ADEBUG << "Start forming state warm start problem with configs setting : "
         << planner_open_space_config_.dual_variable_warm_start_config()
                .ShortDebugString();

  const double rx = 0.0;
  const double ry = 0.0;
  const double r_yaw = 0.0;

  // result for distance approach problem
  Eigen::MatrixXd l_warm_up;
  Eigen::MatrixXd n_warm_up;

  bool dual_variable_warm_start_status = dual_variable_warm_start_->Solve(
      horizon_, ts_, ego_, obstacles_num, obstacles_edges_num, obstacles_A,
      obstacles_b, rx, ry, r_yaw, &l_warm_up, &n_warm_up);

  if (dual_variable_warm_start_status) {
    ADEBUG << "Dual variable problem solved successfully!";
  } else {
    return Status(ErrorCode::PLANNING_ERROR,
                  "Dual variable problem failed to solve");
  }

  // Step 9 : Formulate distance approach problem
  // solution from distance approach
  ADEBUG << "Start Forming Distance approach problem with configs setting : "
         << planner_open_space_config_.distance_approach_config()
                .ShortDebugString();
  // result for distance approach problem
  Eigen::MatrixXd state_result_ds;
  Eigen::MatrixXd control_result_ds;
  Eigen::MatrixXd time_result_ds;
  Eigen::MatrixXd dual_l_result_ds;
  Eigen::MatrixXd dual_n_result_ds;

  // TODO(QiL): Pass dual variable warm start result in.
  bool distance_approach_status = distance_approach_->Solve(
      x0, xF, last_time_u, horizon_, ts_, ego_, xWS, uWS, XYbounds_,
      obstacles_num, obstacles_edges_num, obstacles_A, obstacles_b,
      &state_result_ds, &control_result_ds, &time_result_ds, &dual_l_result_ds,
      &dual_n_result_ds);

  if (distance_approach_status) {
    ADEBUG << "Distance approach problem solved successfully!";
  } else {
    return Status(ErrorCode::PLANNING_ERROR,
                  "Distance approach problem failed to solve");
  }

  // rescale the states to the world frame
  for (std::size_t i = 0; i < horizon_ + 1; i++) {
    double tmp_x = state_result_ds(0, i);
    state_result_ds(0, i) = state_result_ds(0, i) * std::cos(rotate_angle) -
                            state_result_ds(1, i) * std::sin(rotate_angle);
    state_result_ds(1, i) = tmp_x * std::sin(rotate_angle) +
                            state_result_ds(1, i) * std::cos(rotate_angle);
    state_result_ds(0, i) += translate_origin.x();
    state_result_ds(1, i) += translate_origin.y();
    state_result_ds(2, i) += rotate_angle;
  }

  // TODO(Jiaxuan): Step 9 : Trajectory Partition and  Publish TrajectoryPoint
  // in planning trajectory. Result saved in trajectory_partition_.
  // Every time update, use trajectory_partition to store each ADCTrajectory
  double relative_time = 0.0;
  double distance_s = 0.0;
  ADCTrajectories trajectory_partition;
  ADCTrajectory* current_trajectory = trajectory_partition.add_adc_trajectory();
  // set initial gear position for first ADCTrajectory depending on v
  // and check potential edge cases
  if (horizon_ < 3)
    return Status(ErrorCode::PLANNING_ERROR, "Invalid trajectory length!");
  if (state_result_ds(3, 0) > -1e-3 && state_result_ds(3, 1) > -1e-3 &&
      state_result_ds(3, 2) > -1e-3) {
    current_trajectory->set_gear(canbus::Chassis::GEAR_DRIVE);
  } else {
    if (state_result_ds(3, 0) < 1e-3 && state_result_ds(3, 1) < 1e-3 &&
        state_result_ds(3, 2) < 1e-3) {
      current_trajectory->set_gear(canbus::Chassis::GEAR_REVERSE);
    } else {
      return Status(ErrorCode::PLANNING_ERROR, "Invalid trajectory start!");
    }
  }
  // partition trajectory points into each trajectory
  for (std::size_t i = 0; i < horizon_ + 1; i++) {
    // shift from GEAR_DRIVE to GEAR_REVERSE if v < 0
    // then add a new trajectory with GEAR_REVERSE
    if (state_result_ds(3, i) < -1e-3 &&
        current_trajectory->gear() == canbus::Chassis::GEAR_DRIVE) {
      current_trajectory = trajectory_partition.add_adc_trajectory();
      current_trajectory->set_gear(canbus::Chassis::GEAR_REVERSE);
    }
    // shift from GEAR_REVERSE to GEAR_DRIVE if v > 0
    // then add a new trajectory with GEAR_DRIVE
    if (state_result_ds(3, i) > 1e-3 &&
        current_trajectory->gear() == canbus::Chassis::GEAR_REVERSE) {
      current_trajectory = trajectory_partition.add_adc_trajectory();
      current_trajectory->set_gear(canbus::Chassis::GEAR_DRIVE);
    }

    auto* point = current_trajectory->add_trajectory_point();
    relative_time += time_result_ds(0, i);
    point->set_relative_time(relative_time);
    point->mutable_path_point()->set_x(state_result_ds(0, i));
    point->mutable_path_point()->set_y(state_result_ds(1, i));
    point->mutable_path_point()->set_z(0);
    point->mutable_path_point()->set_theta(state_result_ds(2, i));
    if (i > 0) {
      distance_s +=
          std::sqrt((state_result_ds(0, i) - state_result_ds(0, i - 1)) *
                        (state_result_ds(0, i) - state_result_ds(0, i - 1)) +
                    (state_result_ds(1, i) - state_result_ds(1, i - 1)) *
                        (state_result_ds(1, i) - state_result_ds(1, i - 1)));
    }
    point->mutable_path_point()->set_s(distance_s);
    int gear_drive = 1;
    if (current_trajectory->gear() == canbus::Chassis::GEAR_REVERSE)
      gear_drive = -1;

    point->set_v(state_result_ds(3, i) * gear_drive);
    // TODO(Jiaxuan): Verify this steering to kappa equation
    point->mutable_path_point()->set_kappa(
        std::tanh(control_result_ds(0, i) * 470 * M_PI / 180.0 / 16) / 2.85 *
        gear_drive);
    point->set_a(control_result_ds(1, i) * gear_drive);
  }

  trajectory_partition_.CopyFrom(trajectory_partition);
  return Status::OK();
}

apollo::common::Status OpenSpaceTrajectoryGenerator::UpdateTrajectory(
    ADCTrajectories* adc_trajectories) {
  adc_trajectories->CopyFrom(trajectory_partition_);
  return Status::OK();
}

}  // namespace planning
}  // namespace apollo
