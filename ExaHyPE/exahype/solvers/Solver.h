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
 * \author Dominic E. Charrier, Tobias Weinzierl
 **/
 
#ifndef _EXAHYPE_SOLVERS_SOLVER_H_
#define _EXAHYPE_SOLVERS_SOLVER_H_

#include <memory>
#include <string>
#include <iostream>
#include <vector>

#include "tarch/la/Vector.h"
#include "tarch/multicore/BooleanSemaphore.h"

#include "peano/utils/Globals.h"
#include "peano/grid/VertexEnumerator.h"
#include "peano/heap/DoubleHeap.h"
#include "peano/heap/HeapAllocator.h"

#include "exahype/State.h"

#include "exahype/profilers/Profiler.h"
#include "exahype/profilers/simple/NoOpProfiler.h"

#include "multiscalelinkedcell/HangingVertexBookkeeper.h"

// Some helpers
constexpr int power(int basis, int exp) {
  return (exp == 0) ? 1 : basis * power(basis, exp - 1);
}

#ifdef ALIGNMENT
constexpr int addPadding(const int originalSize) {
  return ALIGNMENT/8 * static_cast<int>((originalSize+(ALIGNMENT/8-1))/(ALIGNMENT/8));
}
#else
constexpr int addPadding(const int originalSize) {
  return originalSize;
}
#endif

namespace exahype {
  // Forward declarations
  class Cell;
  class Vertex;

  /**
   * We store the degrees of freedom associated with the ADERDGCellDescription and FiniteVolumesCellDescription
   * instances on this heap.
   * We further use this heap to send and receive face data from one MPI rank to the other.
   */
  #if ALIGNMENT==16
  typedef peano::heap::DoubleHeap<
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::AlignedDoubleSendReceiveTask<16> >,
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::AlignedDoubleSendReceiveTask<16> >,
    peano::heap::RLEBoundaryDataExchanger< double, false, peano::heap::AlignedDoubleSendReceiveTask<16> >,
    std::vector< double, peano::heap::HeapAllocator<double, 16 > >
  >     DataHeap;
  #elif ALIGNMENT==32
  typedef peano::heap::DoubleHeap<
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::AlignedDoubleSendReceiveTask<32> >,
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::AlignedDoubleSendReceiveTask<32> >,
    peano::heap::RLEBoundaryDataExchanger< double, false, peano::heap::AlignedDoubleSendReceiveTask<32> >,
    std::vector< double, peano::heap::HeapAllocator<double, 32 > >
  >     DataHeap;
  #elif ALIGNMENT==64
  typedef peano::heap::DoubleHeap<
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::AlignedDoubleSendReceiveTask<64> >,
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::AlignedDoubleSendReceiveTask<64> >,
    peano::heap::RLEBoundaryDataExchanger< double, false, peano::heap::AlignedDoubleSendReceiveTask<64> >,
    std::vector< double, peano::heap::HeapAllocator<double, 64 > >
  >     DataHeap;
  #elif defined(ALIGNMENT)
  #error ALIGNMENT choice not supported
  #else
  typedef peano::heap::DoubleHeap<
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::SendReceiveTask<double> >,
    peano::heap::SynchronousDataExchanger< double, true,  peano::heap::SendReceiveTask<double> >,
    peano::heap::RLEBoundaryDataExchanger< double, false, peano::heap::SendReceiveTask<double> >
  >     DataHeap;
  #endif


#ifdef Parallel
  /**
   * We abuse this heap to send and receive metadata from one MPI rank to the other.
   * We never actually store data on this heap.
   */
  typedef peano::heap::Heap<
      peano::heap::records::IntegerHeapData,
      peano::heap::SynchronousDataExchanger< peano::heap::records::IntegerHeapData, true,  peano::heap::SendReceiveTask<peano::heap::records::IntegerHeapData> >,
      peano::heap::SynchronousDataExchanger< peano::heap::records::IntegerHeapData, true,  peano::heap::SendReceiveTask<peano::heap::records::IntegerHeapData> >,
      peano::heap::RLEBoundaryDataExchanger< peano::heap::records::IntegerHeapData, false, peano::heap::SendReceiveTask<peano::heap::records::IntegerHeapData> >
  >     MetadataHeap;

  /**
   * Defines an invalid metadata entry.
   */
  static constexpr int InvalidMetadataEntry = -1;

  /**
   * Defines the length of the metadata
   * we send out per solver.
   *
   * First entry is the cell (description) type.
   * Second entry is the augmentation status,
   * third the helper status and the fourth the
   * limiter status.
   */
  static constexpr int NeighbourCommunicationMetadataPerSolver    = 4;

  // TODO(Dominic): We can still encode the information below
  // into a single status int
  static constexpr int NeighbourCommunicationMetadataCellType           = 0;
  static constexpr int NeighbourCommunicationMetadataAugmentationStatus = 1;
  static constexpr int NeighbourCommunicationMetadataHelperStatus       = 2;
  static constexpr int NeighbourCommunicationMetadataLimiterStatus      = 3;

  static constexpr int MasterWorkerCommunicationMetadataPerSolver = 1;

  static constexpr int MasterWorkerCommunicationMetadataSendReceiveData = 0;

  /**
   * TODO(Dominic): Docu is outdated
   *
   * Encodes the metadata as integer sequence.
   *
   * The first element refers to the number of
   * ADERDGCellDescriptions associated with this cell (nADERG).
   * The next 2*nADERG elements store a pair of
   * solver number, and cell description type (encoded as int)
   * for each ADERDGCellDescription associated with this cell (description).
   *
   * The element 1+2*nADERDG refers to the number of
   * FiniteVolumesCellDescriptions associated with this cell (nFV).
   * The remaining 2*nFV elements store a pair of
   * solver number, and cell description type (encoded as int)
   * for each FiniteVolumesCellDescription associated with this cell
   * (description).
   */
  exahype::MetadataHeap::HeapEntries encodeNeighbourCommunicationMetadata(
      const int cellDescriptionsIndex,
      const tarch::la::Vector<DIMENSIONS,int>& src,
      const tarch::la::Vector<DIMENSIONS,int>& dest);

  /**
   * TODO(Dominic): Add docu.
   */
  exahype::MetadataHeap::HeapEntries encodeMasterWorkerCommunicationMetadata(const int cellDescriptionsIndex);

  /**
   * Creates a sequence of \p InvalidMetadataEntry with length
   * exahype::solvers::RegisteredSolvers.size()*NeighbourCommunicationMetadataPerSolver.
   */
  exahype::MetadataHeap::HeapEntries createNeighbourCommunicationMetadataSequenceWithInvalidEntries();

