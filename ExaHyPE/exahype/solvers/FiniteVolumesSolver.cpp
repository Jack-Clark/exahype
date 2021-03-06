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

#include "exahype/solvers/FiniteVolumesSolver.h"

#include <string>
#include <limits>
#include <iomanip>

#include "exahype/Cell.h"
#include "exahype/Vertex.h"
#include "exahype/VertexOperations.h"

#include "peano/utils/Loop.h"

#include "tarch/multicore/Lock.h"

#include "exahype/amr/AdaptiveMeshRefinement.h"


namespace {
constexpr const char* tags[]{"solutionUpdate", "stableTimeStepSize"};
}  // namespace

tarch::logging::Log exahype::solvers::FiniteVolumesSolver::_log( "exahype::solvers::FiniteVolumesSolver");

exahype::solvers::FiniteVolumesSolver::FiniteVolumesSolver(
    const std::string& identifier, int numberOfVariables,
    int numberOfParameters, int nodesPerCoordinateAxis, int ghostLayerWidth,
    double maximumMeshSize, int maximumAdaptiveMeshDepth,
    exahype::solvers::Solver::TimeStepping timeStepping,
    std::unique_ptr<profilers::Profiler> profiler)
    : Solver(identifier, exahype::solvers::Solver::Type::FiniteVolumes,
             numberOfVariables, numberOfParameters, nodesPerCoordinateAxis,
             maximumMeshSize, maximumAdaptiveMeshDepth,
             timeStepping, std::move(profiler)),
            _previousMinTimeStepSize( std::numeric_limits<double>::max() ),
            _minTimeStamp( std::numeric_limits<double>::max() ),
            _minTimeStepSize( std::numeric_limits<double>::max() ),
            _minNextTimeStepSize( std::numeric_limits<double>::max() ),
            _ghostLayerWidth( ghostLayerWidth ),
            _dataPerPatch( (numberOfVariables+numberOfParameters) * power(nodesPerCoordinateAxis, DIMENSIONS + 0) ),
            _ghostDataPerPatch( (numberOfVariables+numberOfParameters) * power(nodesPerCoordinateAxis+2*ghostLayerWidth, DIMENSIONS + 0) - _dataPerPatch ),
            _dataPerPatchFace( _ghostLayerWidth*(numberOfVariables+numberOfParameters)*power(nodesPerCoordinateAxis, DIMENSIONS - 1) ),
            _dataPerPatchBoundary( DIMENSIONS_TIMES_TWO *_dataPerPatchFace) {
  assertion3(_dataPerPatch > 0, numberOfVariables, numberOfParameters,
             nodesPerCoordinateAxis);
  // register tags with profiler
  for (const char* tag : tags) {
    _profiler->registerTag(tag);
  }
}

int exahype::solvers::FiniteVolumesSolver::getUnknownsPerPatch() const {
  return _dataPerPatch;
}

int exahype::solvers::FiniteVolumesSolver::getGhostLayerWidth() const {
  return _ghostLayerWidth;
}

int exahype::solvers::FiniteVolumesSolver::getGhostValuesPerPatch() const {
  return _ghostDataPerPatch;
}

int exahype::solvers::FiniteVolumesSolver::getUnknownsPerFace() const {
  return _dataPerPatchFace;
}

double exahype::solvers::FiniteVolumesSolver::getPreviousMinTimeStepSize() const {
  return _previousMinTimeStepSize;
}

double exahype::solvers::FiniteVolumesSolver::getMinTimeStamp() const {
  return _minTimeStamp;
}

double exahype::solvers::FiniteVolumesSolver::getMinTimeStepSize() const {
  return _minTimeStepSize;
}

void exahype::solvers::FiniteVolumesSolver::updateMinNextTimeStepSize(
    double value) {
  _minNextTimeStepSize = std::min(_minNextTimeStepSize, value);
}

void exahype::solvers::FiniteVolumesSolver::initSolver(
    const double timeStamp,
    const tarch::la::Vector<DIMENSIONS,double>& domainOffset,
    const tarch::la::Vector<DIMENSIONS,double>& domainSize) {
  _domainOffset=domainOffset;
  _domainSize=domainSize;
  _coarsestMeshLevel =
      exahype::solvers::Solver::computeMeshLevel(_maximumMeshSize,domainSize[0]);

  _previousMinTimeStepSize = 0.0;
  _minTimeStepSize = 0.0;
  _minTimeStamp = timeStamp;

  _meshUpdateRequest = true;
}

bool exahype::solvers::FiniteVolumesSolver::isSending(
    const exahype::records::State::AlgorithmSection& section) const {
  return
      section==exahype::records::State::AlgorithmSection::TimeStepping ||
      section==exahype::records::State::AlgorithmSection::PredictionRerunAllSend ||
      section==exahype::records::State::AlgorithmSection::MeshRefinementAllSend ||
      section==exahype::records::State::AlgorithmSection::LocalRecomputationAllSend ||
      section==exahype::records::State::AlgorithmSection::GlobalRecomputationAllSend;
}

bool exahype::solvers::FiniteVolumesSolver::isComputing(
    const exahype::records::State::AlgorithmSection& section) const {
  return section==exahype::records::State::AlgorithmSection::TimeStepping;
}

void exahype::solvers::FiniteVolumesSolver::synchroniseTimeStepping(
    CellDescription& cellDescription) {
  switch (_timeStepping) {
    case TimeStepping::Global:
      cellDescription.setPreviousTimeStepSize(_previousMinTimeStepSize);;
      cellDescription.setTimeStamp(_minTimeStamp);
      cellDescription.setTimeStepSize(_minTimeStepSize);
      break;
    case TimeStepping::GlobalFixed:
      cellDescription.setPreviousTimeStepSize(_previousMinTimeStepSize);
      cellDescription.setTimeStamp(_minTimeStamp);
      cellDescription.setTimeStepSize(_minTimeStepSize);
      break;
  }
}

void exahype::solvers::FiniteVolumesSolver::synchroniseTimeStepping(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  synchroniseTimeStepping(cellDescription);
}

void exahype::solvers::FiniteVolumesSolver::startNewTimeStep() {
  switch (_timeStepping) {
    case TimeStepping::Global:
      _previousMinTimeStepSize  = _minTimeStepSize;
      _minTimeStamp            += _minTimeStepSize;
      _minTimeStepSize          = _minNextTimeStepSize;
      _minNextTimeStepSize      = std::numeric_limits<double>::max();
      break;
    case TimeStepping::GlobalFixed:
      _previousMinTimeStepSize  = _minTimeStepSize;
      _minTimeStamp            += _minTimeStepSize;
      _minTimeStepSize          = _minNextTimeStepSize;
      break;
  }

  _minCellSize     = _nextMinCellSize;
  _maxCellSize     = _nextMaxCellSize;
  _nextMinCellSize = std::numeric_limits<double>::max();
  _nextMaxCellSize = -std::numeric_limits<double>::max(); // "-", min
}

/**
 * Zero predictor and corrector time step size.
 */
void exahype::solvers::FiniteVolumesSolver::zeroTimeStepSizes() {
  _minTimeStepSize = 0;
  assertionEquals(_minNextTimeStepSize,std::numeric_limits<double>::max());
}

void exahype::solvers::FiniteVolumesSolver::rollbackToPreviousTimeStep() {
  switch (_timeStepping) {
    case TimeStepping::Global:
      _minTimeStamp            = _minTimeStamp-_previousMinTimeStepSize;
      _minTimeStepSize         = _previousMinTimeStepSize;

      _previousMinTimeStepSize = std::numeric_limits<double>::max();
      _minNextTimeStepSize     = std::numeric_limits<double>::max();
      break;
    case TimeStepping::GlobalFixed:
      _minTimeStamp             = _minTimeStamp-_previousMinTimeStepSize;
      _minTimeStepSize          = _previousMinTimeStepSize;
      break;
  }
}

void exahype::solvers::FiniteVolumesSolver::reinitialiseTimeStepData() {
  switch (_timeStepping) {
    case TimeStepping::Global:
      // do nothing
      break;
    case TimeStepping::GlobalFixed:
      // do nothing
      break;
  }
}

