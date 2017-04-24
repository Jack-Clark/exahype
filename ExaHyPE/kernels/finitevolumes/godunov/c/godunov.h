/**
 * This file is part of the ExaHyPE project.
 * Copyright (c) 2016  http://exahype.eu
 * All rights reserved.
 *
 * The project has received funding from the European Union's Horizon
 * 2020 research and innovation programme under grant agreement
 * No 671698. For copyrights and licensing, please consult the webpage.
 *
 * Released under the BSD 3 Open Source License.
 * For the full license text, see LICENSE.txt
 **/
#ifndef KERNELS_FINITEVOLUMES_C_GODUNOV_H_
#define KERNELS_FINITEVOLUMES_C_GODUNOV_H_

#include "tarch/la/Vector.h"

namespace kernels {
namespace finitevolumes {
namespace godunov {
namespace c {
  template <bool useSource, bool useNCP, bool useFlux, class SolverType>
  double solutionUpdate(
      SolverType& solver,double* luh_new, const double* luh,
      double** tempStateSizedVectors,double** tempUnknowns,
      const tarch::la::Vector<DIMENSIONS, double>& dx,double dt);
}  // namespace c
}  // namespace godunov
}  // namespace finitevolumes
}  // namespace kernels

#include "kernels/finitevolumes/godunov/c/2d/godunov.cpph"
#include "kernels/finitevolumes/godunov/c/3d/godunov.cpph"

#endif // KERNELS_FINITEVOLUMES_C_GODUNOV_H_