  /**
   * Creates a sequence of \p InvalidMetadataEntry with length
   * exahype::solvers::RegisteredSolvers.size()*MasterWorkerCommunicationMetadataPerSolver.
   */
  exahype::MetadataHeap::HeapEntries createMasterWorkerCommunicationMetadataSequenceWithInvalidEntries();

  /**
   * Checks if all the entries of \p sequence are set to
   * \p InvalidMetadataEntry.
   */
  bool isNeighbourCommunicationMetadataSequenceWithInvalidEntries(exahype::MetadataHeap::HeapEntries& sequence);

  /**
   * Checks if all the entries of \p sequence are set to
   * \p InvalidMetadataEntry.
   */
  bool isMasterWorkerCommunicationMetadataSequenceWithInvalidEntries(exahype::MetadataHeap::HeapEntries& sequence);

  /**
   * Send metadata to rank \p toRank.
   */
  void sendNeighbourCommunicationMetadata(
      const int                                   toRank,
      const int                                   cellDescriptionsIndex,
      const tarch::la::Vector<DIMENSIONS,int>&    src,
      const tarch::la::Vector<DIMENSIONS,int>&    dest,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);

  /**
   * Receive metadata to rank \p toRank.
   *
   * \return The index of the received metadata message
   * on the exahype::MetadataHeap.
   */
  int receiveNeighbourCommunicationMetadata(
      const int                                   fromRank,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);

  /**
   * Send metadata to rank \p toRank.
   */
  void sendMasterWorkerCommunicationMetadata(
      const int                                   toRank,
      const int                                   cellDescriptionsIndex,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);

  /**
   * Receive metadata to rank \p toRank.
   *
   * \return The index of the received metadata message
   * on the exahype::MetadataHeap.
   */
  int receiveMasterWorkerCommunicationMetadata(
      const int                                   fromRank,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);

  /**
   * Send a metadata sequence filled with InvalidMetadataEntry
   * to rank \p toRank.
   */
  void sendNeighbourCommunicationMetadataSequenceWithInvalidEntries(
      const int                                   toRank,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);

  /**
   * Send a metadata sequence filled with InvalidMetadataEntry
   * to rank \p toRank.
   */
  void sendMasterWorkerCommunicationMetadataSequenceWithInvalidEntries(
      const int                                   toRank,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);

  /**
   * Drop metadata sent by rank \p fromRank.
   */
  void dropMetadata(
      const int                                   fromRank,
      const peano::heap::MessageType&             messageType,
      const tarch::la::Vector<DIMENSIONS,double>& x,
      const int                                   level);
  #endif

  namespace solvers {
    class Solver;

    typedef std::vector<Solver*> RegisteredSolversEntries;
    /**
     * All the registered solvers. Has to be declared extern in C++ standard as
     * it is instantiated in the corresponding cpp file.
     */
    extern std::vector<Solver*> RegisteredSolvers;

    /**
     * The limiter domain change that was detected after a solution
     * update or during the limiter status spreading.
     */
    enum class LimiterDomainChange {
      /**
       * A regular change of the limiter domain
       * has occurred. This might be either no change at
       * all or a situation where a cell directly next to a
       * troubled cell has been newly marked as troubled.
       */
      Regular,

      /**
       * A cell which is not directly next to a troubled cell
       * has newly been marked as troubled.
       * The cell and all its neighbours with Limiter-Status other than Ok,
       * are on the finest mesh level.
       *
       * <h2>Consequences</h2>
       * Usually, only a limiter status spreading, reinitialisation
       * and recomputation is necessary.
       *
       * However, if we further detect a cell of type Descendant/EmptyDescendant
       * marked with LimiterStatus other than Ok on the finest mesh level,
       * this status will change to IrregularChangeOnCoarserMeshLevel.
       */
      Irregular,

      /**
       * Scenario 1:
       * A cell which is not directly next to a troubled cell
       * has newly been marked as troubled.
       * The cell is not on the finest mesh level.
       *
       * Scenario 2:
       * A cell of type Descendant/EmptyDescendant
       * was marked with LimiterStatus other than Ok.
       * The cell is on the finest mesh level.
       *
       * <h2>Consequences</h2>
       * The runner must then refine the mesh accordingly, and perform a
       * rollback in all cells to the previous solution. It computes
       * a new time step size in all cells. Next, it recomputes the predictor in all
       * cells, troubled or not. Finally, it reruns the whole ADERDG time step in
       * all cells, troubled or not.
       *
       * This can potentially be relaxed for anarchic time stepping where
       * each cell has its own time step size and stamp.
       */
      IrregularRequiringMeshUpdate,
    };

    /**
     * Converts LimiterDomainChange to its double equivalent.
     */
    double convertToDouble(const LimiterDomainChange& limiterDomainChange);

    /**
     * Converts a double to a LimiterDomainChange.
     */
    LimiterDomainChange convertToLimiterDomainChange(const double value);

    /**
     * Temporary variables that
     * hold information if the grid has been updated
     * or if the limiter domain has changed.
     *
     * Used if we employ multiple threads.
     */
    class SolverFlags;

    /**
     * Sets the limiter domain has changed flags per
     * solver to false.
     */
    void initialiseSolverFlags(SolverFlags& solverFlags);

    /**
     * Sets the limiter domain has changed flags per
     * solver to false.
     */
    void prepareSolverFlags(SolverFlags& solverFlags);

    /**
     * Sets the limiter domain has changed flags per
     * solver to false.
     */
    void deleteSolverFlags(SolverFlags& solverFlags);
  }
}


class exahype::solvers::SolverFlags {
public:
  /**
   * Per solver, we hold a status flag indicating
   * if the limiter domain of the solver has
   * changed.
   *
   * The flag has only a meaning for the LimitingADERDGSolver.
   */
  LimiterDomainChange* _limiterDomainChange = nullptr;

  /**
   * Per solver, we hold a status flag indicating
   * if the solver has requested a mesh update.
   */
  bool* _meshUpdateRequest = nullptr;
};

/**
 * Describes one solver.
 */
class exahype::solvers::Solver {
 private:
  /**
   * Log device.
   */
  static tarch::logging::Log _log;

 public:
  /**
   * The type of a solver.
   */
  enum class Type { ADERDG, FiniteVolumes, LimitingADERDG };

  /**
   * The time stepping mode.
   */
  enum class TimeStepping {
    /**
     * In the global time stepping mode, every cells works with the same time step.
     */
    Global,
    /**
     * In the fixed time stepping mode, we assume that each cell advanced in
     * time with the prescribed time step size. No CFL condition is checked.
     */
    GlobalFixed
    // Local, Anarchic
  };

