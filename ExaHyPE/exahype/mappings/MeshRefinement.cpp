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
 
#include "exahype/mappings/MeshRefinement.h"

#include "peano/utils/Globals.h"
#include "peano/utils/Loop.h"

#include "peano/datatraversal/autotuning/Oracle.h"

#include "tarch/la/VectorScalarOperations.h"

#include "multiscalelinkedcell/HangingVertexBookkeeper.h"

#include "exahype/solvers/LimitingADERDGSolver.h"

#include "exahype/mappings/LimiterStatusSpreading.h"

#ifdef Parallel
bool exahype::mappings::MeshRefinement::IsFirstIteration = true;
#endif

bool exahype::mappings::MeshRefinement::IsInitialMeshRefinement = false;

tarch::logging::Log exahype::mappings::MeshRefinement::_log("exahype::mappings::MeshRefinement");

/**
 * @todo Please tailor the parameters to your mapping's properties.
 */
peano::CommunicationSpecification
exahype::mappings::MeshRefinement::communicationSpecification() const {
  return peano::CommunicationSpecification(
      peano::CommunicationSpecification::ExchangeMasterWorkerData::
          SendDataAndStateBeforeFirstTouchVertexFirstTime,
      peano::CommunicationSpecification::ExchangeWorkerMasterData::
          SendDataAndStateAfterLastTouchVertexLastTime,
      true);
}

peano::MappingSpecification
exahype::mappings::MeshRefinement::touchVertexLastTimeSpecification(int level) const {
  return peano::MappingSpecification(
      #ifdef Parallel
      peano::MappingSpecification::WholeTree,
      #else
      peano::MappingSpecification::Nop,
      #endif
      peano::MappingSpecification::RunConcurrentlyOnFineGrid,true);
}

peano::MappingSpecification
exahype::mappings::MeshRefinement::touchVertexFirstTimeSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::Serial,true);
}

peano::MappingSpecification
exahype::mappings::MeshRefinement::enterCellSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::Serial,true);
}
peano::MappingSpecification
exahype::mappings::MeshRefinement::leaveCellSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::Serial,true);
}
peano::MappingSpecification
exahype::mappings::MeshRefinement::ascendSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidCoarseGridRaces,true);
}
peano::MappingSpecification
exahype::mappings::MeshRefinement::descendSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidCoarseGridRaces,true);
}

#if defined(SharedMemoryParallelisation)
exahype::mappings::MeshRefinement::MeshRefinement(const MeshRefinement& masterThread):
  _localState(masterThread._localState) {
}
#endif

void exahype::mappings::MeshRefinement::beginIteration(
  exahype::State& solverState
) {
  _localState = solverState;

  exahype::solvers::ADERDGSolver::Heap::getInstance().setName("ADERDGCellDescriptionHeap");
  exahype::solvers::FiniteVolumesSolver::Heap::getInstance().setName("FiniteVolumesCellDescriptionHeap");
  DataHeap::getInstance().setName("DataHeap");

  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

    if (solver->getMeshUpdateRequest()) {
      solver->zeroTimeStepSizes();
    }
    //assertion2(!solver->getNextMeshUpdateRequest(),solver->toString(),tarch::parallel::Node::getInstance().getRank());
  }

  #ifdef Parallel
  exahype::solvers::ADERDGSolver::Heap::getInstance().finishedToSendSynchronousData();
  exahype::solvers::FiniteVolumesSolver::Heap::getInstance().finishedToSendSynchronousData();
  DataHeap::getInstance().finishedToSendSynchronousData();

  MetadataHeap::getInstance().setName("MetadataHeap");
  MetadataHeap::getInstance().finishedToSendSynchronousData();
  if (! MetadataHeap::getInstance().validateThatIncomingJoinBuffersAreEmpty() ) {
      exit(-1);
  }

  exahype::solvers::ADERDGSolver::Heap::getInstance().startToSendSynchronousData();
  exahype::solvers::FiniteVolumesSolver::Heap::getInstance().startToSendSynchronousData();
  DataHeap::getInstance().startToSendSynchronousData();
  MetadataHeap::getInstance().startToSendSynchronousData();
  #endif
}

void exahype::mappings::MeshRefinement::endIteration(exahype::State& solverState) {
  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
      auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

      if (solver->getMeshUpdateRequest()) {
        solver->setNextAttainedStableState();
      }
  }

  #ifdef Parallel
  exahype::mappings::MeshRefinement::IsFirstIteration = false;
  #endif
}

