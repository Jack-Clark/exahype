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
 
#include "exahype/solvers/Solver.h"

#include "exahype/Cell.h"

#include "tarch/multicore/Lock.h"

#include <algorithm>
#include <mm_malloc.h> //g++
#include <cstring> //memset

#include "LimitingADERDGSolver.h"
#include "ADERDGSolver.h"
#include "FiniteVolumesSolver.h"

std::vector<exahype::solvers::Solver*> exahype::solvers::RegisteredSolvers;

const int exahype::solvers::Solver::NotFound = -1;


void exahype::solvers::initialiseSolverFlags(exahype::solvers::SolverFlags& solverFlags) {
  assertion(solverFlags._limiterDomainHasChanged==nullptr);
  assertion(solverFlags._gridUpdateRequested    ==nullptr);

  int numberOfSolvers    = exahype::solvers::RegisteredSolvers.size();
  solverFlags._limiterDomainHasChanged = new bool[numberOfSolvers];
  solverFlags._gridUpdateRequested     = new bool[numberOfSolvers];
}

void exahype::solvers::prepareSolverFlags(exahype::solvers::SolverFlags& solverFlags) {
  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    solverFlags._limiterDomainHasChanged[solverNumber] = false;
  }

  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    assertion(!exahype::solvers::RegisteredSolvers[solverNumber]->getNextGridUpdateRequested());
    solverFlags._gridUpdateRequested[solverNumber] = false;
  }
}

void exahype::solvers::deleteSolverFlags(exahype::solvers::SolverFlags& solverFlags) {
  if (solverFlags._limiterDomainHasChanged!=nullptr) {
    assertion(solverFlags._limiterDomainHasChanged!=nullptr);
    assertion(solverFlags._gridUpdateRequested    !=nullptr);

    delete[] solverFlags._limiterDomainHasChanged;
    delete[] solverFlags._gridUpdateRequested;
    solverFlags._limiterDomainHasChanged = nullptr;
    solverFlags._gridUpdateRequested     = nullptr;
  }
}


tarch::multicore::BooleanSemaphore exahype::solvers::Solver::_heapSemaphore;
int                                exahype::solvers::Solver::_NumberOfTriggeredTasks(0);

void exahype::solvers::Solver::waitUntilAllBackgroundTasksHaveTerminated() {
  bool finishedWait = false;

  while (!finishedWait) {
    tarch::multicore::Lock lock(_heapSemaphore);
    finishedWait = _NumberOfTriggeredTasks == 0;
    lock.free();

    tarch::multicore::BooleanSemaphore::sendTaskToBack();
  }
}





exahype::solvers::Solver::Solver(
  const std::string&                     identifier,
  exahype::solvers::Solver::Type         type,
  int                                    numberOfVariables,
  int                                    numberOfParameters,
  int                                    nodesPerCoordinateAxis,
  double                                 maximumMeshSize,
  exahype::solvers::Solver::TimeStepping timeStepping,
  std::unique_ptr<profilers::Profiler>   profiler
  ):  _identifier(identifier),
      _type(type),
      _numberOfVariables(numberOfVariables),
      _numberOfParameters(numberOfParameters),
      _nodesPerCoordinateAxis(nodesPerCoordinateAxis),
      _maximumMeshSize(maximumMeshSize),
      _minCellSize(std::numeric_limits<double>::max()),
      _nextMinCellSize(std::numeric_limits<double>::max()),
      _maxCellSize(-std::numeric_limits<double>::max()), // "-", min
      _nextMaxCellSize(-std::numeric_limits<double>::max()), // "-", min
      _timeStepping(timeStepping),
      _profiler(std::move(profiler)),
      _gridUpdateRequested(false),
      _nextGridUpdateRequested(false) { }


std::string exahype::solvers::Solver::getIdentifier() const {
  return _identifier;
}

std::string exahype::solvers::Solver::toString(const exahype::solvers::Solver::Type& param) {
  switch (param) {
    case Type::ADERDG:        return "ADER-DG";
    case Type::FiniteVolumes:  return "Finite Volumes";
    case Type::LimitingADERDG: return "Limiting ADER-DG";
  }
  return "undefined";
}

std::string exahype::solvers::Solver::toString(const exahype::solvers::Solver::TimeStepping& param) {
  switch (param) {
    case TimeStepping::Global:      return "global";
    case TimeStepping::GlobalFixed: return "globalfixed";
  }
  return "undefined";
}

exahype::solvers::Solver::Type exahype::solvers::Solver::getType() const {
  return _type;
}

exahype::solvers::Solver::TimeStepping exahype::solvers::Solver::getTimeStepping() const {
  return _timeStepping;
}

int exahype::solvers::Solver::getNumberOfVariables() const {
  return _numberOfVariables;
}

int exahype::solvers::Solver::getNumberOfParameters() const {
  return _numberOfParameters;
}

int exahype::solvers::Solver::getNodesPerCoordinateAxis() const {
  return _nodesPerCoordinateAxis;
}

double exahype::solvers::Solver::getMaximumMeshSize() const {
  return _maximumMeshSize;
}

void exahype::solvers::Solver::resetGridUpdateRequestedFlags() {
  _gridUpdateRequested     = false;
  _nextGridUpdateRequested = false;
}

void exahype::solvers::Solver::updateNextGridUpdateRequested(bool nextGridUpdateRequested) {
  _nextGridUpdateRequested |= nextGridUpdateRequested;
}


bool exahype::solvers::Solver::getNextGridUpdateRequested() const {
  return _nextGridUpdateRequested;
}


bool exahype::solvers::Solver::getGridUpdateRequested() const {
  return _gridUpdateRequested;
}

