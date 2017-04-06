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
 
#ifndef _EXAHYPE_PLOTTERS_ADERDG_2_CARTESIAN_PEANO_FILE_FORMAT_H_
#define _EXAHYPE_PLOTTERS_ADERDG_2_CARTESIAN_PEANO_FILE_FORMAT_H_

#include "exahype/plotters/Plotter.h"

#include "tarch/plotter/griddata/blockstructured/PeanoPatchFileWriter.h"

namespace exahype {
  namespace plotters {
    class ADERDG2CartesianPeanoFileFormat;

    class ADERDG2CartesianCellsPeanoFileFormatAscii;
    class ADERDG2CartesianVerticesPeanoFileFormatAscii;
  }
}

/**
 * Common VTK class. Usually not used directly but through one of the subclasses.
 */
class exahype::plotters::ADERDG2CartesianPeanoFileFormat: public exahype::plotters::Plotter::Device {
/*
 protected:
   enum class PlotterType {
     BinaryVTK,
     ASCIIVTK,
     BinaryVTU,
     ASCIIVTU
   };
*/
 private:
  int           _fileCounter;
  const bool    _plotCells;
  std::string   _filename;
  int           _order;
  int           _solverUnknowns;
  int           _writtenUnknowns;
  std::string   _select;

  /**
   * To memorise the time argument from startPlotter(). We need it when we close the plotter for the time series.
   */
  double _time;


  tarch::la::Vector<DIMENSIONS, double>  _regionOfInterestLeftBottomFront;
  tarch::la::Vector<DIMENSIONS, double>  _regionOfInterestRightTopBack;

  tarch::plotter::griddata::Writer::VertexDataWriter*  _vertexDataWriter;
  tarch::plotter::griddata::Writer::CellDataWriter*    _cellDataWriter;
  tarch::plotter::griddata::Writer::VertexDataWriter*  _vertexTimeStampDataWriter;
  tarch::plotter::griddata::Writer::CellDataWriter*    _cellTimeStampDataWriter;

  tarch::plotter::griddata::blockstructured::PeanoPatchFileWriter* _writer;

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
  ADERDG2CartesianPeanoFileFormat(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing, bool plotCells);
  virtual ~ADERDG2CartesianPeanoFileFormat();

  virtual void init(const std::string& filename, int orderPlusOne, int solverUnknowns, int writtenUnknowns, const std::string& select);

  void plotPatch(const int cellDescriptionsIndex, const int element) override;

  void plotPatch(
      const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
      const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch, double* u,
      double timeStamp);

  virtual void startPlotting( double time );
  virtual void finishPlotting();
};


class exahype::plotters::ADERDG2CartesianVerticesPeanoFileFormatAscii: public exahype::plotters::ADERDG2CartesianPeanoFileFormat {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianVerticesPeanoFileFormatAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


class exahype::plotters::ADERDG2CartesianCellsPeanoFileFormatAscii: public exahype::plotters::ADERDG2CartesianPeanoFileFormat {
  public:
    static std::string getIdentifier();
    ADERDG2CartesianCellsPeanoFileFormatAscii(exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing);
};


#endif