void exahype::mappings::MeshRefinement::refineVertexIfNecessary(
  exahype::Vertex&                              fineGridVertex,
  const tarch::la::Vector<DIMENSIONS, double>&  fineGridH,
  int                                           fineGridLevel,
  bool                                          isCalledByCreationalEvent
) const {
  bool refine = false;
  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    refine |= tarch::la::allGreater(fineGridH,p->getMaximumMeshSize());
  }

  if (
    refine
    &&
    fineGridVertex.getRefinementControl()==Vertex::Records::Unrefined
  ) {
    switch ( _localState.mayRefine(isCalledByCreationalEvent,fineGridLevel) ) {
      case State::RefinementAnswer::DontRefineYet:
        break;
      case State::RefinementAnswer::Refine:
        fineGridVertex.refine();
        break;
      case State::RefinementAnswer::EnforceRefinement:
        fineGridVertex.enforceRefine();
        break;
    }
  }
}


void exahype::mappings::MeshRefinement::touchVertexLastTime(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  refineVertexIfNecessary(fineGridVertex,fineGridH,coarseGridVerticesEnumerator.getLevel()+1,false);
}


void exahype::mappings::MeshRefinement::createBoundaryVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  logTraceInWith6Arguments("createBoundaryVertex(...)", fineGridVertex,
                           fineGridX, fineGridH,
                           coarseGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfVertex);

  refineVertexIfNecessary(fineGridVertex,fineGridH,coarseGridVerticesEnumerator.getLevel()+1,true);

  logTraceOutWith1Argument("createBoundaryVertex(...)", fineGridVertex);
}


void exahype::mappings::MeshRefinement::createInnerVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  logTraceInWith6Arguments("createInnerVertex(...)", fineGridVertex, fineGridX,
                           fineGridH, coarseGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfVertex);

  refineVertexIfNecessary(fineGridVertex,fineGridH,coarseGridVerticesEnumerator.getLevel()+1,true);

  logTraceOutWith1Argument("createInnerVertex(...)", fineGridVertex);
}


void exahype::mappings::MeshRefinement::createCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  logTraceInWith4Arguments("createCell(...)", fineGridCell,
                           fineGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfCell);
  fineGridCell.getCellData().setCellDescriptionsIndex(
      multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex);

  logTraceOutWith1Argument("createCell(...)", fineGridCell);
}

void exahype::mappings::MeshRefinement::createHangingVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  logTraceInWith6Arguments("touchVertexFirstTime(...)", fineGridVertex,
                           fineGridX, fineGridH,
                           coarseGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfVertex);

  fineGridVertex.mergeOnlyMetadataAtHangingNode(_localState.getAlgorithmSection());

  logTraceOutWith1Argument("touchVertexFirstTime(...)", fineGridVertex);
}

void exahype::mappings::MeshRefinement::touchVertexFirstTime(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  logTraceInWith6Arguments("touchVertexFirstTime(...)", fineGridVertex,
                           fineGridX, fineGridH,
                           coarseGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfVertex);

  fineGridVertex.mergeOnlyMetadata(_localState.getAlgorithmSection());

  logTraceOutWith1Argument("touchVertexFirstTime(...)", fineGridVertex);
}

void exahype::mappings::MeshRefinement::enterCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  logTraceInWith4Arguments("enterCell(...)", fineGridCell,
                           fineGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfCell);

  assertion(fineGridCell.isInside());

  bool refineFineGridCell = false;
  for (unsigned int solverNumber=0; solverNumber<exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    if (solver->getMeshUpdateRequest()) {
      refineFineGridCell |=
          solver->markForRefinement(
              fineGridCell,
              fineGridVertices,
              fineGridVerticesEnumerator,
              coarseGridCell,
              coarseGridVertices,
              coarseGridVerticesEnumerator,
              fineGridPositionOfCell,
              IsInitialMeshRefinement,
              solverNumber);

      refineFineGridCell |=
          solver->updateStateInEnterCell(
              fineGridCell,
              fineGridVertices,
              fineGridVerticesEnumerator,
              coarseGridCell,
              coarseGridVertices,
              coarseGridVerticesEnumerator,
              fineGridPositionOfCell,
              IsInitialMeshRefinement,
              solverNumber);
    }

    const int element = solver->tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
    if (element!=exahype::solvers::Solver::NotFound) {
      if (solver->getMeshUpdateRequest()) {
        solver->zeroTimeStepSizes(fineGridCell.getCellDescriptionsIndex(),element);
        solver->synchroniseTimeStepping(fineGridCell.getCellDescriptionsIndex(),element);

        solver->setInitialConditions(
            fineGridCell.getCellDescriptionsIndex(),
            element,
            fineGridVertices,
            fineGridVerticesEnumerator);

        if (solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG) {
          static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->
              updateLimiterStatusAndMinAndMaxAfterSetInitialConditions(
                  fineGridCell.getCellDescriptionsIndex(),element);
        }
      }

      solver->prepareNextNeighbourMerging(
          fineGridCell.getCellDescriptionsIndex(),element,
          fineGridVertices,fineGridVerticesEnumerator);
    }
  }

  // Refine all adjacent vertices if necessary and possible.
  //    if (refineFineGridCell && _state.refineInitialGridInTouchVertexLastTime()) {
  if (refineFineGridCell) {
    dfor2(v)
      if (fineGridVertices[ fineGridVerticesEnumerator(v) ].getRefinementControl()==
           exahype::Vertex::Records::RefinementControl::Unrefined
           &&
           !fineGridVertices[ fineGridVerticesEnumerator(v) ].isHangingNode()
           && fineGridVertices[ fineGridVerticesEnumerator(v) ].isInside()
      ) {
        fineGridVertices[ fineGridVerticesEnumerator(v) ].refine();
      }
    enddforx
  }

  logTraceOutWith1Argument("enterCell(...)", fineGridCell);
}

