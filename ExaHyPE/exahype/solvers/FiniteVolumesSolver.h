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
 * @author Dominic E. Charrier, Tobias Weinzierl
 **/

#ifndef _EXAHYPE_SOLVERS_FINITE_VOLUMES_SOLVER_H_
#define _EXAHYPE_SOLVERS_FINITE_VOLUMES_SOLVER_H_


#include "exahype/solvers/Solver.h"
#include "exahype/solvers/UserSolverInterface.h"

#include "exahype/records/FiniteVolumesCellDescription.h"

#include "exahype/Cell.h"
#include "exahype/Vertex.h"

namespace exahype {
namespace solvers {
class FiniteVolumesSolver;
}  // namespace solvers
}  // namespace exahype



/**
 * Abstract base class for one-step Finite Volumes solvers.
 */
class exahype::solvers::FiniteVolumesSolver : public exahype::solvers::Solver, public exahype::solvers::UserFiniteVolumesSolverInterface {
public:
  typedef exahype::DataHeap DataHeap;

  /**
   * Rank-local heap that stores FiniteVolumesCellDescription instances.
   *
   * \note This heap might be shared by multiple FiniteVolumesSolver instances
   * that differ in their solver number and other attributes.
   * @see solvers::Solver::RegisteredSolvers.
   */
  typedef exahype::records::FiniteVolumesCellDescription CellDescription;
  typedef peano::heap::RLEHeap<CellDescription> Heap;

private:
  /**
   * Log device.
   */
  static tarch::logging::Log _log;

  /**
   * Minimum time step size of all patches
   * in the previous iteration.
   */
  double _previousMinTimeStepSize;

  /**
   * Minimum time stamps of all patches.
   */
  double _minTimeStamp;

  /**
   * Minimum time step size of all patches.
   */
  double _minTimeStepSize;

  /**
   * Minimum stable time step size of all patches for
   * the next iteration.
   */
  double _minNextTimeStepSize;

  /**
   * Width of the ghost layer used for
   * reconstruction and Riemann solves.
   */
  int _ghostLayerWidth;

  /**
   * Total number of volume averages and ghost values in a patch.
   * This number does include ghost values.
   */
  int _dataPerPatch;

  /**
   * Total number of ghost values surrounding a patch.
   */
  int _ghostDataPerPatch;

  /**
   * Total number of volume averages per face of the patch.
   * This number does not include ghost values.
   */
  int _dataPerPatchFace;

  /**
   * Total number of volume averages per boundary of the patch.
   * This number does not include ghost values.
   */
  int _dataPerPatchBoundary;

  /**
   * Synchonises the cell description time stamps
   * and time step sizes with the solver ones
   * according to the time stepping mode that
   * is switched on.
   */
  void synchroniseTimeStepping(CellDescription& cellDescription);

#ifdef Parallel
  /**
   * Data messages per neighbour communication.
   * This information is required by the sendEmpty...(...)
   * method.
   */
  static const int DataMessagesPerNeighbourCommunication;
  /**
   * Data messages per fork/join communication.
   * This information is required by the sendEmpty...(...)
   * method.
   */
  static const int DataMessagesPerForkOrJoinCommunication;
  /**
   * Data messages per master worker communication.
   * This information is required by the sendEmpty...(...)
   * method.
   */
  static const int DataMessagesPerMasterWorkerCommunication;
#endif

  /**
   * Sets heap indices of all finite volumes cell descriptions that were
   * received due to a fork or join event to
   * multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex,
   * and the parent index of the cell descriptions to the specified \p
   * parentIndex.
   */
  static void resetDataHeapIndices(
      const int cellDescriptionsIndex,
      const int parentIndex);