double exahype::solvers::FiniteVolumesSolver::getMinNextTimeStepSize() const {
  return _minNextTimeStepSize;
}

int exahype::solvers::FiniteVolumesSolver::tryGetElement(
    const int cellDescriptionsIndex,
    const int solverNumber) const {
  if (Heap::getInstance().isValidIndex(cellDescriptionsIndex)) {
    int element=0;
    for (auto& p : Heap::getInstance().getData(cellDescriptionsIndex)) {
      if (p.getSolverNumber()==solverNumber) {
        return element;
      }
      ++element;
    }
  }
  return NotFound;
}

exahype::solvers::Solver::SubcellPosition exahype::solvers::FiniteVolumesSolver::computeSubcellPositionOfCellOrAncestor(
        const int cellDescriptionsIndex,
        const int element) {
  // TODO(Dominic): Comment code in as soon as we have all the required fields
  // on the cell description.
//  CellDescription& cellDescription =
//      getCellDescription(cellDescriptionsIndex,element);
//
//  return
//      exahype::amr::computeSubcellPositionOfCellOrAncestor
//      <CellDescription,Heap>(cellDescription);
  SubcellPosition empty;
  return empty;
}

///////////////////////////////////
// MODIFY CELL DESCRIPTION
///////////////////////////////////
bool exahype::solvers::FiniteVolumesSolver::markForRefinement(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    const bool initialGrid,
    const int solverNumber) {
  // do nothing
  return false;
}

bool exahype::solvers::FiniteVolumesSolver::updateStateInEnterCell(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    const bool initialGrid,
    const int solverNumber) {
  // Fine grid cell based uniform mesh refinement.
  int fineGridCellElement =
      tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
  if (fineGridCellElement==exahype::solvers::Solver::NotFound &&
      tarch::la::allSmallerEquals(fineGridVerticesEnumerator.getCellSize(),getMaximumMeshSize()) &&
      tarch::la::allGreater(coarseGridVerticesEnumerator.getCellSize(),getMaximumMeshSize())) {
    addNewCell(fineGridCell,fineGridVertices,fineGridVerticesEnumerator,
               multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex,
               solverNumber);
    // Fine grid cell based adaptive mesh refinement operations are not implemented.
  } else if (fineGridCellElement!=exahype::solvers::Solver::NotFound) {
    // do nothing
  }

  return false;
}

void exahype::solvers::FiniteVolumesSolver::addNewCell(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    const int coarseGridCellDescriptionsIndex,
    const int solverNumber) {
  fineGridCell.addNewCellDescription(
              solverNumber,
              CellDescription::Cell,
              CellDescription::None,
              fineGridVerticesEnumerator.getLevel(),
              coarseGridCellDescriptionsIndex,
              fineGridVerticesEnumerator.getCellSize(),
              fineGridVerticesEnumerator.getVertexPosition());
  const int fineGridCellElement =
      tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);

  CellDescription& fineGridCellDescription =
      getCellDescription(fineGridCell.getCellDescriptionsIndex(),fineGridCellElement);
  ensureNecessaryMemoryIsAllocated(fineGridCellDescription);
  fineGridCellDescription.setIsInside(
      exahype::Cell::determineInsideAndOutsideFaces(
          fineGridVertices,fineGridVerticesEnumerator));
}

void exahype::solvers::FiniteVolumesSolver::addNewCellDescription(
    const int cellDescriptionsIndex,
    const int solverNumber,
    const exahype::records::FiniteVolumesCellDescription::Type cellType,
    const exahype::records::FiniteVolumesCellDescription::RefinementEvent refinementEvent,
    const int level,
    const int parentIndex,
    const tarch::la::Vector<DIMENSIONS, double>&  cellSize,
    const tarch::la::Vector<DIMENSIONS, double>&  cellOffset) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion2(static_cast<unsigned int>(solverNumber) < solvers::RegisteredSolvers.size(),
             solverNumber, exahype::solvers::RegisteredSolvers.size());

  CellDescription newCellDescription;
  newCellDescription.setSolverNumber(solverNumber);

  // Default AMR settings
  newCellDescription.setType(cellType);
  newCellDescription.setLevel(level);
  newCellDescription.setRefinementEvent(refinementEvent);
  // newCellDescription.setHelperCellNeedsToStoreFaceData(false); // TODO(Dominic): Add to FV cell descr.

  // Pass geometry information to the cellDescription description
  newCellDescription.setSize(cellSize);
  newCellDescription.setOffset(cellOffset);

  // Default field data indices
  newCellDescription.setSolution(-1);
  newCellDescription.setPreviousSolution(-1);

  Heap::getInstance().getData(cellDescriptionsIndex).push_back(newCellDescription);
}

void exahype::solvers::FiniteVolumesSolver::ensureNoUnnecessaryMemoryIsAllocated(CellDescription& cellDescription) {
  if (DataHeap::getInstance().isValidIndex(cellDescription.getSolution())) {
    switch (cellDescription.getType()) {
      case CellDescription::Erased: {
        waitUntilAllBackgroundTasksHaveTerminated();
        tarch::multicore::Lock lock(_heapSemaphore);

        assertion(DataHeap::getInstance().isValidIndex(cellDescription.getSolution()));
        assertion(DataHeap::getInstance().isValidIndex(cellDescription.getPreviousSolution()));

        DataHeap::getInstance().deleteData(cellDescription.getSolution());
        DataHeap::getInstance().deleteData(cellDescription.getPreviousSolution());

        cellDescription.setSolution(-1);
        cellDescription.setPreviousSolution(-1);
        } break;
      case CellDescription::Cell:
        // do nothing
        break;
      default:
        assertionMsg(false,"No other cell description types are supported at the moment!");
        break;
    }
  }
}

void exahype::solvers::FiniteVolumesSolver::ensureNecessaryMemoryIsAllocated(CellDescription& cellDescription) {
  switch (cellDescription.getType()) {
    case CellDescription::Cell:
      if (!DataHeap::getInstance().isValidIndex(cellDescription.getSolution())) {
        assertion(!DataHeap::getInstance().isValidIndex(cellDescription.getSolution()));
        // Allocate volume DoF for limiter
        const int size = _dataPerPatch+_ghostDataPerPatch;

        waitUntilAllBackgroundTasksHaveTerminated();
        tarch::multicore::Lock lock(_heapSemaphore);

        cellDescription.setSolution(DataHeap::getInstance().createData(size, size, DataHeap::Allocation::DoNotUseAnyRecycledEntry));
        cellDescription.setPreviousSolution(DataHeap::getInstance().createData(size, size, DataHeap::Allocation::DoNotUseAnyRecycledEntry));
      }
      break;
    case CellDescription::Erased:
      // do nothing
    default:
      assertionMsg(false,"No other cell description types are supported at the moment!");
      break;
  }
}

bool exahype::solvers::FiniteVolumesSolver::attainedStableState(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    const int solverNumber) const {
  //  const int element = tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
  //  if (element!=exahype::solvers::Solver::NotFound) {
  //    CellDescription& cellDescription = getCellDescription(fineGridCell.getCellDescriptionsIndex(),element);
  //
  //    return (cellDescription.getRefinementEvent()==CellDescription::RefinementEvent::None);
  //  }

  return true;
}

bool exahype::solvers::FiniteVolumesSolver::updateStateInLeaveCell(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    const int solverNumber) {
  // do nothing

  return false;
}

void exahype::solvers::FiniteVolumesSolver::finaliseStateUpdates(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const int solverNumber) {
  // do nothing
}

///////////////////////////////////
// CELL-LOCAL
//////////////////////////////////
bool exahype::solvers::FiniteVolumesSolver::evaluateRefinementCriterionAfterSolutionUpdate(
      const int cellDescriptionsIndex,
      const int element) {
  // TODO(Dominic): Not implemented.
  return false;
}

