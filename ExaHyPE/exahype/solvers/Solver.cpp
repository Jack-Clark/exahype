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

tarch::logging::Log exahype::solvers::Solver::_log( "exahype::solvers::Solver");

void exahype::solvers::initialiseSolverFlags(exahype::solvers::SolverFlags& solverFlags) {
  assertion(solverFlags._limiterDomainChange==nullptr);
  assertion(solverFlags._meshUpdateRequest  ==nullptr);

  int numberOfSolvers    = exahype::solvers::RegisteredSolvers.size();
  solverFlags._limiterDomainChange = new LimiterDomainChange[numberOfSolvers];
  solverFlags._meshUpdateRequest   = new bool               [numberOfSolvers];
}

void exahype::solvers::prepareSolverFlags(exahype::solvers::SolverFlags& solverFlags) {
  for (unsigned int solverNumber=0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    solverFlags._limiterDomainChange[solverNumber] = LimiterDomainChange::Regular;
    solverFlags._meshUpdateRequest[solverNumber]   = false;
  }
}

void exahype::solvers::deleteSolverFlags(exahype::solvers::SolverFlags& solverFlags) {
  if (solverFlags._limiterDomainChange!=nullptr) {
    assertion(solverFlags._limiterDomainChange!=nullptr);
    assertion(solverFlags._meshUpdateRequest  !=nullptr);

    delete[] solverFlags._limiterDomainChange;
    delete[] solverFlags._meshUpdateRequest;
    solverFlags._limiterDomainChange = nullptr;
    solverFlags._meshUpdateRequest   = nullptr;
  }
}

double exahype::solvers::convertToDouble(const LimiterDomainChange& limiterDomainChange) {
  return static_cast<double>(static_cast<int>(limiterDomainChange));
}

exahype::solvers::LimiterDomainChange exahype::solvers::convertToLimiterDomainChange(const double value) {
  assertion((int) std::round(value)>=static_cast<int>(LimiterDomainChange::Regular));
  assertion((int) std::round(value)<=static_cast<int>(LimiterDomainChange::IrregularRequiringMeshUpdate));
  return static_cast<LimiterDomainChange>((int) std::round(value));
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
  int                                    maximumAdaptiveMeshDepth,
  exahype::solvers::Solver::TimeStepping timeStepping,
  std::unique_ptr<profilers::Profiler>   profiler
  ):  _identifier(identifier),
      _type(type),
      _numberOfVariables(numberOfVariables),
      _numberOfParameters(numberOfParameters),
      _nodesPerCoordinateAxis(nodesPerCoordinateAxis),
      _domainOffset(std::numeric_limits<double>::max()),
      _domainSize(std::numeric_limits<double>::max()),
      _maximumMeshSize(maximumMeshSize),
      _coarsestMeshLevel(3),
      _maximumAdaptiveMeshDepth(maximumAdaptiveMeshDepth),
      _minCellSize(std::numeric_limits<double>::max()),
      _nextMinCellSize(std::numeric_limits<double>::max()),
      _maxCellSize(-std::numeric_limits<double>::max()), // "-", min
      _nextMaxCellSize(-std::numeric_limits<double>::max()), // "-", min
      _timeStepping(timeStepping),
      _profiler(std::move(profiler)),
      _meshUpdateRequest(false),
      _nextMeshUpdateRequest(false),
      _attainedStableState(false),
      _nextAttainedStableState(false){ }


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

