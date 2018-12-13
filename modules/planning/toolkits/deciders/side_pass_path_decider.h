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

#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "cyber/common/macros.h"

#include "modules/common/proto/pnc_point.pb.h"
#include "modules/planning/proto/decider_config.pb.h"

#include "modules/map/pnc_map/path.h"
#include "modules/planning/common/frame.h"
#include "modules/planning/common/reference_line_info.h"
#include "modules/planning/math/finite_element_qp/fem_1d_expanded_jerk_qp_problem.h"
#include "modules/planning/toolkits/deciders/decider.h"

namespace apollo {
namespace planning {

class SidePassPathDecider : public Decider {
 public:
  explicit SidePassPathDecider(const TaskConfig& config);

  enum class SidePassDirection {
    LEFT = 0,
    RIGHT = 1,
  };

 private:
  common::Status Process(Frame* frame,
                         ReferenceLineInfo* reference_line_info) override;

  common::Status BuildSidePathDecision(
      Frame* frame, ReferenceLineInfo* const reference_line_info);

  bool GeneratePath(Frame* frame, ReferenceLineInfo* reference_line_info);

  std::vector<std::tuple<double, double, double>> GetPathBoundaries(
      const common::TrajectoryPoint& planning_start_point,
      const SLBoundary& adc_sl_boundary, const ReferenceLine& reference_line,
      const IndexedList<std::string, Obstacle>& indexed_obstacles);

  const Obstacle* GetNearestObstacle(
      const SLBoundary& adc_sl_boundary, const ReferenceLine& reference_line,
      const IndexedList<std::string, Obstacle>& indexed_obstacles);

  void RecordDebugInfo(ReferenceLineInfo* reference_line_info);

 private:
  common::FrenetFramePoint adc_frenet_frame_point_;
  std::unique_ptr<Fem1dQpProblem> fem_qp_;
  SidePassDirection decided_direction_;
  double delta_s_ = 0.0;
};

}  // namespace planning
}  // namespace apollo
