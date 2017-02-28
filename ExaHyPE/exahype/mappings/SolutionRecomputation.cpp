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
 
#include "exahype/mappings/SolutionRecomputation.h"

#include "tarch/multicore/Loop.h"

#include "peano/utils/Globals.h"
#include "peano/utils/Loop.h"
#include "peano/datatraversal/autotuning/Oracle.h"

#include "exahype/solvers/LimitingADERDGSolver.h"

#include "exahype/VertexOperations.h"
#include "multiscalelinkedcell/HangingVertexBookkeeper.h"

peano::CommunicationSpecification
exahype::mappings::SolutionRecomputation::communicationSpecification() {
  return peano::CommunicationSpecification(
      peano::CommunicationSpecification::ExchangeMasterWorkerData::MaskOutMasterWorkerDataAndStateExchange,
      peano::CommunicationSpecification::ExchangeWorkerMasterData::MaskOutWorkerMasterDataAndStateExchange,
      true);
}

peano::MappingSpecification
exahype::mappings::SolutionRecomputation::touchVertexFirstTimeSpecification() {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::AvoidFineGridRaces,true);
}

peano::MappingSpecification
exahype::mappings::SolutionRecomputation::enterCellSpecification() {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::RunConcurrentlyOnFineGrid,true);
}

// Below specs are all nop

peano::MappingSpecification
exahype::mappings::SolutionRecomputation::touchVertexLastTimeSpecification() {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::RunConcurrentlyOnFineGrid,true);
}

peano::MappingSpecification
exahype::mappings::SolutionRecomputation::leaveCellSpecification() {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidFineGridRaces,true);
}

/**
 * @todo Please tailor the parameters to your mapping's properties.
 */
peano::MappingSpecification
exahype::mappings::SolutionRecomputation::ascendSpecification() {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidCoarseGridRaces,true);
}


peano::MappingSpecification
exahype::mappings::SolutionRecomputation::descendSpecification() {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidCoarseGridRaces,true);
}

tarch::logging::Log exahype::mappings::SolutionRecomputation::_log(
    "exahype::mappings::SolutionRecomputation");

void exahype::mappings::SolutionRecomputation::initialiseTemporaryVariables() {
  exahype::solvers::initialiseTemporaryVariables(_predictionTemporaryVariables);
  exahype::solvers::initialiseTemporaryVariables(_mergingTemporaryVariables);
  exahype::solvers::initialiseTemporaryVariables(_solutionUpdateTemporaryVariables);
}

void exahype::mappings::SolutionRecomputation::deleteTemporaryVariables() {
  exahype::solvers::deleteTemporaryVariables(_predictionTemporaryVariables);
  exahype::solvers::deleteTemporaryVariables(_mergingTemporaryVariables);
  exahype::solvers::deleteTemporaryVariables(_solutionUpdateTemporaryVariables);
}

exahype::mappings::SolutionRecomputation::SolutionRecomputation()
  #ifdef Debug
  :
  _interiorFaceMerges(0),
  _boundaryFaceMerges(0)
  #endif
{
  // do nothing
}

exahype::mappings::SolutionRecomputation::~SolutionRecomputation() {
  deleteTemporaryVariables();
}

#if defined(SharedMemoryParallelisation)
exahype::mappings::SolutionRecomputation::SolutionRecomputation(
    const SolutionRecomputation& masterThread)
: _localState(masterThread._localState) {
  initialiseTemporaryVariables();
}

void exahype::mappings::SolutionRecomputation::mergeWithWorkerThread(
    const SolutionRecomputation& workerThread) {
  // do nothing
}
#endif

void exahype::mappings::SolutionRecomputation::beginIteration(
    exahype::State& solverState) {
  logTraceInWith1Argument("beginIteration(State)", solverState);

  _localState = solverState;

  initialiseTemporaryVariables();

  #ifdef Debug // TODO(Dominic): And not parallel and not shared memory
  _interiorFaceMerges = 0;
  _boundaryFaceMerges = 0;
  #endif

  logTraceOutWith1Argument("beginIteration(State)", solverState);
}