double exahype::solvers::FiniteVolumesSolver::startNewTimeStep(
    const int cellDescriptionsIndex,
    const int element,
    double*   tempEigenvalues) {
  CellDescription& p = getCellDescription(cellDescriptionsIndex,element);

  if (p.getType()==exahype::records::FiniteVolumesCellDescription::Cell) {
    //         assertion1(p.getRefinementEvent()==exahype::records::FiniteVolumesCellDescription::None,p.toString()); // todo
    double* solution = exahype::DataHeap::getInstance().getData(p.getSolution()).data();

    double admissibleTimeStepSize = stableTimeStepSize(
        solution, tempEigenvalues, p.getSize());

    assertion(!std::isnan(admissibleTimeStepSize));

    p.setPreviousTimeStepSize(p.getTimeStepSize());
    p.setTimeStamp(p.getTimeStamp()+p.getTimeStepSize());
    p.setTimeStepSize(admissibleTimeStepSize);

    return admissibleTimeStepSize;
  }

  return std::numeric_limits<double>::max();
}

void exahype::solvers::FiniteVolumesSolver::zeroTimeStepSizes(const int cellDescriptionsIndex, const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell) {
    cellDescription.setTimeStepSize(0.0);
  }
}

void exahype::solvers::FiniteVolumesSolver::rollbackToPreviousTimeStep(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  cellDescription.setTimeStamp(
      cellDescription.getTimeStamp()-cellDescription.getPreviousTimeStepSize());
  cellDescription.setTimeStepSize(cellDescription.getPreviousTimeStepSize());

  cellDescription.setPreviousTimeStepSize(std::numeric_limits<double>::max());
}

void exahype::solvers::FiniteVolumesSolver::setInitialConditions(
    const int cellDescriptionsIndex,
    const int element,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) {
  // reset helper variables
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell
//      && cellDescription.getRefinementEvent()==CellDescription::None
      ) {
    double* solution = exahype::DataHeap::getInstance().getData(cellDescription.getSolution()).data();

    if (useAdjustSolution(
        cellDescription.getOffset()+0.5*cellDescription.getSize(),
        cellDescription.getSize(),
        cellDescription.getTimeStamp()+cellDescription.getTimeStepSize(),
        cellDescription.getTimeStepSize())) {
      adjustSolution(
          solution,
          cellDescription.getOffset()+0.5*cellDescription.getSize(),
          cellDescription.getSize(),
          cellDescription.getTimeStamp()+cellDescription.getTimeStepSize(),
          cellDescription.getTimeStepSize());
    }

    for (int i=0; i<_dataPerPatch+_ghostDataPerPatch; i++) {
      assertion3(std::isfinite(solution[i]),cellDescription.toString(),"setInitialConditions(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
  }
}

void exahype::solvers::FiniteVolumesSolver::updateSolution(
    const int cellDescriptionsIndex,
    const int element,
    double** tempStateSizedVectors,
    double** tempUnknowns,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  assertion1(cellDescription.getNeighbourMergePerformed().all(),cellDescription.toString());

  double* solution    = DataHeap::getInstance().getData(cellDescription.getPreviousSolution()).data();
  double* newSolution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
  std::copy(newSolution,newSolution+_dataPerPatch+_ghostDataPerPatch,solution); // Copy (current solution) in old solution field.

  validateNoNansInFiniteVolumesSolution(cellDescription,cellDescriptionsIndex,"updateSolution");

  //    std::cout << "[pre] solution:" << std::endl;
  //    printFiniteVolumesSolution(cellDescription); // TODO(Dominic): remove
//  if (cellDescriptionsIndex==468) {
//    std::cout << "[pre] solution:" << std::endl;
//    printFiniteVolumesSolution(cellDescription); // TODO(Dominic): remove
//
//    ADERDGSolver::CellDescription& aderPatch =
//        ADERDGSolver::getCellDescription(cellDescriptionsIndex,cellDescription.getSolverNumber());
//    logDebug("updateSolution(...)","aderPatch="<<aderPatch.toString());
//  }

  // TODO(Dominic): Hotfix on master branch. This will not
  // be necessary in the future.
  double admissibleTimeStepSize=0;
  if (cellDescription.getTimeStepSize()>0) {
      solutionUpdate(
          newSolution,solution,tempStateSizedVectors,tempUnknowns,
          cellDescription.getSize(),cellDescription.getTimeStepSize(),admissibleTimeStepSize);
  }

  // cellDescription.getTimeStepSize() = 0 is an initial condition
  assertion2( tarch::la::equals(cellDescription.getTimeStepSize(),0.0) || !std::isnan(admissibleTimeStepSize), cellDescription.toString(), cellDescriptionsIndex );
  assertion2( tarch::la::equals(cellDescription.getTimeStepSize(),0.0) || !std::isinf(admissibleTimeStepSize), cellDescription.toString(), cellDescriptionsIndex );
  assertion2( tarch::la::equals(cellDescription.getTimeStepSize(),0.0) || admissibleTimeStepSize<std::numeric_limits<double>::max(), cellDescription.toString(), cellDescriptionsIndex );

  if ( !tarch::la::equals(cellDescription.getTimeStepSize(), 0.0) && tarch::la::smaller(admissibleTimeStepSize,cellDescription.getTimeStepSize()) ) { //TODO JMG 1.001 factor to prevent same dt computation to throw logerror
    logWarning("updateSolution(...)","Finite volumes solver time step size harmed CFL condition. dt="<<
               cellDescription.getTimeStepSize()<<", dt_adm=" << admissibleTimeStepSize << ". cell=" <<cellDescription.toString());
  }

  if (useAdjustSolution(
      cellDescription.getOffset()+0.5*cellDescription.getSize(),
      cellDescription.getSize(),
      cellDescription.getTimeStamp()+cellDescription.getTimeStepSize(),
      cellDescription.getTimeStepSize())) {
    adjustSolution(
        newSolution,
        cellDescription.getOffset()+0.5*cellDescription.getSize(),
        cellDescription.getSize(),
        cellDescription.getTimeStamp()+cellDescription.getTimeStepSize(),
        cellDescription.getTimeStepSize());
  }

//  std::cout << "[post] solution:" << std::endl;
//  printFiniteVolumesSolution(cellDescription); // TODO(Dominic): remove

  validateNoNansInFiniteVolumesSolution(cellDescription,cellDescriptionsIndex,"updateSolution");
}


void exahype::solvers::FiniteVolumesSolver::rollbackSolution(
    const int cellDescriptionsIndex,
    const int element,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) {
  swapSolutionAndPreviousSolution(cellDescriptionsIndex,element);
}

void exahype::solvers::FiniteVolumesSolver::swapSolutionAndPreviousSolution(
    const int cellDescriptionsIndex,
    const int element) const {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  // Simply swap the heap indices
  const int previousSolution = cellDescription.getPreviousSolution();
  cellDescription.setPreviousSolution(cellDescription.getSolution());
  cellDescription.setSolution(previousSolution);
}

void exahype::solvers::FiniteVolumesSolver::preProcess(
        const int cellDescriptionsIndex,
        const int element) {
  // TODO(Dominic)
//  assertionMsg(false,"Please implement!");
}

void exahype::solvers::FiniteVolumesSolver::postProcess(
        const int cellDescriptionsIndex,
        const int element) {
  // TODO(Dominic)
//  assertionMsg(false,"Please implement!");
}

void exahype::solvers::FiniteVolumesSolver::prolongateDataAndPrepareDataRestriction(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell)
    return;

  assertionMsg(false,"Please implement!");
}

void exahype::solvers::FiniteVolumesSolver::restrictToNextParent(
      const int fineGridCellDescriptionsIndex,
      const int fineGridElement,
      const int coarseGridCellDescriptionsIndex,
      const int coarseGridElement) {
  // do nothing
}

void exahype::solvers::FiniteVolumesSolver::restrictToTopMostParent(
    const int cellDescriptionsIndex,
    const int element,
    const int parentCellDescriptionsIndex,
    const int parentElement,
    const tarch::la::Vector<DIMENSIONS,int>& subcellIndex) {
  assertionMsg(false,"Please implement!");
}

