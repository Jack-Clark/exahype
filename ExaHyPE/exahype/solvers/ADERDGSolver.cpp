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
 * \author Dominic E. Charrier, Tobias Weinzierl, Jean-Matthieu Gallard, Fabian Güra
 **/
#include "exahype/solvers/ADERDGSolver.h"


#include <limits>
#include <iomanip>

#include <algorithm>

#include "exahype/Cell.h"
#include "exahype/Vertex.h"
#include "exahype/VertexOperations.h"

#include "tarch/la/VectorVectorOperations.h"
#include "tarch/multicore/Lock.h"

#include "multiscalelinkedcell/HangingVertexBookkeeper.h"

#include "exahype/amr/AdaptiveMeshRefinement.h"

#include "peano/heap/CompressedFloatingPointNumbers.h"
#include "peano/datatraversal/TaskSet.h"

#include "exahype/solvers/LimitingADERDGSolver.h"


namespace {
  constexpr const char* tags[]{"solutionUpdate",
                             "volumeIntegral",
                             "surfaceIntegral",
                             "riemannSolver",
                             "spaceTimePredictor",
                             "stableTimeStepSize",
                             "solutionAdjustment",
                             "faceUnknownsProlongation",
                             "faceUnknownsRestriction",
                             "volumeUnknownsProlongation",
                             "volumeUnknownsRestriction",
                             "boundaryConditions",
                             "pointSource"
                             };
  constexpr const char* deepProfilingTags[]{
                             "spaceTimePredictor_PDEflux",
                             "spaceTimePredictor_PDEsource",
                             "spaceTimePredictor_overhead",
                             "spaceTimePredictor_PDEncp",
                             "riemannSolver_PDEmatrixb",
                             "riemannSolver_overhead"
  };
  typedef peano::heap::PlainCharHeap CompressedDataHeap;
}


#ifdef Asserts
/**
 * If you enable assertions, we have the option not to remove any entries from
 * any heap but to continue to store all unknown on the standard heap when we
 * compress. This allows us to validate that the data that is compressed in one
 * iteration and uncompressed in the next one does not differ too significantly
 * from the original data. There are however two drawbacks to this approach:
 *
 * - It is costly.
 * - It changes the code semantics - we actually work with other and more heap
 *   entries and thus cannot claim that a code with these assertions equals a
 *   code without any assertions.
 *
 * I thus decided to trigger the comparison of compressed vs. uncompressed data
 * through a special flag.
 */
//#define ValidateCompressedVsUncompressedData

double exahype::solvers::ADERDGSolver::PipedUncompressedBytes = 0;
double exahype::solvers::ADERDGSolver::PipedCompressedBytes = 0;
#endif



tarch::logging::Log exahype::solvers::ADERDGSolver::_log( "exahype::solvers::ADERDGSolver");


double exahype::solvers::ADERDGSolver::CompressionAccuracy = 0.0;

bool exahype::solvers::ADERDGSolver::SpawnCompressionAsBackgroundThread = false;

void exahype::solvers::ADERDGSolver::addNewCellDescription(
  const int cellDescriptionsIndex,
  const int                                      solverNumber,
  const exahype::records::ADERDGCellDescription::Type cellType,
  const exahype::records::ADERDGCellDescription::RefinementEvent refinementEvent,
  const int                                     level,
  const int                                     parentIndex,
  const tarch::la::Vector<DIMENSIONS, double>&  cellSize,
  const tarch::la::Vector<DIMENSIONS, double>&  cellOffset) {

  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion2(parentIndex == -1 || parentIndex != cellDescriptionsIndex, parentIndex, cellDescriptionsIndex);
  assertion2(parentIndex != cellDescriptionsIndex, parentIndex, cellDescriptionsIndex);

  assertion2(static_cast<unsigned int>(solverNumber) < solvers::RegisteredSolvers.size(),
             solverNumber, exahype::solvers::RegisteredSolvers.size());

  CellDescription newCellDescription;
  newCellDescription.setSolverNumber(solverNumber);

  // Default AMR settings
  newCellDescription.setType(cellType);
  newCellDescription.setParentIndex(parentIndex);
  newCellDescription.setLevel(level);
  newCellDescription.setRefinementEvent(refinementEvent);

  newCellDescription.setIsAugmented(false);
  newCellDescription.setAugmentationStatus(0);
  newCellDescription.setFacewiseAugmentationStatus(0); // implicit conversion
  newCellDescription.setHelperStatus(0);
  newCellDescription.setFacewiseHelperStatus(0); // implicit conversion
  if (cellType==CellDescription::Type::Cell) {
    newCellDescription.setHelperStatus(MaximumHelperStatus);
    newCellDescription.setFacewiseHelperStatus(MaximumHelperStatus); // implicit conversion
    // TODO(Dominic): Make sure prolongation and restriction considers this.
  }

  std::bitset<DIMENSIONS_TIMES_TWO> neighbourMergePerformed;  // default construction: no bit set
  newCellDescription.setNeighbourMergePerformed(neighbourMergePerformed);

  // Pass geometry information to the cellDescription description
  newCellDescription.setSize(cellSize);
  newCellDescription.setOffset(cellOffset);

  // Initialise MPI helper variables
  #ifdef Parallel
  newCellDescription.setHasToHoldDataForMasterWorkerCommunication(false);
  for (int faceIndex = 0; faceIndex < DIMENSIONS_TIMES_TWO; faceIndex++) {
    newCellDescription.setFaceDataExchangeCounter(faceIndex,TWO_POWER_D);
  }
  #endif

  // Default field data indices
  newCellDescription.setSolution(-1);
  newCellDescription.setUpdate(-1);
  newCellDescription.setExtrapolatedPredictor(-1);
  newCellDescription.setFluctuation(-1);

  // Limiter meta data (oscillations identificator)
  newCellDescription.setLimiterStatus(0); // 0 is CellDescription::LimiterStatus::Ok
  newCellDescription.setPreviousLimiterStatus(0);
  newCellDescription.setFacewiseLimiterStatus(0);  // implicit conversion
  newCellDescription.setSolutionMin(-1);
  newCellDescription.setSolutionMax(-1);

  // Compression
  newCellDescription.setCompressionState(exahype::records::ADERDGCellDescription::CompressionState::Uncompressed);
  newCellDescription.setSolutionAverages(-1);
  newCellDescription.setUpdateAverages(-1);
  newCellDescription.setExtrapolatedPredictorAverages(-1);
  newCellDescription.setFluctuationAverages(-1);

  newCellDescription.setSolutionCompressed(-1);
  newCellDescription.setUpdateCompressed(-1);
  newCellDescription.setExtrapolatedPredictorCompressed(-1);
  newCellDescription.setFluctuationCompressed(-1);

  ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex).push_back(newCellDescription);
}

/**
 * Returns the ADERDGCellDescription heap vector
 * at address \p cellDescriptionsIndex.
 */
exahype::solvers::ADERDGSolver::Heap::HeapEntries& exahype::solvers::ADERDGSolver::getCellDescriptions(
    const int cellDescriptionsIndex) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);

  return Heap::getInstance().getData(cellDescriptionsIndex);
}

/**
 * Returns the ADERDGCellDescription with index \p element
 * in the heap vector at address \p cellDescriptionsIndex.
 */
exahype::solvers::ADERDGSolver::CellDescription& exahype::solvers::ADERDGSolver::getCellDescription(
    const int cellDescriptionsIndex,
    const int element) {
  assertion2(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex,element);
  assertion2(element>=0,cellDescriptionsIndex,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),cellDescriptionsIndex,element);

  return Heap::getInstance().getData(cellDescriptionsIndex)[element];
}

/**
 * Returns if a ADERDGCellDescription type holds face data.
 */
bool exahype::solvers::ADERDGSolver::holdsFaceData(const CellDescription& cellDescription) {
  assertion1(cellDescription.getType()!=CellDescription::Type::Cell ||
            cellDescription.getHelperStatus()==MaximumHelperStatus,cellDescription.toString());
  return
      cellDescription.getHelperStatus()>=MinimumHelperStatusForAllocatingBoundaryData;
}

void exahype::solvers::ADERDGSolver::ensureNoUnnecessaryMemoryIsAllocated(exahype::records::ADERDGCellDescription& cellDescription) {

  if ((cellDescription.getType()==exahype::records::ADERDGCellDescription::Erased ||
      cellDescription.getType()==exahype::records::ADERDGCellDescription::Descendant ||
      cellDescription.getType()==exahype::records::ADERDGCellDescription::Ancestor)
      &&
      DataHeap::getInstance().isValidIndex(cellDescription.getSolution())
  ) {
    waitUntilAllBackgroundTasksHaveTerminated();

    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getSolution()));
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getPreviousSolution()));
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getUpdate()));

    tarch::multicore::Lock lock(_heapSemaphore);

    if (cellDescription.getUpdate()>=0) {
      DataHeap::getInstance().deleteData(cellDescription.getUpdate());
      assertion(cellDescription.getUpdateCompressed()==-1);
    }
    else {
      assertion(CompressionAccuracy>0.0);
      assertion(cellDescription.getUpdate()==-1);
      CompressedDataHeap::getInstance().deleteData(cellDescription.getUpdateCompressed());
    }

    if (cellDescription.getSolution()>=0) {
      DataHeap::getInstance().deleteData(cellDescription.getSolution());
      assertion(cellDescription.getSolutionCompressed()==-1);
    }
    else {
      assertion(CompressionAccuracy>0.0);
      assertion(cellDescription.getSolution()==-1);
      CompressedDataHeap::getInstance().deleteData(cellDescription.getSolutionCompressed());
    }

    DataHeap::getInstance().deleteData(cellDescription.getUpdateAverages());
    DataHeap::getInstance().deleteData(cellDescription.getSolutionAverages());
    DataHeap::getInstance().deleteData(cellDescription.getPreviousSolutionAverages());

    cellDescription.setPreviousSolution(-1);
    cellDescription.setSolution(-1);
    cellDescription.setUpdate(-1);

    cellDescription.setPreviousSolutionAverages(-1);
    cellDescription.setSolutionAverages(-1);
    cellDescription.setUpdateAverages(-1);

    cellDescription.setPreviousSolutionCompressed(-1);
    cellDescription.setSolutionCompressed(-1);
    cellDescription.setUpdateCompressed(-1);
  }

  if (
      (cellDescription.getType()==exahype::records::ADERDGCellDescription::Erased ||
      (cellDescription.getHelperStatus()<MinimumHelperStatusForAllocatingBoundaryData
      #ifdef Parallel
      && !cellDescription.getHasToHoldDataForMasterWorkerCommunication()
      #endif
      ))
      &&
      DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor())
  ) {
    assertion1(cellDescription.getType()!=CellDescription::Type::Cell ||
               cellDescription.getHelperStatus()==MaximumHelperStatus,
               cellDescription.toString());
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getFluctuation()));

    waitUntilAllBackgroundTasksHaveTerminated();
    tarch::multicore::Lock lock(_heapSemaphore);

    if (cellDescription.getExtrapolatedPredictor()>=0) {
      DataHeap::getInstance().deleteData(cellDescription.getExtrapolatedPredictor());
      assertion(cellDescription.getExtrapolatedPredictorCompressed()==-1);
    }
    else {
      assertion(CompressionAccuracy>0.0);
      assertion(cellDescription.getExtrapolatedPredictor()==-1);
      CompressedDataHeap::getInstance().deleteData(cellDescription.getExtrapolatedPredictorCompressed());
    }

    if (cellDescription.getFluctuation()>=0) {
      DataHeap::getInstance().deleteData(cellDescription.getFluctuation());
      assertion(cellDescription.getFluctuationCompressed()==-1);
    }
    else {
      assertion(CompressionAccuracy>0.0);
      assertion(cellDescription.getFluctuation()==-1);
      CompressedDataHeap::getInstance().deleteData(cellDescription.getFluctuationCompressed());
    }

    DataHeap::getInstance().deleteData(cellDescription.getExtrapolatedPredictorAverages());
    DataHeap::getInstance().deleteData(cellDescription.getFluctuationAverages());

    if (getDMPObservables()>0) {
      assertion(DataHeap::getInstance().isValidIndex(cellDescription.getSolutionMin()));
      assertion(DataHeap::getInstance().isValidIndex(cellDescription.getSolutionMax()));
      DataHeap::getInstance().deleteData(cellDescription.getSolutionMin());
      DataHeap::getInstance().deleteData(cellDescription.getSolutionMax());

      cellDescription.setSolutionMin(-1);
      cellDescription.setSolutionMax(-1);
    }

    cellDescription.setExtrapolatedPredictor(-1);
    cellDescription.setFluctuation(-1);

    cellDescription.setExtrapolatedPredictorCompressed(-1);
    cellDescription.setFluctuationCompressed(-1);
  }
}

void exahype::solvers::ADERDGSolver::ensureNecessaryMemoryIsAllocated(exahype::records::ADERDGCellDescription& cellDescription) {
  if (
      cellDescription.getType()==CellDescription::Cell
      &&
      !DataHeap::getInstance().isValidIndex(cellDescription.getSolution())
  ) {
    waitUntilAllBackgroundTasksHaveTerminated();

    tarch::multicore::Lock lock(_heapSemaphore);
    assertion(!DataHeap::getInstance().isValidIndex(cellDescription.getUpdate()));
    // Allocate volume DoF for limiter
    const int dofPerCell        = getUnknownsPerCell();
    const int dataPointsPerCell = getDataPerCell(); // Only the solution and previousSolution store material parameters
    cellDescription.setPreviousSolution(DataHeap::getInstance().createData(dataPointsPerCell, dataPointsPerCell, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));
    cellDescription.setSolution(DataHeap::getInstance().createData(dataPointsPerCell, dataPointsPerCell, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));
    cellDescription.setUpdate(DataHeap::getInstance().createData(dofPerCell, dofPerCell, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));

    assertionEquals(DataHeap::getInstance().getData(cellDescription.getPreviousSolution()).size(),static_cast<unsigned int>(dataPointsPerCell));
    assertionEquals(DataHeap::getInstance().getData(cellDescription.getUpdate()).capacity(),static_cast<unsigned int>(dofPerCell));
    assertionEquals(DataHeap::getInstance().getData(cellDescription.getUpdate()).size(),static_cast<unsigned int>(dofPerCell));
    assertionEquals(DataHeap::getInstance().getData(cellDescription.getSolution()).capacity(),static_cast<unsigned int>(dataPointsPerCell));

    cellDescription.setUpdateCompressed(-1);
    cellDescription.setSolutionCompressed(-1);
    cellDescription.setPreviousSolutionCompressed(-1);

    if (CompressionAccuracy>0.0) {
      CompressedDataHeap::getInstance().reserveHeapEntriesForRecycling(2);
    }

    cellDescription.setPreviousSolutionAverages( DataHeap::getInstance().createData( getNumberOfVariables()+getNumberOfParameters(), getNumberOfVariables()+getNumberOfParameters(), DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired ) );
    cellDescription.setUpdateAverages(           DataHeap::getInstance().createData( getNumberOfVariables(), getNumberOfVariables(), DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired ) );
    cellDescription.setSolutionAverages(         DataHeap::getInstance().createData( getNumberOfVariables()+getNumberOfParameters(), getNumberOfVariables()+getNumberOfParameters(), DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired ) );

    assertionEquals3(
        DataHeap::getInstance().getData(cellDescription.getPreviousSolutionAverages()).size(),static_cast<unsigned int>(getNumberOfVariables() + getNumberOfParameters()),
        DataHeap::getInstance().getData(cellDescription.getPreviousSolutionAverages()).size(),static_cast<unsigned int>(getNumberOfVariables() + getNumberOfParameters()),
        getNumberOfVariables()
    );
    assertionEquals3(
        DataHeap::getInstance().getData(cellDescription.getUpdateAverages()).size(),static_cast<unsigned int>(getNumberOfVariables()),
        DataHeap::getInstance().getData(cellDescription.getUpdateAverages()).size(),static_cast<unsigned int>(getNumberOfVariables()),
        getNumberOfVariables()
    );
    assertionEquals3(
        DataHeap::getInstance().getData(cellDescription.getSolutionAverages()).size(),static_cast<unsigned int>(getNumberOfVariables() + getNumberOfParameters()),
        DataHeap::getInstance().getData(cellDescription.getSolutionAverages()).size(),static_cast<unsigned int>(getNumberOfVariables() + getNumberOfParameters()),
        getNumberOfVariables()
    );

    cellDescription.setCompressionState(exahype::records::ADERDGCellDescription::Uncompressed);
  }

  assertion1(cellDescription.getType()!=CellDescription::Cell ||
      cellDescription.getHelperStatus()==MaximumHelperStatus,
      cellDescription.toString());

  if(
      cellDescription.getHelperStatus()>=MinimumHelperStatusForAllocatingBoundaryData
      &&
      !DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor())
  ) {
    assertion(!DataHeap::getInstance().isValidIndex(cellDescription.getFluctuation()));

    waitUntilAllBackgroundTasksHaveTerminated();

    tarch::multicore::Lock lock(_heapSemaphore);
    // Allocate face DoF
    const int dataPerBnd = getBndTotalSize();
    const int dofPerBnd  = getBndFluxTotalSize();

    cellDescription.setExtrapolatedPredictor(DataHeap::getInstance().createData(dataPerBnd, dataPerBnd, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));
    cellDescription.setFluctuation(          DataHeap::getInstance().createData(dofPerBnd, dofPerBnd, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));

    assertionEquals3(
        DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).size(),static_cast<unsigned int>(dataPerBnd),
        cellDescription.getExtrapolatedPredictor(),
        cellDescription.toString(),
        toString()
    );
    assertionEquals3(
        DataHeap::getInstance().getData(cellDescription.getFluctuation()).size(),static_cast<unsigned int>(dofPerBnd),
        cellDescription.getExtrapolatedPredictor(),
        cellDescription.toString(),
        toString()
    );

    cellDescription.setExtrapolatedPredictorCompressed(-1);
    cellDescription.setFluctuationCompressed(-1);

    if (CompressionAccuracy>0.0) {
      CompressedDataHeap::getInstance().reserveHeapEntriesForRecycling(2);
    }

    //TODO JMG / Dominic adapt for padding with optimized kernels
    int faceAverageCardinality = getNumberOfVariables() * DIMENSIONS_TIMES_TWO;
    cellDescription.setExtrapolatedPredictorAverages( DataHeap::getInstance().createData( faceAverageCardinality, faceAverageCardinality, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired ) );
    cellDescription.setFluctuationAverages(           DataHeap::getInstance().createData( faceAverageCardinality, faceAverageCardinality, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired ) );

    // Allocate volume DoF for limiter (we need for every of the 2*DIMENSIONS faces an array of min values
    // and array of max values of the neighbour at this face).
    const int numberOfObservables = getDMPObservables();
    if (numberOfObservables>0) {
      cellDescription.setSolutionMin(DataHeap::getInstance().createData(
          numberOfObservables * DIMENSIONS_TIMES_TWO, numberOfObservables * DIMENSIONS_TIMES_TWO, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));
      cellDescription.setSolutionMax(DataHeap::getInstance().createData(
          numberOfObservables * DIMENSIONS_TIMES_TWO, numberOfObservables * DIMENSIONS_TIMES_TWO, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired));

      for (int i=0; i<numberOfObservables * DIMENSIONS_TIMES_TWO; i++) {
        DataHeap::getInstance().getData( cellDescription.getSolutionMin() )[i] = std::numeric_limits<double>::max();
        DataHeap::getInstance().getData( cellDescription.getSolutionMax() )[i] = -std::numeric_limits<double>::max();
      }
    }
  }
}

exahype::solvers::ADERDGSolver::ADERDGSolver(
    const std::string& identifier, int numberOfVariables,
    int numberOfParameters, int DOFPerCoordinateAxis,
    double maximumMeshSize, int maximumAdaptiveMeshDepth,
    int DMPObservables,
    exahype::solvers::Solver::TimeStepping timeStepping,
    std::unique_ptr<profilers::Profiler> profiler)
    : Solver(identifier, Solver::Type::ADERDG, numberOfVariables,
             numberOfParameters, DOFPerCoordinateAxis,
             maximumMeshSize, maximumAdaptiveMeshDepth,
             timeStepping, std::move(profiler)),
     _previousMinCorrectorTimeStamp( std::numeric_limits<double>::max() ),
     _previousMinCorrectorTimeStepSize( std::numeric_limits<double>::max() ),
     _minCorrectorTimeStamp( std::numeric_limits<double>::max() ),
     _minCorrectorTimeStepSize( std::numeric_limits<double>::max() ),
     _minPredictorTimeStamp( std::numeric_limits<double>::max() ),
     _minPredictorTimeStepSize( std::numeric_limits<double>::max() ),
     _minNextPredictorTimeStepSize( std::numeric_limits<double>::max() ),
     _stabilityConditionWasViolated( false ),
     _dofPerFace( numberOfVariables * power(DOFPerCoordinateAxis, DIMENSIONS - 1) ),
     _dofPerCellBoundary( DIMENSIONS_TIMES_TWO * _dofPerFace ),
     _dofPerCell( numberOfVariables * power(DOFPerCoordinateAxis, DIMENSIONS + 0) ),
     _fluxDofPerCell( _dofPerCell * (DIMENSIONS + 1) ),  // +1 for sources
     _spaceTimeDofPerCell( numberOfVariables * power(DOFPerCoordinateAxis, DIMENSIONS + 1) ),
     _spaceTimeFluxDofPerCell( _spaceTimeDofPerCell * (DIMENSIONS + 1) ),  // +1 for sources
     _dataPointsPerCell( (numberOfVariables+numberOfParameters) * power(DOFPerCoordinateAxis, DIMENSIONS + 0) ),
     _DMPObservables(DMPObservables)
{
  // register tags with profiler
  for (const char* tag : tags) {
    _profiler->registerTag(tag);
  }
  
  for (const char* tag : deepProfilingTags) {
    _profiler->registerTag(tag); //TODO JMG only if using deepProfiling
  }

  CompressedDataHeap::getInstance().setName("compressed-data");
}

int exahype::solvers::ADERDGSolver::getUnknownsPerFace() const {
  return _dofPerFace;
}

int exahype::solvers::ADERDGSolver::getUnknownsPerCellBoundary() const {
  return _dofPerCellBoundary;
}

int exahype::solvers::ADERDGSolver::getUnknownsPerCell() const {
  return _dofPerCell;
}

int exahype::solvers::ADERDGSolver::getFluxUnknownsPerCell() const {
  return _fluxDofPerCell;
}

int exahype::solvers::ADERDGSolver::getSpaceTimeUnknownsPerCell() const {
  return _spaceTimeDofPerCell;
}

int exahype::solvers::ADERDGSolver::getSpaceTimeFluxUnknownsPerCell() const {
  return _spaceTimeFluxDofPerCell;
}

int exahype::solvers::ADERDGSolver::getDataPerFace() const {
  return (_numberOfVariables+_numberOfParameters) * power(_nodesPerCoordinateAxis, DIMENSIONS - 1);
}

int exahype::solvers::ADERDGSolver::getDataPerCellBoundary() const {
  return (_numberOfVariables+_numberOfParameters) * power(_nodesPerCoordinateAxis, DIMENSIONS - 1) * DIMENSIONS_TIMES_TWO;
}

int exahype::solvers::ADERDGSolver::getDataPerCell() const {
  return (_numberOfVariables+_numberOfParameters) * power(_nodesPerCoordinateAxis, DIMENSIONS + 0);
}

int exahype::solvers::ADERDGSolver::getSpaceTimeDataPerCell() const {
  return (_numberOfVariables+_numberOfParameters) * power(_nodesPerCoordinateAxis, DIMENSIONS + 1);
}

int exahype::solvers::ADERDGSolver::getDMPObservables() const {
  return _DMPObservables;
}

void exahype::solvers::ADERDGSolver::synchroniseTimeStepping(
    CellDescription& p) const {
  switch (_timeStepping) {
    case TimeStepping::Global:
      p.setPreviousCorrectorTimeStamp(_previousMinCorrectorTimeStamp);
      p.setPreviousCorrectorTimeStepSize(_previousMinCorrectorTimeStepSize);

      p.setCorrectorTimeStamp(_minCorrectorTimeStamp);
      p.setCorrectorTimeStepSize(_minCorrectorTimeStepSize);

      p.setPredictorTimeStamp(_minPredictorTimeStamp);
      p.setPredictorTimeStepSize(_minPredictorTimeStepSize);
      break;
    case TimeStepping::GlobalFixed:
      p.setPreviousCorrectorTimeStamp(_previousMinCorrectorTimeStamp);
      p.setPreviousCorrectorTimeStepSize(_previousMinCorrectorTimeStepSize);

      p.setCorrectorTimeStamp(_minCorrectorTimeStamp);
      p.setCorrectorTimeStepSize(_minCorrectorTimeStepSize);

      p.setPredictorTimeStamp(_minPredictorTimeStamp);
      p.setPredictorTimeStepSize(_minPredictorTimeStepSize);
      break;
  }
}

void exahype::solvers::ADERDGSolver::synchroniseTimeStepping(
      const int cellDescriptionsIndex,
      const int element) {
  synchroniseTimeStepping(Heap::getInstance().getData(cellDescriptionsIndex)[element]);
}