void exahype::mappings::MeshRefinement::leaveCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  logTraceInWith4Arguments("leaveCell(...)", fineGridCell,
                           fineGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfCell);
  bool erasedCellDescriptionOfAllSolvers = true;

  for (unsigned int solverNumber=0; solverNumber<exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    if (solver->getMeshUpdateRequest()) {
      erasedCellDescriptionOfAllSolvers &=
          solver->updateStateInLeaveCell(
              fineGridCell,
              fineGridVertices,
              fineGridVerticesEnumerator,
              coarseGridCell,
              coarseGridVertices,
              coarseGridVerticesEnumerator,
              fineGridPositionOfCell,
              solverNumber);

      bool isStable = solver->attainedStableState(
          fineGridCell,
          fineGridVertices,
          fineGridVerticesEnumerator,
          solverNumber);

      solver->updateNextAttainedStableState(isStable);
    }
  }

  // We assume that the solvers have all removed
  // their data from the heap arrays associated with this
  // cell.
  if (erasedCellDescriptionOfAllSolvers) {
    fineGridCell.shutdownMetaData();

//    dfor2(v)
//    if (coarseGridVertices[ coarseGridVerticesEnumerator(v) ].getRefinementControl()==
//        exahype::Vertex::Records::RefinementControl::Refined
//        &&
//        !coarseGridVertices[ coarseGridVerticesEnumerator(v) ].isHangingNode()
//          &&
//          fineGridVertices[ coarseGridVerticesEnumerator(v) ].isInside()
//    ) {
//      coarseGridVertices[ coarseGridVerticesEnumerator(v) ].erase();
//    }
//    enddforx
  }

  logTraceOutWith1Argument("leaveCell(...)", fineGridCell);
}

#ifdef Parallel
void exahype::mappings::MeshRefinement::mergeWithNeighbour(
    exahype::Vertex& vertex, const exahype::Vertex& neighbour, int fromRank,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH, int level) {
  logTraceInWith6Arguments("mergeWithNeighbour(...)", vertex, neighbour,
                           fromRank, fineGridX, fineGridH, level);

  if (exahype::mappings::MeshRefinement::IsFirstIteration) {
    return;
  }
  vertex.mergeOnlyWithNeighbourMetadata(
      fromRank,fineGridX,fineGridH,level,
      _localState.getAlgorithmSection());

  logTraceOut("mergeWithNeighbour(...)");
}

void exahype::mappings::MeshRefinement::prepareSendToNeighbour(
    exahype::Vertex& vertex, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  logTraceInWith5Arguments("prepareSendToNeighbour(...)", vertex,
                           toRank, x, h, level);

  vertex.sendOnlyMetadataToNeighbour(toRank,x,h,level);

  logTraceOut("prepareSendToNeighbour(...)");
}

bool exahype::mappings::MeshRefinement::prepareSendToWorker(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    int worker) {
  exahype::sendMasterWorkerCommunicationMetadata(
      worker,
      fineGridCell.getCellDescriptionsIndex(),
      fineGridVerticesEnumerator.getCellCenter(),
      fineGridVerticesEnumerator.getLevel());

  return true;
}


