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

void exahype::solvers::initialiseTemporaryVariables(exahype::solvers::PredictionTemporaryVariables& temporaryVariables) {
  assertion(temporaryVariables._tempSpaceTimeUnknowns    ==nullptr);
  assertion(temporaryVariables._tempSpaceTimeFluxUnknowns==nullptr);
  assertion(temporaryVariables._tempUnknowns             ==nullptr);
  assertion(temporaryVariables._tempFluxUnknowns         ==nullptr);
  assertion(temporaryVariables._tempStateSizedVectors    ==nullptr);
  assertion(temporaryVariables._tempPointForceSources    ==nullptr);

  int numberOfSolvers        = exahype::solvers::RegisteredSolvers.size();
  temporaryVariables._tempSpaceTimeUnknowns     = new double**[numberOfSolvers]; // == lQi, lQi_old, rhs, rhs_0 (unchanged by optimisation)
  temporaryVariables._tempSpaceTimeFluxUnknowns = new double**[numberOfSolvers]; // == lFi, gradQ
  temporaryVariables._tempUnknowns              = new double* [numberOfSolvers]; // == lQhi
  temporaryVariables._tempFluxUnknowns          = new double* [numberOfSolvers]; // == lFhi
  temporaryVariables._tempStateSizedVectors     = new double* [numberOfSolvers]; // == BGradQ
  temporaryVariables._tempPointForceSources     = new double* [numberOfSolvers];

  exahype::solvers::ADERDGSolver* aderdgSolver = nullptr;

  int solverNumber=0;
  for (auto solver : exahype::solvers::RegisteredSolvers) {
    switch( solver->getType() ) {
    case exahype::solvers::Solver::Type::ADERDG:
      aderdgSolver = static_cast<exahype::solvers::ADERDGSolver*>(solver);
      break;
    case exahype::solvers::Solver::Type::LimitingADERDG:
      aderdgSolver = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver().get();
      break;
    default:
      aderdgSolver = nullptr;
      break;
    }

    if (aderdgSolver!=nullptr) {
      if(aderdgSolver->alignTempArray()) {
        temporaryVariables._tempSpaceTimeUnknowns[solverNumber] = new double*[4];
        for (int i=0; i<4; ++i) { // max; see spaceTimePredictorNonlinear
          temporaryVariables._tempSpaceTimeUnknowns[solverNumber][i] =
              (double *) _mm_malloc(sizeof(double)*aderdgSolver->getTempSpaceTimeUnknownsSize(), ALIGNMENT);
          std::memset(temporaryVariables._tempSpaceTimeUnknowns[solverNumber][i], 0, sizeof(double)*aderdgSolver->getTempSpaceTimeUnknownsSize());
        }
        //
        temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber] = new double*[2];
        for (int i=0; i<2; ++i) { // max; see spaceTimePredictorNonlinear
          temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber][i] =
              (double *) _mm_malloc(sizeof(double)*aderdgSolver->getTempSpaceTimeFluxUnknownsSize(), ALIGNMENT);
          std::memset(temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber][i], 0, sizeof(double)*aderdgSolver->getTempSpaceTimeFluxUnknownsSize());
        }
        //
        temporaryVariables._tempUnknowns    [solverNumber]      = (double *) _mm_malloc(sizeof(double)*aderdgSolver->getTempUnknownsSize(), ALIGNMENT);
        //
        temporaryVariables._tempFluxUnknowns[solverNumber]      = (double *) _mm_malloc(sizeof(double)*aderdgSolver->getTempFluxUnknownsSize(), ALIGNMENT);
         //
        temporaryVariables._tempStateSizedVectors[solverNumber] = (double *) _mm_malloc(sizeof(double)*aderdgSolver->getTempStateSizedVectorsSize(), ALIGNMENT);

        if(aderdgSolver->usePointSource()) { //TODO KD
          temporaryVariables._tempPointForceSources    [solverNumber] = (double *) _mm_malloc(sizeof(double)*aderdgSolver->getTempSpaceTimeUnknownsSize(), ALIGNMENT);
        } else {
          temporaryVariables._tempPointForceSources    [solverNumber] = nullptr;
        }
      } else {
        temporaryVariables._tempSpaceTimeUnknowns[solverNumber] = new double*[4];
        for (int i=0; i<4; ++i) { // max; see spaceTimePredictorNonlinear
          temporaryVariables._tempSpaceTimeUnknowns[solverNumber][i] =
              new double[aderdgSolver->getTempSpaceTimeUnknownsSize()]();
        }
        //
        temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber] = new double*[2];
        for (int i=0; i<2; ++i) { // max; see spaceTimePredictorNonlinear
          temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber][i] =
              new double[aderdgSolver->getTempSpaceTimeFluxUnknownsSize()]();
        }
        //
        temporaryVariables._tempUnknowns    [solverNumber]      = new double[aderdgSolver->getTempUnknownsSize()];
        //
        temporaryVariables._tempFluxUnknowns[solverNumber]      = new double[aderdgSolver->getTempFluxUnknownsSize()];
         //
        temporaryVariables._tempStateSizedVectors[solverNumber] = new double[aderdgSolver->getTempStateSizedVectorsSize()];

        if(aderdgSolver->usePointSource()) { //TODO KD
          temporaryVariables._tempPointForceSources    [solverNumber] = new double[aderdgSolver->getTempSpaceTimeUnknownsSize()];
        } else {
          temporaryVariables._tempPointForceSources    [solverNumber] = nullptr;
        }
      }
    } else {
      temporaryVariables._tempSpaceTimeUnknowns    [solverNumber] = nullptr;
      temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber] = nullptr;
      temporaryVariables._tempUnknowns             [solverNumber] = nullptr;
      temporaryVariables._tempFluxUnknowns         [solverNumber] = nullptr;
      temporaryVariables._tempStateSizedVectors    [solverNumber] = nullptr;
      temporaryVariables._tempPointForceSources    [solverNumber] = nullptr;
    }

    ++solverNumber;
  }
}