void exahype::solvers::ADERDGSolver::startNewTimeStep() {
  switch (_timeStepping) {
    case TimeStepping::Global:
      // n-1
      _previousMinCorrectorTimeStamp    = _minCorrectorTimeStamp;
      _previousMinCorrectorTimeStepSize = _minCorrectorTimeStepSize;
      // n
      _minCorrectorTimeStamp    = _minPredictorTimeStamp;
      _minCorrectorTimeStepSize = _minPredictorTimeStepSize;
      // n+1
      _minPredictorTimeStamp    = _minPredictorTimeStamp + _minPredictorTimeStepSize;
      _minPredictorTimeStepSize = _minNextPredictorTimeStepSize;

      _minNextPredictorTimeStepSize = std::numeric_limits<double>::max();
      break;
    case TimeStepping::GlobalFixed:
      // n-1
      _previousMinCorrectorTimeStamp    = _minCorrectorTimeStamp;
      _previousMinCorrectorTimeStepSize = _minCorrectorTimeStepSize;
      // n
      _minCorrectorTimeStamp    = _minPredictorTimeStamp;
      _minCorrectorTimeStepSize = _minPredictorTimeStepSize;
      // n+1
      _minPredictorTimeStamp    = _minPredictorTimeStamp + _minPredictorTimeStepSize;
      _minPredictorTimeStepSize = _minNextPredictorTimeStepSize;
      break;
  }

  _minCellSize     = _nextMinCellSize;
  _maxCellSize     = _nextMaxCellSize;
  _nextMinCellSize = std::numeric_limits<double>::max();
  _nextMaxCellSize = -std::numeric_limits<double>::max(); // "-", min
}

void exahype::solvers::ADERDGSolver::zeroTimeStepSizes() {
  _minCorrectorTimeStepSize = 0;
  _minPredictorTimeStepSize = 0;

  _minPredictorTimeStamp = _minCorrectorTimeStamp;
}

void exahype::solvers::ADERDGSolver::reconstructStandardTimeSteppingData() {
  //  _previousMinCorrectorTimeStepSize = _minCorrectorTimeStepSize; // TODO(Dominic): Should not necessary.
  _minPredictorTimeStamp    = _minCorrectorTimeStamp+_minCorrectorTimeStepSize;
  _minCorrectorTimeStamp    = _minPredictorTimeStamp;
  _minCorrectorTimeStepSize = _minPredictorTimeStepSize;

  assertionEquals(_minCorrectorTimeStamp,_minPredictorTimeStamp);
  assertionEquals(_minCorrectorTimeStepSize,_minPredictorTimeStepSize);
}

void exahype::solvers::ADERDGSolver::reinitialiseTimeStepData() {
  switch (_timeStepping) {
    case TimeStepping::Global:
      _minPredictorTimeStepSize = _minNextPredictorTimeStepSize;
      _minPredictorTimeStamp    = _minCorrectorTimeStamp+_minNextPredictorTimeStepSize;
      break;
    case TimeStepping::GlobalFixed:
      //do nothing
      break;
  }
}

void exahype::solvers::ADERDGSolver::rollbackToPreviousTimeStep() {
  switch (_timeStepping) {
    case TimeStepping::Global:
      _minNextPredictorTimeStepSize             = std::numeric_limits<double>::max();

      _minPredictorTimeStamp                    = _minCorrectorTimeStamp;
      _minPredictorTimeStepSize                 = _minCorrectorTimeStepSize;

      _minCorrectorTimeStamp                    = _previousMinCorrectorTimeStamp;
      _minCorrectorTimeStepSize                 = _previousMinCorrectorTimeStepSize;

      _previousMinCorrectorTimeStamp            = std::numeric_limits<double>::max();
      _previousMinCorrectorTimeStepSize         = std::numeric_limits<double>::max();
      break;
    case TimeStepping::GlobalFixed:
      _minPredictorTimeStamp                    = _minCorrectorTimeStamp;
      _minPredictorTimeStepSize                 = _minCorrectorTimeStepSize;

      _minCorrectorTimeStamp                    = _previousMinCorrectorTimeStamp;
      _minCorrectorTimeStepSize                 = _previousMinCorrectorTimeStepSize;

      _previousMinCorrectorTimeStamp            = std::numeric_limits<double>::max();
      _previousMinCorrectorTimeStepSize         = std::numeric_limits<double>::max();
      break;
  }

  _nextMinCellSize = std::numeric_limits<double>::max();
  _nextMaxCellSize = -std::numeric_limits<double>::max(); // "-", min
}

void exahype::solvers::ADERDGSolver::reconstructStandardTimeSteppingDataAfterRollback() {
  _minPredictorTimeStamp            = _minCorrectorTimeStamp;    // corrector time stamp is now the previous corrector time stamp
  _minPredictorTimeStepSize         = _minCorrectorTimeStepSize; // corrector time step size is now the previous corrector time step size
  _previousMinCorrectorTimeStepSize = std::numeric_limits<double>::max();
}

void exahype::solvers::ADERDGSolver::updateMinNextPredictorTimeStepSize(
    const double& minNextPredictorTimeStepSize) {
  switch (_timeStepping) {
    case TimeStepping::Global:
      _minNextPredictorTimeStepSize =
          std::min(_minNextPredictorTimeStepSize, minNextPredictorTimeStepSize);
      break;
    case TimeStepping::GlobalFixed: // TODO(Dominic): Problematic in MPI where we merge with the worker first
      _minNextPredictorTimeStepSize =
          _minPredictorTimeStamp == _minCorrectorTimeStamp
              ? std::min(_minNextPredictorTimeStepSize,
                         minNextPredictorTimeStepSize)
              : _minNextPredictorTimeStepSize;
      break;
  }
}

double exahype::solvers::ADERDGSolver::getMinNextPredictorTimeStepSize() const {
  return _minNextPredictorTimeStepSize;
}

void exahype::solvers::ADERDGSolver::setMinCorrectorTimeStamp(
    double minCorrectorTimeStamp) {
  _minCorrectorTimeStamp = minCorrectorTimeStamp;
}

double exahype::solvers::ADERDGSolver::getMinCorrectorTimeStamp() const {
  return _minCorrectorTimeStamp;
}

void exahype::solvers::ADERDGSolver::setMinPredictorTimeStamp(
    double minPredictorTimeStamp) {
  _minPredictorTimeStamp = minPredictorTimeStamp;
}

double exahype::solvers::ADERDGSolver::getMinPredictorTimeStamp() const {
  return _minPredictorTimeStamp;
}

void exahype::solvers::ADERDGSolver::setMinCorrectorTimeStepSize(
    double minCorrectorTimeStepSize) {
  _minCorrectorTimeStepSize = minCorrectorTimeStepSize;
}

double exahype::solvers::ADERDGSolver::getMinCorrectorTimeStepSize() const {
  return _minCorrectorTimeStepSize;
}

void exahype::solvers::ADERDGSolver::setMinPredictorTimeStepSize(
    double minPredictorTimeStepSize) {
  _minPredictorTimeStepSize = minPredictorTimeStepSize;
}

double exahype::solvers::ADERDGSolver::getMinPredictorTimeStepSize() const {
  return _minPredictorTimeStepSize;
}

double exahype::solvers::ADERDGSolver::getPreviousMinCorrectorTimeStepSize() const {
  return _previousMinCorrectorTimeStepSize;
}

void exahype::solvers::ADERDGSolver::setPreviousMinCorrectorTimeStamp(double value) {
  _previousMinCorrectorTimeStamp = value;
}

double exahype::solvers::ADERDGSolver::getPreviousMinCorrectorTimeStamp() const {
  return _previousMinCorrectorTimeStamp;
}

void exahype::solvers::ADERDGSolver::setPreviousMinCorrectorTimeStepSize(double value) {
  _previousMinCorrectorTimeStepSize = value;
}

double exahype::solvers::ADERDGSolver::getMinTimeStamp() const {
  return getMinCorrectorTimeStamp();
}

double exahype::solvers::ADERDGSolver::getMinTimeStepSize() const {
  return getMinCorrectorTimeStepSize();
}

double exahype::solvers::ADERDGSolver::getMinNextTimeStepSize() const {
  return getMinNextPredictorTimeStepSize();
}

void exahype::solvers::ADERDGSolver::updateMinNextTimeStepSize( double value ) {
  updateMinNextPredictorTimeStepSize(value);
}

void exahype::solvers::ADERDGSolver::initSolver(
    const double timeStamp,
    const tarch::la::Vector<DIMENSIONS,double>& domainOffset,
    const tarch::la::Vector<DIMENSIONS,double>& domainSize) {
  _domainOffset=domainOffset;
  _domainSize=domainSize;
  _coarsestMeshLevel =
      exahype::solvers::Solver::computeMeshLevel(_maximumMeshSize,domainSize[0]);

  setPreviousMinCorrectorTimeStepSize(0.0);
  setMinCorrectorTimeStepSize(0.0);
  setMinPredictorTimeStepSize(0.0);

  setPreviousMinCorrectorTimeStamp(timeStamp);
  setMinCorrectorTimeStamp(timeStamp);
  setMinPredictorTimeStamp(timeStamp);

  _meshUpdateRequest = true;
}

bool exahype::solvers::ADERDGSolver::isSending(
    const exahype::records::State::AlgorithmSection& section) const {
  bool isSending = false;

  switch (section) {
    case exahype::records::State::AlgorithmSection::TimeStepping:
      isSending = true;
      break;
    case exahype::records::State::AlgorithmSection::LimiterStatusSpreading:
      isSending = false;
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinement:
      isSending = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinementAllSend:
      isSending = true;
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinementOrGlobalRecomputation:
      isSending = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinementOrLocalOrGlobalRecomputation:
      isSending = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::LocalRecomputationAllSend:
      isSending = true;
      break;
    case exahype::records::State::AlgorithmSection::GlobalRecomputationAllSend:
      isSending = true;
      break;
    case exahype::records::State::AlgorithmSection::PredictionRerunAllSend:
      isSending = true;
  }

  return isSending;
}

bool exahype::solvers::ADERDGSolver::isComputing(
    const exahype::records::State::AlgorithmSection& section) const {
  bool isComputing = false;

  switch (section) {
    case exahype::records::State::AlgorithmSection::TimeStepping:
      isComputing = true;
      break;
    case exahype::records::State::AlgorithmSection::LimiterStatusSpreading:
      isComputing = false;
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinement:
      isComputing = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinementAllSend:
      isComputing = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinementOrGlobalRecomputation:
      isComputing = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::MeshRefinementOrLocalOrGlobalRecomputation:
      isComputing = getMeshUpdateRequest();
      break;
    case exahype::records::State::AlgorithmSection::LocalRecomputationAllSend:
      isComputing = false;
      break;
    case exahype::records::State::AlgorithmSection::GlobalRecomputationAllSend:
      isComputing = false;
      break;
    case exahype::records::State::AlgorithmSection::PredictionRerunAllSend:
      isComputing = getStabilityConditionWasViolated();
  }

  return isComputing;
}


void exahype::solvers::ADERDGSolver::initFusedSolverTimeStepSizes() {
  setPreviousMinCorrectorTimeStepSize(getMinPredictorTimeStepSize());
  setMinCorrectorTimeStepSize(getMinPredictorTimeStepSize());
  setMinPredictorTimeStepSize(getMinPredictorTimeStepSize());
}

void exahype::solvers::ADERDGSolver::setStabilityConditionWasViolated(bool state) {
  _stabilityConditionWasViolated = state;
}

bool exahype::solvers::ADERDGSolver::getStabilityConditionWasViolated() const {
  return _stabilityConditionWasViolated;
}

bool exahype::solvers::ADERDGSolver::isValidCellDescriptionIndex(
      const int cellDescriptionsIndex) const {
    return Heap::getInstance().isValidIndex(cellDescriptionsIndex);
  }

int exahype::solvers::ADERDGSolver::tryGetElement(
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

exahype::solvers::Solver::SubcellPosition
exahype::solvers::ADERDGSolver::computeSubcellPositionOfCellOrAncestor(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription =
      getCellDescription(cellDescriptionsIndex,element);

  return
      exahype::amr::computeSubcellPositionOfCellOrAncestor
      <CellDescription,Heap>(cellDescription);
}

///////////////////////////////////
// CELL-LOCAL MESH REFINEMENT
///////////////////////////////////
void exahype::solvers::ADERDGSolver::ensureConsistencyOfParentIndex(
    CellDescription& fineGridCellDescription,
    const int coarseGridCellDescriptionsIndex,
    const int solverNumber) {
  fineGridCellDescription.setParentIndex(multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex);
  int coarseGridElement = tryGetElement(coarseGridCellDescriptionsIndex,solverNumber);
  if (coarseGridElement!=exahype::solvers::Solver::NotFound) {
    fineGridCellDescription.setParentIndex(coarseGridCellDescriptionsIndex);
    // In this case, we are not at a master worker boundary only our parent is.
    #ifdef Parallel
    fineGridCellDescription.setHasToHoldDataForMasterWorkerCommunication(false);
    #endif
  }
}

bool exahype::solvers::ADERDGSolver::markForRefinement(
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
  const int fineGridCellElement =
      tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);

  // Fine grid cell based adaptive mesh refinement operations.
  if (fineGridCellElement!=exahype::solvers::Solver::NotFound) {
    CellDescription& fineGridCellDescription = Heap::getInstance().getData(
        fineGridCell.getCellDescriptionsIndex())[fineGridCellElement];

    #ifdef Parallel
    ensureConsistencyOfParentIndex(fineGridCellDescription,coarseGridCell.getCellDescriptionsIndex(),solverNumber);
    #endif

    #if defined(Asserts) || defined(Debug)
    const int coarseGridCellElement =
        tryGetElement(coarseGridCell.getCellDescriptionsIndex(),solverNumber);
    #endif
    assertion3(
        coarseGridCellElement==exahype::solvers::Solver::NotFound ||
        fineGridCellDescription.getParentIndex()==coarseGridCell.getCellDescriptionsIndex(),
        fineGridCellDescription.toString(),fineGridCell.toString(),
        coarseGridCell.toString()); // see mergeCellDescriptionsWithRemoteData.

    return markForRefinement(fineGridCellDescription);
  }

  return false;
}

///////////////////////////////////
// CELL-LOCAL MESH REFINEMENT
///////////////////////////////////
bool exahype::solvers::ADERDGSolver::updateStateInEnterCell(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    const bool initialGrid,
    const int solverNumber) {
  bool refineFineGridCell = false;

  // Fine grid cell based uniform mesh refinement.
  const int fineGridCellElement =
      tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
  if (fineGridCellElement==exahype::solvers::Solver::NotFound &&
      tarch::la::allSmallerEquals(fineGridVerticesEnumerator.getCellSize(),getMaximumMeshSize()) &&
      tarch::la::allGreater(coarseGridVerticesEnumerator.getCellSize(),getMaximumMeshSize())) {
    logDebug("updateStateInEnterCell(...)","Add new uniform grid cell with offset "<<fineGridVerticesEnumerator.getVertexPosition() <<
            " at level "<<fineGridVerticesEnumerator.getLevel());

    addNewCell(fineGridCell,fineGridVertices,fineGridVerticesEnumerator,
               multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex,
               solverNumber); // TODO(Dominic): Can directly refine if we directly evaluate initial conditions here.
  // Fine grid cell based adaptive mesh refinement operations.
  } else if (fineGridCellElement!=exahype::solvers::Solver::NotFound) {
    CellDescription& fineGridCellDescription =
        getCellDescription(fineGridCell.getCellDescriptionsIndex(),fineGridCellElement);
    #ifdef Parallel
    ensureConsistencyOfParentIndex(fineGridCellDescription,coarseGridCell.getCellDescriptionsIndex(),solverNumber);
    #endif
    #if defined(Asserts) || defined(Debug)
    const int coarseGridCellElement =
        tryGetElement(coarseGridCell.getCellDescriptionsIndex(),solverNumber);
    #endif
    assertion3(coarseGridCellElement==exahype::solvers::Solver::NotFound ||
        fineGridCellDescription.getParentIndex()==coarseGridCell.getCellDescriptionsIndex(),
        fineGridCellDescription.toString(),fineGridCell.toString(),
        coarseGridCell.toString()); // see mergeCellDescriptionsWithRemoteData.

    // actions requiring adjacency info
    const tarch::la::Vector<TWO_POWER_D_TIMES_TWO_POWER_D,int>&
      indicesAdjacentToFineGridVertices =
          VertexOperations::readCellDescriptionsIndex(
                          fineGridVerticesEnumerator,fineGridVertices);

    if (multiscalelinkedcell::adjacencyInformationIsConsistent(
        indicesAdjacentToFineGridVertices)) {
      const tarch::la::Vector<THREE_POWER_D, int> neighbourCellDescriptionIndices =
          multiscalelinkedcell::getIndicesAroundCell(
              indicesAdjacentToFineGridVertices);

      // TODO(Dominic): Does this information indicate that a cell adjacent
      // to a worker/master rank???

      #ifdef Parallel
      fineGridCellDescription.setAdjacentToRemoteRank(
          exahype::Cell::isAdjacentToRemoteRank(fineGridVertices,fineGridVerticesEnumerator));
      #endif

      // Ensure we have allocated enough memory.
      updateHelperStatus(fineGridCellDescription);
      ensureNecessaryMemoryIsAllocated(fineGridCellDescription);
      ensureNoUnnecessaryMemoryIsAllocated(fineGridCellDescription);

      // marking for augmentation
      updateAugmentationStatus(fineGridCellDescription);
      refineFineGridCell |= // TODO(Dominic): Change to the template version.
          markForAugmentation(
              fineGridCellDescription,
              neighbourCellDescriptionIndices,
              fineGridCell.isAssignedToRemoteRank());
    }
  }

  // Coarse grid cell based adaptive mesh refinement operations.
  // Add new cells to the grid and veto erasing or deaugmenting children
  // requests if there are cells on the fine level.
  const int coarseGridCellElement =
      tryGetElement(coarseGridCell.getCellDescriptionsIndex(),solverNumber);
  if (coarseGridCellElement!=exahype::solvers::Solver::NotFound) {
    CellDescription& coarseGridCellDescription = getCellDescription(
        coarseGridCell.getCellDescriptionsIndex(),coarseGridCellElement);

    vetoErasingOrDeaugmentingChildrenRequest(
        coarseGridCellDescription,
        fineGridCell.getCellDescriptionsIndex());

    // TODO(Dominic): Pass limiter status flag down to the new cell
    addNewDescendantIfAugmentingRequested(
            fineGridCell,fineGridVertices,fineGridVerticesEnumerator,
            coarseGridCellDescription,coarseGridCell.getCellDescriptionsIndex());
    addNewCellIfRefinementRequested(
        fineGridCell,fineGridVertices,fineGridVerticesEnumerator,fineGridPositionOfCell,
        coarseGridCellDescription,coarseGridCell.getCellDescriptionsIndex(),
        initialGrid);
  }

  return refineFineGridCell;
}

bool exahype::solvers::ADERDGSolver::markForRefinement(
    CellDescription& fineGridCellDescription) {
  bool refineFineGridCell = false;
  bool vetoErasing        = true;

  double* solution = 0;
  exahype::solvers::Solver::RefinementControl refinementControl;

  switch (fineGridCellDescription.getType()) {
    case CellDescription::Cell:
      switch (fineGridCellDescription.getRefinementEvent()) {
        case CellDescription::RefiningRequested:
          refineFineGridCell = true;
          break;
        case CellDescription::None:
        case CellDescription::AugmentingRequested:
          solution = DataHeap::getInstance().getData(fineGridCellDescription.getSolution()).data();
          refinementControl =
              refinementCriterion(
                  solution,fineGridCellDescription.getOffset()+0.5*fineGridCellDescription.getSize(),
                  fineGridCellDescription.getSize(),
                  fineGridCellDescription.getCorrectorTimeStamp()+fineGridCellDescription.getCorrectorTimeStepSize(),
                  fineGridCellDescription.getLevel());

          switch (refinementControl) {
            case exahype::solvers::Solver::RefinementControl::Refine:
              if (fineGridCellDescription.getLevel()<getMaximumAdaptiveMeshLevel()) {
                fineGridCellDescription.setRefinementEvent(CellDescription::RefiningRequested);
                refineFineGridCell = true;
              }
              break;
            case exahype::solvers::Solver::RefinementControl::Erase:
              vetoErasing = false;
              break;
            default:
              break;
          }
          break;
        default:
          break;
      }
      break;
    case CellDescription::Ancestor:
      if (
          fineGridCellDescription.getRefinementEvent()==CellDescription::None
          && !fineGridCellDescription.getNewlyCreated() // we do not erase newly created Ancestors
      ) {
        fineGridCellDescription.setRefinementEvent(CellDescription::ErasingChildrenRequested);

        /*  TODO(Dominic): Add to docu:
         * If this refinement event is set,
         * the parent Ancestor asks its
         * children if they want to be erased. If not,
         * the children change the RefinementEvent
         * of the parent to None. If so,
         * they leave the parent's RefinementEvent
         * unchanged.
         */
      }
      break;
    default:
      // do nothing
      break;
  }

  if (vetoErasing) {
    int coarseGridCellElement = tryGetElement(fineGridCellDescription.getParentIndex(),
                                              fineGridCellDescription.getSolverNumber());
    if (coarseGridCellElement!=exahype::solvers::Solver::NotFound) {
      auto& coarseGridCellDescription = getCellDescription(fineGridCellDescription.getParentIndex(),
                                                           coarseGridCellElement);

      if (coarseGridCellDescription.getRefinementEvent()==CellDescription::ErasingChildrenRequested ||
          coarseGridCellDescription.getRefinementEvent()==CellDescription::ChangeChildrenToDescendantsRequested) {
        coarseGridCellDescription.setRefinementEvent(CellDescription::None);

        assertion1(coarseGridCellDescription.getType()==CellDescription::Ancestor,
                   coarseGridCellDescription.toString());
      }
    }
  }

  return refineFineGridCell;
}

bool exahype::solvers::ADERDGSolver::markForAugmentation(
    CellDescription& fineGridCellDescription,
    const tarch::la::Vector<THREE_POWER_D, int>& neighbourCellDescriptionIndices,
    const bool assignedToRemoteRank) {
  const int coarseGridElement = tryGetElement(
      fineGridCellDescription.getParentIndex(),
      fineGridCellDescription.getSolverNumber());
  // First check if we can set the deaugmenting children requested triggered event of the coarse grid cell
  // to a "real" deaugmenting children requested event.
  // TODO(Dominic): thread-safety
  if (coarseGridElement!=exahype::solvers::Solver::NotFound) {
    CellDescription& coarseGridCellDescription =
        getCellDescription(fineGridCellDescription.getParentIndex(),coarseGridElement);
    if (coarseGridCellDescription.getRefinementEvent()==CellDescription::DeaugmentingChildrenRequestedTriggered) {
      coarseGridCellDescription.setRefinementEvent(CellDescription::DeaugmentingChildrenRequested);
    }
  }

  // Then do fine grid cell stuff
  bool refineFineGridCell = false;

  // Further augment or deaugment cells and descendants if no other event
  // or an augmentation event has been triggered.
  switch (fineGridCellDescription.getRefinementEvent()) {
  case CellDescription::AugmentingRequested:
    refineFineGridCell = true;
    break;
  case CellDescription::None:
    switch (fineGridCellDescription.getType()) {
    case CellDescription::Cell:
    case CellDescription::Descendant:
      if (fineGridCellDescription.getAugmentationStatus()==0) {
        fineGridCellDescription.setRefinementEvent(CellDescription::DeaugmentingChildrenRequestedTriggered);
      }
      if (!fineGridCellDescription.getIsAugmented() &&
          fineGridCellDescription.getAugmentationStatus()>0) {
        fineGridCellDescription.setRefinementEvent(CellDescription::AugmentingRequested);
        refineFineGridCell = true;
      }
      break;
    default:
      break;
    }
    break;
    default:
      break;
  }

  // And check if we must veto the deaugmenting children request of the parent.
  if (
      fineGridCellDescription.getIsAugmented() ||
      fineGridCellDescription.getAugmentationStatus()>0
  ) {
    const int coarseGridCellElement = tryGetElement(fineGridCellDescription.getParentIndex(),
                                              fineGridCellDescription.getSolverNumber());
    if (coarseGridCellElement!=exahype::solvers::Solver::NotFound) {
      auto& coarseGridCellDescription = getCellDescription(fineGridCellDescription.getParentIndex(),
                                                           coarseGridCellElement);
      if (coarseGridCellDescription.getRefinementEvent()==CellDescription::DeaugmentingChildrenRequested) {
        coarseGridCellDescription.setRefinementEvent(CellDescription::None);

        assertion1(fineGridCellDescription.getType()==CellDescription::Descendant,
                   fineGridCellDescription.toString());
        assertion1(coarseGridCellDescription.getType()==CellDescription::Cell ||
                   coarseGridCellDescription.getType()==CellDescription::Descendant,
                   coarseGridCellDescription.toString());
      }
    }
  }

  return refineFineGridCell;
}