void exahype::solvers::Solver::setNextGridUpdateRequested() {
  _gridUpdateRequested     = _nextGridUpdateRequested;
  _nextGridUpdateRequested = false;
}


double exahype::solvers::Solver::getMinSolverTimeStampOfAllSolvers() {
  double currentMinTimeStamp = std::numeric_limits<double>::max();

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    currentMinTimeStamp =
        std::min(currentMinTimeStamp, p->getMinTimeStamp());
  }
  return currentMinTimeStamp;
}

double exahype::solvers::Solver::estimateMinNextSolverTimeStampOfAllSolvers() {
  double currentMinTimeStamp = std::numeric_limits<double>::max();

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    currentMinTimeStamp =
        std::min(currentMinTimeStamp, p->getMinTimeStamp()+p->getMinTimeStepSize());
  }
  return currentMinTimeStamp;
}

double exahype::solvers::Solver::getMinSolverTimeStepSizeOfAllSolvers() {
  double currentMinTimeStepSize = std::numeric_limits<double>::max();

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    currentMinTimeStepSize =
        std::min(currentMinTimeStepSize, p->getMinTimeStepSize());
  }

  return currentMinTimeStepSize;
}


double exahype::solvers::Solver::getMaxSolverTimeStampOfAllSolvers() {
  double currentMaxTimeStamp = -std::numeric_limits<double>::max(); // "-", min

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    currentMaxTimeStamp =
        std::max(currentMaxTimeStamp, p->getMinTimeStamp());
  }

  return currentMaxTimeStamp;
}


bool exahype::solvers::Solver::allSolversUseTimeSteppingScheme(solvers::Solver::TimeStepping scheme) {
  bool result = true;

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    result &= p->_timeStepping==scheme;
  }

  return result;
}

// TODO(Dominic): This does exactly the opposite
double exahype::solvers::Solver::getCoarsestMeshSizeOfAllSolvers() {
  double result = std::numeric_limits<double>::max();

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    result = std::min( result, p->_maximumMeshSize );
  }

  return result;
}

// TODO(Dominic): This does exactly the opposite
double exahype::solvers::Solver::getFinestMaximumMeshSizeOfAllSolvers() {
  double result = -std::numeric_limits<double>::max(); // "-", min

  for (const auto& p : exahype::solvers::RegisteredSolvers) {
    result = std::max( result, p->_maximumMeshSize );
  }

  return result;
}

int exahype::solvers::Solver::getMaxAdaptiveRefinementDepthOfAllSolvers() {
  int maxDepth = 0;

  for (auto solver : exahype::solvers::RegisteredSolvers) {
    assertion1(solver->getMaxCellSize()>0,solver->getMaxCellSize());
    assertion1(solver->getMinCellSize()>0,solver->getMinCellSize());

    maxDepth =  std::max (
        maxDepth,
        tarch::la::round(
            std::log(solver->getMaxCellSize()/solver->getMinCellSize())/std::log(3)));
  }

  assertion1(maxDepth>=0,maxDepth);
  return maxDepth;
}

bool exahype::solvers::Solver::oneSolverRequestedGridUpdate() {
  for (auto* solver : exahype::solvers::RegisteredSolvers) {
    if (solver->getGridUpdateRequested()) {
      return true;
    }
  }
  return false;
}

std::string exahype::solvers::Solver::toString() const {
  std::ostringstream stringstr;
  toString(stringstr);
  return stringstr.str();
}

void exahype::solvers::Solver::toString(std::ostream& out) const {
  out << "(";
  out << "_identifier:" << _identifier;
  out << ",";
  out << "_type:" << toString(_type);
  out << ",";
  out << "_numberOfVariables:" << _numberOfVariables;
  out << ",";
  out << "_numberOfParameters:" << _numberOfParameters;
  out << ",";
  out << "_nodesPerCoordinateAxis:" << _nodesPerCoordinateAxis;
  out << ",";
  out << "_maximumMeshSize:" << _maximumMeshSize;
  out << ",";
  out << "_timeStepping:" << toString(_timeStepping);
  out <<  ")";
}

#ifdef Parallel
void exahype::solvers::Solver::sendGridUpdateFlagsToMaster(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level){
  std::vector<double> meshRefinementFlags(0,1);
  meshRefinementFlags.push_back(_gridUpdateRequested ? 1.0 : -1.0); // TODO(Dominic): ugly

  assertion1(meshRefinementFlags.size()==1,meshRefinementFlags.size());

  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
//    logDebug("sendDataToMaster(...)","Sending time step data: " <<
//             "data[0]=" << meshRefinementFlags[0]); TODO(Dominic):
  }

  DataHeap::getInstance().sendData(
      meshRefinementFlags.data(), meshRefinementFlags.size(),
      masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::Solver::mergeWithWorkerGridUpdateFlags(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  std::vector<double> receivedTimeStepData(1);

  if (true || tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
//    logDebug("mergeWithWorkerData(...)","Receiving grid update flags [pre] from rank " << workerRank); TODO(Dominic):
  }

  DataHeap::getInstance().receiveData(
      receivedTimeStepData.data(),receivedTimeStepData.size(),workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);

  assertion1(receivedTimeStepData.size()==1,receivedTimeStepData.size());

  int index=0;
  _nextGridUpdateRequested |= ( receivedTimeStepData[index++] > 0 ) ? true : false;

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
//    logDebug("mergeWithWorkerData(...)","Received grid update flags: " <<
//             "data[0]=" << receivedTimeStepData[0]);
//
//    logDebug("mergeWithWorkerData(...)","Updated grid update flags: " <<
//             "_nextGridUpdateRequested=" << _nextGridUpdateRequested); TODO(Dominic):
  }
}
#endif