void exahype::solvers::deleteTemporaryVariables(exahype::solvers::PredictionTemporaryVariables& temporaryVariables) {
  if (temporaryVariables._tempSpaceTimeUnknowns!=nullptr) {
    assertion(temporaryVariables._tempSpaceTimeFluxUnknowns!=nullptr);
    assertion(temporaryVariables._tempUnknowns             !=nullptr);
    assertion(temporaryVariables._tempFluxUnknowns         !=nullptr);
    assertion(temporaryVariables._tempStateSizedVectors    !=nullptr);
    assertion(temporaryVariables._tempPointForceSources    !=nullptr);

    int solverNumber=0;
    exahype::solvers::ADERDGSolver* aderdgSolver = nullptr;

    for (auto solver : exahype::solvers::RegisteredSolvers) {
      switch( solver->getType() ) {
      case exahype::solvers::Solver::Type::ADERDG:
        aderdgSolver = static_cast<exahype::solvers::ADERDGSolver*>(solver);
        break;
      case exahype::solvers::Solver::Type::LimitingADERDG:
        aderdgSolver = static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver().get();
        break;
      default:
        aderdgSolver = nullptr;
        break;
      }

      if (aderdgSolver!=nullptr) {
        if(aderdgSolver->alignTempArray()) {
          //
          for (int i=0; i<4; ++i) {
            _mm_free(temporaryVariables._tempSpaceTimeUnknowns[solverNumber][i]);
          }
          delete[] temporaryVariables._tempSpaceTimeUnknowns[solverNumber];
          temporaryVariables._tempSpaceTimeUnknowns[solverNumber] = nullptr;
          //
          for (int i=0; i<2; ++i) {
            _mm_free(temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber][i]);
          }
          delete[] temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber];
          temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber] = nullptr;
          //
          _mm_free(temporaryVariables._tempUnknowns[solverNumber]);
          temporaryVariables._tempUnknowns[solverNumber] = nullptr;
          //
          _mm_free(temporaryVariables._tempFluxUnknowns[solverNumber]);
          temporaryVariables._tempFluxUnknowns[solverNumber] = nullptr;
          //
          _mm_free(temporaryVariables._tempStateSizedVectors[solverNumber]);
          temporaryVariables._tempStateSizedVectors[solverNumber] = nullptr;

          if(aderdgSolver->usePointSource()) { //TODO KD
            _mm_free(temporaryVariables._tempPointForceSources[solverNumber]);
            temporaryVariables._tempPointForceSources[solverNumber] = nullptr;
          }
        } else {
          //
          for (int i=0; i<4; ++i) {
            delete[] temporaryVariables._tempSpaceTimeUnknowns[solverNumber][i];
          }
          delete[] temporaryVariables._tempSpaceTimeUnknowns[solverNumber];
          temporaryVariables._tempSpaceTimeUnknowns[solverNumber] = nullptr;
          //
          for (int i=0; i<2; ++i) {
            delete[] temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber][i];
          }
          delete[] temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber];
          temporaryVariables._tempSpaceTimeFluxUnknowns[solverNumber] = nullptr;
          //
          delete[] temporaryVariables._tempUnknowns[solverNumber];
          temporaryVariables._tempUnknowns[solverNumber] = nullptr;
          //
          delete[] temporaryVariables._tempFluxUnknowns[solverNumber];
          temporaryVariables._tempFluxUnknowns[solverNumber] = nullptr;
          //
          delete[] temporaryVariables._tempStateSizedVectors[solverNumber];
          temporaryVariables._tempStateSizedVectors[solverNumber] = nullptr;

          if(aderdgSolver->usePointSource()) { //TODO KD
            delete[] temporaryVariables._tempPointForceSources[solverNumber];
            temporaryVariables._tempPointForceSources[solverNumber] = nullptr;
          }
        }

      }

      ++solverNumber;
    }

    delete[] temporaryVariables._tempSpaceTimeUnknowns;
    delete[] temporaryVariables._tempSpaceTimeFluxUnknowns;
    delete[] temporaryVariables._tempUnknowns;
    delete[] temporaryVariables._tempFluxUnknowns;
    delete[] temporaryVariables._tempStateSizedVectors;
    delete[] temporaryVariables._tempPointForceSources;
    temporaryVariables._tempSpaceTimeUnknowns     = nullptr;
    temporaryVariables._tempSpaceTimeFluxUnknowns = nullptr;
    temporaryVariables._tempUnknowns              = nullptr;
    temporaryVariables._tempFluxUnknowns          = nullptr;
    temporaryVariables._tempStateSizedVectors     = nullptr;
    temporaryVariables._tempPointForceSources     = nullptr;
  }
}

