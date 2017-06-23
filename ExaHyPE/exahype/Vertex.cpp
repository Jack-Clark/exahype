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
 
#include "exahype/Vertex.h"
#include "peano/utils/Loop.h"
#include "peano/grid/Checkpoint.h"

#include "peano/datatraversal/autotuning/Oracle.h"

#include "multiscalelinkedcell/HangingVertexBookkeeper.h"

#include "exahype/State.h"

#include "exahype/solvers/ADERDGSolver.h"
#include "exahype/solvers/FiniteVolumesSolver.h"

tarch::logging::Log exahype::Vertex::_log( "exahype::Vertex");

exahype::Vertex::Vertex() : Base() {
  _vertexData.setCellDescriptionsIndex(
      multiscalelinkedcell::HangingVertexBookkeeper::getInstance()
          .createVertexLinkMapForNewVertex() );
}

exahype::Vertex::Vertex(const Base::DoNotCallStandardConstructor& value)
    : Base(value) {
  // do nothing
}

exahype::Vertex::Vertex(const Base::PersistentVertex& argument)
    : Base(argument) {
  // do nothing
}

tarch::la::Vector<TWO_POWER_D, int>
exahype::Vertex::getCellDescriptionsIndex() const {
  return _vertexData.getCellDescriptionsIndex();
}

void exahype::Vertex::mergeOnlyMetadata(
    const exahype::records::State::AlgorithmSection& section) {
  assertion(!isHangingNode());
  assertion(isInside() || isBoundary());

  dfor2(pos1)
    dfor2(pos2)
      if (hasToMergeNeighbours(pos1,pos1Scalar,pos2,pos2Scalar)) { // Implies that we have two valid indices on the correct level
        auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
            parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined15);
        pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
          auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
          if (solver->isComputing(section)) {
            const int cellDescriptionsIndex1 = getCellDescriptionsIndex()[pos1Scalar];
            const int cellDescriptionsIndex2 = getCellDescriptionsIndex()[pos2Scalar];
            const int element1 = solver->tryGetElement(cellDescriptionsIndex1,solverNumber);
            const int element2 = solver->tryGetElement(cellDescriptionsIndex2,solverNumber);
            if (element2>=0 && element1>=0) {
              solver->mergeNeighboursMetadata(
                  cellDescriptionsIndex1,element1,cellDescriptionsIndex2,element2,pos1,pos2);
            }
          }
        endpfor
        grainSize.parallelSectionHasTerminated();

        setMergePerformed(pos1,pos2,true);
      }
      if (hasToMergeWithBoundaryData(pos1,pos1Scalar,pos2,pos2Scalar)) {  // Implies that one is valid the other a DomainBoundaryAdjacencyIndex
        auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
            parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined15);
        pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
        auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
        if (solver->isComputing(section)) {
          const int cellDescriptionsIndex1 = getCellDescriptionsIndex()[pos1Scalar];
          const int cellDescriptionsIndex2 = getCellDescriptionsIndex()[pos2Scalar];
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

        setMergePerformed(pos1,pos2,true);
      }
    enddforx
  enddforx
}


void exahype::Vertex::mergeOnlyMetadataAtHangingNode(
    const exahype::records::State::AlgorithmSection& section) {
  assertion(isHangingNode());

  dfor2(pos1)
    dfor2(pos2)
      if (
          hasToMergeWithBoundaryData(pos1,pos1Scalar,pos2,pos2Scalar)
          ||
          hasToMergeWithEmptyCell(pos1,pos1Scalar,pos2,pos2Scalar)
      ) {
        auto grainSize = peano::datatraversal::autotuning::Oracle::getInstance().
            parallelise(solvers::RegisteredSolvers.size(), peano::datatraversal::autotuning::MethodTrace::UserDefined15);
        pfor(solverNumber, 0, static_cast<int>(solvers::RegisteredSolvers.size()),grainSize.getGrainSize())
        auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];
        if (solver->isComputing(section)) {
          const int cellDescriptionsIndex1 = getCellDescriptionsIndex()[pos1Scalar];
          const int cellDescriptionsIndex2 = getCellDescriptionsIndex()[pos2Scalar];
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

        setMergePerformed(pos1,pos2,true);
      }
    enddforx
  enddforx
}