void exahype::mappings::MeshRefinement::receiveDataFromMaster(
    exahype::Cell& receivedCell, exahype::Vertex* receivedVertices,
    const peano::grid::VertexEnumerator& receivedVerticesEnumerator,
    exahype::Vertex* const receivedCoarseGridVertices,
    const peano::grid::VertexEnumerator& receivedCoarseGridVerticesEnumerator,
    exahype::Cell& receivedCoarseGridCell,
    exahype::Vertex* const workersCoarseGridVertices,
    const peano::grid::VertexEnumerator& workersCoarseGridVerticesEnumerator,
    exahype::Cell& workersCoarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  if (receivedCell.isInitialised()) {
    const int receivedMetadataIndex =
        exahype::receiveMasterWorkerCommunicationMetadata(
            tarch::parallel::NodePool::getInstance().getMasterRank(),
            receivedVerticesEnumerator.getCellCenter(),
            receivedVerticesEnumerator.getLevel());
    MetadataHeap::HeapEntries& receivedMetadata =
        MetadataHeap::getInstance().getData(receivedMetadataIndex);

    for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
      auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
      if (solver->getMeshUpdateRequest()) {
        const int element = solver->tryGetElement(receivedCell.getCellDescriptionsIndex(),solverNumber);
        const int offset  = exahype::MasterWorkerCommunicationMetadataPerSolver*solverNumber;
        if (element!=exahype::solvers::Solver::NotFound) {
          exahype::MetadataHeap::HeapEntries metadataPortion(
              receivedMetadata.begin()+offset,
              receivedMetadata.begin()+offset+exahype::MasterWorkerCommunicationMetadataPerSolver);

          solver->mergeWithMasterWorkerMetadata(metadataPortion,receivedCell.getCellDescriptionsIndex(),element);
        }
      }
    }
    MetadataHeap::getInstance().deleteData(receivedMetadataIndex);
  } else {
    exahype::dropMetadata(
        tarch::parallel::NodePool::getInstance().getMasterRank(),
        peano::heap::MessageType::MasterWorkerCommunication,
        receivedVerticesEnumerator.getCellCenter(),
        receivedVerticesEnumerator.getLevel());
  } // else do nothing
}


void exahype::mappings::MeshRefinement::prepareCopyToRemoteNode(
    exahype::Cell& localCell, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  if (localCell.isInside() && localCell.isInitialised()) {
    exahype::solvers::ADERDGSolver::sendCellDescriptions(toRank,localCell.getCellDescriptionsIndex(),
        peano::heap::MessageType::ForkOrJoinCommunication,cellCentre,level);
    exahype::solvers::FiniteVolumesSolver::sendCellDescriptions(toRank,localCell.getCellDescriptionsIndex(),
        peano::heap::MessageType::ForkOrJoinCommunication,cellCentre,level);

    for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
        auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

      const int element = solver->tryGetElement(localCell.getCellDescriptionsIndex(),solverNumber);
      if(element!=exahype::solvers::Solver::NotFound) {
        solver->sendDataToWorkerOrMasterDueToForkOrJoin(
            toRank,localCell.getCellDescriptionsIndex(),element,cellCentre,level);
      } else {
        solver->sendEmptyDataToWorkerOrMasterDueToForkOrJoin(toRank,cellCentre,level);
      }
    }

    exahype::solvers::ADERDGSolver::eraseCellDescriptions(localCell.getCellDescriptionsIndex());
    exahype::solvers::FiniteVolumesSolver::eraseCellDescriptions(localCell.getCellDescriptionsIndex());
  } else if (localCell.isInside() && !localCell.isInitialised()){
    exahype::solvers::ADERDGSolver::sendEmptyCellDescriptions(toRank,
        peano::heap::MessageType::ForkOrJoinCommunication,cellCentre,level);
    exahype::solvers::FiniteVolumesSolver::sendEmptyCellDescriptions(toRank,
        peano::heap::MessageType::ForkOrJoinCommunication,cellCentre,level);

    for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
       auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

      solver->sendEmptyDataToWorkerOrMasterDueToForkOrJoin(toRank,cellCentre,level);
    }
  }
}

