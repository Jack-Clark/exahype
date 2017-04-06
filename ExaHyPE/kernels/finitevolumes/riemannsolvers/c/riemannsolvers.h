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
#ifndef KERNELS_FINITEVOLUMES_RIEMANNSOLVERS_C_RIEMANNSOLVERS_H_
#define KERNELS_FINITEVOLUMES_RIEMANNSOLVERS_C_RIEMANNSOLVERS_H_

#include "kernels/KernelUtils.h"

namespace kernels {
namespace finitevolumes {
namespace riemannsolvers {
namespace c {

/**
 * A simple Rusanov flux considering pointwise
 * left and right values.
 *
 * \note This does not result in a well-balanced scheme.
 * It is not a good Riemann solver for the Shallow Water Equations (SWE) e.g.
 */
template <typename SolverType>
double rusanov(SolverType& solver, double* fL, double *fR, const double* qL, const double* qR,
               int normalNonZero);
} // namespace c
} // namespace riemansolvers
} // namespace finitevolumes
} // namespace kernels

#include "rusanov.cpph"

#endif /* KERNELS_FINITEVOLUMES_RIEMANNSOLVERS_C_RIEMANNSOLVERS_H_ */