void exahype::solvers::initialiseTemporaryVariables(exahype::solvers::MergingTemporaryVariables& temporaryVariables) {
  assertion(temporaryVariables._tempStateSizedVectors       ==nullptr);
  assertion(temporaryVariables._tempStateSizedSquareMatrices==nullptr);
  assertion(temporaryVariables._tempFaceUnknowns            ==nullptr);

  int numberOfSolvers = exahype::solvers::RegisteredSolvers.size();
  temporaryVariables._tempStateSizedVectors        = new double**[numberOfSolvers];
  temporaryVariables._tempStateSizedSquareMatrices = new double**[numberOfSolvers];
  temporaryVariables._tempFaceUnknowns             = new double**[numberOfSolvers];
//    _tempSpaceTimeFaceUnknownsArray = new double* [numberOfSolvers]; todo

  int solverNumber=0;
  for (auto solver : exahype::solvers::RegisteredSolvers) {
    int numberOfStateSizedVectors  = 0;
    int numberOfStateSizedMatrices = 0;
    int numberOfFaceUnknowns       = 0;

    int lengthOfStateSizedVectors  = 0;
    int lengthOfFaceUnknowns       = 0;

    switch (solver->getType()) {
      case exahype::solvers::Solver::Type::ADERDG:
        numberOfStateSizedVectors  = 6; // See riemannSolverNonlinear
        numberOfStateSizedMatrices = 3; // See riemannSolverLinear
        numberOfFaceUnknowns       = 3; // See exahype::solvers::ADERDGSolver::applyBoundaryConditions
        lengthOfFaceUnknowns       =
            static_cast<exahype::solvers::ADERDGSolver*>(solver)->getBndFaceSize(); // == getDataPerFace() + eventual padding
        lengthOfStateSizedVectors       =
             static_cast<exahype::solvers::ADERDGSolver*>(solver)->getTempStateSizedVectorsSize(); // variables + parameters
        break;
      case exahype::solvers::Solver::Type::LimitingADERDG:
        // Needs the same temporary variables as the normal ADER-DG scheme.
        numberOfStateSizedVectors  = 6;
        numberOfStateSizedMatrices = 3;
        numberOfFaceUnknowns       = 3;
        lengthOfFaceUnknowns       = std::max(
            static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver()->getBndFaceSize(), // == getDataPerFace() + eventual padding
            static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getLimiter()->getUnknownsPerFace() );
        lengthOfStateSizedVectors       =
             static_cast<exahype::solvers::LimitingADERDGSolver*>(solver)->getSolver()->getTempStateSizedVectorsSize(); // variables + parameters; same for both solvers
        break;
      case exahype::solvers::Solver::Type::FiniteVolumes:
        numberOfFaceUnknowns = 2; // See exahype::solvers::FiniteVolumesSolver::mergeWithBoundaryData
        lengthOfFaceUnknowns =
            static_cast<exahype::solvers::FiniteVolumesSolver*>(solver)->getUnknownsPerFace();
        lengthOfStateSizedVectors       =
            static_cast<exahype::solvers::FiniteVolumesSolver*>(solver)->getTempStateSizedVectorsSize(); // variables + parameters
        break;
    }

    temporaryVariables._tempStateSizedVectors[solverNumber] = nullptr;
    if (numberOfStateSizedVectors>0) {
      temporaryVariables._tempStateSizedVectors[solverNumber] = new double*[numberOfStateSizedVectors];
      temporaryVariables._tempStateSizedVectors[solverNumber][0] = new double[numberOfStateSizedVectors * lengthOfStateSizedVectors];
      for (int i=1; i<numberOfStateSizedVectors; ++i) { // see riemanSolverLinear
        temporaryVariables._tempStateSizedVectors[solverNumber][i] = temporaryVariables._tempStateSizedVectors[solverNumber][i-1] + lengthOfStateSizedVectors;
      }
    }
    //
    temporaryVariables._tempStateSizedSquareMatrices[solverNumber] = nullptr;
    if (numberOfStateSizedMatrices>0) {
      temporaryVariables._tempStateSizedSquareMatrices[solverNumber] = new double*[numberOfStateSizedMatrices];
      temporaryVariables._tempStateSizedSquareMatrices[solverNumber][0] =
          new double[numberOfStateSizedMatrices* solver->getNumberOfVariables() * solver->getNumberOfVariables()];
      for (int i=1; i<numberOfStateSizedMatrices; ++i) { // see riemanSolverLinear
        temporaryVariables._tempStateSizedSquareMatrices[solverNumber][i] =
            temporaryVariables. _tempStateSizedSquareMatrices[solverNumber][i-1] +
            solver->getNumberOfVariables() * solver->getNumberOfVariables();
      }
    }
    //
    temporaryVariables._tempFaceUnknowns[solverNumber] = nullptr;
    if (numberOfFaceUnknowns>0) {
      temporaryVariables._tempFaceUnknowns[solverNumber] = new double*[numberOfFaceUnknowns];
      temporaryVariables._tempFaceUnknowns[solverNumber][0] = new double[numberOfFaceUnknowns*lengthOfFaceUnknowns](); //initialized to 0 to ensure padding is initialized if existing
      for (int i=1; i<numberOfFaceUnknowns; ++i) { // see ADERDGSolver::applyBoundaryConditions(...)
        temporaryVariables._tempFaceUnknowns[solverNumber][i] = temporaryVariables._tempFaceUnknowns[solverNumber][i-1] + lengthOfFaceUnknowns;
      }
    }

    ++solverNumber;
  }
}