  /**
   * The refinement control states
   * returned by the user functions.
   */
  enum class RefinementControl { Keep = 0, Refine = 1, Erase = 2 };

  /**
   * This struct is used in the AMR context
   * to lookup a parent cell description and
   * for computing the subcell position of the child
   * with respect to this parent.
   *
   * TODO(Dominic): Move to more appropriate place.
   */
  typedef struct SubcellPosition {
    int parentCellDescriptionsIndex;
    int parentElement;
    tarch::la::Vector<DIMENSIONS,int> subcellIndex;
    int levelDifference;

    SubcellPosition() :
      parentCellDescriptionsIndex(multiscalelinkedcell::HangingVertexBookkeeper::InvalidAdjacencyIndex),
      parentElement(NotFound),
      subcellIndex(-1),
      levelDifference(-1) {}
    ~SubcellPosition() {}
  } SubcellPosition;

  /**
   * The augmentation control states.
   */
  enum class AugmentationControl {
    /**
     * Indicates that a spacetree cell is next to another spacetree cell
     * of type exahype::records::ADERDGCellDescription::Cell.
     */
    NextToCell = 0,
    /**
     * Indicates that a spacetree cell is next to another spacetree cell
     * of type exahype::records::ADERDGCellDescription::Ancestor or
     * exahype::records::ADERDGCellDescription::EmptyAncestor.
     */
    NextToAncestor = 1,
    /**
     * Indicates that a spacetree cell is both, next to another spacetree cell
     * of type exahype::records::ADERDGCellDescription::Ancestor or
     * exahype::records::ADERDGCellDescription::EmptyAncestor, and
     * a spacetree cell of type
     * exahype::records::ADERDGCellDescription::Cell.
     */
    NextToCellAndAncestor = 2,
    /**
     * Indicates that a spacetree cell is neither, next to another spacetree cell
     * of type exahype::records::ADERDGCellDescription::Ancestor or
     * exahype::records::ADERDGCellDescription::EmptyAncestor, nor
     * next to a spacetree cell of type exahype::records::ADERDGCellDescription::Cell.
     *
     * A cell of type exahype::records::ADERDGCellDescription::Descendant can then request erasing.
     * A cell of type exahype::records::ADERDGCellDescription::Cell does then not need
     * to request augmenting.
     */
     Default = 3
  };

  /**
   * Default return value of function getElement(...)
   * If we do not find the element in a vector
   * stored at a heap address.
   */
  static const int NotFound;

  /**
   * Run over all solvers and identify the minimal time stamp.
   */
  static double getMinSolverTimeStampOfAllSolvers();

  /**
   * Run over all solvers and identify the minimal sum of minimal time stamp
   * plus the minimal time step size.
   *
   * The result is a lower bound of the minimum time stamp
   * that will be obtained in the following time step.
   */
  static double estimateMinNextSolverTimeStampOfAllSolvers();

  /**
   * Run over all solvers and identify the minimal time step size.
   */
  static double getMinSolverTimeStepSizeOfAllSolvers();

  /**
   * Run over all solvers and identify the maximal time stamp.
   *
   * On the individual patches, we do use the min time stamp so
   * far, so the routine returns the maximum over all min solver
   * time stamps.
   */
  static double getMaxSolverTimeStampOfAllSolvers();

  static bool allSolversUseTimeSteppingScheme(solvers::Solver::TimeStepping scheme);

  static double getCoarsestMeshSizeOfAllSolvers();
  static double getFinestMaximumMeshSizeOfAllSolvers();

  /**
   * Run over all solvers and identify the maximum depth of adaptive
   * refinement employed.
   *
   * This number directly correlates with the number
   * of grid iterations to run for performing an erasing operation.
   */
  static int getMaxAdaptiveRefinementDepthOfAllSolvers();

  /**
   * Loop over the solver registry and check if one
   * of the solvers has requested a mesh update.
   */
  static bool oneSolverRequestedMeshUpdate();

  /**
   * Loop over the solver registry and check if one
   * of the solver's mesh refinement has not
   * attained a stable state yet.
   */
  static bool oneSolverHasNotAttainedStableState();

  /**
   * Returns true if one of the solvers used a time step size
   * that violated the CFL condition.
   *
   * TODO(Dominic): Rename. Name can be confused with
   * oneSolverHasNotAttainedStableState.
   */
  static bool stabilityConditionOfOneSolverWasViolated();

 /**
  * Some solvers can deploy data conversion into the background. How this is
  * done is solver-specific. However, we have to wait until all tasks have
  * terminated if we want to modify the heap, i.e. insert new data or remove
  * data. Therefore, the wait (as well as the underlying semaphore) belong
  * into this abstract superclass.
  */
 static void waitUntilAllBackgroundTasksHaveTerminated();

 protected:
  /**
   * @see waitUntilAllBackgroundTasksHaveTerminated()
   */
  static int                                _NumberOfTriggeredTasks;

  /**
   * @see waitUntilAllBackgroundTasksHaveTerminated()
   */
  static tarch::multicore::BooleanSemaphore _heapSemaphore;

  /**
   * Each solver has an identifier/name. It is used for debug purposes only.
   */
  const std::string _identifier;

  const Type _type;

  /**
   * The number of state variables of the conservation or balance law.
   */
  const int _numberOfVariables;

  /**
   * The number of parameters, e.g, material parameters.
   */
  const int _numberOfParameters;

  /**
   * The number of nodal basis functions that are employed in each
   * coordinate direction.
   */
  const int _nodesPerCoordinateAxis;

  /**
   * The offset of the computational domain.
   *
   * Is initialised by the initSolver method.
   */
  tarch::la::Vector<DIMENSIONS,double> _domainOffset;

  /**
   * The size of the computational domain.
   * * Is initialised by the initSolver method.
   */
  tarch::la::Vector<DIMENSIONS,double> _domainSize;

  /**
   * The maximum extent a cell is allowed to have in each coordinate direction.
   *
   * \note This is an upper bound specified in the specification file.
   * This is not the actual maximum extent of a cell.
   */
  const double _maximumMeshSize;

  /**
   * The coarsest level of the adaptive mesh that is
   * occupied by this solver.
   */
  int _coarsestMeshLevel;

  /**
   * The maximum depth the adaptive mesh is allowed
   * to occupy (set by the user).
   * Summing this value with _coarsestMeshdLevel results in
   * the finest mesh level the solver might occupy during the
   * simulation.
   */
  const int _maximumAdaptiveMeshDepth;