bool exahype::Vertex::hasToMergeNeighbours(
    const tarch::la::Vector<DIMENSIONS,int>& pos1,
    const int pos1Scalar,
    const tarch::la::Vector<DIMENSIONS,int>& pos2,
    const int pos2Scalar) const {
  assertion(!isHangingNode());

  if (tarch::la::countEqualEntries(pos1,pos2)==(DIMENSIONS-1)) {
    const int cellDescriptionsIndex1 = _vertexData.getCellDescriptionsIndex(pos1Scalar);
    const int cellDescriptionsIndex2 = _vertexData.getCellDescriptionsIndex(pos2Scalar);

    if (//cellDescriptionsIndex1!=cellDescriptionsIndex2 && // This scenario occured during one run due to inconsistent adjacency indices
        exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1) &&
        exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)) {
      assertion1(pos1Scalar!=pos2Scalar,pos1Scalar);
      assertion1(cellDescriptionsIndex1!=cellDescriptionsIndex2,cellDescriptionsIndex1);
      assertion1(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1),
          cellDescriptionsIndex1);
      assertion1(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2),
          cellDescriptionsIndex2);

      const int direction    = tarch::la::equalsReturnIndex(pos1, pos2);
      const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
      const int orientation2 = 1-orientation1;

      const int faceIndex1 = 2*direction+orientation1;
      const int faceIndex2 = 2*direction+orientation2;

      // cell 1
      for (auto& p1 : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
        if (p1.getNeighbourMergePerformed(faceIndex1)) {
          return false;
        }
      }
      for (auto& p1 : exahype::solvers::
          FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
        if (p1.getNeighbourMergePerformed(faceIndex1)) {
          return false;
        }
      }

      // cell 2
      for (auto& p2 : exahype::solvers::
          ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
        if (p2.getNeighbourMergePerformed(faceIndex2)) {
          return false;
        }
      }
      for (auto& p2 : exahype::solvers::
          FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
        if (p2.getNeighbourMergePerformed(faceIndex2)) {
          return false;
        }
      }

      return true;
    }
  }

  return false;
}