void exahype::solvers::ADERDGSolver::vetoErasingOrDeaugmentingChildrenRequest(
    CellDescription& coarseGridCellDescription,
    const int fineGridCellDescriptionsIndex) {
  const int fineGridCellElement =
      tryGetElement(fineGridCellDescriptionsIndex,
          coarseGridCellDescription.getSolverNumber());
  if (fineGridCellElement!=exahype::solvers::Solver::NotFound) {
    CellDescription& fineGridCellDescription =
       getCellDescription(fineGridCellDescriptionsIndex,fineGridCellElement);
    if (fineGridCellDescription.getIsAugmented()
        ||
        fineGridCellDescription.getRefinementEvent()==CellDescription::RefinementEvent::Augmenting
        ||
        fineGridCellDescription.getRefinementEvent()==CellDescription::RefinementEvent::AugmentingRequested) {
      switch (coarseGridCellDescription.getRefinementEvent()) {
      case CellDescription::DeaugmentingChildrenRequested:
        assertion1(coarseGridCellDescription.getType()==CellDescription::Descendant,coarseGridCellDescription.toString());
        coarseGridCellDescription.setRefinementEvent(CellDescription::None);
        break;
      case CellDescription::ErasingChildrenRequested:
        assertion1(coarseGridCellDescription.getType()==CellDescription::Ancestor,
            coarseGridCellDescription.toString());

        coarseGridCellDescription.setRefinementEvent(
            CellDescription::ChangeChildrenToDescendantsRequested);
        break;
      default:
        break;
      }
    }
  }


  // TODO(Dominic): Old code for reference
//  const int coarseGridCellParentElement = tryGetElement(coarseGridCellDescription.getParentIndex(),
//                                                  coarseGridCellDescription.getSolverNumber());
//  const int fineGridCellElement = tryGetElement(fineGridCellDescriptionsIndex,
//                                          coarseGridCellDescription.getSolverNumber());
//  if (fineGridCellElement!=exahype::solvers::Solver::NotFound &&
//      coarseGridCellParentElement!=exahype::solvers::Solver::NotFound) {
//    CellDescription& coarseGridCellDescriptionParent =
//        getCellDescription(coarseGridCellDescription.getParentIndex(),coarseGridCellParentElement);
//
//        switch (coarseGridCellDescriptionParent.getRefinementEvent()) {
//          case CellDescription::DeaugmentingChildrenRequested:
//            assertion1(coarseGridCellDescription.getType()==CellDescription::Descendant,coarseGridCellDescription.toString());
//            coarseGridCellDescriptionParent.setRefinementEvent(CellDescription::None);
//            break;
//          case CellDescription::ErasingChildrenRequested:
//            assertion1(coarseGridCellDescription.getType()==CellDescription::Cell,
//                       coarseGridCellDescription.toString());
//
//            coarseGridCellDescriptionParent.setRefinementEvent(
//                CellDescription::ChangeChildrenToDescendantsRequested);
//            break;
//          default:
//            break;
//        }
//  }
}

void exahype::solvers::ADERDGSolver::addNewCell(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    const int coarseGridCellDescriptionsIndex,
    const int solverNumber) {
  logDebug("addNewCell(...)","Add new grid cell with center "<<fineGridVerticesEnumerator.getCellCenter() <<
              " at level "<<fineGridVerticesEnumerator.getLevel());

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

void exahype::solvers::ADERDGSolver::addNewDescendantIfAugmentingRequested(
     exahype::Cell& fineGridCell,
     exahype::Vertex* const fineGridVertices,
     const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
     CellDescription& coarseGridCellDescription,
     const int coarseGridCellDescriptionsIndex) {
  if (coarseGridCellDescription.getRefinementEvent()==CellDescription::AugmentingRequested ||
      coarseGridCellDescription.getRefinementEvent()==CellDescription::Augmenting) {
    assertion1(coarseGridCellDescription.getType()==CellDescription::Cell ||
               coarseGridCellDescription.getType()==CellDescription::Descendant,
               coarseGridCellDescription.toString());
    const int fineGridElement =
        tryGetElement(fineGridCell.getCellDescriptionsIndex(),
                      coarseGridCellDescription.getSolverNumber());

    coarseGridCellDescription.setRefinementEvent(CellDescription::None);
    if (fineGridElement==exahype::solvers::Solver::NotFound) {
      fineGridCell.addNewCellDescription( // (EmptyDescendant),None
          coarseGridCellDescription.getSolverNumber(),
          CellDescription::Descendant,
          CellDescription::None,
          fineGridVerticesEnumerator.getLevel(),
          coarseGridCellDescriptionsIndex,
          fineGridVerticesEnumerator.getCellSize(),
          fineGridVerticesEnumerator.getVertexPosition());
      const int fineGridElement =
          tryGetElement(fineGridCell.getCellDescriptionsIndex(),
                        coarseGridCellDescription.getSolverNumber());
      CellDescription& fineGridCellDescription =
          getCellDescription(fineGridCell.getCellDescriptionsIndex(),fineGridElement);
      fineGridCellDescription.setIsInside(coarseGridCellDescription.getIsInside());

      coarseGridCellDescription.setRefinementEvent(CellDescription::Augmenting);
    } else if (fineGridElement!=exahype::solvers::Solver::NotFound &&
               coarseGridCellDescription.getRefinementEvent()==CellDescription::AugmentingRequested){
      /**
       * Reset an augmentation request if the child cell does hold
       * a Descendant or EmptyDescendant cell description with
       * the same solver number.
       *
       * This scenario occurs if an augmentation request is triggered in
       * enterCell() or mergeWithNeighbourMetadata(...).
       *
       * A similar scenario can never occur for refinement requests
       * since only cell descriptions of type Cell can be refined.
       * Ancestors and EmptyAncestors can never request refinement.
       * TODO(Dominic): Add to docu.
       */
      coarseGridCellDescription.setRefinementEvent(CellDescription::None);

      #if defined(Debug) || defined(Asserts)
      CellDescription& fineGridCellDescription =
                getCellDescription(fineGridCell.getCellDescriptionsIndex(),fineGridElement);
      #endif
      assertion1(fineGridCellDescription.getType()==CellDescription::Descendant,
                 fineGridCellDescription.toString());
    }
  }
}

void exahype::solvers::ADERDGSolver::addNewCellIfRefinementRequested(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    const tarch::la::Vector<DIMENSIONS,int>& fineGridPositionOfCell,
    CellDescription& coarseGridCellDescription,
    const int coarseGridCellDescriptionsIndex,
    const bool initialGrid) {
  if (coarseGridCellDescription.getRefinementEvent()==CellDescription::RefiningRequested ||
      coarseGridCellDescription.getRefinementEvent()==CellDescription::Refining) {
    assertion1(coarseGridCellDescription.getType()==CellDescription::Cell,
               coarseGridCellDescription.toString());
    int fineGridCellElement =
        tryGetElement(fineGridCell.getCellDescriptionsIndex(),
                      coarseGridCellDescription.getSolverNumber());

    if (fineGridCellElement==exahype::solvers::Solver::NotFound) {
      addNewCell(fineGridCell,fineGridVertices,fineGridVerticesEnumerator,
                 coarseGridCellDescriptionsIndex,
                 coarseGridCellDescription.getSolverNumber());
      fineGridCellElement =
          tryGetElement(fineGridCell.getCellDescriptionsIndex(),
                        coarseGridCellDescription.getSolverNumber());
      CellDescription& fineGridCellDescription =
          getCellDescription(fineGridCell.getCellDescriptionsIndex(),fineGridCellElement);
      fineGridCellDescription.setIsInside(coarseGridCellDescription.getIsInside());

      prolongateVolumeData(
          fineGridCellDescription,coarseGridCellDescription,fineGridPositionOfCell,initialGrid);
    } else {
      CellDescription& fineGridCellDescription = getCellDescription(fineGridCell.getCellDescriptionsIndex(),fineGridCellElement);
      assertion2(fineGridCellDescription.getType()==CellDescription::Descendant,
                 fineGridCellDescription.toString(),coarseGridCellDescription.toString());
      assertion2(fineGridCellDescription.getParentIndex()==coarseGridCellDescriptionsIndex,
                 fineGridCellDescription.toString(),coarseGridCellDescriptionsIndex);

      fineGridCellDescription.setType(CellDescription::Cell);
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      fineGridCellDescription.setHelperStatus(MaximumHelperStatus);
      fineGridCellDescription.setFacewiseHelperStatus(MaximumHelperStatus); // implicit conversion
      ensureNecessaryMemoryIsAllocated(fineGridCellDescription);

      prolongateVolumeData(
          fineGridCellDescription,coarseGridCellDescription,fineGridPositionOfCell,initialGrid);
    }

    coarseGridCellDescription.setRefinementEvent(CellDescription::Refining);
  }
}

void exahype::solvers::ADERDGSolver::prolongateVolumeData(
    CellDescription&       fineGridCellDescription,
    const CellDescription& coarseGridCellDescription,
  const tarch::la::Vector<DIMENSIONS, int>& subcellIndex,
  const bool initialGrid) {
  const int levelFine = fineGridCellDescription.getLevel();
  const int levelCoarse = coarseGridCellDescription.getLevel();
  assertion(levelCoarse < levelFine);

  // current solution
  double* solutionFine   = DataHeap::getInstance().getData(
      fineGridCellDescription.getSolution()).data();
  double* solutionCoarse = DataHeap::getInstance().getData(
      coarseGridCellDescription.getSolution()).data();
  volumeUnknownsProlongation(
      solutionFine,solutionCoarse,
      levelCoarse,levelFine,
      subcellIndex);

  // previous solution
  assertion(DataHeap::getInstance().isValidIndex(fineGridCellDescription.getPreviousSolution()));
  double* previousSolutionFine   = DataHeap::getInstance().getData(
      fineGridCellDescription.getPreviousSolution()).data();
  double* previousSolutionCoarse = DataHeap::getInstance().getData(
      coarseGridCellDescription.getPreviousSolution()).data();
  volumeUnknownsProlongation(
      previousSolutionFine,previousSolutionCoarse,
      levelCoarse,levelFine,
      subcellIndex);

  fineGridCellDescription.setCorrectorTimeStamp(coarseGridCellDescription.getCorrectorTimeStamp());
  fineGridCellDescription.setPredictorTimeStamp(coarseGridCellDescription.getPredictorTimeStamp());
  fineGridCellDescription.setCorrectorTimeStepSize(coarseGridCellDescription.getCorrectorTimeStepSize());
  fineGridCellDescription.setPredictorTimeStepSize(coarseGridCellDescription.getPredictorTimeStepSize());

  // TODO Dominic: This is a little inconsistent since I orignially tried to hide
  // the limiting from the pure ADER-DG scheme
  fineGridCellDescription.setPreviousLimiterStatus(CellDescription::LimiterStatus::Ok);
  fineGridCellDescription.setLimiterStatus(CellDescription::LimiterStatus::Ok);

  // TODO Dominic:
  // Add to docu. We disregarded the following since it can lead to inconsistencies.
  // During the inital mesh build where we only refine
  // according to the PAD, we don't want to have a too broad refined area.
  // We thus do not flag children cells with troubled
  //
  if (!initialGrid
      &&
      coarseGridCellDescription.getLimiterStatus()==CellDescription::LimiterStatus::Troubled) {
    fineGridCellDescription.setLimiterStatus(CellDescription::LimiterStatus::Troubled);
    fineGridCellDescription.setIterationsToCureTroubledCell(coarseGridCellDescription.getIterationsToCureTroubledCell());
  }
  overwriteFacewiseLimiterStatus(fineGridCellDescription);
}

bool exahype::solvers::ADERDGSolver::attainedStableState(
        exahype::Cell& fineGridCell,
        exahype::Vertex* const fineGridVertices,
        const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
        const int solverNumber) const {
  const int element = tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
  if (element!=exahype::solvers::Solver::NotFound) {
    CellDescription& cellDescription = getCellDescription(fineGridCell.getCellDescriptionsIndex(),element);

      const tarch::la::Vector<TWO_POWER_D_TIMES_TWO_POWER_D,int>&
        indicesAdjacentToFineGridVertices =
            VertexOperations::readCellDescriptionsIndex(
                fineGridVerticesEnumerator,fineGridVertices);

      return (cellDescription.getRefinementEvent()==CellDescription::RefinementEvent::None
              && multiscalelinkedcell::adjacencyInformationIsConsistent(indicesAdjacentToFineGridVertices));
  }

  return true;
}

bool exahype::solvers::ADERDGSolver::updateStateInLeaveCell(
    exahype::Cell& fineGridCell,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
     const int solverNumber) {
  const int fineGridCellElement =
      tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
  if (fineGridCellElement!=exahype::solvers::Solver::NotFound) {
    CellDescription& fineGridCellDescription = getCellDescription(
            fineGridCell.getCellDescriptionsIndex(),fineGridCellElement);

    // start or finish collective operations
    startOrFinishCollectiveRefinementOperations(fineGridCellDescription);

    const int coarseGridCellElement =
        tryGetElement(coarseGridCell.getCellDescriptionsIndex(),solverNumber);
    if (coarseGridCellElement!=exahype::solvers::Solver::NotFound) {
      assertion3(fineGridCellDescription.getParentIndex()==coarseGridCell.getCellDescriptionsIndex(),
                 fineGridCellDescription.toString(),fineGridCell.toString(),
                 coarseGridCell.toString()); // see mergeCellDescriptionsWithRemoteData.

      CellDescription& coarseGridCellDescription = getCellDescription(
          fineGridCellDescription.getParentIndex(),coarseGridCellElement);
      assertion1(fineGridCellDescription.getSolverNumber()==
          coarseGridCellDescription.getSolverNumber(),
                     fineGridCellDescription.toString());

      bool eraseOfFineGridCellRequested =
          eraseCellDescriptionIfNecessary(
              fineGridCell.getCellDescriptionsIndex(),
              fineGridCellElement,
              fineGridPositionOfCell,
              coarseGridCellDescription);

      return eraseOfFineGridCellRequested;
    }

    return false;
  }

//  return fineGridVerticesEnumerator.getLevel()>getCoarsestMeshLevel();
  return false;
}

void exahype::solvers::ADERDGSolver::prepareVolumeDataRestriction(
    CellDescription& cellDescription) const {
  double* solution =
      DataHeap::getInstance().getData(cellDescription.getSolution()).data();
  std::fill_n(solution,_dataPointsPerCell,0.0);
  double* previousSolution =
      DataHeap::getInstance().getData(cellDescription.getPreviousSolution()).data();
  std::fill_n(previousSolution,_dataPointsPerCell,0.0);
}

void exahype::solvers::ADERDGSolver::startOrFinishCollectiveRefinementOperations(
     CellDescription& fineGridCellDescription) {
  switch (fineGridCellDescription.getRefinementEvent()) {
    case CellDescription::Refining:
      assertion1(fineGridCellDescription.getType()==CellDescription::Cell,
                 fineGridCellDescription.toString());
      fineGridCellDescription.setType(CellDescription::Ancestor);
      fineGridCellDescription.setAugmentationStatus(MaximumAugmentationStatus);
      fineGridCellDescription.setFacewiseAugmentationStatus(MaximumAugmentationStatus); // implicit conversion
      fineGridCellDescription.setHelperStatus(0);
      fineGridCellDescription.setFacewiseHelperStatus(0); // implicit conversion
      fineGridCellDescription.setIsAugmented(false); // since we might replace descendants with cells
      fineGridCellDescription.setNewlyCreated(true);
      ensureNoUnnecessaryMemoryIsAllocated(fineGridCellDescription);
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      break;
    case CellDescription::ChangeChildrenToDescendantsRequested:
      fineGridCellDescription.setType(CellDescription::Cell);
      fineGridCellDescription.setAugmentationStatus(0);
      fineGridCellDescription.setFacewiseAugmentationStatus(0); // implicit conversion
      fineGridCellDescription.setHelperStatus(MaximumHelperStatus);
      fineGridCellDescription.setFacewiseHelperStatus(MaximumHelperStatus); // implicit conversion
      ensureNecessaryMemoryIsAllocated(fineGridCellDescription);
      prepareVolumeDataRestriction(fineGridCellDescription);
      fineGridCellDescription.setRefinementEvent(CellDescription::ChangeChildrenToDescendants);
      break;
    case CellDescription::ChangeChildrenToDescendants:
      fineGridCellDescription.setIsAugmented(true);
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      break;
    case CellDescription::ErasingChildrenRequested:
      fineGridCellDescription.setType(CellDescription::Cell);
      fineGridCellDescription.setAugmentationStatus(0);
      fineGridCellDescription.setFacewiseAugmentationStatus(0); // implicit conversion
      fineGridCellDescription.setHelperStatus(MaximumHelperStatus);
      fineGridCellDescription.setFacewiseHelperStatus(MaximumHelperStatus); // implicit conversion
      ensureNecessaryMemoryIsAllocated(fineGridCellDescription);
      prepareVolumeDataRestriction(fineGridCellDescription);
      fineGridCellDescription.setRefinementEvent(CellDescription::ErasingChildren);
      break;
    case CellDescription::ErasingChildren:
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      break;
    case CellDescription::Augmenting:
      fineGridCellDescription.setIsAugmented(true);
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      break;
    case CellDescription::DeaugmentingChildrenRequestedTriggered: // This means we are actually a leaf cell of this solver.
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      break;
    case CellDescription::DeaugmentingChildrenRequested:
      fineGridCellDescription.setRefinementEvent(CellDescription::DeaugmentingChildren);
      break;
    case CellDescription::DeaugmentingChildren:
      fineGridCellDescription.setIsAugmented(false);
      fineGridCellDescription.setRefinementEvent(CellDescription::None);
      break;
    default:
      break;
  }
}

bool exahype::solvers::ADERDGSolver::eraseCellDescriptionIfNecessary(
    const int cellDescriptionsIndex,
    const int fineGridCellElement,
    const tarch::la::Vector<DIMENSIONS,int>& fineGridPositionOfCell,
    CellDescription& coarseGridCellDescription) {
  if (coarseGridCellDescription.getRefinementEvent()==CellDescription::ErasingChildren) {
    CellDescription& fineGridCellDescription = getCellDescription(
          cellDescriptionsIndex,fineGridCellElement);

    // restrict values.
    restrictVolumeData(
        coarseGridCellDescription,
        fineGridCellDescription,
        fineGridPositionOfCell);
    // TODO(Dominic): Reconsider for anarchic time stepping.
//    coarseGridCellDescription.setCorrectorTimeStamp(fineGridCellDescription.getCorrectorTimeStamp());
//    coarseGridCellDescription.setPredictorTimeStamp(fineGridCellDescription.getPredictorTimeStamp());

    // erase cell description // or change to descendant
    fineGridCellDescription.setType(CellDescription::Erased);
    fineGridCellDescription.setHelperStatus(0);
    fineGridCellDescription.setFacewiseHelperStatus(0); // implicit conversion
    ensureNoUnnecessaryMemoryIsAllocated(fineGridCellDescription);

    getCellDescriptions(cellDescriptionsIndex).erase(
        getCellDescriptions(cellDescriptionsIndex).begin()+fineGridCellElement);

    return true;
  } else if (coarseGridCellDescription.getRefinementEvent()==CellDescription::ChangeChildrenToDescendants) {
    CellDescription& fineGridCellDescription = getCellDescription(
        cellDescriptionsIndex,fineGridCellElement);

    // restrict values.
    restrictVolumeData(
        coarseGridCellDescription,
        fineGridCellDescription,
        fineGridPositionOfCell);
    // TODO(Dominic): Reconsider for anarchic time stepping.
//    coarseGridCellDescription.setCorrectorTimeStamp(fineGridCellDescription.getCorrectorTimeStamp());
//    coarseGridCellDescription.setPredictorTimeStamp(fineGridCellDescription.getPredictorTimeStamp());

    // erase cell description // or change to descendant
    fineGridCellDescription.setType(CellDescription::Descendant);
    fineGridCellDescription.setHelperStatus(0);
    fineGridCellDescription.setFacewiseHelperStatus(0); // implicit conversion
    ensureNoUnnecessaryMemoryIsAllocated(fineGridCellDescription);

    return false;
  } else if (coarseGridCellDescription.getRefinementEvent()==CellDescription::DeaugmentingChildren) {
    CellDescription& fineGridCellDescription = getCellDescription(
          cellDescriptionsIndex,fineGridCellElement);

    fineGridCellDescription.setType(CellDescription::Erased);
    fineGridCellDescription.setHelperStatus(0);
    fineGridCellDescription.setFacewiseHelperStatus(0); // implicit conversion
    ensureNoUnnecessaryMemoryIsAllocated(fineGridCellDescription);

    getCellDescriptions(cellDescriptionsIndex).erase(
        getCellDescriptions(cellDescriptionsIndex).begin()+fineGridCellElement);
    return true;
  }

  return false;
}

void exahype::solvers::ADERDGSolver::restrictVolumeData(
    CellDescription&       coarseGridCellDescription,
    const CellDescription& fineGridCellDescription,
    const tarch::la::Vector<DIMENSIONS, int>& subcellIndex) {
//  assertion1(coarseGridCellDescription.getLimiterStatus()==CellDescription::LimiterStatus::Ok,
//      coarseGridCellDescription.toString()); // TODO(Dominic): Does not always apply see veto
  assertion1(fineGridCellDescription.getLimiterStatus()==CellDescription::LimiterStatus::Ok,
        fineGridCellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(
      fineGridCellDescription.getSolution()),fineGridCellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(
      coarseGridCellDescription.getSolution()),coarseGridCellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(
      fineGridCellDescription.getPreviousSolution()),fineGridCellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(
      coarseGridCellDescription.getPreviousSolution()),coarseGridCellDescription.toString());

  const int levelFine  = fineGridCellDescription.getLevel();
  const int levelCoarse = coarseGridCellDescription.getLevel();
  assertion(levelCoarse < levelFine);

  // restrict current solution
  double* solutionFine   = DataHeap::getInstance().getData(
      fineGridCellDescription.getSolution()).data();
  double* solutionCoarse = DataHeap::getInstance().getData(
      coarseGridCellDescription.getSolution()).data();
  volumeUnknownsRestriction(
      solutionCoarse,solutionFine,
      levelCoarse,levelFine,
      subcellIndex);

  // restrict next solution
  double* previousSolutionFine   = DataHeap::getInstance().getData(
      fineGridCellDescription.getPreviousSolution()).data();
  double* previousSolutionCoarse = DataHeap::getInstance().getData(
      coarseGridCellDescription.getPreviousSolution()).data();
  volumeUnknownsRestriction(
      previousSolutionCoarse,previousSolutionFine,
      levelCoarse,levelFine,
      subcellIndex);

  // Reset the min and max
  const int numberOfObservables = getDMPObservables();
  if (numberOfObservables>0) {
    double* solutionMin = DataHeap::getInstance().getData(
        coarseGridCellDescription.getSolutionMin()).data();
    std::fill_n(solutionMin,DIMENSIONS_TIMES_TWO*numberOfObservables,
        std::numeric_limits<double>::max());
    double* solutionMax = DataHeap::getInstance().getData(
        coarseGridCellDescription.getSolutionMax()).data();
    std::fill_n(solutionMax,DIMENSIONS_TIMES_TWO*numberOfObservables,
        -std::numeric_limits<double>::max()); // Be aware of "-"
  }

  // TODO(Dominic): What to do with the time step data for anarchic time stepping?
  // Tobias proposed some waiting procedure. Until they all have reached
  // a certain time level.
//  coarseGridCellDescription.setCorrectorTimeStamp(fineGridCellDescription.getCorrectorTimeStamp());
//  coarseGridCellDescription.setPredictorTimeStamp(fineGridCellDescription.getPredictorTimeStamp());
//  coarseGridCellDescription.setCorrectorTimeStepSize(fineGridCellDescription.getCorrectorTimeStepSize());
//  coarseGridCellDescription.setPredictorTimeStepSize(fineGridCellDescription.getPredictorTimeStepSize());
}

void exahype::solvers::ADERDGSolver::finaliseStateUpdates(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const int solverNumber) {
  const int element =
      tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
  if (element!=exahype::solvers::Solver::NotFound) {
    CellDescription& cellDescription = getCellDescription(fineGridCell.getCellDescriptionsIndex(),element);

    cellDescription.setNewlyCreated(false);
  }
}

////////////////////////////////////////
// CELL-LOCAL
////////////////////////////////////////
void exahype::solvers::ADERDGSolver::validateNoNansInADERDGSolver(
  const CellDescription& cellDescription,
  const std::string& methodTraceOfCaller
) {
  int dataPerCell             = 0;
  int unknownsPerCell         = 0;
  int dataPerCellBoundary     = 0;
  int unknownsPerCellBoundary = 0;

  #if defined(Debug) || defined(Asserts)
  double* luh = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
  double* lduh = DataHeap::getInstance().getData(cellDescription.getUpdate()).data();

  double* lQhbnd = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
  double* lFhbnd = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();

  dataPerCell             = getDataPerCell();
  unknownsPerCell         = getUnknownsPerCell();

  dataPerCellBoundary     = getBndTotalSize();
  unknownsPerCellBoundary = getBndFluxTotalSize();
  #endif

  assertion1(getType()==exahype::solvers::Solver::Type::ADERDG,cellDescription.toString());

  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getSolution()),cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getUpdate()),cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor()),cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getFluctuation()),cellDescription.toString());

  for (int i=0; i<dataPerCell; i++) {
    assertion4(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(luh[i]),
        cellDescription.toString(),toString(),methodTraceOfCaller,i);
  }

  for (int i=0; i<unknownsPerCell; i++) {
    assertion4(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(lduh[i]),
          cellDescription.toString(),toString(),methodTraceOfCaller,i);
  }

  for (int i=0; i<dataPerCellBoundary; i++) {
    assertion4(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(lQhbnd[i]),
          cellDescription.toString(),toString(),methodTraceOfCaller,i);
  }

  for (int i=0; i<unknownsPerCellBoundary; i++) {
    assertion4(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(lFhbnd[i]),
        cellDescription.toString(),toString(),methodTraceOfCaller,i);
  } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
}