void exahype::solvers::deleteTemporaryVariables(exahype::solvers::MergingTemporaryVariables& temporaryVariables) {
  if (temporaryVariables._tempStateSizedVectors!=nullptr) {
    assertion(temporaryVariables._tempStateSizedSquareMatrices!=nullptr);

    int solverNumber=0;
    for (auto solver : exahype::solvers::RegisteredSolvers) {
      int numberOfStateSizedVectors  = 0;
      int numberOfStateSizedMatrices = 0;
      int numberOfFaceUnknowns       = 0;
      switch (solver->getType()) {
        case exahype::solvers::Solver::Type::ADERDG:
        case exahype::solvers::Solver::Type::LimitingADERDG:
          numberOfStateSizedVectors  = 6; // See riemannSolverLinear
          numberOfStateSizedMatrices = 3; // See riemannSolverLinear
          numberOfFaceUnknowns       = 3; // See exahype::solvers::ADERDGSolver::applyBoundaryConditions
          break;
        case exahype::solvers::Solver::Type::FiniteVolumes:
          numberOfFaceUnknowns       = 2; // See exahype::solvers::FiniteVolumesSolver::mergeWithBoundaryData
          break;
      }

      if (numberOfStateSizedVectors>0) {
        delete[] temporaryVariables._tempStateSizedVectors[solverNumber][0];
        delete[] temporaryVariables._tempStateSizedVectors[solverNumber];
        temporaryVariables._tempStateSizedVectors[solverNumber] = nullptr;
      }
      //
      if (numberOfStateSizedMatrices>0) {
        delete[] temporaryVariables._tempStateSizedSquareMatrices[solverNumber][0];
        delete[] temporaryVariables._tempStateSizedSquareMatrices[solverNumber];
        temporaryVariables._tempStateSizedSquareMatrices[solverNumber] = nullptr;
      }
      //
      if (numberOfFaceUnknowns>0) {
        delete[] temporaryVariables._tempFaceUnknowns[solverNumber][0];
        delete[] temporaryVariables._tempFaceUnknowns[solverNumber];
        temporaryVariables._tempFaceUnknowns[solverNumber] = nullptr;
      }
      //
      // _tempSpaceTimeFaceUnknownsArray[solverNumber] = nullptr; // todo

      ++solverNumber;
    }

    delete[] temporaryVariables._tempStateSizedVectors;
    delete[] temporaryVariables._tempStateSizedSquareMatrices;
    delete[] temporaryVariables._tempFaceUnknowns;
//    delete[] _tempSpaceTimeFaceUnknownsArray; todo
    temporaryVariables._tempStateSizedVectors        = nullptr;
    temporaryVariables._tempStateSizedSquareMatrices = nullptr;
    temporaryVariables._tempFaceUnknowns             = nullptr;
//    _tempSpaceTimeFaceUnknownsArray  = nullptr; todo
  }
}

