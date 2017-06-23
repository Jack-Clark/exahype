/*
 * LimitingADERDGSolver.h
 *
 *  Created on: 26 Oct 2016
 *      Author: dominic
 */

#ifndef LIMITEDADERDGSOLVER_H_
#define LIMITEDADERDGSOLVER_H_

#include "exahype/solvers/ADERDGSolver.h"
#include "exahype/solvers/FiniteVolumesSolver.h"

#include "tarch/multicore/BooleanSemaphore.h"

#include "exahype/plotters/Plotter.h"

#include "exahype/profilers/simple/NoOpProfiler.h"

#include "exahype/solvers/TemporaryVariables.h"

namespace exahype {
namespace solvers {

/**
 * A solver that combines high-order ADER-DG
 * with a more robust Finite Volumes solver in areas
 * where shocks are present.
 *
 * The ADER-DG solver is regarded as the main solver
 * that deals with time step sizes, mesh update requests etc.
 *
 * <h1>Algorithm sections</h1>
 * The solver might be active in one of the
 * following algorithm sections.
 *
 * <h2>Mesh refinement</h2>
 * TODO
 *
 * <h2>Local recomputation</h2>
 * TODO
 *
 * <h2>Global recomputation</h2>
 * The solver is redoing the last ADER-DG time
 * step completely but performs some mesh
 * refinement beforehand.
 *
 * More precisely, the solver will fist perform a rollback to
 * the previous time step> It will then perform mesh refinement
 * until a troubled compute cell and all its compute cell
 * neighours are placed on the finest mesh level.
 * Next it will perform the computation of a new
 * time step size and then, the computation of
 * a new space-time predictor.
 * Afterwards a solution update in all cells is performed.
 * Now, the solution of the cells has evolved
 * to the anticipated time step.
 * Lastly, a recomputation of the predictor is performed.
 *
 * The following scenario causes the solver
 * to switch to this algorithmic section:
 *
 * Scenario 1:
 * A compute cell was marked as troubled on a
 * mesh level coarser than the finest one.
 *
 * Scenario 2: A cell of type Descendant/EmptyDescendant
 * was marked with a LimiterStatus other than Ok.
 */
class LimitingADERDGSolver;

} /* namespace solvers */
} /* namespace exahype */

class exahype::solvers::LimitingADERDGSolver : public exahype::solvers::Solver {

private:
  typedef exahype::records::ADERDGCellDescription SolverPatch;
  typedef peano::heap::RLEHeap<SolverPatch> SolverHeap;

  typedef exahype::records::FiniteVolumesCellDescription LimiterPatch;
  typedef peano::heap::RLEHeap<LimiterPatch> LimiterHeap;


  /**
   * Log device.
   */
  static tarch::logging::Log _log;

  /**
   * The ADERDG solver.
   */
  std::unique_ptr<exahype::solvers::ADERDGSolver> _solver;

  /**
   * The finite volumes solver used for the a posteriori subcell limiting.
   */
  std::unique_ptr<exahype::solvers::FiniteVolumesSolver> _limiter;

  /**
   * A flag indicating that the limiter domain has changed.
   * This might be the case if either a cell has been
   * newly marked as troubled or healed.
   */
  exahype::solvers::LimiterDomainChange _limiterDomainChange;

  /**
   * The limiterDomainHasChanged for the next
   * iteration.
   */
  exahype::solvers::LimiterDomainChange _nextLimiterDomainChange;

  /**
   * The maximum relaxation parameter
   * used for the discrete maximum principle.
   */
  const double _DMPMaximumRelaxationParameter;

  /**
   * The difference scaling
   * used for the discrete maximum principle.
   */
  const double _DMPDifferenceScaling;

  /**
   * A counter holding the number of iterations to
   * cure a troubled cell.
   * This counter will be initialised to a certain
   * (user-dependent?) value if a cell is flagged as troubled.
   *
   * If the cell is not troubled for one iteration, the counter is
   * decreased until it reaches 0. Then, the
   * cell is considered as cured.
   * Note that the counter can be reset to the maximum value
   * in the meantime if the cell is marked again as troubled.
   *
   * This counter prevents that a cell is toggling between
   * troubled and Ok (cured).
   */
  int _iterationsToCureTroubledCell;

  /**
   * TODO(Dominc): Remove after docu is recycled.
   *
   * This operation sets the solutions' minimum and maximum value on a cell.
   * The routine is to be invoked after the code has determined the new minimum
   * and maximum value within a cell. In turn, it evaluates whether the new
   * minimum and maximum value have decreased or grown, respectively.
   *
   * If the new min/max values indicate that the new solution comprises
   * oscillations, the routine returns false. This is an indicator that the
   * solution should be limited.
   *
   * If the new min/max values fit, the routine returns true.
   *
   * <h2>Implementation</h2>
   * We hold the min/max information exclusively on the faces. The first thing
   * the routine does is to project the min/max values into the cell. For this
   * it evaluates the 2d faces. The projected value then is compared to the
   * arguments. Once the results of the operation is determined, the routine
   * writes the new arguments onto the 2d face entries. This, on the one hand,
   * stores the data for the subsequent time step, but it also propagates the
   * min/max information into the face-connected neighbours.
   *
   * @param  min          New minimum values within the cell. Array of length
   *                      _numberOfUnknowns.
   * @param  max          New maximum values within the cell
   * @param  solverIndex  Number of the solver within the cell. Please ensure
   *                      that solverIndex refers to an ADER-DG solver.
   * @return True if the new min and max values fit into the restricted min
   *   max solutions. Return false if we seem to run into oscillations.
   */
  //  void setSolutionMinMax(double* min, double* max) const;