  /**
   * Checks if the parent index of a fine grid cell description
   * was set to RemoteAdjacencyIndex during a previous forking event.
   *
   * If so, check if there exists a coarse grid cell description
   * which must have been also received during a previous fork event.
   * If so, update the parent index of the fine grid cell description
   * with the coarse grid cell descriptions index.
   */
  void ensureConsistencyOfParentIndex(
      CellDescription& cellDescription,
      const int coarseGridCellDescriptionsIndex,
      const int solverNumber);

public:
  /**
    * Returns the ADERDGCellDescription.
    */
   static Heap::HeapEntries& getCellDescriptions(
       const int cellDescriptionsIndex) {
     assertion1(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);

     return Heap::getInstance().getData(cellDescriptionsIndex);
   }

   /**
    * Returns the ADERDGCellDescription.
    */
   static CellDescription& getCellDescription(
       const int cellDescriptionsIndex,
       const int element) {
     assertion2(Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex,element);
     assertion2(element>=0,cellDescriptionsIndex,element);
     assertion2(static_cast<unsigned int>(element)<Heap::getInstance().getData(cellDescriptionsIndex).size(),cellDescriptionsIndex,element);

     return Heap::getInstance().getData(cellDescriptionsIndex)[element];
   }

   /**
    * Push a new cell description to the back
    * of the heap vector at \p cellDescriptionsIndex.
    *
    * \param TODO docu
    */
   static void addNewCellDescription(
       const int cellDescriptionsIndex,
       const int solverNumber,
       const CellDescription::Type cellType,
       const CellDescription::RefinementEvent refinementEvent,
       const int level,
       const int parentIndex,
       const tarch::la::Vector<DIMENSIONS, double>&  cellSize,
       const tarch::la::Vector<DIMENSIONS, double>&  cellOffset);


   /**
    * Returns if a ADERDGCellDescription type holds face data.
    */
   static bool holdsFaceData(const CellDescription::Type& cellDescriptionType) {
     return cellDescriptionType==CellDescription::Cell ||
            cellDescriptionType==CellDescription::Ancestor   ||
            cellDescriptionType==CellDescription::Descendant;
   }

  FiniteVolumesSolver(const std::string& identifier, int numberOfVariables,
      int numberOfParameters, int nodesPerCoordinateAxis, int ghostLayerWidth,
      double maximumMeshSize, int maximumAdaptiveMeshDepth,
      exahype::solvers::Solver::TimeStepping timeStepping,
      std::unique_ptr<profilers::Profiler> profiler =
          std::unique_ptr<profilers::Profiler>(
              new profilers::simple::NoOpProfiler("")));

  virtual ~FiniteVolumesSolver() {}

  // Disallow copy and assignment
  FiniteVolumesSolver(const FiniteVolumesSolver& other) = delete;
  FiniteVolumesSolver& operator=(const FiniteVolumesSolver& other) = delete;

  /**
   * \brief Returns a stable time step size.
   *
   * \param[in] luh             Cell-local solution DoF.
   * \param[in] tempEigenvalues A temporary array of size equalling the number of variables.
   * \param[in] cellSize        Extent of the cell in each coordinate direction.
   */
  virtual double stableTimeStepSize(
      const double* const luh,
      double* tempEigenvalues,
      const tarch::la::Vector<DIMENSIONS, double>& cellSize) = 0;

  /**
   * Extract volume averages belonging to the boundary layer
   * of the neighbour patch and store them in the ghost layer
   * of the current patch.
   *
   * Depending on the implementation (if reconstruction is applied),
   * the boundary layer/ghost layer might not just be a single layer.
   *
   * \param luhbnd Points to the extrapolated solution values.
   * \param luh Points to the the new solution values.
   * \param neighbourPosition Contains the relative position of the neighbour patch
   * with respect to the patch this method was invoked for. The entries of the vector are in the range
   * {-1,0,1}.
   *
   * \note The theoretical arithmetic intensity of this operation is zero.
   * \note This operation is invoked per vertex in touchVertexFirstTime and mergeWithNeighbour
   * in mapping Merging.
   *
   * <h2>MPI</h2>
   * No ghost layer is necessary if a patch is surrounded only
   * by local cells. However as soon as the cell is adjacent
   * to a MPI boundary this becomes necessary.
   * We thus always hold ghost layers.
   */
  virtual void ghostLayerFilling(
      double* luh,
      const double* luhNeighbour,
      const tarch::la::Vector<DIMENSIONS,int>& neighbourPosition) = 0;

