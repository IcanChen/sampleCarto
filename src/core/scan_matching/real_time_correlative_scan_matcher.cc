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

#include "real_time_correlative_scan_matcher.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "Eigen/Geometry"
#include "src/common/lua_parameter_dictionary.h"
#include "src/common/math.h"
#include "src/core/map/probability_grid.h"
#include "src/sensor/point_cloud.h"
#include "src/transform/transform.h"
#include "glog/logging.h"

namespace sample_carto {
namespace core {
namespace scan_matching {

void RealTimeCorrelativeScanMatcherOptions::Create(common::LuaParameterDictionary *const parameter_dictionary)
{
    linear_search_window_ = parameter_dictionary->GetDouble("linear_search_window");
    angular_search_window_ = parameter_dictionary->GetDouble("angular_search_window");
    translation_delta_cost_weight_ = parameter_dictionary->GetDouble("translation_delta_cost_weight");
    rotation_delta_cost_weight_ = parameter_dictionary->GetDouble("rotation_delta_cost_weight");
    CHECK_GE(translation_delta_cost_weight_, 0.);
    CHECK_GE(rotation_delta_cost_weight_, 0.);
}

RealTimeCorrelativeScanMatcher::RealTimeCorrelativeScanMatcher(
    const RealTimeCorrelativeScanMatcherOptions& options)
    : options_(options) {}

std::vector<Candidate>
RealTimeCorrelativeScanMatcher::GenerateExhaustiveSearchCandidates(
    const SearchParameters& search_parameters) const {
  int num_candidates = 0;
  for (int scan_index = 0; scan_index != search_parameters.num_scans;
       ++scan_index) {
    const int num_linear_x_candidates =
        (search_parameters.linear_bounds[scan_index].max_x -
         search_parameters.linear_bounds[scan_index].min_x + 1);
    const int num_linear_y_candidates =
        (search_parameters.linear_bounds[scan_index].max_y -
         search_parameters.linear_bounds[scan_index].min_y + 1);
    num_candidates += num_linear_x_candidates * num_linear_y_candidates;
  }
  std::vector<Candidate> candidates;
  candidates.reserve(num_candidates);
  for (int scan_index = 0; scan_index != search_parameters.num_scans;
       ++scan_index) {
    for (int x_index_offset = search_parameters.linear_bounds[scan_index].min_x;
         x_index_offset <= search_parameters.linear_bounds[scan_index].max_x;
         ++x_index_offset) {
      for (int y_index_offset =
               search_parameters.linear_bounds[scan_index].min_y;
           y_index_offset <= search_parameters.linear_bounds[scan_index].max_y;
           ++y_index_offset) {
        candidates.emplace_back(scan_index, x_index_offset, y_index_offset,
                                search_parameters);
      }
    }
  }
  CHECK_EQ(candidates.size(), num_candidates);
  return candidates;
}

double RealTimeCorrelativeScanMatcher::Match(
    const transform::Rigid2d& initial_pose_estimate,
    const sensor::PointCloud& point_cloud,
    const map::ProbabilityGrid& probability_grid,
    transform::Rigid2d* pose_estimate) const {
  CHECK_NOTNULL(pose_estimate);

  const Eigen::Rotation2Dd initial_rotation = initial_pose_estimate.rotation();
  const sensor::PointCloud rotated_point_cloud = sensor::TransformPointCloud(
      point_cloud,
      transform::Rigid3f::Rotation(Eigen::AngleAxisf(
          initial_rotation.cast<float>().angle(), Eigen::Vector3f::UnitZ())));
  const SearchParameters search_parameters(
      options_.linear_search_window_, options_.angular_search_window_,
      rotated_point_cloud, probability_grid.limits().resolution());

  const std::vector<sensor::PointCloud> rotated_scans =
      GenerateRotatedScans(rotated_point_cloud, search_parameters);
  const std::vector<DiscreteScan> discrete_scans = DiscretizeScans(
      probability_grid.limits(), rotated_scans,
      Eigen::Translation2f(initial_pose_estimate.translation().x(),
                           initial_pose_estimate.translation().y()));
  std::vector<Candidate> candidates =
      GenerateExhaustiveSearchCandidates(search_parameters);
  ScoreCandidates(probability_grid, discrete_scans, search_parameters,
                  &candidates);

  const Candidate& best_candidate =
      *std::max_element(candidates.begin(), candidates.end());
      //(liu) add
  std::sort(candidates.begin(), candidates.end(), std::greater<Candidate>());

  for (int i = 0; i < candidates.size(); i++)
  {
      if (candidates[0].score * 0.95 > candidates[i].score)
      {
          break;
      }

      double diff = sqrt((candidates[i].x - candidates[0].x) * (candidates[i].x - candidates[0].x) + (candidates[i].y - candidates[0].y) * (candidates[i].y - candidates[0].y));
      if (diff >= 0.2)
      {
          return 0;
      }
  }
  *pose_estimate = transform::Rigid2d(
      {initial_pose_estimate.translation().x() + best_candidate.x,
       initial_pose_estimate.translation().y() + best_candidate.y},
      initial_rotation * Eigen::Rotation2Dd(best_candidate.orientation));
  return best_candidate.score;
}

void RealTimeCorrelativeScanMatcher::ScoreCandidates(
    const map::ProbabilityGrid& probability_grid,
    const std::vector<DiscreteScan>& discrete_scans,
    const SearchParameters& search_parameters,
    std::vector<Candidate>* const candidates) const {
  for (Candidate& candidate : *candidates) {
    candidate.score = 0.f;
    for (const Eigen::Array2i& xy_index :
         discrete_scans[candidate.scan_index]) {
      const Eigen::Array2i proposed_xy_index(
          xy_index.x() + candidate.x_index_offset,
          xy_index.y() + candidate.y_index_offset);
      const float probability =
          probability_grid.GetProbability(proposed_xy_index);
      candidate.score += probability;
    }
    candidate.score /=
        static_cast<float>(discrete_scans[candidate.scan_index].size());
    candidate.score *=
        std::exp(-common::Pow2(std::hypot(candidate.x, candidate.y) *
                                   options_.translation_delta_cost_weight_ +
                               std::abs(candidate.orientation) *
                                   options_.rotation_delta_cost_weight_));
    CHECK_GT(candidate.score, 0.f);
  }
}

}  // namespace scan_matching
}  // namespace core
}  // namespace sample_carto