  /**
   * Merge the solution min and max values on a face between two cell
   * descriptions. Signature is similar to that of the solver of a Riemann problem.
   */
  void mergeSolutionMinMaxOnFace(
      SolverPatch& pLeft,
      SolverPatch& pRight,
      const int faceIndexLeft,
      const int faceIndexRight) const;

  /**
   * Checks if the updated solution
   * of the ADER-DG solver contains unphysical oscillations (false)
   * or not (true).
   *
   * Compute the new min and max at the same time.
   */
  bool evaluateDiscreteMaximumPrincipleAndDetermineMinAndMax(SolverPatch& solverPatch);

  /**
   * Checks if the updated solution
   * of the ADER-DG solver contains unphysical oscillations (false)
   * or not (true).
   */
  bool evaluateDiscreteMaximumPrinciple(SolverPatch& solverPatch);

  /**
   * Checks if the updated solution
   * of the ADER-DG solver is
   * a physically admissible one (true).
   */
  bool evaluatePhysicalAdmissibilityCriterion(SolverPatch& solverPatch);

  /**
   * Computes the cell-local minimum and maximum
   * of the solution (per variable)
   * and makes them accessible per face.
   */
  void determineSolverMinAndMax(SolverPatch& solverPatch);

  /**
   * Computes the cell-local minimum and maximum
   * of the limiter's solution (per variable)
   * and makes them accessible per face.
   *
   * This method is used for troubled cells that
   * do not hold a valid ADER-DG solution,
   * as well as their neighbours.
   */
  void determineLimiterMinAndMax(SolverPatch& solverPatch,LimiterPatch& limiterPatch);

  /**
   * Updates the merged limiter status based on the cell-local ADER-DG solution
   * values,
   */
  void updateLimiterStatusAfterSolutionUpdate(SolverPatch& solverPatch,const bool isTroubled);

  /**
   * Deallocates a limiter patch.
   *
   * \note Thread-safe.
   */
  void deallocateLimiterPatch(
      const int cellDescriptionsIndex,
      const int solverElement) const;

  /**
   * Allocates a new limiter patch.
   *
   * \return The index of the patch in the heap
   * vector at address \p cellDescriptionsIndex.
   *
   * \note Thread-safe.
   */
  int allocateLimiterPatch(
          const int cellDescriptionsIndex,
          const int solverElement) const;

  /**
   * Deallocates the limiter patch for solver patches
   * that of type Cell and flagged with limiter status
   * Ok.
   *
   * \note This operation should never be performed during the mesh refinement
   * iterations since then a limiter patch holding a valid FV solution
   * might be removed in one of the first iteration but is
   * then required after the limiter status spreading has converged to
   * perform a troubled cell recomputation.
   */
  void ensureNoUnrequiredLimiterPatchIsAllocatedOnComputeCell(
      const int cellDescriptionsIndex,
      const int solverElement) const;

  /**
   * Allocates a limiter patch and performs a DG to FV projection
   */
  void allocateLimiterPatchAfterSolutionUpdate(
      const int cellDescriptionsIndex,const int solverElement) const;

  /**
   * Update the limiter status based on the cell-local solution values.
   *
   * If the new limiter status is changed to or remains troubled,
   * set the iterationsToCureTroubledCell counter to a certain
   * maximum value.
   * If the limiter status changes from troubled to something else,
   * decrease the iterationsToCureTroubledCell counter.
   * If the counter is set to zero, change a troubled cell
   * to NeighbourOfCellIsTroubled1.
   *
   * Further write back the new value to the boundary.
   *
   * \param[in] isTroubled A bool indicating if the patch's solution is (still) troubled
   *
   * \return True if the limiter domain changes irregularly in the cell, i.e.,
   * if a patch with status Ok, NeighbourOfTroubled3, NeighbourOfTroubled4
   * changes its status to Troubled.
   *
   * If the limiter status changes regularly, i.e., from NeighbourOfTroubled1
   * to Troubled or from Troubled to NeighbourOfTroubled3, NeighbourOfTroubled4, this
   * methods returns false.
   */
  LimiterDomainChange determineLimiterStatusAfterSolutionUpdate(
      SolverPatch& solverPatch,const bool isTroubled) const;

  /**
   * Takes the FV solution from the limiter patch and projects it on the
   * DG space, overwrites the DG solution on the solver patch with the projected values.
   */
  void projectFVSolutionOnDGSpace(SolverPatch& solverPatch,LimiterPatch& limiterPatch) const;

  /**
   * Takes the DG solution from the solver patch and projects it on the
   * FV space, overwrites the FV solution on the limiter patch with the projected values.
   */
  void projectDGSolutionOnFVSpace(SolverPatch& solverPatch,LimiterPatch& limiterPatch) const;

  /**
   * Vetoes an erasing request if the cell is within
   * or right next to a region which is refined according to
   * the limiter status.
   */
  void vetoErasingChildrenRequestBasedOnLimiterStatus(
      const int fineGridCellDescriptionsIndex,
      const int fineGridSolverElement,
      const int coarseGridCellDescriptionsIndex) const;