bool exahype::Vertex::hasToMergeWithEmptyCell(
      const tarch::la::Vector<DIMENSIONS,int>& pos1,
      const int pos1Scalar,
      const tarch::la::Vector<DIMENSIONS,int>& pos2,
      const int pos2Scalar) const {
  if (tarch::la::countEqualEntries(pos1,pos2)==(DIMENSIONS-1)) {
    const int cellDescriptionsIndex1 =
        _vertexData.getCellDescriptionsIndex(pos1Scalar);
    const int cellDescriptionsIndex2 =
        _vertexData.getCellDescriptionsIndex(pos2Scalar);

    const bool validIndexNextToInvalidIndex =
        (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)
            &&
            cellDescriptionsIndex2==multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex)
            ||
            (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)
                &&
                cellDescriptionsIndex1==multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex);

    if (validIndexNextToInvalidIndex) {
      const int direction    = tarch::la::equalsReturnIndex(pos1, pos2);
      const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
      const int orientation2 = 1-orientation1;

      const int faceIndex1 = 2*direction+orientation1;
      const int faceIndex2 = 2*direction+orientation2;

      // ADER-DG
      if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)) {
        for (auto& p1 : exahype::solvers::
            ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
          if (p1.getIsInside(faceIndex1)
              && !p1.getNeighbourMergePerformed(faceIndex1)) {
            return true;
          }
        }
      } else if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)) {
        for (auto& p2 : exahype::solvers::
            ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
          if (p2.getIsInside(faceIndex2)
              && !p2.getNeighbourMergePerformed(faceIndex2)) {
            return true;
          }
        }
      }

      // Finite Volumes
      if (exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)) {
        for (auto& p1 : exahype::solvers::
            FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
          if (p1.getIsInside(faceIndex1)
              && !p1.getNeighbourMergePerformed(faceIndex1)) {
            return true;
          }
        }
      } else if (exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)) {
        for (auto& p2 : exahype::solvers::
            FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
          if (p2.getIsInside(faceIndex2)
              && !p2.getNeighbourMergePerformed(faceIndex2)) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool exahype::Vertex::hasToMergeWithBoundaryData(
      const tarch::la::Vector<DIMENSIONS,int>& pos1,
      const int pos1Scalar,
      const tarch::la::Vector<DIMENSIONS,int>& pos2,
      const int pos2Scalar) const {
  if (tarch::la::countEqualEntries(pos1,pos2)==(DIMENSIONS-1)) {
    const int cellDescriptionsIndex1 =
        _vertexData.getCellDescriptionsIndex(pos1Scalar);
    const int cellDescriptionsIndex2 =
        _vertexData.getCellDescriptionsIndex(pos2Scalar);

    const bool validIndexNextToBoundaryIndex =
        (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)
            &&
            cellDescriptionsIndex2==multiscalelinkedcell::HangingVertexBookkeeper::DomainBoundaryAdjacencyIndex)
            ||
            (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)
                &&
                cellDescriptionsIndex1==multiscalelinkedcell::HangingVertexBookkeeper::DomainBoundaryAdjacencyIndex);

    if (validIndexNextToBoundaryIndex) {
      const int direction    = tarch::la::equalsReturnIndex(pos1, pos2);
      const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
      const int orientation2 = 1-orientation1;

      const int faceIndex1 = 2*direction+orientation1;
      const int faceIndex2 = 2*direction+orientation2;

      // ADER-DG
      if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)) {
        for (auto& p1 : exahype::solvers::
            ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
          if (!p1.getIsInside(faceIndex1)
              && !p1.getNeighbourMergePerformed(faceIndex1)) {
            return true;
          }
        }
      } else if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)) {
        for (auto& p2 : exahype::solvers::
            ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
          if (!p2.getIsInside(faceIndex2)
              && !p2.getNeighbourMergePerformed(faceIndex2)) {
            return true;
          }
        }
      }

      // Finite Volumes
      if (exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)) {
        for (auto& p1 : exahype::solvers::
            FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
          if (!p1.getIsInside(faceIndex1)
              && !p1.getNeighbourMergePerformed(faceIndex1)) {
            return true;
          }
        }
      } else if (exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)) {
        for (auto& p2 : exahype::solvers::
            FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
          if (!p2.getIsInside(faceIndex2)
              && !p2.getNeighbourMergePerformed(faceIndex2)) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

void exahype::Vertex::setMergePerformed(
        const tarch::la::Vector<DIMENSIONS,int>& pos1,
        const tarch::la::Vector<DIMENSIONS,int>& pos2,
        bool state) const {
  if (tarch::la::countEqualEntries(pos1,pos2)!=(DIMENSIONS-1)) {
    return; // We only consider faces; no corners. TODO(Dominic): Be aware of this
  }

  const int pos1Scalar = peano::utils::dLinearisedWithoutLookup(pos1,2);
  const int pos2Scalar = peano::utils::dLinearisedWithoutLookup(pos2,2);
  const int cellDescriptionsIndex1 = getCellDescriptionsIndex()[pos1Scalar];
  const int cellDescriptionsIndex2 = getCellDescriptionsIndex()[pos2Scalar];

  const int direction    = tarch::la::equalsReturnIndex(pos1, pos2);
  const int orientation1 = (1 + pos2(direction) - pos1(direction))/2;
  const int orientation2 = 1-orientation1;

  const int faceIndex1 = 2*direction+orientation1;
  const int faceIndex2 = 2*direction+orientation2;

  // ADER-DG
  if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1)) {
    for (auto& p1 : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
      p1.setNeighbourMergePerformed(faceIndex1,state);
    }

    assertion(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex1));
    for (auto& p1 : exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex1)) {
      p1.setNeighbourMergePerformed(faceIndex1,state);
    }
  }

  if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2)) {
    for (auto& p2 : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
      p2.setNeighbourMergePerformed(faceIndex2,state);
    }

    assertion(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex2));
    for (auto& p2 : exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(cellDescriptionsIndex2)) {
      p2.setNeighbourMergePerformed(faceIndex2,state);
    }
  }
}

#if Parallel
bool exahype::Vertex::hasToSendMetadata(
  const tarch::la::Vector<DIMENSIONS,int>& src,
  const tarch::la::Vector<DIMENSIONS,int>& dest,
  const int toRank) const {
  const int srcScalar  = peano::utils::dLinearisedWithoutLookup(src,2);
  const int destScalar = peano::utils::dLinearisedWithoutLookup(dest,2);
  const tarch::la::Vector<TWO_POWER_D,int> adjacentRanks = getAdjacentRanks();

  return tarch::la::countEqualEntries(dest, src) == (DIMENSIONS-1) &&
         adjacentRanks(destScalar)   != tarch::parallel::Node::getGlobalMasterRank() &&
         adjacentRanks(destScalar)   == toRank &&
         adjacentRanks(srcScalar)    != tarch::parallel::Node::getGlobalMasterRank() &&
         (adjacentRanks(srcScalar)   == tarch::parallel::Node::getInstance().getRank() ||
         State::isForkTriggeredForRank(adjacentRanks(srcScalar)));
}

