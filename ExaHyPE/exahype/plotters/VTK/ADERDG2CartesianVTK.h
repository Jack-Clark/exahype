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
 
#ifndef _EXAHYPE_PLOTTERS_ADERDG_2_CARTESIAN_VTK_H_
#define _EXAHYPE_PLOTTERS_ADERDG_2_CARTESIAN_VTK_H_

#include "exahype/plotters/Plotter.h"

#include "tarch/plotter/griddata/blockstructured/PatchWriterUnstructured.h"
#include "tarch/plotter/griddata/VTUTimeSeriesWriter.h"

namespace exahype {
  namespace plotters {
    class ADERDG2CartesianVTK;

    class ADERDG2CartesianVerticesVTKAscii;
    class ADERDG2CartesianVerticesVTKBinary;
    class ADERDG2CartesianCellsVTKAscii;
    class ADERDG2CartesianCellsVTKBinary;

    class ADERDG2CartesianVerticesVTUAscii;
    class ADERDG2CartesianVerticesVTUBinary;
    class ADERDG2CartesianCellsVTUAscii;
    class ADERDG2CartesianCellsVTUBinary;

    class Slicer; // external forward decl, #include exahype/plotters/slicing/Slicer.h
  }
}

/**
 * Common VTK class. Usually not used directly but through one of the subclasses.
 */
class exahype::plotters::ADERDG2CartesianVTK: public exahype::plotters::Plotter::Device {
 protected:
   enum class PlotterType {
     BinaryVTK,
     ASCIIVTK,
     BinaryVTU,
     ASCIIVTU
   };
 private:
  int           _fileCounter;
  const PlotterType _plotterType;
  const bool    _plotCells;
  std::string   _filename;
  int           _order;
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


  exahype::plotters::Slicer *slicer;
  static tarch::logging::Log _log;

  tarch::plotter::griddata::Writer::VertexDataWriter*  _vertexDataWriter;
  tarch::plotter::griddata::Writer::CellDataWriter*    _cellDataWriter;
  tarch::plotter::griddata::Writer::VertexDataWriter*  _vertexTimeStampDataWriter;
  tarch::plotter::griddata::Writer::CellDataWriter*    _cellTimeStampDataWriter;

  tarch::plotter::griddata::blockstructured::PatchWriter::SinglePatchWriter* _gridWriter;
  tarch::plotter::griddata::blockstructured::PatchWriterUnstructured*        _patchWriter;

  void writeTimeStampDataToPatch( double timeStamp, int vertexIndex, int cellIndex );

  void plotVertexData(
    int firstVertexIndex,
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    double* u,
    double timeStamp
  );

  void plotCellData(
    int firstCellIndex,
    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
    double* u,
    double timeStamp
  );
 public:
  ADERDG2CartesianVTK(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing, PlotterType plotterType, bool plotCells);
  virtual ~ADERDG2CartesianVTK();

  virtual void init(const std::string& filename, int orderPlusOne, int solverUnknowns, int writtenUnknowns, const std::string& select);

  void plotPatch(const int cellDescriptionsIndex, const int element) override;

  void plotPatch(
      const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
      const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch, double* u,
      double timeStamp);

  virtual void startPlotting( double time );
  virtual void finishPlotting();
};


class exahype::plotters::ADERDG2CartesianVerticesVTKAscii: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianVerticesVTKAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianVerticesVTKBinary: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianVerticesVTKBinary(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianCellsVTKAscii: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianCellsVTKAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianCellsVTKBinary: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianCellsVTKBinary(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianVerticesVTUAscii: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianVerticesVTUAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianVerticesVTUBinary: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianVerticesVTUBinary(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianCellsVTUAscii: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianCellsVTUAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianCellsVTUBinary: public exahype::plotters::ADERDG2CartesianVTK {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianCellsVTUBinary(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};

#endif
