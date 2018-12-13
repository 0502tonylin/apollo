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

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "modules/prediction/container/obstacles/obstacle.h"
#include "modules/prediction/evaluator/evaluator.h"
#include "modules/prediction/proto/feature.pb.h"
#include "modules/prediction/proto/fnn_vehicle_model.pb.h"
#include "modules/prediction/proto/lane_graph.pb.h"

namespace apollo {
namespace prediction {

class CruiseMLPEvaluator : public Evaluator {
 public:
  /**
   * @brief Constructor
   */
  CruiseMLPEvaluator();

  /**
   * @brief Destructor
   */
  virtual ~CruiseMLPEvaluator() = default;

  /**
   * @brief Override Evaluate
   * @param Obstacle pointer
   */
  void Evaluate(Obstacle* obstacle_ptr) override;

  /**
   * @brief Extract feature vector
   * @param Obstacle pointer
   *        Lane Sequence pointer
   */
  void ExtractFeatureValues(Obstacle* obstacle_ptr,
                            LaneSequence* lane_sequence_ptr,
                            std::vector<double>* feature_values);
  void Clear();

 private:
  /**
   * @brief Set obstacle feature vector
   * @param Obstacle pointer
   *        Feature container in a vector for receiving the feature values
   */
  void SetObstacleFeatureValues(const Obstacle* obstacle_ptr,
                                std::vector<double>* feature_values);

  /**
   * @brief Set interaction feature vector
   * @param Obstacle pointer
   *        Lane sequence pointer
   *        Feature container in a vector for receiving the feature values
   */
  void SetInteractionFeatureValues(Obstacle* obstacle_ptr,
                                   LaneSequence* lane_sequence_ptr,
                                   std::vector<double>* feature_values);

  /**
   * @brief Set lane feature vector
   * @param Obstacle pointer
   *        Lane sequence pointer
   *        Feature container in a vector for receiving the feature values
   */
  void SetLaneFeatureValues(const Obstacle* obstacle_ptr,
                            const LaneSequence* lane_sequence_ptr,
                            std::vector<double>* feature_values);

  /**
   * @brief Load mode file
   * @param Model file name
   */
  void LoadModel(const std::string& model_file);

  /**
   * @brief Compute probability of a junction exit
   */
  double ComputeFinishTime(const std::vector<double>& feature_values);

  /**
   * @brief Save offline feature values in proto
   * @param Lane sequence
   * @param Vector of feature values
   */
  void SaveOfflineFeatures(LaneSequence* sequence,
                           const std::vector<double>& feature_values);

 private:
  static const size_t OBSTACLE_FEATURE_SIZE = 23;
  static const size_t INTERACTION_FEATURE_SIZE = 8;
  static const size_t LANE_FEATURE_SIZE = 180;
  std::unique_ptr<FnnVehicleModel> model_ptr_;
};

}  // namespace prediction
}  // namespace apollo