void exahype::mappings::SolutionRecomputation::endIteration(
    exahype::State& solverState) {
  logTraceInWith1Argument("endIteration(State)", solverState);

  deleteTemporaryVariables();

  #if defined(Debug) // TODO(Dominic): Use logDebug if it works with filters
  logInfo("endIteration(...)","interiorFaceSolves: " << _interiorFaceMerges);
  logInfo("endIteration(...)","boundaryFaceSolves: " << _boundaryFaceMerges);
  #endif

  logTraceOutWith1Argument("endIteration(State)", solverState);
}

void exahype::mappings::SolutionRecomputation::enterCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  logTraceInWith4Arguments("enterCell(...)", fineGridCell,
                           fineGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfCell);

  if (fineGridCell.isInitialised()) {
    const int numberOfSolvers = exahype::solvers::RegisteredSolvers.size();
    auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().parallelise(numberOfSolvers, peano::datatraversal::autotuning::MethodTrace::UserDefined14);
    pfor(i, 0, numberOfSolvers, grainSize.getGrainSize())
      auto solver = exahype::solvers::RegisteredSolvers[i];

      const int element = solver->tryGetElement(fineGridCell.getCellDescriptionsIndex(),i);
      if (element!=exahype::solvers::Solver::NotFound) {
        if (solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG
            && static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getLimiterDomainHasChanged()) {
          auto* limitingADERSolver = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver);

          limitingADERSolver->updateMergedLimiterStatus(fineGridCell.getCellDescriptionsIndex(),element); // update face-wise
                                                                                              // limiter status before recomputation

          limitingADERSolver->recomputeSolution(
              fineGridCell.getCellDescriptionsIndex(),
              element,
              _solutionUpdateTemporaryVariables,
              fineGridVertices,
              fineGridVerticesEnumerator);

          if (exahype::State::fuseADERDGPhases()) {
            limitingADERSolver->recomputePredictor(
                fineGridCell.getCellDescriptionsIndex(),
                element,
                _predictionTemporaryVariables,
                fineGridVertices,
                fineGridVerticesEnumerator);
          }

          // It is important that we update the cell-wise limiter status only after the recomputation since we use
          // the previous and current limiter status in the recomputation.
          limitingADERSolver->updateLimiterStatus(fineGridCell.getCellDescriptionsIndex(),element);

          limitingADERSolver->determineMinAndMax(fineGridCell.getCellDescriptionsIndex(),element);
        }

        solver->prepareNextNeighbourMerging(
            fineGridCell.getCellDescriptionsIndex(),element,
            fineGridVertices,fineGridVerticesEnumerator); // !!! Has to be done for all solvers (cf. touchVertexFirstTime etc.)
      }
    endpfor
    grainSize.parallelSectionHasTerminated();
  }
  logTraceOutWith1Argument("enterCell(...)", fineGridCell);
}