  /**
   * Depending on the finest adaptive mesh level and the given level,
   * compute the minimum limiter status for which we need to refine
   * a cell.
   */
  int computeMinimumLimiterStatusForRefinement(int level) const;

#ifdef Parallel
  /**
   * Data messages per neighbour communication.
   * This information is required by the sendEmpty...(...)
   * methods.
   */
  static const int DataMessagesPerNeighbourCommunication;
  /**
   * Data messages per fork/join communication.
   * This information is required by the sendEmpty...(...)
   * methods.
   */
  static const int DataMessagesPerForkOrJoinCommunication;
  /**
   * Data messages per master worker communication.
   * This information is required by the sendEmpty...(...)
   * methods.
   */
  static const int DataMessagesPerMasterWorkerCommunication;

  /**
   * Send the solution minimum and maximum values per variable
   * and further the merged limiter status of the solver patch
   * \p element in heap array \p cellDescriptionsIndex to the
   * neighbour.
   *
   * \see exahype::solvers::Solver::sendDataToNeighbour
   * for a description of the parameters.
   */
  void sendMinAndMaxToNeighbour(
      const int                                    toRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) const;

  /**
   * Receive the solution minimum and maximum values per variable
   * and further the merged limiter status for the solver patch
   * \p element in heap array \p cellDescriptionsIndex from the
   * neighbour.
   *
   * \see exahype::solvers::Solver::sendDataToNeighbour
   * for a description of the parameters.
   */
  void mergeWithNeighbourMinAndMax(
      const int                                    fromRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) const;

  /**
   * Single-sided variant of mergeSolutionMinMaxOnFace() that is required
   * for MPI where min and max value are explicitly exchanged through messages.
   */
  void mergeSolutionMinMaxOnFace(
      SolverPatch&  solverPatch,
      const int     faceIndex,
      const double* const min, const double* const  max) const;

#endif

public:
  /*
   * Check if a solver requested limiter status spreading.
   * Such a request might stem from a limiting ADERDGSolver which
   * has requested mesh refinement or a local
   * or global recomputation.
   *
   * TODO(Dominic): We should distinguish between mesh update requests
   * stemming from the limiter status based refinement criterion and
   * and those stemming from the user's refinement criterion.
   */
  static bool oneSolverRequestedLimiterStatusSpreading();

  /*
   * Check if a solver requested either local or global
   * recomputation.
   */
  static bool oneSolverRequestedLocalOrGlobalRecomputation();

  /*
   * Check if a solver requested local recomputation
   * recomputation.
   */
  static bool oneSolverRequestedLocalRecomputation();

  /*
   * Check if a solver requested either global
   * recomputation.
   */
  static bool oneSolverRequestedGlobalRecomputation();

  /**
   * Create a limiting ADER-DG solver.
   *
   * <h2>Discrete maximum principle</h2>
   * By default this constructor initialises the maximum relaxation
   * parameter to the value to \f$ \delta_0 = 1\cdot 10^{-4} \f$
   * and the difference scaling parameter to \f$ \epsilon = 1\cdot 10^{-3} \f$.
   * See Dumbser et al., 2014. doi:10.1016/j.jcp.2014.08.009 for more details on
   * the notation.
   */
  LimitingADERDGSolver(
      const std::string& identifier,
      std::unique_ptr<exahype::solvers::ADERDGSolver> solver,
      std::unique_ptr<exahype::solvers::FiniteVolumesSolver> limiter,
      const double DMPRelaxationParameter=1e-4,
      const double DMPDifferenceScaling=1e-3,
      const int iterationsToCureTroubledCell=2
      );

  virtual ~LimitingADERDGSolver() {
    _solver.reset();
    _limiter.reset();
  }

  // Disallow copy and assignment
  LimitingADERDGSolver(const ADERDGSolver& other) = delete;
  LimitingADERDGSolver& operator=(const ADERDGSolver& other) = delete;

  /**
   * Wire through to the ADER-DG solver.
   */
  void updateNextMeshUpdateRequest(const bool& meshUpdateRequest) override;
  bool getNextMeshUpdateRequest() const override;
  bool getMeshUpdateRequest() const override;
  void setNextMeshUpdateRequest() override;

  /**
   * Wire through to the ADER-DG solver.
   */
  void updateNextAttainedStableState(const bool& attainedStableState) override;
  bool getNextAttainedStableState() const override;
  bool getAttainedStableState() const override;
  void setNextAttainedStableState() override;

  /*
   * A time stamp minimised over all the ADERDG and FV solver
   * patches.
   */
  double getMinTimeStamp() const override;

  /**
   * Run over all solvers and identify the minimal time step size.
   */
  double getMinTimeStepSize() const override;

  double getMinNextTimeStepSize() const override;

  void updateMinNextTimeStepSize(double value) override;

  /**
   * \see ::exahype::solvers::Solver::initSolver
   *
   * Additionally, set the
   * ::_limiterDomainChangedIrregularly flag to true
   * since the limiter mappings all check this flag
   * in order to distinguish between solvers in a multisolver
   * run.
   */
  void initSolver(
        const double timeStamp,
        const tarch::la::Vector<DIMENSIONS,double>& domainOffset,
        const tarch::la::Vector<DIMENSIONS,double>& domainSize) override;

  bool isSending(const exahype::records::State::AlgorithmSection& section) const override;

  bool isComputing(const exahype::records::State::AlgorithmSection& section) const override;

  void synchroniseTimeStepping(
          const int cellDescriptionsIndex,
          const int element) override;

