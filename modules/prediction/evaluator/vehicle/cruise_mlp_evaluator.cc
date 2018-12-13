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

#include <limits>

#include "modules/prediction/evaluator/vehicle/cruise_mlp_evaluator.h"
#include "modules/common/math/math_utils.h"
#include "modules/common/util/file.h"
#include "modules/map/proto/map_lane.pb.h"
#include "modules/prediction/common/feature_output.h"
#include "modules/prediction/common/prediction_gflags.h"
#include "modules/prediction/common/prediction_util.h"
#include "modules/prediction/common/validation_checker.h"

namespace apollo {
namespace prediction {

// Helper function for computing the mean value of a vector.
double ComputeMean(const std::vector<double>& nums, size_t start, size_t end) {
  int count = 0;
  double sum = 0.0;
  for (size_t i = start; i <= end && i < nums.size(); i++) {
    sum += nums[i];
    ++count;
  }
  return (count == 0) ? 0.0 : sum / count;
}

CruiseMLPEvaluator::CruiseMLPEvaluator() {
  LoadModel(FLAGS_evaluator_vehicle_cruise_mlp_file);
}

void CruiseMLPEvaluator::Clear() {
}

void CruiseMLPEvaluator::Evaluate(Obstacle* obstacle_ptr) {
  // Sanity checks.
  Clear();
  CHECK_NOTNULL(obstacle_ptr);
  int id = obstacle_ptr->id();
  if (!obstacle_ptr->latest_feature().IsInitialized()) {
    AERROR << "Obstacle [" << id << "] has no latest feature.";
    return;
  }
  Feature* latest_feature_ptr = obstacle_ptr->mutable_latest_feature();
  CHECK_NOTNULL(latest_feature_ptr);
  if (!latest_feature_ptr->has_lane() ||
      !latest_feature_ptr->lane().has_lane_graph()) {
    ADEBUG << "Obstacle [" << id << "] has no lane graph.";
    return;
  }
  LaneGraph* lane_graph_ptr =
      latest_feature_ptr->mutable_lane()->mutable_lane_graph();
  CHECK_NOTNULL(lane_graph_ptr);
  if (lane_graph_ptr->lane_sequence_size() == 0) {
    AERROR << "Obstacle [" << id << "] has no lane sequences.";
    return;
  }

  // For every possible lane sequence, extract needed features.
  for (int i = 0; i < lane_graph_ptr->lane_sequence_size(); ++i) {
    LaneSequence* lane_sequence_ptr = lane_graph_ptr->mutable_lane_sequence(i);
    CHECK_NOTNULL(lane_sequence_ptr);
    std::vector<double> feature_values;
    ExtractFeatureValues(obstacle_ptr, lane_sequence_ptr, &feature_values);
    double finish_time = ComputeFinishTime(feature_values);
    lane_sequence_ptr->set_time_to_lane_center(finish_time);
  }

  if (FLAGS_prediction_offline_mode) {
    FeatureOutput::Insert(*latest_feature_ptr);
    ADEBUG << "Insert cruise feature into feature output";
  }
}

void CruiseMLPEvaluator::ExtractFeatureValues
    (Obstacle* obstacle_ptr,
     LaneSequence* lane_sequence_ptr,
     std::vector<double>* feature_values) {
  // Sanity checks.
  CHECK_NOTNULL(obstacle_ptr);
  CHECK_NOTNULL(lane_sequence_ptr);
  int id = obstacle_ptr->id();

  // Extract obstacle related features.
  std::vector<double> obstacle_feature_values;
  SetObstacleFeatureValues(obstacle_ptr, &obstacle_feature_values);
  if (obstacle_feature_values.size() != OBSTACLE_FEATURE_SIZE) {
    ADEBUG << "Obstacle [" << id << "] has fewer than "
           << "expected obstacle feature_values "
           << obstacle_feature_values.size() << ".";
    return;
  }
  feature_values->insert(feature_values->end(), obstacle_feature_values.begin(),
                         obstacle_feature_values.end());
  // Extract lane related features.
  std::vector<double> lane_feature_values;
  SetLaneFeatureValues(obstacle_ptr, lane_sequence_ptr, &lane_feature_values);
  if (lane_feature_values.size() != LANE_FEATURE_SIZE) {
    ADEBUG << "Obstacle [" << id << "] has fewer than "
           << "expected lane feature_values" << lane_feature_values.size()
           << ".";
    return;
  }
  feature_values->insert(feature_values->end(), lane_feature_values.begin(),
                         lane_feature_values.end());

  // For offline training, write the extracted features into proto.
  if (FLAGS_prediction_offline_mode) {
    SaveOfflineFeatures(lane_sequence_ptr, *feature_values);
    ADEBUG << "Save cruise mlp features for obstacle ["
           << obstacle_ptr->id() << "] with dim ["
           << feature_values->size() << "]";
  }
}

void CruiseMLPEvaluator::SetObstacleFeatureValues(
    const Obstacle* obstacle_ptr, std::vector<double>* feature_values) {
  // Sanity checks.
  CHECK_NOTNULL(obstacle_ptr);
  feature_values->clear();
  feature_values->reserve(OBSTACLE_FEATURE_SIZE);

  std::vector<double> thetas;
  std::vector<double> lane_ls;
  std::vector<double> dist_lbs;
  std::vector<double> dist_rbs;
  std::vector<int> lane_types;
  std::vector<double> speeds;
  std::vector<double> timestamps;

  double obs_feature_history_start_time =
      obstacle_ptr->timestamp() - FLAGS_prediction_duration;
  int count = 0;
  // Starting from the most recent timestamp and going backward.
  for (std::size_t i = 0; i < obstacle_ptr->history_size(); ++i) {
    const Feature& feature = obstacle_ptr->feature(i);
    if (!feature.IsInitialized()) {
      continue;
    }
    if (feature.timestamp() < obs_feature_history_start_time) {
      break;
    }
    if (feature.has_lane() && feature.lane().has_lane_feature()) {
      thetas.push_back(feature.lane().lane_feature().angle_diff());
      lane_ls.push_back(feature.lane().lane_feature().lane_l());
      dist_lbs.push_back(feature.lane().lane_feature().dist_to_left_boundary());
      dist_rbs.push_back(
          feature.lane().lane_feature().dist_to_right_boundary());
      lane_types.push_back(feature.lane().lane_feature().lane_turn_type());
      timestamps.push_back(feature.timestamp());
      speeds.push_back(feature.speed());
      ++count;
    }
  }
  if (count <= 0) {
    return;
  }

  int curr_size = 5;
  int hist_size = obstacle_ptr->history_size();
  double theta_mean = ComputeMean(thetas, 0, hist_size - 1);
  double theta_filtered = ComputeMean(thetas, 0, curr_size - 1);
  double lane_l_mean = ComputeMean(lane_ls, 0, hist_size - 1);
  double lane_l_filtered = ComputeMean(lane_ls, 0, curr_size - 1);
  double speed_mean = ComputeMean(speeds, 0, hist_size - 1);

  double time_diff = timestamps.front() - timestamps.back();
  double dist_lb_rate = (timestamps.size() > 1)
                            ? (dist_lbs.front() - dist_lbs.back()) / time_diff
                            : 0.0;
  double dist_rb_rate = (timestamps.size() > 1)
                            ? (dist_rbs.front() - dist_rbs.back()) / time_diff
                            : 0.0;

  double delta_t = 0.0;
  if (timestamps.size() > 1) {
    delta_t =
        (timestamps.front() - timestamps.back()) / (timestamps.size() - 1);
  }
  double angle_curr = ComputeMean(thetas, 0, curr_size - 1);
  double angle_prev = ComputeMean(thetas, curr_size, 2 * curr_size - 1);
  double angle_diff =
      (hist_size >= 2 * curr_size) ? angle_curr - angle_prev : 0.0;

  double lane_l_curr = ComputeMean(lane_ls, 0, curr_size - 1);
  double lane_l_prev = ComputeMean(lane_ls, curr_size, 2 * curr_size - 1);
  double lane_l_diff =
      (hist_size >= 2 * curr_size) ? lane_l_curr - lane_l_prev : 0.0;

  double angle_diff_rate = 0.0;
  double lane_l_diff_rate = 0.0;
  if (delta_t > std::numeric_limits<double>::epsilon()) {
    angle_diff_rate = angle_diff / (delta_t * curr_size);
    lane_l_diff_rate = lane_l_diff / (delta_t * curr_size);
  }

  double acc = 0.0;
  double jerk = 0.0;
  if (static_cast<int>(speeds.size()) >= 3 * curr_size &&
      delta_t > std::numeric_limits<double>::epsilon()) {
    double speed_1st_recent =
        ComputeMean(speeds, 0, curr_size - 1);
    double speed_2nd_recent =
        ComputeMean(speeds, curr_size, 2 * curr_size - 1);
    double speed_3rd_recent =
        ComputeMean(speeds, 2 * curr_size, 3 * curr_size - 1);
    acc = (speed_1st_recent - speed_2nd_recent) / (curr_size * delta_t);
    jerk = (speed_1st_recent - 2 * speed_2nd_recent + speed_3rd_recent) /
          (curr_size * curr_size * delta_t * delta_t);
  }

  double dist_lb_rate_curr = 0.0;
  if (hist_size >= 2 * curr_size &&
      delta_t > std::numeric_limits<double>::epsilon()) {
    double dist_lb_curr = ComputeMean(dist_lbs, 0, curr_size - 1);
    double dist_lb_prev = ComputeMean(dist_lbs, curr_size, 2 * curr_size - 1);
    dist_lb_rate_curr = (dist_lb_curr - dist_lb_prev) / (curr_size * delta_t);
  }

  double dist_rb_rate_curr = 0.0;
  if (hist_size >= 2 * curr_size &&
      delta_t > std::numeric_limits<double>::epsilon()) {
    double dist_rb_curr = ComputeMean(dist_rbs, 0, curr_size - 1);
    double dist_rb_prev = ComputeMean(dist_rbs, curr_size, 2 * curr_size - 1);
    dist_rb_rate_curr = (dist_rb_curr - dist_rb_prev) / (curr_size * delta_t);
  }

  // Setup obstacle feature values.
  feature_values->push_back(theta_filtered);
  feature_values->push_back(theta_mean);
  feature_values->push_back(theta_filtered - theta_mean);
  feature_values->push_back(angle_diff);
  feature_values->push_back(angle_diff_rate);

  feature_values->push_back(lane_l_filtered);
  feature_values->push_back(lane_l_mean);
  feature_values->push_back(lane_l_filtered - lane_l_mean);
  feature_values->push_back(lane_l_diff);
  feature_values->push_back(lane_l_diff_rate);

  feature_values->push_back(speed_mean);
  feature_values->push_back(acc);
  feature_values->push_back(jerk);

  feature_values->push_back(dist_lbs.front());
  feature_values->push_back(dist_lb_rate);
  feature_values->push_back(dist_lb_rate_curr);

  feature_values->push_back(dist_rbs.front());
  feature_values->push_back(dist_rb_rate);
  feature_values->push_back(dist_rb_rate_curr);

  feature_values->push_back(lane_types.front() == 0 ? 1.0 : 0.0);
  feature_values->push_back(lane_types.front() == 1 ? 1.0 : 0.0);
  feature_values->push_back(lane_types.front() == 2 ? 1.0 : 0.0);
  feature_values->push_back(lane_types.front() == 3 ? 1.0 : 0.0);
}

void CruiseMLPEvaluator::SetLaneFeatureValues
    (const Obstacle* obstacle_ptr, const LaneSequence* lane_sequence_ptr,
     std::vector<double>* feature_values) {
  // Sanity checks.
  feature_values->clear();
  feature_values->reserve(LANE_FEATURE_SIZE);
  const Feature& feature = obstacle_ptr->latest_feature();
  if (!feature.IsInitialized()) {
    ADEBUG << "Obstacle [" << obstacle_ptr->id() << "] has no latest feature.";
    return;
  } else if (!feature.has_position()) {
    ADEBUG << "Obstacle [" << obstacle_ptr->id() << "] has no position.";
    return;
  }

  double heading = feature.velocity_heading();
  double speed = feature.speed();
  for (int i = 0; i < lane_sequence_ptr->lane_segment_size(); ++i) {
    if (feature_values->size() >= LANE_FEATURE_SIZE) {
      break;
    }
    const LaneSegment& lane_segment = lane_sequence_ptr->lane_segment(i);
    for (int j = 0; j < lane_segment.lane_point_size(); ++j) {
      if (feature_values->size() >= LANE_FEATURE_SIZE) {
        break;
      }
      const LanePoint& lane_point = lane_segment.lane_point(j);
      if (!lane_point.has_position()) {
        AERROR << "Lane point has no position.";
        continue;
      }
      double diff_x = lane_point.position().x() - feature.position().x();
      double diff_y = lane_point.position().y() - feature.position().y();
      double angle = std::atan2(diff_x, diff_y);
      feature_values->push_back(lane_point.kappa());
      feature_values->push_back(speed * speed * lane_point.kappa());
      feature_values->push_back(std::sin(angle - heading));
      feature_values->push_back(lane_point.relative_l());
      feature_values->push_back(lane_point.heading());
      feature_values->push_back(lane_point.angle_diff());
    }
  }

  // If the lane points are not sufficient, apply a linear extrapolation.
  std::size_t size = feature_values->size();
  while (size >= 6 && size < LANE_FEATURE_SIZE) {
    double lane_kappa = feature_values->operator[](size - 6);
    double centri_acc = feature_values->operator[](size - 5);
    double heading_diff = feature_values->operator[](size - 4);
    double lane_l_diff = feature_values->operator[](size - 3);
    double heading = feature_values->operator[](size - 2);
    double angle_diff = feature_values->operator[](size - 1);
    feature_values->push_back(lane_kappa);
    feature_values->push_back(centri_acc);
    feature_values->push_back(heading_diff);
    feature_values->push_back(lane_l_diff);
    feature_values->push_back(heading);
    feature_values->push_back(angle_diff);
    size = feature_values->size();
  }
}

// TODO(all): uncomment this once the model is trained and ready.
void CruiseMLPEvaluator::LoadModel(const std::string& model_file) {
  // Currently, it's using FnnVehicleModel
  // TODO(all) implement it using the generic "network" class.
  // model_ptr_.reset(new FnnVehicleModel());
  // CHECK(model_ptr_ != nullptr);
  // CHECK(common::util::GetProtoFromFile(model_file, model_ptr_.get()))
  //     << "Unable to load model file: " << model_file << ".";

  // AINFO << "Succeeded in loading the model file: " << model_file << ".";
}

// TODO(all): implement this once the model is trained and ready.
double CruiseMLPEvaluator::ComputeFinishTime(
    const std::vector<double>& feature_values) {
  return 6.0;
}

void CruiseMLPEvaluator::SaveOfflineFeatures(
    LaneSequence* sequence, const std::vector<double>& feature_values) {
  for (double feature_value : feature_values) {
    sequence->mutable_features()->add_mlp_features(feature_value);
  }
}

}  // namespace prediction
}  // namespace apollo