void exahype::Vertex::sendOnlyMetadataToNeighbour(
    const int toRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h,
    int level) const {
  if (tarch::la::allGreater(h,exahype::solvers::Solver::getCoarsestMeshSizeOfAllSolvers())) {
    return;
  }
  #if !defined(PeriodicBC)
  if (isBoundary()) return;
  #endif

  tarch::la::Vector<TWO_POWER_D, int> adjacentADERDGCellDescriptionsIndices =
      getCellDescriptionsIndex();

  dfor2(dest)
    dfor2(src)
      if (hasToSendMetadata(src,dest,toRank)) {
        const int srcCellDescriptionIndex = adjacentADERDGCellDescriptionsIndices(srcScalar);
        if (exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(srcCellDescriptionIndex)) {
          exahype::sendNeighbourCommunicationMetadata(
              toRank,srcCellDescriptionIndex,src,dest,x,level);
        } else {
          exahype::sendNeighbourCommunicationMetadataSequenceWithInvalidEntries(
              toRank,x,level);
        }
      }
    enddforx
  enddforx
}

bool exahype::Vertex::hasToReceiveMetadata(
  const tarch::la::Vector<DIMENSIONS,int>& src,
  const tarch::la::Vector<DIMENSIONS,int>& dest,
  const int fromRank) const {
  const int srcScalar  = peano::utils::dLinearisedWithoutLookup(src,2);
  const int destScalar = peano::utils::dLinearisedWithoutLookup(dest,2);
  const tarch::la::Vector<TWO_POWER_D,int> adjacentRanks = getAdjacentRanks();

  return tarch::la::countEqualEntries(dest, src) == (DIMENSIONS-1) &&
      adjacentRanks(srcScalar)    != tarch::parallel::Node::getGlobalMasterRank() &&
      adjacentRanks(srcScalar)    == fromRank &&
      adjacentRanks(destScalar)   != tarch::parallel::Node::getGlobalMasterRank() &&
      (adjacentRanks(destScalar)  == tarch::parallel::Node::getInstance().getRank() ||
       State::isForkingRank(adjacentRanks(destScalar)));
}

void exahype::Vertex::mergeOnlyWithNeighbourMetadata(
    const int fromRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h,
    const int level,
    const exahype::records::State::AlgorithmSection& section) const {
  if (tarch::la::allGreater(h,exahype::solvers::Solver::getCoarsestMeshSizeOfAllSolvers())) {
    return;
  }
  #if !defined(PeriodicBC)
    if (isBoundary()) return;
  #endif

  dfor2(myDest)
    dfor2(mySrc)
      tarch::la::Vector<DIMENSIONS, int> dest = tarch::la::Vector<DIMENSIONS, int>(1) - myDest;
      tarch::la::Vector<DIMENSIONS, int> src  = tarch::la::Vector<DIMENSIONS, int>(1) - mySrc;
      int destScalar = TWO_POWER_D - myDestScalar - 1;

      if (hasToReceiveMetadata(src,dest,fromRank)) {
        logDebug("mergeOnlyWithNeighbourMetadata(...)","[pre] rec. from rank "<<fromRank<<", x:"<<
                 x.toString() << ", level=" <<level << ", adjacentRanks: "
                 << getAdjacentRanks());

        const int receivedMetadataIndex = MetadataHeap::getInstance().
            createData(0,exahype::NeighbourCommunicationMetadataPerSolver*exahype::solvers::RegisteredSolvers.size());
        assertion(MetadataHeap::getInstance().getData(receivedMetadataIndex).empty());
        MetadataHeap::getInstance().receiveData(
            receivedMetadataIndex,
            fromRank, x, level,
            peano::heap::MessageType::NeighbourCommunication);
        MetadataHeap::HeapEntries& receivedMetadata = MetadataHeap::getInstance().
            getData(receivedMetadataIndex);

        for(unsigned int solverNumber = solvers::RegisteredSolvers.size(); solverNumber-- > 0;) {
          auto* solver = solvers::RegisteredSolvers[solverNumber];
          if (solver->isComputing(section)) {
            const int offset  = exahype::NeighbourCommunicationMetadataPerSolver*solverNumber;
            if (receivedMetadata[offset].getU()!=exahype::InvalidMetadataEntry) {
              const int element = solver->tryGetElement(
                  getCellDescriptionsIndex()[destScalar],solverNumber);
              if (element!=exahype::solvers::Solver::NotFound) {
                MetadataHeap::HeapEntries metadataPortion(
                    receivedMetadata.begin()+offset,
                    receivedMetadata.begin()+offset+exahype::NeighbourCommunicationMetadataPerSolver);

                solver->mergeWithNeighbourMetadata(
                    metadataPortion,
                    src, dest,
                    getCellDescriptionsIndex()[destScalar],element);
              }
            }

            logDebug("mergeWithNeighbour(...)","solverNumber: " << solverNumber);
            logDebug("mergeWithNeighbour(...)","neighbourTypeAsInt: "
                << receivedMetadata[solverNumber].getU());
          }
        }

        // Clean up
        MetadataHeap::getInstance().deleteData(receivedMetadataIndex);
      }
    enddforx
  enddforx
}

