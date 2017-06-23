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

#ifndef EXAHYPE_MAPPINGS_LimiterStatusSpreading_H_
#define EXAHYPE_MAPPINGS_LimiterStatusSpreading_H_

#include "tarch/la/Vector.h"
#include "tarch/logging/Log.h"

#include "peano/CommunicationSpecification.h"
#include "peano/MappingSpecification.h"
#include "peano/grid/VertexEnumerator.h"

#include "tarch/multicore/MulticoreDefinitions.h"

#include "exahype/Cell.h"
#include "exahype/State.h"
#include "exahype/Vertex.h"

#include "peano/utils/Globals.h"

namespace exahype {
namespace mappings {
class LimiterStatusSpreading;
}
}

/**
 * This mapping is used to spread the limiter status
 * among neighbouring cells and from child to parent.
 *
 * TODO(Dominic): Update docu
 *
 * @author Dominic E. Charrier
 */
class exahype::mappings::LimiterStatusSpreading {
private:
  /**
   * Logging device for the trace macros.
   */
  static tarch::logging::Log _log;

  /**
   * Local copy of the state.
   */
  exahype::State _localState;

  /**
   * Per solver a flag, indicating if has requested
   * a mesh update request or a limiter domain change.
   */
  exahype::solvers::SolverFlags _solverFlags;

public:
  #ifdef Parallel
  /**
   * This variable is unset in LimiterStatusSpreading::beginIteration(...) in the first iteration
   * of LimiterStatusSpreading and then reset in
   * MeshRefinement::beginIteration(...) and Reinitialisaion::beginIteration().
   */
  static bool IsFirstIteration;
  #endif
  /**
   * Switched on.
   */
  peano::MappingSpecification touchVertexFirstTimeSpecification(int level) const;
  /**
   * Switched off
   */
  peano::MappingSpecification touchVertexLastTimeSpecification(int level) const;
  peano::MappingSpecification enterCellSpecification(int level) const;
  peano::MappingSpecification leaveCellSpecification(int level) const;
  peano::MappingSpecification ascendSpecification(int level) const;
  peano::MappingSpecification descendSpecification(int level) const;

  peano::CommunicationSpecification communicationSpecification() const;

#if defined(SharedMemoryParallelisation)
  /**
   * We copy over the veto flag from the master thread
   */
  LimiterStatusSpreading(const LimiterStatusSpreading& masterThread);
#endif
  /**
   * Update the cell-wise limiter status only for solvers
   * of type LimitingADERDG for which the limiter domain
   * has changed irregularly.
   *
   * For these solvers further check if a grid update
   * is necessary after the spreading.
   */
  void enterCell(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);


  /**
   * Initialise all heaps.
   *
   * For each solver, reset the grid update requested flag
   * to false.
   *
   * <h2>MPI</h2>
   * Finish the previous synchronous sends and
   * start synchronous sending again.
   */
  void beginIteration(exahype::State& solverState);

  /**
   * For each solver, set the grid update requested flag
   * for the next iteration.
   *
   * <h2>MPI</h2>
   * If this rank is the global master, update the
   * initial grid refinement strategy.
   */
  void endIteration(exahype::State& solverState);


#ifdef Parallel

  void mergeWithNeighbour(exahype::Vertex& vertex,
      const exahype::Vertex& neighbour, int fromRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const tarch::la::Vector<DIMENSIONS, double>& h,
      int level);

  /**
   * Loop over all cells adjacent to the
   * vertex. If one of the cells is adjacent
   * to a MPI boundary, send the solver type of all solvers
   * registered to the neighbouring MPI rank.
   */
  void prepareSendToNeighbour(exahype::Vertex& vertex, int toRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const tarch::la::Vector<DIMENSIONS, double>& h,
      int level);


  /**
   * Reduce the grid update requested flag up
   * to the master.
   */
  void prepareSendToMaster(
      exahype::Cell& localCell, exahype::Vertex* vertices,
      const peano::grid::VertexEnumerator& verticesEnumerator,
      const exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);

  /**
   * Merge the master's grid update requested flag with the one of the workers.
   */
  void mergeWithWorker(exahype::Cell& localCell,
                       const exahype::Cell& receivedMasterCell,
                       const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
                       const tarch::la::Vector<DIMENSIONS, double>& cellSize,
                       int level);

  //
  // All methods below are nop,
  //
  // ==================================


