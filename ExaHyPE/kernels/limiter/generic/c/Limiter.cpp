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

#include "../Limiter.h"

#include <algorithm>

#include "tarch/la/ScalarOperations.h"

#include "exahype/solvers/ADERDGSolver.h"

//Fortran (Limiter.f90): GetSubcellData
void kernels::limiter::generic::c::projectOnFVLimiterSpace(
    const double* const luh, const int numberOfVariables, const int basisSize, const int ghostLayerWidth, double* const lim) {
  const int basisSizeLim = getBasisSizeLim(basisSize);
  
#if DIMENSIONS == 3
  const int basisSize3D       = basisSize;
  const int basisSizeLim3D    = basisSizeLim;
  const int ghostLayerWidth3D = ghostLayerWidth;
#else
  constexpr int basisSize3D       = 1;
  constexpr int basisSizeLim3D    = 1;
  constexpr int ghostLayerWidth3D = 0;
#endif

  idx4 idxLuh(basisSize3D, basisSize, basisSize, numberOfVariables);
  idx4 idxLim(basisSizeLim3D+2*ghostLayerWidth3D, basisSizeLim+2*ghostLayerWidth, basisSizeLim+2*ghostLayerWidth, numberOfVariables);
  idx2 idxConv(basisSize, basisSizeLim); 
  
  int x,y,z,v,ix,iy,iz; 
  
  //tensor operation
  for(z=ghostLayerWidth3D; z<basisSizeLim3D+ghostLayerWidth3D; z++) { // We can skip x,y,z>=basisSizeLim+ghostLayerWidth
    for(y=ghostLayerWidth; y<basisSizeLim+ghostLayerWidth; y++) {
      for(x=ghostLayerWidth; x<basisSizeLim+ghostLayerWidth; x++) {
        for(v=0; v<numberOfVariables; v++) {
          lim[idxLim(z,y,x,v)] = 0;
          for(iz=0; iz<basisSize3D; iz++) {
            for(iy=0; iy<basisSize; iy++) {
              for(ix=0; ix<basisSize; ix++) {
                lim[idxLim(z,y,x,v)] += luh[idxLuh(iz,iy,ix,v)] 
                                        #if DIMENSIONS == 3
                                        * uh2lim[basisSize-1][idxConv(iz,z-ghostLayerWidth3D)]
                                        #endif
                                        * uh2lim[basisSize-1][idxConv(iy,y-ghostLayerWidth)]
                                        * uh2lim[basisSize-1][idxConv(ix,x-ghostLayerWidth)];
              }
            }
          }
        }
      }
    }
  }
  
}

//Fortran (Limiter.f90): PutSubcellData
void kernels::limiter::generic::c::projectOnDGSpace(
    const double* const lim, const int numberOfVariables, const int basisSize, const int ghostLayerWidth, double* const luh) {
  const int basisSizeLim = getBasisSizeLim(basisSize);
  
#if DIMENSIONS == 3
  const int basisSize3D       = basisSize;
  const int basisSizeLim3D    = basisSizeLim;
  const int ghostLayerWidth3D = ghostLayerWidth;
#else
  constexpr int basisSize3D       = 1;
  constexpr int basisSizeLim3D    = 1;
  constexpr int ghostLayerWidth3D = 0;
#endif
  
  idx4 idxLuh(basisSize3D, basisSize, basisSize, numberOfVariables);
  idx4 idxLim(basisSizeLim3D+2*ghostLayerWidth3D, basisSizeLim+2*ghostLayerWidth, basisSizeLim+2*ghostLayerWidth, numberOfVariables);
  idx2 idxConv(basisSizeLim, basisSize);
  
  int x,y,z,v,ix,iy,iz;

  //tensor operation
  for(z=0; z<basisSize3D; z++) {
    for(y=0; y<basisSize; y++) {
      for(x=0; x<basisSize; x++) {
        for(v=0; v<numberOfVariables; v++) {
          luh[idxLuh(z,y,x,v)] = 0;
          for(iz=ghostLayerWidth3D; iz<basisSizeLim3D+ghostLayerWidth3D; iz++) { // We can skip ix,iy,iz>=basisSizeLim+ghostLayerWidth
            for(iy=ghostLayerWidth; iy<basisSizeLim+ghostLayerWidth; iy++) {
              for(ix=ghostLayerWidth; ix<basisSizeLim+ghostLayerWidth; ix++) {
                luh[idxLuh(z,y,x,v)] += lim[idxLim(iz,iy,ix,v)] 
                                        #if DIMENSIONS == 3
                                        * lim2uh[basisSize-1][idxConv(iz-ghostLayerWidth3D,z)]
                                        #endif
                                        * lim2uh[basisSize-1][idxConv(iy-ghostLayerWidth,y)]
                                        * lim2uh[basisSize-1][idxConv(ix-ghostLayerWidth,x)];
              }
            }
          }
        }
      }
    }
  }
  
}