void exahype::Vertex::dropNeighbourMetadata(
    const int fromRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const tarch::la::Vector<DIMENSIONS, double>& h,
    const int level) const {
  if (tarch::la::allGreater(h,exahype::solvers::Solver::getCoarsestMeshSizeOfAllSolvers())) {
    return;
  }
  #if !defined(PeriodicBC)
    if (isBoundary()) return;
  #endif

  dfor2(myDest)
    dfor2(mySrc)
      tarch::la::Vector<DIMENSIONS, int> dest = tarch::la::Vector<DIMENSIONS, int>(1) - myDest;
      tarch::la::Vector<DIMENSIONS, int> src  = tarch::la::Vector<DIMENSIONS, int>(1) - mySrc;
      int destScalar = TWO_POWER_D - myDestScalar - 1;

      if (hasToReceiveMetadata(src,dest,fromRank)) {
        logDebug("dropNeighbourMetadata(...)","[pre] rec. from rank "<<fromRank<<", x:"<<
                 x.toString() << ", level=" <<level << ", adjacentRanks: "
                 << getAdjacentRanks());

        MetadataHeap::getInstance().receiveData(
            fromRank, x, level,
            peano::heap::MessageType::NeighbourCommunication);
      }
    enddforx
  enddforx
}


bool exahype::Vertex::hasToSendDataToNeighbour(
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest) const {
  const int srcScalar  = peano::utils::dLinearisedWithoutLookup(src,2);
  const int srcCellDescriptionsIndex =
      _vertexData.getCellDescriptionsIndex(srcScalar);

  if (!exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(srcCellDescriptionsIndex) ||
      (exahype::solvers::ADERDGSolver::Heap::getInstance().getData(srcCellDescriptionsIndex).empty()
          && exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(srcCellDescriptionsIndex).empty())) {
    return false; // !!! Make sure to consider all solver types here
  }

  const int normalOfExchangedFace = tarch::la::equalsReturnIndex(src, dest);
  assertion(normalOfExchangedFace >= 0 && normalOfExchangedFace < DIMENSIONS);
  const int faceIndex = 2 * normalOfExchangedFace +
      (src(normalOfExchangedFace) < dest(normalOfExchangedFace) ? 1 : 0); // !!! Be aware of the "<" !!!

  // ADER-DG
  for (auto& p : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(srcCellDescriptionsIndex)) {
    if (
        #if !defined(PeriodicBC)
        !p.getIsInside(faceIndex) ||
        #endif
        p.getFaceDataExchangeCounter(faceIndex)!=0) {
      return false;
    }
  }

  // FV // TODO(Dominic): Make template
  for (auto& p : exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(srcCellDescriptionsIndex)) {
    if (
        #if !defined(PeriodicBC)
        !p.getIsInside(faceIndex) ||
        #endif
        p.getFaceDataExchangeCounter(faceIndex)!=0) {
      return false;
    }
  }

  return true;
}

