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
 
#include "exahype/plotters/Plotter.h"

#include "exahype/plotters/VTK/ADERDG2CartesianVTK.h"
#include "exahype/plotters/ADERDG2CartesianPeanoPatchFileFormat.h"
#include "exahype/plotters/ADERDG2LegendrePeanoPatchFileFormat.h"
#include "exahype/plotters/VTK/ADERDG2LegendreVTK.h"
#include "exahype/plotters/CSV/ADERDG2LegendreCSV.h"
#include "exahype/plotters/VTK/ADERDG2LegendreDivergenceVTK.h"
#include "exahype/plotters/ADERDG2ProbeAscii.h"

#include "exahype/plotters/CarpetHDF5/ADERDG2CarpetHDF5.h"
#include "exahype/plotters/CarpetHDF5/FiniteVolume2CarpetHDF5.h"
#include "exahype/plotters/FlashHDF5/ADERDG2FlashHDF5.h"
#include "exahype/plotters/VTK/FiniteVolumes2VTK.h"
#include "exahype/plotters/VTK/LimitingADERDG2CartesianVTK.h"
#include "exahype/plotters/VTK/LimitingADERDGSubcells2CartesianVTK.h"
#include "exahype/solvers/LimitingADERDGSolver.h"

/* BEGIN Case intensitive string comparison: http://stackoverflow.com/a/23944175 */
bool icompare_pred(unsigned char a, unsigned char b) {
	return std::tolower(a) == std::tolower(b);
}

bool equalsIgnoreCase(std::string const& a, std::string const& b) {
    if (a.length()==b.length()) {
        return std::equal(b.begin(), b.end(),
                           a.begin(), icompare_pred);
    }
    else {
        return false;
    }
}
/* END Case intensitive string comparison: http://stackoverflow.com/a/23944175 */

std::vector<exahype::plotters::Plotter*> exahype::plotters::RegisteredPlotters;

tarch::logging::Log exahype::plotters::Plotter::_log( "exahype::plotters::Plotter" );

exahype::plotters::Plotter::Plotter(
        const int solverConfig,const int plotterConfig,
        const exahype::Parser& parser,
        Device* device) :
        _solver(solverConfig),
        _identifier(parser.getIdentifierForPlotter(solverConfig, plotterConfig)),
        _writtenUnknowns(parser.getUnknownsForPlotter(solverConfig, plotterConfig)),
        _time(parser.getFirstSnapshotTimeForPlotter(solverConfig, plotterConfig)),
        _solverTimeStamp(-std::numeric_limits<double>::max()),
        _repeat(parser.getRepeatTimeForPlotter(solverConfig, plotterConfig)),
        _filename(parser.getFilenameForPlotter(solverConfig, plotterConfig)),
        _select(parser.getSelectorForPlotter(solverConfig, plotterConfig)),
        _isActive(false),
        _device(device) {
  if (_time < 0.0) {
    logError("Plotter(...)",
        "plotter's first snapshot time is set to negative value "
        << _time << ". Plotter configuration=" << toString() );
  }
  if (_repeat < 0.0) {
    logError("Plotter(...)", "plotter's repeat time is set to negative value "
        << _repeat << ". Plotter configuration=" << toString() );
  }
  logInfo("Plotter(...)", "write snapshot to file "
      << _filename << " every " << _repeat
      << " time units with first snapshot at " << _time
      << ". plotter type is " << _identifier << ". Plotter configuration=" << toString() );

  if (  _writtenUnknowns < 0) {
    logError("Plotter(...)", "plotter's field 'variables' was assigned the negative integer "
        << _writtenUnknowns );
  }

  assertion(_solver < static_cast<int>(solvers::RegisteredSolvers.size()));

  if (_device!=nullptr) {
    _device->init(
        _filename,
        solvers::RegisteredSolvers[_solver]->getNodesPerCoordinateAxis(),
        solvers::RegisteredSolvers[_solver]->getNumberOfVariables()+solvers::RegisteredSolvers[_solver]->getNumberOfParameters(),
        _writtenUnknowns,
        _select
    );
  }
  else if (_identifier=="notoken") {
    logError(
      "Plotter(...)",
      "unable to set up " << (plotterConfig+1) << "th plotter for the "
      << (_solver+1) << "th solverNumber. Ensure number of plot sections "
      << "equals number of plotters originally passed to toolkit and "
      << "validate that plot syntax is correct"
    );
  }
  else {
    logError(
      "Plotter(...)",
      "unknown plotter type "
          << _identifier << " for "
          << solvers::RegisteredSolvers[_solver]->getIdentifier()
    << ". Potential reasons: you have not specified a valid identifier following the plot keyword or you have specified a plotter in the ExaHyPE toolkit and later removed this plotter from the config"
    );
  }
}