  /**
   * Similar to ghostLayerFilling but we do not work with
   * complete patches from a local neighbour here but with smaller arrays received
   * from a remote neighbour or containing boundary conditions.
   *
   * \note The theoretical arithmetic intensity of this operation is zero.
   * \note This operation is invoked per vertex in mergeWithNeighbour in mapping Merging.
   */
  virtual void ghostLayerFillingAtBoundary(
      double* luh,
      const double* luhbnd,
      const tarch::la::Vector<DIMENSIONS,int>& boundaryPosition) = 0;

  /**
   * Extract boundary layers of \p luh before
   * sending them away via MPI.
   *
   * \note The theoretical arithmetic intensity of this operation is zero.
   * \note This operation is invoked per vertex in prepareSendToNeighbour in mapping Sending.
   */
  virtual void boundaryLayerExtraction(
      double* luhbnd,
      const double* luh,
      const tarch::la::Vector<DIMENSIONS,int>& boundaryPosition) = 0;

  /**
   * Return the state variables at the boundary.
   *
   * @param[inout] stateOut
   * @param[in]    stateIn
   * @param[in]    cellCentre    Cell centre.
   * @param[in]    cellSize      Cell size.
   * @param[in]    t             The time.
   * @param[in]    dt            A time step size.
   * @param[in]    normalNonZero Index of the nonzero normal vector component,
   *i.e., 0 for e_x, 1 for e_y, and 2 for e_z.
   */
  virtual void boundaryConditions(double* stateOut,
      const double* const stateIn,
      const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS,double>& cellSize,
      const double t,const double dt,
      const int faceIndex,
      const int normalNonZero) = 0;

  /**
   * Compute the Riemann problem.
   * 
   * This function shall implement a pointwise riemann Solver, in contrast to the ADERDGSolver::riemannSolver
   * function which implements a patch-wise riemann solver.
   * 
   * In a fully conservative scheme, it is fL = fR and the Riemann solver really computes the fluxes
   * in normalNonzero direction steming from the contribution of qL and qR.
   * 
   * \param[out]   fL      the fluxes on the left side of the point cell (already allocated)
   * \param[out]   fR      the fluxes on the right side of the point cell (already allocated).
   * \param[in]    qL      the state vector in the left neighbour cell
   * \param[in]    qR      the state vector in the right neighbour cell
   * \param[in]    normalNonZero  Index of the nonzero normal vector component.
   **/
  virtual double riemannSolver(double* fL, double *fR, const double* qL, const double* qR, int normalNonZero) = 0;

  virtual void solutionUpdate(
      double* luhNew,const double* luh,
      double** tempStateSizedArrays,double** tempUnknowns,
      const tarch::la::Vector<DIMENSIONS, double>& dx,
      const double dt, double& maxAdmissibleDt) = 0;

  /**
   * Adjust the conserved variables and parameters (together: Q) at a given time t at the (quadrature) point x.
   *
   * \note Use this function and ::useAdjustSolution to set initial conditions.
   *
   * \param[in]    x         the physical coordinate on the face.
   * \param[in]    w         (deprecated) the quadrature weight corresponding to the quadrature point w.
   * \param[in]    t         the start of the time interval.
   * \param[in]    dt        the width of the time interval.
   * \param[inout] Q         the conserved variables (and parameters) associated with a quadrature point
   *                         as C array (already allocated).
   */
  virtual void adjustSolution(
      double* luh, const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS, double>& dx,
      const double t,
      const double dt) = 0;