bool kernels::limiter::generic::c::discreteMaximumPrincipleAndMinAndMaxSearch(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    const double relaxationParameter,const double differenceScaling,
    double* boundaryMinPerObservable, double* boundaryMaxPerObservable) {
  const int numberOfObservables = solver->getDMPObservables();

  double* localMinPerObservable = new double[numberOfObservables];
  double* localMaxPerObservable = new double[numberOfObservables];

  // 1. Determine the new cell-local -minimum and maximummin and max
  findCellLocalMinAndMax(luh,solver,localMinPerObservable,localMaxPerObservable);

  // 2. Compare to the boundary minimum and maximum
  bool discreteMaximumPrincipleSatisfied=true;
  for(int v = 0; v < numberOfObservables; v++) {
    double boundaryMin = boundaryMinPerObservable[v];
    for (int i=1; i<DIMENSIONS_TIMES_TWO; i++) {
      boundaryMin = std::min( boundaryMin, boundaryMinPerObservable[i*numberOfObservables+v] );
    }
    double boundaryMax = boundaryMaxPerObservable[v];
    for (int i=1; i<DIMENSIONS_TIMES_TWO; i++) {
      boundaryMax = std::max( boundaryMax, boundaryMaxPerObservable[i*numberOfObservables+v] );
    }
    double scaledDifference = (boundaryMax - boundaryMin) * differenceScaling;

    assertion5(tarch::la::greaterEquals(scaledDifference,0.0),scaledDifference,boundaryMin,boundaryMax,localMinPerObservable[v],localMaxPerObservable[v]);
    scaledDifference = std::max( scaledDifference, relaxationParameter );

    if((localMinPerObservable[v] < (boundaryMin - scaledDifference)) ||
       (localMaxPerObservable[v] > (boundaryMax + scaledDifference))) {
      discreteMaximumPrincipleSatisfied=false;
    }

    // TODO(Dominic): A little hacky

    // We have the new min and max directly available now and
    // overwrite the block for face 0 with it
    boundaryMinPerObservable[v] = localMinPerObservable[v];
    boundaryMaxPerObservable[v] = localMaxPerObservable[v];

    // In the block for face 1, we write the boundary min and max
    boundaryMinPerObservable[v+numberOfObservables] = boundaryMin;
    boundaryMaxPerObservable[v+numberOfObservables] = boundaryMax;
  }

  // clean up
  delete[] localMinPerObservable;
  delete[] localMaxPerObservable;

  return discreteMaximumPrincipleSatisfied;
}