  void reconstructStandardTimeSteppingData() {
    _solver->reconstructStandardTimeSteppingData();
    _limiter->setMinTimeStamp(_solver->getMinCorrectorTimeStamp());
    _limiter->setMinTimeStepSize(_solver->getMinCorrectorTimeStepSize());
  }

  /**
   * We always override the limiter time step
   * data by the ADER-DG one before a solution update.
   */
  void startNewTimeStep() override;

  void zeroTimeStepSizes() override;

  /**
   * TODO(Dominic): Add docu.
   */
  LimiterDomainChange getNextLimiterDomainChange() const;
  /**
   * TODO(Dominic): Add docu.
   */
  void updateNextLimiterDomainChange(LimiterDomainChange limiterDomainChange);
  /**
   * TODO(Dominic): Add docu.
   * Can also be used to reset the _nextLimiterDomainChange
   * state to Regular.
   */
  void setNextLimiterDomainChange();
  /**
   * TODO(Dominic): Add docu.
   */
  LimiterDomainChange getLimiterDomainChange() const;

  /**
   * Roll back the time step data to the
   * ones of the previous time step.
   */
  void rollbackToPreviousTimeStep();

  void reconstructStandardTimeSteppingDataAfterRollback();

  void reinitialiseTimeStepData() override;

  void updateNextMinCellSize(double minCellSize) override;
  void updateNextMaxCellSize(double maxCellSize) override;
  double getNextMinCellSize() const override;
  double getNextMaxCellSize() const override;
  double getMinCellSize() const override;
  double getMaxCellSize() const override;

  bool isValidCellDescriptionIndex(
      const int cellDescriptionsIndex) const override ;

  /**
   * Returns the index of the solver patch registered for the solver with
   * index \p solverNumber in exahype::solvers::RegisteredSolvers.
   * If no patch is registered for the solver, this function
   * returns exahype::solvers::Solver::NotFound.
   */
  int tryGetElement(
      const int cellDescriptionsIndex,
      const int solverNumber) const override {
    return _solver->tryGetElement(cellDescriptionsIndex,solverNumber);
  }

  /**
   * Returns the index of the limiter patch registered for the solver with
   * index \p solverNumber in exahype::solvers::RegisteredSolvers.
   * If no patch is registered for the solver, this function
   * returns exahype::solvers::Solver::NotFound.
   */
  int tryGetLimiterElement(
      const int cellDescriptionsIndex,
      const int solverNumber) const {
    return _limiter->tryGetElement(cellDescriptionsIndex,solverNumber);
  }

  /**
   * Returns the index of the limiter patch corresponding to
   * the solver patch with index \p solverElement.
   * Both patches link to the same ::LimitingADERDGSolver.
   * If no limiter patch is found, this function
   * returns exahype::solvers::Solver::NotFound.
   */
  int tryGetLimiterElementFromSolverElement(
      const int cellDescriptionsIndex,
      const int solverElement) const {
    SolverPatch& solverPatch = _solver->getCellDescription(cellDescriptionsIndex,solverElement);
    return _limiter->tryGetElement(cellDescriptionsIndex,solverPatch.getSolverNumber());
  }

  /**
    * \see exahype::amr::computeSubcellPositionOfCellOrAncestor
    */
  SubcellPosition computeSubcellPositionOfCellOrAncestor(
      const int cellDescriptionsIndex,
      const int element) override;

  ///////////////////////////////////
  // MODIFY CELL DESCRIPTION
  ///////////////////////////////////

  /**
   * \return true in case a cell on a coarser mesh level is marked as
   * Troubled or in case a cell on a coarser
   * mesh level was marked with a limiter status other than troubled
   * and for the given refinement level, it is required to refine this cell.
   * Otherwise return false.
   *
   * In order to ensure that all four helper cells around the actual troubled cell
   * fit on the finest mesh level, we need to refine additional cells around a troubled
   * cell. The number of additionally refined cells around a troubled cells depends
   * here on the difference in levels to the finest mesh level.
   *
   * At a sufficent distance to the finest level, the minimum set of cells that needs to be refined around a troubled cell
   * are their 3^d neighbours. However since our limiter status flagging
   * only considers direct (face) neighbours, we need to refine all cells with
   * a limiter status Troubled-1 and Troubled-2.
   */
  bool evaluateLimiterStatusRefinementCriterion(
      const int cellDescriptionsIndex,const int solverElement) const;


  /**
   * Evaluate a stricter DMP as an a-priori refinement
   * criterion.
   *
   * TODO(Dominic): It is a little hacked together
   */
  bool evaluateDiscreteMaximumPrincipleRefinementCriterion(
      const int cellDescriptionsIndex,
      const int element) const;

  /**
   * Based on the limiter status of a solver patch
   * and the solver patch's type, we perform the
   * following actions:
   *
   * | New Status | Type                        | Action                                                                                           |
   * ----------------------------------------------------------------------------------------------------------------------------------------------|
   * | O/NNT      | Any                         | Do nothing.                                                                                      |
   * | T/NT       | Cell                        | Set RefinementRequested event on parent cell if its current event is None or AugmentingRequested |
   * | T/NT       | Descendant                  | Set RefinementRequested event if current event is None or AugmentingRequested                    |
   * | T/NT       | Else                        | Do nothing                                                                                       |
   *
   * \note Currently we assume that the problem and load-balancing is so well-behaved that
   * we always find a Cell as parent of a Descendant on the same MPI rank. We further do not
   * consider Master-Worker boundaries in the lookup of the parent.
   *
   * Legend: O: Ok, T: Troubled, NT: NeighbourOfTroubled1..2, NNT: NeighbourOfTroubled3..4
   */
  bool markForRefinementBasedOnLimiterStatus(
        exahype::Cell& fineGridCell,
        exahype::Vertex* const fineGridVertices,
        const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
        exahype::Cell& coarseGridCell,
        exahype::Vertex* const coarseGridVertices,
        const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
        const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
        const bool initialGrid,
        const int solverNumber);

