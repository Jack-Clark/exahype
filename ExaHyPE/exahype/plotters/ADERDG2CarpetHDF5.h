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
 
#ifndef _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_
#define _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_

#include "exahype/plotters/Plotter.h"

namespace exahype {
  namespace plotters {
    class ADERDG2CarpetHDF5;
    class ADERDG2CarpetHDF5Writer;
  }
}

/**
 * An experimental plotter to produce files similar to the CarpetHDF5 file format.
 * This device is similiar to the LegendreVTK format but just with regular block patches,
 * binary HDF5 output and trying to keep compatible to HDF5.
 * 
 * This code only compiles if you define HDF5. Otherwise instantiation of this
 * class yields an error.
 * 
 */
class exahype::plotters::ADERDG2CarpetHDF5 : public exahype::plotters::Plotter::Device {
 public:
  int           fileCounter;
  std::string   filename;
  int           order;
  int           solverUnknowns;
  int           writtenUnknowns;
  std::string   select;
  double        time;
  int           iteration;
  int           component;

  /**
   * Pimpl idiom: In order to avoid any HDF5 dependency when compiling without.
   **/
  ADERDG2CarpetHDF5Writer* writer;

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
  
  // should be in some generic library
  void interpolateCartesianPatch(
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    double *u,
    double *mappedCell,
    double timeStamp
  );
};

#endif/* _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_ */