void kernels::limiter::generic::c::findCellLocalLimiterMinAndMax(
    const double* const lim,
    const exahype::solvers::ADERDGSolver* solver,
    const int ghostLayerWidth,
    double* const localMinPerVariables, double* const localMaxPerVariables
) {
  const int numberOfObservables = solver->getDMPObservables();
  std::fill_n(localMinPerVariables,numberOfObservables,std::numeric_limits<double>::max());
  std::fill_n(localMaxPerVariables,numberOfObservables,-std::numeric_limits<double>::max());

  const int basisSize    = solver->getNodesPerCoordinateAxis();
  const int basisSizeLim = getBasisSizeLim(basisSize);
  int basisSizeLim3D    = 1;
  int ghostLayerWidth3D = 0;
  #if DIMENSIONS == 3
  basisSizeLim3D    = basisSizeLim;
  ghostLayerWidth3D = ghostLayerWidth;
  #endif

  double* observables = new double[numberOfObservables];
  const int numberOfVariables = solver->getNumberOfVariables();
  idx4 idxLim(basisSizeLim3D+2*ghostLayerWidth3D,basisSizeLim+2*ghostLayerWidth,basisSizeLim+2*ghostLayerWidth,numberOfVariables);
  for (int iz=ghostLayerWidth3D; iz<basisSizeLim3D+ghostLayerWidth3D; ++iz) { // skip the last element
    for (int iy=ghostLayerWidth; iy<basisSizeLim+ghostLayerWidth; ++iy) {
      for (int ix=ghostLayerWidth; ix<basisSizeLim+ghostLayerWidth; ++ix) {
        solver->mapDiscreteMaximumPrincipleObservables(
            observables,numberOfObservables,lim+idxLim(iz,iy,ix,0));

        for (int v=0; v<numberOfObservables; v++) {
          localMinPerVariables[v] = std::min ( localMinPerVariables[v], observables[v] );
          localMaxPerVariables[v] = std::max ( localMaxPerVariables[v], observables[v] );
        }
      }
    }
  }

  // clean up
  delete[] observables;
}

/**
 * localMinPerVariables, localMaxPerVariables are double[numberOfVariables]
 */
void kernels::limiter::generic::c::findCellLocalMinAndMax(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    double* const localMinPerVariables, double* const localMaxPerVariables) {
  const int numberOfObservables = solver->getDMPObservables();
  std::fill_n(localMinPerVariables,numberOfObservables,std::numeric_limits<double>::max());
  std::fill_n(localMaxPerVariables,numberOfObservables,-std::numeric_limits<double>::max());

  const int basisSize = solver->getNodesPerCoordinateAxis();
  int basisSize3D = 1;
  #if DIMENSIONS == 3
  basisSize3D = basisSize;
  #endif

  double* observables = new double[numberOfObservables];
  const int numberOfVariables = solver->getNumberOfVariables();
  idx4 idx(basisSize3D,basisSize,basisSize,numberOfVariables);
  for(int iz = 0; iz < basisSize3D; iz++) {
    for(int iy = 0; iy < basisSize;   iy++) {
      for(int ix = 0; ix < basisSize;   ix++) {
        solver->mapDiscreteMaximumPrincipleObservables(
            observables,numberOfObservables,luh+idx(iz,iy,ix,0));

        for (int v=0; v<numberOfObservables; v++) {
          localMinPerVariables[v] = std::min ( localMinPerVariables[v], observables[v] );
          localMaxPerVariables[v] = std::max ( localMaxPerVariables[v], observables[v] );
        }
      }
    }
  }
  compareWithADERDGSolutionAtGaussLobattoNodes(luh, solver, localMinPerVariables, localMaxPerVariables);
  compareWithADERDGSolutionAtFVSubcellCenters (luh, solver, localMinPerVariables, localMaxPerVariables);

  // clean up
  delete[] observables;
}

//*************************
//*** Private functions ***
//*************************
/**
 * Auxilliary function to findMinMax
 * Project to GaussLobatto and modify the min/max if required
 */