///////////////////////////////////
// NEIGHBOUR
///////////////////////////////////
void exahype::solvers::FiniteVolumesSolver::mergeNeighboursMetadata(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2) {
  // do nothing
}

void exahype::solvers::FiniteVolumesSolver::mergeNeighbours(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2,
    double**                                  tempFaceUnknowns,
    double**                                  tempStateSizedVectors,
    double**                                  tempStateSizedSquareMatrices) {
  CellDescription& cellDescription1 = getCellDescription(cellDescriptionsIndex1,element1);
  CellDescription& cellDescription2 = getCellDescription(cellDescriptionsIndex2,element2);

  if (cellDescription1.getType()==CellDescription::Cell ||
      cellDescription2.getType()==CellDescription::Cell) {
    synchroniseTimeStepping(cellDescription1);
    synchroniseTimeStepping(cellDescription2);

    double* solution1 = DataHeap::getInstance().getData(cellDescription1.getSolution()).data();
    double* solution2 = DataHeap::getInstance().getData(cellDescription2.getSolution()).data();

    ghostLayerFilling(solution1,solution2,pos2-pos1);
    ghostLayerFilling(solution2,solution1,pos1-pos2);
  }

  return;

  assertionMsg(false,"Not implemented.");
}

void exahype::solvers::FiniteVolumesSolver::mergeWithBoundaryData(
    const int                                 cellDescriptionsIndex,
    const int                                 element,
    const tarch::la::Vector<DIMENSIONS, int>& posCell,
    const tarch::la::Vector<DIMENSIONS, int>& posBoundary,
    double**                                  tempFaceUnknowns,
    double**                                  tempStateSizedVectors,
    double**                                  tempStateSizedSquareMatrices) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell) {
    synchroniseTimeStepping(cellDescription);

    double* luh       = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
    double* luhbndIn  = tempFaceUnknowns[0];
    double* luhbndOut = tempFaceUnknowns[1];

    assertion1(getUnknownsPerFace() <= &luhbndOut[0] - &luhbndIn[0], &luhbndOut[0]-&luhbndIn[0]);

    assertion2(tarch::la::countEqualEntries(posCell,posBoundary)==DIMENSIONS-1,posCell.toString(),posBoundary.toString());

    const int normalNonZero = tarch::la::equalsReturnIndex(posCell, posBoundary);
    assertion(normalNonZero >= 0 && normalNonZero < DIMENSIONS);
    const int faceIndex = 2 * normalNonZero +
        (posCell(normalNonZero) < posBoundary(normalNonZero) ? 1 : 0);

    boundaryLayerExtraction(luhbndIn,luh,posBoundary-posCell);

    boundaryConditions(
        luhbndOut,luhbndIn,
        cellDescription.getOffset()+0.5*cellDescription.getSize(),
        cellDescription.getSize(),
        cellDescription.getTimeStamp(),
        cellDescription.getTimeStepSize(),
        faceIndex,
        normalNonZero);

    ghostLayerFillingAtBoundary(luh,luhbndOut,posBoundary-posCell);
  }
}

void exahype::solvers::FiniteVolumesSolver::mergeWithBoundaryOrEmptyCellMetadata(
      const int cellDescriptionsIndex,
      const int element,
      const tarch::la::Vector<DIMENSIONS, int>& posCell,
      const tarch::la::Vector<DIMENSIONS, int>& posBoundaryOrEmptyCell) {
   // do nothing
}

void exahype::solvers::FiniteVolumesSolver::prepareNextNeighbourMerging(
    const int cellDescriptionsIndex,const int element,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) const {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  exahype::Cell::resetNeighbourMergeHelperVariables(
      cellDescription,fineGridVertices,fineGridVerticesEnumerator);
}


#ifdef Parallel
const int exahype::solvers::FiniteVolumesSolver::DataMessagesPerNeighbourCommunication    = 1;
const int exahype::solvers::FiniteVolumesSolver::DataMessagesPerForkOrJoinCommunication   = 1;
const int exahype::solvers::FiniteVolumesSolver::DataMessagesPerMasterWorkerCommunication = 1;

void exahype::solvers::FiniteVolumesSolver::sendCellDescriptions(
    const int                                     toRank,
    const int                                     cellDescriptionsIndex,
    const peano::heap::MessageType&               messageType,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),
      cellDescriptionsIndex);

  if (Heap::getInstance().getData(cellDescriptionsIndex).size()>0) {
    Heap::getInstance().sendData(cellDescriptionsIndex,
                                 toRank,x,level,messageType);
  } else {
    sendEmptyCellDescriptions(toRank,messageType,x,level);
  }
}

void exahype::solvers::FiniteVolumesSolver::eraseCellDescriptions(
    const int cellDescriptionsIndex) {
  assertion(Heap::getInstance().isValidIndex(cellDescriptionsIndex));

  for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    const int element = solver->tryGetElement(cellDescriptionsIndex,solverNumber);
    if (element!=exahype::solvers::Solver::NotFound) {
      auto& cellDescription = getCellDescription(cellDescriptionsIndex,element);
      if (cellDescription.getType()==CellDescription::Type::Cell) {
        cellDescription.setType(CellDescription::Type::Erased);
        static_cast<FiniteVolumesSolver*>(solver)->ensureNoUnnecessaryMemoryIsAllocated(cellDescription);
      }

      Heap::getInstance().getData(cellDescriptionsIndex).erase(
          Heap::getInstance().getData(cellDescriptionsIndex).begin()+element);
    }
  }
}

void exahype::solvers::FiniteVolumesSolver::sendEmptyCellDescriptions(
    const int                                     toRank,
    const peano::heap::MessageType&               messageType,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  Heap::HeapEntries emptyMessage(0);
  Heap::getInstance().sendData(emptyMessage,
      toRank,x,level,messageType);
}

void exahype::solvers::FiniteVolumesSolver::mergeCellDescriptionsWithRemoteData(
    const int                                     fromRank,
    exahype::Cell&                                localCell,
    const peano::heap::MessageType&               messageType,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  waitUntilAllBackgroundTasksHaveTerminated();
  tarch::multicore::Lock lock(_heapSemaphore);

  logDebug("mergeCellDescriptionsWithRemoteData(...)","[pre] receive for cell ("
        "offset="<< x.toString() <<
        "level="<< level << ")");

  const int receivedCellDescriptionsIndex =
      Heap::getInstance().createData(0,exahype::solvers::RegisteredSolvers.size());
  Heap::getInstance().receiveData(
      receivedCellDescriptionsIndex,fromRank,x,level,messageType);

  logDebug("mergeCellDescriptionsWithRemoteData(...)","received " <<
      Heap::getInstance().getData(receivedCellDescriptionsIndex).size() <<
      " cell descriptions for cell ("
      "offset="<< x.toString() <<
      "level="<< level << ")");

  if (Heap::getInstance().getData(receivedCellDescriptionsIndex).size()>0) {
    // TODO(Dominic): We reset the parent and heap indices of a received cell
    // to -1 and RemoteAdjacencyIndex, respectively.
    // If we receive parent and children cells during a fork event,
    //
    // We use the information of a parentIndex of a fine grid cell description
    // set to RemoteAdjacencyIndex to update the parent index with
    // the index of the coarse grid cell description in enterCell(..).
    resetDataHeapIndices(receivedCellDescriptionsIndex,
        multiscalelinkedcell::HangingVertexBookkeeper::RemoteAdjacencyIndex);

    if (!localCell.isInitialised()) {
      localCell.setupMetaData();
    }
    assertion1(Heap::getInstance().isValidIndex(localCell.getCellDescriptionsIndex()),
        localCell.getCellDescriptionsIndex());
    Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).reserve(
        std::max(Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).size(),
            Heap::getInstance().getData(receivedCellDescriptionsIndex).size()));

    for (auto& pReceived : Heap::getInstance().getData(receivedCellDescriptionsIndex)) {
      logDebug("mergeCellDescriptionsWithRemoteData(...)","Received " <<
          " cell description for cell ("
          "offset="<< x.toString() <<
          ",level="<< level <<
          ",isRoot="<< localCell.isRoot() <<
          ",isAssignedToRemoteRank="<< localCell.isAssignedToRemoteRank() <<
          ") with type="<< pReceived.getType());

      bool found = false;
      for (auto& pLocal : Heap::getInstance().getData(localCell.getCellDescriptionsIndex())) {
        if (pReceived.getSolverNumber()==pLocal.getSolverNumber()) {
          found = true;

          assertion(pReceived.getType()==pLocal.getType());
          if (pLocal.getType()==CellDescription::Type::Cell
//              || // TODO(Dominic)
//              pLocal.getType()==CellDescription::Type::EmptyAncestor ||
//              pLocal.getType()==CellDescription::Type::Ancestor ||
//              pLocal.getType()==CellDescription::Type::Descendant
          ) {
            assertionNumericalEquals2(pLocal.getTimeStamp(),pReceived.getTimeStamp(),
                pLocal.toString(),pReceived.toString());
            assertionNumericalEquals2(pLocal.getTimeStepSize(),pReceived.getTimeStepSize(),
                pLocal.toString(),pReceived.toString());
          }
        }
      }

      if (!found) {
        FiniteVolumesSolver* solver =
            static_cast<FiniteVolumesSolver*>(RegisteredSolvers[pReceived.getSolverNumber()]);

        solver->ensureNecessaryMemoryIsAllocated(pReceived);
        Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).
            push_back(pReceived);
      }
    }
  }

  Heap::getInstance().deleteData(receivedCellDescriptionsIndex);
  assertion(!Heap::getInstance().isValidIndex(receivedCellDescriptionsIndex));
}