  /**
   * The minimum extent in each coordinate direction at least one cell in the grid has.
   *
   * This value needs to be updated every time the grid has been changed.
   */
  double _minCellSize; // TODO(Dominic): Remove these cell size variables and put them into the solver subclasses.
  double _nextMinCellSize; // TODO(Dominic): Remove these cell size variables and put them into the solver subclasses.

  /**
   * The maximum extent in each coordinate direction at least one cell in the grid has.
   *
   * This value needs to be updated every time the grid has been changed.
   */
  double _maxCellSize; // TODO(Dominic): Remove these cell size variables and put them into the solver subclasses.
  double _nextMaxCellSize; // TODO(Dominic): Remove these cell size variables and put them into the solver subclasses.

  /**
   * The time stepping mode of this solver.
   */
  const TimeStepping _timeStepping;

  /**
   * A profiler for this solver.
   */
  std::unique_ptr<profilers::Profiler> _profiler;

  /**
   * Flag indicating if a mesh update was
   * requested by this solver.
   *
   * This is the state after the
   * time step size computation.
   *
   * <h2>MPI</h2>
   * This is the state after this rank's
   * solver has merged its state
   * with its workers' worker.
   */
  bool _meshUpdateRequest;

  /**
   * Flag indicating if a mesh update was
   * requested by this solver.
   *
   * This is the state before the
   * time step size computation.
   *
   * <h2>MPI</h2>
   * This is the state before this rank's
   * solver has merged its state
   * with its workers' solver.
   */
  bool _nextMeshUpdateRequest;

  /**
   * Flag indicating if the mesh refinement
   * performed by this solver attained a stable state.
   *
   * <h2>MPI</h2>
   * This is the state after this rank's
   * solver has merged its state
   * with its workers' worker.
   */
  bool _attainedStableState;

  /**
   * Flag indicating if the mesh refinement
   * performed by this solver attained a stable state.
   *
   * This is the state before the
   * time step size computation.
   *
   * <h2>MPI</h2>
   * This is the state before this rank's
   * solver has merged its state
   * with its workers' solver.
   */
  bool _nextAttainedStableState;

 public:
  Solver(const std::string& identifier, exahype::solvers::Solver::Type type,
         int numberOfVariables, int numberOfParameters,
         int nodesPerCoordinateAxis,
         double maximumMeshSize,
         int maximumAdaptiveMeshDepth,
         exahype::solvers::Solver::TimeStepping timeStepping,
         std::unique_ptr<profilers::Profiler> profiler =
             std::unique_ptr<profilers::Profiler>(
                 new profilers::simple::NoOpProfiler("")));

  virtual ~Solver() { _profiler->writeToConfiguredOutput(); }

  // Disallow copy and assignment
  Solver(const Solver& other) = delete;
  Solver& operator=(const Solver& other) = delete;

  /**
   * Return a string representation for the type \p param.
   */
  static std::string toString(const exahype::solvers::Solver::Type& param);

  /**
   * Return a string representation for the time stepping mode \p param.
   */
  static std::string toString(const exahype::solvers::Solver::TimeStepping& param);

  /**
   * Return the grid level corresponding to the given mesh size with
   * respect to the given domainSize.
   */
  static int computeMeshLevel(double meshSize, double domainSize);

  /**
   * Returns the maximum extent a mesh cell is allowed to have
   * in all coordinate directions.
   * This maximum mesh size is used both as a
   * constraint on the AMR as well as to set up the initial
   * grid. If you return the extent of the computational domain in
   * each coordinate direction or larger values,
   * you indicate that this solver is not active in the domain.
   */
  double getMaximumMeshSize() const;

  /**
   * The coarsest level of the adaptive mesh that is
   * occupied by this solver.
   */
  double getCoarsestMeshLevel() const;

  /**
   * The maximum depth the adaptive mesh is allowed to
   * occupy (set by the user).
   * Summing this value with _coarsestMeshdLevel results in
   * the finest mesh level the solver might occupy during the
   * simulation.
   */
  double getMaximumAdaptiveMeshDepth() const;

  /**
   * The finest level of the adaptive mesh that might be
   * occupied by this solver.
   */
  double getMaximumAdaptiveMeshLevel() const;

  virtual void updateNextMinCellSize(double minCellSize);

  virtual void updateNextMaxCellSize(double maxCellSize);

  virtual double getNextMinCellSize() const;

  virtual double getNextMaxCellSize() const;

  virtual double getMinCellSize() const;

  virtual double getMaxCellSize() const;

  /**
   * Returns the identifier of this solver.
   */
  std::string getIdentifier() const;

  /**
   * Returns the type of this solver.
   */
  Type getType() const;

  /**
   * Returns the time stepping algorithm this solver is using.
   */
  TimeStepping getTimeStepping() const;

  /**
   * Returns the number of state variables.
   */
  int getNumberOfVariables() const;

  /**
   * Returns the number of parameters, e.g.,material constants etc.
   */
  int getNumberOfParameters() const;

  /**
   * If you use a higher order method, then this operation returns the
   * polynomial degree plus one. If you use a Finite Volume method, it
   * returns the number of cells within a patch per coordinate axis.
   */
  int getNodesPerCoordinateAxis() const;

  virtual std::string toString() const;

  virtual void toString(std::ostream& out) const;

  /**
   * Reset the mesh update flags.
   *
   * \deprecated
   */
  virtual void resetMeshUpdateRequestFlags();

  /**
   * Update if a mesh update was requested by this solver.
   *
   * <h2>MPI</h2>
   * This is the state before we have send data to the master rank
   * and have merged the state with this rank's workers.
   */
  virtual void updateNextMeshUpdateRequest(const bool& meshUpdateRequest);

  /**
   * Indicates if a mesh update was requested
   * by this solver.
   *
   * <h2>MPI</h2>
   * This is the state before we have send data to the master rank
   * and have merged the state with this rank's workers.
   */
  virtual bool getNextMeshUpdateRequest() const;

  /**
   * Indicates if a mesh update was requested
   * by this solver.
   *
   * <h2>MPI</h2>
   *This is the state before we have send data to the master rank
   * and have merged the state with this rank's workers.
   */
  virtual bool getMeshUpdateRequest() const;

  /**
   * Overwrite the _MeshUpdateRequest flag
   * by the _nextMeshUpdateRequest flag.
   * Reset the _nextMeshUpdateRequest flag
   * to false;
   */
  virtual void setNextMeshUpdateRequest();