  /**
   * Update the cellwise limiter status using the facewise limiter status
   * values.
   *
   * Ensure a limiter patch is allocated on all cells
   * on the finest level of the grid which have
   * a limiter status greater than 0.
   *
   * Dellocate the limiter patch on helper cells.
   *
   * \note We overwrite the facewise limiter status values with the new value
   * in order to use the updateLimiterStatusAfterSetInitialConditions function
   * afterwards which calls determineLimiterStatus(...) again.
   */
  void updateLimiterStatusDuringLimiterStatusSpreading(
      const int cellDescriptionsIndex, const int solverElement) const;

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

  void reconstructStandardTimeSteppingData(const int cellDescriptionsIndex,int element) const {
    _solver->reconstructStandardTimeSteppingData(cellDescriptionsIndex,element);

    SolverPatch& solverPatch = _solver->getCellDescription(cellDescriptionsIndex,element);
    const int limiterElement = _limiter->tryGetElement(cellDescriptionsIndex,solverPatch.getSolverNumber());
    if (limiterElement!=exahype::solvers::Solver::NotFound) {
      LimiterPatch& limiterPatch = _limiter->getCellDescription(cellDescriptionsIndex,limiterElement);
      limiterPatch.setTimeStamp(solverPatch.getCorrectorTimeStamp());
      limiterPatch.setTimeStepSize(solverPatch.getCorrectorTimeStepSize());
    }
  }

 /**
   * Rollback to the previous time step, i.e,
   * overwrite the time step size and time stamp
   * fields of the solver and limiter patches
   * by the values used in the previous iteration.
   */
  void rollbackToPreviousTimeStep(
      const int cellDescriptionsIndex,
      const int solverElement);

  /**
   * TODO(Dominic): Docu
   */
  void reconstructStandardTimeSteppingDataAfterRollback(
      const int cellDescriptionsIndex,
      const int element) const;

  /**
   * TODO(Dominic): I need the whole limiter recomputation
   * procedure also for the initial conditions.
   *
   * This includes computing, sending, and merging
   * of the min/max values.
   */
  void setInitialConditions(
      const int cellDescriptionsIndex,
      const int element,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) override;

  /**
   * This method assumes the ADERDG solver's cell-local limiter status has
   * already been determined.
   *
   * Before performing an update with the limiter,
   * set the ADER-DG time step sizes for the limiter patch.
   * (ADER-DG is always dictating the time step sizes.)
   *
   * \see determineLimiterStatusAfterLimiterStatusSpreading(...)
   */
  void updateSolution(
      const int cellDescriptionsIndex,
      const int element,
      double** tempStateSizedArrays,
      double** tempUnknowns,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) override;

  /**
   * Determine the new cell-local min max values.
   *
   * Must be invoked after ::determineLimiterStatusAfterSolutionUpdate.
   *
   * TODO(Dominic): Tobias's integer
   * flagging idea might reduce complexity here
   */
  void determineMinAndMax(
      const int cellDescriptionsIndex,
      const int element);

  /**
   * Evaluates a discrete maximum principle (DMP) and
   * the physical admissibility detection (PAD) criterion for
   * the solution values stored for any solver patch
   * that is of type Cell independent of the mesh level
   * it is located at.
   * This method then invokes
   * ::determinLimiterStatusAfterSolutionUpdate(SolverPatch&,const bool)
   * with the result of these checks.
   *
   * For solver patches of a type other than Cell,
   * we simply update the limiter status using
   * the information taken from the neighbour
   * merging.
   */
  exahype::solvers::LimiterDomainChange
  updateLimiterStatusAndMinAndMaxAfterSolutionUpdate(
      const int cellDescriptionsIndex,
      const int element);

  /**
   * Similar to ::determineLimiterStatusAfterSolutionUpdate(const int,const int)
   * Does only evaluate the physical admissibility detection (PAD) but not the
   * discrete maximum principle (DMP).
   *
   * \note We overwrite the facewise limiter status values with the new value
   * in order to reuse the determineLimiterStatusAfterSolutionUpdate function
   * which calls determineLimiterStatus(...) again.
   */
  exahype::solvers::LimiterDomainChange updateLimiterStatusAndMinAndMaxAfterSetInitialConditions(
      const int cellDescriptionsIndex,
      const int element);

  /**
   * Update the merged limiter status with a unified value based on
   * the merged limiter statuses per face of the cell.
   *
   * \return LimiterDomainChange::Regular as along as the function does not
   * detect a cell of type Descendant/EmptyDescendant on the finest mesh level
   * with a limiter status other than Ok.
   *
   * \see ::determineLimiterStatus
   */
  exahype::solvers::LimiterDomainChange updateLimiterStatus(
      const int cellDescriptionsIndex,const int element) const;


  /**
   * Update the limiter status of the solver patch
   * using the boundary states.
   *
   * Then write back the result to the boundary.
   */
  exahype::solvers::LimiterDomainChange updateLimiterStatus(SolverPatch& solverPatch) const;

