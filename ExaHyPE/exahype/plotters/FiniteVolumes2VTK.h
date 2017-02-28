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
 
#ifndef _EXAHYPE_PLOTTERS_FINITE_VOLUMES_2_VTK_H_
#define _EXAHYPE_PLOTTERS_FINITE_VOLUMES_2_VTK_H_

#include "exahype/plotters/Plotter.h"

#include "tarch/plotter/griddata/blockstructured/PatchWriterUnstructured.h"

#include "tarch/plotter/griddata/VTUTimeSeriesWriter.h"

namespace exahype {
  namespace plotters {
    class FiniteVolumes2VTK;

    class FiniteVolumes2VTKAscii;
    class FiniteVolumes2VTKBinary;

    class FiniteVolumes2VTUAscii;
    class FiniteVolumes2VTUBinary;
  }
}

class exahype::plotters::FiniteVolumes2VTK: public exahype::plotters::Plotter::Device {
  protected:
   enum class PlotterType {
     BinaryVTK,
     ASCIIVTK,
     BinaryVTU,
     ASCIIVTU
   };
 private:
  const PlotterType _plotterType;
  int           _fileCounter;
  std::string   _filename;
  int           _numberOfCellsPerAxis;
  int           _ghostLayerWidth;
  int           _solverUnknowns;
  int           _writtenUnknowns;
  std::string   _select;

  /**
   * Is obviously only used if we use vtu instead of the vtk legacy format.
   */
  tarch::plotter::griddata::VTUTimeSeriesWriter _timeSeriesWriter;

  /**
   * To memorise the time argument from startPlotter(). We need it when we close the plotter for the time series.
   */
  double _time;

  tarch::la::Vector<DIMENSIONS, double>  _regionOfInterestLeftBottomFront;
  tarch::la::Vector<DIMENSIONS, double>  _regionOfInterestRightTopBack;

  tarch::plotter::griddata::blockstructured::PatchWriterUnstructured*
      _patchWriter;
  tarch::plotter::griddata::blockstructured::PatchWriter::SinglePatchWriter*
      _gridWriter;

  tarch::plotter::griddata::Writer::CellDataWriter*  _timeStampDataWriter;
  tarch::plotter::griddata::Writer::CellDataWriter*  _cellDataWriter;

  static tarch::logging::Log _log;

 public:
  FiniteVolumes2VTK(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing, const int ghostLayerWidth, PlotterType plotterType);
  virtual ~FiniteVolumes2VTK();

  virtual void init(const std::string& filename, int numberOfCellsPerAxis, int unknowns, int writtenUnknowns, const std::string& select);

  void plotPatch(const int cellDescriptionsIndex, const int element) override;

  void plotPatch(
      const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
      const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch, double* u,
      double timeStamp);

  virtual void startPlotting( double time );
  virtual void finishPlotting();
};


class exahype::plotters::FiniteVolumes2VTKAscii: public exahype::plotters::FiniteVolumes2VTK {
  public:
    FiniteVolumes2VTKAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,const int ghostLayerWidth);

    static std::string getIdentifier();
};


class exahype::plotters::FiniteVolumes2VTKBinary: public exahype::plotters::FiniteVolumes2VTK {
  public:
    FiniteVolumes2VTKBinary(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,const int ghostLayerWidth);

    static std::string getIdentifier();
};


class exahype::plotters::FiniteVolumes2VTUAscii: public exahype::plotters::FiniteVolumes2VTK {
  public:
    FiniteVolumes2VTUAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,const int ghostLayerWidth);

    static std::string getIdentifier();
};


class exahype::plotters::FiniteVolumes2VTUBinary: public exahype::plotters::FiniteVolumes2VTK {
  public:
    FiniteVolumes2VTUBinary(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,const int ghostLayerWidth);

    static std::string getIdentifier();
};


#endif
