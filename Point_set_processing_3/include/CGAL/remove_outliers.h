// Copyright (c) 2007-09  INRIA Sophia-Antipolis (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org).
//
// $URL$
// $Id$
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-Commercial
//
// Author(s) : Laurent Saboret and Nader Salman and Pierre Alliez

#ifndef CGAL_REMOVE_OUTLIERS_H
#define CGAL_REMOVE_OUTLIERS_H

#include <CGAL/license/Point_set_processing_3.h>

#include <CGAL/disable_warnings.h>

#include <CGAL/Point_set_processing_3/internal/Neighbor_query.h>
#include <CGAL/property_map.h>
#include <CGAL/point_set_processing_assertions.h>
#include <functional>

#include <CGAL/boost/graph/Named_function_parameters.h>
#include <CGAL/boost/graph/named_params_helper.h>

#include <iterator>
#include <algorithm>
#include <map>

namespace CGAL {


// ----------------------------------------------------------------------------
// Private section
// ----------------------------------------------------------------------------
/// \cond SKIP_IN_MANUAL
namespace internal {


/// Utility function for remove_outliers():
/// Computes average squared distance to the K nearest neighbors.
///
/// \pre `k >= 2`
///
/// @tparam Kernel Geometric traits class.
/// @tparam Tree KD-tree.
///
/// @return computed distance.
template <typename NeighborQuery>
typename NeighborQuery::Kernel::FT
compute_avg_knn_sq_distance_3(
  const typename NeighborQuery::Kernel::Point_3& query, ///< 3D point to project
    NeighborQuery& neighbor_query,                            ///< KD-tree
    unsigned int k,                        ///< number of neighbors
    typename NeighborQuery::Kernel::FT neighbor_radius)
{
    // geometric types
    typedef typename NeighborQuery::Kernel Kernel;
    typedef typename Kernel::FT FT;
    typedef typename Kernel::Point_3 Point;

    std::vector<Point> points;
    neighbor_query.get_points (query, k, neighbor_radius, std::back_inserter(points));

    // compute average squared distance
    typename Kernel::Compute_squared_distance_3 sqd;
    FT sq_distance = (FT)0.0;
    for(typename std::vector<Point>::iterator neighbor = points.begin(); neighbor != points.end(); neighbor++)
        sq_distance += sqd(*neighbor, query);
    sq_distance /= FT(points.size());
    return sq_distance;
}

} /* namespace internal */
/// \endcond


// ----------------------------------------------------------------------------
// Public section
// ----------------------------------------------------------------------------

/**
   \ingroup PkgPointSetProcessing3Algorithms
   Removes outliers:
   - computes average squared distance to the nearest neighbors,
   - and sorts the points in increasing order of average distance.

   This method modifies the order of input points so as to pack all remaining points first,
   and returns an iterator over the first point to remove (see erase-remove idiom).
   For this reason it should not be called on sorted containers.

   \pre `k >= 2`

   \tparam PointRange is a model of `Range`. The value type of
   its iterator is the key type of the named parameter `point_map`.

   \param points input point range.
   \param k number of neighbors
   \param np optional sequence of \ref psp_namedparameters "Named Parameters" among the ones listed below.

   \cgalNamedParamsBegin
     \cgalParamBegin{point_map} a model of `ReadablePropertyMap` with value type `geom_traits::Point_3`.
     If this parameter is omitted, `CGAL::Identity_property_map<geom_traits::Point_3>` is used.\cgalParamEnd
     \cgalParamBegin{neighbor_radius} spherical neighborhood
     radius. If provided, the neighborhood of a query point is
     computed with a fixed spherical radius instead of a fixed number
     of neighbors. In that case, the parameter `k` is used as a limit
     on the number of points returned by each spherical query (to
     avoid overly large number of points in high density areas). If no
     limit is wanted, use `k=0`.\cgalParamEnd
     \cgalParamBegin{threshold_percent} maximum percentage of points to remove.\cgalParamEnd
     \cgalParamBegin{threshold_distance} minimum distance for a point to be considered as outlier
     (distance here is the square root of the average squared distance to K nearest neighbors).\cgalParamEnd
     \cgalParamBegin{callback} an instance of
      `std::function<bool(double)>`. It is called regularly when the
      algorithm is running: the current advancement (between 0. and
      1.) is passed as parameter. If it returns `true`, then the
      algorithm continues its execution normally; if it returns
      `false`, the algorithm is stopped, all points are left unchanged
      and the function return `points.end()`.\cgalParamEnd
     \cgalParamBegin{geom_traits} an instance of a geometric traits class, model of `Kernel`\cgalParamEnd
   \cgalNamedParamsEnd

   \return iterator over the first point to remove.

   \note There are two thresholds that can be used:
   `threshold_percent` and `threshold_distance`. This function
   returns the smallest number of outliers such that at least one of
   these threshold is fulfilled. This means that if
   `threshold_percent=100`, only `threshold_distance` is taken into
   account; if `threshold_distance=0` only `threshold_percent` is
   taken into account.
*/
template <typename PointRange,
          typename NamedParameters
>
typename PointRange::iterator
remove_outliers(
  PointRange& points,
  unsigned int k,
  const NamedParameters& np)
{
  using parameters::choose_parameter;
  using parameters::get_parameter;

  // geometric types
  typedef typename CGAL::GetPointMap<PointRange, NamedParameters>::type PointMap;
  typedef typename Point_set_processing_3::GetK<PointRange, NamedParameters>::Kernel Kernel;

  PointMap point_map = choose_parameter<PointMap>(get_parameter(np, internal_np::point_map));
  typename Kernel::FT neighbor_radius = choose_parameter(get_parameter(np, internal_np::neighbor_radius),
                                                         typename Kernel::FT(0));
  double threshold_percent = choose_parameter(get_parameter(np, internal_np::threshold_percent), 10.);
  double threshold_distance = choose_parameter(get_parameter(np, internal_np::threshold_distance), 0.);
  const std::function<bool(double)>& callback = choose_parameter(get_parameter(np, internal_np::callback),
                                                                 std::function<bool(double)>());

  typedef typename Kernel::FT FT;

  // basic geometric types
  typedef typename PointRange::iterator iterator;
  typedef typename iterator::value_type value_type;

  // actual type of input points
  typedef typename std::iterator_traits<typename PointRange::iterator>::value_type Enriched_point;

  // types for K nearest neighbors search structure
  typedef Point_set_processing_3::internal::Neighbor_query<Kernel, PointRange&, PointMap> Neighbor_query;

  // precondition: at least one element in the container.
  // to fix: should have at least three distinct points
  // but this is costly to check
  CGAL_point_set_processing_precondition(points.begin() != points.end());

  // precondition: at least 2 nearest neighbors
  CGAL_point_set_processing_precondition(k >= 2);

  CGAL_point_set_processing_precondition(threshold_percent >= 0 && threshold_percent <= 100);

  Neighbor_query neighbor_query (points, point_map);

  std::size_t nb_points = points.size();

  // iterate over input points and add them to multimap sorted by distance to k
  std::multimap<FT,Enriched_point> sorted_points;
  std::size_t nb = 0;
  for(const value_type& vt : points)
  {
    FT sq_distance = internal::compute_avg_knn_sq_distance_3(
      get(point_map, vt),
      neighbor_query, k, neighbor_radius);
    sorted_points.insert( std::make_pair(sq_distance, vt) );
    if (callback && !callback ((nb+1) / double(nb_points)))
      return points.end();
    ++ nb;
  }

  // Replaces [points.begin(), points.end()) range by the multimap content.
  // Returns the iterator after the (100-threshold_percent) % best points.
  typename PointRange::iterator first_point_to_remove = points.begin();
  typename PointRange::iterator dst = points.begin();
  int first_index_to_remove = int(double(sorted_points.size()) * ((100.0-threshold_percent)/100.0));
  typename std::multimap<FT,Enriched_point>::iterator src;
  int index;
  for (src = sorted_points.begin(), index = 0;
       src != sorted_points.end();
       ++src, ++index)
  {
    *dst++ = src->second;
    if (index <= first_index_to_remove ||
        src->first < threshold_distance * threshold_distance)
      first_point_to_remove = dst;
  }

  return first_point_to_remove;
}

/// \cond SKIP_IN_MANUAL
// variant with default NP
template <typename PointRange>
typename PointRange::iterator
remove_outliers(
  PointRange& points,
  unsigned int k) ///< number of neighbors.
{
  return remove_outliers (points, k, CGAL::Point_set_processing_3::parameters::all_default(points));
}
/// \endcond


} //namespace CGAL

#include <CGAL/enable_warnings.h>

#endif // CGAL_REMOVE_OUTLIERS_H