  /*
   * Deallocate the limiter patch on all AMR related
   * helper cells.
   *
   * It is safe to use this method during
   * the mesh refinement iterations.
   *
   * \note Thread-safe.
   */
   void deallocateLimiterPatchOnHelperCell(
       const int cellDescriptionsIndex,
       const int solverElement) const;

   /*
    * Ensures that a limiter patch is allocated
    * on all compute cells (Cell) on the finest mesh
    * level that are flagged
    * with a limiter status other than Ok.
    *
    * It is safe to use this method during
    * the mesh refinement iterations.
    */
   bool ensureRequiredLimiterPatchIsAllocated(
           const int cellDescriptionsIndex,
           const int solverElement) const;

  /**
   * Reinitialises cells that have been subject to a limiter status change.
   * This method is invoked (during and??) after the limiter status spreading.
   *
   * The method has to take into account which solution, the solver's
   * or the limiter's, was populated with valid solution values
   * in the last iteration. The action of this method is
   * thus based on the new and old limiter status.
   *
   * We perform the following actions based on the
   * old and new limiter status:
   *
   * | New Status | Old Status | Action                                                                                                                                        |
   * ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
   * | O          | O          | Do nothing.                                                                                                                                   |
   * | ~          | T/NT/NNT   | Remove the limiter patch.                                                                                                                     |
   * | T/NT/NNT   | T/NT       | Roll back the limiter solution.                                                                                                               |
   * | ~          | O/NNT      | Roll back the solver solution. Initialise limiter patch if necessary. Project (old, valid) solver solution onto the limiter's solution space. |
   *
   * Legend: O: Ok, T: Troubled, NT: NeighbourOfTroubled1..2, NNT: NeighbourOfTroubled3..4
   *
   * We do not overwrite the old limiter status set in this method.
   * We compute the new limiter status based on the merged limiter statuses associated
   * with the faces.
   *
   * <h2>A-posteriori refinement</h2>
   * In case of a-posteriori refinement, we also perform a rollback
   * in the Ok cells. Then, the global time step size used by the predictor
   * is not valid anymore (assumption: global time stepping)
   *  and the last solution update has to be redone.
   *
   * <h2>Compute cell limiter patch deallocation</h2>
   * It is only safe to deallocate unrequired compute cell limiter patches after
   * the mesh refinement iterations since we might throw away valid
   * FV values during the first iterations. However, then find out later
   * that we need them after the limiter status diffusion
   * has converged.
   * Helper cell limiter patches can be deallocated during
   * the mesh refinement iterations.
   */
  void reinitialiseSolvers(
      const int cellDescriptionsIndex,
      const int element,
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) const;

  /**
   * Recompute the solution in cells that have been subject to a limiter status change
   * This method is invoked after the solver reinitialisation
   * (see exahype::solvers::LimitingADERDGSolver::reinitialiseSolvers).
   *
   * It evolves the solution of the solver and limiter in the reinitialised cells to the
   * correct time stamp.
   *
   * We perform the following actions based on the
   * new limiter status:
   *
   * |New Status | Action                                                                                                                                      |
   * ----------------------------------------------------------------------------------------------------------------------------------------------------------|
   * |O          | Do nothing. Solver solution has been evolved correctly before. DG solution is correct.                                                                             |
   * |T/NT       | Evolve FV solver. Project result onto the ADER-DG space. Recompute the space-time predictor if not initial recomputation.                                                                                  |
   * |NNT        | Evolve solver and project its solution onto the limiter solution space. (We had to do a rollback beforehand in the reinitialisation phase.) |
   *
   * Legend: O: Ok, T: Troubled, NT: NeighbourOfTroubled1..2, NNT: NeighbourOfTroubled3..4
   */
  void recomputeSolution(
      const int cellDescriptionsIndex,
      const int element,
      exahype::solvers::SolutionUpdateTemporaryVariables& solutionUpdateTemporaryVariables,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator);

  /**
   * !!! Only for fused time stepping !!!
   *
   * Recompute the predictor in particular cells.
   *
   * We perform the following actions based on the
   * new and old limiter status:
   *
   * |New Status | Old Status | Action                                                                                                                                      |
   * ----------------------------------------------------------------------------------------------------------------------------------------------------------|
   * |O/NNTT     | O/NT/NNT   | Do nothing. Underlying DG solution has not changed.
   * |           | T          | Recompute predictor. Cell was skipped before in predictor computation
   * |           |            | since it is marked T. See mapping Prediction::enterCell.
   * |NT         | *          | Recompute predictor. DG solution has been recomputed.
   * |T          | *          | Not necesssary to compute predictor
   *
   * Legend: O: Ok, T: Troubled, NT: NeighbourIsTroubledCell, NNT: NeighbourIsNeighbourOfTroubledCell
   *
   * We do not overwrite the old limiter status set in this method.
   * We compute the new limiter status based on the merged limiter statuses associated
   * with the faces.
   */
  void recomputePredictor(
      const int cellDescriptionsIndex,
        const int element,
        exahype::solvers::PredictionTemporaryVariables& predictionTemporaryVariables,
        exahype::Vertex* const fineGridVertices,
        const peano::grid::VertexEnumerator& fineGridVerticesEnumerator);

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

  /**
   * Restrict the Troubled limiter status of a cell
   * up to the parent if the parent exists.
   *
   * Any other limiter status is ignored.
   *
   * \note This operation is not thread-safe
   */
  void restrictLimiterStatus(
      const int cellDescriptionsIndex,
      const int element,
      const int parentCellDescriptionsIndex,
      const int parentElement) const;

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

