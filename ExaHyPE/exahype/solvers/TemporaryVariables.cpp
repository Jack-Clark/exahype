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

#include "exahype/solvers/TemporaryVariables.h"
#include "exahype/solvers/Solver.h"


#include "tarch/Assertions.h"


exahype::solvers::TimeStepSizeComputationTemporaryVariables::TimeStepSizeComputationTemporaryVariables():
  _tempEigenValues( nullptr ) {
}


void exahype::solvers::initialiseTemporaryVariables(exahype::solvers::TimeStepSizeComputationTemporaryVariables& temporaryVariables) {
  int numberOfSolvers = exahype::solvers::RegisteredSolvers.size();
  if (temporaryVariables._tempEigenValues==nullptr && numberOfSolvers>0) {
    temporaryVariables._tempEigenValues = new double*[numberOfSolvers];

    int solverNumber=0;
    for (auto solver : exahype::solvers::RegisteredSolvers) {
      assertion( solver->getNumberOfVariables()>0 );
      temporaryVariables._tempEigenValues[solverNumber]  = new double[
        solver->getNumberOfVariables() + solver->getNumberOfParameters() ];
      ++solverNumber;
    }
  }
}

void exahype::solvers::deleteTemporaryVariables(exahype::solvers::TimeStepSizeComputationTemporaryVariables& temporaryVariables) {
  if(temporaryVariables._tempEigenValues!=nullptr) {
    for (unsigned int solverNumber=0;
        solverNumber < exahype::solvers::RegisteredSolvers.size(); ++solverNumber) {
      assertion( temporaryVariables._tempEigenValues[solverNumber]!=nullptr );

      delete[] temporaryVariables._tempEigenValues[solverNumber];
      temporaryVariables._tempEigenValues[solverNumber] = nullptr;
    }

    delete[] temporaryVariables._tempEigenValues;
    temporaryVariables._tempEigenValues = nullptr;
  }
}
