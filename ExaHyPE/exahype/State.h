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
  records::State::MergeMode getMergeMode() const;

  /**
   * Return the send mode that is currently active.
   */
  records::State::SendMode getSendMode() const;

  /**
   * Merging and Sending contexts.
   * See both mappings for more details.
   */
  void switchToInitialConditionAndTimeStepSizeComputationContext();

  void switchToPredictionAndFusedTimeSteppingInitialisationContext();

  void switchToADERDGTimeStepContext();

  void switchToPredictionRerunContext();

  void switchToNeighbourDataMergingContext();

  void switchToPredictionContext();

  void switchToSolutionUpdateContext();

  void switchToTimeStepSizeComputationContext();

  void switchToPreAMRContext();

  void switchToPostAMRContext();

  /**
   * Merge and synchronise the time step sizes over different
   * ranks.
   *
   * TODO Time step size merging might not be necessary.
   */
  void switchToLimiterStatusSpreadingContext();

  /**
   * Additionally drop face data.
   *
   * Merge and synchronise the time step sizes over different
   * ranks.
   *
   * TODO Time step size merging might not be necessary.
   */
  void switchToLimiterStatusSpreadingFusedTimeSteppingContext();

  void switchToReinitialisationContext();

  void switchToRecomputeSolutionAndTimeStepSizeComputationContext();

  void switchToRecomputeSolutionAndTimeStepSizeComputationFusedTimeSteppingContext();

  void setStabilityConditionOfOneSolverWasViolated(bool state) ;

  bool stabilityConditionOfOneSolverWasViolated() const;

  void setReinitTimeStepData(bool state);

  bool reinitTimeStepData() const;

  /**
   * Indicates that the fused time stepping
   * scheme is used in the runner
   * instead of the standard time stepping.
   */
  static bool fuseADERDGPhases();

  void setTimeStepSizeWeightForPredictionRerun(double value);

  double getTimeStepSizeWeightForPredictionRerun() const;

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
