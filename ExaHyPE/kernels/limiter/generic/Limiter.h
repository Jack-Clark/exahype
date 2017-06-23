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

#ifndef _EXAHYPE_KERNELS_LIMITER_GENERIC_H_
#define _EXAHYPE_KERNELS_ADERDG_GENERIC_H_

#include <algorithm>
#include <stdexcept>
#include <stdlib.h>

#include "../../LimiterProjectionMatrices.h"
#include "../../GaussLegendreQuadrature.h"
#include "../../KernelUtils.h"

#include "peano/utils/Globals.h"

// forward declarations
namespace exahype {
namespace solvers {
 class ADERDGSolver;
}
}

namespace kernels {
namespace limiter {
namespace generic {
namespace c {

/**
 * \brief Projection ADERDG -> FV
 *
 * Projects the ADERDG solution onto
 * the finite volumes limiter space.
 *
 * \param[in] basisSize The size of the ADER-DG basis per coordinate axis (order+1).
 * \param[in] ghostLayerWidth The ghost layer width in cells of the finite volumes patch
 */
void projectOnFVLimiterSpace(const double* const luh, const int numberOfVariables, const int basisSize, const int ghostLayerWidth, double* const lim);
/**
 * \brief Projection FV -> ADERDG
 *
 * Projects the finite volumes limiter solution onto
 * the DG space.
 *
 * \param[in] basisSize The size of the ADER-DG basis per coordinate axis (order+1)
 * \param[in] ghostLayerWidth The ghost layer width in cells of the finite volumes patch.
 */
void projectOnDGSpace(const double* const lim, const int numberOfVariables, const int basisSize, const int ghostLayerWidth, double* const luh);

/**
 * Determine the cell-local minimum and maximum
 * values from the solution evaluated at the
 * Gauss-Legendre nodes, the Gauss-Lobatto nodes as well as at
 * the subcell limiters.
 */
void findCellLocalMinAndMax(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    double* const localMinPerVariables, double* const localMaxPerVariable);

/**
 * Find the minimum and maximum per variable in the limiter solution.
 *
 * We need this function to compute the minimum and maximum
 * values for cells that do not hold a valid ADER-DG
 * solution (troubled cells) and their neighbours.
 * See SUBROUTINE GetMinMax in file
 * ADERDG_Limiter_3D/Limiter.f90.
 *
 * \param[in] basisSize The size of the ADER-DG basis per coordinate axis (order+1)
 * \param[in] ghostLayerWidth The ghost layer width in cells of the finite volumes patch.
 */
void findCellLocalLimiterMinAndMax(
    const double* const lim,
    const exahype::solvers::ADERDGSolver* solver,
    const int ghostLayerWidth,
    double* localMinPerObservable, double* localMaxPerObservable);

/**
 * Returns true if the nodal solution degrees of freedom
 * satisfy a discrete maximum principle.
 *
 * Writes the new cell local min and max to the face 0 block
 * of boundaryMinPerVariables and \p boundaryMaxPerVariables.
 *
 * Writes the minimised/maximised boundary min and max to the face 1 block
 * of boundaryMinPerVariables and \p boundaryMaxPerVariables.
 *
 * \note[24/11/16]
 * We currently abuse the term Voronoi neighbour for direct neighbour.
 *
 * \param[in] luh                           The nodal solution degrees of freedom
 * \param[in] solver                        An ADERDG solver
 * \param[in] relaxationParameter The relaxation parameter for the discrete maximum principle (DMP).
 * \param[in] differenceScaling             The difference scaling factor for the discrete maximum principle (DMP).
 * \param[inout] boundaryMinPerVariables    An array of size \p numberOfVariables times DIMENSIONS_TIMES_TWO
 *                                          containing the minimum values per variable of the current cell
 *                                          and its neighbour at the particular face. Together these values
 *                                          can be used to compute the Voronoi maximum per variable.
 * \param[inout] boundaryMaxPerVariables    An array of size \p numberOfVariables times DIMENSIONS_TIMES_TWO
 *                                          containing the maximum values per variable of the current cell
 *                                          and its neighbour at the particular face. Together these values
 *                                          can be used to compute the Voronoi maximum per variable.
 *
 * <h2>Background</h2>
 * A candidate solution \f$ u^{*}_h(x,t^{n+1}) \f$ is said to satisfy
 * the discrete maximum principle if it satisfies a relaxed
 * maximum principle of the form
 *
 * \f[
 *   \min_{y \in V_i} (u_h(y,t^n)) - \delta \leq \, u^{*}_h(x,t^{n+1}) \leq \, \max_{y \in V_i} (u_h(y,t^n)) + \delta,
 *   \;\forall \x \in T_i
 * \f]
 *
 * for every element \f$ T_i \f$ in the mesh. Above, $\f$ V_i \f$ is a set containing the Voronoi neighbours
 * of element \f$ T_i \f$ and the \f$ T_i \f$ itself.
 * The relaxation parameter \f$ \delta \f$ is computed according to:
 *
 * \f[
 *  \delta = \max \left( \delta_0,\, \epsilon \cdot \left( \max_{y \in V_i} (u_h(y,t^n)) - \min_{y \in V_i} (u_h(y,t^n)) \right) \right),
 * \f]
 * with \f$ \delta_0 \f$ denoting the maximum relaxation parameter we want to allow,
 * and \epsilon scales the difference of Voronoi maximum and minimum.
 *
 * See doi:10.1016/j.jcp.2014.08.009 for more details.
 */
bool discreteMaximumPrincipleAndMinAndMaxSearch(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    const double relaxationParameter,const double differenceScaling,
    double* boundaryMinPerVariables, double* boundaryMaxPerVariables);

//************************
//*** Helper functions ***
//************************

inline int getBasisSizeLim(const int basisSize) {
  return 2*(basisSize-1)+1;
}

//*************************
//*** Private functions ***
//*************************

/**
 * Compare the min and max values with the interpolated values
 * at the Gauss-Lobatto quadrature nodes.
 *
 * The n Gauss-Lobatto nodes are the support points of
 * the maxima of the Legendre polynomial of order n-1.
 */
void compareWithADERDGSolutionAtGaussLobattoNodes(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    double* min, double* max);

/**
 * Compare the min and max values with the interpolated values
 * at the FV subcell centers.
 *
 * \note It is very important to make this check. Otherwise
 * wrong values at the FV subcell centers get not noticed and
 * this becomes a problem if we project the DG solution onto the FV
 * subcell.
 */
void compareWithADERDGSolutionAtFVSubcellCenters(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    double* min, double* max);

} // namespace c
} // namespace generic
} // namespace limiter
} // namespace kernel


#endif //_EXAHYPE_KERNELS_LIMITER_GENERIC_H_