void exahype::solvers::FiniteVolumesSolver::resetDataHeapIndices(
    const int cellDescriptionsIndex,
    const int parentIndex) {
  for (auto& p : Heap::getInstance().getData(cellDescriptionsIndex)) {
    p.setParentIndex(parentIndex);

    // Default field data indices
    p.setPreviousSolution(-1);
    p.setSolution(-1);
  }
}

void exahype::solvers::FiniteVolumesSolver::ensureConsistencyOfParentIndex(
    CellDescription& fineGridCellDescription,
    const int coarseGridCellDescriptionsIndex,
    const int solverNumber) {
  fineGridCellDescription.setParentIndex(multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex);
  int coarseGridElement = tryGetElement(coarseGridCellDescriptionsIndex,solverNumber);
  if (coarseGridElement!=exahype::solvers::Solver::NotFound) {
    fineGridCellDescription.setParentIndex(coarseGridCellDescriptionsIndex);
    // In this case, we are not at a master worker boundary only our parent is.
//    fineGridCellDescription.setHasToHoldDataForMasterWorkerCommunication(false);  TODO(Dominic): Introduce to FiniteVolumesCellDescription.def
  }
}

/**
 * Drop cell descriptions received from \p fromRank.
 */
void exahype::solvers::FiniteVolumesSolver::dropCellDescriptions(
    const int                                     fromRank,
    const peano::heap::MessageType&               messageType,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  Heap::getInstance().receiveData(fromRank,x,level,messageType);
}

////////////////////////////////////
// MASTER <=> WORKER
////////////////////////////////////
void
exahype::solvers::FiniteVolumesSolver::appendMasterWorkerCommunicationMetadata(
    exahype::MetadataHeap::HeapEntries& metadata,
    const int cellDescriptionsIndex,
    const int solverNumber) {
  for (int i = 0; i < exahype::MasterWorkerCommunicationMetadataPerSolver; ++i) {
    metadata.push_back(exahype::InvalidMetadataEntry); // implicit conversion
  }
}

void exahype::solvers::FiniteVolumesSolver::mergeWithMasterWorkerMetadata(
      const MetadataHeap::HeapEntries& receivedMetadata,
      const int                        cellDescriptionsIndex,
      const int                        element) {
  // do nothing
}

///////////////////////////////////
// FORK OR JOIN
///////////////////////////////////

void exahype::solvers::FiniteVolumesSolver::sendDataToWorkerOrMasterDueToForkOrJoin(
    const int                                     toRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
      element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  CellDescription& p = Heap::getInstance().getData(cellDescriptionsIndex)[element];

  if (p.getType()==CellDescription::Cell) {
    double* solution = DataHeap::getInstance().getData(p.getSolution()).data();

    logDebug("sendDataToWorkerOrMasterDueToForkOrJoin(...)","solution of solver " << p.getSolverNumber() << " sent to rank "<<toRank<<
        ", cell: "<< x << ", level: " << level);

    DataHeap::getInstance().sendData(
        solution, getUnknownsPerPatch()+getGhostValuesPerPatch(), toRank, x, level,
        peano::heap::MessageType::ForkOrJoinCommunication);
  }
}


void exahype::solvers::FiniteVolumesSolver::sendEmptyDataToWorkerOrMasterDueToForkOrJoin(
    const int                                     toRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerForkOrJoinCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, toRank, x, level,
        peano::heap::MessageType::ForkOrJoinCommunication);
}


void exahype::solvers::FiniteVolumesSolver::mergeWithWorkerOrMasterDataDueToForkOrJoin(
    const int                                     fromRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  auto& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  assertion4(tarch::la::equals(x,cellDescription.getOffset()+0.5*cellDescription.getSize()),x,cellDescription.getOffset()+0.5*cellDescription.getSize(),level,cellDescription.getLevel());
  assertion2(cellDescription.getLevel()==level,cellDescription.getLevel(),level);

  if (cellDescription.getType()==CellDescription::Cell) {
    logDebug("mergeWithRemoteDataDueToForkOrJoin(...)","[solution] receive from rank "<<fromRank<<
             ", cell: "<< x << ", level: " << level);

    DataHeap::getInstance().getData(cellDescription.getSolution()).clear();
    DataHeap::getInstance().receiveData(
        cellDescription.getSolution(),fromRank,x,level,
        peano::heap::MessageType::ForkOrJoinCommunication);
  }
}

void exahype::solvers::FiniteVolumesSolver::dropWorkerOrMasterDataDueToForkOrJoin(
    const int                                     fromRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  for(int receives=0; receives<DataMessagesPerForkOrJoinCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        fromRank, x, level,
        peano::heap::MessageType::ForkOrJoinCommunication);
}

///////////////////////////////////
// NEIGHBOUR
///////////////////////////////////
void
exahype::solvers::FiniteVolumesSolver::appendNeighbourCommunicationMetadata(
    exahype::MetadataHeap::HeapEntries& metadata,
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest,
    const int cellDescriptionsIndex,
    const int solverNumber) {
  const int element = tryGetElement(cellDescriptionsIndex,solverNumber);

  if (element!=exahype::solvers::Solver::NotFound)  {
    CellDescription& cellDescription =
        getCellDescription(cellDescriptionsIndex,element);
    metadata.push_back(static_cast<int>(cellDescription.getType()));
    metadata.push_back(exahype::InvalidMetadataEntry);
    metadata.push_back(exahype::InvalidMetadataEntry);
    metadata.push_back(exahype::InvalidMetadataEntry);
  } else {
    for (int i = 0; i < exahype::NeighbourCommunicationMetadataPerSolver; ++i) {
      metadata.push_back(exahype::InvalidMetadataEntry); // implicit conversion
    }
  }
}

void exahype::solvers::FiniteVolumesSolver::mergeWithNeighbourMetadata(
    const exahype::MetadataHeap::HeapEntries& metadata,
    const tarch::la::Vector<DIMENSIONS, int>& src,
    const tarch::la::Vector<DIMENSIONS, int>& dest,
    const int cellDescriptionsIndex,
    const int element) {
  // do nothing
}

