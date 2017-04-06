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
#ifndef _SHARED_MEMORY_LABELS_H_
#define _SHARED_MEMORY_LABELS_H_


#include "peano/datatraversal/autotuning/MethodTrace.h"


namespace sharedmemorylabels {
  const auto GenericKernelsTrivialGuessLoop                = peano::datatraversal::autotuning::MethodTrace::UserDefined20;
  const auto GenericKernelsComputeOldSolutionsImpactOnRhs  = peano::datatraversal::autotuning::MethodTrace::UserDefined21;
  const auto GenericKernelsFluxSourceNCPLoop               = peano::datatraversal::autotuning::MethodTrace::UserDefined22;
  const auto GenericKernelsCopyRhs                         = peano::datatraversal::autotuning::MethodTrace::UserDefined23;
  const auto GenericKernelsComputeDerivatives              = peano::datatraversal::autotuning::MethodTrace::UserDefined24;
  const auto GenericKernelsSourceNCP                       = peano::datatraversal::autotuning::MethodTrace::UserDefined25;
  const auto GenericKernelsDiscreteTimeIntegral            = peano::datatraversal::autotuning::MethodTrace::UserDefined26;
}

#endif