  /**
   * Update if the mesh refinement of this solver attained
   * a stable state.
   *
   * <h2>MPI</h2>
   * This is the state before we have send data to the master rank
   * and have merged the state with this rank's workers.
   */
  virtual void updateNextAttainedStableState(const bool& attainedStableState);

  /**
   * Indicates if the mesh refinement of this solver
   * attained a stable state.
   *
   * <h2>MPI</h2>
   * This is the state before we have send data to the master rank
   * and have merged the state with this rank's workers.
   */
  virtual bool getNextAttainedStableState() const;

  /**
   * Indicates if the mesh refinement of this solver
   * attained a stable state.
   *
   * <h2>MPI</h2>
   *This is the state before we have send data to the master rank
   * and have merged the state with this rank's workers.
   */
  virtual bool getAttainedStableState() const;

  /**
   * Overwrite the _attainedStableState flag
   * by the _nextAttainedStableState flag.
   * Reset the _nextAttainedStableState flag
   * to false;
   */
  virtual void setNextAttainedStableState();

  /**
   * Run over all solvers and identify the minimal time stamp.
   */
  virtual double getMinTimeStamp() const = 0;

  /**
   * The minimum time step size
   * of all cell descriptions.
   */
  virtual double getMinTimeStepSize() const = 0;

  virtual void updateMinNextTimeStepSize(double value) = 0;

  /**
   * Initialise the solver's time stamps and time step sizes.
   * Further use the bounding box and the already known
   * maximum mesh size to compute the coarsest grid level
   * this solver is placed on.
   *
   * \note It is very important that the domainSize
   * is chosen as an multiple of the coarsest mesh size
   * of all solvers within the grid.
   *
   * The maximum adaptive refinement level is defined
   * with respect to this level.
   */
  virtual void initSolver(
      const double timeStamp,
      const tarch::la::Vector<DIMENSIONS,double>& domainOffset,
      const tarch::la::Vector<DIMENSIONS,double>& domainSize) = 0;

  /**
   * \return true if the solver is sending time step or neighbour data
   * in the current algorithmic section.
   * This depends usually on internal flags of the solver such as ones indicating
   * a mesh update request or a limiter domain change during a previous time stepping
   * iteration.
   *
   * \note Merging has not to be treated separately from computing.
   */
  virtual bool isSending(const exahype::records::State::AlgorithmSection& section) const = 0;

  /**
   * \return true if the solver is computing in the current algorithmic section.
   * This depends usually on internal flags of the solver such as ones indicating
   * a mesh update request or a limiter domain change during a previous time stepping
   * iteration.
   */
  virtual bool isComputing(const exahype::records::State::AlgorithmSection& section) const = 0;

