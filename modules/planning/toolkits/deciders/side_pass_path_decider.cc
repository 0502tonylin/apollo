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

#include "modules/planning/toolkits/deciders/side_pass_path_decider.h"

#include <algorithm>
#include <string>
#include <utility>

#include "modules/common/proto/pnc_point.pb.h"

#include "modules/common/configs/vehicle_config_helper.h"
#include "modules/planning/common/frame.h"
#include "modules/planning/common/planning_gflags.h"

namespace apollo {
namespace planning {

using apollo::common::ErrorCode;
using apollo::common::Status;
using apollo::common::TrajectoryPoint;
using apollo::common::VehicleConfigHelper;
using apollo::hdmap::PathOverlap;

constexpr double kRoadBuffer = 0.2;
constexpr double kObstacleBuffer = 0.1;
constexpr double kPlanDistAfterObs = 5.0;
constexpr double kSidePassPathLength = 50.0;

SidePassPathDecider::SidePassPathDecider(const TaskConfig &config)
    : Decider(config) {
  fem_qp_.reset(new Fem1dExpandedJerkQpProblem());
  delta_s_ = config.side_pass_path_decider_config().path_resolution();
  const int n = static_cast<int>(
      config.side_pass_path_decider_config().total_path_length() / delta_s_);
  std::array<double, 3> l_init = {0.0, 0.0, 0.0};
  std::array<double, 5> w = {
      config.side_pass_path_decider_config().l_weight(),
      config.side_pass_path_decider_config().dl_weight(),
      config.side_pass_path_decider_config().ddl_weight(),
      config.side_pass_path_decider_config().dddl_weight(),
      config.side_pass_path_decider_config().guiding_line_weight(),
  };
  CHECK(fem_qp_->Init(n, l_init, delta_s_, w,
                      config.side_pass_path_decider_config().max_dddl()));
}

Status SidePassPathDecider::Process(Frame *frame,
                                    ReferenceLineInfo *reference_line_info) {
  adc_frenet_frame_point_ =
      reference_line_info->reference_line().GetFrenetPoint(
          frame->PlanningStartPoint());
  GeneratePath(frame, reference_line_info);
  return Status::OK();
}

Status SidePassPathDecider::BuildSidePathDecision(
    Frame *frame, ReferenceLineInfo *const reference_line_info) {
  // TODO(All): decide side pass from left or right.
  // For now, just go left at all times.
  decided_direction_ = SidePassDirection::LEFT;
  return Status().OK();
}

// TODO(All): currently it's the 1st version, and only consider one
// vehicular obstacle ahead. It side-passes that obstacle and move
// back to original reference_line immediately. (without considering
// subsequent obstacles)
bool SidePassPathDecider::GeneratePath(Frame *frame,
                                       ReferenceLineInfo *reference_line_info) {
  // Sanity checks.
  CHECK_NOTNULL(frame);
  CHECK_NOTNULL(reference_line_info);

  // TODO(All): Check if ADC has fully stopped.

  // Decide whether to side-pass from left or right.
  if (BuildSidePathDecision(frame, reference_line_info) != Status().OK()) {
    AERROR << "Failed to decide on a side-pass direction.";
    return false;
  }

  auto lateral_bounds = GetPathBoundaries(
      frame->PlanningStartPoint(), reference_line_info->AdcSlBoundary(),
      reference_line_info->reference_line(),
      reference_line_info->path_decision()->obstacles());

  for (const auto &bd : lateral_bounds) {
    ADEBUG << std::get<0>(bd) << ": " << std::get<1>(bd) << ", "
           << std::get<2>(bd);
  }

  // Call optimizer to generate smooth path.
  fem_qp_->SetVariableBounds(lateral_bounds);
  if (!fem_qp_->Optimize()) {
    AERROR << "Fail to optimize in SidePassPathDecider.";
    return false;
  }

  // TODO(All): put optimized results into ReferenceLineInfo.
  // Update Reference_Line_Info with this newly generated path.
  std::vector<common::FrenetFramePoint> frenet_frame_path;
  double accumulated_s = adc_frenet_frame_point_.s();
  for (size_t i = 0; i < fem_qp_->x().size(); ++i) {
    common::FrenetFramePoint frenet_frame_point;
    ADEBUG << "FrenetFramePath: s = " << accumulated_s
           << ", l = " << fem_qp_->x()[i];
    if (accumulated_s >= reference_line_info->reference_line().Length()) {
      break;
    }
    frenet_frame_point.set_s(accumulated_s);
    frenet_frame_point.set_l(fem_qp_->x()[i]);
    frenet_frame_point.set_dl(fem_qp_->x_derivative()[i]);
    frenet_frame_point.set_ddl(fem_qp_->x_second_order_derivative()[i]);
    frenet_frame_path.push_back(std::move(frenet_frame_point));
    accumulated_s += delta_s_;
  }

  auto path_data = reference_line_info->mutable_path_data();
  path_data->SetReferenceLine(&reference_line_info->reference_line());
  path_data->SetFrenetPath(FrenetFramePath(frenet_frame_path));

  RecordDebugInfo(reference_line_info);
  return true;
}

std::vector<std::tuple<double, double, double>>
SidePassPathDecider::GetPathBoundaries(
    const TrajectoryPoint &planning_start_point,
    const SLBoundary &adc_sl_boundary, const ReferenceLine &reference_line,
    const IndexedList<std::string, Obstacle> &indexed_obstacles) {
  std::vector<std::tuple<double, double, double>> lateral_bounds;

  const auto nearest_obs_sl_boundary =
      GetNearestObstacle(adc_sl_boundary, reference_line, indexed_obstacles)
          ->PerceptionSLBoundary();

  // Get road info at every point and fill in the boundary condition vector.
  const double s_increment = 1.0;
  bool is_blocked_by_obs = false;

  // Currently, it only considers one obstacle.
  // For future scaling so that multiple obstacles can be considered,
  // a sweep-line method can be used. The code here leaves some room
  // for the sweep-line method.
  for (double curr_s = adc_frenet_frame_point_.s();
       curr_s < std::min(kSidePassPathLength, reference_line.Length());
       curr_s += s_increment) {
    std::tuple<double, double, double> lateral_bound{
        curr_s - adc_frenet_frame_point_.s(), 0.0, 0.0};

    // Check if boundary should be dictated by obstacle or road
    if (curr_s >= nearest_obs_sl_boundary.start_s() - kPlanDistAfterObs &&
        curr_s <= nearest_obs_sl_boundary.end_s() + kPlanDistAfterObs) {
      is_blocked_by_obs = true;
    } else {
      is_blocked_by_obs = false;
    }
    // Get the road info at the current s.
    double lane_left_width_at_curr_s = 0.0;
    double lane_right_width_at_curr_s = 0.0;
    reference_line.GetLaneWidth(curr_s, &lane_left_width_at_curr_s,
                                &lane_right_width_at_curr_s);
    const double adc_half_width =
        VehicleConfigHelper::GetConfig().vehicle_param().width() / 2.0;

    // TODO(All): calculate drivable areas
    // lower bound
    std::get<1>(lateral_bound) =
        -(lane_right_width_at_curr_s - adc_half_width - kRoadBuffer);
    // upper bound
    std::get<2>(lateral_bound) =
        lane_left_width_at_curr_s - adc_half_width - kRoadBuffer;

    if (is_blocked_by_obs) {
      if (decided_direction_ == SidePassDirection::LEFT) {
        std::get<1>(lateral_bound) = nearest_obs_sl_boundary.end_l() +
                                     FLAGS_static_decision_nudge_l_buffer +
                                     kObstacleBuffer + adc_half_width;
        std::get<2>(lateral_bound) += lane_left_width_at_curr_s;
      } else if (decided_direction_ == SidePassDirection::RIGHT) {
        std::get<1>(lateral_bound) -= lane_right_width_at_curr_s;
        std::get<2>(lateral_bound) = nearest_obs_sl_boundary.start_l() -
                                     FLAGS_static_decision_nudge_l_buffer -
                                     kObstacleBuffer - adc_half_width;
      } else {
        AERROR << "Side-pass direction undefined.";
        return lateral_bounds;
      }
    }
    ADEBUG << "obsrbound: " << std::get<0>(lateral_bound) << ", "
           << std::get<1>(lateral_bound) << ", " << std::get<2>(lateral_bound);

    lateral_bounds.push_back(lateral_bound);
  }

  return lateral_bounds;
}

const Obstacle *SidePassPathDecider::GetNearestObstacle(
    const SLBoundary &adc_sl_boundary, const ReferenceLine &reference_line,
    const IndexedList<std::string, Obstacle> &indexed_obstacles) {
  const Obstacle *nearest_obstacle = nullptr;

  // Generate the boundary conditions for the selected direction
  // based on the obstacle ahead and road conditions.
  double adc_end_s = adc_sl_boundary.end_s();

  // Get obstacle info.
  bool no_obs_selected = true;
  double nearest_obs_start_s = 0.0;
  for (const auto *obstacle : indexed_obstacles.Items()) {
    // Filter out obstacles that are behind ADC.
    double obs_start_s = obstacle->PerceptionSLBoundary().start_s();
    double obs_end_s = obstacle->PerceptionSLBoundary().end_s();
    if (obs_end_s < adc_end_s) {
      continue;
    }
    // TODO(All): ignores obstacles that are partially ahead of ADC
    if (obs_start_s < adc_end_s) {
      continue;
    }
    // Filter out those out-of-lane obstacles.
    double lane_left_width_at_start_s = 0.0;
    double lane_left_width_at_end_s = 0.0;
    double lane_right_width_at_start_s = 0.0;
    double lane_right_width_at_end_s = 0.0;
    reference_line.GetLaneWidth(obs_start_s, &lane_left_width_at_start_s,
                                &lane_right_width_at_start_s);
    reference_line.GetLaneWidth(obs_end_s, &lane_left_width_at_end_s,
                                &lane_right_width_at_end_s);
    double lane_left_width = std::min(std::abs(lane_left_width_at_start_s),
                                      std::abs(lane_left_width_at_end_s));
    double lane_right_width = std::min(std::abs(lane_right_width_at_start_s),
                                       std::abs(lane_right_width_at_end_s));
    double obs_start_l = obstacle->PerceptionSLBoundary().start_l();
    double obs_end_l = obstacle->PerceptionSLBoundary().end_l();

    // filter out-of-lane obstacles
    if (obs_start_l > lane_left_width || obs_end_l < -lane_right_width) {
      continue;
    }
    // do NOT sidepass non-vehicle obstacles.
    if (obstacle->Perception().type() !=
        perception::PerceptionObstacle::VEHICLE) {
      continue;
    }
    // For obstacles of interests, select the nearest one.
    // TODO(All): currently, regardless of the orientation
    // of the obstacle, it treats the obstacle as a rectangle
    // with two edges parallel to the reference line and the
    // other two perpendicular to that.
    if (no_obs_selected) {
      nearest_obs_start_s = obs_start_s;
      nearest_obstacle = obstacle;
      no_obs_selected = false;
    }
    if (nearest_obs_start_s > obs_start_s) {
      nearest_obs_start_s = obs_start_s;
    }
  }
  return nearest_obstacle;
}

void SidePassPathDecider::RecordDebugInfo(
    ReferenceLineInfo *reference_line_info) {
  const auto &path_points =
      reference_line_info->path_data().discretized_path().path_points();
  auto *ptr_optimized_path =
      reference_line_info->mutable_debug()->mutable_planning_data()->add_path();
  ptr_optimized_path->set_name(Name());
  ptr_optimized_path->mutable_path_point()->CopyFrom(
      {path_points.begin(), path_points.end()});
}

}  // namespace planning
}  // namespace apollo
