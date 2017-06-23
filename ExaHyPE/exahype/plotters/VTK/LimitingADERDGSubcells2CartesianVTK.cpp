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

#include "LimitingADERDGSubcells2CartesianVTK.h"
#include "tarch/parallel/Node.h"

// @todo 16/05/03:Dominic Etienne Charreir Plotter depends now on kernels.
// Should thus be placed in kernel module or the solver
// should provide a function that computes solution values
// at equidistant grid points
#include "kernels/DGMatrices.h"
#include "peano/utils/Loop.h"


#include "tarch/plotter/griddata/unstructured/vtk/VTKTextFileWriter.h"
#include "tarch/plotter/griddata/unstructured/vtk/VTKBinaryFileWriter.h"


#include "kernels/DGBasisFunctions.h"

#include "exahype/solvers/LimitingADERDGSolver.h"

std::string exahype::plotters::LimitingADERDGSubcells2CartesianCellsVTKAscii::getIdentifier() {
  return "vtk::Cartesian::subcells::limited::ascii";
}


exahype::plotters::LimitingADERDGSubcells2CartesianCellsVTKAscii::LimitingADERDGSubcells2CartesianCellsVTKAscii(
    exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,
    const int ghostLayerWidth):
    LimitingADERDGSubcells2CartesianVTK(postProcessing,ghostLayerWidth,false) {
}


std::string exahype::plotters::LimitingADERDGSubcells2CartesianCellsVTKBinary::getIdentifier() {
 return "vtk::Cartesian::subcells::limited::binary";
}


exahype::plotters::LimitingADERDGSubcells2CartesianCellsVTKBinary::LimitingADERDGSubcells2CartesianCellsVTKBinary(
    exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,
    const int ghostLayerWidth):
    LimitingADERDGSubcells2CartesianVTK(postProcessing,ghostLayerWidth,true) {
}

tarch::logging::Log exahype::plotters::LimitingADERDGSubcells2CartesianVTK::_log("exahype::plotters::LimitingADERDGSubcells2CartesianVTK");

exahype::plotters::LimitingADERDGSubcells2CartesianVTK::LimitingADERDGSubcells2CartesianVTK(
    exahype::plotters::Plotter::UserOnTheFlyPostProcessing* postProcessing,
    const int ghostLayerWidth,
    const bool isBinary)
  :
  Device(postProcessing),
  _fileCounter(-1),
  _isBinary(isBinary),
  _order(-1),
  _solverUnknowns(-1),
  _writtenUnknowns(-1),
  _ghostLayerWidth(ghostLayerWidth),
  _gridWriter(nullptr),
  _patchWriter(nullptr),
  _vertexDataWriter(nullptr),
  _cellDataWriter(nullptr),
  _timeStampCellDataWriter(nullptr),
  _cellLimiterStatusWriter(nullptr),
  _cellPreviousLimiterStatusWriter(nullptr)
{}


void exahype::plotters::LimitingADERDGSubcells2CartesianVTK::init(
  const std::string& filename,
  int                orderPlusOne,
  int                unknowns,
  int                writtenUnknowns,
  const std::string& select
) {
  _filename          = filename;
  _order             = orderPlusOne-1;
  _solverUnknowns    = unknowns;
  _select            = select;
  _patchWriter       = nullptr;
  _writtenUnknowns   = writtenUnknowns;

  double x;
  x = Parser::getValueFromPropertyString( select, "left" );
  _regionOfInterestLeftBottomFront(0) = x!=x ? -std::numeric_limits<double>::max() : x; // "-", min
  x = Parser::getValueFromPropertyString( select, "bottom" );
  _regionOfInterestLeftBottomFront(1) = x!=x ? -std::numeric_limits<double>::max() : x; // "-", min
#if DIMENSIONS==3
  x = Parser::getValueFromPropertyString( select, "front" );
  _regionOfInterestLeftBottomFront(2) = x!=x ? -std::numeric_limits<double>::max() : x; // "-", min
#endif


  x = Parser::getValueFromPropertyString( select, "right" );
  _regionOfInterestRightTopBack(0) = x!=x ? std::numeric_limits<double>::max() : x;
  x = Parser::getValueFromPropertyString( select, "top" );
  _regionOfInterestRightTopBack(1) = x!=x ? std::numeric_limits<double>::max() : x;
#if DIMENSIONS==3
  x = Parser::getValueFromPropertyString( select, "back" );
  _regionOfInterestRightTopBack(2) = x!=x ? std::numeric_limits<double>::max() : x;
#endif
}