void exahype::mappings::SolutionRecomputation::touchVertexFirstTime(
  exahype::Vertex& fineGridVertex,
  const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
  const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
  exahype::Vertex* const coarseGridVertices,
  const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
  exahype::Cell& coarseGridCell,
  const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
    dfor2(pos1)
      dfor2(pos2)
        if (fineGridVertex.hasToMergeNeighbours(pos1,pos2)) { // Assumes that we have to valid indices
          auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
              parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined15);
          pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
            auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

            if (solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG
                && static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getLimiterDomainHasChanged()) {
              const int cellDescriptionsIndex1 = fineGridVertex.getCellDescriptionsIndex()[pos1Scalar];
              const int cellDescriptionsIndex2 = fineGridVertex.getCellDescriptionsIndex()[pos2Scalar];
              const int element1 = solver->tryGetElement(cellDescriptionsIndex1,solverNumber);
              const int element2 = solver->tryGetElement(cellDescriptionsIndex2,solverNumber);
              if (element2>=0 && element1>=0) {
                static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->
                    mergeNeighboursBasedOnLimiterStatus(
                    cellDescriptionsIndex1,element1,
                    cellDescriptionsIndex2,element2,
                    pos1,pos2,
                    true, /* isRecomputation */
                    _mergingTemporaryVariables._tempFaceUnknowns[solverNumber],
                    _mergingTemporaryVariables._tempStateSizedVectors[solverNumber],
                    _mergingTemporaryVariables._tempStateSizedSquareMatrices[solverNumber]);
              }
            }

            #ifdef Debug // TODO(Dominic)
            _interiorFaceMerges++;
            #endif
          endpfor
          grainSize.parallelSectionHasTerminated();

          fineGridVertex.setMergePerformed(pos1,pos2,true);
        }
        if (fineGridVertex.hasToMergeWithBoundaryData(pos1,pos2)) {
          auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
              parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined16);
          pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
            auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
            const int cellDescriptionsIndex1 = fineGridVertex.getCellDescriptionsIndex()[pos1Scalar];
            const int cellDescriptionsIndex2 = fineGridVertex.getCellDescriptionsIndex()[pos2Scalar];
            int element1 = solver->tryGetElement(cellDescriptionsIndex1,solverNumber);
            int element2 = solver->tryGetElement(cellDescriptionsIndex2,solverNumber);
            assertion4((element1==exahype::solvers::Solver::NotFound &&
                        element2==exahype::solvers::Solver::NotFound)
                       || (element1 >= 0 && element2==exahype::solvers::Solver::NotFound)
                       || (element2 >= 0 && element1==exahype::solvers::Solver::NotFound),
                       cellDescriptionsIndex1,cellDescriptionsIndex2,element1,element2); // TODO(Dominic): Move down

            if (solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG
                && static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getLimiterDomainHasChanged()) {
              if (element1 >= 0) {
                auto& solverPatch1 = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->
                    getSolver()->getCellDescription(cellDescriptionsIndex1,element1);

                static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->
                    mergeWithBoundaryDataBasedOnLimiterStatus( // !!! Be aware of indices "2" and "1" and the order of the arguments.
                                              cellDescriptionsIndex1,element1,
                                              solverPatch1.getMergedLimiterStatus(0), // !!! We assume here that we have already unified the merged limiter status values.
                                              pos1,pos2,                              // The cell-based limiter status is still holding the old value though.
                                              true,
                                              _mergingTemporaryVariables._tempFaceUnknowns[solverNumber],
                                              _mergingTemporaryVariables._tempStateSizedVectors[solverNumber],
                                              _mergingTemporaryVariables._tempStateSizedSquareMatrices[solverNumber]);

                #ifdef Debug
                _boundaryFaceMerges++;
                #endif
              }
              if (element2 >= 0){
                auto& solverPatch2 = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->
                    getSolver()->getCellDescription(cellDescriptionsIndex2,element2);

                static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->
                    mergeWithBoundaryDataBasedOnLimiterStatus( // !!! Be aware of indices "2" and "1" and the order of the arguments.
                                              cellDescriptionsIndex2,element2,
                                              solverPatch2.getMergedLimiterStatus(0), // !!! We assume here that we have already unified the merged limiter status values
                                              pos2,pos1,                              // The cell-based limiter status is still holding the old value though.
                                              true,
                                              _mergingTemporaryVariables._tempFaceUnknowns[solverNumber],
                                              _mergingTemporaryVariables._tempStateSizedVectors[solverNumber],
                                              _mergingTemporaryVariables._tempStateSizedSquareMatrices[solverNumber]);
                #ifdef Debug
                _boundaryFaceMerges++;
                #endif
              }
            }
          endpfor
          grainSize.parallelSectionHasTerminated();

          fineGridVertex.setMergePerformed(pos1,pos2,true);
        }
      enddforx
    enddforx
}


