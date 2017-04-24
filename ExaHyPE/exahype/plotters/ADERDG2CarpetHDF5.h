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

namespace kernels {
  class index; // instead #include "kernels/KernelUtils.h"
}

/**
 * <h2>Writing CarpetHDF5 files which are compatible to Cactus/EinsteinToolkit</h2>
 * 
 * This plotters produces files similar to the CarpetHDF5 file format. This file format is
 * produced by the https://www.carpetcode.org/ Carpet code for http://cactuscode.org/ Cactus
 * by the http://einsteintoolkit.org/ Einsteintoolkit.
 *
 * By using this plotter, users can use their Cactus postprocessing tools seamless with ExaHyPE
 * output without even knowing. There are also readers included into Visit and Amira for
 * opening the CarpetHDF5 file format.
 *
 * <h3>Output format variants</h3>
 * 
 * The writer has two major flags to trigger the way how H5 files are produced:
 * 
 * <strong> oneFilePerTimestep </strong> mimics the way how ExaHyPE produces VTK files: There is
 * always (at least) one file per timestep. This is <em>not</em> the way how the CarpetHDF5 file
 * format works and you will have to join the HDF5 files with tools like h5join (shipped with
 * Carpet/Cactus) in order to read these files with ordinary writers. Nevertheless this is helpful
 * for debugging or immediately opening single timestep snapshots in Visit.
 * 
 * <strong> allUnknownsInOneFile </strong> allows you to reduce the number of H5 files by putting
 * the (vector of) all written unknowns in a single file. This also mimics how ExaHyPE's VTK files
 * look like. In contrast, in Cactus one typically has one hdf5 file per group/per physical field.
 * This allows easily to copy only the interesting fields from a supercomputer.
 *
 * <h3>General limitations</h3>
 * 
 * <strong> No global MPI writing </strong> Currently, each MPI rank produces its own file. In
 * contrast, in Cactus there is by default a single file accross all ranks. Again, files can be
 * easily merged with standard cactus tools.
 *
 * <strong> Structure of array vs. array of structures </strong>
 * The CarpetHDF5 file format directly resembles the closest way how to dump Cactus memory into files.
 * Cactus stores structures of arrays, for instance the four hydro variables {rho,velx,vely,velz,eps}
 * are stored as four big arrays in Cactus. In contrast, ExaHyPE stores arrays of structures, hence
 * in principle one big array where at each point there are the four variables. For this very reason,
 * producing CarpetHDF5 files comes with a lot of overhead, ie. a needless amount of over and over
 * repeated metadata and way too much components.
 *
 * By no means this is the fault of the HDF5 binary table format but really the way how CarpetHDF5
 * works or can be "maximally streched" to also fit ExaHyPE in.
 * 
 * <h3>How to build this plotter into your release</h3>
 * 
 * By default, this plotter is excluded from compiling. If you enable it in your spec file, the code
 * will stop at startup, throwing a message that this plotter is not supported.
 * 
 * In order to use this plotter, define "HDF5" and provide the HDF5 serial C++ header path and
 * libraries. This can be done by adding to your project
 *
    PROJECT_CFLAGS+=-DHDF5
    PROJECT_CFLAGS+=-I/usr/include/hdf5/serial -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -Wdate-time -D_FORTIFY_SOURCE=2 -fstack-protector-strong
    PROJECT_LFLAGS+=-L/usr/lib/x86_64-linux-gnu/hdf5/serial -lhdf5 -l hdf5_cpp /usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5_hl.a /usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5.a
    
 * This snippet works on a standard Ubuntu system with hdf5 developer libraries installed.
 * I extracted them from "h5cc -show" and "pkg-config --libs hdf5"
 * 
 * <h3>Connection to other plotters in ExaHyPE</h3>
 *
 * Actually this plotter is quite similar to the CartesianVTK format but just with regular
 * block patches. It is also similar to the new PeanoPatchFileFormat which was developed
 * in the same time.
 *
 * Thanks to Roland Haas for support in Munich and Frankfurt when coming up with the file format.
 *
 * @author Sven Köppel
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
  kernels::index *writtenCellIdx, *singleFieldIdx;
  char**        writtenQuantitiesNames;

  /**
   * Pimpl idiom: In order to avoid any HDF5 dependency all HDF5 logic is hidden inside this
   * class (instance).
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
  
  // TODO: These ADER interpolating routines should be in some generic library
  void interpolateCartesianPatch(
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    double *u,
    double *mappedCell,
    double timeStamp
  );
};

#endif/* _EXAHYPE_PLOTTERS_ADERDG_2_CARPETHDF5_H_ */