  /**
   * Check if we need to adjust the conserved variables and parameters (together: Q) in a cell
   * within the time interval [t,t+dt].
   *
   * \note Use this function and ::adjustSolution to set initial conditions.
   *
   * \param[in]    center    The center of the cell.
   * \param[in]    dx        The extent of the cell.
   * \param[in]    t         the start of the time interval.
   * \param[in]    dt        the width of the time interval.
   * \return true if the solution has to be adjusted.
   */
  virtual bool useAdjustSolution(
      const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS, double>& dx,
      const double t,
      const double dt) const = 0;

  /**
   * Pointwise solution adjustment.
   * 
   * In the FV solver, we currently don't support both patchwise
   * and pointwise adjustment @TODO.
   * 
   * \param[in]   x   The position (array with DIMENSIONS entries)
   * \param[in]   w   (deprecated) the quadrature weight.
   * \param[in]   t   the start of the time interval
   * \param[in]   dt  the width of the time interval.
   * \param[inout] Q  the conserved variables and parameters as C array (already allocated).
   * 
   **/
  virtual void adjustSolution(const double* const x,const double w,const double t,const double dt, double* Q) = 0;

  
  /**
   * @defgroup AMR Solver routines for adaptive mesh refinement
   */
  ///@{
  /**
   * The refinement criterion that must be defined by the user.
   *
   */
  // @todo: 16/04/06:Dominic Etienne Charrier Consider to correct the level in
  // the invoking code, i.e., level-> level-1
  // since this is was the user expects.
  virtual exahype::solvers::Solver::RefinementControl refinementCriterion(
      const double* luh, const tarch::la::Vector<DIMENSIONS, double>& cellCentre,
      const tarch::la::Vector<DIMENSIONS, double>& cellSize,
      const double time,
      const int level) = 0;

  /**
   * Returns the min time step size of the
   * previous iteration.
   * This value is initialised with zero
   * to enable an initial "rollback".
   */
  double getPreviousMinTimeStepSize() const;

  double getMinTimeStamp() const override;

  /**
   * The number of unknowns per patch.
   * This number does not include ghost layer values.
   */
  int getUnknownsPerPatch() const; // TODO(Dominic): Rename

  /**
   * Get the width of the ghost layer of the patch.
   */
  int getGhostLayerWidth() const;

  /**
   * Get the total number of ghost values per patch.
   */
  int getGhostValuesPerPatch() const; // TODO(Dominic): Rename

  /**
   * This operation returns the number of unknowns per
   * face of a patch.
   *
   * This number does not include ghost values.
   */
  int getUnknownsPerFace() const; // TODO(Dominic): Rename


  virtual int getTempUnknownsSize()              const {return getUnknownsPerPatch();} // TODO function should be renamed
  virtual int getBndFaceSize()                   const {return getUnknownsPerFace();} // TODO function should be renamed
  virtual int getTempStateSizedVectorsSize()     const {return getNumberOfVariables()+getNumberOfParameters();} //dataPoints // TODO function should be renamed

  /**
   * Run over all solvers and identify the minimal time step size.
   */
  double getMinTimeStepSize() const override;

  void updateMinNextTimeStepSize( double value ) override;

  void setMinTimeStepSize(double value) { // TODO(Dominic): Hack
    _minTimeStepSize = value;
  }

  void setMinTimeStamp(double value) { // TODO(Dominic): Hack
    _minTimeStamp = value;
  }

  void setMinNextTimeStepSize(double value) { // TODO(Dominic): Hack
      _minNextTimeStepSize = value;
  }

  void setPreviousMinTimeStepSize(double value) { // TODO(Dominic): Hack
    _previousMinTimeStepSize = value;
  }

  void initSolver(
      const double timeStamp,
      const tarch::la::Vector<DIMENSIONS,double>& domainOffset,
      const tarch::la::Vector<DIMENSIONS,double>& domainSize) override;

  bool isSending(const exahype::records::State::AlgorithmSection& section) const override;

  bool isComputing(const exahype::records::State::AlgorithmSection& section) const override;

  void synchroniseTimeStepping(
          const int cellDescriptionsIndex,
          const int element) override;