void kernels::limiter::generic::c::compareWithADERDGSolutionAtGaussLobattoNodes(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    double* min, double* max) {
  const int basisSize   = solver->getNodesPerCoordinateAxis();
  const int order       = basisSize-1;

  int basisSize3D = 1;
  #if DIMENSIONS == 3
  basisSize3D = basisSize;
  #endif

  const int numberOfVariables = solver->getNumberOfVariables();
  idx4 idx(basisSize3D,basisSize,basisSize,numberOfVariables);
  idx2 idxConv(basisSize,basisSize);

  const int numberOfObservables = solver->getDMPObservables();
  double* observables = new double[numberOfObservables];
  double* lobValues   = new double[numberOfVariables];
  for(int z=0; z<basisSize3D; z++) {
    for(int y=0; y<basisSize; y++) {
      for(int x=0; x<basisSize; x++) {
        for(int v=0; v<numberOfVariables; v++) {
          lobValues[v] = 0.0;
          for(int iz=0; iz<basisSize3D; iz++) {
            for(int iy=0; iy<basisSize; iy++) {
              for(int ix=0; ix<basisSize;ix++) {
                lobValues[v] += luh[idx(iz,iy,ix,v)]
                            #if DIMENSIONS == 3
                            * uh2lob[order][idxConv(iz,z)]
                            #endif
                            * uh2lob[order][idxConv(iy,y)]
                            * uh2lob[order][idxConv(ix,x)];
              }
            }
          }
        }

        solver->mapDiscreteMaximumPrincipleObservables(
            observables,numberOfObservables,lobValues);
        for (int v=0; v<numberOfObservables; v++) {
          min[v] = std::min( min[v], observables[v] );
          max[v] = std::max( max[v], observables[v] );
        }
      }
    }
  }

  // clean up
  delete[] observables;
  delete[] lobValues;
}

/**
 * Auxilliary function to findMinMax
 * Project onto FV subcell nodes and modify the min/max if required
 */
void kernels::limiter::generic::c::compareWithADERDGSolutionAtFVSubcellCenters(
    const double* const luh,
    const exahype::solvers::ADERDGSolver* solver,
    double* min, double* max) {
  const int basisSize    = solver->getNodesPerCoordinateAxis();
  const int basisSizeLim = getBasisSizeLim(basisSize);

  int basisSize3D    = 1;
  int basisSizeLim3D = 1;
  #if DIMENSIONS == 3
  basisSize3D    = basisSize;
  basisSizeLim3D = basisSizeLim;
  #endif

  const int numberOfVariables   = solver->getNumberOfVariables();
  idx4 idxLuh(basisSize3D,basisSize,basisSize,numberOfVariables);
  idx4 idxLim(basisSizeLim3D,basisSizeLim,basisSizeLim,numberOfVariables);
  idx2 idxConv(basisSize,basisSizeLim);

  const int numberOfObservables = solver->getDMPObservables();

  double* observables = new double[numberOfObservables];
  double* limValues   = new double[numberOfVariables];

  //tensor operation
  for(int z=0; z<basisSizeLim3D; z++) {
    for(int y=0; y<basisSizeLim; y++) {
      for(int x=0; x<basisSizeLim; x++) {
        for(int v=0; v<numberOfVariables; v++) {
          limValues[v] = 0.0;
          for(int iz=0; iz<basisSize3D; iz++) {
            for(int iy=0; iy<basisSize; iy++) {
              for(int ix=0; ix<basisSize; ix++) {
                limValues[v] += luh[idxLuh(iz,iy,ix,v)]
                                #if DIMENSIONS == 3
                                * uh2lim[basisSize-1][idxConv(iz,z)]
                                #endif
                                * uh2lim[basisSize-1][idxConv(iy,y)]
                                * uh2lim[basisSize-1][idxConv(ix,x)];
              }
            }
          }
        }

        solver->mapDiscreteMaximumPrincipleObservables(
            observables,numberOfObservables,limValues);
        for (int v=0; v<numberOfObservables; v++) {
          min[v] = std::min( min[v], observables[v] );
          max[v] = std::max( max[v], observables[v] );
        }
      }
    }
  }

  // clean up
  delete[] observables;
  delete[] limValues;
}