bool exahype::solvers::ADERDGSolver::evaluateRefinementCriterionAfterSolutionUpdate(
      const int cellDescriptionsIndex,
      const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Type::Cell &&
      cellDescription.getLevel()<getMaximumAdaptiveMeshLevel()) {
    const double* solution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
    RefinementControl refinementControl = refinementCriterion(
                      solution,cellDescription.getOffset()+0.5*cellDescription.getSize(),
                      cellDescription.getSize(),
                      cellDescription.getCorrectorTimeStamp()+cellDescription.getCorrectorTimeStepSize(),
                      cellDescription.getLevel());

    // TODO(Dominic): Set cell description refinement events? Yes or no?
    return refinementControl==RefinementControl::Refine;
  }

  return false;
}

void exahype::solvers::ADERDGSolver::performPredictionAndVolumeIntegral(
    exahype::records::ADERDGCellDescription& cellDescription,
    double** tempSpaceTimeUnknowns,
    double** tempSpaceTimeFluxUnknowns,
    double*  tempUnknowns,
    double*  tempFluxUnknowns,
    double*  tempStateSizedVector,
    double*  tempPointForceSources) {
  assertion1(cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None,cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getSolution()),cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getUpdate()),cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor()),cellDescription.toString());
  assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getFluctuation()),cellDescription.toString());

  assertion2(std::isfinite(cellDescription.getPredictorTimeStepSize()),
             cellDescription.toString(),toString());
  assertion3(cellDescription.getPredictorTimeStepSize()<
             std::numeric_limits<double>::max(),
             cellDescription.toString(),toString(),tarch::parallel::Node::getInstance().getRank());
  assertion2(cellDescription.getPredictorTimeStepSize()>0,
             cellDescription.toString(),toString());

  assertion2(std::isfinite(cellDescription.getPredictorTimeStamp()),
             cellDescription.toString(),toString());
  assertion2(cellDescription.getPredictorTimeStamp()<
             std::numeric_limits<double>::max(),
             cellDescription.toString(),toString());
  assertion2(cellDescription.getPredictorTimeStamp()>=0,
             cellDescription.toString(),toString());

  assertion2(cellDescription.getLimiterStatus()==tarch::la::max(cellDescription.getFacewiseLimiterStatus()),
      cellDescription.getLimiterStatus(),cellDescription.getFacewiseLimiterStatus());

  // persistent fields
  // volume DoF (basisSize**(DIMENSIONS))
  double* luh  = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
  double* lduh = DataHeap::getInstance().getData(cellDescription.getUpdate()).data();
  // face DoF (basisSize**(DIMENSIONS-1))
  double* lQhbnd = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
  double* lFhbnd = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();

  for (int i=0; i<getUnknownsPerCell(); i++) { // cellDescription.getCorrectorTimeStepSize==0.0 is an initial condition
    assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(luh[i]),cellDescription.toString(),"performPredictionAndVolumeIntegral(...)",i);
  } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.

  if(usePointSource()) { //disable kernel if not needed
      pointSource(cellDescription.getCorrectorTimeStamp() , cellDescription.getCorrectorTimeStepSize(), cellDescription.getOffset()+0.5*cellDescription.getSize(), cellDescription.getSize(), tempPointForceSources); //TODO KD
      // luh, t, dt, cell cell center, cell size, data allocation for forceVect
    }
//TODO JMG move everything to inverseDx and use Peano to get it when Dominic implemente it
#ifdef OPT_KERNELS
  double* dx = &cellDescription.getSize()[0];
#if DIMENSIONS==2
  double inverseDx[2];
#else
  double inverseDx[3];
  inverseDx[2] = 1.0/dx[2];
#endif
  inverseDx[0] = 1.0/dx[0];
  inverseDx[1] = 1.0/dx[1];
  spaceTimePredictor(
      lQhbnd,
      lFhbnd,
      tempSpaceTimeUnknowns,
      tempSpaceTimeFluxUnknowns,
      tempUnknowns,
      tempFluxUnknowns,
      tempStateSizedVector,
      luh,
      &inverseDx[0], //TODO JMG use cellDescription.getInverseSize() when implemented
      cellDescription.getPredictorTimeStepSize(),
      tempPointForceSources);
      
  // TODO(Future Opt.)
  // Volume integral should be performed using the space time
  // flux unknowns. Something equivalent can also be done for
  // the extrpolated fluxes. Here, we can also perform the
  // time averaging on the fly.
  // Remove the tempFluxUnkowns and tempUnknowns.
  volumeIntegral(
      lduh,
      tempFluxUnknowns,
      &inverseDx[0]); //TODO JMG use cellDescription.getInverseSize() when implemented
#else 
  spaceTimePredictor(
      lQhbnd,
      lFhbnd,
      tempSpaceTimeUnknowns,
      tempSpaceTimeFluxUnknowns,
      tempUnknowns,
      tempFluxUnknowns,
      tempStateSizedVector,
      luh,
      cellDescription.getSize(),
      cellDescription.getPredictorTimeStepSize(),
      tempPointForceSources);

  // TODO(Future Opt.)
  // Volume integral should be performed using the space time
  // flux unknowns. Something equivalent can also be done for
  // the extrpolated fluxes. Here, we can also perform the
  // time averaging on the fly.
  // Remove the tempFluxUnkowns and tempUnknowns.
  volumeIntegral(
      lduh,
      tempFluxUnknowns,
      cellDescription.getSize());
#endif

  for (int i=0; i<getTempSpaceTimeUnknownsSize(); i++) { // cellDescription.getCorrectorTimeStepSize==0.0 is an initial condition
    assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(tempSpaceTimeUnknowns[0][i]),cellDescription.toString(),"performPredictionAndVolumeIntegral(...)",i);
  } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
  for (int i=0; i<getSpaceTimeFluxUnknownsPerCell(); i++) {
    assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(tempSpaceTimeFluxUnknowns[0][i]), cellDescription.toString(),"performPredictionAndVolumeIntegral",i);
  } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.

  #if defined(Debug) || defined(Asserts)
  if(usePaddedData_nVar()) {
    //TODO JMG add assert ignoring padding
  } else {
    for (int i=0; i<getDataPerCell(); i++) {
    assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(tempUnknowns[i]),cellDescription.toString(),"performPredictionAndVolumeIntegral(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
    for (int i=0; i<getFluxUnknownsPerCell(); i++) {
      assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(tempFluxUnknowns[i]),cellDescription.toString(),"performPredictionAndVolumeIntegral(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
  }
  #endif
}

double exahype::solvers::ADERDGSolver::startNewTimeStep(
    const int cellDescriptionsIndex,
    const int element,
    double*   tempEigenvalues) {
  CellDescription& cellDescription =
      exahype::solvers::ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex)[element];

  if (cellDescription.getType()==exahype::records::ADERDGCellDescription::Cell) {
    assertion1(cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None,cellDescription.toString());
    const double* luh = exahype::DataHeap::getInstance().getData(cellDescription.getSolution()).data();

    validateNoNansInADERDGSolver(cellDescription,"startNewTimeStep(...)");
//TODO JMG move everything to inverseDx and use Peano to get it when Dominic implemente it
#ifdef OPT_KERNELS
    double* dx = &cellDescription.getSize()[0];
#if DIMENSIONS==2
    double inverseDx[2];
#else
    double inverseDx[3];
    inverseDx[2] = 1.0/dx[2];
#endif
    inverseDx[0] = 1.0/dx[0];
    inverseDx[1] = 1.0/dx[1];
    double admissibleTimeStepSize =
        stableTimeStepSize(luh,tempEigenvalues,&inverseDx[0]); //TODO JMG use cellDescription.getInverseSize() when implemented
#else
    double admissibleTimeStepSize =
        stableTimeStepSize(luh,tempEigenvalues,cellDescription.getSize());
#endif
    assertion2(admissibleTimeStepSize>0,admissibleTimeStepSize,cellDescription.toString());
    assertion3(admissibleTimeStepSize<std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),admissibleTimeStepSize,cellDescription.toString());
    assertion2(std::isfinite(admissibleTimeStepSize),admissibleTimeStepSize,cellDescription.toString());

    // Direct update of the cell description time steps
    // Note that these local quantities might
    // be overwritten again by the synchronisation
    // happening in the next time step.
    // n-1
    cellDescription.setPreviousCorrectorTimeStamp(cellDescription.getCorrectorTimeStamp());
    cellDescription.setPreviousCorrectorTimeStepSize(cellDescription.getCorrectorTimeStepSize());

    // n
    cellDescription.setCorrectorTimeStamp(cellDescription.getPredictorTimeStamp());
    cellDescription.setCorrectorTimeStepSize(cellDescription.getPredictorTimeStepSize());

    // n+1
    cellDescription.setPredictorTimeStamp(cellDescription.getPredictorTimeStamp() + cellDescription.getPredictorTimeStepSize());
    cellDescription.setPredictorTimeStepSize(admissibleTimeStepSize);

    return admissibleTimeStepSize;
  }

  return std::numeric_limits<double>::max();
}

void exahype::solvers::ADERDGSolver::zeroTimeStepSizes(const int cellDescriptionsIndex, const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell) {
    cellDescription.setCorrectorTimeStepSize(0.0);
    cellDescription.setPredictorTimeStepSize(0.0);

    cellDescription.setPredictorTimeStamp(
        cellDescription.getCorrectorTimeStamp());
  }
}

void exahype::solvers::ADERDGSolver::reconstructStandardTimeSteppingData(const int cellDescriptionsIndex,int element) const {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell) {
    //  cellDescription.setPreviousCorrectorTimeStepSize(cellDescription.getCorrectorTimeStepSize()); TODO(Dominic): Should not be necessary: Prove by induction
//    logDebug("reconstructStandardTimeSteppingData(...)","cellDescription.getCorrectorTimeStamp()="<<cellDescription.getCorrectorTimeStamp());
//    logDebug("reconstructStandardTimeSteppingData(...)","cellDescription.getCorrectorTimeStepSize()="<<cellDescription.getCorrectorTimeStepSize()); TODO(Dominic): remove

    cellDescription.setPredictorTimeStamp(cellDescription.getCorrectorTimeStamp()+cellDescription.getCorrectorTimeStepSize());
    cellDescription.setCorrectorTimeStamp(cellDescription.getPredictorTimeStamp());
    cellDescription.setCorrectorTimeStepSize(cellDescription.getPredictorTimeStepSize());

    assertionEquals(cellDescription.getCorrectorTimeStamp(),cellDescription.getPredictorTimeStamp());
    assertionEquals(cellDescription.getCorrectorTimeStepSize(),cellDescription.getPredictorTimeStepSize());
  }
}


void exahype::solvers::ADERDGSolver::rollbackToPreviousTimeStep(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  // n+1
  cellDescription.setPredictorTimeStamp(cellDescription.getCorrectorTimeStamp());
  cellDescription.setPredictorTimeStepSize(cellDescription.getCorrectorTimeStepSize());

  // n
  cellDescription.setCorrectorTimeStamp(cellDescription.getPreviousCorrectorTimeStamp());
  cellDescription.setCorrectorTimeStepSize(cellDescription.getPreviousCorrectorTimeStepSize());

  // n-1
  cellDescription.setPreviousCorrectorTimeStamp(std::numeric_limits<double>::max());
  cellDescription.setPreviousCorrectorTimeStepSize(std::numeric_limits<double>::max()); // TODO(Dominic): get rid of the last time level.
}

void exahype::solvers::ADERDGSolver::reconstructStandardTimeSteppingDataAfterRollback(
    const int cellDescriptionsIndex,
    const int element) const {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  cellDescription.setPredictorTimeStamp(cellDescription.getCorrectorTimeStamp());
  cellDescription.setPredictorTimeStepSize(cellDescription.getCorrectorTimeStepSize());

  cellDescription.setPreviousCorrectorTimeStepSize(std::numeric_limits<double>::max());
}