bool exahype::Vertex::hasToMergeWithNeighbourData(
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest) const {
  const int destScalar  = peano::utils::dLinearisedWithoutLookup(dest,2);
  const int destCellDescriptionsIndex =
      _vertexData.getCellDescriptionsIndex(destScalar);

  if (!exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(destCellDescriptionsIndex) ||
      (exahype::solvers::ADERDGSolver::Heap::getInstance().getData(destCellDescriptionsIndex).empty()
       && exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(destCellDescriptionsIndex).empty())) {
    return false; // !!! Make sure to consider all solver types here
  }

  const int normalOfExchangedFace = tarch::la::equalsReturnIndex(src, dest);
  assertion(normalOfExchangedFace >= 0 && normalOfExchangedFace < DIMENSIONS);
  const int faceIndex = 2 * normalOfExchangedFace +
      (src(normalOfExchangedFace) > dest(normalOfExchangedFace) ? 1 : 0); // !!! Be aware of the ">" !!!

  // ADER-DG
  for (auto& p : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(destCellDescriptionsIndex)) {
    if (
        #if !defined(PeriodicBC)
        !p.getIsInside(faceIndex) ||
        #endif
        p.getFaceDataExchangeCounter(faceIndex)!=0) {
      return false;
    }
  }

  // FV
  for (auto& p : exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(destCellDescriptionsIndex)) {
    if (
        #if !defined(PeriodicBC)
        !p.getIsInside(faceIndex) ||
        #endif
        p.getFaceDataExchangeCounter(faceIndex)!=0) {
      return false;
    }
  }
  return true;
}

void exahype::Vertex::tryDecrementFaceDataExchangeCountersOfSource(
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest) const {
  const int srcScalar  = peano::utils::dLinearisedWithoutLookup(src,2);
  const int srcCellDescriptionsIndex =
      _vertexData.getCellDescriptionsIndex(srcScalar);

  if (!exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(srcCellDescriptionsIndex) ||
      (exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(srcCellDescriptionsIndex).empty()
       && exahype::solvers::ADERDGSolver::Heap::getInstance().getData(srcCellDescriptionsIndex).empty()))
    return;

  assertion1(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().
      isValidIndex(srcCellDescriptionsIndex),
      srcCellDescriptionsIndex);

  const int normalOfExchangedFace = tarch::la::equalsReturnIndex(src,dest);
  assertion(normalOfExchangedFace >= 0 && normalOfExchangedFace < DIMENSIONS);
  const int faceIndex = 2 * normalOfExchangedFace +
      (src(normalOfExchangedFace) < dest(normalOfExchangedFace) ? 1 : 0); // !!! Be aware of the "<" !!!

  // ADER-DG
  for (auto& p : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(srcCellDescriptionsIndex)) {
    int newCounterValue = p.getFaceDataExchangeCounter(faceIndex)-1;
    assertion2(newCounterValue>=0,newCounterValue,p.toString());
    assertion1(newCounterValue<TWO_POWER_D,newCounterValue);
    p.setFaceDataExchangeCounter(faceIndex,newCounterValue);
  }

  // FV
  for (auto& p : exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(srcCellDescriptionsIndex)) {
    int newCounterValue = p.getFaceDataExchangeCounter(faceIndex)-1;
    assertion2(newCounterValue>=0,newCounterValue,p.toString());
    assertion1(newCounterValue<TWO_POWER_D,newCounterValue);
    p.setFaceDataExchangeCounter(faceIndex,newCounterValue);
  }
}

void exahype::Vertex::setFaceDataExchangeCountersOfDestination(
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest,
    const int value) const {
  const int destScalar  = peano::utils::dLinearisedWithoutLookup(dest,2);
  const int destCellDescriptionsIndex =
      _vertexData.getCellDescriptionsIndex(destScalar);

  assertion1(exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(destCellDescriptionsIndex),destCellDescriptionsIndex);
  assertion1(exahype::solvers::FiniteVolumesSolver::Heap::getInstance().isValidIndex(destCellDescriptionsIndex),destCellDescriptionsIndex);

  const int normalOfExchangedFace = tarch::la::equalsReturnIndex(src, dest);
  assertion(normalOfExchangedFace >= 0 && normalOfExchangedFace < DIMENSIONS);
  const int faceIndex = 2 * normalOfExchangedFace +
      (src(normalOfExchangedFace) > dest(normalOfExchangedFace) ? 1 : 0); // !!! Be aware of the ">" !!!

  // ADER-DG
  for (auto& p : exahype::solvers::ADERDGSolver::Heap::getInstance().getData(destCellDescriptionsIndex)) {
    p.setFaceDataExchangeCounter(faceIndex,value);
  }

  // FV
  for (auto& p : exahype::solvers::FiniteVolumesSolver::Heap::getInstance().getData(destCellDescriptionsIndex)) {
    p.setFaceDataExchangeCounter(faceIndex,value);
  }
}
#endif