void exahype::solvers::FiniteVolumesSolver::sendDataToNeighbour(
    const int                                     toRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, int>&     src,
    const tarch::la::Vector<DIMENSIONS, int>&     dest,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion( tarch::la::countEqualEntries(src,dest)==(DIMENSIONS-1) ); // TODO(Dominic): If we only use Godunov
/*
  if (tarch::la::countEqualEntries(src,dest)!=(DIMENSIONS-1)) {
    return; // We only consider faces; no corners.
  }
*/
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (holdsFaceData(cellDescription.getType())) {
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getSolution()));
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getPreviousSolution()));

    const int numberOfFaceDof = getUnknownsPerFace();
    const int boundaryLayerToSendIndex = DataHeap::getInstance().createData(0, numberOfFaceDof);
    double* luhbnd = DataHeap::getInstance().getData(boundaryLayerToSendIndex).data();

    const double* luh = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
    boundaryLayerExtraction(luhbnd,luh,dest-src);

    logDebug(
        "sendDataToNeighbour(...)",
        "send "<<DataMessagesPerNeighbourCommunication<<" arrays to rank " <<
        toRank << " for cell="<<cellDescription.getOffset()
//        << "and face=" << faceIndex
        << " from vertex x=" << x << ", level=" << level <<
        ", src type=" << multiscalelinkedcell::indexToString(cellDescriptionsIndex) <<
        ", src=" << src << ", dest=" << dest
//        << ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    // Send order: minMax,lQhbnd,lFhbnd
    // Receive order: lFhbnd,lQhbnd,minMax
    DataHeap::getInstance().sendData(
        luhbnd, numberOfFaceDof, toRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);
    // TODO(Dominic): If anarchic time stepping send the time step over too.

    DataHeap::getInstance().deleteData(boundaryLayerToSendIndex,true);
  } else {
    std::vector<double> emptyArray(0,0);

    for(int sends=0; sends<DataMessagesPerNeighbourCommunication; ++sends) {
      DataHeap::getInstance().sendData(
          emptyArray, toRank, x, level,
          peano::heap::MessageType::NeighbourCommunication);
    }
  }
}

void exahype::solvers::FiniteVolumesSolver::sendEmptyDataToNeighbour(
    const int                                     toRank,
    const tarch::la::Vector<DIMENSIONS, int>&     src,
    const tarch::la::Vector<DIMENSIONS, int>&     dest,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerNeighbourCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, toRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);
}

void exahype::solvers::FiniteVolumesSolver::mergeWithNeighbourData(
    const int                                    fromRank,
    const MetadataHeap::HeapEntries&             neighbourMetadata,
    const int                                    cellDescriptionsIndex,
    const int                                    element,
    const tarch::la::Vector<DIMENSIONS, int>&    src,
    const tarch::la::Vector<DIMENSIONS, int>&    dest,
    double**                                     tempFaceUnknowns,
    double**                                     tempStateSizedVectors,
    double**                                     tempStateSizedSquareMatrices,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  if (tarch::la::countEqualEntries(src,dest)!=(DIMENSIONS-1)) {
    return; // We only consider faces; no corners.
  }

  waitUntilAllBackgroundTasksHaveTerminated();
  tarch::multicore::Lock lock(_heapSemaphore);

  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  synchroniseTimeStepping(cellDescription);
  CellDescription::Type neighbourType =
      static_cast<CellDescription::Type>(neighbourMetadata[exahype::NeighbourCommunicationMetadataCellType].getU());

  #if defined(Asserts) || defined(Debug)
  const int normalOfExchangedFace = tarch::la::equalsReturnIndex(src, dest);
  assertion(normalOfExchangedFace >= 0 && normalOfExchangedFace < DIMENSIONS);
  const int faceIndex = 2 * normalOfExchangedFace +
          (src(normalOfExchangedFace) > dest(normalOfExchangedFace) ? 1 : 0); // !!! Be aware of the ">" !!!
  #endif

  // TODO(Dominic): Add to docu: We only perform a Riemann solve if a Cell is involved.
  // Solving Riemann problems at a Ancestor Ancestor boundary might lead to problems
  // if one Ancestor is just used for restriction.
  if(neighbourType==CellDescription::Type::Cell || cellDescription.getType()==CellDescription::Type::Cell){
    tarch::multicore::Lock lock(_heapSemaphore);

    assertion1(holdsFaceData(neighbourType),neighbourType);
    assertion1(holdsFaceData(cellDescription.getType()),cellDescription.toString());

    assertion4(!cellDescription.getNeighbourMergePerformed(faceIndex),
        faceIndex,cellDescriptionsIndex,cellDescription.getOffset().toString(),cellDescription.getLevel());
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getSolution()));
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getPreviousSolution()));

    logDebug(
        "mergeWithNeighbourData(...)", "receive "<<DataMessagesPerNeighbourCommunication<<" arrays from rank " <<
        fromRank << " for vertex x=" << x << ", level=" << level <<
        ", src type=" << cellDescription.getType() <<
        ", src=" << src << ", dest=" << dest <<
        ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    // TODO(Dominic): If anarchic time stepping, receive the time step too.
    //
    // Copy the received boundary layer into a ghost layer of the solution.
    // TODO(Dominic): Pipe it directly through the Riemann solver if
    // we only use the Godunov method and not higher-order FVM methods.
    // For methods that are higher order in time, e.g., MUSCL-Hancock, we usually need
    // corner neighbours. This is why we currently adapt a GATHER-UPDATE algorithm
    // instead of a SOLVE RIEMANN PROBLEM AT BOUNDARY-UPDATE INTERIOR scheme.
    const int numberOfFaceDof      = getUnknownsPerFace();
    const int receivedBoundaryLayerIndex = DataHeap::getInstance().createData(0, numberOfFaceDof);
    double* luhbnd = DataHeap::getInstance().getData(receivedBoundaryLayerIndex).data();
    assertion(DataHeap::getInstance().getData(receivedBoundaryLayerIndex).empty());


    // Send order: minMax,lQhbnd,lFhbnd
    // Receive order: lFhbnd,lQhbnd,minMax
    DataHeap::getInstance().receiveData(luhbnd, numberOfFaceDof, fromRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);

    logDebug(
        "mergeWithNeighbourData(...)", "[pre] solve Riemann problem with received data." <<
        " cellDescription=" << cellDescription.toString() <<
        ",faceIndexForCell=" << faceIndex <<
        ",normalOfExchangedFac=" << normalOfExchangedFace <<
        ",x=" << x.toString() << ", level=" << level <<
        ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    double* luh = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
    ghostLayerFillingAtBoundary(luh,luhbnd,src-dest);

    DataHeap::getInstance().deleteData(receivedBoundaryLayerIndex,true);
  } else  {
    logDebug(
        "mergeWithNeighbourData(...)", "drop one array from rank " <<
        fromRank << " for vertex x=" << x << ", level=" << level <<
        ", src type=" << multiscalelinkedcell::indexToString(cellDescriptionsIndex) <<
        ", src=" << src << ", dest=" << dest
        << ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    dropNeighbourData(fromRank,src,dest,x,level);
  }
}

void exahype::solvers::FiniteVolumesSolver::dropNeighbourData(
    const int                                     fromRank,
    const tarch::la::Vector<DIMENSIONS, int>&     src,
    const tarch::la::Vector<DIMENSIONS, int>&     dest,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  for(int receives=0; receives<DataMessagesPerNeighbourCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        fromRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);
}

///////////////////////////////////
// WORKER->MASTER
///////////////////////////////////

/*
 * At the time of sending data to the master,
 * we have already performed a time step update locally
 * on the rank. We thus need to communicate the
 * current min predictor time step size to the master.
 * The next min predictor time step size is
 * already reset locally to the maximum double value.
 *
 * However on the master's side, we need to
 * merge the received time step size with
 * the next min predictor time step size since
 * the master has not yet performed a time step update
 * (i.e. called TimeStepSizeComputation::endIteration()).
 */
void exahype::solvers::FiniteVolumesSolver::sendDataToMaster(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> timeStepDataToReduce(0,4);
  timeStepDataToReduce.push_back(_minTimeStepSize);
  timeStepDataToReduce.push_back(_meshUpdateRequest ? 1.0 : -1.0);
  timeStepDataToReduce.push_back(_minCellSize);
  timeStepDataToReduce.push_back(_maxCellSize);

  assertion1(timeStepDataToReduce.size()==4,timeStepDataToReduce.size());
  assertion1(std::isfinite(timeStepDataToReduce[0]),timeStepDataToReduce[0]);
  if (_timeStepping==TimeStepping::Global) {
    assertionNumericalEquals1(_minNextTimeStepSize,std::numeric_limits<double>::max(),
        tarch::parallel::Node::getInstance().getRank());
  }

  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("sendDataToMaster(...)","Sending time step data: " <<
        "data[0]=" << timeStepDataToReduce[0] <<
        ",data[1]=" << timeStepDataToReduce[1] <<
        ",data[2]=" << timeStepDataToReduce[2] <<
        ",data[3]=" << timeStepDataToReduce[3]);
  }

  DataHeap::getInstance().sendData(
      timeStepDataToReduce.data(), timeStepDataToReduce.size(),
      masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

/**
 * At the time of the merging,
 * the workers and the master have already performed
 * at local update of the next predictor time step size
 * and of the predictor time stamp.
 * We thus need to minimise over both quantities.
 */
void exahype::solvers::FiniteVolumesSolver::mergeWithWorkerData(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> receivedTimeStepData(4);

  if (true || tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithWorkerData(...)","Receiving time step data [pre] from rank " << workerRank);
  }

  DataHeap::getInstance().receiveData(
      receivedTimeStepData.data(),receivedTimeStepData.size(),workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);

  assertion1(receivedTimeStepData.size()==4,receivedTimeStepData.size());
  assertion1(receivedTimeStepData[0]>=0,receivedTimeStepData[0]);
  assertion1(std::isfinite(receivedTimeStepData[0]),receivedTimeStepData[0]);
  // The master solver has not yet updated its minNextTimeStepSize.
  // Thus it does not yet equal MAX_DOUBLE.

  int index=0;
  _minNextTimeStepSize      = std::min( _minNextTimeStepSize, receivedTimeStepData[index++] );
  _nextMeshUpdateRequest |= ( receivedTimeStepData[index++] > 0 ) ? true : false;
  _nextMinCellSize          = std::min( _nextMinCellSize, receivedTimeStepData[index++] );
  _nextMaxCellSize          = std::max( _nextMaxCellSize, receivedTimeStepData[index++] );

  if (true || tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithWorkerData(...)","Receiving time step data: " <<
             "data[0]=" << receivedTimeStepData[0] <<
             ",data[1]=" << receivedTimeStepData[1] <<
             ",data[2]=" << receivedTimeStepData[2] <<
             ",data[3]=" << receivedTimeStepData[3] );

    logDebug("mergeWithWorkerData(...)","Updated time step fields: " <<
             "_minNextTimeStepSize="     << _minNextTimeStepSize <<
             "_nextMeshUpdateRequest=" << _nextMeshUpdateRequest <<
             ",_nextMinCellSize="        << _nextMinCellSize <<
             ",_nextMaxCellSize="        << _nextMaxCellSize);
  }
}

void exahype::solvers::FiniteVolumesSolver::sendEmptyDataToMaster(
    const int                                     masterRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerMasterWorkerCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::FiniteVolumesSolver::sendDataToMaster(
    const int                                     masterRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
      element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): Please implement.
//  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
//
//  if (cellDescription.getType()==CellDescription::Ancestor) {
//    double* extrapolatedPredictor = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
//    double* fluctuations          = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();
//
//    logDebug("sendDataToMaster(...)","Face data of solver " << cellDescription.getSolverNumber() << " sent to rank "<<masterRank<<
//        ", cell: "<< x << ", level: " << level);
//
//    // No inverted message order since we do synchronous data exchange.
//    // Order: extrapolatedPredictor,fluctuations.
//    DataHeap::getInstance().sendData(
//        extrapolatedPredictor, getUnknownsPerCellBoundary(), masterRank, x, level,
//        peano::heap::MessageType::MasterWorkerCommunication);
//    DataHeap::getInstance().sendData(
//        fluctuations, getUnknownsPerCellBoundary(), masterRank, x, level,
//        peano::heap::MessageType::MasterWorkerCommunication);
//  } else {
    sendEmptyDataToMaster(masterRank,x,level);
//  }
}

void exahype::solvers::FiniteVolumesSolver::mergeWithWorkerData(
    const int                                     workerRank,
    const exahype::MetadataHeap::HeapEntries&     workerMetadata,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  logDebug("mergeWithWorkerData(...)","Merge with worker data from rank "<<workerRank<<
             ", cell: "<< x << ", level: " << level);

  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): Implementation is similar to ADER-DG code if someone wants to
  // implement AMR for the pure finite volumes scheme

  dropWorkerData(workerRank,x,level);
}