  /**
   * Copies the time stepping data from the global solver onto the patch's time
   * meta data.
   *
   * \param[in] element Index of the cell description in
   *                    the array at address \p cellDescriptionsIndex.
   */
  virtual void synchroniseTimeStepping(
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
   * This routine is called if we perform
   * a time step update
   */
  virtual void startNewTimeStep() = 0;

  /**
   * This routine is called after the
   * mesh has been updated and we might have
   * to reinitialise some of the time
   * stamps or time step sizes before
   * we call startNewTimeStep().
   *
   * At the time of the call of this
   * function, a new next time step size
   * has already been computed.
   *
   * TODO(Dominic): Need something like this for each cell description if
   * we do fused time stepping?
   */
  virtual void reinitialiseTimeStepData() = 0;

  virtual double getMinNextTimeStepSize() const=0;

  /**
   * Returns true if the index \p cellDescriptionsIndex
   * is a valid heap index.
   */
  virtual bool isValidCellDescriptionIndex(
      const int cellDescriptionsIndex) const = 0;

  /**
   * If an entry for this solver exists,
   * return the element index of the cell description
   * in the array at address \p cellDescriptionsIndex.
   * Otherwise and if \p cellDescriptionsIndex is an
   * invalid index, return Solver::NotFound.
   */
  virtual int tryGetElement(
      const int cellDescriptionsIndex,
      const int solverNumber) const = 0;

  /**
   * \see exahype::amr::computeSubcellPositionOfCellOrAncestor
   */
  virtual SubcellPosition computeSubcellPositionOfCellOrAncestor(
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
   * Evaluates the user refinement criterion and sets
   * the RefinementEvent of a cell description to RefinementRequested
   * if the users criterion has been accepted,
   * or vetoes the ErasingChildrenRequested RefinementEvent
   * set on an Ancestor if
   * the users criterion returns a keep.
   *
   * \note We moved this routine out of the updateStateInEnterCell
   * since it does a-priori refinement, and we want to
   * reuse the updateStateInEnterCell and updateStateInLeaveCell methods
   * for a-posteriori refinement performed by the LimitingADERDGSolver
   * in scenarios where the mesh is refined based on a criterion
   * that takes the limiter status into account.
   */
  virtual bool markForRefinement(
        exahype::Cell& fineGridCell,
        exahype::Vertex* const fineGridVertices,
        const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
        exahype::Cell& coarseGridCell,
        exahype::Vertex* const coarseGridVertices,
        const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
        const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
        const bool initialGrid,
        const int solverNumber) = 0;

  /**
   * Modify a cell description in enter cell event.
   * This event should be used for single cell operations
   * like marking for refinement, erasing, augmenting,
   * or deaugmenting.
   *
   * Returns true if a performed action requires to
   * refine the mesh.
   *
   * \note We use this at the moment only
   * for refinement events. We can consider later
   * on to merge the time stepping functionality
   * (solution update, predictor comp.) into
   * this hook.
   */
  virtual bool updateStateInEnterCell(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const bool initialGrid,
      const int solverNumber) = 0;

  /**
   * Refinement routine that should be used for
   * collective children-parent operations.
   *
   * Returns true if the grid cell associated with
   * the cell description can be erased due to
   * an erasing/deaugmenting process that was finished
   * for the cell description or if there is simply
   * no cell description allocated for this solver.
   * Returns false otherwise.
   *
   * \note We use this at the moment only
   * for refinement events. We can consider later
   * on to merge the time stepping functionality
   * (solution update, predictor comp.) into
   * this hook.
   */
  virtual bool updateStateInLeaveCell(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const int solverNumber) = 0;

  /**
   * Returns true if the solver has attained
   * a stable state on the cell description
   *
   * \param fineGridCell a fine grid cell
   * \param fineGridVertices vertices surrounding the fine grid cell
   * \param fineGridVerticesEnumerator a enumerator for the fine grid vertices
   * \param solverNumber a solver number
   */
  virtual bool attainedStableState(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      const int solverNumber) const = 0;

  /**
   * This method is called after the
   * mesh refinement iterations where this
   * solver performs states updates in
   * the enterCell() and leaveCell().
   *
   * This method is used to finalise some state
   * updates.
   *
   * TODO(Dominic): Docu
   */
  virtual void finaliseStateUpdates(
      exahype::Cell& fineGridCell,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator,
      exahype::Cell& coarseGridCell,
      exahype::Vertex* const coarseGridVertices,
      const peano::grid::VertexEnumerator& coarseGridVerticesEnumerator,
      const tarch::la::Vector<DIMENSIONS, int>& fineGridPositionOfCell,
      const int solverNumber) = 0;

  /////////////////////////////////////
  // CELL-LOCAL
  /////////////////////////////////////
  /**
   * Evaluate the refinement criterion after
   * a solution update has performed.
   *
   * We currently only return RefinementType::APrioriRefinement (or
   * RefinementType::APosterrioriRefinement)
   * if a cell requested refinement.
   * ExaHyPE might then stop the
   * time stepping and update the mesh
   * before continuing. Erasing is here not considered.
   *
   * \return RefinementType::APrioriRefinement (or
   * RefinementType::APosterrioriRefinement) if a mesh update is necessary.
   */
  virtual bool evaluateRefinementCriterionAfterSolutionUpdate(
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
   * Zeroes all the time step sizes.
   * This method is used by the adaptive mesh refinement mapping.
   * After the mesh refinement, we need to recompute
   * the time step sizes.
   *
   * <h1>ADER-DG</h1>
   * Further resets the predictor time stamp to take
   * the value of the corrector time stamp.
   * The fused must be initialised again after
   * each mesh refinement.
   */
  virtual void zeroTimeStepSizes() = 0;

  /**
   * Compute and return a new admissible time step
   * size for the cell description
   * \p element in the array at address
   * \p cellDescriptionsIndex.
   *
   * Then, update the time stamps and time step
   * sizes on the cell description accordingly.
   *
   * \return The new admissible time step size if the
   * cell description is of type Cell or
   * std::numeric_limits<double>::max().
   *
   * \note The update of the time stamps
   * and the time step sizes of the cell description
   * performed in this method can be revoked by the
   * solver's time stepping synchronisation mode.
   * If a time stepping mode like global or globalfixed
   * is chosen, then the changes made here are simply
   * overwritten. You might want to take a look
   * into method synchroniseTimeStepping().
   *
   * \note Peano only copies the calling mapping
   * for each thread. The solvers in the solver registry are
   * not copied once for each thread.
   *
   * \see startNewTimeStep(),
   *      synchroniseTimeStepping(int,int),
   *      synchroniseTimeStepping(CellDescription&)
   */
  virtual double startNewTimeStep(
      const int cellDescriptionsIndex,
      const int element,
      double*   tempEigenvalues) = 0;

  /**
   * Zeroes all the time step sizes.
   * This method is used by the adaptive mesh refinement mapping.
   * After the mesh refinement, we need to recompute
   * the time step sizes.
   *
   * <h1>ADER-DG</h1>
   * Further resets the predictor time stamp to take
   * the value of the corrector time stamp.
   * The fused must be initialised again after
   * each mesh refinement.
   *
   * \note We do not overwrite _minNextTimeStepSize or an
   * equivalent value since this would erase the time
   * step size of the fixed time stepping schemes ("globalfixed" etc.)
   */
  virtual void zeroTimeStepSizes(const int cellDescriptionsIndex, const int element) = 0;

  /**
   * Impose initial conditions.
   *
   * \note Make sure to reset neighbour merge
   * helper variables in this method call.
   */
  virtual void setInitialConditions(
      const int cellDescriptionsIndex,
      const int element,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) = 0;

  /**
   * Update the solution of a cell description.
   *
   * \note Make sure to reset neighbour merge
   * helper variables in this method call.
   */
  virtual void updateSolution(
      const int cellDescriptionsIndex,
      const int element,
      double** tempStateSizedArrays,
      double** tempUnknowns,
      exahype::Vertex* const fineGridVertices,
      const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) = 0;

  /**
     * In this method, the solver can perform post-processing
     * operations, e.g., compression of the
     * degrees of freedoms.
     *
     * \param[in] element Index of the cell description
     *                    holding the data to send out in
     *                    the array at address \p cellDescriptionsIndex.
     *                    This is not the solver number.
     *
     * \see tryGetElement
     */
    virtual void preProcess(
        const int cellDescriptionsIndex,
        const int element) = 0;

  /**
   * In this method, the solver can perform post-processing
   * operations, e.g., compression of the
   * degrees of freedoms.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array at address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   *
   * \see tryGetElement
   */
  virtual void postProcess(
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
    * Prolongates face data from a parent ADERDGCellDescription to
    * \p cellDescription if the fine grid cell associated with
    * \p cellDescription is adjacent to a boundary of the
    * coarse grid cell associated with the parent cell description.
    *
    * Further zero out the face data of ancestors.
    *
    * \note This function assumes a top-down traversal of the grid and must thus
    * be called from the enterCell(...) or descend(...) mapping methods.
    *
    * \note Calling this method makes only sense if \p cellDescription is of type
    * Descendant or Ancestor.
    */
  virtual void prolongateDataAndPrepareDataRestriction(
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
   * Restricts data to the the next parent independent of
   * the cell type of the fine grid and coarse grid cell.
   *
   * \p This operation is always surrounded by
   * a lock. No locks are required internally.
   *
   * \note This function assumes a bottom-up traversal of the grid and must thus
   * be called from the leaveCell(...) or ascend(...) mapping methods.
   */
  virtual void restrictToNextParent(
      const int fineGridCellDescriptionsIndex,
      const int fineGridElement,
      const int coarseGridCellDescriptionsIndex,
      const int coarseGridElement) = 0;

  /**
   * Restrict face data to the top most parent which has allocated face data arrays (Ancestor)
   * if and only if the fine grid cell (Cell) has a face which intersects with one of the top most parent
   * cell's faces.
   *
   * \note This function is used to restrict face data to the top most
   * parent. We skip all intermediate parents if they do not
   * need to hold data (EmptyAncestor).
   *
   * \p This operation is always surrounded by
   * a lock. No locks are required internally.
   *
   * \note This function assumes a bottom-up traversal of the grid and must thus
   * be called from the leaveCell(...) or ascend(...) mapping methods.
   */
  virtual void restrictToTopMostParent(
        const int cellDescriptionsIndex,
        const int element,
        const int parentCellDescriptionsIndex,
        const int parentElement,
        const tarch::la::Vector<DIMENSIONS,int>& subcellIndex) = 0;

  /**
   * Merge the metadata of two cell descriptions.
   *
   * \param[in] element Index of the cell description
   *            holding the data to send out in
   *            the array at address \p cellDescriptionsIndex.
   *            This is not the solver number.
   *
   * \see tryGetElement
   */
  virtual void mergeNeighboursMetadata(
        const int                                 cellDescriptionsIndex1,
        const int                                 element1,
        const int                                 cellDescriptionsIndex2,
        const int                                 element2,
        const tarch::la::Vector<DIMENSIONS, int>& pos1,
        const tarch::la::Vector<DIMENSIONS, int>& pos2) = 0;

  /**
   * Receive solver data from neighbour rank and write
   * it on the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \note Peano only copies the calling mapping
   * for each thread. The solvers in the solver registry are
   *  not copied once for each thread.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array at address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   *
   * \see tryGetElement
   *
   * <h2>Temporary variables</h2>
   * See  exahype::mappings::Merging::prepareTemporaryVariables()
   * exahype::mappings::SolutionRecomputation::prepareTemporaryVariables()
   * for details on the size of the allocation of
   * the temporary variables.
   */
  virtual void mergeNeighbours(
        const int                                 cellDescriptionsIndex1,
        const int                                 element1,
        const int                                 cellDescriptionsIndex2,
        const int                                 element2,
        const tarch::la::Vector<DIMENSIONS, int>& pos1,
        const tarch::la::Vector<DIMENSIONS, int>& pos2,
        double**                                  tempFaceUnknowns,
        double**                                  tempStateSizedVectors,
        double**                                  tempStateSizedSquareMatrices) = 0;

  /**
   * Take the cell descriptions \p element
   * from array at address \p cellDescriptionsIndex
   * and merge it with boundary data.
   *
   * \note Peano only copies the calling mapping
   * for each thread. The solvers in the solver registry are
   *  not copied once for each thread.
   *
   * \param[in] element Index of the cell description
   *                    at address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   *
   * \see tryGetElement
   *
   * <h2>Temporary variables</h2>
   * See  exahype::mappings::Merging::prepareTemporaryVariables()
   * exahype::mappings::SolutionRecomputation::prepareTemporaryVariables()
   * for details on the size of the allocation of
   * the temporary variables.
   */
  virtual void mergeWithBoundaryData(
        const int                                 cellDescriptionsIndex,
        const int                                 element,
        const tarch::la::Vector<DIMENSIONS, int>& posCell,
        const tarch::la::Vector<DIMENSIONS, int>& posBoundary,
        double**                                  tempFaceUnknowns,
        double**                                  tempStateSizedVectors,
        double**                                  tempStateSizedSquareMatrices) =0;

  /**
   * Merge cell description \p element in
   * the cell descriptions array stored at \p
   * cellDescriptionsIndex with metadata
   * stemming from the boundary or an empty cell.
   *
   * This function is used to prescribe boundary
   * conditions for the diffused limiter status,
   * augmentation, and helper status.
   *
   * Usually the boundary conditions of these
   * diffusion processes are set to the lowest
   * value in this function.
   */
  virtual void mergeWithBoundaryOrEmptyCellMetadata(
      const int cellDescriptionsIndex,
      const int element,
      const tarch::la::Vector<DIMENSIONS, int>& posCell,
      const tarch::la::Vector<DIMENSIONS, int>& posBoundaryOrEmptyCell) = 0;

  /**
   * After all merges with neighbour and boundary data have been performed for all the surrounding
   * faces of a cell (description), we prepare the neighbour merge helper variables
   * for the next neighbour merge in one of the following grid traversals.
   */
  virtual void prepareNextNeighbourMerging(
        const int cellDescriptionsIndex,
        const int element,
        exahype::Vertex* const fineGridVertices,
        const peano::grid::VertexEnumerator& fineGridVerticesEnumerator) const = 0;

  #ifdef Parallel
  /**
   * If a cell description was allocated at heap address \p cellDescriptionsIndex
   * for solver \p solverNumber, encode metadata of the cell description
   * and push it to the back of the metadata vector \p metadata.
   *
   * Otherwise, push exahype::NeighbourCommunicationMetadataPerSolver
   * times exahype::InvalidMetadataEntry to the back of the vector.
   */
  virtual void appendNeighbourCommunicationMetadata(
      MetadataHeap::HeapEntries& metadata,
      const tarch::la::Vector<DIMENSIONS,int>& src,
      const tarch::la::Vector<DIMENSIONS,int>& dest,
      const int cellDescriptionsIndex,
      const int solverNumber) = 0;

  /**
   * Merge cell description \p element in
   * the cell descriptions array stored at \p
   * cellDescriptionsIndex with metadata.
   *
   * Currently, the neighbour metadata is only the neighbour
   * type as int \p neighbourTypeAsInt and
   * the neighbour's limiter status as int.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array with address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void mergeWithNeighbourMetadata(
      const MetadataHeap::HeapEntries&  metadata,
      const tarch::la::Vector<DIMENSIONS, int>& src,
      const tarch::la::Vector<DIMENSIONS, int>& dest,
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
   * Send solver data to neighbour rank. Read the data from
   * the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \param[in] element Index of the ADERDGCellDescription
   *                    holding the data to send out in
   *                    the heap vector at \p cellDescriptionsIndex.
   *                    This is not the solver number.
   *
   * \see tryGetElement
   */
  virtual void sendDataToNeighbour(
      const int                                    toRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Send empty solver data to neighbour rank.
   */
  virtual void sendEmptyDataToNeighbour(
      const int                                    toRank,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Receive solver data from neighbour rank and write
   * it on the cell description \p element in
   * the cell descriptions array stored at \p
   * cellDescriptionsIndex.
   *
   * \note Peano only copies the calling mapping
   * for each thread. The solvers in the solver registry are
   *  not copied once for each thread.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array with address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   *
   * <h2>Temporary variables</h2>
   * See  exahype::mappings::Merging::prepareTemporaryVariables()
   * exahype::mappings::SolutionRecomputation::prepareTemporaryVariables()
   * for details on the size of the allocation of
   * the temporary variables.
   */
  virtual void mergeWithNeighbourData(
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
      const int                                    level) = 0;

  /**
   * Drop solver data from neighbour rank.
   */
  virtual void dropNeighbourData(
      const int                                    fromRank,
      const tarch::la::Vector<DIMENSIONS, int>&    src,
      const tarch::la::Vector<DIMENSIONS, int>&    dest,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Send solver data to master or worker rank. Read the data from
   * the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array with address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void sendDataToWorkerOrMasterDueToForkOrJoin(
      const int                                    toRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Send empty solver data to master or worker rank
   * due to fork or join.
   */
  virtual void sendEmptyDataToWorkerOrMasterDueToForkOrJoin(
      const int                                    toRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Merge with solver data from master or worker rank
   * that was sent out due to a fork or join. Wrote the data to
   * the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array with address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void mergeWithWorkerOrMasterDataDueToForkOrJoin(
      const int                                    fromRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Drop solver data from master or worker rank
   * that was sent out due to a fork or join.
   */
  virtual void dropWorkerOrMasterDataDueToForkOrJoin(
      const int                                    fromRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  ///////////////////////////////////
  // WORKER->MASTER
  ///////////////////////////////////

  /**
   * Determine if the solver has to reduce data for the cell description
   * \p element at heap address \p cellDescriptionsIndex.
   * This is the case if we have adaptive mesh refinement
   * activated and a cell description located at the
   * master worker boundary is of type Ancestor (not EmptyAncestor).
   * In this case the worker will restrict face data up to the
   * master in this iteration.
   *
   * Note that this function must be called in
   * prepareSendToWorker(...) if you plug it into the Peano engine.
   *
   * Note that this function is not in control of determining when to reduce
   * time step data. This should be done outside of this function.
   */
  virtual bool hasToSendDataToMaster(
      const int cellDescriptionsIndex,
      const int element) = 0;

  /**
   * Send data to the master that is not
   * depending on a particular cell description.
   *
   * This operation might be used for the reduction of a global
   * minimum time step size over all MPI ranks.
   *
   * \note We always assume that
   * startNewTimeStep() has been already called on the
   * local solver instance. You thus
   * have to return the updated local time step size.
   *
   * \see startNewTimeStep(), mergeWithWorkerData()
   */
  virtual void sendDataToMaster(
      const int                                    masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Merge with solver data from worker rank.
   */
  virtual void mergeWithWorkerData(
      const int                                    workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Compile a message containing mesh update flags
   * for the master.
   *
   * The initial capacity defaults to 2 but can be modified
   * to attend more data to the message.
   *
   * \see exahype::solvers::Solver::sendMeshUpdateFlagsToMaster,
   * exahype::solvers::LimitingADERDGSolver::sendMeshUpdateFlagsToMaster
   */
  exahype::DataHeap::HeapEntries
  compileMeshUpdateFlagsForMaster(const int capacity=2) const;

  /*
   * Send the rank-local mesh update request and
   * limiter domain change to the master.
   *
   * At the time of sending data to the master,
   * we have already set the next
   * mesh update request locally.
   * We thus need to communicate the
   * current mesh update request to the master.
   */
  void sendMeshUpdateFlagsToMaster(
      const int                                    masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level);

  /**
   * Merge with the workers mesh update flags.
   *
   * \see exahype::solvers::Solver::mergeWithWorkerMeshUpdateFlags,
   * exahype::solvers::LimitingADERDGSolver::mergeWithWorkerMeshUpdateFlags
   */
  void mergeWithWorkerMeshUpdateFlags(const DataHeap::HeapEntries& message);

  /**
   * Merge with the workers mesh update flags.
   *
   * The master has not yet performed swapped
   * the current values with the "next" values.
   * This will happen after the merge.
   */
  void mergeWithWorkerMeshUpdateFlags(
      const int                                    workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level);

  /**
   * Send solver data to master rank. Read the data from
   * the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the heap vector at \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void sendDataToMaster(
      const int                                    masterRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Send empty solver data to master rank.
   */
  virtual void sendEmptyDataToMaster(
      const int                                    masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Merge with solver data from worker rank.
   * Write the data to the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array with address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void mergeWithWorkerData(
      const int                                    workerRank,
      const MetadataHeap::HeapEntries&             workerMetadata,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Drop solver data from worker rank.
   */
  virtual void dropWorkerData(
      const int                                    workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  ///////////////////////////////////
  // MASTER->WORKER
  ///////////////////////////////////

  /**
   * If a cell description was allocated at heap address \p cellDescriptionsIndex
   * for solver \p solverNumber, encode metadata of the cell description
   * and push it to the back of the metadata vector \p metadata.
   *
   * Otherwise, push exahype::MasterWorkerCommunicationMetadataPerSolver
   * times exahype::InvalidMetadataEntry to the back of the vector.
   */
  virtual void appendMasterWorkerCommunicationMetadata(
      MetadataHeap::HeapEntries& metadata,
      const int cellDescriptionsIndex,
      const int solverNumber) = 0;

  /**
   * TODO(Dominic): docu
   */
  virtual void mergeWithMasterWorkerMetadata(
        const MetadataHeap::HeapEntries& receivedMetadata,
        const int                        cellDescriptionsIndex,
        const int                        element) = 0;

  /**
   * Send data to the worker that is not
   * depending on a particular cell description.
   *
   * This operation might be used for the synchronisation
   * of a global minimum time step size over all MPI ranks.
   *
   * \see startNewTimeStep(), mergeWithMasterData()
   */
  virtual void sendDataToWorker(
      const int                                    workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Merge with solver data from master rank.
   */
  virtual void mergeWithMasterData(
      const int                                    masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Send solver data to worker rank. Read the data from
   * the cell description \p element in the cell descriptions
   * vector stored at \p cellDescriptionsIndex.
   *
   * \param[in] element Index of the ADERDGCellDescription
   *                    holding the data to send out in
   *                    the heap vector at \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void sendDataToWorker(
      const int                                    workerRank,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Send empty solver data to worker rank.
   */
  virtual void sendEmptyDataToWorker(
      const int                                    workerRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Merge with solver data from master rank
   * that was sent out due to a fork or join. Write the data to
   * the cell description \p element in
   * the cell descriptions vector stored at \p
   * cellDescriptionsIndex.
   *
   * \param[in] element Index of the cell description
   *                    holding the data to send out in
   *                    the array with address \p cellDescriptionsIndex.
   *                    This is not the solver number.
   */
  virtual void mergeWithMasterData(
      const int                                    masterRank,
      const MetadataHeap::HeapEntries&             masterMetadata,
      const int                                    cellDescriptionsIndex,
      const int                                    element,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;

  /**
   * Drop solver data from master rank.
   */
  virtual void dropMasterData(
      const int                                    masterRank,
      const tarch::la::Vector<DIMENSIONS, double>& x,
      const int                                    level) = 0;
  #endif
};

#endif