void exahype::solvers::ADERDGSolver::setInitialConditions(
    const int cellDescriptionsIndex,
    const int element,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) {
  // reset helper variables
  CellDescription& cellDescription  = getCellDescription(cellDescriptionsIndex,element);
  exahype::Cell::resetNeighbourMergeHelperVariables(
          cellDescription,fineGridVertices,fineGridVerticesEnumerator);

  // initial conditions
  if (cellDescription.getType()==exahype::records::ADERDGCellDescription::Cell &&
      cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None) {
    double* luh = exahype::DataHeap::getInstance().getData(cellDescription.getSolution()).data();

    if (
      useAdjustSolution(
        cellDescription.getOffset()+0.5*cellDescription.getSize(),
        cellDescription.getSize(),
        cellDescription.getCorrectorTimeStamp(),
        cellDescription.getCorrectorTimeStepSize()
      )
      !=AdjustSolutionValue::No
    ) {
        adjustSolution(
            luh,
            cellDescription.getOffset()+0.5*cellDescription.getSize(),
            cellDescription.getSize(),
            cellDescription.getCorrectorTimeStamp(),
            cellDescription.getCorrectorTimeStepSize());
    }

    for (int i=0; i<getUnknownsPerCell(); i++) {
      assertion3(std::isfinite(luh[i]),cellDescription.toString(),"setInitialConditions(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
  }
}

void exahype::solvers::ADERDGSolver::updateSolution(
    const int cellDescriptionsIndex,
    const int element,
    double** tempStateSizedArrays,
    double** tempUnknowns,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) {
  // reset helper variables
  CellDescription& cellDescription  = getCellDescription(cellDescriptionsIndex,element);
  assertion2(cellDescription.getType()!=CellDescription::Type::Cell ||
      cellDescription.getNeighbourMergePerformed().all(),cellDescriptionsIndex,cellDescription.toString());

  exahype::Cell::resetNeighbourMergeHelperVariables(
        cellDescription,fineGridVertices,fineGridVerticesEnumerator);

  if (cellDescription.getType()==exahype::records::ADERDGCellDescription::Cell &&
      cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None) {
    double* solution    = DataHeap::getInstance().getData(cellDescription.getPreviousSolution()).data();
    double* newSolution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();
    std::copy(newSolution,newSolution+_dofPerCell,solution); // Copy (current solution) in old solution field.

    double* lduh   = exahype::DataHeap::getInstance().getData(cellDescription.getUpdate()).data();
    double* lFhbnd = exahype::DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();

    for (int i=0; i<getDataPerCell(); i++) { // cellDescription.getCorrectorTimeStepSize()==0.0 is an initial condition
      assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0)  || std::isfinite(solution[i]),cellDescription.toString(),"updateSolution(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.

    for (int i=0; i<getUnknownsPerCell(); i++) {
      assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0)  || std::isfinite(lduh[i]),cellDescription.toString(),"updateSolution",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.

    for (int i=0; i<getBndFluxTotalSize(); i++) {
      assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0)  || tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0) || std::isfinite(lFhbnd[i]),cellDescription.toString(),"updateSolution",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
    //TODO JMG move everything to inverseDx and use Peano to get it when Dominic implemente it
    #ifdef OPT_KERNELS
    double* dx = &cellDescription.getSize()[0];
    #if DIMENSIONS==2
    double inverseDx[2];
    #else
    double inverseDx[3];
    inverseDx[2] = 1.0/dx[2];
    #endif
    inverseDx[0] = 1.0/dx[0];
    inverseDx[1] = 1.0/dx[1];
    surfaceIntegral(lduh,lFhbnd,&inverseDx[0]); //TODO JMG use cellDescription.getInverseSize() when implemented
    #else
    surfaceIntegral(lduh,lFhbnd,cellDescription.getSize());
    #endif

    for (int i=0; i<getUnknownsPerCell(); i++) {
      assertion3(tarch::la::equals(cellDescription.getCorrectorTimeStepSize(),0.0)  || std::isfinite(lduh[i]),cellDescription.toString(),"updateSolution(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.

    solutionUpdate(newSolution,lduh,cellDescription.getCorrectorTimeStepSize());

    if (
      useAdjustSolution(
        cellDescription.getOffset()+0.5*cellDescription.getSize(),
        cellDescription.getSize(),
        cellDescription.getCorrectorTimeStamp()+cellDescription.getCorrectorTimeStepSize(),
        cellDescription.getCorrectorTimeStamp()
      )
      !=AdjustSolutionValue::No
    ) {
      adjustSolution(
          newSolution,
          cellDescription.getOffset()+0.5*cellDescription.getSize(),
          cellDescription.getSize(),
          cellDescription.getCorrectorTimeStamp()+cellDescription.getCorrectorTimeStepSize(),
          cellDescription.getCorrectorTimeStepSize());
    }

    for (int i=0; i<getDataPerCell(); i++) {
      assertion4(std::isfinite(newSolution[i]),cellDescriptionsIndex,cellDescription.toString(),"updateSolution(...)",i);
    } // Dead code elimination will get rid of this loop if Asserts/Debug flags are not set.
  }
  assertion(cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None);
}

void exahype::solvers::ADERDGSolver::rollbackSolution(
    const int cellDescriptionsIndex,
    const int element,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) {
  // reset helper variables
  CellDescription& cellDescription  = getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==exahype::records::ADERDGCellDescription::Cell &&
      cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None) {
    swapSolutionAndPreviousSolution(cellDescriptionsIndex,element);
  }
  assertion(cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None);
}

void exahype::solvers::ADERDGSolver::swapSolutionAndPreviousSolution(
    const int cellDescriptionsIndex,
    const int element) const {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  assertion(cellDescription.getType()==exahype::records::ADERDGCellDescription::Cell);
  assertion(cellDescription.getRefinementEvent()==exahype::records::ADERDGCellDescription::None);

  // Simply swap the heap indices
  const int previousSolution = cellDescription.getPreviousSolution();
  cellDescription.setPreviousSolution(cellDescription.getSolution());
  cellDescription.setSolution(previousSolution);
}

void exahype::solvers::ADERDGSolver::preProcess(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (
    cellDescription.getType()==CellDescription::Type::Cell
    #ifdef Parallel
    &&
    !cellDescription.getAdjacentToRemoteRank() // TODO(Dominic): What is going on here?
    #endif
  ) {
    uncompress(cellDescription);
  }
}

void exahype::solvers::ADERDGSolver::postProcess(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  if (
      cellDescription.getType()==CellDescription::Type::Cell
      #ifdef Parallel
      &&
      !cellDescription.getAdjacentToRemoteRank() // TODO(Dominic): What is going on here?
      #endif
    ) {
    compress(cellDescription);
  }
}

void exahype::solvers::ADERDGSolver::prepareFaceDataOfAncestor(CellDescription& cellDescription) {
  logDebug("prepareFaceDataOfAncestor(...)","cell="<<cellDescription.getOffset()+0.5*cellDescription.getSize() <<
          ", level=" << cellDescription.getLevel());

  assertion1(cellDescription.getType()==CellDescription::Ancestor,cellDescription.toString());
  std::fill_n(DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).begin(),
              getBndTotalSize(), 0.0);
  std::fill_n(DataHeap::getInstance().getData(cellDescription.getFluctuation()).begin(),
              getBndFluxTotalSize(), 0.0);

  #if defined(Debug) || defined(Asserts)
  double* Q = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
  double* F = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();
  #endif

  for(int i=0; i<getBndTotalSize(); ++i) {
    assertion2(tarch::la::equals(Q[i],0.0),i,Q[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.


  for(int i=0; i<getBndFluxTotalSize(); ++i) {
    assertion2(tarch::la::equals(F[i],0.0),i,F[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

  // observables min and max
  const int numberOfObservables = getDMPObservables();
  if (numberOfObservables>0) {
    std::fill_n(DataHeap::getInstance().getData(cellDescription.getSolutionMin()).begin(),
        numberOfObservables*DIMENSIONS_TIMES_TWO, std::numeric_limits<double>::max());
    std::fill_n(DataHeap::getInstance().getData(cellDescription.getSolutionMax()).begin(),
        numberOfObservables*DIMENSIONS_TIMES_TWO, -std::numeric_limits<double>::max()); // !!! Be aware of the "-"
  }
}

void exahype::solvers::ADERDGSolver::prolongateFaceDataToDescendant(
    CellDescription& cellDescription,
    SubcellPosition& subcellPosition) {
  assertion2(exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(
      subcellPosition.parentCellDescriptionsIndex),
      subcellPosition.parentCellDescriptionsIndex,cellDescription.toString());

  CellDescription& cellDescriptionParent = Heap::getInstance().getData(
      subcellPosition.parentCellDescriptionsIndex)[subcellPosition.parentElement];

  assertion(cellDescriptionParent.getSolverNumber() == cellDescription.getSolverNumber());
  assertion(cellDescriptionParent.getType() == exahype::records::ADERDGCellDescription::Cell ||
            cellDescriptionParent.getType() == exahype::records::ADERDGCellDescription::Descendant);

  const int levelFine   = cellDescription.getLevel();
  const int levelCoarse = cellDescriptionParent.getLevel();
  assertion(levelCoarse < levelFine);
  const int levelDelta = levelFine - levelCoarse;

  for (int d = 0; d < DIMENSIONS; ++d) {
    // Check if cell is at "left" or "right" d face of parent
    if (subcellPosition.subcellIndex[d]==0 ||
        subcellPosition.subcellIndex[d]==tarch::la::aPowI(levelDelta,3)-1) {
      const int faceIndex = 2*d + ((subcellPosition.subcellIndex[d]==0) ? 0 : 1); // Do not remove brackets.

      const int numberOfFaceDof = getBndFaceSize();
      const int numberOfFluxDof = getBndFluxSize();

      // Q
      assertion1(DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor()),cellDescription.toString());
      double* lQhbndFine = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data() +
          (faceIndex * numberOfFaceDof);
      const double* lQhbndCoarse = DataHeap::getInstance().getData(cellDescriptionParent.getExtrapolatedPredictor()).data() +
          (faceIndex * numberOfFaceDof);
      // flux
      double* lFhbndFine = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data() +
          (faceIndex * numberOfFluxDof);
      const double* lFhbndCoarse = DataHeap::getInstance().getData(cellDescriptionParent.getFluctuation()).data() +
          (faceIndex * numberOfFluxDof);

      faceUnknownsProlongation(lQhbndFine,lFhbndFine,lQhbndCoarse,
                               lFhbndCoarse, levelCoarse, levelFine,
                               exahype::amr::getSubfaceIndex(subcellPosition.subcellIndex,d));


      prolongateObservablesMinAndMax(cellDescription,cellDescriptionParent,faceIndex);
    }
  }
}

void exahype::solvers::ADERDGSolver::prolongateObservablesMinAndMax(
    const CellDescription& cellDescription,
    const CellDescription& cellDescriptionParent,
    const int faceIndex) const {
  const int numberOfObservables = getDMPObservables();
  if (numberOfObservables>0) {
    // fine
    double* minFine = DataHeap::getInstance().getData(cellDescription.getSolutionMin()).data() +
        (faceIndex * numberOfObservables);
    double* maxFine = DataHeap::getInstance().getData(cellDescription.getSolutionMax()).data() +
        (faceIndex * numberOfObservables);
    // coarse
    const double* minCoarse = DataHeap::getInstance().getData(cellDescriptionParent.getSolutionMin()).data() +
        (faceIndex * numberOfObservables);
    const double* maxCoarse = DataHeap::getInstance().getData(cellDescriptionParent.getSolutionMax()).data() +
        (faceIndex * numberOfObservables);

    std::copy_n( minCoarse,numberOfObservables, minFine );
    std::copy_n( maxCoarse,numberOfObservables, maxFine );
  }
}

void exahype::solvers::ADERDGSolver::prolongateDataAndPrepareDataRestriction(
    const int cellDescriptionsIndex,
    const int element) {
  CellDescription& cellDescription =
      exahype::solvers::ADERDGSolver::getCellDescription(cellDescriptionsIndex,element);

  if (
      cellDescription.getType()==CellDescription::Type::Ancestor
      &&
      cellDescription.getHelperStatus()>=MinimumHelperStatusForAllocatingBoundaryData
  ) {
    prepareFaceDataOfAncestor(cellDescription);
  } else if (
      cellDescription.getType()==CellDescription::Type::Descendant
      &&
      cellDescription.getHelperStatus()>=MinimumHelperStatusForAllocatingBoundaryData
      &&
      isValidCellDescriptionIndex(cellDescription.getParentIndex())
  ) {
    exahype::solvers::Solver::SubcellPosition
    subcellPosition =
        exahype::amr::computeSubcellPositionOfDescendant<CellDescription,Heap,false>(
            cellDescription);

    prolongateFaceDataToDescendant(cellDescription,subcellPosition);
  }
  assertion1(
      cellDescription.getType()!=CellDescription::Type::Descendant ||
      (isValidCellDescriptionIndex(cellDescription.getParentIndex()) ||
          cellDescription.getParentIndex()==multiscalelinkedcell::HangingVertexBookkeeper::RemoteAdjacencyIndex),
          cellDescription.toString());
}

void exahype::solvers::ADERDGSolver::restrictToNextParent(
      const int fineGridCellDescriptionsIndex,
      const int fineGridElement,
      const int coarseGridCellDescriptionsIndex,
      const int coarseGridElement) {
  // do nothing
}

void exahype::solvers::ADERDGSolver::restrictToTopMostParent(
                  const int cellDescriptionsIndex,
                  const int element,
                  const int parentCellDescriptionsIndex,
                  const int parentElement,
                  const tarch::la::Vector<DIMENSIONS,int>& subcellIndex) {
  CellDescription& cellDescription =
      getCellDescription(cellDescriptionsIndex,element);
  CellDescription& parentCellDescription =
      getCellDescription(parentCellDescriptionsIndex,parentElement);
  assertion(parentCellDescription.getSolverNumber()==cellDescription.getSolverNumber());
  assertion1(parentCellDescription.getType()==CellDescription::Type::Ancestor,
            parentCellDescription.toString());

  const int levelFine   = cellDescription.getLevel();
  const int levelCoarse = parentCellDescription.getLevel();
  assertion(levelCoarse < levelFine);
  const int levelDelta  = levelFine - levelCoarse;

  for (int d = 0; d < DIMENSIONS; d++) {
    if (subcellIndex[d]==0 ||
        subcellIndex[d]==tarch::la::aPowI(levelDelta,3)-1) {
      const int faceIndex = 2*d + ((subcellIndex[d]==0) ? 0 : 1); // Do not remove brackets.

      logDebug("restrictData(...)","cell=" << cellDescription.getOffset()+0.5*cellDescription.getSize() <<
               ",level=" << cellDescription.getLevel() <<
               ",d=" << d <<
               ",face=" << faceIndex << ",subcellIndex" << subcellIndex.toString() <<
               " to " <<
               " cell="<<parentCellDescription.getOffset()+0.5*parentCellDescription.getSize()<<
               " level="<<parentCellDescription.getLevel());

      #ifdef Parallel
      logDebug("restrictData(...)",
               "forMasterWorkerComm="<<cellDescription.getHasToHoldDataForMasterWorkerCommunication());
      #endif

      const int numberOfFaceDof = getBndFaceSize();
      const int numberOfFluxDof = getBndFluxSize();

      const double* lQhbndFine = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data() +
          (faceIndex * numberOfFaceDof);
      double* lQhbndCoarse = DataHeap::getInstance().getData(parentCellDescription.getExtrapolatedPredictor()).data() +
          (faceIndex * numberOfFaceDof);

      const double* lFhbndFine = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data() +
          (faceIndex * numberOfFluxDof);
      double* lFhbndCoarse = DataHeap::getInstance().getData(parentCellDescription.getFluctuation()).data() +
          (faceIndex * numberOfFluxDof);

      faceUnknownsRestriction(lQhbndCoarse,lFhbndCoarse,lQhbndFine,lFhbndFine,
                              levelCoarse, levelFine,
                              exahype::amr::getSubfaceIndex(subcellIndex,d));

      restrictObservablesMinAndMax(parentCellDescription,cellDescription,faceIndex);
    }
  }
}

void exahype::solvers::ADERDGSolver::restrictObservablesMinAndMax(
    const CellDescription& cellDescription,
    const CellDescription& parentCellDescription,
    const int faceIndex) const {
  const int numberOfObservables = getDMPObservables();
  if (numberOfObservables>0) {
    // fine
    double* minFine = DataHeap::getInstance().getData(cellDescription.getSolutionMin()).data() +
        (faceIndex * numberOfObservables);
    double* maxFine = DataHeap::getInstance().getData(cellDescription.getSolutionMax()).data() +
        (faceIndex * numberOfObservables);
    // coarse
    double* minCoarse = DataHeap::getInstance().getData(parentCellDescription.getSolutionMin()).data() +
        (faceIndex * numberOfObservables);
    double* maxCoarse = DataHeap::getInstance().getData(parentCellDescription.getSolutionMax()).data() +
        (faceIndex * numberOfObservables);

    for (int i=0; i<numberOfObservables; i++) {
      *(minCoarse+i) = std::min( *(minFine+i), *(minCoarse+i) );
      *(maxCoarse+i) = std::max( *(maxFine+i), *(maxCoarse+i) );
    }
  }
}

///////////////////////////////////
// NEIGHBOUR
///////////////////////////////////

// limiter status
exahype::solvers::ADERDGSolver::CellDescription::LimiterStatus
exahype::solvers::ADERDGSolver::toLimiterStatusEnum(const int limiterStatusAsInt) {
  assertion1( limiterStatusAsInt >= 0, limiterStatusAsInt );
  const int newLimiterStatusAsInt=std::min(
      limiterStatusAsInt,
      static_cast<int>(CellDescription::LimiterStatus::Troubled) );

  return static_cast<CellDescription::LimiterStatus>(newLimiterStatusAsInt);
}

void exahype::solvers::ADERDGSolver::mergeWithLimiterStatus(
    CellDescription& cellDescription,
    const int faceIndex,
    const int otherLimiterStatus) const {
  const int croppedOtherLimiterStatus =
      std::min(
          otherLimiterStatus,
          static_cast<int>(CellDescription::LimiterStatus::Troubled) );

  int limiterStatus =
      std::min(
        cellDescription.getFacewiseLimiterStatus(faceIndex),
        static_cast<int>(CellDescription::LimiterStatus::Troubled) );
  limiterStatus =
      std::max( limiterStatus, croppedOtherLimiterStatus );

  cellDescription.setFacewiseLimiterStatus( faceIndex, std::max( 0, limiterStatus-1 ) );
}

/**
 * Iterate over the merged limiter statuses per face and
 * determine a unique value.
 */
int
exahype::solvers::ADERDGSolver::determineLimiterStatus(
    CellDescription& cellDescription) {
  return tarch::la::max(cellDescription.getFacewiseLimiterStatus());
}

void
exahype::solvers::ADERDGSolver::overwriteFacewiseLimiterStatus(
    CellDescription& cellDescription) {
  for (int faceIndex=0; faceIndex<DIMENSIONS_TIMES_TWO; faceIndex++) {
    cellDescription.setFacewiseLimiterStatus(faceIndex,cellDescription.getLimiterStatus());
  }
}

void exahype::solvers::ADERDGSolver::mergeNeighboursLimiterStatus(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2) const {
  CellDescription& cellDescription1 = getCellDescription(cellDescriptionsIndex1,element1);
  CellDescription& cellDescription2 = getCellDescription(cellDescriptionsIndex2,element2);

  const int direction    = tarch::la::equalsReturnIndex(pos1,pos2);
  const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
  const int orientation2 = 1-orientation1;

  const int faceIndex1 = 2*direction+orientation1;
  const int faceIndex2 = 2*direction+orientation2;

  const int limiterStatus1 = cellDescription1.getFacewiseLimiterStatus(faceIndex1);
  const int limiterStatus2 = cellDescription2.getFacewiseLimiterStatus(faceIndex2);

  mergeWithLimiterStatus(cellDescription1,faceIndex1,limiterStatus2);
  mergeWithLimiterStatus(cellDescription2,faceIndex2,limiterStatus1);
}

// helper status
int exahype::solvers::ADERDGSolver::MaximumHelperStatus                          = 2;
int exahype::solvers::ADERDGSolver::MinimumHelperStatusForAllocatingBoundaryData = 1;

void
exahype::solvers::ADERDGSolver::updateHelperStatus(
    exahype::solvers::ADERDGSolver::CellDescription& cellDescription) const {
  cellDescription.setHelperStatus(determineHelperStatus(cellDescription));
  overwriteFacewiseHelperStatus(cellDescription);
  assertion1(
      cellDescription.getType()!=CellDescription::Type::Cell ||
      cellDescription.getHelperStatus()==MaximumHelperStatus,
      cellDescription.toString());
}

int
exahype::solvers::ADERDGSolver::determineHelperStatus(
    exahype::solvers::ADERDGSolver::CellDescription& cellDescription) const {
  return
      (cellDescription.getType()==CellDescription::Type::Cell) ?
          MaximumHelperStatus :
          tarch::la::max(cellDescription.getFacewiseHelperStatus());
}

void exahype::solvers::ADERDGSolver::overwriteFacewiseHelperStatus(
    exahype::solvers::ADERDGSolver::CellDescription& cellDescription) const {
  for (int faceIndex=0; faceIndex<DIMENSIONS_TIMES_TWO; faceIndex++) {
    cellDescription.setFacewiseHelperStatus(faceIndex,
        cellDescription.getHelperStatus());
  }
}

void exahype::solvers::ADERDGSolver::mergeWithHelperStatus(
    CellDescription& cellDescription,
    const int faceIndex,
    const int otherHelperStatus) const {
  const int helperStatus =
      std::max( cellDescription.getHelperStatus(), otherHelperStatus );
  assertion3(helperStatus<=MaximumHelperStatus,
             helperStatus,otherHelperStatus,
             cellDescription.getHelperStatus());
  cellDescription.setFacewiseHelperStatus( faceIndex, std::max( 0, helperStatus-1 ) );
}

void exahype::solvers::ADERDGSolver::mergeNeighboursHelperStatus(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2) const {
  CellDescription& cellDescription1 = getCellDescription(cellDescriptionsIndex1,element1);
  CellDescription& cellDescription2 = getCellDescription(cellDescriptionsIndex2,element2);

  const int direction    = tarch::la::equalsReturnIndex(pos1,pos2);
  const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
  const int orientation2 = 1-orientation1;

  const int faceIndex1 = 2*direction+orientation1;
  const int faceIndex2 = 2*direction+orientation2;

  const int helperStatus1 = cellDescription1.getFacewiseHelperStatus(faceIndex1);
  const int helperStatus2 = cellDescription2.getFacewiseHelperStatus(faceIndex2);

  mergeWithHelperStatus(cellDescription1,2*direction+orientation1,helperStatus2);
  mergeWithHelperStatus(cellDescription2,2*direction+orientation2,helperStatus1);
}

// augmentation status
int exahype::solvers::ADERDGSolver::MaximumAugmentationStatus                = 2;
int exahype::solvers::ADERDGSolver::MinimumAugmentationStatusForAugmentation = 1;

void
exahype::solvers::ADERDGSolver::updateAugmentationStatus(
    exahype::solvers::ADERDGSolver::CellDescription& cellDescription) const {
  cellDescription.setAugmentationStatus(determineAugmentationStatus(cellDescription));
  overwriteFacewiseAugmentationStatus(cellDescription);
  assertion1(
      cellDescription.getType()!=CellDescription::Type::Ancestor ||
      cellDescription.getAugmentationStatus()==MaximumAugmentationStatus,
      cellDescription.toString());
}

int
exahype::solvers::ADERDGSolver::determineAugmentationStatus(
    exahype::solvers::ADERDGSolver::CellDescription& cellDescription) const {
  return
      (cellDescription.getType()==CellDescription::Type::Ancestor) ?
          MaximumAugmentationStatus :
          tarch::la::max(cellDescription.getFacewiseAugmentationStatus());
}

void exahype::solvers::ADERDGSolver::overwriteFacewiseAugmentationStatus(
    exahype::solvers::ADERDGSolver::CellDescription& cellDescription) const {
  for (int faceIndex=0; faceIndex<DIMENSIONS_TIMES_TWO; faceIndex++) {
    cellDescription.setFacewiseAugmentationStatus(
        faceIndex,cellDescription.getAugmentationStatus());
  }
}

void exahype::solvers::ADERDGSolver::mergeWithAugmentationStatus(
    CellDescription& cellDescription,
    const int faceIndex,
    const int otherAugmentationStatus) const {
  const int augmentationStatus =
      std::max( cellDescription.getAugmentationStatus(), otherAugmentationStatus );
  assertion3(
      augmentationStatus<=MaximumAugmentationStatus,
      augmentationStatus,otherAugmentationStatus,
      cellDescription.getAugmentationStatus());
  cellDescription.setFacewiseAugmentationStatus( faceIndex, std::max( 0, augmentationStatus-1 ) );
}

void exahype::solvers::ADERDGSolver::mergeNeighboursAugmentationStatus(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2) const {
  CellDescription& cellDescription1 = getCellDescription(cellDescriptionsIndex1,element1);
  CellDescription& cellDescription2 = getCellDescription(cellDescriptionsIndex2,element2);

  const int direction    = tarch::la::equalsReturnIndex(pos1,pos2);
  const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
  const int orientation2 = 1-orientation1;

  const int faceIndex1 = 2*direction+orientation1;
  const int faceIndex2 = 2*direction+orientation2;

  const int augmentationStatus1 = cellDescription1.getFacewiseAugmentationStatus(faceIndex1);
  const int augmentationStatus2 = cellDescription2.getFacewiseAugmentationStatus(faceIndex2);

  mergeWithAugmentationStatus(cellDescription1,2*direction+orientation1,augmentationStatus2);
  mergeWithAugmentationStatus(cellDescription2,2*direction+orientation2,augmentationStatus1);
}

// merge metadata
void exahype::solvers::ADERDGSolver::mergeNeighboursMetadata(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2) {
  mergeNeighboursHelperStatus      (cellDescriptionsIndex1,element1,cellDescriptionsIndex2,element2,pos1,pos2);
  mergeNeighboursAugmentationStatus(cellDescriptionsIndex1,element1,cellDescriptionsIndex2,element2,pos1,pos2);
  mergeNeighboursLimiterStatus     (cellDescriptionsIndex1,element1,cellDescriptionsIndex2,element2,pos1,pos2);
}

// merge compute data
void exahype::solvers::ADERDGSolver::mergeNeighbours(
    const int                                 cellDescriptionsIndex1,
    const int                                 element1,
    const int                                 cellDescriptionsIndex2,
    const int                                 element2,
    const tarch::la::Vector<DIMENSIONS, int>& pos1,
    const tarch::la::Vector<DIMENSIONS, int>& pos2,
    double**                                  tempFaceUnknowns,
    double**                                  tempStateSizedVectors,
    double**                                  tempStateSizedSquareMatrices) {
  assertion1(tarch::la::countEqualEntries(pos1,pos2)==(DIMENSIONS-1),tarch::la::countEqualEntries(pos1,pos2));
  const int direction    = tarch::la::equalsReturnIndex(pos1, pos2);
  const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;

  const int indexOfRightFaceOfLeftCell = 2*direction+1;
  const int indexOfLeftFaceOfRightCell = 2*direction+0;

  CellDescription& cellDescriptionLeft  =
      (orientation1==1) ?
          getCellDescription(cellDescriptionsIndex1,element1)
          :
          getCellDescription(cellDescriptionsIndex2,element2);
  CellDescription& cellDescriptionRight =
      (orientation1==1)
          ?
          getCellDescription(cellDescriptionsIndex2,element2)
          :
          getCellDescription(cellDescriptionsIndex1,element1);

  peano::datatraversal::TaskSet uncompression(
    [&] () -> void {
      uncompress(cellDescriptionLeft);
    },
    [&] () -> void {
      uncompress(cellDescriptionRight);
    },
    true
  );

  solveRiemannProblemAtInterface(
      cellDescriptionLeft,cellDescriptionRight,indexOfRightFaceOfLeftCell,indexOfLeftFaceOfRightCell,
      tempFaceUnknowns,tempStateSizedVectors,tempStateSizedSquareMatrices);
}



void exahype::solvers::ADERDGSolver::solveRiemannProblemAtInterface(
    CellDescription& pLeft,
    CellDescription& pRight,
    const int faceIndexLeft,
    const int faceIndexRight,
    double**  tempFaceUnknowns,
    double**  tempStateSizedVectors,
    double**  tempStateSizedSquareMatrices) {
  if (pLeft.getType()==CellDescription::Cell ||
      pRight.getType()==CellDescription::Cell) {
    assertion1(DataHeap::getInstance().isValidIndex(pLeft.getExtrapolatedPredictor()),pLeft.toString());
    assertion1(DataHeap::getInstance().isValidIndex(pLeft.getFluctuation()),pLeft.toString());
    assertion1(DataHeap::getInstance().isValidIndex(pRight.getExtrapolatedPredictor()),pRight.toString());
    assertion1(DataHeap::getInstance().isValidIndex(pRight.getFluctuation()),pRight.toString());
    assertion1(holdsFaceData(pLeft),pLeft.toString());
    assertion1(holdsFaceData(pRight),pRight.toString());
    assertion1(pLeft.getRefinementEvent()==CellDescription::None,pLeft.toString());
    assertion1(pRight.getRefinementEvent()==CellDescription::None,pRight.toString());
    assertionEquals4(pLeft.getNeighbourMergePerformed(faceIndexLeft),pRight.getNeighbourMergePerformed(faceIndexRight),faceIndexLeft,faceIndexRight,pLeft.toString(),pRight.toString());
    assertion4(std::abs(faceIndexLeft-faceIndexRight)==1,faceIndexLeft,faceIndexRight,pLeft.toString(),pRight.toString());

    const int dataPerFace = getBndFaceSize();
    const int dofPerFace  = getBndFluxSize();

    double* QL = DataHeap::getInstance() .getData(pLeft.getExtrapolatedPredictor()).data() + /// !!! Be aware of the dataPerFace, Left, Right
        (faceIndexLeft * dataPerFace);
    double* QR = DataHeap::getInstance().getData(pRight.getExtrapolatedPredictor()).data() +
        (faceIndexRight * dataPerFace);

    double* FL = DataHeap::getInstance().getData(pLeft.getFluctuation()).data() + /// !!! Be aware of the dofPerFace, Left, Right
        (faceIndexLeft * dofPerFace);
    double* FR = DataHeap::getInstance().getData(pRight.getFluctuation()).data() +
        (faceIndexRight * dofPerFace);

    // todo Time step must be interpolated in local time stepping case
    // both time step sizes are the same, so the min has no effect here.
    assertion1(faceIndexLeft>=0,faceIndexLeft);
    assertion1(faceIndexRight>=0,faceIndexRight);
    assertion1(faceIndexLeft<DIMENSIONS_TIMES_TWO,faceIndexLeft);
    assertion1(faceIndexRight<DIMENSIONS_TIMES_TWO,faceIndexRight);
    assertion1(faceIndexRight%2==0,faceIndexRight);
    const int normalDirection = (faceIndexRight - (faceIndexRight %2))/2;
    assertion3(normalDirection==(faceIndexLeft - (faceIndexLeft %2))/2,normalDirection,faceIndexLeft,faceIndexRight);
    assertion3(normalDirection<DIMENSIONS,normalDirection,faceIndexLeft,faceIndexRight);

    // Synchronise time stepping.
    synchroniseTimeStepping(pLeft);
    synchroniseTimeStepping(pRight);

    assertion3(std::isfinite(pLeft.getCorrectorTimeStepSize()),pLeft.toString(),faceIndexLeft,normalDirection);
    assertion3(std::isfinite(pRight.getCorrectorTimeStepSize()),pRight.toString(),faceIndexRight,normalDirection);
    assertion3(pLeft.getCorrectorTimeStepSize()>=0.0,pLeft.toString(),faceIndexLeft,normalDirection);
    assertion3(pRight.getCorrectorTimeStepSize()>=0.0,pRight.toString(),faceIndexRight,normalDirection);

    for(int i=0; i<dataPerFace; ++i) {
      assertion5(tarch::la::equals(pLeft.getCorrectorTimeStepSize(),0.0) || std::isfinite(QL[i]),pLeft.toString(),faceIndexLeft,normalDirection,i,QL[i]);
      assertion5(tarch::la::equals(pRight.getCorrectorTimeStepSize(),0.0) || std::isfinite(QR[i]),pRight.toString(),faceIndexRight,normalDirection,i,QR[i]);
    }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

    for(int i=0; i<dofPerFace; ++i) {
      assertion5(tarch::la::equals(pLeft.getCorrectorTimeStepSize(),0.0) || std::isfinite(FL[i]),pLeft.toString(),faceIndexLeft,normalDirection,i,FL[i]);
      assertion5(tarch::la::equals(pRight.getCorrectorTimeStepSize(),0.0) || std::isfinite(FR[i]),pRight.toString(),faceIndexRight,normalDirection,i,FR[i]);
    }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

    riemannSolver(
        FL,FR,QL,QR,
        tempFaceUnknowns[0],tempStateSizedVectors,tempStateSizedSquareMatrices,
        std::min(pLeft.getCorrectorTimeStepSize(),
            pRight.getCorrectorTimeStepSize()),
        normalDirection, false);

    for(int i=0; i<dofPerFace; ++i) {
      assertion8(tarch::la::equals(pLeft.getCorrectorTimeStepSize(),0.0) || (std::isfinite(FL[i]) && std::isfinite(FR[i])),
                 pLeft.toString(),faceIndexLeft,pRight.toString(),faceIndexRight,normalDirection,i,FL[i],FR[i]);
    }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

    assertion1(DataHeap::getInstance().isValidIndex(pLeft.getExtrapolatedPredictor()),pLeft.toString());
    assertion1(DataHeap::getInstance().isValidIndex(pLeft.getFluctuation()),pLeft.toString());
    assertion1(DataHeap::getInstance().isValidIndex(pRight.getExtrapolatedPredictor()),pRight.toString());
    assertion1(DataHeap::getInstance().isValidIndex(pRight.getFluctuation()),pRight.toString());
  }
}

void exahype::solvers::ADERDGSolver::mergeWithBoundaryData(
    const int                                 cellDescriptionsIndex,
    const int                                 element,
    const tarch::la::Vector<DIMENSIONS, int>& posCell,
    const tarch::la::Vector<DIMENSIONS, int>& posBoundary,
    double**                                  tempFaceUnknowns,
    double**                                  tempStateSizedVectors,
    double**                                  tempStateSizedSquareMatrices) {
  if (tarch::la::countEqualEntries(posCell,posBoundary)!=(DIMENSIONS-1)) {
    return; // We only consider faces; no corners.
  }

  CellDescription& cellDescription =
      getCellDescription(cellDescriptionsIndex,element);

  if (cellDescription.getType()==CellDescription::Cell) {
    const int direction   = tarch::la::equalsReturnIndex(posCell, posBoundary);
    const int orientation = (1 + posBoundary(direction) - posCell(direction))/2;

    uncompress(cellDescription);

    applyBoundaryConditions(
        cellDescription,2*direction+orientation,
        tempFaceUnknowns,tempStateSizedVectors,tempStateSizedSquareMatrices);
  }
}

void exahype::solvers::ADERDGSolver::applyBoundaryConditions(
    CellDescription& p,
    const int faceIndex,
    double**  tempFaceUnknowns,
    double**  tempStateSizedVectors,
    double**  tempStateSizedSquareMatrices) {
  assertion1(p.getType()==CellDescription::Cell,p.toString());
  assertion1(p.getRefinementEvent()==CellDescription::None,p.toString());
  assertion1(DataHeap::getInstance().isValidIndex(p.getExtrapolatedPredictor()),p.toString());
  assertion1(DataHeap::getInstance().isValidIndex(p.getFluctuation()),p.toString());

  const int dataPerFace = getBndFaceSize();
  const int dofPerFace  = getBndFluxSize();

  double* QIn = DataHeap::getInstance().getData(p.getExtrapolatedPredictor()).data() +
      (faceIndex * dataPerFace);
  double* FIn = DataHeap::getInstance().getData(p.getFluctuation()).data() +
      (faceIndex * dofPerFace);

  const int normalDirection = (faceIndex - faceIndex % 2)/2;
  assertion2(normalDirection<DIMENSIONS,faceIndex,normalDirection);
  
  for(int i=0; i<dataPerFace; ++i) {
    assertion5(std::isfinite(QIn[i]), p.toString(),
        faceIndex, normalDirection, i, QIn[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

  for(int i=0; i<dofPerFace; ++i) {
    assertion5(std::isfinite(FIn[i]), p.toString(),
        faceIndex, normalDirection, i, FIn[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

  // Synchronise time stepping.
  synchroniseTimeStepping(p);

  double* QOut = tempFaceUnknowns[1];
  double* FOut  = tempFaceUnknowns[2];

  // TODO(Dominic): Hand in space-time volume data. Time integrate it afterwards
  boundaryConditions(FOut,QOut,
      FIn,QIn,
      p.getOffset() + 0.5*p.getSize(), // centre
      p.getSize(),
      p.getCorrectorTimeStamp(),
      p.getCorrectorTimeStepSize(),
      faceIndex,
      normalDirection);

  assertion4(std::isfinite(p.getCorrectorTimeStamp()),p.toString(),faceIndex,normalDirection,p.getCorrectorTimeStamp());
  assertion4(std::isfinite(p.getCorrectorTimeStepSize()),p.toString(),faceIndex,normalDirection,p.getCorrectorTimeStepSize());
  assertion4(p.getCorrectorTimeStepSize()>=0.0, p.toString(),faceIndex, normalDirection,p.getCorrectorTimeStepSize());
  for(int i=0; i<dataPerFace; ++i) {
    assertion5(tarch::la::equals(p.getCorrectorTimeStepSize(),0.0) || std::isfinite(QIn[i]),p.toString(),faceIndex,normalDirection,i,QIn[i]);
    assertion5(tarch::la::equals(p.getCorrectorTimeStepSize(),0.0) || std::isfinite(QOut[i]),p.toString(),faceIndex,normalDirection,i,QOut[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.
  for(int i=0; i<dofPerFace; ++i) {
    assertion5(tarch::la::equals(p.getCorrectorTimeStepSize(),0.0) || std::isfinite(FIn[i]),p.toString(),faceIndex,normalDirection,i,FIn[i]);
    assertion5(tarch::la::equals(p.getCorrectorTimeStepSize(),0.0) || std::isfinite(FOut[i]),p.toString(),faceIndex,normalDirection,i,FOut[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

  // @todo(Dominic): Add to docu why we need this. Left or right inpclut
  if (faceIndex % 2 == 0) {
    riemannSolver(FOut, FIn, QOut, QIn,
        tempFaceUnknowns[0],tempStateSizedVectors,tempStateSizedSquareMatrices,
        p.getCorrectorTimeStepSize(),
        normalDirection,true);
  } else {
    riemannSolver(FIn, FOut, QIn, QOut,
        tempFaceUnknowns[0],tempStateSizedVectors,tempStateSizedSquareMatrices,
        p.getCorrectorTimeStepSize(),
        normalDirection,true);
  }

  for(int i=0; i<dofPerFace; ++i) {
    assertion5(tarch::la::equals(p.getCorrectorTimeStepSize(),0.0) || std::isfinite(FIn[i]),p.toString(),faceIndex,normalDirection,i,FIn[i]);
    assertion5(tarch::la::equals(p.getCorrectorTimeStepSize(),0.0) || std::isfinite(FOut[i]),p.toString(),faceIndex,normalDirection,i,FOut[i]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.
}

void exahype::solvers::ADERDGSolver::mergeWithBoundaryOrEmptyCellMetadata(
      const int cellDescriptionsIndex,
      const int element,
      const tarch::la::Vector<DIMENSIONS, int>& posCell,
      const tarch::la::Vector<DIMENSIONS, int>& posBoundaryOrEmptyCell) {
   CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

   const int direction   = tarch::la::equalsReturnIndex(posCell, posBoundaryOrEmptyCell);
   const int orientation = (1 + posBoundaryOrEmptyCell(direction) - posCell(direction))/2;
   const int faceIndex   = 2*direction+orientation;

   cellDescription.setFacewiseAugmentationStatus(faceIndex,0);
   cellDescription.setFacewiseHelperStatus(faceIndex,0);
   cellDescription.setFacewiseLimiterStatus(faceIndex,0);
}

void exahype::solvers::ADERDGSolver::prepareNextNeighbourMerging(
    const int cellDescriptionsIndex,const int element,
    exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) const {
  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  exahype::Cell::resetNeighbourMergeHelperVariables(
      cellDescription,fineGridVertices,fineGridVerticesEnumerator);
}

#ifdef Parallel
const int exahype::solvers::ADERDGSolver::DataMessagesPerNeighbourCommunication    = 2;
const int exahype::solvers::ADERDGSolver::DataMessagesPerForkOrJoinCommunication   = 1;
const int exahype::solvers::ADERDGSolver::DataMessagesPerMasterWorkerCommunication = 2;

/**
 * After the forking the master's cell descriptions
 * are not accessed by enterCell(...) on the master
 * anymore. However we still use them as buffer for saving data.
 */
void exahype::solvers::ADERDGSolver::sendCellDescriptions(
    const int                                     toRank,
    const int                                     cellDescriptionsIndex,
    const peano::heap::MessageType&               messageType,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),
      cellDescriptionsIndex);

  if (Heap::getInstance().getInstance().getData(cellDescriptionsIndex).size()>0) {
    for (CellDescription& cellDescription : Heap::getInstance().getData(cellDescriptionsIndex)) {
      if (cellDescription.getType()==CellDescription::Type::Ancestor) {
        Solver::SubcellPosition subcellPosition =
            exahype::amr::computeSubcellPositionOfCellOrAncestorOrEmptyAncestor
            <CellDescription,Heap>(cellDescription);

        if (subcellPosition.parentElement!=NotFound) {
          cellDescription.setHasToHoldDataForMasterWorkerCommunication(true);

          auto* solver = exahype::solvers::RegisteredSolvers[cellDescription.getSolverNumber()];

          switch (solver->getType()) {
            case exahype::solvers::Solver::Type::ADERDG:
              static_cast<exahype::solvers::ADERDGSolver*>(solver)->ensureNecessaryMemoryIsAllocated(cellDescription);
              break;
            case exahype::solvers::Solver::Type::LimitingADERDG:
              static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver()->ensureNecessaryMemoryIsAllocated(cellDescription);
              break;
            case exahype::solvers::Solver::Type::FiniteVolumes:
              assertionMsg(false,"Solver type not supported!");
              break;
          }
        }
      } else if (cellDescription.getType()==CellDescription::Type::Descendant) {

        cellDescription.setHasToHoldDataForMasterWorkerCommunication(true);

        auto* solver = exahype::solvers::RegisteredSolvers[cellDescription.getSolverNumber()];

        switch (solver->getType()) {
          case exahype::solvers::Solver::Type::ADERDG:
            static_cast<exahype::solvers::ADERDGSolver*>(solver)->ensureNecessaryMemoryIsAllocated(cellDescription);
            break;
          case exahype::solvers::Solver::Type::LimitingADERDG:
            static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver()->ensureNecessaryMemoryIsAllocated(cellDescription);
            break;
          case exahype::solvers::Solver::Type::FiniteVolumes:
            assertionMsg(false,"Solver type not supported!");
            break;
        }
      }
    }

    logDebug("sendCellDescriptions(...)","send "<<
            Heap::getInstance().getData(cellDescriptionsIndex).size()<<
            " cell descriptions to rank "<<toRank<<
            " at (center="<< x.toString() <<
            ",level="<< level << ")");

    Heap::getInstance().sendData(cellDescriptionsIndex,
                                 toRank,x,level,messageType);
  } else {
    sendEmptyCellDescriptions(toRank,messageType,x,level);
  }
}

void exahype::solvers::ADERDGSolver::eraseCellDescriptions(
    const int cellDescriptionsIndex) {
  assertion(Heap::getInstance().isValidIndex(cellDescriptionsIndex));

  for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    const int element = solver->tryGetElement(cellDescriptionsIndex,solverNumber);
    if (element!=exahype::solvers::Solver::NotFound) {
      auto& cellDescription = getCellDescription(cellDescriptionsIndex,element);
      if (cellDescription.getType()==CellDescription::Type::Cell) {
        cellDescription.setType(CellDescription::Type::Erased);
        static_cast<ADERDGSolver*>(solver)->ensureNoUnnecessaryMemoryIsAllocated(cellDescription);
      }

      Heap::getInstance().getData(cellDescriptionsIndex).erase(
          Heap::getInstance().getData(cellDescriptionsIndex).begin()+element);
    }
  }
}

void exahype::solvers::ADERDGSolver::sendEmptyCellDescriptions(
    const int                                     toRank,
    const peano::heap::MessageType&               messageType,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  logDebug("sendEmptyCellDescriptions(...)","send empty message to " <<
          "rank "<<toRank <<
          " at (center="<< x.toString() <<
          ",level="<< level << ")");

  Heap::HeapEntries emptyMessage(0);
  Heap::getInstance().sendData(emptyMessage,
      toRank,x,level,messageType);
}

void exahype::solvers::ADERDGSolver::mergeCellDescriptionsWithRemoteData(
    const int                                    fromRank,
    exahype::Cell&                               localCell,
    const peano::heap::MessageType&              messageType,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  waitUntilAllBackgroundTasksHaveTerminated();
  tarch::multicore::Lock lock(_heapSemaphore);

  const int receivedCellDescriptionsIndex =
      Heap::getInstance().createData(0,exahype::solvers::RegisteredSolvers.size());
  Heap::getInstance().receiveData(
      receivedCellDescriptionsIndex,fromRank,x,level,messageType);

  logDebug("mergeCellDescriptionsWithRemoteData(...)","received " <<
          Heap::getInstance().getData(receivedCellDescriptionsIndex).size() <<
          " cell descriptions for cell ("
          "centre="<< x.toString() <<
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

      logDebug("mergeCellDescriptionsWithRemoteData(...)","setup metadata for " <<
                    "cell ("
                    "centre="<< x.toString() <<
                    ",level="<< level <<
                    ",isRoot="<< localCell.isRoot() <<
                    ",isAssignedToRemoteRank="<< localCell.isAssignedToRemoteRank());
    }
    assertion1(Heap::getInstance().isValidIndex(localCell.getCellDescriptionsIndex()),
               localCell.getCellDescriptionsIndex());
    Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).reserve(
        std::max(Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).size(),
                 Heap::getInstance().getData(receivedCellDescriptionsIndex).size()));

//    assertion(Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).empty());
    for (auto& pReceived : Heap::getInstance().getData(receivedCellDescriptionsIndex)) {
      logDebug("mergeCellDescriptionsWithRemoteData(...)","received " <<
              "cell description for cell ("
              "centre="<< x.toString() <<
              ",level="<< level <<
              ",isRoot="<< localCell.isRoot() <<
              ",isAssignedToRemoteRank="<< localCell.isAssignedToRemoteRank() <<
              ") with type="<< pReceived.getType());

      bool found = false;
      for (auto& pLocal : Heap::getInstance().getData(localCell.getCellDescriptionsIndex())) {
        if (pReceived.getSolverNumber()==pLocal.getSolverNumber()) {
          found = true;

          pLocal.setHasToHoldDataForMasterWorkerCommunication(false);

          assertion8(pReceived.getType()==pLocal.getType(),pReceived.getType(),pLocal.getType(),
                     pLocal.getOffset()+0.5*pLocal.getSize(),
                     pLocal.getLevel(),
                     pReceived.getOffset()+0.5*pReceived.getSize(),
                     pReceived.getLevel(),
                     x,
                     tarch::parallel::Node::getInstance().getRank());

          if (pLocal.getType()==CellDescription::Type::Cell ||
              pLocal.getType()==CellDescription::Type::Ancestor ||
              pLocal.getType()==CellDescription::Type::Descendant
          ) {
            assertionNumericalEquals2(pLocal.getCorrectorTimeStamp(),pReceived.getCorrectorTimeStamp(),
                                      pLocal.toString(),pReceived.toString());
            assertionNumericalEquals2(pLocal.getCorrectorTimeStepSize(),pReceived.getCorrectorTimeStepSize(),
                                      pLocal.toString(),pReceived.toString());
            assertionNumericalEquals2(pLocal.getPredictorTimeStamp(),pReceived.getPredictorTimeStamp(),
                                      pLocal.toString(),pReceived.toString());
            assertionNumericalEquals2(pLocal.getPredictorTimeStepSize(),pReceived.getPredictorTimeStepSize(),
                                      pLocal.toString(),pReceived.toString());
          }
        }
      }

      if (!found) {
        auto* solver = exahype::solvers::RegisteredSolvers[pReceived.getSolverNumber()];

        switch (solver->getType()) {
          case exahype::solvers::Solver::Type::ADERDG:
            static_cast<exahype::solvers::ADERDGSolver*>(solver)->ensureNecessaryMemoryIsAllocated(pReceived);
            break;
          case exahype::solvers::Solver::Type::LimitingADERDG:
            static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver()->ensureNecessaryMemoryIsAllocated(pReceived);
            break;
          case exahype::solvers::Solver::Type::FiniteVolumes:
            assertionMsg(false,"Solver type not supported!");
            break;
        }

        Heap::getInstance().getData(localCell.getCellDescriptionsIndex()).
            push_back(pReceived);
      }
    }
  }

  Heap::getInstance().deleteData(receivedCellDescriptionsIndex);
  assertion(!Heap::getInstance().isValidIndex(receivedCellDescriptionsIndex));
}

void exahype::solvers::ADERDGSolver::resetDataHeapIndices(
    const int cellDescriptionsIndex,
    const int parentIndex) {
  for (auto& p : Heap::getInstance().getData(cellDescriptionsIndex)) {
    p.setParentIndex(parentIndex);

    // Default field data indices
    p.setSolution(-1);
    p.setUpdate(-1);
    p.setExtrapolatedPredictor(-1);
    p.setFluctuation(-1);

    // Limiter meta data (oscillations identificator)
    p.setSolutionMin(-1);
    p.setSolutionMax(-1);
  }
}

/**
 * Drop cell descriptions received from \p fromRank.
 */
void exahype::solvers::ADERDGSolver::dropCellDescriptions(
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
exahype::solvers::ADERDGSolver::appendMasterWorkerCommunicationMetadata(
    MetadataHeap::HeapEntries& metadata,
    const int cellDescriptionsIndex,
    const int solverNumber) {
  const int element = tryGetElement(cellDescriptionsIndex,solverNumber);

  if (element!=exahype::solvers::Solver::NotFound)  {
    CellDescription& cellDescription =
        getCellDescription(cellDescriptionsIndex,element);
    metadata.push_back(
        (cellDescription.getHasToHoldDataForMasterWorkerCommunication()) ? 1 : 0 );
  } else {
    for (int i = 0; i < exahype::MasterWorkerCommunicationMetadataPerSolver; ++i) {
      metadata.push_back(exahype::InvalidMetadataEntry); // implicit conversion
    }
  }
}

void exahype::solvers::ADERDGSolver::mergeWithMasterWorkerMetadata(
      const MetadataHeap::HeapEntries& receivedMetadata,
      const int                        cellDescriptionsIndex,
      const int                        element) {
  if (element!=exahype::solvers::Solver::NotFound)  {
    CellDescription& cellDescription =
        getCellDescription(cellDescriptionsIndex,element);

    int index=0;
    cellDescription.setHasToHoldDataForMasterWorkerCommunication(
        (receivedMetadata[index++].getU()==1) ? true : false );
  }
}

///////////////////////////////////
// FORK OR JOIN
///////////////////////////////////

void exahype::solvers::ADERDGSolver::sendDataToWorkerOrMasterDueToForkOrJoin(
    const int                                     toRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());
  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];

  if (cellDescription.getType()==CellDescription::Cell) {
    double* solution = DataHeap::getInstance().getData(cellDescription.getSolution()).data();

    logDebug("sendDataToWorkerOrMasterDueToForkOrJoin(...)",""
        "solution of solver " << cellDescription.getSolverNumber() << " sent to rank "<<toRank<<
             ", cell: "<< x << ", level: " << level);

    DataHeap::getInstance().sendData(
        solution, getUnknownsPerCell(), toRank, x, level,
        peano::heap::MessageType::ForkOrJoinCommunication);
  }
}


void exahype::solvers::ADERDGSolver::sendEmptyDataToWorkerOrMasterDueToForkOrJoin(
    const int                                     toRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerForkOrJoinCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, toRank, x, level,
        peano::heap::MessageType::ForkOrJoinCommunication);
}


void exahype::solvers::ADERDGSolver::mergeWithWorkerOrMasterDataDueToForkOrJoin(
    const int                                     fromRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  auto& p = exahype::solvers::ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex)[element];
  assertion4(tarch::la::equals(x,p.getOffset()+0.5*p.getSize()),x,p.getOffset()+0.5*p.getSize(),level,p.getLevel());
  assertion2(p.getLevel()==level,p.getLevel(),level);

  if (p.getType()==CellDescription::Cell) {
    logDebug("mergeWithRemoteDataDueToForkOrJoin(...)","[solution] receive from rank "<<fromRank<<
             ", cell: "<< x << ", level: " << level);

    DataHeap::getInstance().getData(p.getSolution()).clear();
    DataHeap::getInstance().receiveData(
        p.getSolution(),fromRank,x,level,
        peano::heap::MessageType::ForkOrJoinCommunication);
  }
}

void exahype::solvers::ADERDGSolver::dropWorkerOrMasterDataDueToForkOrJoin(
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
exahype::solvers::ADERDGSolver::appendNeighbourCommunicationMetadata(
    exahype::MetadataHeap::HeapEntries& metadata,
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest,
    const int cellDescriptionsIndex,
    const int solverNumber) {
  const int element = tryGetElement(cellDescriptionsIndex,solverNumber);

  if (element!=exahype::solvers::Solver::NotFound)  {
    CellDescription& cellDescription =
        getCellDescription(cellDescriptionsIndex,element);

    const int direction   = tarch::la::equalsReturnIndex(src, dest);
    const int orientation = (1 + dest(direction) - src(direction))/2;
    const int faceIndex   = 2*direction+orientation;

    metadata.push_back(static_cast<int>(cellDescription.getType()));
    metadata.push_back(cellDescription.getFacewiseAugmentationStatus(faceIndex));
    metadata.push_back(cellDescription.getFacewiseHelperStatus(faceIndex));
    metadata.push_back(cellDescription.getFacewiseLimiterStatus(faceIndex));
  } else {
    for (int i = 0; i < exahype::NeighbourCommunicationMetadataPerSolver; ++i) {
      metadata.push_back(exahype::InvalidMetadataEntry); // implicit conversion
    }
  }
}

void exahype::solvers::ADERDGSolver::mergeWithNeighbourMetadata(
    const exahype::MetadataHeap::HeapEntries& neighbourMetadata,
    const tarch::la::Vector<DIMENSIONS, int>& src,
    const tarch::la::Vector<DIMENSIONS, int>& dest,
    const int                                 cellDescriptionsIndex,
    const int                                 element) {
  if (tarch::la::countEqualEntries(src,dest)!=DIMENSIONS-1) { // only consider faces
    return;
  }
  const int direction   = tarch::la::equalsReturnIndex(src, dest);
  const int orientation = (1 + src(direction) - dest(direction))/2;
  const int faceIndex   = 2*direction+orientation;

  const int neighbourAugmentationStatus =
      neighbourMetadata[exahype::NeighbourCommunicationMetadataAugmentationStatus].getU();
  const int neighbourHelperStatus       =
      neighbourMetadata[exahype::NeighbourCommunicationMetadataHelperStatus      ].getU();
  const int neighbourLimiterStatus      =
        neighbourMetadata[exahype::NeighbourCommunicationMetadataLimiterStatus   ].getU();

  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);

  mergeWithAugmentationStatus(cellDescription,faceIndex,neighbourAugmentationStatus);
  mergeWithHelperStatus      (cellDescription,faceIndex,neighbourHelperStatus);
  mergeWithLimiterStatus     (cellDescription,faceIndex,neighbourLimiterStatus);
}

void exahype::solvers::ADERDGSolver::sendDataToNeighbour(
    const int                                     toRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, int>&     src,
    const tarch::la::Vector<DIMENSIONS, int>&     dest,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion( tarch::la::countEqualEntries(src,dest)==(DIMENSIONS-1) );

  const int direction    = tarch::la::equalsReturnIndex(src, dest);
  const int orientation  = (1 + dest(direction) - src(direction))/2;
  const int faceIndex    = 2*direction+orientation;

  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
  if (holdsFaceData(cellDescription)) {
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor()));
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getFluctuation()));

    const int numberOfFaceDof = getBndFaceSize();
    const double* lQhbnd = DataHeap::getInstance().getData(
        cellDescription.getExtrapolatedPredictor()).data() +
        (faceIndex * numberOfFaceDof);

    const int numberOfFluxDof = getBndFluxSize();
    const double* lFhbnd = DataHeap::getInstance().getData(
        cellDescription.getFluctuation()).data() +
        (faceIndex * numberOfFluxDof);

    logDebug(
        "sendDataToNeighbour(...)",
        "send "<<DataMessagesPerNeighbourCommunication<<" arrays to rank " <<
        toRank << " for cell="<<cellDescription.getOffset()<< " and face=" << faceIndex << " from vertex x=" << x << ", level=" << level <<
        ", src type=" << multiscalelinkedcell::indexToString(cellDescriptionsIndex) <<
        ", src=" << src << ", dest=" << dest <<
        ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    // Send order: lQhbnd,lFhbnd
    // Receive order: lFhbnd,lQhbnd
    DataHeap::getInstance().sendData(
        lQhbnd, numberOfFaceDof, toRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);
    DataHeap::getInstance().sendData(
        lFhbnd, numberOfFluxDof, toRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);
    // TODO(Dominic): If anarchic time stepping send the time step over too.
  } else {
    std::vector<double> emptyMessage(0);

    for(int sends=0; sends<DataMessagesPerNeighbourCommunication; ++sends) {
      DataHeap::getInstance().sendData(
          emptyMessage, toRank, x, level,
          peano::heap::MessageType::NeighbourCommunication);
    }
  }
}

void exahype::solvers::ADERDGSolver::sendEmptyDataToNeighbour(
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

void exahype::solvers::ADERDGSolver::mergeWithNeighbourData(
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
  const int direction    = tarch::la::equalsReturnIndex(src, dest);
  const int orientation  = (1 + src(direction) - dest(direction))/2;
  const int faceIndex    = 2*direction+orientation;

  waitUntilAllBackgroundTasksHaveTerminated();

  CellDescription& cellDescription = getCellDescription(cellDescriptionsIndex,element);
  // TODO(Dominic): Add to docu: We only perform a Riemann solve if a Cell is involved.
  // Solving Riemann problems at a Ancestor Ancestor boundary might lead to problems
  // if one Ancestor is just used for restriction.
  CellDescription::Type neighbourType =
      static_cast<CellDescription::Type>(neighbourMetadata[exahype::NeighbourCommunicationMetadataCellType].getU());
  if(neighbourType==CellDescription::Type::Cell || cellDescription.getType()==CellDescription::Type::Cell){
    tarch::multicore::Lock lock(_heapSemaphore);
    assertion2(holdsFaceData(cellDescription),cellDescription.toString(),tarch::parallel::Node::getInstance().getRank());

    const int dataPerFace = getBndFaceSize();
    const int dofPerFace  = getBndFluxSize();
    const int receivedlQhbndIndex   = DataHeap::getInstance().createData(dataPerFace, dataPerFace);
    const int receivedlFhbndIndex   = DataHeap::getInstance().createData(dofPerFace, dofPerFace);

    assertion(!DataHeap::getInstance().getData(receivedlQhbndIndex).empty());
    assertion(!DataHeap::getInstance().getData(receivedlFhbndIndex).empty());
    assertion4(!cellDescription.getNeighbourMergePerformed(faceIndex),
        faceIndex,cellDescriptionsIndex,cellDescription.getOffset().toString(),cellDescription.getLevel());
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getExtrapolatedPredictor()));
    assertion(DataHeap::getInstance().isValidIndex(cellDescription.getFluctuation()));
    logDebug(
        "mergeWithNeighbourData(...)", "receive "<<DataMessagesPerNeighbourCommunication<<" arrays from rank " <<
        fromRank << " for vertex x=" << x << ", level=" << level <<
        ", src type=" << cellDescription.getType() <<
        ", src=" << src << ", dest=" << dest <<
        ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    // Send order: lQhbnd,lFhbnd
    // Receive order: lFhbnd,lQhbnd // TODO change to double variant
    DataHeap::getInstance().receiveData(
        DataHeap::getInstance().getData(receivedlFhbndIndex).data(),dataPerFace,
        fromRank, x, level,peano::heap::MessageType::NeighbourCommunication);
    DataHeap::getInstance().receiveData(
        DataHeap::getInstance().getData(receivedlQhbndIndex).data(),dataPerFace,
        fromRank, x, level,peano::heap::MessageType::NeighbourCommunication);
    logDebug(
        "mergeWithNeighbourData(...)", "[pre] solve Riemann problem with received data." <<
        " cellDescription=" << cellDescription.toString() <<
        ",faceIndexForCell=" << faceIndex <<
        ",normalOfExchangedFac=" << direction <<
        ",x=" << x.toString() << ", level=" << level <<
        ", counter=" << cellDescription.getFaceDataExchangeCounter(faceIndex)
    );

    // TODO collect neighbour time step data here out of receivedMinMax and
    // use it for solveRiemannProblemAtInterface.

    solveRiemannProblemAtInterface(
        cellDescription,
        faceIndex,
        receivedlQhbndIndex,
        receivedlFhbndIndex,
        tempFaceUnknowns,
        tempStateSizedVectors,
        tempStateSizedSquareMatrices);

    // TODO(Dominic): If anarchic time stepping, receive the time step too.

    DataHeap::getInstance().deleteData(receivedlQhbndIndex,true);
    DataHeap::getInstance().deleteData(receivedlFhbndIndex,true);
  } else  {
    dropNeighbourData(fromRank,src,dest,x,level);
  }
}

void exahype::solvers::ADERDGSolver::solveRiemannProblemAtInterface(
    records::ADERDGCellDescription& cellDescription,
    const int faceIndex,
    const int indexOfQValues,
    const int indexOfFValues,
    double**  tempFaceUnknowns,
    double**  tempStateSizedVectors,
    double**  tempStateSizedSquareMatrices) {
  cellDescription.setNeighbourMergePerformed(faceIndex, true);

  const int dataPerFace = getBndFaceSize();
  const int dofPerFace  = getBndFluxSize();

  logDebug("solveRiemannProblemAtInterface(...)",
      "cell-description=" << cellDescription.toString());

  double* QL = 0;
  double* QR = 0;
  double* FL = 0;
  double* FR = 0;

  assertionEquals(DataHeap::getInstance().getData(indexOfQValues).size(),
      static_cast<unsigned int>(dataPerFace));
  assertionEquals(DataHeap::getInstance().getData(indexOfFValues).size(),
      static_cast<unsigned int>(dofPerFace));

  // @todo Doku im Header warum wir das hier brauchen,
  if (faceIndex % 2 == 0) {
    QL = DataHeap::getInstance().getData(indexOfQValues).data();
    QR = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data() +
        (faceIndex * dataPerFace);
    FL = DataHeap::getInstance().getData(indexOfFValues).data();
    FR = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data() +
        (faceIndex * dofPerFace);
  } else {
    QR = DataHeap::getInstance().getData(indexOfQValues).data();
    QL = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data() +
        (faceIndex * dataPerFace);
    FR = DataHeap::getInstance().getData(indexOfFValues).data();
    FL = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data() +
        (faceIndex * dofPerFace);
  }

  // Synchronise time stepping.
  synchroniseTimeStepping(cellDescription);

  const int normalDirection = (faceIndex - faceIndex%2)/2; // faceIndex=2*normalNonZero+f, f=0,1
  assertion2(normalDirection<DIMENSIONS,faceIndex,normalDirection);
  riemannSolver(FL, FR, QL, QR,
      tempFaceUnknowns[0],tempStateSizedVectors,tempStateSizedSquareMatrices,
      cellDescription.getCorrectorTimeStepSize(),
      normalDirection,false);

  for (int ii = 0; ii<dataPerFace; ii++) {
    assertion10(std::isfinite(QR[ii]), cellDescription.toString(),
        faceIndex, normalDirection, indexOfQValues, indexOfFValues,
        ii, QR[ii], QL[ii], FR[ii], FL[ii]);
    assertion10(std::isfinite(QL[ii]), cellDescription.toString(),
        faceIndex, normalDirection, indexOfQValues, indexOfFValues,
        ii, QR[ii], QL[ii], FR[ii], FL[ii]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.

  for (int ii = 0; ii<dofPerFace; ii++) {
    assertion10(std::isfinite(FR[ii]), cellDescription.toString(),
        faceIndex, normalDirection, indexOfQValues, indexOfFValues,
        ii, QR[ii], QL[ii], FR[ii], FL[ii]);
    assertion10(std::isfinite(FL[ii]), cellDescription.toString(),
        faceIndex, normalDirection, indexOfQValues, indexOfFValues,
        ii, QR[ii], QL[ii], FR[ii], FL[ii]);
  }  // Dead code elimination will get rid of this loop if Asserts flag is not set.
}

void exahype::solvers::ADERDGSolver::dropNeighbourData(
    const int                                     fromRank,
    const tarch::la::Vector<DIMENSIONS, int>&     src,
    const tarch::la::Vector<DIMENSIONS, int>&     dest,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  logDebug(
      "dropNeighbourData(...)", "drop "<<DataMessagesPerNeighbourCommunication<<" arrays from rank " <<
      fromRank << " for vertex x=" << x << ", level=" << level <<
      ", src=" << src << ", dest=" << dest
  );

  for(int receives=0; receives<DataMessagesPerNeighbourCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        fromRank, x, level,
        peano::heap::MessageType::NeighbourCommunication);
}

///////////////////////////////////
// WORKER->MASTER
///////////////////////////////////
exahype::DataHeap::HeapEntries
exahype::solvers::ADERDGSolver::compileMessageForMaster(const int capacity) const {
  DataHeap::HeapEntries messageForMaster(0,std::max(4,capacity));
  messageForMaster.push_back(_minPredictorTimeStepSize);
  messageForMaster.push_back(_minCellSize);
  messageForMaster.push_back(_maxCellSize);
  messageForMaster.push_back(_meshUpdateRequest ? 1.0 : -1.0);
  return messageForMaster;
}

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
void exahype::solvers::ADERDGSolver::sendDataToMaster(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level){
  DataHeap::HeapEntries messageForMaster = compileMessageForMaster();

  assertion1(messageForMaster.size()==4,messageForMaster.size());
  assertion1(std::isfinite(messageForMaster[0]),messageForMaster[0]);
  if (_timeStepping==TimeStepping::Global) {
    assertionNumericalEquals1(_minNextPredictorTimeStepSize,std::numeric_limits<double>::max(),
                                tarch::parallel::Node::getInstance().getRank());
  }

  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("sendDataToMaster(...)","Sending time step data: " <<
             "data[0]=" << messageForMaster[0] <<
             ",data[1]=" << messageForMaster[1] <<
             ",data[2]=" << messageForMaster[2] <<
             ",data[3]=" << messageForMaster[3]);
  }

  DataHeap::getInstance().sendData(
      messageForMaster.data(), messageForMaster.size(),
      masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::ADERDGSolver::mergeWithWorkerData(const DataHeap::HeapEntries& message) {
  assertion1(message[0]>=0,message[0]);
  assertion1(std::isfinite(message[0]),message[0]);
  // The master solver has not yet updated its minNextPredictorTimeStepSize.
  // Thus it does not equal MAX_DOUBLE.

  int index=0;
  _minNextPredictorTimeStepSize  = std::min( _minNextPredictorTimeStepSize, message[index++] );
  _nextMinCellSize               = std::min( _nextMinCellSize, message[index++] );
  _nextMaxCellSize               = std::max( _nextMaxCellSize, message[index++] );
  _nextMeshUpdateRequest        |= (message[index++]) > 0 ? true : false;

  if (true || tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithWorkerData(...)","[post] Receiving time step data: " <<
        "data[0]=" << message[0] <<
        ",data[1]=" << message[1] <<
        ",data[2]=" << message[2] <<
        ",data[3]=" << message[3] );
    logDebug("mergeWithWorkerData(...)","[post] Updated time step fields: " <<
        ",_minNextPredictorTimeStepSize=" << _minNextPredictorTimeStepSize <<
        ",_nextMeshUpdateRequest=" << _nextMeshUpdateRequest <<
        ",_nextMinCellSize=" << _nextMinCellSize <<
        ",_nextMaxCellSize=" << _nextMaxCellSize);
  }
}

/**
 * At the time of the merging,
 * the workers and the master have already performed
 * at local update of the next predictor time step size
 * and of the predictor time stamp.
 * We thus need to minimise over both quantities.
 */
void exahype::solvers::ADERDGSolver::mergeWithWorkerData(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  DataHeap::HeapEntries messageFromWorker(4);

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithWorkerData(...)","Receiving time step data [pre] from rank " << workerRank);
  }

  DataHeap::getInstance().receiveData(
      messageFromWorker.data(),messageFromWorker.size(),workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);

  assertion1(messageFromWorker.size()==4,messageFromWorker.size());
  mergeWithWorkerData(messageFromWorker);
}

void exahype::solvers::ADERDGSolver::sendEmptyDataToMaster(
    const int                                     masterRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  logDebug("sendEmptyDataToMaster(...)","empty data for solver sent to rank "<<masterRank<<
           ", cell: "<< x << ", level: " << level);

  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerMasterWorkerCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::ADERDGSolver::sendDataToMaster(
    const int                                     masterRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): Add sends of min and max

  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
  if (
      cellDescription.getType()==CellDescription::Ancestor
      &&
      cellDescription.getHasToHoldDataForMasterWorkerCommunication()
   ) {
    double* extrapolatedPredictor = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
    double* fluctuations          = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();

    logDebug("sendDataToMaster(...)","face data of solver " << cellDescription.getSolverNumber() << " sent to rank "<<masterRank<<
             ", cell: "<< x << ", level: " << level);

    // No inverted message order since we do synchronous data exchange.
    // Order: extrapolatedPredictor,fluctuations.
    DataHeap::getInstance().sendData(
        extrapolatedPredictor, getBndTotalSize(), masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
    DataHeap::getInstance().sendData(
        fluctuations, getBndFluxTotalSize(), masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
  } else {
    sendEmptyDataToMaster(masterRank,x,level);
  }
}

void exahype::solvers::ADERDGSolver::mergeWithWorkerData(
    const int                                     workerRank,
    const MetadataHeap::HeapEntries&              workerMetadata,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  logDebug("mergeWithWorkerData(...)","merge with worker data from rank "<<workerRank<<
             ", cell: "<< x << ", level: " << level);

  // TODO(Dominic): Add merges of min and max

  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
  if (
      cellDescription.getType()==CellDescription::Type::Ancestor
      &&
      cellDescription.getHasToHoldDataForMasterWorkerCommunication()
  ) {
    logDebug("mergeWithWorkerData(...)","Received face data for solver " <<
             cellDescription.getSolverNumber() << " from Rank "<<workerRank<<
             ", cell: "<< x << ", level: " << level);

    // No inverted message order since we do synchronous data exchange.
    // Order: extrapolatedPredictor,fluctuations.
    // Make sure you clear the arrays before you append(!) data via receive!
    DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).clear();
    DataHeap::getInstance().getData(cellDescription.getFluctuation()).clear();

    DataHeap::getInstance().receiveData(
        cellDescription.getExtrapolatedPredictor(), workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
    DataHeap::getInstance().receiveData(
        cellDescription.getFluctuation(), workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);

    exahype::solvers::Solver::SubcellPosition subcellPosition =
        computeSubcellPositionOfCellOrAncestor(cellDescriptionsIndex,element);

    // TODO(Dominic): Add to docu. I can be the top most Ancestor too.
    if (subcellPosition.parentElement!=exahype::solvers::Solver::NotFound) {
      #if defined(Debug)
      CellDescription& parentCellDescription =
          getCellDescription(subcellPosition.parentCellDescriptionsIndex,subcellPosition.parentElement);
      #endif

      logDebug("mergeWithWorkerData(...)","restricting face data for solver " <<
               cellDescription.getSolverNumber() << " from Rank "<<workerRank<<
               " from cell="<< x << ", level=" << level <<
               " to cell="<<parentCellDescription.getOffset()+0.5*parentCellDescription.getSize() <<
               " level="<<parentCellDescription.getLevel());

      restrictToTopMostParent(
          cellDescriptionsIndex,element,
          subcellPosition.parentCellDescriptionsIndex,subcellPosition.parentElement,
          subcellPosition.subcellIndex);
    }
  } else  {
    dropWorkerData(workerRank,x,level);
  }
}

void exahype::solvers::ADERDGSolver::dropWorkerData(
    const int                                     workerRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  logDebug("dropWorkerData(...)","dropping worker data from rank "<<workerRank<<
             ", cell: "<< x << ", level: " << level);

  for(int receives=0; receives<DataMessagesPerMasterWorkerCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}

///////////////////////////////////
// MASTER->WORKER
///////////////////////////////////
exahype::DataHeap::HeapEntries
exahype::solvers::ADERDGSolver::compileMessageForWorker(const int capacity) const {
  DataHeap::HeapEntries messageForWorker(0,std::max(8,capacity));
  messageForWorker.push_back(_minCorrectorTimeStamp);
  messageForWorker.push_back(_minCorrectorTimeStepSize);
  messageForWorker.push_back(_minPredictorTimeStamp);
  messageForWorker.push_back(_minPredictorTimeStepSize);

  messageForWorker.push_back(_minCellSize);
  messageForWorker.push_back(_maxCellSize);

  messageForWorker.push_back(_meshUpdateRequest ? 1.0 : -1.0);

  messageForWorker.push_back(_stabilityConditionWasViolated ? 1.0 : -1.0);

  assertion1(messageForWorker.size()==8,messageForWorker.size());
  assertion1(std::isfinite(messageForWorker[0]),messageForWorker[0]);
  assertion1(std::isfinite(messageForWorker[1]),messageForWorker[1]);
  assertion1(std::isfinite(messageForWorker[2]),messageForWorker[2]);
  assertion1(std::isfinite(messageForWorker[3]),messageForWorker[3]);

  if (_timeStepping==TimeStepping::Global) {
    assertionEquals1(_minNextPredictorTimeStepSize,std::numeric_limits<double>::max(),
                     tarch::parallel::Node::getInstance().getRank());
  }

  return messageForWorker;
}

void exahype::solvers::ADERDGSolver::sendDataToWorker(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  DataHeap::HeapEntries messageForWorker = compileMessageForWorker();

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("sendDataToWorker(...)","Broadcasting time step data: " <<
            " data[0]=" << messageForWorker[0] <<
            ",data[1]=" << messageForWorker[1] <<
            ",data[2]=" << messageForWorker[2] <<
            ",data[3]=" << messageForWorker[3] <<
            ",data[4]=" << messageForWorker[4] <<
            ",data[5]=" << messageForWorker[5] <<
            ",data[6]=" << messageForWorker[6] <<
            ",data[7]=" << messageForWorker[7]);
    logDebug("sendDataWorker(...)","_minNextPredictorTimeStepSize="<<_minNextPredictorTimeStepSize);
  }

  DataHeap::getInstance().sendData(
      messageForWorker.data(), messageForWorker.size(),
      workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::ADERDGSolver::mergeWithMasterData(const DataHeap::HeapEntries& message) {
  int index=0;
  _minCorrectorTimeStamp         = message[index++];
  _minCorrectorTimeStepSize      = message[index++];
  _minPredictorTimeStamp         = message[index++];
  _minPredictorTimeStepSize      = message[index++];

  _minCellSize                   = message[index++];
  _maxCellSize                   = message[index++];

  _meshUpdateRequest             = (message[index++] > 0.0) ? true : false;
  _stabilityConditionWasViolated = (message[index++] > 0.0) ? true : false;

  logDebug("mergeWithMasterData(...)",
      "_stabilityConditionWasViolated="<< _stabilityConditionWasViolated);
}

void exahype::solvers::ADERDGSolver::mergeWithMasterData(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  DataHeap::HeapEntries messageFromMaster(8);
  DataHeap::getInstance().receiveData(
      messageFromMaster.data(),messageFromMaster.size(),masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);

  assertion1(messageFromMaster.size()==8,messageFromMaster.size());
  mergeWithMasterData(messageFromMaster);

  if (_timeStepping==TimeStepping::Global) {
    assertionNumericalEquals1(_minNextPredictorTimeStepSize,std::numeric_limits<double>::max(),
                                  _minNextPredictorTimeStepSize);
  }

  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithMasterData(...)","Received time step data: " <<
            "data[0]="  << messageFromMaster[0] <<
            ",data[1]=" << messageFromMaster[1] <<
            ",data[2]=" << messageFromMaster[2] <<
            ",data[3]=" << messageFromMaster[3] <<
            ",data[4]=" << messageFromMaster[4] <<
            ",data[5]=" << messageFromMaster[5]<<
            ",data[6]=" << messageFromMaster[6]<<
            ",data[7]=" << messageFromMaster[7]);
  }
}

bool exahype::solvers::ADERDGSolver::hasToSendDataToMaster(
    const int cellDescriptionsIndex,
    const int element) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
  if (
      cellDescription.getType()==CellDescription::Ancestor
  ) {
    #if defined(Debug) || defined(Asserts)
    exahype::solvers::Solver::SubcellPosition subcellPosition =
        computeSubcellPositionOfCellOrAncestor(cellDescriptionsIndex,element);
    assertion(cellDescription.getHasToHoldDataForMasterWorkerCommunication() ||
              subcellPosition.parentElement==exahype::solvers::Solver::NotFound);
    #endif

    return cellDescription.getHasToHoldDataForMasterWorkerCommunication();
  }

  return false;
}

void exahype::solvers::ADERDGSolver::sendDataToWorker(
    const int                                     workerRank,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level) {
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): Add sends of min and max

  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
  if (
      cellDescription.getType()==CellDescription::Descendant &&
      cellDescription.getHasToHoldDataForMasterWorkerCommunication()
  ) {
    exahype::solvers::Solver::SubcellPosition subcellPosition =
        exahype::amr::computeSubcellPositionOfDescendant<CellDescription,Heap,false>(cellDescription);
    prolongateFaceDataToDescendant(cellDescription,subcellPosition);

    double* extrapolatedPredictor = DataHeap::getInstance().getData(cellDescription.getExtrapolatedPredictor()).data();
    double* fluctuations          = DataHeap::getInstance().getData(cellDescription.getFluctuation()).data();

    DataHeap::getInstance().sendData(
        extrapolatedPredictor, getBndTotalSize(), workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication); // No inverted message order since we do synchronous data exchange.
                                                              // Order: extraplolatedPredictor,fluctuations.
    DataHeap::getInstance().sendData(
        fluctuations, getBndFluxTotalSize(), workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);

    logDebug("sendDataToWorker(...)","sent face data of solver " <<
             cellDescription.getSolverNumber() << " to rank "<< workerRank <<
             ", cell: "<< x << ", level: " << level);
  } else {
    sendEmptyDataToWorker(workerRank,x,level);
  }
}

void exahype::solvers::ADERDGSolver::sendEmptyDataToWorker(
    const int                                     workerRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  std::vector<double> emptyMessage(0);
  for(int sends=0; sends<DataMessagesPerMasterWorkerCommunication; ++sends)
    DataHeap::getInstance().sendData(
        emptyMessage, workerRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::ADERDGSolver::mergeWithMasterData(
    const int                                     masterRank,
    const MetadataHeap::HeapEntries&              masterMetadata,
    const int                                     cellDescriptionsIndex,
    const int                                     element,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
    const int                                     level){
  assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);
  assertion1(element>=0,element);
  assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),
             element,Heap::getInstance().getData(cellDescriptionsIndex).size());

  // TODO(Dominic): Add merges of min and max

  CellDescription& cellDescription = Heap::getInstance().getData(cellDescriptionsIndex)[element];
  if (
      cellDescription.getType()==CellDescription::Descendant &&
      cellDescription.getHasToHoldDataForMasterWorkerCommunication()
  ) {
    logDebug("mergeWithMasterData(...)","received face data for solver " <<
             cellDescription.getSolverNumber() << " from rank "<<masterRank<<
             ", cell: "<< x << ", level: " << level);

    // No inverted send and receives order since we do synchronous data exchange.
    // Order: extraplolatedPredictor,fluctuations
    DataHeap::getInstance().receiveData(
        cellDescription.getExtrapolatedPredictor(), masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
    DataHeap::getInstance().receiveData(
        cellDescription.getFluctuation(), masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
  } else {
    dropMasterData(masterRank,x,level);
  }
}

void exahype::solvers::ADERDGSolver::dropMasterData(
    const int                                     masterRank,
    const tarch::la::Vector<DIMENSIONS, double>&  x,
        const int                                     level) {
  for(int receives=0; receives<DataMessagesPerMasterWorkerCommunication; ++receives)
    DataHeap::getInstance().receiveData(
        masterRank, x, level,
        peano::heap::MessageType::MasterWorkerCommunication);
}
#endif

std::string exahype::solvers::ADERDGSolver::toString() const {
  std::ostringstream stringstr;
  toString(stringstr);
  return stringstr.str();
}

void exahype::solvers::ADERDGSolver::toString (std::ostream& out) const {
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
  out << "_unknownsPerFace:" << _dofPerFace;
  out << ",";
  out << "_unknownsPerCellBoundary:" << _dofPerCellBoundary;
  out << ",";
  out << "_unknownsPerCell:" << _dofPerCell;
  out << ",";
  out << "_fluxUnknownsPerCell:" << _fluxDofPerCell;
  out << ",";
  out << "_spaceTimeUnknownsPerCell:" << _spaceTimeDofPerCell;
  out << ",";
  out << "_spaceTimeFluxUnknownsPerCell:" << _spaceTimeFluxDofPerCell;
  out << ",";
  out << "_previousMinCorrectorTimeStepSize:" << _previousMinCorrectorTimeStepSize;
  out << ",";
  out << "_minCorrectorTimeStamp:" << _minCorrectorTimeStamp;
  out << ",";
  out << "_minCorrectorTimeStepSize:" << _minCorrectorTimeStepSize;
  out << ",";
  out << "_minPredictorTimeStepSize:" << _minPredictorTimeStepSize;
  out << ",";
  out << "_minNextPredictorTimeStepSize:" << _minNextPredictorTimeStepSize;
  out <<  ")";
}


exahype::solvers::ADERDGSolver::CompressionTask::CompressionTask(
  ADERDGSolver&                             solver,
  exahype::records::ADERDGCellDescription&  cellDescription
):
  _solver(solver),
  _cellDescription(cellDescription) {
}


void exahype::solvers::ADERDGSolver::CompressionTask::operator()() {
  _solver.determineUnknownAverages(_cellDescription);
  _solver.computeHierarchicalTransform(_cellDescription,-1.0);
  _solver.putUnknownsIntoByteStream(_cellDescription);

  tarch::multicore::Lock lock(_heapSemaphore);
  _cellDescription.setCompressionState(exahype::records::ADERDGCellDescription::Compressed);
  _NumberOfTriggeredTasks--;
  assertion( _NumberOfTriggeredTasks>=0 );
}


void exahype::solvers::ADERDGSolver::compress(exahype::records::ADERDGCellDescription& cellDescription) {
  assertion1( cellDescription.getCompressionState() ==  exahype::records::ADERDGCellDescription::Uncompressed, cellDescription.toString() );
  if (CompressionAccuracy>0.0) {
    if (SpawnCompressionAsBackgroundThread) {
      cellDescription.setCompressionState(exahype::records::ADERDGCellDescription::CurrentlyProcessed);

      tarch::multicore::Lock lock(_heapSemaphore);
      _NumberOfTriggeredTasks++;
      lock.free();

      CompressionTask myTask( *this, cellDescription );
      peano::datatraversal::TaskSet spawnedSet( myTask );
    }
    else {
      determineUnknownAverages(cellDescription);
      computeHierarchicalTransform(cellDescription,-1.0);
      putUnknownsIntoByteStream(cellDescription);
      cellDescription.setCompressionState(exahype::records::ADERDGCellDescription::Compressed);
    }
  }
}


void exahype::solvers::ADERDGSolver::uncompress(exahype::records::ADERDGCellDescription& cellDescription) {
  #ifdef SharedMemoryParallelisation
  bool madeDecision = CompressionAccuracy<=0.0;
  bool uncompress   = false;

  while (!madeDecision) {
    tarch::multicore::Lock lock(_heapSemaphore);
    madeDecision = cellDescription.getCompressionState() != exahype::records::ADERDGCellDescription::CurrentlyProcessed;
    uncompress   = cellDescription.getCompressionState() == exahype::records::ADERDGCellDescription::Compressed;
    if (uncompress) {
      cellDescription.setCompressionState( exahype::records::ADERDGCellDescription::CurrentlyProcessed );
    }
    lock.free();

    tarch::multicore::BooleanSemaphore::sendTaskToBack();
  }
  #else
  bool uncompress = CompressionAccuracy>0.0
      && cellDescription.getCompressionState() == exahype::records::ADERDGCellDescription::Compressed;
  #endif

/*
  #ifdef Parallel
  assertion1(!cellDescription.getAdjacentToRemoteRank() || cellDescription.getCompressionState() == exahype::records::ADERDGCellDescription::Compressed,
             cellDescription.toString());
  #endif
*/

  if (uncompress) {
    pullUnknownsFromByteStream(cellDescription);
    computeHierarchicalTransform(cellDescription,1.0);

    tarch::multicore::Lock lock(_heapSemaphore);
    cellDescription.setCompressionState(exahype::records::ADERDGCellDescription::Uncompressed);
  }
}


void exahype::solvers::ADERDGSolver::determineUnknownAverages(
  exahype::records::ADERDGCellDescription& cellDescription
) {
  for (int variableNumber=0; variableNumber<getNumberOfVariables()+getNumberOfParameters(); variableNumber++) {
    double solutionAverage         = 0.0;
    double previousSolutionAverage = 0.0;
    double updateAverage           = 0.0;

    int    numberOfDoFsPerVariable  = power(getNodesPerCoordinateAxis(), DIMENSIONS);
    for (int i=0; i<numberOfDoFsPerVariable; i++) {
      solutionAverage         += DataHeap::getInstance().getData( cellDescription.getSolution()         )[i + variableNumber * numberOfDoFsPerVariable];
      previousSolutionAverage += DataHeap::getInstance().getData( cellDescription.getPreviousSolution() )[i + variableNumber * numberOfDoFsPerVariable];
      updateAverage           += DataHeap::getInstance().getData( cellDescription.getUpdate()           )[i + variableNumber * numberOfDoFsPerVariable];
    }
    DataHeap::getInstance().getData( cellDescription.getSolutionAverages()         )[variableNumber] = solutionAverage         / numberOfDoFsPerVariable;
    DataHeap::getInstance().getData( cellDescription.getPreviousSolutionAverages() )[variableNumber] = previousSolutionAverage / numberOfDoFsPerVariable;
    DataHeap::getInstance().getData( cellDescription.getUpdateAverages()           )[variableNumber] = updateAverage           / numberOfDoFsPerVariable;

    for (int face=0; face<2*DIMENSIONS; face++) {
      double extrapolatedPredictorAverage = 0.0;
      double boundaryFluxAverage          = 0.0;
      int    numberOfDoFsPerVariable  = power(getNodesPerCoordinateAxis(), DIMENSIONS-1);
      for (int i=0; i<numberOfDoFsPerVariable; i++) {
        extrapolatedPredictorAverage += DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictor() )[i + variableNumber * numberOfDoFsPerVariable + getNumberOfVariables() * numberOfDoFsPerVariable * face ];
        boundaryFluxAverage          += DataHeap::getInstance().getData( cellDescription.getFluctuation()           )[i + variableNumber * numberOfDoFsPerVariable + getNumberOfVariables() * numberOfDoFsPerVariable * face ];
      }
      DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictorAverages() )[variableNumber+getNumberOfVariables() * face] = extrapolatedPredictorAverage / numberOfDoFsPerVariable;
      DataHeap::getInstance().getData( cellDescription.getFluctuationAverages()           )[variableNumber+getNumberOfVariables() * face] = boundaryFluxAverage   / numberOfDoFsPerVariable;
    }
  }
}


void exahype::solvers::ADERDGSolver::computeHierarchicalTransform(exahype::records::ADERDGCellDescription& cellDescription, double sign) {
  for (int variableNumber=0; variableNumber<getNumberOfVariables()+getNumberOfParameters(); variableNumber++) {
    int    numberOfDoFsPerVariable  = power(getNodesPerCoordinateAxis(), DIMENSIONS);
    for (int i=0; i<numberOfDoFsPerVariable; i++) {
      DataHeap::getInstance().getData( cellDescription.getSolution()         )[i + variableNumber * numberOfDoFsPerVariable] += sign * DataHeap::getInstance().getData( cellDescription.getSolutionAverages() )[variableNumber];
      DataHeap::getInstance().getData( cellDescription.getPreviousSolution() )[i + variableNumber * numberOfDoFsPerVariable] += sign * DataHeap::getInstance().getData( cellDescription.getPreviousSolutionAverages() )[variableNumber];
      DataHeap::getInstance().getData( cellDescription.getUpdate()           )[i + variableNumber * numberOfDoFsPerVariable] += sign * DataHeap::getInstance().getData( cellDescription.getUpdateAverages()   )[variableNumber];
    }

    for (int face=0; face<2*DIMENSIONS; face++) {
      int    numberOfDoFsPerVariable  = power(getNodesPerCoordinateAxis(), DIMENSIONS-1);
      for (int i=0; i<numberOfDoFsPerVariable; i++) {
        DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictor() )[i + variableNumber * numberOfDoFsPerVariable + getNumberOfVariables() * numberOfDoFsPerVariable * face ] += sign * DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictorAverages() )[variableNumber+getNumberOfVariables() * face];
        DataHeap::getInstance().getData( cellDescription.getFluctuation()           )[i + variableNumber * numberOfDoFsPerVariable + getNumberOfVariables() * numberOfDoFsPerVariable * face ] += sign * DataHeap::getInstance().getData( cellDescription.getFluctuationAverages()           )[variableNumber+getNumberOfVariables() * face];
      }
    }
  }
}


void exahype::solvers::ADERDGSolver::tearApart(int numberOfEntries, int normalHeapIndex, int compressedHeapIndex, int bytesForMantissa) {
  char exponent;
  long int mantissa;
  char* pMantissa = reinterpret_cast<char*>( &(mantissa) );

  assertion( DataHeap::getInstance().isValidIndex(normalHeapIndex) );
  assertion( CompressedDataHeap::getInstance().isValidIndex(compressedHeapIndex) );
  assertion2( static_cast<int>(DataHeap::getInstance().getData(normalHeapIndex).size())==numberOfEntries, DataHeap::getInstance().getData(normalHeapIndex).size(), numberOfEntries );
  assertion( CompressedDataHeap::getInstance().getData(compressedHeapIndex).empty() );

  CompressedDataHeap::getInstance().getData( compressedHeapIndex ).resize(numberOfEntries * (bytesForMantissa+1));

  int compressedDataHeapIndex = 0;
  for (int i=0; i<numberOfEntries; i++) {
    peano::heap::decompose(
      DataHeap::getInstance().getData( normalHeapIndex )[i],
      exponent, mantissa, bytesForMantissa
    );
    CompressedDataHeap::getInstance().getData( compressedHeapIndex )[compressedDataHeapIndex]._persistentRecords._u = exponent;
    compressedDataHeapIndex++;
    for (int j=0; j<bytesForMantissa; j++) {
      CompressedDataHeap::getInstance().getData( compressedHeapIndex )[compressedDataHeapIndex]._persistentRecords._u = pMantissa[j];
      compressedDataHeapIndex++;
    }
  }
}


void exahype::solvers::ADERDGSolver::glueTogether(int numberOfEntries, int normalHeapIndex, int compressedHeapIndex, int bytesForMantissa) {
  char exponent  = 0;
  long int mantissa;
  char* pMantissa = reinterpret_cast<char*>( &(mantissa) );

  assertion( DataHeap::getInstance().isValidIndex(normalHeapIndex) );
  assertion( CompressedDataHeap::getInstance().isValidIndex(compressedHeapIndex) );
  assertion5(
    static_cast<int>(CompressedDataHeap::getInstance().getData(compressedHeapIndex).size())==numberOfEntries * (bytesForMantissa+1),
    CompressedDataHeap::getInstance().getData(compressedHeapIndex).size(), numberOfEntries * (bytesForMantissa+1),
    numberOfEntries, compressedHeapIndex, bytesForMantissa
  );

  #ifdef ValidateCompressedVsUncompressedData
  assertion( static_cast<int>(DataHeap::getInstance().getData(normalHeapIndex).size())==numberOfEntries );
  #else
  DataHeap::getInstance().getData(normalHeapIndex).resize(numberOfEntries);
  #endif

  int compressedDataHeapIndex = numberOfEntries * (bytesForMantissa+1)-1;
  for (int i=numberOfEntries-1; i>=0; i--) {
    mantissa = 0;
    for (int j=bytesForMantissa-1; j>=0; j--) {
      pMantissa[j] = CompressedDataHeap::getInstance().getData( compressedHeapIndex )[compressedDataHeapIndex]._persistentRecords._u; // TODO(Dominic):This line fails
      compressedDataHeapIndex--;
    }
    exponent = CompressedDataHeap::getInstance().getData( compressedHeapIndex )[compressedDataHeapIndex]._persistentRecords._u;
    compressedDataHeapIndex--;
    double reconstructedValue = peano::heap::compose(
      exponent, mantissa, bytesForMantissa
    );
    #ifdef ValidateCompressedVsUncompressedData
    assertion7(
      tarch::la::equals( DataHeap::getInstance().getData(normalHeapIndex)[i], reconstructedValue, CompressionAccuracy ),
      DataHeap::getInstance().getData(normalHeapIndex)[i], reconstructedValue, DataHeap::getInstance().getData(normalHeapIndex)[i] - reconstructedValue,
      CompressionAccuracy, bytesForMantissa, numberOfEntries, normalHeapIndex
    );
    #else
    DataHeap::getInstance().getData(normalHeapIndex)[i] = reconstructedValue;
    #endif
  }
}



void exahype::solvers::ADERDGSolver::putUnknownsIntoByteStream(exahype::records::ADERDGCellDescription& cellDescription) {
  assertion(CompressionAccuracy>0.0);

  assertion( cellDescription.getPreviousSolutionCompressed()==-1 );
  assertion( cellDescription.getSolutionCompressed()==-1 );
  assertion( cellDescription.getUpdateCompressed()==-1 );
  assertion( cellDescription.getExtrapolatedPredictorCompressed()==-1 );
  assertion( cellDescription.getFluctuationCompressed()==-1 );

  int compressionOfPreviousSolution;
  int compressionOfSolution;
  int compressionOfUpdate;
  int compressionOfExtrapolatedPredictor;
  int compressionOfFluctuation;

  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getSolution() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getPreviousSolution() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getUpdate() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getExtrapolatedPredictor() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getFluctuation() ));

  peano::datatraversal::TaskSet compressionFactorIdentification(
    [&]() -> void  { compressionOfPreviousSolution = peano::heap::findMostAgressiveCompression(
      DataHeap::getInstance().getData( cellDescription.getPreviousSolution() ).data(),
      getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS),
      CompressionAccuracy
      );},
    [&] () -> void  { compressionOfSolution = peano::heap::findMostAgressiveCompression(
      DataHeap::getInstance().getData( cellDescription.getSolution() ).data(),
      getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS),
      CompressionAccuracy
      );},
    [&]() -> void  { compressionOfUpdate = peano::heap::findMostAgressiveCompression(
      DataHeap::getInstance().getData( cellDescription.getUpdate() ).data(),
      getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS),
      CompressionAccuracy
      );},
    [&]() -> void  { compressionOfExtrapolatedPredictor = peano::heap::findMostAgressiveCompression(
      DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictor() ).data(),
      getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS-1) * 2 * DIMENSIONS,
      CompressionAccuracy
      );},
    [&]() -> void  { compressionOfFluctuation = peano::heap::findMostAgressiveCompression(
      DataHeap::getInstance().getData( cellDescription.getFluctuation() ).data(),
      getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS-1) * 2 * DIMENSIONS,
      CompressionAccuracy
      );},
      true
  );

  assertion(1<=compressionOfPreviousSolution);
  assertion(1<=compressionOfSolution);
  assertion(1<=compressionOfUpdate);
  assertion(1<=compressionOfExtrapolatedPredictor);
  assertion(1<=compressionOfFluctuation);

  assertion(compressionOfPreviousSolution<=7);
  assertion(compressionOfSolution<=7);
  assertion(compressionOfUpdate<=7);
  assertion(compressionOfExtrapolatedPredictor<=7);
  assertion(compressionOfFluctuation<=7);

  peano::datatraversal::TaskSet runParallelTasks(
    [&]() -> void {
      cellDescription.setBytesPerDoFInPreviousSolution(compressionOfPreviousSolution);
      if (compressionOfPreviousSolution<7) {
        tarch::multicore::Lock lock(_heapSemaphore);
        cellDescription.setPreviousSolutionCompressed( CompressedDataHeap::getInstance().createData(0,0,peano::heap::Allocation::UseOnlyRecycledEntries) );
        assertion( cellDescription.getPreviousSolutionCompressed()>=0 );
        lock.free();

        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS);
        tearApart(numberOfEntries, cellDescription.getPreviousSolution(), cellDescription.getPreviousSolutionCompressed(), compressionOfPreviousSolution);

        #if defined(Asserts)
        lock.lock();
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getPreviousSolution() ).size() * 8.0;
        PipedCompressedBytes   += CompressedDataHeap::getInstance().getData( cellDescription.getPreviousSolutionCompressed() ).size();
        lock.free();
        #endif

        #if !defined(ValidateCompressedVsUncompressedData)
        lock.lock();
        DataHeap::getInstance().deleteData( cellDescription.getPreviousSolution(), true );
        cellDescription.setPreviousSolution( -1 );
        #endif
      }
      else {
        #if defined(Asserts)
        tarch::multicore::Lock lock(_heapSemaphore);
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getPreviousSolution() ).size() * 8.0;
        PipedCompressedBytes   += DataHeap::getInstance().getData( cellDescription.getPreviousSolution() ).size() * 8.0;
        #endif
      }
    },
    [&]() -> void {
      cellDescription.setBytesPerDoFInSolution(compressionOfSolution);
      if (compressionOfSolution<7) {
        tarch::multicore::Lock lock(_heapSemaphore);
        cellDescription.setSolutionCompressed(CompressedDataHeap::getInstance().createData(0,0,peano::heap::Allocation::UseOnlyRecycledEntries));
        assertion1( cellDescription.getSolutionCompressed()>=0, cellDescription.getSolutionCompressed() );
        lock.free();

        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS);

        tearApart(numberOfEntries, cellDescription.getSolution(), cellDescription.getSolutionCompressed(), compressionOfSolution);

        #if defined(Asserts)
        lock.lock();
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getSolution() ).size() * 8.0;
        PipedCompressedBytes   += CompressedDataHeap::getInstance().getData( cellDescription.getSolutionCompressed() ).size();
        lock.free();
        #endif

        #if !defined(ValidateCompressedVsUncompressedData)
        lock.lock();
        DataHeap::getInstance().deleteData( cellDescription.getSolution(), true );
        cellDescription.setSolution( -1 );
        #endif
      }
      else {
        #if defined(Asserts)
        tarch::multicore::Lock lock(_heapSemaphore);
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getSolution() ).size() * 8.0;
        PipedCompressedBytes   += DataHeap::getInstance().getData( cellDescription.getSolution() ).size() * 8.0;
        #endif
      }
    },
    [&]() -> void {
      cellDescription.setBytesPerDoFInUpdate(compressionOfUpdate);
      if (compressionOfUpdate<7) {
        tarch::multicore::Lock lock(_heapSemaphore);
        cellDescription.setUpdateCompressed( CompressedDataHeap::getInstance().createData(0,0,peano::heap::Allocation::UseOnlyRecycledEntries) );
        assertion( cellDescription.getUpdateCompressed()>=0 );
        lock.free();

        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS);
        tearApart(numberOfEntries, cellDescription.getUpdate(), cellDescription.getUpdateCompressed(), compressionOfUpdate);

        #if defined(Asserts)
        lock.lock();
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getUpdate() ).size() * 8.0;
        PipedCompressedBytes   += CompressedDataHeap::getInstance().getData( cellDescription.getUpdateCompressed() ).size();
        lock.free();
        #endif

        #if !defined(ValidateCompressedVsUncompressedData)
        lock.lock();
        DataHeap::getInstance().deleteData( cellDescription.getUpdate(), true );
        cellDescription.setUpdate( -1 );
        #endif
      }
      else {
        #if defined(Asserts)
        tarch::multicore::Lock lock(_heapSemaphore);
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getUpdate() ).size() * 8.0;
        PipedCompressedBytes   += DataHeap::getInstance().getData( cellDescription.getUpdate() ).size() * 8.0;
        #endif
      }
    },
    [&]() -> void {
      cellDescription.setBytesPerDoFInExtrapolatedPredictor(compressionOfExtrapolatedPredictor);
      if (compressionOfExtrapolatedPredictor<7) {
        tarch::multicore::Lock lock(_heapSemaphore);
        cellDescription.setExtrapolatedPredictorCompressed( CompressedDataHeap::getInstance().createData(0,0,peano::heap::Allocation::UseOnlyRecycledEntries) );
        assertion( cellDescription.getExtrapolatedPredictorCompressed()>=0 );
        lock.free();

        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS-1) * 2 * DIMENSIONS;
        tearApart(numberOfEntries, cellDescription.getExtrapolatedPredictor(), cellDescription.getExtrapolatedPredictorCompressed(), compressionOfExtrapolatedPredictor);

        #if defined(Asserts)
        lock.lock();
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictor() ).size() * 8.0;
        PipedCompressedBytes   += CompressedDataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictorCompressed() ).size();
        lock.free();
        #endif

        #if !defined(ValidateCompressedVsUncompressedData)
        lock.lock();
        DataHeap::getInstance().deleteData( cellDescription.getExtrapolatedPredictor(), true );
        cellDescription.setExtrapolatedPredictor( -1 );
        #endif
      }
      else {
        #if defined(Asserts)
        tarch::multicore::Lock lock(_heapSemaphore);
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictor() ).size() * 8.0;
        PipedCompressedBytes   += DataHeap::getInstance().getData( cellDescription.getExtrapolatedPredictor() ).size() * 8.0;
        #endif
      }
    },
    [&]() -> void {
      cellDescription.setBytesPerDoFInFluctuation(compressionOfFluctuation);
      if (compressionOfFluctuation<7) {
        tarch::multicore::Lock lock(_heapSemaphore);
        cellDescription.setFluctuationCompressed( CompressedDataHeap::getInstance().createData(0,0,peano::heap::Allocation::UseOnlyRecycledEntries) );
        assertion( cellDescription.getFluctuationCompressed()>=0 );
        lock.free();

        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS-1) * 2 * DIMENSIONS;
        tearApart(numberOfEntries, cellDescription.getFluctuation(), cellDescription.getFluctuationCompressed(), compressionOfFluctuation);

        #if defined(Asserts)
        lock.lock();
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getFluctuation() ).size() * 8.0;
        PipedCompressedBytes   += CompressedDataHeap::getInstance().getData( cellDescription.getFluctuationCompressed() ).size();
        lock.free();
        #endif

        #if !defined(ValidateCompressedVsUncompressedData)
        lock.lock();
        DataHeap::getInstance().deleteData( cellDescription.getFluctuation(), true );
        cellDescription.setFluctuation( -1 );
        #endif
      }
      else {
        #if defined(Asserts)
        tarch::multicore::Lock lock(_heapSemaphore);
        PipedUncompressedBytes += DataHeap::getInstance().getData( cellDescription.getFluctuation() ).size() * 8.0;
        PipedCompressedBytes   += DataHeap::getInstance().getData( cellDescription.getFluctuation() ).size() * 8.0;
        #endif
      }
    },
    true
  );
}


