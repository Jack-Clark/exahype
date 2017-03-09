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
 
#ifndef _EXAHYPE_STATE_H_
#define _EXAHYPE_STATE_H_

#include "exahype/records/State.h"
#include "peano/grid/State.h"

#include <vector>
#include <memory>

#include "peano/grid/Checkpoint.h"

namespace exahype {
class State;

/**
 * Forward declaration
 */
class Vertex;
/**
 * Forward declaration
 */
class Cell;

namespace repositories {
  /**
   * Forward declaration
   */
  class RepositoryArrayStack;
    class RepositorySTDStack;
  }
}



/**
 * Blueprint for solver state.
 *
 * This file has originally been created by the PDT and may be manually extended
 *to
 * the needs of your application. We do not recommend to remove anything!
 */
class exahype::State : public peano::grid::State<exahype::records::State> {
 private:
  typedef class peano::grid::State<exahype::records::State> Base;

  /**
   * Needed for checkpointing.
   */
  friend class exahype::repositories::RepositoryArrayStack;
  friend class exahype::repositories::RepositorySTDStack;

  void writeToCheckpoint(
      peano::grid::Checkpoint<Vertex, Cell>& checkpoint) const;
  void readFromCheckpoint(
      const peano::grid::Checkpoint<Vertex, Cell>& checkpoint);

 public:
  static bool FuseADERDGPhases;

  /**
   * Default Constructor
   *
   * This constructor is required by the framework's data container. Do not
   * remove it.
   */
  State();

  /**
   * Constructor
   *
   * This constructor is required by the framework's data container. Do not
   * remove it. It is kind of a copy constructor that converts an object which
   * comprises solely persistent attributes into a full attribute. This very
   * functionality is implemented within the super type, i.e. this constructor
   * has to invoke the correponsing super type's constructor and not the super
   * type standard constructor.
   */
  State(const Base::PersistentState& argument);

  /**
   * Merge this state with another state
   *
   * @todo Clarify which stuff has to be merged
   */
  void merge(const State& anotherState);
  ///@}

  /**
   * Return the merge mode that is currently active.
   */
  records::State::MergeMode getMergeMode() const {
    return _stateData.getMergeMode();
  }

  /**
   * Return the send mode that is currently active.
   */
  records::State::SendMode getSendMode() const {
    return _stateData.getSendMode();
  }

  /**
   * Merging and Sending contexts.
   * See both mappings for more details.
   */
  void switchToInitialConditionAndTimeStepSizeComputationContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepData);
    _stateData.setSendMode (records::State::SendMode::ReduceAndMergeTimeStepData);
  }

  void switchToPredictionAndFusedTimeSteppingInitialisationContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepData);
    _stateData.setSendMode (records::State::SendMode::SendFaceData);
  }

  void switchToADERDGTimeStepContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepDataAndMergeFaceData);
    _stateData.setSendMode (records::State::SendMode::ReduceAndMergeTimeStepDataAndSendFaceData);
  }

  void switchToPredictionRerunContext() {
    switchToPredictionContext();
  }

  void switchToNeighbourDataMergingContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::MergeFaceData);
    _stateData.setSendMode (records::State::SendMode::SendNothing);
  }

  void switchToPredictionContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepData);
    _stateData.setSendMode (records::State::SendMode::SendFaceData);
  }

  void switchToSolutionUpdateContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::MergeNothing);
    _stateData.setSendMode (records::State::SendMode::SendNothing);
  }

  void switchToTimeStepSizeComputationContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false); // TODO(Dominic): rename
    _stateData.setMergeMode(records::State::MergeMode::MergeNothing);
    _stateData.setSendMode (records::State::SendMode::ReduceAndMergeTimeStepData);
  }

  void switchToPreAMRContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(true);
    #endif
    _stateData.setReinitTimeStepData(false); // TODO(Dominic): rename
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepData);
    _stateData.setSendMode (records::State::SendMode::SendNothing);
  }

  void switchToPostAMRContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(true);
    _stateData.setMergeMode(records::State::MergeMode::MergeNothing);
    _stateData.setSendMode (records::State::SendMode::ReduceAndMergeTimeStepData);
  }

  /**
   * Merge and synchronise the time step sizes over different
   * ranks.
   *
   * TODO Time step size merging might not be necessary.
   */
  void switchToLimiterStatusSpreadingContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepData);
    _stateData.setSendMode (records::State::SendMode::SendNothing);
  }

  /**
   * Additionally drop face data.
   *
   * Merge and synchronise the time step sizes over different
   * ranks.
   *
   * TODO Time step size merging might not be necessary.
   */
  void switchToLimiterStatusSpreadingFusedTimeSteppingContext() {
#ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
#endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::BroadcastAndMergeTimeStepData);
    _stateData.setSendMode (records::State::SendMode::SendNothing);
  }

  void switchToReinitialisationContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    // We are merging a limiter status but we do not use the merging and sending mappings. So, we can use any value here.
    _stateData.setMergeMode(records::State::MergeMode::MergeNothing);
    _stateData.setSendMode (records::State::SendMode::SendNothing);
  }

  void switchToRecomputeSolutionAndTimeStepSizeComputationContext() {
    #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::MergeNothing);
    _stateData.setSendMode (records::State::SendMode::ReduceAndMergeTimeStepData);
  }

  void switchToRecomputeSolutionAndTimeStepSizeComputationFusedTimeSteppingContext() {
   #ifdef Parallel
    _stateData.setFirstGridSetupIteration(false);
    #endif
    _stateData.setReinitTimeStepData(false);
    _stateData.setMergeMode(records::State::MergeMode::MergeNothing);
    _stateData.setSendMode (records::State::SendMode::ReduceAndMergeTimeStepDataAndSendFaceData);
  }

  //
  void setStabilityConditionOfOneSolverWasViolated(bool state) {
    _stateData.setStabilityConditionOfOneSolverWasViolated(state);
  }

  bool stabilityConditionOfOneSolverWasViolated() const {
    return _stateData.getStabilityConditionOfOneSolverWasViolated();
  }

  void setReinitTimeStepData(bool state) {
    _stateData.setReinitTimeStepData(state);
  }

  bool reinitTimeStepData() const  {
    return _stateData.getReinitTimeStepData();
  }

  /**
   * Indicates that the fused time stepping
   * scheme is used in the runner
   * instead of the standard time stepping.
   */
  static bool fuseADERDGPhases()  {
    return FuseADERDGPhases;
  }

  void setTimeStepSizeWeightForPredictionRerun(double value) {
    _stateData.setTimeStepSizeWeightForPredictionRerun(value);
  }

  double getTimeStepSizeWeightForPredictionRerun() const {
    return _stateData.getTimeStepSizeWeightForPredictionRerun();
  }

  // @todo Please remove
  #ifdef Parallel
  bool firstGridSetupIteration() const {
    return _stateData.getFirstGridSetupIteration();
  }
  void setFirstGridSetupIteration(bool state) {
    return _stateData.setFirstGridSetupIteration(state);
  }
  #endif

  /**
   * Has to be called after the iteration!
   *
   * Please consult Peano guidebook Section 6.3.2 for details.
   */
  void endedGridConstructionIteration(int finestGridLevelPossible);

  /**
   * Please consult Peano guidebook Section 6.3.2 for details.
   */
  enum RefinementAnswer {
    DontRefineYet,
    Refine,
    EnforceRefinement
  };
  RefinementAnswer mayRefine(bool isCreationalEvent, int level) const;

  /**
   * Please consult Peano guidebook Section 6.3.2 for details.
   */
  bool continueToConstructGrid() const;
};

#endif
