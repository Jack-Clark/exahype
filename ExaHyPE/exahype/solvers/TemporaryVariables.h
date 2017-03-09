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

#ifndef _EXAHYPE_SOLVERS_TEMPORARY_VARIABLES_H_
#define _EXAHYPE_SOLVERS_TEMPORARY_VARIABLES_H_

namespace exahype {
  namespace solvers {
    class TimeStepSizeComputationTemporaryVariables;

    /**
     * Initialises the temporary variables
     * used in the TimeStepSizeComputation mapping.
     *
     * \note We parallelise over the domain
     * (mapping is copied for each thread) and
     * over the solvers registered on a cell.
     *
     * \note We need to initialise the temporary variables
     * per mapping and not in the solvers since the
     * solvers in exahype::solvers::RegisteredSolvers
     * are not copied for every thread.
     *
     * @todo Would be nicer to make them a member of TimeStepSizeComputationTemporaryVariables
     */
    void initialiseTemporaryVariables(TimeStepSizeComputationTemporaryVariables& temporaryVariables);

    /**
     * Deletes the temporary variables
     * used in the TimeStepSizeComputation mapping.
     *
     * @todo Would be nicer to make them a member of TimeStepSizeComputationTemporaryVariables
     */
    void deleteTemporaryVariables(TimeStepSizeComputationTemporaryVariables& temporaryVariables);
  }
}



struct exahype::solvers::TimeStepSizeComputationTemporaryVariables {
  private:
    /**
     * This is a container, so it is not a good idea to copy it.
     */
    TimeStepSizeComputationTemporaryVariables( const TimeStepSizeComputationTemporaryVariables& ) {};
  public:
    TimeStepSizeComputationTemporaryVariables();
    double** _tempEigenValues = nullptr;
};


#endif