void exahype::solvers::ADERDGSolver::pullUnknownsFromByteStream(exahype::records::ADERDGCellDescription& cellDescription) {
  assertion(CompressionAccuracy>0.0);

  #if !defined(ValidateCompressedVsUncompressedData)
  const int unknownsPerCell         = getUnknownsPerCell();
  const int dataPointsPerCell       = getDataPerCell();
  const int unknownsPerCellBoundary = getUnknownsPerCellBoundary();

  {
    tarch::multicore::Lock lock(_heapSemaphore);
    cellDescription.setPreviousSolution( DataHeap::getInstance().createData( dataPointsPerCell,         dataPointsPerCell,         DataHeap::Allocation::UseOnlyRecycledEntries) );
    cellDescription.setSolution( DataHeap::getInstance().createData(         dataPointsPerCell,         dataPointsPerCell,         DataHeap::Allocation::UseOnlyRecycledEntries) );
    cellDescription.setUpdate( DataHeap::getInstance().createData(           unknownsPerCell,         unknownsPerCell,         DataHeap::Allocation::UseOnlyRecycledEntries) );

    cellDescription.setExtrapolatedPredictor( DataHeap::getInstance().createData( unknownsPerCellBoundary, unknownsPerCellBoundary, DataHeap::Allocation::UseOnlyRecycledEntries) );
    cellDescription.setFluctuation( DataHeap::getInstance().createData(           unknownsPerCellBoundary, unknownsPerCellBoundary, DataHeap::Allocation::UseOnlyRecycledEntries) );
    lock.free();

    if (cellDescription.getPreviousSolution()==-1) {
      waitUntilAllBackgroundTasksHaveTerminated();
      lock.lock();
      cellDescription.setPreviousSolution( DataHeap::getInstance().createData( dataPointsPerCell, dataPointsPerCell, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired) );
      lock.free();
    }
    if (cellDescription.getSolution()==-1) {
      waitUntilAllBackgroundTasksHaveTerminated();
      lock.lock();
      cellDescription.setSolution( DataHeap::getInstance().createData( dataPointsPerCell, dataPointsPerCell, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired) );
      lock.free();
    }
    if (cellDescription.getUpdate()==-1) {
      waitUntilAllBackgroundTasksHaveTerminated();
      lock.lock();
      cellDescription.setUpdate( DataHeap::getInstance().createData( unknownsPerCell, unknownsPerCell, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired) );
      lock.free();
    }
    if (cellDescription.getExtrapolatedPredictor()==-1) {
      waitUntilAllBackgroundTasksHaveTerminated();
      lock.lock();
      cellDescription.setExtrapolatedPredictor( DataHeap::getInstance().createData(unknownsPerCellBoundary, unknownsPerCellBoundary, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired) );
      lock.free();
    }
    if (cellDescription.getFluctuation()==-1) {
      waitUntilAllBackgroundTasksHaveTerminated();
      lock.lock();
      cellDescription.setFluctuation( DataHeap::getInstance().createData( unknownsPerCellBoundary, unknownsPerCellBoundary, DataHeap::Allocation::UseRecycledEntriesIfPossibleCreateNewEntriesIfRequired) );
      lock.free();
    }
  }
  #else
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getPreviousSolution() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getSolution() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getUpdate() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getExtrapolatedPredictor() ));
  assertion( DataHeap::getInstance().isValidIndex( cellDescription.getFluctuation() ));
  #endif

  assertion1(
      CompressedDataHeap::getInstance().isValidIndex( cellDescription.getPreviousSolutionCompressed() ),
      cellDescription.getPreviousSolutionCompressed()
    );
  assertion1(
    CompressedDataHeap::getInstance().isValidIndex( cellDescription.getSolutionCompressed() ),
    cellDescription.getSolutionCompressed()
  );
  assertion1(
    CompressedDataHeap::getInstance().isValidIndex( cellDescription.getUpdateCompressed() ),
    cellDescription.getUpdateCompressed()
  );
  assertion1(
    CompressedDataHeap::getInstance().isValidIndex( cellDescription.getExtrapolatedPredictorCompressed() ),
    cellDescription.getExtrapolatedPredictorCompressed()
  );
  assertion1(
    CompressedDataHeap::getInstance().isValidIndex( cellDescription.getFluctuationCompressed() ),
    cellDescription.getFluctuationCompressed()
  );

  peano::datatraversal::TaskSet glueTasks(
    [&]() -> void {
      if (cellDescription.getBytesPerDoFInPreviousSolution()<7) {
        assertion1( DataHeap::getInstance().isValidIndex( cellDescription.getPreviousSolution() ), cellDescription.getPreviousSolution());
        assertion( CompressedDataHeap::getInstance().isValidIndex( cellDescription.getPreviousSolutionCompressed() ));
        const int numberOfEntries = ( getNumberOfVariables()+getNumberOfParameters() ) * power(getNodesPerCoordinateAxis(), DIMENSIONS);
        glueTogether(numberOfEntries, cellDescription.getPreviousSolution(), cellDescription.getPreviousSolutionCompressed(), cellDescription.getBytesPerDoFInPreviousSolution());
        tarch::multicore::Lock lock(_heapSemaphore);
        CompressedDataHeap::getInstance().deleteData( cellDescription.getPreviousSolutionCompressed(), true );
        cellDescription.setPreviousSolutionCompressed( -1 );
      }
    },
    [&]() -> void {
      if (cellDescription.getBytesPerDoFInSolution()<7) {
        assertion1( DataHeap::getInstance().isValidIndex( cellDescription.getSolution() ), cellDescription.getSolution() );
        assertion( CompressedDataHeap::getInstance().isValidIndex( cellDescription.getSolutionCompressed() ));
        const int numberOfEntries = ( getNumberOfVariables()+getNumberOfParameters() ) * power(getNodesPerCoordinateAxis(), DIMENSIONS);
        glueTogether(numberOfEntries, cellDescription.getSolution(), cellDescription.getSolutionCompressed(), cellDescription.getBytesPerDoFInSolution());
        tarch::multicore::Lock lock(_heapSemaphore);
        CompressedDataHeap::getInstance().deleteData( cellDescription.getSolutionCompressed(), true );
        cellDescription.setSolutionCompressed( -1 );
      }
    },
    [&]() -> void {
      if (cellDescription.getBytesPerDoFInUpdate()<7) {
        assertion1( DataHeap::getInstance().isValidIndex( cellDescription.getUpdate() ), cellDescription.getUpdate());
        assertion( CompressedDataHeap::getInstance().isValidIndex( cellDescription.getUpdateCompressed() ));
        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS);
        glueTogether(numberOfEntries, cellDescription.getUpdate(), cellDescription.getUpdateCompressed(), cellDescription.getBytesPerDoFInUpdate());
        tarch::multicore::Lock lock(_heapSemaphore);
        CompressedDataHeap::getInstance().deleteData( cellDescription.getUpdateCompressed(), true );
        cellDescription.setUpdateCompressed( -1 );
      }
    },
    [&]() -> void {
      if (cellDescription.getBytesPerDoFInExtrapolatedPredictor()<7) {
        assertion1( DataHeap::getInstance().isValidIndex( cellDescription.getExtrapolatedPredictor() ), cellDescription.getExtrapolatedPredictor());
        assertion( CompressedDataHeap::getInstance().isValidIndex( cellDescription.getExtrapolatedPredictorCompressed() ));
        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS-1) * 2 * DIMENSIONS;
        glueTogether(numberOfEntries, cellDescription.getExtrapolatedPredictor(), cellDescription.getExtrapolatedPredictorCompressed(), cellDescription.getBytesPerDoFInExtrapolatedPredictor());
        tarch::multicore::Lock lock(_heapSemaphore);
        CompressedDataHeap::getInstance().deleteData( cellDescription.getExtrapolatedPredictorCompressed(), true );
        cellDescription.setExtrapolatedPredictorCompressed( -1 );
      }
    },
    [&]() -> void {
      if (cellDescription.getBytesPerDoFInFluctuation()<7) {
        assertion1( DataHeap::getInstance().isValidIndex( cellDescription.getFluctuation() ), cellDescription.getFluctuation());
        assertion( CompressedDataHeap::getInstance().isValidIndex( cellDescription.getFluctuationCompressed() ));
        const int numberOfEntries = getNumberOfVariables() * power(getNodesPerCoordinateAxis(), DIMENSIONS-1) * 2 * DIMENSIONS;
        glueTogether(numberOfEntries, cellDescription.getFluctuation(), cellDescription.getFluctuationCompressed(), cellDescription.getBytesPerDoFInFluctuation());
        tarch::multicore::Lock lock(_heapSemaphore);
        CompressedDataHeap::getInstance().deleteData( cellDescription.getFluctuationCompressed(), true );
        cellDescription.setFluctuationCompressed( -1 );
      }
    },
    true
  );
}

void exahype::solvers::ADERDGSolver::pointSource(
    const double t,
    const double dt, 
    const tarch::la::Vector<DIMENSIONS,double>& center,
    const tarch::la::Vector<DIMENSIONS,double>& dx, 
    double* tempPointForceSources) {}
