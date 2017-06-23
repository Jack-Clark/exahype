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
 * 
 * @authors: Sven Koeppel
 **/
 
#ifndef _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_
#define _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_

#include "exahype/plotters/Plotter.h"

namespace exahype {
  namespace plotters {
    class ADERDG2CarpetHDF5;
    class CarpetHDF5Writer;
    
    class CartesianSlicer; // forward decl
  }
}

namespace kernels {
  class index; // instead #include "kernels/KernelUtils.h"
}

/**
 * <h2>Projecting ADERDG data onto the CarpetHDF5 file format</h2>
 * 
 * This plotter is very similar to the CartesianVTK format, it projects the ADERDG DOF
 * onto a regular grid which is written out using the CarpetHDF5Writer.
 * 
 */
class exahype::plotters::ADERDG2CarpetHDF5 : public exahype::plotters::Plotter::Device {
 public:
  typedef tarch::la::Vector<DIMENSIONS, double> dvec;

  /**
   * Pimpl idiom: In order to avoid any HDF5 dependency all HDF5 logic is hidden inside this
   * class (instance).
   **/
  CarpetHDF5Writer* writer;
  static tarch::logging::Log _log;

  static std::string getIdentifier();

  ADERDG2CarpetHDF5(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);

  virtual ~ADERDG2CarpetHDF5();

  virtual void init(const std::string& filename, int orderPlusOne, int solverUnknowns, int writtenUnknowns, const std::string& select);

  virtual void plotPatch(
        const int cellDescriptionsIndex,
        const int element);
  
  virtual void plotPatch(
      const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
      const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch, double* u,
      double timeStamp);
  
  virtual void startPlotting( double time );
  virtual void finishPlotting();

  // TODO: These ADER interpolating routines should be in some generic library
  void interpolateCartesianPatch(
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& dx,
    double *u,
    double *mappedCell,
    double timeStamp
  );

  void interpolateCartesianSlicedPatch(
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& dx,
    double *u,
    double *mappedCell,
    double timeStamp,
    const exahype::plotters::CartesianSlicer& slicer
  );
};

#endif/* _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_ */