  void startNewTimeStep() override;

  void zeroTimeStepSizes() override;

  /**
   * Roll back the time step data to the
   * ones of the previous time step.
   */
  void rollbackToPreviousTimeStep();

  void reinitialiseTimeStepData() override;

  double getMinNextTimeStepSize() const override;

  bool isValidCellDescriptionIndex(
      const int cellDescriptionsIndex) const override {
    return Heap::getInstance().isValidIndex(cellDescriptionsIndex);
  }

  int tryGetElement(
      const int cellDescriptionsIndex,
      const int solverNumber) const override;

  SubcellPosition computeSubcellPositionOfCellOrAncestor(
        const int cellDescriptionsIndex,
        const int element) override;

  ///////////////////////////////////
  // MODIFY CELL DESCRIPTION
  ///////////////////////////////////
  /**
   * Initialise cell description of type Cell.
   * Initialise the refinement event with None.
   */
  void addNewCell(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      const int coarseGridCellDescriptionsIndex,
      const int solverNumber);

  /**
   * Checks if no unnecessary memory is allocated for the cell description.
   * If this is not the case, it deallocates the unnecessarily allocated memory.
   */
  void ensureNoUnnecessaryMemoryIsAllocated(CellDescription& cellDescription);

  /**
   * Checks if all the necessary memory is allocated for the cell description.
   * If this is not the case, it allocates the necessary
   * memory for the cell description.
   */
  void ensureNecessaryMemoryIsAllocated(CellDescription& cellDescription);


  bool markForRefinement(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const bool initialGrid,
      const int solverNumber) override;

  bool updateStateInEnterCell(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const bool initialGrid,
      const int solverNumber) override;

  bool updateStateInLeaveCell(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const int solverNumber) override;

  bool attainedStableState(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      const int solverNumber) const override;

  void finaliseStateUpdates(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const int solverNumber) override;

  ///////////////////////////////////
  // CELL-LOCAL
  //////////////////////////////////
  bool evaluateRefinementCriterionAfterSolutionUpdate(
        const int cellDescriptionsIndex,
        const int element) override;

  double startNewTimeStep(
      const int cellDescriptionsIndex,
      const int element,
      double*   tempEigenvalues) override;

  void zeroTimeStepSizes(const int cellDescriptionsIndex, const int solverElement) override;

  /**
   * Rolls the solver time step data back to the
   * previous time step for a cell description.
   * Note that the newest time step
   * data is lost in this process.
   */
  void rollbackToPreviousTimeStep(
      const int cellDescriptionsIndex,
      const int element);

  void setInitialConditions(
      const int cellDescriptionsIndex,
      const int element,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) override;

  void updateSolution(
      const int cellDescriptionsIndex,
      const int element,
      double** tempStateSizedArrays,
      double** tempUnknowns,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) override;

  /**
   * Rolls back the solver's solution on the
   * particular cell description.
   * This method is used by the ADER-DG a-posteriori
   * subcell limiter (LimitingADERDGSolver).
   *
   * <h2>Open issues</h2>
   * A rollback is of course not possible if we have adjusted the solution
   * values. Assuming the rollback is invoked by a LimitingADERDGSolver,
   * we should use the adjusted FVM solution as reference solution.
   * A similar issue occurs if we impose initial conditions that
   * include a discontinuity.
   */
  void rollbackSolution(
      const int cellDescriptionsIndex,
      const int element,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator);

  void swapSolutionAndPreviousSolution(
      const int cellDescriptionsIndex,
      const int element) const;

  void preProcess(
      const int cellDescriptionsIndex,
      const int element) override;

  void postProcess(
      const int cellDescriptionsIndex,
      const int element) override;

  void prolongateDataAndPrepareDataRestriction(
      const int cellDescriptionsIndex,
      const int element) override;

  void restrictToNextParent(
        const int fineGridCellDescriptionsIndex,
        const int fineGridElement,
        const int coarseGridCellDescriptionsIndex,
        const int coarseGridElement) override;