  /**
   * Nop.
   */
  void prepareCopyToRemoteNode(exahype::Vertex& localVertex, int toRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const tarch::la::Vector<DIMENSIONS, double>& h,
      int level);
  /**
   * Nop.
   */
  void prepareCopyToRemoteNode(
      exahype::Cell& localCell, int toRank,
      const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level);
  /**
   * Nop.
   */
  void mergeWithRemoteDataDueToForkOrJoin(
      exahype::Vertex& localVertex, const exahype::Vertex& masterOrWorkerVertex,
      int fromRank, const tarch::la::Vector<DIMENSIONS, double>& x,
      const tarch::la::Vector<DIMENSIONS, double>& h, int level);
  /**
   * Nop.
   */
  void mergeWithRemoteDataDueToForkOrJoin(
      exahype::Cell& localCell, const exahype::Cell& masterOrWorkerCell,
      int fromRank, const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS, double>& cellSize, int level);
  /**
   * Nop.
   */
  bool prepareSendToWorker(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      int worker);
  /**
   * Nop.
   */
  void mergeWithMaster(
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
      exahype::State& masterState);
  /**
   * Nop.
   */
  void receiveDataFromMaster(
      exahype::Cell& receivedCell, exahype::Vertex* receivedVertices,
      const peano::grid::VertexEnumerator& receivedVerticesEnumerator,
      exahype::Vertex* const receivedCoarseGridVertices,
      const peano::grid::VertexEnumerator& receivedCoarseGridVerticesEnumerator,
      exahype::Cell& receivedCoarseGridCell,
      exahype::Vertex* const workersCoarseGridVertices,
      const peano::grid::VertexEnumerator& workersCoarseGridVerticesEnumerator,
      exahype::Cell& workersCoarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);
  /**
   * Nop.
   */
  void mergeWithWorker(exahype::Vertex& localVertex,
      const exahype::Vertex& receivedMasterVertex,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const tarch::la::Vector<DIMENSIONS, double>& h,
      int level);
#endif
  /**
   * Nop
   */
  LimiterStatusSpreading();
  /**
   * Nop
   */
  virtual ~LimiterStatusSpreading();
#if defined(SharedMemoryParallelisation)
  /**
   * Nop.
   */
  void mergeWithWorkerThread(const LimiterStatusSpreading& workerThread);
#endif
  /**
   * Nop.
   */
  void createInnerVertex(
      exahype::Vertex& fineGridVertex,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);
  /**
   * Nop.
   */
  void createBoundaryVertex(
      exahype::Vertex& fineGridVertex,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);

  /**
   * Nop.
   */
  void createCell(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);

  /**
   * Nop.
   */
  void touchVertexLastTime(
      exahype::Vertex& fineGridVertex,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);
  /**
   * Nop.
   */
  void leaveCell(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);

  /**
   * Nop.
   */
  void createHangingVertex(
      exahype::Vertex& fineGridVertex,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
      const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);
  /**
   * Nop.
   */
  void destroyHangingVertex(
     const exahype::Vertex& fineGridVertex,
     const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
     const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
     exahype::Vertex* const coarseGridVertices,
     const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
     exahype::Cell& coarseGridCell,
     const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);
 /**
  * Nop.
  */
 void destroyVertex(
     const exahype::Vertex& fineGridVertex,
     const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
     const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
     exahype::Vertex* const coarseGridVertices,
     const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
     exahype::Cell& coarseGridCell,
     const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);
 /**
  * Nop.
  */
 void destroyCell(
     const exahype::Cell& fineGridCell,
     exahype::Vertex* const fineGridVertices,
     const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
     exahype::Vertex* const coarseGridVertices,
     const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
     exahype::Cell& coarseGridCell,
     const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);
 /**
  * Nop.
  */
 void touchVertexFirstTime(
     exahype::Vertex& fineGridVertex,
     const tarch::la::Vector<DIMENSIONS, double>& fineGridX,
     const tarch::la::Vector<DIMENSIONS, double>& fineGridH,
     exahype::Vertex* const coarseGridVertices,
     const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
     exahype::Cell& coarseGridCell,
     const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfVertex);

 /**
  * Nop.
  */
 void descend(
     exahype::Cell* const fineGridCells,
     exahype::Vertex* const fineGridVertices,
     const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
     exahype::Vertex* const coarseGridVertices,
     const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
     exahype::Cell& coarseGridCell);
 /**
  * Nop.
  */
 void ascend(exahype::Cell* const fineGridCells,
     exahype::Vertex* const fineGridVertices,
     const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
     exahype::Vertex* const coarseGridVertices,
     const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
     exahype::Cell& coarseGridCell);
};
#endif