#ifdef Parallel
///////////////////////////////////////
// NEIGHBOUR
///////////////////////////////////////
void exahype::mappings::SolutionRecomputation::mergeWithNeighbour(
    exahype::Vertex& vertex, const exahype::Vertex& neighbour, int fromRank,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH, int level) {
  dfor2(myDest)
    dfor2(mySrc)
      tarch::la::Vector<DIMENSIONS, int> dest = tarch::la::Vector<DIMENSIONS, int>(1) - myDest;
      tarch::la::Vector<DIMENSIONS, int> src  = tarch::la::Vector<DIMENSIONS, int>(1) - mySrc;

      int destScalar = TWO_POWER_D - myDestScalar - 1;
      int srcScalar  = TWO_POWER_D - mySrcScalar  - 1;

      if (vertex.hasToReceiveMetadata(src,dest,fromRank)) {
        int receivedMetadataIndex = MetadataHeap::getInstance().
            createData(0,exahype::solvers::RegisteredSolvers.size());
        MetadataHeap::getInstance().receiveData(
            receivedMetadataIndex,
            fromRank, fineGridX, level,
            peano::heap::MessageType::NeighbourCommunication);
        exahype::MetadataHeap::HeapEntries& receivedMetadata = MetadataHeap::getInstance().getData(receivedMetadataIndex);
        assertion(receivedMetadata.size()==solvers::RegisteredSolvers.size());

        if(vertex.hasToMergeWithNeighbourData(src,dest)) { // Only comm. data once per face
          mergeNeighourData(
              fromRank,
              src,dest,
              vertex.getCellDescriptionsIndex()[srcScalar],
              vertex.getCellDescriptionsIndex()[destScalar],
              fineGridX,level,
              receivedMetadata);

          vertex.setFaceDataExchangeCountersOfDestination(src,dest,TWO_POWER_D); // !!! Do not forget this
          vertex.setMergePerformed(src,dest,true);
        } else {
          dropNeighbourData(
              fromRank,
              src,dest,
              vertex.getCellDescriptionsIndex()[srcScalar],
              vertex.getCellDescriptionsIndex()[destScalar],
              fineGridX,level,
              receivedMetadata);
        }
        // Clean up
        MetadataHeap::getInstance().deleteData(receivedMetadataIndex);
      }
    enddforx
  enddforx
}

void exahype::mappings::SolutionRecomputation::dropNeighbourData(
    const int                                    fromRank,
    const tarch::la::Vector<DIMENSIONS, int>&    src,
    const tarch::la::Vector<DIMENSIONS, int>&    dest,
    const int                                    srcCellDescriptionIndex,
    const int                                    destCellDescriptionIndex,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level,
    const exahype::MetadataHeap::HeapEntries&    receivedMetadata) {
  for(unsigned int solverNumber = solvers::RegisteredSolvers.size(); solverNumber-- > 0;) {
    auto* solver = solvers::RegisteredSolvers[solverNumber];

    if (solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG
        && static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getLimiterDomainHasChanged()) {
      auto* limitingADERDGSolver = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver);

      logDebug("dropNeighbourData(...)", "drop data for solver " << solverNumber << " from rank " <<
              fromRank << " at vertex x=" << x << ", level=" << level <<
              ", src=" << src << ", dest=" << dest);

      limitingADERDGSolver->dropNeighbourSolverAndLimiterData(fromRank,src,dest,x,level);
    }
  }
}

