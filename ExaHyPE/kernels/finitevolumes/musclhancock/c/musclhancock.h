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
#ifndef KERNELS_FINITEVOLUMES_C_MUSCLHANCOCK_H_
#define KERNELS_FINITEVOLUMES_C_MUSCLHANCOCK_H_

#include "tarch/la/Vector.h"

namespace kernels {
namespace finitevolumes {
namespace musclhancock {
namespace c {
  /**
   * This is the CFL factor for checking
   * if the time step size estimated at the
   * end of the last iteration is admissible.
   *
   * It can be chosen very close to one.
   */
  constexpr double CFL = 0.99;

  /**
   * Returns 0 if a and b have opposite signs.
   * Returns the minimum of a and b if
   * they have the same sign.
   */
  double minmod(double a, double b);

  template <bool useSource, bool useNCP, bool useFlux, class SolverType>
  double solutionUpdate(
      SolverType& solver,double* luh_new, const double* luh,
      double** tempStateSizedVectors,double** tempUnknowns,
      const tarch::la::Vector<DIMENSIONS, double>& dx,double dt);
}  // namespace c
}  // namespace musclhancock
}  // namespace finitevolumes
}  // namespace kernels

#include "kernels/finitevolumes/musclhancock/c/2d/musclhancock.cpph"
#include "kernels/finitevolumes/musclhancock/c/3d/musclhancock.cpph"

#endif // KERNELS_FINITEVOLUMES_C_MUSCLHANCOCK_H_