  /**
   * Merge solver boundary data (and other values) of two adjacent
   * cells based on their limiter status.
   *
   * The solver involved in the neighbour merge
   * is selected according to the following scheme:
   *
   * | Status 1 | Status 2 | Solver to Merge
   * ---------------------------------------
   * | O        | O        | ADER-DG       |
   * | O        | NNT      | ADER-DG       |// O|NNT x O|NNT
   * | NNT      | O        | ADER-DG       |
   * | NNT      | NNT      | ADER-DG       |
   *
   * | NNT      | NT       | FV            |
   * | NT       | NNT      | FV            | // NT&NNT | N&NNT
   *
   * | NT       | NT       | FV            |
   * | NT       | T        | FV            |
   * | T        | NT       | FV            | // T|NT x T|NT
   * | T        | T        | FV            |
   *
   * Legend: O: Ok, T: Troubled, NT: NeighbourIsTroubledCell, NNT: NeighbourIsNeighbourOfTroubledCell
   *
   * <h2>Solution Recomputation</h2>
   * If we perform a solution recomputation, we do not need to perform
   * a solution update in the non-troubled solver patches and the patches
   * with status neighbourIsNeighbourOfTroubledCell. Instead we
   * can simply reuse the already computed update to advance in
   * time to the desired time stamp.
   *
   * We thus do not need to merge these patches with neighbour data
   * in the recomputation phase.
   *
   * \note Limiting is only performed on the finest level
   * of the mesh. The other levels work only with the ADER-DG
   * solver.
   *
   * TODO(Dominic): Remove limiterstatus1 and limiterStatus2 argument.
   * They depend on the isRecomputation value
   */
  void mergeNeighboursBasedOnLimiterStatus(
      const int                                 cellDescriptionsIndex1,
      const int                                 element1,
      const int                                 cellDescriptionsIndex2,
      const int                                 element2,
      const tarch::la::Vector<DIMENSIONS, int>& pos1,
      const tarch::la::Vector<DIMENSIONS, int>& pos2,
      const bool                                isRecomputation,
      double**                                  tempFaceUnknowns,
      double**                                  tempStateSizedVectors,
      double**                                  tempStateSizedSquareMatrices) const;

  /**
   * Merges the min max of two neighbours sharing a face.
   *
   * This method is used to detect cells that are
   * troubled after the imposition of initial conditions.
   */
  void mergeSolutionMinMaxOnFace(
      const int                                 cellDescriptionsIndex1,
      const int                                 element1,
      const int                                 cellDescriptionsIndex2,
      const int                                 element2,
      const tarch::la::Vector<DIMENSIONS, int>& pos1,
      const tarch::la::Vector<DIMENSIONS, int>& pos2);

  /**
   * Depending on the limiter status, we impose boundary conditions
   * onto the solution of the solver or of the limiter.
   */
  void mergeWithBoundaryData(
      const int                                 cellDescriptionsIndex,
      const int                                 element,
      const tarch::la::Vector<DIMENSIONS, int>& posCell,
      const tarch::la::Vector<DIMENSIONS, int>& posBoundary,
      double**                                  tempFaceUnknowns,
      double**                                  tempStateSizedVectors,
      double**                                  tempStateSizedSquareMatrices) override;

  /**
   * Merge solver boundary data (and other values) of a
   * cell with the boundary conditions based on the cell's
   * limiter status.
   *
   * The solver involved in the merge
   * is selected according to the following scheme:
   *
   * | Status   | Solver to Merge |
   * ------------------------------
   * | O        | ADER-DG         |
   * | NNT      | ADER-DG         |
   *
   * | NT       | FV              |
   * | T        | FV              |
   *
   * Legend: O: Ok, T: Troubled, NT: NeighbourIsTroubledCell, NNT: NeighbourIsNeighbourOfTroubledCell
   *
   * <h2>Solution Recomputation</h2>
   * If we perform a solution recomputation, we do not need to perform
   * a solution update in the non-troubled solver patches and the patches
   * with status neighbourIsNeighbourOfTroubledCell. Instead we
   * can simply reuse the already computed update to advance in
   * time to the desired time stamp.
   *
   * We thus do not need to merge these patches with boundary data
   * in the recomputation phase.
   *
   * \param[in] isRecomputation Flag indicating if this merge is part of a solution recomputation phase.
   */
  void mergeWithBoundaryDataBasedOnLimiterStatus(
        const int                                 cellDescriptionsIndex,
        const int                                 element,
        const int                                 limiterStatusAsInt,
        const tarch::la::Vector<DIMENSIONS, int>& posCell,
        const tarch::la::Vector<DIMENSIONS, int>& posBoundary,
        const bool                                isRecomputation,
        double**                                  tempFaceUnknowns,
        double**                                  tempStateSizedVectors,
        double**                                  tempStateSizedSquareMatrices);

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
  // NEIGHBOUR
  ///////////////////////////////////
  void appendNeighbourCommunicationMetadata(
      exahype::MetadataHeap::HeapEntries& metadata,
      const tarch::la::Vector<DIMENSIONS,int>& src,
      const tarch::la::Vector<DIMENSIONS,int>& dest,
      const int cellDescriptionsIndex,
      const int solverNumber) override;