void exahype::solvers::initialiseTemporaryVariables(exahype::solvers::SolutionUpdateTemporaryVariables& temporaryVariables) {
  assertion(temporaryVariables._tempStateSizedVectors==nullptr);
  assertion(temporaryVariables._tempUnknowns         ==nullptr);

  int numberOfSolvers    = exahype::solvers::RegisteredSolvers.size();
  temporaryVariables._tempStateSizedVectors = new double**[numberOfSolvers];
  temporaryVariables._tempUnknowns          = new double**[numberOfSolvers];

  int solverNumber=0;
  for (auto solver : exahype::solvers::RegisteredSolvers) {
    if  (solver->getType()==exahype::solvers::Solver::Type::FiniteVolumes ||
        solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG) {
      const int numberOfStateSizedVectors = 1+2*DIMENSIONS; // max; see riemannSolverNonlinear(5) or kernels::finitevolumes::godunov::solutionUpdate (1+2*DIMENSIONS)
      temporaryVariables._tempStateSizedVectors[solverNumber]    = new double*[numberOfStateSizedVectors];
      temporaryVariables._tempStateSizedVectors[solverNumber][0] = new double[numberOfStateSizedVectors * solver->getNumberOfVariables()];
      for (int i=1; i<numberOfStateSizedVectors; ++i) {
        temporaryVariables._tempStateSizedVectors[solverNumber][i] = temporaryVariables._tempStateSizedVectors[solverNumber][i-1]+solver->getNumberOfVariables();
      }
      //
      // TODO(Dominic): This will change if we use a different method than a 1st order Godunov method:
      temporaryVariables._tempUnknowns[solverNumber] = nullptr;
    } else {
      temporaryVariables._tempStateSizedVectors[solverNumber] = nullptr;
      temporaryVariables._tempUnknowns         [solverNumber] = nullptr;
    }

    ++solverNumber;
  }
}

