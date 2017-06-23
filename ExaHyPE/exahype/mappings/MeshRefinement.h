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

#ifndef EXAHYPE_MAPPINGS_MeshRefinement_H_
#define EXAHYPE_MAPPINGS_MeshRefinement_H_

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
class MeshRefinement;
}
}

/**
 * TODO(Dominic): Update documentation.
 *
 * This mapping builds up the regular base mesh used by all simulations.
 * The regular mesh is determined by the minimal mesh size of all involved
 * solvers. Basically, the only routine that does something in this mapping
 * is the private one. The only thing this private routine does is to call
 * refine().
 *
 *
 * @author Dominic E. Charrier, Tobias Weinzierl
 */
class exahype::mappings::MeshRefinement {
private:
  /**
   * Logging device for the trace macros.
   */
  static tarch::logging::Log _log;

  /**
   * I use a copy of the state to determine whether I'm allowed to refine or not.
   */
  State _localState;

  /**
   * TODO(Tobias): Add docu.
   */
  void refineVertexIfNecessary(
      exahype::Vertex&                              fineGridVertex,
      const tarch::la::Vector<DIMENSIONS, double>&  fineGridH,
      int                                           fineGridLevel,
      bool                                          isCalledByCreationalEvent
  ) const;

  #ifdef Parallel
  /**
   * Returns false if the \p cellDescriptionsIndex is invalid,
   * or if no cell descriptions is registered for this cellDescriptionsIndex,
   * i.e., the vector is empty.
   * Further returns false if the geometry information on the cell descriptions
   * the \p cellDescriptionsIndex is pointing at does not match
   * with \p cellCentre and \p cellSize.
   *
   * Returns true otherwise.
   */
  static bool geometryInfoDoesMatch(
      const int cellDescriptionsIndex,
      const tarch::la::Vector<DIMENSIONS,double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS,double>& cellSize,
      const int level);
  #endif

public:

  static bool IsInitialMeshRefinement;

  #ifdef Parallel
  /**
   * This variable is unset in MeshRefinement::beginIteration(...) in the first iteration
   * of MeshRefinement and then reset in
   * FinaliseMeshRefinement::beginIteration(...).
   */
  static bool IsFirstIteration;
  #endif
  /**
   * Switched off in serial mode where everything is done in the creational
   * routines. Switched on in parallel mode.
   */
  peano::MappingSpecification touchVertexLastTimeSpecification(int level) const;

  /**
   * We merge the limite status between neighbouring cells.
   * We thus avoid fine grid races.
   */
  peano::MappingSpecification touchVertexFirstTimeSpecification(int level) const;

  /**
   * Traverse the cells in serial. Might
   * be relaxed when all semaphores are in place.
   */
  peano::MappingSpecification enterCellSpecification(int level) const;

  /**
   * Traverse the cells in serial. Might
   * be relaxed when all semaphores are in place.
   */
  peano::MappingSpecification leaveCellSpecification(int level) const;

  /**
   * Switched off
   */
  peano::MappingSpecification ascendSpecification(int level) const;
  peano::MappingSpecification descendSpecification(int level) const;

  peano::CommunicationSpecification communicationSpecification() const;

#if defined(SharedMemoryParallelisation)
  /**
   * We copy over the veto flag from the master thread
   */
  MeshRefinement(const MeshRefinement& masterThread);
#endif
  /**
   * TODO(Tobias): Add docu.
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
   * TODO(Tobias): Add docu.
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
    * Initialises the cell descriptions index of a new
    * fine grid cell to an invalid default value.
    */
  void createCell(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);

  /**
   * Loop over the solver and update the solver state.
   * If at least one of the solvers requested refinement,
   * refine the fine grid cell hosting the solvers.
   *
   * Further update the gridUpdateRequested flag
   * of each solver.
   *
   * Further synchronise the time stepping of the patches
   * with the solver and zero the time step sizes.
   *
   * TODO(Dominic): Update the docu
   *
   * We distinguish among three different refinement modes:
   *
   * RefinementMode | Action
   * ---------------|------------------------------
   * Initial        | In this refinement mode, we evaluate the user's refinement criterion
   *                | as well as the limiter's physical admissibility detection (PAD) criterion
   *                | if a LimitingADERDGSolver is employed.
   *                | [LimitingADERDGSolver] We aggressively refine all cells that do not satisfy the PAD down
   *                | to the finest level specified by the user for a solver.
   *                | The user's refinement criterion is used
   *                | to resolve other features of the solution more accurately.
   * APriori        | Refine the mesh according to the user's refinement criterion
   *                | after a solution update has been performed.
   * APosteriori    | [LimitingADERDGSolver] Ensure that cells which have been newly marked as Troubled
   *                | and their next two neighbours always reside on the finest level of the grid.
   *
   * Open Issues:
   * * TODO(Dominic): The refinement criteria have to consider the maximum depth of the adaptive mesh (supplied by the user)
   * * TODO(Dominic): We have to merge the LimiterStatusSpreading with the mesh refinement
   */
  void enterCell(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);

  /**
   * Loop over the solver and update the solver state.
   * If all of the solvers hosted by the cell requested erasing,
   * erase the fine grid cell.
   *
   * Further update the gridUpdateRequested flag
   * of each solver.In contrast to the grid update request handling
   * during the time stepping, we set the flag here also for erasing requests.
   * The rationale is that we do only stop the time
   * stepping if the problem is not well resolved anymore.
   */
  void leaveCell(
      exahype::Cell& fineGridCell, exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell);

  /**
   * TODO(Tobias): Add docu.
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
   * Initialise all heaps.
   *
   * For each solver, reset the grid update requested flag
   * to false.
   *
   * Further zero the time step sizes of the solver.
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
  MeshRefinement();
  /**
   * Nop
   */
  virtual ~MeshRefinement();
#if defined(SharedMemoryParallelisation)
  /**
   * Nop.
   */
  void mergeWithWorkerThread(const MeshRefinement& workerThread);
#endif
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