// TODO(Dominic): How to deal with cell descriptions index that
// is copied from the remote rank but is a valid index on the local
// remote rank? Currently use geometryInfoDoesMatch!
void exahype::mappings::MeshRefinement::mergeWithRemoteDataDueToForkOrJoin(
        exahype::Cell& localCell, const exahype::Cell& masterOrWorkerCell,
        int fromRank, const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
        const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  if (localCell.isInside()) {
    if (!geometryInfoDoesMatch(localCell.getCellDescriptionsIndex(),cellCentre,cellSize,level)) {
      localCell.setCellDescriptionsIndex(
          multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex);
    }

    exahype::solvers::ADERDGSolver::mergeCellDescriptionsWithRemoteData(
        fromRank,localCell,
        peano::heap::MessageType::ForkOrJoinCommunication,
        cellCentre,level);
    exahype::solvers::FiniteVolumesSolver::mergeCellDescriptionsWithRemoteData(
        fromRank,localCell,
        peano::heap::MessageType::ForkOrJoinCommunication,
        cellCentre,level);

    for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
      auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

      int element = solver->tryGetElement(localCell.getCellDescriptionsIndex(),solverNumber);
      if (element!=exahype::solvers::Solver::NotFound) {
        solver->mergeWithWorkerOrMasterDataDueToForkOrJoin(
            fromRank,localCell.getCellDescriptionsIndex(),element,cellCentre,level);
      } else {
        solver->dropWorkerOrMasterDataDueToForkOrJoin(fromRank,cellCentre,level);
      }
    }
  }
}

bool exahype::mappings::MeshRefinement::geometryInfoDoesMatch(
    const int cellDescriptionsIndex,
    const tarch::la::Vector<DIMENSIONS,double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS,double>& cellSize,
    const int level) {
  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    const int element = solver->tryGetElement(cellDescriptionsIndex,solverNumber);
    if (element!=exahype::solvers::Solver::NotFound) {
      switch (solver->getType()) {
        case exahype::solvers::Solver::Type::ADERDG:
        case exahype::solvers::Solver::Type::LimitingADERDG: {
          auto& cellDescription = exahype::solvers::ADERDGSolver::getCellDescription(
              cellDescriptionsIndex,element);
          if (!tarch::la::equals(
              cellCentre,cellDescription.getOffset()+0.5*cellDescription.getSize()) ||
              cellDescription.getLevel()!=level) {
            return false;
          }
        } break;
        case exahype::solvers::Solver::Type::FiniteVolumes: {
          auto& cellDescription = exahype::solvers::FiniteVolumesSolver::getCellDescription(
              cellDescriptionsIndex,element);

          if (!tarch::la::equals(
              cellCentre,cellDescription.getOffset()+0.5*cellDescription.getSize()) ||
              cellDescription.getLevel()!=level) {
            return false;
          }
        } break;
      }
    }
  }
  return true;
}

void exahype::mappings::MeshRefinement::prepareSendToMaster(
    exahype::Cell& localCell, exahype::Vertex* vertices,
    const peano::grid::VertexEnumerator& verticesEnumerator,
    const exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {

  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
      auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

      solver->sendMeshUpdateFlagsToMaster(
          tarch::parallel::NodePool::getInstance().getMasterRank(),
          verticesEnumerator.getCellCenter(),
          verticesEnumerator.getLevel());
  }
}

void exahype::mappings::MeshRefinement::mergeWithMaster(
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
  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); solverNumber++) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

    solver->mergeWithWorkerMeshUpdateFlags(
        worker,
        fineGridVerticesEnumerator.getCellCenter(),
        fineGridVerticesEnumerator.getLevel());
  }
}


//
// All methods below are nop,
//
// ==================================



void exahype::mappings::MeshRefinement::prepareCopyToRemoteNode(
    exahype::Vertex& localVertex, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::MeshRefinement::mergeWithRemoteDataDueToForkOrJoin(
    exahype::Vertex& localVertex, const exahype::Vertex& masterOrWorkerVertex,
    int fromRank, const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::MeshRefinement::mergeWithWorker(
    exahype::Cell& localCell, const exahype::Cell& receivedMasterCell,
    const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  // do nothing
}

void exahype::mappings::MeshRefinement::mergeWithWorker(
    exahype::Vertex& localVertex, const exahype::Vertex& receivedMasterVertex,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}
#endif

exahype::mappings::MeshRefinement::MeshRefinement() {
  // do nothing
}

exahype::mappings::MeshRefinement::~MeshRefinement() {
  // do nothing
}

#if defined(SharedMemoryParallelisation)
void exahype::mappings::MeshRefinement::mergeWithWorkerThread(
    const MeshRefinement& workerThread) {
  // do nothing
}
#endif

void exahype::mappings::MeshRefinement::destroyHangingVertex(
    const exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::MeshRefinement::destroyVertex(
    const exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::MeshRefinement::destroyCell(
    const exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}


void exahype::mappings::MeshRefinement::descend(
    exahype::Cell* const fineGridCells, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell) {
  // do nothing
}

void exahype::mappings::MeshRefinement::ascend(
    exahype::Cell* const fineGridCells, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell) {
  // do nothing
}