  void restrictToTopMostParent(
      const int cellDescriptionsIndex,
      const int element,
      const int parentCellDescriptionsIndex,
      const int parentElement,
      const tarch::la::Vector<DIMENSIONS,int>& subcellIndex) override;


  ///////////////////////////////////
  // NEIGHBOUR
  ///////////////////////////////////
  void mergeNeighboursMetadata(
      const int                                 cellDescriptionsIndex1,
      const int                                 element1,
      const int                                 cellDescriptionsIndex2,
      const int                                 element2,
      const tarch::la::Vector<DIMENSIONS, int>& pos1,
      const tarch::la::Vector<DIMENSIONS, int>& pos2) override;

  void mergeNeighbours(
      const int                                 cellDescriptionsIndex1,
      const int                                 element1,
      const int                                 cellDescriptionsIndex2,
      const int                                 element2,
      const tarch::la::Vector<DIMENSIONS, int>& pos1,
      const tarch::la::Vector<DIMENSIONS, int>& pos2,
      double**                                  tempFaceUnknowns,
      double**                                  tempStateSizedVectors,
      double**                                  tempStateSizedSquareMatrices) override;

  void mergeWithBoundaryData(
      const int                                 cellDescriptionsIndex,
      const int                                 element,
      const tarch::la::Vector<DIMENSIONS, int>& posCell,
      const tarch::la::Vector<DIMENSIONS, int>& posBoundary,
      double**                                  tempFaceUnknowns,
      double**                                  tempStateSizedVectors,
      double**                                  tempStateSizedSquareMatrices) override;

  void mergeWithBoundaryOrEmptyCellMetadata(
      const int cellDescriptionsIndex,
      const int element,
      const tarch::la::Vector<DIMENSIONS, int>& posCell,
      const tarch::la::Vector<DIMENSIONS, int>& posBoundaryOrEmptyCell) override;

  void prepareNextNeighbourMerging(
        const int cellDescriptionsIndex,const int element,
        exahype::Vertex* const fineGridVertices,
        const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) const override;
#ifdef Parallel
  ///////////////////////////////////
  // MASTER<=>WORKER
  ///////////////////////////////////
  void appendMasterWorkerCommunicationMetadata(
      exahype::MetadataHeap::HeapEntries& metadata,
      const int cellDescriptionsIndex,
      const int solverNumber) override;

  void mergeWithMasterWorkerMetadata(
        const MetadataHeap::HeapEntries& receivedMetadata,
        const int                        cellDescriptionsIndex,
        const int                        element) override;

  ///////////////////////////////////
  // FORK OR JOIN
  ///////////////////////////////////
  /**
   * Send all ADER-DG cell descriptions to rank
   * \p toRank.
   */
  static void sendCellDescriptions(
      const int                                     toRank,
      const int                                     cellDescriptionsIndex,
      const peano::heap::MessageType&               messageType,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level);

  /**
   * Erase all cell descriptions of type \p Cell.
   *
   * TODO(Dominic): It might be possible to delete all cell
   * descriptions. Discuss with Tobias.
   */
  static void eraseCellDescriptions(const int cellDescriptionsIndex);

  /**
   * Send an empty message to rank
   * \p toRank.
   */
  static void sendEmptyCellDescriptions(
      const int                                     toRank,
      const peano::heap::MessageType&               messageType,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level);

  /**
   * Receives cell descriptions from rank \p fromRank
   * and resets the data heap indices to -1.
   *
   * If a received cell description has the same
   * solver number as a cell description in the
   * array at address \p cellDescriptionsIndex,
   * we merge the metadata (time stamps, time step size)
   * of both cell descriptions.
   *
   * If no cell description in the array at address
   * \p cellDescriptionsIndex can be found with the
   * same solver number than a received cell description,
   * we push the received cell description to
   * the back of the array at address \p cellDescriptions
   * Index.
   *
   * This operation is intended to be used in combination
   * with the solver method mergeWithWorkerOrMasterDataDueToForkOrJoin(...).
   * Here, we would merge first the cell descriptions sent by the master and worker
   * and then merge the data that is sent out right after.
   */
  static void mergeCellDescriptionsWithRemoteData(
      const int                                     fromRank,
      exahype::Cell&                                localCell,
      const peano::heap::MessageType&               messageType,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level);