void exahype::solvers::deleteTemporaryVariables(exahype::solvers::SolutionUpdateTemporaryVariables& temporaryVariables) {
  if (temporaryVariables._tempStateSizedVectors!=nullptr) {
    assertion(temporaryVariables._tempUnknowns!=nullptr);

    int solverNumber=0;
    for (auto solver : exahype::solvers::RegisteredSolvers) {
      if (solver->getType()==exahype::solvers::Solver::Type::FiniteVolumes ||
          solver->getType()==exahype::solvers::Solver::Type::LimitingADERDG) {
        //
        delete[] temporaryVariables._tempStateSizedVectors[solverNumber][0];
        delete[] temporaryVariables._tempStateSizedVectors[solverNumber];
        temporaryVariables._tempStateSizedVectors[solverNumber] = nullptr;
        // TODO(Dominic): This will change if we use a different method than a 1st order Godunov method:
        temporaryVariables._tempUnknowns[solverNumber] = nullptr;
      }

      ++solverNumber;
    }

    delete[] temporaryVariables._tempStateSizedVectors;
    delete[] temporaryVariables._tempUnknowns;
    temporaryVariables._tempStateSizedVectors = nullptr;
    temporaryVariables._tempUnknowns          = nullptr;
  }
}


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
    logDebug("sendDataToMaster(...)","Sending time step data: " <<
             "data[0]=" << meshRefinementFlags[0]);
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
    logDebug("mergeWithWorkerData(...)","Receiving grid update flags [pre] from rank " << workerRank);
  }

  DataHeap::getInstance().receiveData(
      receivedTimeStepData.data(),receivedTimeStepData.size(),workerRank, x, level,
      peano::heap::MessageType::MasterWorkerCommunication);

  assertion1(receivedTimeStepData.size()==1,receivedTimeStepData.size());

  int index=0;
  _nextGridUpdateRequested |= ( receivedTimeStepData[index++] > 0 ) ? true : false;

  if (tarch::parallel::Node::getInstance().getRank()==
      tarch::parallel::Node::getInstance().getGlobalMasterRank()) {
    logDebug("mergeWithWorkerData(...)","Received grid update flags: " <<
             "data[0]=" << receivedTimeStepData[0]);

    logDebug("mergeWithWorkerData(...)","Updated grid update flags: " <<
             "_nextGridUpdateRequested=" << _nextGridUpdateRequested);
  }
}
#endif
