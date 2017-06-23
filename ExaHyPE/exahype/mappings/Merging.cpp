/**
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
 
#include "exahype/mappings/Merging.h"

#include "tarch/multicore/Loop.h"

#include "peano/utils/Loop.h"
#include "peano/datatraversal/autotuning/Oracle.h"

#include "exahype/solvers/LimitingADERDGSolver.h"

#include "peano/utils/UserInterface.h"

peano::CommunicationSpecification
exahype::mappings::Merging::communicationSpecification() const {
  return peano::CommunicationSpecification(
      peano::CommunicationSpecification::ExchangeMasterWorkerData::SendDataAndStateBeforeFirstTouchVertexFirstTime,
      peano::CommunicationSpecification::ExchangeWorkerMasterData::MaskOutWorkerMasterDataAndStateExchange,
      true);
}

peano::MappingSpecification
exahype::mappings::Merging::touchVertexFirstTimeSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::AvoidFineGridRaces,true);
}

// Specifications below are all nop.
peano::MappingSpecification
exahype::mappings::Merging::enterCellSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::WholeTree,
      peano::MappingSpecification::RunConcurrentlyOnFineGrid,true);
}


peano::MappingSpecification
exahype::mappings::Merging::touchVertexLastTimeSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::RunConcurrentlyOnFineGrid,true);
}

peano::MappingSpecification
exahype::mappings::Merging::leaveCellSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidFineGridRaces,true);
}


peano::MappingSpecification
exahype::mappings::Merging::ascendSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidCoarseGridRaces,true);
}


peano::MappingSpecification
exahype::mappings::Merging::descendSpecification(int level) const {
  return peano::MappingSpecification(
      peano::MappingSpecification::Nop,
      peano::MappingSpecification::AvoidCoarseGridRaces,true);
}

tarch::logging::Log exahype::mappings::Merging::_log(
    "exahype::mappings::Merging");

exahype::mappings::Merging::Merging()
  #ifdef Debug
  :
  _remoteBoundaryFaceMerges(0)
  ,_interiorFaceMerges(0)
  ,_boundaryFaceMerges(0)
  #endif
{
  // do nothing
}

exahype::mappings::Merging::~Merging() {
  exahype::solvers::deleteTemporaryVariables(_temporaryVariables);
}

#if defined(SharedMemoryParallelisation)
exahype::mappings::Merging::Merging(const Merging& masterThread) :
  _localState(masterThread._localState)
  #ifdef Debug
  ,_remoteBoundaryFaceMerges(0)
  ,_interiorFaceMerges(0)
  ,_boundaryFaceMerges(0)
  #endif
  {
  exahype::solvers::initialiseTemporaryVariables(_temporaryVariables);
}
#endif

void exahype::mappings::Merging::beginIteration(
    exahype::State& solverState) {
  logTraceInWith1Argument("beginIteration(State)", solverState);

  exahype::solvers::initialiseTemporaryVariables(_temporaryVariables);

  _localState = solverState;

  logDebug("beginIteration(State)",
      "MergeMode="<<exahype::records::State::toString(_localState.getMergeMode())<<
      ", SendMode="<<exahype::records::State::toString(_localState.getSendMode())<<
      ", AlgorithmSection="<<exahype::records::State::toString(_localState.getAlgorithmSection()));

  #ifdef Parallel
  if (_localState.getMergeMode()!=exahype::records::State::MergeMode::MergeNothing) {
    exahype::solvers::ADERDGSolver::Heap::getInstance().finishedToSendSynchronousData();
    exahype::solvers::FiniteVolumesSolver::Heap::getInstance().finishedToSendSynchronousData();
    DataHeap::getInstance().finishedToSendSynchronousData();
    MetadataHeap::getInstance().finishedToSendSynchronousData();
    MetadataHeap::getInstance().validateThatIncomingJoinBuffersAreEmpty();

    if (! MetadataHeap::getInstance().validateThatIncomingJoinBuffersAreEmpty() ) {
        exit(-1);
    }
  }
  #endif

  #ifdef Debug // TODO(Dominic): And not parallel and not shared memory
  _remoteBoundaryFaceMerges = 0;
  _interiorFaceMerges = 0;
  _boundaryFaceMerges = 0;
  #endif

  logTraceOutWith1Argument("beginIteration(State)", solverState);
}

void exahype::mappings::Merging::endIteration(
    exahype::State& solverState) {
  logTraceInWith1Argument("endIteration(State)", solverState);

  exahype::solvers::deleteTemporaryVariables(_temporaryVariables);

  #if defined(Debug) // TODO(Dominic): Use logDebug if it works with filters
  logDebug("endIteration(state)","interiorFaceSolves: " << _interiorFaceMerges);
  logDebug("endIteration(state)","boundaryFaceSolves: " << _boundaryFaceMerges);
  #endif

  logDebug("endIteration(state)","remoteBoundaryFaceMerges: " << _remoteBoundaryFaceMerges);

  logTraceOutWith1Argument("endIteration(State)", solverState);
}

void exahype::mappings::Merging::mergeNeighboursDataAndMetadata(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS,int>&  pos1,
    const int pos1Scalar,
    const tarch::la::Vector<DIMENSIONS,int>&  pos2,
    const int pos2Scalar) {
  auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
  parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined7);
  pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    if (solver->isComputing(_localState.getAlgorithmSection())) {
      const int cellDescriptionsIndex1 = fineGridVertex.getCellDescriptionsIndex()[pos1Scalar];
      const int cellDescriptionsIndex2 = fineGridVertex.getCellDescriptionsIndex()[pos2Scalar];
      const int element1 = solver->tryGetElement(cellDescriptionsIndex1,solverNumber);
      const int element2 = solver->tryGetElement(cellDescriptionsIndex2,solverNumber);
      if (element2>=0 && element1>=0) {
        solver->mergeNeighbours(
            cellDescriptionsIndex1,element1,cellDescriptionsIndex2,element2,pos1,pos2,
            _temporaryVariables._tempFaceUnknowns[solverNumber],
            _temporaryVariables._tempStateSizedVectors[solverNumber],
            _temporaryVariables._tempStateSizedSquareMatrices[solverNumber]);

        if (_localState.getAlgorithmSection()==exahype::records::State::AlgorithmSection::TimeStepping) {
          solver->mergeNeighboursMetadata(
              cellDescriptionsIndex1,element1,cellDescriptionsIndex2,element2,pos1,pos2);
        }
      }
      #ifdef Debug // TODO(Dominic):
      _interiorFaceMerges++;
      #endif
    }
  endpfor
  grainSize.parallelSectionHasTerminated();
}

void exahype::mappings::Merging::mergeWithBoundaryDataAndMetadata(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS,int>&  pos1,
    const int pos1Scalar,
    const tarch::la::Vector<DIMENSIONS,int>&  pos2,
    const int pos2Scalar) {
  auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
  parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined8);
  pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
    if (solver->isComputing(_localState.getAlgorithmSection())) {
      const int cellDescriptionsIndex1 = fineGridVertex.getCellDescriptionsIndex()[pos1Scalar];
      const int cellDescriptionsIndex2 = fineGridVertex.getCellDescriptionsIndex()[pos2Scalar];
      int element1 = solver->tryGetElement(cellDescriptionsIndex1,solverNumber);
      int element2 = solver->tryGetElement(cellDescriptionsIndex2,solverNumber);
      assertion4((element1==exahype::solvers::Solver::NotFound &&
                  element2==exahype::solvers::Solver::NotFound)
                 || (element1 >= 0 && element2==exahype::solvers::Solver::NotFound)
                 || (element2 >= 0 && element1==exahype::solvers::Solver::NotFound),
                 cellDescriptionsIndex1,cellDescriptionsIndex2,element1,element2);

      if (element1 >= 0) {
        solver->mergeWithBoundaryData(cellDescriptionsIndex1,element1,pos1,pos2,
                                      _temporaryVariables._tempFaceUnknowns[solverNumber],
                                      _temporaryVariables._tempStateSizedVectors[solverNumber],
                                      _temporaryVariables._tempStateSizedSquareMatrices[solverNumber]);
        if (_localState.getAlgorithmSection()==exahype::records::State::AlgorithmSection::TimeStepping) {
          solver->mergeWithBoundaryOrEmptyCellMetadata(cellDescriptionsIndex1,element1,pos1,pos2);
        }

        #ifdef Debug
        _boundaryFaceMerges++;
        #endif
      }
      if (element2 >= 0){
        solver->mergeWithBoundaryData(cellDescriptionsIndex2,element2,pos2,pos1,
                                      _temporaryVariables._tempFaceUnknowns[solverNumber],
                                      _temporaryVariables._tempStateSizedVectors[solverNumber],
                                      _temporaryVariables._tempStateSizedSquareMatrices[solverNumber]);

        if (_localState.getAlgorithmSection()==exahype::records::State::AlgorithmSection::TimeStepping) {
          solver->mergeWithBoundaryOrEmptyCellMetadata(cellDescriptionsIndex2,element2,pos2,pos1);
        }

        #ifdef Debug
        _boundaryFaceMerges++;
        #endif
      }
    }
  endpfor
  grainSize.parallelSectionHasTerminated();
}

void exahype::mappings::Merging::mergeWithBoundaryOrEmptyCellMetadata(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS,int>&  pos1,
    const int pos1Scalar,
    const tarch::la::Vector<DIMENSIONS,int>&  pos2,
    const int pos2Scalar) {
  auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
      parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined15);
  pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
  auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
  if (solver->isComputing(_localState.getAlgorithmSection())) {
    const int cellDescriptionsIndex1 = fineGridVertex.getCellDescriptionsIndex()[pos1Scalar];
    const int cellDescriptionsIndex2 = fineGridVertex.getCellDescriptionsIndex()[pos2Scalar];
    const int element1 = solver->tryGetElement(cellDescriptionsIndex1,solverNumber);
    const int element2 = solver->tryGetElement(cellDescriptionsIndex2,solverNumber);
    if (element1>=0) {
      assertion(element2==exahype::solvers::Solver::NotFound);
      solver->mergeWithBoundaryOrEmptyCellMetadata(
          cellDescriptionsIndex1,element1,pos1,pos2);
    }
    if (element2>=0) {
      assertion(element1==exahype::solvers::Solver::NotFound);
      solver->mergeWithBoundaryOrEmptyCellMetadata(
          cellDescriptionsIndex2,element2,pos2,pos1); // Be aware of the order
    }
  }
  endpfor
  grainSize.parallelSectionHasTerminated();
}

void exahype::mappings::Merging::createHangingVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  logTraceInWith6Arguments("createHangingVertex(...)", fineGridVertex,
                           fineGridX, fineGridH,
                           coarseGridVerticesEnumerator.toString(),
                           coarseGridCell, fineGridPositionOfVertex);

  if (_localState.getMergeMode()==exahype::records::State::MergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::BroadcastAndMergeTimeStepDataAndMergeFaceData) {
    dfor2(pos1)
      dfor2(pos2)
        if (fineGridVertex.hasToMergeWithBoundaryData(pos1,pos1Scalar,pos2,pos2Scalar)) {
          mergeWithBoundaryDataAndMetadata(fineGridVertex,pos1,pos1Scalar,pos2,pos2Scalar);

          fineGridVertex.setMergePerformed(pos1,pos2,true);
        }
        if (fineGridVertex.hasToMergeWithEmptyCell(pos1,pos1Scalar,pos2,pos2Scalar)) {
          mergeWithBoundaryOrEmptyCellMetadata(fineGridVertex,pos1,pos1Scalar,pos2,pos2Scalar);

          fineGridVertex.setMergePerformed(pos1,pos2,true);
        }
      enddforx
    enddforx
  }

  logTraceOutWith1Argument("createHangingVertex(...)", fineGridVertex);
}

void exahype::mappings::Merging::touchVertexFirstTime(
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

  if (_localState.getMergeMode()==exahype::records::State::MergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::BroadcastAndMergeTimeStepDataAndMergeFaceData) {
    dfor2(pos1)
      dfor2(pos2)
        // TODO(Dominic): There are some redundant parts in these checks
        if (fineGridVertex.hasToMergeNeighbours(pos1,pos1Scalar,pos2,pos2Scalar)) { // Assumes that we have to valid indices
          mergeNeighboursDataAndMetadata(fineGridVertex,pos1,pos1Scalar,pos2,pos2Scalar);

          fineGridVertex.setMergePerformed(pos1,pos2,true);
        }
        if (fineGridVertex.hasToMergeWithBoundaryData(pos1,pos1Scalar,pos2,pos2Scalar)) {
          mergeWithBoundaryDataAndMetadata(fineGridVertex,pos1,pos1Scalar,pos2,pos2Scalar);

          fineGridVertex.setMergePerformed(pos1,pos2,true);
        }
      enddforx
    enddforx
  }

  logTraceOutWith1Argument("touchVertexFirstTime(...)", fineGridVertex);
}

#ifdef Parallel
///////////////////////////////////////
// NEIGHBOUR
///////////////////////////////////////
// TODO(Dominic): Add to docu: We receive metadata on all vertices.
// We receive only for Cell/Ancestor/Descendants cell descriptions face data.
// EmptyAncestor/EmptyDescendants/InvalidAdjacencyIndices drop face data
// that was sent to them by Cells/Ancestors/Descendants.
void exahype::mappings::Merging::mergeWithNeighbour(
    exahype::Vertex& vertex, const exahype::Vertex& neighbour, int fromRank,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH, int level) {
  if (tarch::la::allGreater(fineGridH,exahype::solvers::Solver::getCoarsestMeshSizeOfAllSolvers())) {
    return;
  }
  
  if (
      _localState.getMergeMode()==exahype::records::State::MergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::BroadcastAndMergeTimeStepDataAndMergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::DropFaceData ||
      _localState.getMergeMode()==exahype::records::State::BroadcastAndMergeTimeStepDataAndDropFaceData
  ) {
    // logDebug("mergeWithNeighbour(...)","hasToMerge");

    dfor2(myDest)
      dfor2(mySrc)
        tarch::la::Vector<DIMENSIONS, int> dest = tarch::la::Vector<DIMENSIONS, int>(1) - myDest;
        tarch::la::Vector<DIMENSIONS, int> src  = tarch::la::Vector<DIMENSIONS, int>(1) - mySrc;

        int destScalar = TWO_POWER_D - myDestScalar - 1;
        int srcScalar  = TWO_POWER_D - mySrcScalar  - 1;

        if (vertex.hasToReceiveMetadata(src,dest,fromRank)) {
          // logDebug("mergeWithNeighbour(...)","hasToReceiveMetadata");

          const int receivedMetadataIndex =
          exahype::receiveNeighbourCommunicationMetadata(
              fromRank, fineGridX, level);
          exahype::MetadataHeap::HeapEntries& receivedMetadata =
              MetadataHeap::getInstance().getData(receivedMetadataIndex);
          assertion(receivedMetadata.size()==exahype::NeighbourCommunicationMetadataPerSolver*solvers::RegisteredSolvers.size());

          if(vertex.hasToMergeWithNeighbourData(src,dest)) {
            // logDebug("mergeWithNeighbour(...)","hasToMergeWithNeighbourData");

            if (_localState.getMergeMode()==exahype::records::State::MergeFaceData ||
                _localState.getMergeMode()==exahype::records::State::BroadcastAndMergeTimeStepDataAndMergeFaceData) {
              mergeWithNeighbourData(
                  fromRank,
                  vertex.getCellDescriptionsIndex()[srcScalar],
                  vertex.getCellDescriptionsIndex()[destScalar],
                  src,dest,
                  fineGridX,level,
                  receivedMetadata);
            } else { // _localState.getMergeMode()==exahype::records::State::DropFaceData ||
                     // _localState.getMergeMode()==exahype::records::State::BroadcastAndMergeTimeStepDataAndDropFaceData
              dropNeighbourData(
                  fromRank,
                  vertex.getCellDescriptionsIndex()[srcScalar],
                  vertex.getCellDescriptionsIndex()[destScalar],
                  src,dest,
                  fineGridX,level,
                  receivedMetadata);
            }

            vertex.setFaceDataExchangeCountersOfDestination(src,dest,TWO_POWER_D);
            vertex.setMergePerformed(src,dest,true);
          } else {
            dropNeighbourData(
                fromRank,
                vertex.getCellDescriptionsIndex()[srcScalar],
                vertex.getCellDescriptionsIndex()[destScalar],
                src,dest,
                fineGridX,level,
                receivedMetadata);
          }
          // Clean up
          MetadataHeap::getInstance().deleteData(receivedMetadataIndex);
        }
      enddforx
    enddforx
  }
}

void exahype::mappings::Merging::mergeWithNeighbourData(
        const int fromRank,
        const int srcCellDescriptionIndex,
        const int destCellDescriptionIndex,
        const tarch::la::Vector<DIMENSIONS,int>& src,
        const tarch::la::Vector<DIMENSIONS,int>& dest,
        const tarch::la::Vector<DIMENSIONS, double>& x,
        const int level,
        const exahype::MetadataHeap::HeapEntries& receivedMetadata) {
  for(unsigned int solverNumber = solvers::RegisteredSolvers.size(); solverNumber-- > 0;) {
    auto* solver = solvers::RegisteredSolvers[solverNumber];
    if (solver->isComputing(_localState.getAlgorithmSection())) {
      const int offset = exahype::NeighbourCommunicationMetadataPerSolver*solverNumber;
      if (receivedMetadata[offset].getU()!=exahype::InvalidMetadataEntry) {
        const int element = solver->tryGetElement(destCellDescriptionIndex,solverNumber);
        assertion1(element>=0,element);

        exahype::MetadataHeap::HeapEntries metadataPortion(
            receivedMetadata.begin()+offset,
            receivedMetadata.begin()+offset+exahype::NeighbourCommunicationMetadataPerSolver);

        logDebug(
            "mergeWithNeighbour(...)", "receive data for solver " << solverNumber << " from " <<
            fromRank << " at vertex x=" << x << ", level=" << level <<
            ", src=" << src << ", dest=" << dest);

        solver->mergeWithNeighbourData(
            fromRank,
            metadataPortion,
            destCellDescriptionIndex,element,src,dest,
            _temporaryVariables._tempFaceUnknowns[solverNumber],
            _temporaryVariables._tempStateSizedVectors[solverNumber],
            _temporaryVariables._tempStateSizedSquareMatrices[solverNumber],
            x,level);

        if (_localState.getAlgorithmSection()==exahype::records::State::AlgorithmSection::TimeStepping) {
          solver->mergeWithNeighbourMetadata(
              metadataPortion,
              src, dest,
              destCellDescriptionIndex,element);
        }


      } else {
        logDebug(
            "mergeWithNeighbour(...)", "drop data for solver " << solverNumber << " from " <<
            fromRank << " at vertex x=" << x << ", level=" << level <<
            ", src=" << src << ", dest=" << dest);

        solver->dropNeighbourData(
            fromRank,src,dest,x,level);
      }
      #ifdef Debug
      _remoteBoundaryFaceMerges++;
      #endif
    }
  }
}

void exahype::mappings::Merging::dropNeighbourData(
    const int fromRank,
    const int srcCellDescriptionIndex,
    const int destCellDescriptionIndex,
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int level,
    const exahype::MetadataHeap::HeapEntries& receivedMetadata) {
  for(unsigned int solverNumber = solvers::RegisteredSolvers.size(); solverNumber-- > 0;) {
    auto* solver = solvers::RegisteredSolvers[solverNumber];

    logDebug("dropNeighbourData(...)", "drop data from rank" <<
             fromRank << " at vertex x=" << x << ", level=" << level <<
             ", src=" << src << ", dest=" << dest << ", solverNumber=" << solverNumber <<
             ", algorithmSection="<<exahype::records::State::toString(_localState.getAlgorithmSection()));

    if (solver->isComputing(_localState.getAlgorithmSection())) {
      solver->dropNeighbourData(fromRank,src,dest,x,level);
      #ifdef Debug
      _remoteBoundaryFaceMerges++;
      #endif
    }
  }
}

///////////////////////////////////////
// MASTER->WORKER
///////////////////////////////////////
bool exahype::mappings::Merging::prepareSendToWorker(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
    int worker) {
  logDebug("prepareSendToWorker(...)","MergeMode="<<_localState.getMergeMode()<<", SendMode="<<_localState.getSendMode());

  if (_localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepData ||
      _localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepDataAndMergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepDataAndDropFaceData) {
    // Send global solver data
    for (auto& solver : exahype::solvers::RegisteredSolvers) {
      solver->sendDataToWorker(
          worker,
          fineGridVerticesEnumerator.getCellCenter(),
          fineGridVerticesEnumerator.getLevel());
    }

    // Send global plotter data
    for (auto& plotter : exahype::plotters::RegisteredPlotters) {
      plotter->sendDataToWorker(
          worker,
          fineGridVerticesEnumerator.getCellCenter(),
          fineGridVerticesEnumerator.getLevel());
    }
  }

  if ((_localState.getMergeMode()==exahype::records::State::MergeMode::MergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepDataAndMergeFaceData)
      && fineGridCell.isInside()) { // TODO(Dominic): Geometry
    exahype::sendMasterWorkerCommunicationMetadata( // TODO(Dominic): Always send. Check again
        worker,fineGridCell.getCellDescriptionsIndex(),
        fineGridVerticesEnumerator.getCellCenter(),
        fineGridVerticesEnumerator.getLevel());

    if (fineGridCell.isInitialised()) {
      for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
        auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
        if (solver->isComputing(_localState.getAlgorithmSection())) {
          const int element = solver->tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
          if (element!=exahype::solvers::Solver::NotFound) {
            solver->sendDataToWorker(
                worker,
                fineGridCell.getCellDescriptionsIndex(),element,
                fineGridVerticesEnumerator.getCellCenter(),
                fineGridVerticesEnumerator.getLevel());
          } else {
            solver->sendEmptyDataToWorker(
                worker,
                fineGridVerticesEnumerator.getCellCenter(),
                fineGridVerticesEnumerator.getLevel());
          }
        }
      }
    } else {
      // TODO(Dominic): Probably not necessary if cell is not initialised.
      for (auto* solver : exahype::solvers::RegisteredSolvers) {
        if (solver->isComputing(_localState.getAlgorithmSection())) {
          solver->sendEmptyDataToWorker(
              worker,
              fineGridVerticesEnumerator.getCellCenter(),
              fineGridVerticesEnumerator.getLevel());
        }
      }
    } // else do nothing
  }

  return false;
}

void exahype::mappings::Merging::receiveDataFromMaster(
    exahype::Cell& receivedCell, exahype::Vertex* receivedVertices,
    const peano::grid::VertexEnumerator& receivedVerticesEnumerator,
    exahype::Vertex* const receivedCoarseGridVertices,
    const peano::grid::VertexEnumerator& receivedCoarseGridVerticesEnumerator,
    exahype::Cell& receivedCoarseGridCell,
    exahype::Vertex* const workersCoarseGridVertices,
    const peano::grid::VertexEnumerator& workersCoarseGridVerticesEnumerator,
    exahype::Cell& workersCoarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  if (_localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepData ||
      _localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepDataAndMergeFaceData ||
      _localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepDataAndDropFaceData) {
    // Receive global solver data from master
    for (auto& solver : exahype::solvers::RegisteredSolvers) {
      solver->mergeWithMasterData(
          tarch::parallel::NodePool::getInstance().getMasterRank(),
          receivedVerticesEnumerator.getCellCenter(),
          receivedVerticesEnumerator.getLevel());
    }

    // Receive global plotter data from master
    for (auto& plotter : exahype::plotters::RegisteredPlotters) {
      plotter->mergeWithMasterData(
          tarch::parallel::NodePool::getInstance().getMasterRank(),
          receivedVerticesEnumerator.getCellCenter(),
          receivedVerticesEnumerator.getLevel());
    }
  }

  if ((_localState.getMergeMode()==exahype::records::State::MergeMode::MergeFaceData ||
       _localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepDataAndMergeFaceData)
      &&
      receivedCell.isInside()) {  // TODO(Dominic): Geometry
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
        if (solver->isComputing(_localState.getAlgorithmSection())) {
          const int element = solver->tryGetElement(receivedCell.getCellDescriptionsIndex(),solverNumber);
          const int offset  = exahype::MasterWorkerCommunicationMetadataPerSolver*solverNumber;
          if (element!=exahype::solvers::Solver::NotFound &&
              receivedMetadata[offset].getU()!=exahype::InvalidMetadataEntry) {

            exahype::MetadataHeap::HeapEntries metadataPortion(
                receivedMetadata.begin()+offset,
                receivedMetadata.begin()+offset+exahype::MasterWorkerCommunicationMetadataPerSolver);

            solver->mergeWithMasterData(
                tarch::parallel::NodePool::getInstance().getMasterRank(),
                metadataPortion,
                receivedCell.getCellDescriptionsIndex(),element,
                receivedVerticesEnumerator.getCellCenter(),
                receivedVerticesEnumerator.getLevel());
          } else {
            solver->dropMasterData(
                tarch::parallel::NodePool::getInstance().getMasterRank(),
                receivedVerticesEnumerator.getCellCenter(),
                receivedVerticesEnumerator.getLevel());
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

      for (auto solver : exahype::solvers::RegisteredSolvers) {
        if (solver->isComputing(_localState.getAlgorithmSection())) {
          solver->dropMasterData(
              tarch::parallel::NodePool::getInstance().getMasterRank(),
              receivedVerticesEnumerator.getCellCenter(),
              receivedVerticesEnumerator.getLevel());
        }
      }
    } // else do nothing
  }
}


//
// Below all methods are nop.
//
// ====================================


void exahype::mappings::Merging::mergeWithWorker(
    exahype::Cell& localCell, const exahype::Cell& receivedMasterCell,
    const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
   // do nothing
}

void exahype::mappings::Merging::mergeWithRemoteDataDueToForkOrJoin(
    exahype::Vertex& localVertex, const exahype::Vertex& masterOrWorkerVertex,
    int fromRank, const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::Merging::mergeWithRemoteDataDueToForkOrJoin(
    exahype::Cell& localCell, const exahype::Cell& masterOrWorkerCell,
    int fromRank, const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  // do nothing
}

void exahype::mappings::Merging::mergeWithWorker(
    exahype::Vertex& localVertex, const exahype::Vertex& receivedMasterVertex,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::Merging::prepareSendToMaster(
    exahype::Cell& localCell, exahype::Vertex* vertices,
    const peano::grid::VertexEnumerator& verticesEnumerator,
    const exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    const exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::Merging::mergeWithMaster(
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

void exahype::mappings::Merging::prepareSendToNeighbour(
    exahype::Vertex& vertex, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::Merging::prepareCopyToRemoteNode(
    exahype::Vertex& localVertex, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h, int level) {
  // do nothing
}

void exahype::mappings::Merging::prepareCopyToRemoteNode(
    exahype::Cell& localCell, int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
    const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level) {
  // do nothing
}

#endif

#if defined(SharedMemoryParallelisation)
void exahype::mappings::Merging::mergeWithWorkerThread(
    const Merging& workerThread) {
  // do nothing
}
#endif

void exahype::mappings::Merging::enterCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // Does not need to merge time step data if face data is merged also since we merge then anyway
  // in touchVertexFirstTime()
  if (_localState.getMergeMode()==exahype::records::State::MergeMode::BroadcastAndMergeTimeStepData) {
    for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
      auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
      if (solver->isComputing(_localState.getAlgorithmSection())) {
        int element = solver->tryGetElement(fineGridCell.getCellDescriptionsIndex(),solverNumber);
        if (element!=exahype::solvers::Solver::NotFound) {
          solver->synchroniseTimeStepping(fineGridCell.getCellDescriptionsIndex(),element);
        }
      }
    }
  }
}

void exahype::mappings::Merging::destroyHangingVertex(
    const exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::Merging::createInnerVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::Merging::createBoundaryVertex(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::Merging::destroyVertex(
    const exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::Merging::createCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::Merging::destroyCell(
    const exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::Merging::touchVertexLastTime(
    exahype::Vertex& fineGridVertex,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
    const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex) {
  // do nothing
}

void exahype::mappings::Merging::leaveCell(
    exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell,
    const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell) {
  // do nothing
}

void exahype::mappings::Merging::descend(
    exahype::Cell* const fineGridCells, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell) {
  // do nothing
}

void exahype::mappings::Merging::ascend(
    exahype::Cell* const fineGridCells, exahype::Vertex* const fineGridVertices,
    const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
    exahype::Vertex* const coarseGridVertices,
    const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
    exahype::Cell& coarseGridCell) {
  // do nothing
}