void exahype::plotters::LimitingADERDGSubcells2CartesianVTK::startPlotting( double time ) {
  _fileCounter++;

  assertion( _patchWriter==nullptr );

  if (_writtenUnknowns>0) {
    if (_isBinary) {
      _patchWriter =
        new tarch::plotter::griddata::blockstructured::PatchWriterUnstructured(
          new tarch::plotter::griddata::unstructured::vtk::VTKBinaryFileWriter());
    }
    else {
      _patchWriter =
        new tarch::plotter::griddata::blockstructured::PatchWriterUnstructured(
          new tarch::plotter::griddata::unstructured::vtk::VTKTextFileWriter());
    }

    _gridWriter                = _patchWriter->createSinglePatchWriter();

    _cellDataWriter            = _patchWriter->createCellDataWriter("Q", _writtenUnknowns);
    _vertexDataWriter          = nullptr;

    _cellLimiterStatusWriter   = _patchWriter->createCellDataWriter("Limiter-Status(0-O,1..2-DG,3..4-FV,5-T)", 1);
    _cellPreviousLimiterStatusWriter   = _patchWriter->createCellDataWriter("Previous-Limiter-Status(0-O,1..2-DG,3..4-FV,5-T)", 1);

    _timeStampCellDataWriter   = _patchWriter->createCellDataWriter("time", 1);

    assertion( _patchWriter!=nullptr );
    assertion( _gridWriter!=nullptr );
    assertion( _timeStampCellDataWriter!=nullptr );
  }

  _postProcessing->startPlotting( time );
}


void exahype::plotters::LimitingADERDGSubcells2CartesianVTK::finishPlotting() {
  _postProcessing->finishPlotting();

  if (_writtenUnknowns>0) {
    assertion( _patchWriter!=nullptr );
    assertion( _gridWriter!=nullptr );
    assertion( _timeStampCellDataWriter!=nullptr );

    _gridWriter->close();
//    if (_timeStampCellDataWriter!=nullptr) _timeStampCellDataWriter->close();
    if (_vertexDataWriter!=nullptr)                  _vertexDataWriter->close();
    if (_cellDataWriter!=nullptr)                    _cellDataWriter->close();
    if (_cellLimiterStatusWriter!=nullptr)           _cellLimiterStatusWriter->close();
    if (_cellPreviousLimiterStatusWriter!=nullptr)   _cellPreviousLimiterStatusWriter->close();
    _timeStampCellDataWriter->close();

    std::ostringstream snapshotFileName;
    snapshotFileName << _filename
                     << "-" << _fileCounter;

    const bool hasBeenSuccessful =
      _patchWriter->writeToFile(snapshotFileName.str());
    if (!hasBeenSuccessful) {
      exit(-1);
    }
  }

  if (_vertexDataWriter!=nullptr)          delete _vertexDataWriter;
  if (_cellDataWriter!=nullptr)            delete _cellDataWriter;
  if (_timeStampCellDataWriter!=nullptr)   delete _timeStampCellDataWriter;
  if (_cellLimiterStatusWriter!=nullptr)   delete _cellLimiterStatusWriter;
  if (_cellPreviousLimiterStatusWriter!=nullptr)   delete _cellPreviousLimiterStatusWriter;
  if (_gridWriter!=nullptr)                delete _gridWriter;
  if (_patchWriter!=nullptr)               delete _patchWriter;

  _vertexDataWriter                  = nullptr;
  _cellDataWriter                    = nullptr;
  _patchWriter                       = nullptr;
  _timeStampCellDataWriter           = nullptr;
  _cellLimiterStatusWriter           = nullptr;
  _cellPreviousLimiterStatusWriter   = nullptr;
  _gridWriter                = nullptr;
}



exahype::plotters::LimitingADERDGSubcells2CartesianVTK::~LimitingADERDGSubcells2CartesianVTK() {
}