void exahype::solvers::FiniteVolumesSolver::dropWorkerData(
    const int                                     workerRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  logDebug("dropWorkerData(...)","Dropping worker data from rank "<<workerRank<<
               ", cell: "<< x << ", level: " << level);

  for(int receives=0; receives<DataMessagesPerMasterWorkerCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}

///////////////////////////////////
// MASTER->WORKER
///////////////////////////////////
void exahype::solvers::FiniteVolumesSolver::sendDataToWorker(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> timeStepDataToSend(0,4);
  timeStepDataToSend.push_back(_minTimeStamp); // TODO(Dominic): Append previous time step size
  timeStepDataToSend.push_back(_minTimeStepSize);

  timeStepDataToSend.push_back(_minCellSize);
  timeStepDataToSend.push_back(_maxCellSize);

  assertion1(timeStepDataToSend.size()==4,timeStepDataToSend.size());
  assertion1(std::isfinite(timeStepDataToSend[0]),timeStepDataToSend[0]);
  assertion1(std::isfinite(timeStepDataToSend[1]),timeStepDataToSend[1]);

  if (_timeStepping==TimeStepping::Global) {
    assertionEquals1(_minNextTimeStepSize,std::numeric_limits<double>::max(),
        tarch::parallel::Node::getInstance().getRank());
  }

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("sendDataToWorker(...)","Broadcasting time step data: " <<
        " data[0]=" << timeStepDataToSend[0] <<
        ",data[1]=" << timeStepDataToSend[1] <<
        ",data[2]=" << timeStepDataToSend[2] <<
        ",data[3]=" << timeStepDataToSend[3]);
    logDebug("sendDataWorker(...)","_minNextTimeStepSize="<<_minNextTimeStepSize);
  }

  DataHeap::getInstance().sendData(
      timeStepDataToSend.data(), timeStepDataToSend.size(),
      workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::FiniteVolumesSolver::mergeWithMasterData(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> receivedTimeStepData(4);
  DataHeap::getInstance().receiveData(
      receivedTimeStepData.data(),receivedTimeStepData.size(),masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
  assertion1(receivedTimeStepData.size()==4,receivedTimeStepData.size());

  if (_timeStepping==TimeStepping::Global) {
    assertionNumericalEquals1(_minNextTimeStepSize,std::numeric_limits<double>::max(),
        _minNextTimeStepSize);
  }

  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithMasterData(...)","Received time step data: " <<
        "data[0]="  << receivedTimeStepData[0] <<
        ",data[1]=" << receivedTimeStepData[1] <<
        ",data[2]=" << receivedTimeStepData[2] <<
        ",data[3]=" << receivedTimeStepData[3]);
  }

  _minTimeStamp    = receivedTimeStepData[0];
  _minTimeStepSize = receivedTimeStepData[1];

  _minCellSize              = receivedTimeStepData[2];
  _maxCellSize              = receivedTimeStepData[3];
}

bool exahype::solvers::FiniteVolumesSolver::hasToSendDataToMaster(
    const int cellDescriptionsIndex,
    const int element) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());
//
//  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
//
//  if (cellDescription.getType()==CellDescription::Ancestor) {
//    return true;
//  } else if (cellDescription.getType()==CellDescription::EmptyAncestor) {
//    #if defined(Debug) || defined(Asserts)
//    exahype::solvers::Solver::SubcellPosition subcellPosition =
//        computeSubcellPositionOfCellOrAncestor(cellDescriptionsIndex,element);
//    assertion(subcellPosition.parentElement==exahype::solvers::Solver::NotFound);
//    #endif
//  }

  return false;
}

