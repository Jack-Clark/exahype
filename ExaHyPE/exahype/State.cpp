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
 
#include "exahype/State.h"
#include "exahype/Cell.h"
#include "exahype/Vertex.h"

#include "peano/grid/Checkpoint.h"

#include "exahype/solvers/Solver.h"

#include "tarch/parallel/NodePool.h"

#include <limits>

bool exahype::State::FuseADERDGPhases = false;

exahype::State::State() : Base() {
}

exahype::State::State(const Base::PersistentState& argument) : Base(argument) {
  // do nothing
}

void exahype::State::merge(const exahype::State& anotherState) {
  // do nothing
}

void exahype::State::writeToCheckpoint(
    peano::grid::Checkpoint<exahype::Vertex, exahype::Cell>& checkpoint) const {
  // do nothing
}

void exahype::State::readFromCheckpoint(
    const peano::grid::Checkpoint<exahype::Vertex, exahype::Cell>& checkpoint) {
  // do nothing
}


void exahype::State::endedGridConstructionIteration(int finestGridLevelPossible) {
  const bool idleNodesLeft =
    tarch::parallel::NodePool::getInstance().getNumberOfIdleNodes()>0;
  const bool nodePoolHasGivenOutRankSizeLastQuery =
    tarch::parallel::NodePool::getInstance().hasGivenOutRankSizeLastQuery();

  // No more nodes left. Start to enforce refinement
  if (!idleNodesLeft && _stateData.getMaxRefinementLevelAllowed()>=0) {
    _stateData.setMaxRefinementLevelAllowed(-1);
  }
  // Refinement is enforced. So we decrease counter. Once we underrun -2, grid
  // construction can terminate as all enforced refined went through.
  else if (!idleNodesLeft && _stateData.getMaxRefinementLevelAllowed()<=-1) {
    _stateData.setMaxRefinementLevelAllowed(
      _stateData.getMaxRefinementLevelAllowed()-1 );
  }
  // Seems that max permitted level has exceeded max grid level. We may assume
  // that there are more MPI ranks than available trees.
  else if (isGridStationary() &&
    _stateData.getMaxRefinementLevelAllowed()>finestGridLevelPossible) {
    _stateData.setMaxRefinementLevelAllowed( -3 );
  }
  else if (nodePoolHasGivenOutRankSizeLastQuery &&
    _stateData.getMaxRefinementLevelAllowed()>=2) {
    _stateData.setMaxRefinementLevelAllowed(
      _stateData.getMaxRefinementLevelAllowed()-2);
  }
  // Nothing has changed in this grid iteration in the grid and we haven't
  // given out new workers. So increase the permitted maximum grid level by
  // one and give another try whether the grid adds more vertices.
  else if (
      !(nodePoolHasGivenOutRankSizeLastQuery)
    && isGridStationary()
  ) {
    _stateData.setMaxRefinementLevelAllowed(
      _stateData.getMaxRefinementLevelAllowed()+1);
  }
}


exahype::State::RefinementAnswer exahype::State::mayRefine(
  bool isCreationalEvent, int level) const
{
  #ifdef Parallel
  if (
    _stateData.getMaxRefinementLevelAllowed()<=-2
    &&
    isCreationalEvent
  ) {
    return RefinementAnswer::EnforceRefinement;
  }
  else if ( _stateData.getMaxRefinementLevelAllowed()<0 ) {
    return RefinementAnswer::Refine;
  }
  else if (
    _stateData.getMaxRefinementLevelAllowed()>level
    &&
    !isCreationalEvent
    &&
    mayForkDueToLoadBalancing()
  ) {
    return RefinementAnswer::Refine;
  }
  else {
    return RefinementAnswer::DontRefineYet;
  }
  #else
  if (isCreationalEvent) {
    return RefinementAnswer::Refine;
  }
  else {
    return RefinementAnswer::DontRefineYet;
  }
  #endif
}


bool exahype::State::continueToConstructGrid() const {
  #ifdef Parallel
  return _stateData.getMaxRefinementLevelAllowed()>=-3;
  #else
  return !isGridBalanced();
  #endif
}