void exahype::plotters::LimitingADERDGSubcells2CartesianVTK::plotPatch(const int cellDescriptionsIndex, const int element) {
  auto& solverPatch = exahype::solvers::ADERDGSolver::getCellDescription(cellDescriptionsIndex,element);

  if (solverPatch.getType()==exahype::solvers::ADERDGSolver::CellDescription::Type::Cell) {
    typedef exahype::solvers::ADERDGSolver::CellDescription::LimiterStatus LimiterStatus;
    int limiterStatus         = solverPatch.getLimiterStatus();
    int previousLimiterStatus = solverPatch.getPreviousLimiterStatus();

    // ignore limiter status on coarser mesh levels
    assertion(static_cast<unsigned int>(solverPatch.getSolverNumber())
        <exahype::solvers::RegisteredSolvers.size());
    if (solverPatch.getLevel()
        <exahype::solvers::RegisteredSolvers[solverPatch.getSolverNumber()]->getMaximumAdaptiveMeshLevel()) {
      limiterStatus         = LimiterStatus::Ok;
      previousLimiterStatus = LimiterStatus::Ok;
    }

    switch(limiterStatus) {
      case LimiterStatus::Troubled:
      case LimiterStatus::NeighbourOfTroubled1:
      case LimiterStatus::NeighbourOfTroubled2: {
        const int limiterElement =
            static_cast<exahype::solvers::LimitingADERDGSolver*>(
                exahype::solvers::RegisteredSolvers[solverPatch.getSolverNumber()])->
                tryGetLimiterElementFromSolverElement(cellDescriptionsIndex,element);
        auto& limiterPatch =
            exahype::solvers::FiniteVolumesSolver::getCellDescription(cellDescriptionsIndex,limiterElement);

        double* limiterSolution = DataHeap::getInstance().getData(limiterPatch.getSolution()).data();
        plotFiniteVolumesPatch(
            limiterPatch.getOffset(),
            limiterPatch.getSize(), limiterSolution,
            limiterPatch.getTimeStamp(),
            limiterStatus,
            previousLimiterStatus);
      } break;
    }
  }
}

void exahype::plotters::LimitingADERDGSubcells2CartesianVTK::plotFiniteVolumesPatch(
  const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,
  const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,
  const double* const u,
  const double timeStamp,
  const int limiterStatus, const int previousLimiterStatus) {
  if (
    tarch::la::allSmaller(_regionOfInterestLeftBottomFront,offsetOfPatch+sizeOfPatch)
    &&
    tarch::la::allGreater(_regionOfInterestRightTopBack,offsetOfPatch)
  ) {
    logDebug("plotPatch(...)","offset of patch: "<<offsetOfPatch
    <<", size of patch: "<<sizeOfPatch
    <<", time stamp: "<<timeStamp);

    assertion( _writtenUnknowns==0 || _patchWriter!=nullptr );
    assertion( _writtenUnknowns==0 || _gridWriter!=nullptr );
    assertion( _writtenUnknowns==0 || _timeStampCellDataWriter!=nullptr );

    const int numberOfCellsPerAxis = 2*_order+1;

    int cellIndex = _writtenUnknowns==0 ? -1 : _gridWriter->plotPatch(offsetOfPatch, sizeOfPatch, numberOfCellsPerAxis).second;

    double* sourceValue = new double[_solverUnknowns];
    double* value       = _writtenUnknowns==0 ? nullptr : new double[_writtenUnknowns];

    dfor(i,numberOfCellsPerAxis+_ghostLayerWidth) {
      if (tarch::la::allSmaller(i,numberOfCellsPerAxis+_ghostLayerWidth)
          && tarch::la::allGreater(i,_ghostLayerWidth-1)) {
        if (_writtenUnknowns>0) {
          _timeStampCellDataWriter->plotCell(cellIndex, timeStamp);
          _cellLimiterStatusWriter->plotCell(cellIndex, limiterStatus);
          _cellPreviousLimiterStatusWriter->plotCell(cellIndex, previousLimiterStatus);
        }

        for (int unknown=0; unknown < _solverUnknowns; unknown++) {
          sourceValue[unknown] =
            u[peano::utils::dLinearisedWithoutLookup(i,numberOfCellsPerAxis+2*_ghostLayerWidth)*_solverUnknowns+unknown];
        } // !!! Be aware of the "2*_ghostLayerWidth" !!!

        assertion(sizeOfPatch(0)==sizeOfPatch(1));

        _postProcessing->mapQuantities(
          offsetOfPatch,
          sizeOfPatch,
          offsetOfPatch + (i-_ghostLayerWidth).convertScalar<double>()* (sizeOfPatch(0)/(numberOfCellsPerAxis)),
          i-_ghostLayerWidth,
          sourceValue,
          value,
          timeStamp
        );

        if (_writtenUnknowns>0) {
          _cellDataWriter->plotCell(cellIndex, value, _writtenUnknowns);
        }
        cellIndex++;
      }
    }

    delete[] sourceValue;
    delete[] value;
  }
}
