/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
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

#pragma once

#include "gflags/gflags.h"

// System gflags
DECLARE_string(prediction_module_name);
DECLARE_string(prediction_conf_file);
DECLARE_string(prediction_adapter_config_filename);
DECLARE_string(prediction_data_dir);

DECLARE_bool(prediction_test_mode);
DECLARE_double(prediction_test_duration);

DECLARE_bool(prediction_offline_mode);
DECLARE_string(prediction_offline_bags);

DECLARE_double(prediction_duration);
DECLARE_double(prediction_period);
DECLARE_double(double_precision);
DECLARE_double(min_prediction_length);

// Bag replay timestamp gap
DECLARE_double(replay_timestamp_gap);
DECLARE_int32(max_num_dump_feature);

// Map
DECLARE_double(lane_search_radius);
DECLARE_double(lane_search_radius_in_junction);
DECLARE_double(junction_search_radius);

// Scenario
DECLARE_double(junction_distance_threshold);
DECLARE_bool(enable_prioritize_obstacles);
DECLARE_bool(enable_junction_feature);

// Obstacle features
DECLARE_double(scan_length);
DECLARE_double(scan_width);
DECLARE_double(back_dist_ignore_ped);
DECLARE_uint32(cruise_historical_frame_length);
DECLARE_bool(enable_kf_tracking);
DECLARE_double(max_acc);
DECLARE_double(min_acc);
DECLARE_double(max_speed);
DECLARE_double(max_angle_diff_to_adjust_velocity);
DECLARE_double(q_var);
DECLARE_double(r_var);
DECLARE_double(p_var);
DECLARE_double(go_approach_rate);

DECLARE_int32(min_still_obstacle_history_length);
DECLARE_int32(max_still_obstacle_history_length);
DECLARE_double(still_obstacle_speed_threshold);
DECLARE_double(still_pedestrian_speed_threshold);
DECLARE_double(still_obstacle_position_std);
DECLARE_double(still_pedestrian_position_std);
DECLARE_double(max_history_time);
DECLARE_double(target_lane_gap);
DECLARE_int32(max_num_current_lane);
DECLARE_int32(max_num_nearby_lane);
DECLARE_double(max_lane_angle_diff);
DECLARE_int32(max_num_current_lane_in_junction);
DECLARE_int32(max_num_nearby_lane_in_junction);
DECLARE_double(max_lane_angle_diff_in_junction);
DECLARE_bool(enable_pedestrian_acc);
DECLARE_double(coeff_mul_sigma);
DECLARE_double(pedestrian_max_speed);
DECLARE_double(pedestrian_max_acc);
DECLARE_double(prediction_pedestrian_total_time);
DECLARE_double(still_speed);
DECLARE_string(evaluator_vehicle_mlp_file);
DECLARE_string(evaluator_cruise_vehicle_go_model_file);
DECLARE_string(evaluator_cruise_vehicle_cutin_model_file);
DECLARE_string(evaluator_vehicle_rnn_file);
DECLARE_string(evaluator_vehicle_cruise_mlp_file);
DECLARE_string(evaluator_vehicle_junction_mlp_file);
DECLARE_int32(max_num_obstacles);
DECLARE_double(valid_position_diff_threshold);
DECLARE_double(valid_position_diff_rate_threshold);
DECLARE_double(split_rate);
DECLARE_double(rnn_min_lane_relatice_s);
DECLARE_bool(adjust_velocity_by_obstacle_heading);
DECLARE_bool(adjust_velocity_by_position_shift);
DECLARE_bool(adjust_vehicle_heading_by_lane);
DECLARE_double(heading_filter_param);
DECLARE_uint32(max_num_lane_point);

// Validation checker
DECLARE_double(centripetal_acc_coeff);

// Obstacle trajectory
DECLARE_bool(enable_cruise_regression);
DECLARE_double(lane_sequence_threshold_cruise);
DECLARE_double(lane_sequence_threshold_junction);
DECLARE_double(lane_change_dist);
DECLARE_bool(enable_lane_sequence_acc);
DECLARE_bool(enable_trim_prediction_trajectory);
DECLARE_bool(enable_trajectory_validation_check);
DECLARE_double(distance_beyond_junction);
DECLARE_double(adc_trajectory_search_length);
DECLARE_double(virtual_lane_radius);
DECLARE_double(default_lateral_approach_speed);
DECLARE_double(centripedal_acc_threshold);

// move sequence prediction
DECLARE_double(time_upper_bound_to_lane_center);
DECLARE_double(time_lower_bound_to_lane_center);
DECLARE_double(sample_time_gap);
DECLARE_double(cost_alpha);
DECLARE_double(default_time_to_lat_end_state);
DECLARE_double(turning_curvature_lower_bound);
DECLARE_double(turning_curvature_upper_bound);
DECLARE_double(speed_at_lower_curvature);
DECLARE_double(speed_at_upper_curvature);
DECLARE_double(cost_function_alpha);
DECLARE_double(cost_function_sigma);
DECLARE_bool(use_bell_curve_for_cost_function);

DECLARE_int32(road_graph_max_search_horizon);