exahype::plotters::Plotter::Plotter(
        const int solverConfig,const int plotterConfig,
        const exahype::Parser& parser, UserOnTheFlyPostProcessing* postProcessing)
    : _solver(solverConfig),
      _identifier(parser.getIdentifierForPlotter(solverConfig, plotterConfig)),
      _writtenUnknowns(parser.getUnknownsForPlotter(solverConfig, plotterConfig)),
      _time(parser.getFirstSnapshotTimeForPlotter(solverConfig, plotterConfig)),
      _solverTimeStamp(-std::numeric_limits<double>::max()),
      _repeat(parser.getRepeatTimeForPlotter(solverConfig, plotterConfig)),
      _filename(parser.getFilenameForPlotter(solverConfig, plotterConfig)),
      _select(parser.getSelectorForPlotter(solverConfig, plotterConfig)),
      _isActive(false),
      _device(nullptr) {
  if (_time < 0.0) {
    logError("Plotter(...)",
      "plotter's first snapshot time is set to negative value "
      << _time << ". Plotter configuration=" << toString() );
  }
  if (_repeat < 0.0) {
    logError("Plotter(...)", "plotter's repeat time is set to negative value "
      << _repeat << ". Plotter configuration=" << toString() );
  }
  logInfo("Plotter(...)", "write snapshot to file "
    << _filename << " every " << _repeat
    << " time units with first snapshot at " << _time
    << ". plotter type is " << _identifier << ". Plotter configuration=" << toString() );

  if (  _writtenUnknowns < 0) {
      logError("Plotter(...)", "plotter's field 'variables' was assigned negative integer "
        << _writtenUnknowns);
  }

  assertion(_solver < static_cast<int>(solvers::RegisteredSolvers.size()));

  exahype::solvers::Solver::Type solvertype = solvers::RegisteredSolvers[_solver]->getType();
  switch (solvertype) {
    case exahype::solvers::Solver::Type::ADERDG:
      /**
       * This is actually some kind of switch expression though switches do
       * not work for strings, so we map it onto an if-then-else cascade.
       */
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsPeanoFileFormatAscii::getIdentifier())) {
        _device = new ADERDG2CartesianCellsPeanoFileFormatAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesPeanoFileFormatAscii::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesPeanoFileFormatAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsPeanoFileFormatHDF5::getIdentifier())) {
        _device = new ADERDG2CartesianCellsPeanoFileFormatHDF5(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesPeanoFileFormatHDF5::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesPeanoFileFormatHDF5(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsPeanoFileFormatAscii::getIdentifier())) {
        _device = new ADERDG2LegendreCellsPeanoFileFormatAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesPeanoFileFormatAscii::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesPeanoFileFormatAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsPeanoFileFormatHDF5::getIdentifier())) {
        _device = new ADERDG2LegendreCellsPeanoFileFormatHDF5(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesPeanoFileFormatHDF5::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesPeanoFileFormatHDF5(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTKAscii::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTKBinary::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTKBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTKAscii::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTKBinary::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTKBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTKAscii::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTKBinary::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTKBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTKAscii::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTKBinary::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTKBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTUAscii::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTUBinary::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTUBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTUAscii::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTUBinary::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTUBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTUAscii::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTUBinary::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTUBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTUAscii::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTUBinary::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTUBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTKAscii::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTKBinary::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTKBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTUAscii::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTUBinary::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTUBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2ProbeAscii::getIdentifier())) {
        _device = new ADERDG2ProbeAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCSV::getIdentifier())) {
        _device = new ADERDG2LegendreCSV(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CarpetHDF5::getIdentifier())) {
        _device = new ADERDG2CarpetHDF5(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2FlashHDF5::getIdentifier())) {
        _device = new ADERDG2FlashHDF5(postProcessing);
      }
    break;
    case exahype::solvers::Solver::Type::FiniteVolumes:
      /**
       * This is actually some kind of switch expression though switches do
       * not work for strings, so we map it onto an if-then-else cascade.
       */
      if (equalsIgnoreCase(_identifier, FiniteVolumesCells2VTKAscii::getIdentifier())) {
        _device = new FiniteVolumesCells2VTKAscii(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, FiniteVolumesCells2VTKBinary::getIdentifier())) {
        _device = new FiniteVolumesCells2VTKBinary(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, FiniteVolumesCells2VTUAscii::getIdentifier())) {
        _device = new FiniteVolumesCells2VTUAscii(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, FiniteVolumesCells2VTUBinary::getIdentifier())) {
        _device = new FiniteVolumesCells2VTUBinary(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (_identifier.compare( FiniteVolumesVertices2VTKAscii::getIdentifier() ) == 0) {
        _device = new FiniteVolumesVertices2VTKAscii(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (_identifier.compare( FiniteVolumesVertices2VTKBinary::getIdentifier() ) == 0) {
        _device = new FiniteVolumesVertices2VTKBinary(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (_identifier.compare( FiniteVolumesVertices2VTUAscii::getIdentifier() ) == 0) {
        _device = new FiniteVolumesVertices2VTUAscii(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (_identifier.compare( FiniteVolumesVertices2VTUBinary::getIdentifier() ) == 0) {
        _device = new FiniteVolumesVertices2VTUBinary(
            postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CarpetHDF5::getIdentifier())) {
        _device = new FiniteVolume2CarpetHDF5(
	    postProcessing,static_cast<exahype::solvers::FiniteVolumesSolver*>(
                solvers::RegisteredSolvers[_solver])->getGhostLayerWidth()
	);
      }
    break;
    case exahype::solvers::Solver::Type::LimitingADERDG:
      /**
       * Plotters for any ADER-DG scheme.
       *
       * This is actually some kind of switch expression though switches do
       * not work for strings, so we map it onto an if-then-else cascade.
       */
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsPeanoFileFormatAscii::getIdentifier())) {
        _device = new ADERDG2CartesianCellsPeanoFileFormatAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesPeanoFileFormatAscii::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesPeanoFileFormatAscii(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTKAscii::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTKBinary::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTKBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTKAscii::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTKBinary::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTKBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTKAscii::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTKBinary::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTKBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTKAscii::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTKBinary::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTKBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTUAscii::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianVerticesVTUBinary::getIdentifier())) {
        _device = new ADERDG2CartesianVerticesVTUBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTUAscii::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CartesianCellsVTUBinary::getIdentifier())) {
        _device = new ADERDG2CartesianCellsVTUBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTUAscii::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreVerticesVTUBinary::getIdentifier())) {
        _device = new ADERDG2LegendreVerticesVTUBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTUAscii::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCellsVTUBinary::getIdentifier())) {
        _device = new ADERDG2LegendreCellsVTUBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTKAscii::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTKAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTKBinary::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTKBinary(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTUAscii::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTUAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreDivergenceVerticesVTUBinary::getIdentifier())) {
        _device = new ADERDG2LegendreDivergenceVerticesVTUBinary(postProcessing);
      }

      if (equalsIgnoreCase(_identifier, ADERDG2ProbeAscii::getIdentifier())) {
        _device = new ADERDG2ProbeAscii(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2LegendreCSV::getIdentifier())) {
        _device = new ADERDG2LegendreCSV(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2CarpetHDF5::getIdentifier())) {
        _device = new ADERDG2CarpetHDF5(postProcessing);
      }
      if (equalsIgnoreCase(_identifier, ADERDG2FlashHDF5::getIdentifier())) {
        _device = new ADERDG2FlashHDF5(postProcessing);
      }

      /**
       * Plotters specifically for the limiting ADER-DG scheme.
       *
       * This is actually some kind of switch expression though switches do
       * not work for strings, so we map it onto an if-then-else cascade.
       */
      if (equalsIgnoreCase(_identifier, LimitingADERDG2CartesianVerticesVTKAscii::getIdentifier())) {
        _device = new LimitingADERDG2CartesianVerticesVTKAscii(
            postProcessing,static_cast<exahype::solvers::LimitingADERDGSolver*>(
                solvers::RegisteredSolvers[_solver])->getLimiter()->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, LimitingADERDG2CartesianVerticesVTKBinary::getIdentifier())) {
        _device = new LimitingADERDG2CartesianVerticesVTKBinary(
            postProcessing,static_cast<exahype::solvers::LimitingADERDGSolver*>(
            solvers::RegisteredSolvers[_solver])->getLimiter()->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, LimitingADERDG2CartesianCellsVTKAscii::getIdentifier())) {
        _device = new LimitingADERDG2CartesianCellsVTKAscii(
            postProcessing,static_cast<exahype::solvers::LimitingADERDGSolver*>(
                solvers::RegisteredSolvers[_solver])->getLimiter()->getGhostLayerWidth());
      }
      if (equalsIgnoreCase(_identifier, LimitingADERDG2CartesianCellsVTKBinary::getIdentifier())) {
        _device = new LimitingADERDG2CartesianCellsVTKBinary(
            postProcessing,static_cast<exahype::solvers::LimitingADERDGSolver*>(
                solvers::RegisteredSolvers[_solver])->getLimiter()->getGhostLayerWidth());
      }

      if (equalsIgnoreCase( _identifier, LimitingADERDGSubcells2CartesianCellsVTKAscii::getIdentifier() )) {
        _device = new LimitingADERDGSubcells2CartesianCellsVTKAscii(
            postProcessing,static_cast<exahype::solvers::LimitingADERDGSolver*>(
                solvers::RegisteredSolvers[_solver])->getLimiter()->getGhostLayerWidth());
      }
      if (equalsIgnoreCase( _identifier, LimitingADERDGSubcells2CartesianCellsVTKBinary::getIdentifier() )) {
        _device = new LimitingADERDGSubcells2CartesianCellsVTKBinary(
            postProcessing,static_cast<exahype::solvers::LimitingADERDGSolver*>(
                solvers::RegisteredSolvers[_solver])->getLimiter()->getGhostLayerWidth());
      }
    break;
  }

  if (_device!=nullptr) {
    _device->init(
        _filename,
        solvers::RegisteredSolvers[_solver]->getNodesPerCoordinateAxis(),
        solvers::RegisteredSolvers[_solver]->getNumberOfVariables()+solvers::RegisteredSolvers[_solver]->getNumberOfParameters(),
        _writtenUnknowns,
        _select
    );
  }
  else if (_identifier=="notoken") {
    logError(
      "Plotter(...)",
      "unable to set up " << (plotterConfig+1) << "th plotter for the "
      << (_solver+1) << "th solverNumber. Ensure number of plot sections "
      << "equals number of plotters originally passed to toolkit and "
      << "validate that plot syntax is correct"
    );
  }
  else {
    logError(
      "Plotter(...)",
      "unknown plotter type "
          << _identifier << " for "
          << solvers::RegisteredSolvers[_solver]->getIdentifier()
	  << ". Potential reasons: you have not specified a valid identifier following the plot keyword or you have specified a plotter in the ExaHyPE toolkit and later removed this plotter from the config"
    );
  }
}

exahype::plotters::Plotter::Plotter(
    const int solverConfig,const int plotterConfig,
    const exahype::Parser& parser, UserOnTheFlyPostProcessing* postProcessing,
    const int solverDataSource)
: exahype::plotters::Plotter::Plotter(solverConfig,plotterConfig,parser,postProcessing) {
  _solver   = solverDataSource;
  _filename = _filename + "_" + std::to_string(solverDataSource);

  // TODO(Dominic): Looks like a hack. Clean.

  if (_device!=nullptr) {
    _device->init(
        _filename,
        solvers::RegisteredSolvers[_solver]->getNodesPerCoordinateAxis(),
        solvers::RegisteredSolvers[_solver]->getNumberOfVariables()+solvers::RegisteredSolvers[_solver]->getNumberOfParameters(),
        _writtenUnknowns,
        _select
    );
  }
}

std::string exahype::plotters::Plotter::toString() const {
  std::ostringstream msg;

  msg << "(solver no=" << _solver
      << ",plotter identifier (type)=" << _identifier
      << ",written unknowns=" << _writtenUnknowns
      << ",time=" << _time
      << ",repeat=" << _repeat
      << ",file name=" << _filename
      << ",select statement=" << _select
      << ",device configured=" << (_device!=nullptr)
      << ")";

  return msg.str();
}


exahype::plotters::Plotter::~Plotter() {
  if (_device!=nullptr) {
    delete _device;
    _device = nullptr;
  }
}


double exahype::plotters::Plotter::getNextPlotTime() const {
  return _time;
}


bool exahype::plotters::Plotter::checkWetherPlotterBecomesActiveAndStartPlottingIfActive(double currentTimeStamp) {
  if ((_time >= 0.0) && tarch::la::greaterEquals(currentTimeStamp, _time)) {
    _solverTimeStamp = currentTimeStamp;
    
    if (_device==nullptr){
      logError(
        "checkWetherPlotterBecomesActiveAndStartPlottingIfActive(double)",
        "unknown plotter type " << _identifier << " piping into file " << _filename
      );
    }
    else {
      assertion(_device!=nullptr);
      _isActive = true;
      _device->startPlotting(currentTimeStamp);
    }
  } else {
    _solverTimeStamp = -std::numeric_limits<double>::max();
  }

  logDebug(
    "checkWetherPlotterBecomesActiveAndStartPlottingIfActive(double)",
    "plotter="<< _identifier <<
    ", active=" << ( isActive() ? "yes" : "no" ) <<
    ", plotter time=" << _time <<
    ", solver time=" << currentTimeStamp <<
    ", device=" << ( (_device==nullptr) ? "null" : "initialised" )
  );

  return isActive();
}


bool exahype::plotters::Plotter::isActive() const {
  return _isActive;
}


bool exahype::plotters::Plotter::plotDataFromSolver(int solver) const {
  return isActive() && _solver == solver;
}


void exahype::plotters::Plotter::plotPatch(
  const int cellDescriptionsIndex,
  const int element) {
  assertion(_device != nullptr);
  if (_device!=nullptr) {
    _device->plotPatch(cellDescriptionsIndex,element);
  }
}

void exahype::plotters::Plotter::finishedPlotting() {
  assertion(isActive());
  if (_repeat > 0.0) {
    while (_time <= _solverTimeStamp) {
      _time += _repeat;
    }
  } else {
    _time = -1.0;
  }
  if (_device!=nullptr) {
    _device->finishPlotting();
  }
  _isActive = false;
}


bool exahype::plotters::startPlottingIfAPlotterIsActive(double currentTimeStamp) {
  bool result = false;
  for (const auto& p : RegisteredPlotters) {
    result |= p->checkWetherPlotterBecomesActiveAndStartPlottingIfActive(currentTimeStamp);
  }
  return result;
}


double exahype::plotters::getTimeOfNextPlot() {
  double result = std::numeric_limits<double>::max();
  for (const auto& p : RegisteredPlotters) {
    result = std::min(result,p->getNextPlotTime());
  }
  return result;
}


void exahype::plotters::finishedPlotting() {
  for (auto& p : RegisteredPlotters) {
    if (p->isActive()) {
      p->finishedPlotting();
    }
  }
}


std::string exahype::plotters::Plotter::getFileName() const {
  return _filename;
}

#ifdef Parallel
void exahype::plotters::Plotter::sendDataToWorker(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> plotterDataToSend(0,1);
  plotterDataToSend.push_back(_time);
  assertion1(plotterDataToSend.size()==1,plotterDataToSend.size());
  assertion1(std::isfinite(plotterDataToSend[0]),plotterDataToSend[0]);

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("sendDataToWorker(...)","Broadcasting plotter data: " <<
        " data[0]=" << plotterDataToSend[0]);
    logDebug("sendDataWorker(...)","_time="<<_time);
  }

  DataHeap::getInstance().sendData(
      plotterDataToSend.data(), plotterDataToSend.size(),
      workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::plotters::Plotter::mergeWithMasterData(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> receivedPlotterData(1);
  DataHeap::getInstance().receiveData(
      receivedPlotterData.data(),receivedPlotterData.size(),masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
  assertion1(receivedPlotterData.size()==1,receivedPlotterData.size());

  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithMasterData(...)","Received plotter data: " <<
        "data[0]="  << receivedPlotterData[0]);
  }

  _time = receivedPlotterData[0];
}
#endif