  void mergeWithNeighbourMetadata(
      const exahype::MetadataHeap::HeapEntries& neighbourMetadata,
      const tarch::la::Vector<DIMENSIONS, int>& src,
      const tarch::la::Vector<DIMENSIONS, int>& dest,
      const int                                 cellDescriptionsIndex,
      const int                                 element) override;

  void sendDataToNeighbour(
      const int                                     toRank,
      const int                                     cellDescriptionsIndex,
      const int                                     element,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  /**
   * Send data or empty data to the neighbour data based
   * on the limiter status.
   *
   * \param[in] isRecomputation Indicates if this called within a solution recomputation
   *                            process.
   * \param[in] limiterStatus   This method is used in two different contexts (see \p isRecomputation)
   *                            which either make use of the unified face-wise limiter status (isRecomputation)
   *                            or make use of the cell-wise limiter status (!isRecomputation).
   *
   * <h2>SolutionRecomputation</h2>
   * In case this method is called within a solution recomputation,
   * the ADER-DG solver does only send empty messages to the neighbour.
   * Otherwise it merges the received data and adds it to the update.
   *
   * \note This method assumes that there has been a unified face-wise limiter status value
   * determined and written back to the faces a-priori.
   *
   * <h2>Possible optimisations</h2>
   * Depending on isRecomputation we do not need to send both, solver and limiter,
   * data for patches with status NeighbourIsNeighbourOfTroubledCell and NeighbourOfTroubledCell.
   */
  void sendDataToNeighbourBasedOnLimiterStatus(
        const int                                    toRank,
        const int                                    cellDescriptionsIndex,
        const int                                    element,
        const tarch::la::Vector<DIMENSIONS, int>&    src,
        const tarch::la::Vector<DIMENSIONS, int>&    dest,
        const bool                                   isRecomputation,
        const tarch::la::Vector<DIMENSIONS, double>& x,
        const int                                    level) const;

  void sendEmptyDataToNeighbour(
      const int                                     toRank,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;

  void mergeWithNeighbourData(
      const int                                    fromRank,
      const exahype::MetadataHeap::HeapEntries&    neighbourMetadata,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      double**                                     tempFaceUnknowns,
      double**                                     tempStateSizedVectors,
      double**                                     tempStateSizedSquareMatrices,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) override;

  /**
   * Merge or drop received neighbour data based
   * on the limiter status.
   *
   * \param isRecomputation Indicates if this called within a solution recomputation
   *                        process.
   * \param limiterStatus   This method is used in two different contexts (see \p isRecomputation)
   *                        which either make use of the unified face-wise limiter status (isRecomputation)
   *                        or make use of the cell-wise limiter status (!isRecomputation).
   *
   * <h2>SolutionRecomputation</h2>
   * In case this method is called within a solution recomputation,
   * the ADER-DG solver drops the received boundary data.
   * Otherwise it merges the received data and adds it to the update.
   *
   *  \note This method assumes that there has been a unified face-wise limiter status value
   *  determined and written back to the faces.
   */
  void mergeWithNeighbourDataBasedOnLimiterStatus(
      const int                                    fromRank,
      const exahype::MetadataHeap::HeapEntries&    neighbourMetadata,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      const bool                                   isRecomputation,
      double**                                     tempFaceUnknowns,
      double**                                     tempStateSizedVectors,
      double**                                     tempStateSizedSquareMatrices,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) const;

  void dropNeighbourData(
      const int                                     fromRank,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) override;


  ///////////////////////////////////////
  // NEIGHBOUR - Solution Recomputation
  ///////////////////////////////////////
  /**
   * TODO(Dominic):
   * Add more docu.
   *
   * We do not send the minimum and maximum solution values
   * during the solution recomputation process.
   */
  void sendEmptySolverAndLimiterDataToNeighbour(
      const int                                     toRank,
      const tarch::la::Vector<DIMENSIONS, int>&     src,
      const tarch::la::Vector<DIMENSIONS, int>&     dest,
      const tarch::la::Vector<DIMENSIONS, double>&  x,
      const int                                     level) const;

  /**
   * TODO(Dominic):
   * Add more docu.
   *
   * We do not send the minimum and maximum solution values
   * during the solution recomputation process.
   */
  void dropNeighbourSolverAndLimiterData(
        const int                                     fromRank,
        const tarch::la::Vector<DIMENSIONS, int>&     src,
        const tarch::la::Vector<DIMENSIONS, int>&     dest,
        const tarch::la::Vector<DIMENSIONS, double>&  x,
        const int                                     level) const;

  /////////////////////////////////////
  // MASTER<=>WORKER
  /////////////////////////////////////
  void appendMasterWorkerCommunicationMetadata(
      exahype::MetadataHeap::HeapEntries& metadata,
      const int cellDescriptionsIndex,
      const int solverNumber) override;

  void mergeWithMasterWorkerMetadata(
      const exahype::MetadataHeap::HeapEntries& neighbourMetadata,
      const int                                 cellDescriptionsIndex,
      const int                                 element) override;

  /////////////////////////////////////
  // FORK OR JOIN
  /////////////////////////////////////
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

  std::string toString() const override;

  void toString (std::ostream& out) const override;

  const std::unique_ptr<exahype::solvers::FiniteVolumesSolver>&
  getLimiter () const {
    return _limiter;
  }

  const std::unique_ptr<exahype::solvers::ADERDGSolver>&
  getSolver () const {
    return _solver;
  }
};


#endif /* LIMITEDADERDGSOLVER_H_ */