  /**
   * Drop cell descriptions received from \p fromRank.
   */
  static void dropCellDescriptions(
      const int                                     fromRank,
      const peano::heap::MessageType&               messageType,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level);

  ///////////////////////////////////
  // NEIGHBOUR
  ///////////////////////////////////
  void appendNeighbourCommunicationMetadata(
      exahype::MetadataHeap::HeapEntries& metadata,
      const tarch::la::Vector<DIMENSIONS,int>& src,
      const tarch::la::Vector<DIMENSIONS,int>& dest,
      const int cellDescriptionsIndex,
      const int solverNumber) override;

  void mergeWithNeighbourMetadata(
      const exahype::MetadataHeap::HeapEntries& metadata,
      const tarch::la::Vector<DIMENSIONS, int>& src,
      const tarch::la::Vector<DIMENSIONS, int>& dest,
      const int cellDescriptionsIndex,
      const int element) override;

  void sendDataToNeighbour(
      const int                                     toRank,
      const int                                     cellDescriptionsIndex,
      const int                                     elementIndex,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void sendEmptyDataToNeighbour(
      const int                                     toRank,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void mergeWithNeighbourData(
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
      const int                                    level) override;


  void dropNeighbourData(
      const int                                     fromRank,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void sendDataToWorkerOrMasterDueToForkOrJoin(
      const int                                     toRank,
      const int                                     cellDescriptionsIndex,
      const int                                     element,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void sendEmptyDataToWorkerOrMasterDueToForkOrJoin(
      const int                                     toRank,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void mergeWithWorkerOrMasterDataDueToForkOrJoin(
      const int                                     fromRank,
      const int                                     cellDescriptionsIndex,
      const int                                     element,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void dropWorkerOrMasterDataDueToForkOrJoin(
      const int                                     fromRank,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  ///////////////////////////////////
  // WORKER->MASTER
  ///////////////////////////////////
  bool hasToSendDataToMaster(
        const int cellDescriptionsIndex,
        const int element) override;

  void sendDataToMaster(
      const int                                    masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) override;

  void mergeWithWorkerData(
      const int                                    workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) override;

  void sendDataToMaster(
      const int                                     masterRank,
      const int                                     cellDescriptionsIndex,
      const int                                     element,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void sendEmptyDataToMaster(
      const int                                     masterRank,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void mergeWithWorkerData(
      const int                                    workerRank,
      const exahype::MetadataHeap::HeapEntries&    workerMetadata,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) override;

  void dropWorkerData(
      const int                                     workerRank,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  ///////////////////////////////////
  // MASTER->WORKER
  ///////////////////////////////////
  void sendDataToWorker(
      const                                        int workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) override;

  void mergeWithMasterData(
      const                                        int masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) override;

  void sendDataToWorker(
      const int                                     workerRank,
      const int                                     cellDescriptionsIndex,
      const int                                     element,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void sendEmptyDataToWorker(
      const int                                     workerRank,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void mergeWithMasterData(
      const int                                     masterRank,
      const exahype::MetadataHeap::HeapEntries&     masterMetadata,
      const int                                     cellDescriptionsIndex,
      const int                                     element,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void dropMasterData(
      const int                                     masterRank,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;
#endif

  void validateNoNansInFiniteVolumesSolution(CellDescription& cellDescription,const int cellDescriptionsIndex,const char* methodTrace) const;

  void printFiniteVolumesSolution(CellDescription& cellDescription) const;

  void printFiniteVolumesBoundaryLayer(const double* luhbnd)  const;

  std::string toString() const override;

  void toString (std::ostream& out) const override;
};

#endif