void exahype::mappings::SolutionRecomputation::mergeNeighourData(
    const int                                    fromRank,
    const tarch::la::Vector<DIMENSIONS,int>&     src,
    const tarch::la::Vector<DIMENSIONS,int>&     dest,
    const int                                    srcCellDescriptionIndex,
    const int                                    destCellDescriptionIndex,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level,
    const exahype::MetadataHeap::HeapEntries&    receivedMetadata) {
  assertion(exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(destCellDescriptionIndex));
  assertion(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(destCellDescriptionIndex));

  for(unsigned int solverNumber = solvers::RegisteredSolvers.size(); solverNumber-- > 0;) {
    auto* solver = solvers::RegisteredSolvers[solverNumber];

    if (solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG
        && static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getLimiterDomainHasChanged()) {
      int element = solver->tryGetElement(destCellDescriptionIndex,solverNumber);

      if (element!=exahype::solvers::Solver::NotFound
          && receivedMetadata[solverNumber].getU()!=exahype::Vertex::InvalidMetadataEntry) {
        auto* limitingADERDGSolver = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver);

        logDebug("mergeWithNeighbourData(...)", "receive data for solver " << solverNumber << " from rank " <<
                      fromRank << " at vertex x=" << x << ", level=" << level <<
                      ", src=" << src << ", dest=" << dest);

        limitingADERDGSolver->mergeWithNeighbourDataBasedOnLimiterStatus(
            fromRank,receivedMetadata[solverNumber].getU(),
            destCellDescriptionIndex,element,src,dest,
            true, /* isRecomputation */
            _mergingTemporaryVariables._tempFaceUnknowns[solverNumber],
            _mergingTemporaryVariables._tempStateSizedVectors[solverNumber],
            _mergingTemporaryVariables._tempStateSizedSquareMatrices[solverNumber],
            x,level);
      } else {

        logDebug("mergeWithNeighbourData(...)", "drop data for solver " << solverNumber << " from rank " <<
                      fromRank << " at vertex x=" << x << ", level=" << level <<
                      ", src=" << src << ", dest=" << dest);

        auto* limitingADERDGSolver = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver);
        limitingADERDGSolver->dropNeighbourSolverAndLimiterData(fromRank,src,dest,x,level);
      }
    }
  }
}


//
// Below all methods are nop.
//
//=====================================

void exahype::mappings::SolutionRecomputation::prepareSendToNeighbour(
    exahype::Vertex& vertex, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::prepareCopyToRemoteNode(
    exahype::Vertex& localVertex, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::prepareCopyToRemoteNode(
    exahype::Cell& localCell, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::mergeWithRemoteDataDueToForkOrJoin(
    exahype::Vertex& localVertex, const exahype::Vertex& masterOrWorkerVertex,
    int fromRank, const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::mergeWithRemoteDataDueToForkOrJoin(
    exahype::Cell& localCell, const exahype::Cell& masterOrWorkerCell,
    int fromRank, const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  // do nothing
}

bool exahype::mappings::SolutionRecomputation::prepareSendToWorker(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    int worker) {
  // do nothing
  return false;
}

void exahype::mappings::SolutionRecomputation::prepareSendToMaster(
    exahype::Cell& localCell, exahype::Vertex* vertices,
    const peano::grid::VertexEnumerator& verticesEnumerator,
    const exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::mergeWithMaster(
    const exahype::Cell& workerGridCell,
    exahype::Vertex* const workerGridVertices,
    const peano::grid::VertexEnumerator& workerEnumerator,
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    int worker, const exahype::State& workerState,
    exahype::State& masterState) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::receiveDataFromMaster(
    exahype::Cell& receivedCell, exahype::Vertex* receivedVertices,
    const peano::grid::VertexEnumerator& receivedVerticesEnumerator,
    exahype::Vertex* const receivedCoarseGridVertices,
    const peano::grid::VertexEnumerator& receivedCoarseGridVerticesEnumerator,
    exahype::Cell& receivedCoarseGridCell,
    exahype::Vertex* const workersCoarseGridVertices,
    const peano::grid::VertexEnumerator& workersCoarseGridVerticesEnumerator,
    exahype::Cell& workersCoarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::mergeWithWorker(
    exahype::Cell& localCell, const exahype::Cell& receivedMasterCell,
    const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::mergeWithWorker(
    exahype::Vertex& localVertex, const exahype::Vertex& receivedMasterVertex,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}
#endif

void exahype::mappings::SolutionRecomputation::createHangingVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::destroyHangingVertex(
    const exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::createInnerVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::createBoundaryVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::destroyVertex(
    const exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::createCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::destroyCell(
    const exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::touchVertexLastTime(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::leaveCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::descend(
    exahype::Cell* const fineGridCells, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell) {
  // do nothing
}

void exahype::mappings::SolutionRecomputation::ascend(
    exahype::Cell* const fineGridCells, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell) {
  // do nothing
}