int exahype::solvers::Solver::computeMeshLevel(double meshSize, double domainSize) {
  int    result      = 1;
  double currenthMax = std::numeric_limits<double>::max();
  while (currenthMax>meshSize) {
    currenthMax = domainSize / threePowI(result);
    result++;
  }

  return std::max(3,result);
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

double exahype::solvers::Solver::getCoarsestMeshLevel() const {
  return _coarsestMeshLevel;
}

double exahype::solvers::Solver::getMaximumAdaptiveMeshDepth() const {
  return _maximumAdaptiveMeshDepth;
}

double exahype::solvers::Solver::getMaximumAdaptiveMeshLevel() const {
  return _coarsestMeshLevel+_maximumAdaptiveMeshDepth;
}

 void exahype::solvers::Solver::updateNextMinCellSize(double minCellSize) {
  _nextMinCellSize = std::min( _nextMinCellSize, minCellSize );
}

 void exahype::solvers::Solver::updateNextMaxCellSize(double maxCellSize) {
  _nextMaxCellSize = std::max( _nextMaxCellSize, maxCellSize );
}

 double exahype::solvers::Solver::getNextMinCellSize() const {
  return _nextMinCellSize;
}

 double exahype::solvers::Solver::getNextMaxCellSize() const {
  return _nextMaxCellSize;
}

 double exahype::solvers::Solver::getMinCellSize() const {
  return _minCellSize;
}

 double exahype::solvers::Solver::getMaxCellSize() const {
  return _maxCellSize;
}

void exahype::solvers::Solver::resetMeshUpdateRequestFlags() {
  _meshUpdateRequest     = false;
  _nextMeshUpdateRequest = false;
}


void exahype::solvers::Solver::updateNextMeshUpdateRequest(
    const bool& meshUpdateRequest) {
  _nextMeshUpdateRequest |= meshUpdateRequest;
}
bool exahype::solvers::Solver::getNextMeshUpdateRequest() const {
  return _nextMeshUpdateRequest;
}
void exahype::solvers::Solver::setNextMeshUpdateRequest() {
  _meshUpdateRequest     = _nextMeshUpdateRequest;
  _nextMeshUpdateRequest = false;
}
bool exahype::solvers::Solver::getMeshUpdateRequest() const {
  return _meshUpdateRequest;
}


void exahype::solvers::Solver::updateNextAttainedStableState(
    const bool& attainedStableState) {
  _nextAttainedStableState &= attainedStableState;
}
bool exahype::solvers::Solver::getNextAttainedStableState() const {
  return _nextAttainedStableState;
}
void exahype::solvers::Solver::setNextAttainedStableState() {
  _attainedStableState     = _nextAttainedStableState;
  _nextAttainedStableState = true;
}
bool exahype::solvers::Solver::getAttainedStableState() const {
  return _attainedStableState;
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

bool exahype::solvers::Solver::oneSolverRequestedMeshUpdate() {
  for (auto* solver : exahype::solvers::RegisteredSolvers) {
    if (solver->getMeshUpdateRequest()) {
      return true;
    }
  }
  return false;
}

bool exahype::solvers::Solver::oneSolverHasNotAttainedStableState() {
  for (auto* solver : exahype::solvers::RegisteredSolvers) {
    if (!solver->getAttainedStableState()) {
      return true;
    }
  }
  return false;
}

bool exahype::solvers::Solver::stabilityConditionOfOneSolverWasViolated() {
  for (auto* solver : exahype::solvers::RegisteredSolvers) {
    switch (solver->getType()) {
      case Type::ADERDG:
        if (static_cast<ADERDGSolver*>(solver)->getStabilityConditionWasViolated())
          return true;
        break;
      case Type::LimitingADERDG:
        if (static_cast<LimitingADERDGSolver*>(solver)->getSolver().get()->
            getStabilityConditionWasViolated())
          return true;
        break;
      default:
        break;
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
exahype::MetadataHeap::HeapEntries exahype::encodeNeighbourCommunicationMetadata(
    int cellDescriptionsIndex,
    const tarch::la::Vector<DIMENSIONS,int>& src,
    const tarch::la::Vector<DIMENSIONS,int>& dest) {
  assertion1(exahype::solvers::ADERDGSolver::Heap::getInstance().isValidIndex(cellDescriptionsIndex),cellDescriptionsIndex);

  const int length =
      exahype::solvers::RegisteredSolvers.size()*exahype::NeighbourCommunicationMetadataPerSolver;
  exahype::MetadataHeap::HeapEntries encodedMetaData;
  encodedMetaData.reserve(length);

  for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

    solver->appendNeighbourCommunicationMetadata(
        encodedMetaData,
        src,dest,
        cellDescriptionsIndex,
        solverNumber);
  }
  assertion2(static_cast<int>(encodedMetaData.size())==length,encodedMetaData.size(),length);
  return encodedMetaData;
}

exahype::MetadataHeap::HeapEntries exahype::encodeMasterWorkerCommunicationMetadata(int cellDescriptionsIndex) {
  const int length =
      exahype::solvers::RegisteredSolvers.size()*exahype::MasterWorkerCommunicationMetadataPerSolver;
  exahype::MetadataHeap::HeapEntries encodedMetaData;
  encodedMetaData.reserve(length);

  for (unsigned int solverNumber = 0; solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
    auto* solver = exahype::solvers::RegisteredSolvers[solverNumber];

    solver->appendMasterWorkerCommunicationMetadata(
        encodedMetaData,cellDescriptionsIndex,solverNumber);
  }
  assertion2(static_cast<int>(encodedMetaData.size())==length,encodedMetaData.size(),length);
  return encodedMetaData;
}

exahype::MetadataHeap::HeapEntries exahype::createNeighbourCommunicationMetadataSequenceWithInvalidEntries() {
    exahype::MetadataHeap::HeapEntries encodedMetaData(
        exahype::solvers::RegisteredSolvers.size()*exahype::NeighbourCommunicationMetadataPerSolver,
        exahype::solvers::RegisteredSolvers.size()*exahype::NeighbourCommunicationMetadataPerSolver);
    std::fill_n(encodedMetaData.begin(),encodedMetaData.size(),InvalidMetadataEntry); // Implicit conversion.
    return encodedMetaData;
}

exahype::MetadataHeap::HeapEntries exahype::createMasterWorkerCommunicationMetadataSequenceWithInvalidEntries() {
    exahype::MetadataHeap::HeapEntries encodedMetaData(
        exahype::solvers::RegisteredSolvers.size()*exahype::MasterWorkerCommunicationMetadataPerSolver,
        exahype::solvers::RegisteredSolvers.size()*exahype::MasterWorkerCommunicationMetadataPerSolver);
    std::fill_n(encodedMetaData.begin(),encodedMetaData.size(),InvalidMetadataEntry); // Implicit conversion.
    return encodedMetaData;
}

bool exahype::isNeighbourCommunicationMetadataSequenceWithInvalidEntries(exahype::MetadataHeap::HeapEntries& sequence) {
   assertion(sequence.size() == exahype::solvers::RegisteredSolvers.size()*exahype::NeighbourCommunicationMetadataPerSolver);

   for (auto& m : sequence)
     if (m.getU()==exahype::InvalidMetadataEntry)
       return false;

   return true;
}

bool exahype::isMasterWorkerCommunicationMetadataSequenceWithInvalidEntries(exahype::MetadataHeap::HeapEntries& sequence) {
   assertion(sequence.size() == exahype::solvers::RegisteredSolvers.size()*exahype::MasterWorkerCommunicationMetadataPerSolver);

   for (auto& m : sequence)
     if (m.getU()==exahype::InvalidMetadataEntry)
       return false;

   return true;
}

void exahype::sendNeighbourCommunicationMetadata(
    const int                                   toRank,
    const int                                   cellDescriptionsIndex,
    const tarch::la::Vector<DIMENSIONS,int>&    src,
    const tarch::la::Vector<DIMENSIONS,int>&    dest,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  MetadataHeap::HeapEntries encodedMetadata =
      encodeNeighbourCommunicationMetadata(cellDescriptionsIndex,src,dest);
  MetadataHeap::getInstance().sendData(
      encodedMetadata,toRank,
      x,level,peano::heap::MessageType::NeighbourCommunication);
}
void exahype::sendMasterWorkerCommunicationMetadataSequenceWithInvalidEntries(
    const int                                   toRank,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  MetadataHeap::HeapEntries encodedMetadata =
      createMasterWorkerCommunicationMetadataSequenceWithInvalidEntries();
  MetadataHeap::getInstance().sendData(
      encodedMetadata,toRank,x,level,
      peano::heap::MessageType::MasterWorkerCommunication);
}
int exahype::receiveNeighbourCommunicationMetadata(
    const int                                   fromRank,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  const int receivedMetadataIndex = MetadataHeap::getInstance().createData(
      0,exahype::MasterWorkerCommunicationMetadataPerSolver*exahype::solvers::RegisteredSolvers.size());
  MetadataHeap::getInstance().receiveData(
      receivedMetadataIndex,
      fromRank, x, level,
      peano::heap::MessageType::NeighbourCommunication);
  return receivedMetadataIndex;
}


void exahype::sendMasterWorkerCommunicationMetadata(
    const int                                   toRank,
    const int                                   cellDescriptionsIndex,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  MetadataHeap::HeapEntries metadata =
      encodeMasterWorkerCommunicationMetadata(cellDescriptionsIndex);
  MetadataHeap::getInstance().sendData(
      metadata,toRank,x,level,peano::heap::MessageType::MasterWorkerCommunication);
}
void exahype::sendNeighbourCommunicationMetadataSequenceWithInvalidEntries(
    const int                                   toRank,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  MetadataHeap::HeapEntries encodedMetadata =
      createNeighbourCommunicationMetadataSequenceWithInvalidEntries();
  MetadataHeap::getInstance().sendData(
      encodedMetadata,toRank,x,level,
      peano::heap::MessageType::NeighbourCommunication);
}
int exahype::receiveMasterWorkerCommunicationMetadata(
    const int                                   fromRank,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  const int receivedMetadataIndex = MetadataHeap::getInstance().createData(
      0,exahype::MasterWorkerCommunicationMetadataPerSolver*exahype::solvers::RegisteredSolvers.size());
  MetadataHeap::getInstance().receiveData(
      receivedMetadataIndex,
      fromRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
  return receivedMetadataIndex;
}

/**
 * Drop metadata sent by rank \p fromRank.
 */
void exahype::dropMetadata(
    const int                                   fromRank,
    const peano::heap::MessageType&             messageType,
    const tarch::la::Vector<DIMENSIONS,double>& x,
    const int                                   level) {
  MetadataHeap::getInstance().receiveData(
      fromRank,x,level,messageType);
}

exahype::DataHeap::HeapEntries
exahype::solvers::Solver::compileMeshUpdateFlagsForMaster(const int capacity) const {
  DataHeap::HeapEntries meshUpdateFlags(0,std::max(2,capacity)); // !!! does not fill the vector

  meshUpdateFlags.push_back(getMeshUpdateRequest()   ? 1.0 : -1.0);
  meshUpdateFlags.push_back(getAttainedStableState() ? 1.0 : -1.0);
  return meshUpdateFlags;
}

void exahype::solvers::Solver::sendMeshUpdateFlagsToMaster(
    const int                                    masterRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level){
  DataHeap::HeapEntries meshRefinementFlags = compileMeshUpdateFlagsForMaster();

  assertion1(meshRefinementFlags.size()==2,meshRefinementFlags.size());
  if (tarch::parallel::Node::getInstance().getRank()!=
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("sendDataToMaster(...)","sending mesh update flags: " <<
             "data[0]=" << meshRefinementFlags[0] <<
             ",data[1]=" << meshRefinementFlags[1]);
  }

  DataHeap::getInstance().sendData(
      meshRefinementFlags.data(), meshRefinementFlags.size(),
      masterRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);
}

void exahype::solvers::Solver::mergeWithWorkerMeshUpdateFlags(const DataHeap::HeapEntries& message) {
  int index=0;
  updateNextMeshUpdateRequest  ( ( message[index++] > 0 ) ? true : false );
  updateNextAttainedStableState( ( message[index++] > 0 ) ? true : false );
}

void exahype::solvers::Solver::mergeWithWorkerMeshUpdateFlags(
    const int                                    workerRank,
    const tarch::la::Vector<DIMENSIONS, double>& x,
    const int                                    level) {
  DataHeap::HeapEntries messageFromWorker(2); // !!! fills the vector

  DataHeap::getInstance().receiveData(
      messageFromWorker.data(),messageFromWorker.size(),workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);

  mergeWithWorkerMeshUpdateFlags(messageFromWorker);
  assertion1(messageFromWorker.size()==2,messageFromWorker.size());

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithWorkerData(...)","received mesh update flags: " <<
             "data[0]=" << messageFromWorker[0] <<
             ",data[1]=" << messageFromWorker[1]);
  }
}
#endif