void exahype::solvers::FiniteVolumesSolver::sendDataToWorker(
    const int                                     workerRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): Please implement
//  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
//  if (cellDescription.getType()==CellDescription::Descendant) {
//    exahype::solvers::Solver::SubcellPosition subcellPosition =
//        exahype::amr::computeSubcellPositionOfDescendant<CellDescription,Heap>(cellDescription);
//    prolongateFaceDataToDescendant(cellDescription,subcellPosition);
//
//    double* extrapolatedPredictor = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
//    double* fluctuations          = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();
//
//    // No inverted message order since we do synchronous data exchange.
//    // Order: extraplolatedPredictor,fluctuations.
//    DataHeap::getInstance().sendData(
//        extrapolatedPredictor, getUnknownsPerCellBoundary(), workerRank, x, level,
//        peano::heap::MessageType::MasterWorkerCommunication);
//    DataHeap::getInstance().sendData(
//        fluctuations, getUnknownsPerCellBoundary(), workerRank, x, level,
//        peano::heap::MessageType::MasterWorkerCommunication);
//
//    logDebug("sendDataToWorker(...)","Sent face data of solver " <<
//             cellDescription.getSolverNumber() << " to rank "<< workerRank <<
//             ", cell: "<< x << ", level: " << level);
//  } else {
    sendEmptyDataToWorker(workerRank,x,level);
//  }
}

void exahype::solvers::FiniteVolumesSolver::sendEmptyDataToWorker(
    const int                                     workerRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerMasterWorkerCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::FiniteVolumesSolver::mergeWithMasterData(
    const int                                     masterRank,
    const exahype::MetadataHeap::HeapEntries&     masterMetadata,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
      element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): See ADER-DG solver if you want to implement AMR for the FV solver

  dropMasterData(masterRank,x,level);
}

void exahype::solvers::FiniteVolumesSolver::dropMasterData(
    const int                                     masterRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
        const int                                     level) {
  for(int receives=0; receives<DataMessagesPerMasterWorkerCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}
#endif

void exahype::solvers::FiniteVolumesSolver::validateNoNansInFiniteVolumesSolution(
    CellDescription& cellDescription,const int cellDescriptionsIndex,const char* methodTrace)  const {
  #if defined(Asserts)
  double* solution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
  #endif

  dfor(i,_nodesPerCoordinateAxis+_ghostLayerWidth) {
    if (tarch::la::allSmaller(i,_nodesPerCoordinateAxis+_ghostLayerWidth)
    && tarch::la::allGreater(i,_ghostLayerWidth-1)) {
      for (int unknown=0; unknown < _numberOfVariables; unknown++) {
        #if defined(Asserts)
        int iScalar = peano::utils::dLinearisedWithoutLookup(i,_nodesPerCoordinateAxis+2*_ghostLayerWidth)*_numberOfVariables+unknown;
        #endif // cellDescription.getTimeStepSize()==0.0 is an initial condition
        assertion7(tarch::la::equals(cellDescription.getTimeStepSize(),0.0)  || std::isfinite(solution[iScalar]),
                   cellDescription.toString(),cellDescriptionsIndex,solution[iScalar],i.toString(),
                   _nodesPerCoordinateAxis,_ghostLayerWidth,
                   methodTrace);
      }
    }
  } // Dead code elimination should get rid of this loop if Asserts is not set.
}

void exahype::solvers::FiniteVolumesSolver::printFiniteVolumesSolution(
    CellDescription& cellDescription)  const {
  #if DIMENSIONS==2
  double* solution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();

  for (int unknown=0; unknown < _numberOfVariables; unknown++) {
    std::cout <<  "unknown=" << unknown << std::endl;
    dfor(i,_nodesPerCoordinateAxis+2*_ghostLayerWidth) {
      int iScalar = peano::utils::dLinearisedWithoutLookup(i,_nodesPerCoordinateAxis+2*_ghostLayerWidth)*_numberOfVariables+unknown;
      std::cout << std::setprecision(3) << solution[iScalar] << ",";
      if (i(0)==_nodesPerCoordinateAxis+2*_ghostLayerWidth-1) {
        std::cout << std::endl;
      }
    }
  }
  std::cout <<  "}" << std::endl;
  #else
  double* solution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();

  for (int unknown=0; unknown < _numberOfVariables; unknown++) {
    std::cout <<  "unknown=" << unknown << std::endl;
    dfor(i,_nodesPerCoordinateAxis+2*_ghostLayerWidth) {
      int iScalar = peano::utils::dLinearisedWithoutLookup(i,_nodesPerCoordinateAxis+2*_ghostLayerWidth)*_numberOfVariables+unknown;
      std::cout << solution[iScalar] << ",";
      if (i(0)==_nodesPerCoordinateAxis+2*_ghostLayerWidth-1 &&
          i(1)==_nodesPerCoordinateAxis+2*_ghostLayerWidth-1) {
        std::cout << std::endl;
      }
    }
  }
  std::cout <<  "}" << std::endl;
  #endif
}

void exahype::solvers::FiniteVolumesSolver::printFiniteVolumesBoundaryLayer(const double* luhbnd)  const {
  #if DIMENSIONS==2
  for (int unknown=0; unknown < _numberOfVariables; unknown++) {
    std::cout <<  "unknown=" << unknown << std::endl;
    for(int i=0; i<_nodesPerCoordinateAxis; ++i) {
      int iScalar = i*_numberOfVariables+unknown;
      std::cout << luhbnd[iScalar] << ",";
      if (i==_nodesPerCoordinateAxis-1) {
        std::cout << std::endl;
      }
    }
  }
  std::cout <<  "}" << std::endl;
  #else
  for (int unknown=0; unknown < _numberOfVariables; unknown++) {
      std::cout <<  "unknown=" << unknown << std::endl;
      for(int j=0; j<_nodesPerCoordinateAxis; ++j) {
      for(int i=0; i<_nodesPerCoordinateAxis; ++i) {
        int iScalar = (j*_nodesPerCoordinateAxis+i)*_numberOfVariables+unknown;
        std::cout << luhbnd[iScalar] << ",";
        if (j==_nodesPerCoordinateAxis-1 &&
            i==_nodesPerCoordinateAxis-1) {
          std::cout << std::endl;
        }
      }
      }
    }
    std::cout <<  "}" << std::endl;
  #endif
}

std::string exahype::solvers::FiniteVolumesSolver::toString() const {
  std::ostringstream stringstr;
  toString(stringstr);
  return stringstr.str();
}

void exahype::solvers::FiniteVolumesSolver::toString (std::ostream& out) const {
  out << "(";
  out << "_identifier:" << _identifier;
  out << ",";
  out << "_type:" << exahype::solvers::Solver::toString(_type);
  out << ",";
  out << "_numberOfVariables:" << _numberOfVariables;
  out << ",";
  out << "_numberOfParameters:" << _numberOfParameters;
  out << ",";
  out << "_nodesPerCoordinateAxis:" << _nodesPerCoordinateAxis;
  out << ",";
  out << "_maximumMeshSize:" << _maximumMeshSize;
  out << ",";
  out << "_timeStepping:" << exahype::solvers::Solver::toString(_timeStepping); // only solver attributes
  out << ",";
  out << "_unknownsPerPatchFace:" << _dataPerPatchFace;
  out << ",";
  out << "_dataPerPatchBoundary:" << _dataPerPatchBoundary;
  out << ",";
  out << "_unknownsPerPatch:" << _dataPerPatch;
  out << ",";
  out << "_previousMinTimeStepSize:" << _previousMinTimeStepSize;
  out << ",";
  out << "_minTimeStamp:" << _minTimeStamp;
  out << ",";
  out << "_minTimeStepSize:" << _minTimeStepSize;
  out << ",";
  out << "_nextMinTimeStepSize:" << _minNextTimeStepSize;
  out <<  ")";
}
