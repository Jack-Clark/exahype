#include "exahype/repositories/RepositorySTDStack.h"

#include "tarch/Assertions.h"
#include "tarch/timing/Watch.h"

#include "tarch/compiler/CompilerSpecificSettings.h"

#include "tarch/parallel/Node.h"
#include "tarch/parallel/NodePool.h"

#ifdef Parallel
#include "peano/parallel/SendReceiveBufferPool.h"
#include "peano/parallel/loadbalancing/Oracle.h"
#endif

#include "peano/datatraversal/autotuning/Oracle.h"

#include "tarch/compiler/CompilerSpecificSettings.h"

#if !defined(CompilerICC)
#include "peano/grid/Grid.cpph"
#endif


tarch::logging::Log exahype::repositories::RepositorySTDStack::_log( "exahype::repositories::RepositorySTDStack" );


exahype::repositories::RepositorySTDStack::RepositorySTDStack(
  peano::geometry::Geometry&                   geometry,
  const tarch::la::Vector<DIMENSIONS,double>&  domainSize,
  const tarch::la::Vector<DIMENSIONS,double>&  computationalDomainOffset
):
  _geometry(geometry),
  _cellStack(),
  _vertexStack(),
  _solverState(),
  _gridWithMeshRefinement(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndFusedTimeSteppingInitialisation(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot2d(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithGridErasing(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithADERDGTimeStep(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPlotAndADERDGTimeStep(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithLimiterStatusSpreading(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithReinitialisation(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithLocalRecomputationAndTimeStepSizeComputation(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithNeighbourDataMerging(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithSolutionUpdate(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithTimeStepSizeComputation(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPrediction(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndPlot(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndPlot2d(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithFinaliseMeshRefinementAndTimeStepSizeComputation(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithMergeTimeStepData(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithMergeTimeStepDataDropFaceData(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithFinaliseMeshRefinementAndReinitialisation(_vertexStack,_cellStack,_geometry,_solverState,domainSize,computationalDomainOffset,_regularGridContainer,_traversalOrderOnTopLevel),

  _repositoryState() {
  logTraceIn( "RepositorySTDStack(...)" );
  _repositoryState.setAction( exahype::records::RepositoryState::Terminate );

  peano::datatraversal::autotuning::Oracle::getInstance().setNumberOfOracles(exahype::records::RepositoryState::NumberOfAdapters);
  #ifdef Parallel
  peano::parallel::loadbalancing::Oracle::getInstance().setNumberOfOracles(exahype::records::RepositoryState::NumberOfAdapters);
  #endif
  
  logTraceOut( "RepositorySTDStack(...)" );
}



exahype::repositories::RepositorySTDStack::RepositorySTDStack(
  peano::geometry::Geometry&                   geometry
):
  _geometry(geometry),
  _cellStack(),
  _vertexStack(),
  _solverState(),
  _gridWithMeshRefinement(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndFusedTimeSteppingInitialisation(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot2d(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithGridErasing(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithADERDGTimeStep(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPlotAndADERDGTimeStep(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithLimiterStatusSpreading(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithReinitialisation(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithLocalRecomputationAndTimeStepSizeComputation(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithNeighbourDataMerging(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithSolutionUpdate(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithTimeStepSizeComputation(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPrediction(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndPlot(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithPredictionAndPlot2d(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithFinaliseMeshRefinementAndTimeStepSizeComputation(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithMergeTimeStepData(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithMergeTimeStepDataDropFaceData(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),
  _gridWithFinaliseMeshRefinementAndReinitialisation(_vertexStack,_cellStack,_geometry,_solverState,_regularGridContainer,_traversalOrderOnTopLevel),

  _repositoryState() {
  logTraceIn( "RepositorySTDStack(Geometry&)" );

  _repositoryState.setAction( exahype::records::RepositoryState::Terminate );
  
  peano::datatraversal::autotuning::Oracle::getInstance().setNumberOfOracles(exahype::records::RepositoryState::NumberOfAdapters);
  #ifdef Parallel
  peano::parallel::loadbalancing::Oracle::getInstance().setNumberOfOracles(exahype::records::RepositoryState::NumberOfAdapters);
  #endif
  
  logTraceOut( "RepositorySTDStack(Geometry&)" );
}
    
   
exahype::repositories::RepositorySTDStack::~RepositorySTDStack() {
  assertionMsg( _repositoryState.getAction() == exahype::records::RepositoryState::Terminate, "terminate() must be called before destroying repository." );
}


void exahype::repositories::RepositorySTDStack::restart(
  const tarch::la::Vector<DIMENSIONS,double>&  domainSize,
  const tarch::la::Vector<DIMENSIONS,double>&  domainOffset,
  int                                          domainLevel,
  const tarch::la::Vector<DIMENSIONS,int>&     positionOfCentralElementWithRespectToCoarserRemoteLevel
) {
  logTraceInWith4Arguments( "restart(...)", domainSize, domainOffset, domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel );
  #ifdef Parallel
  assertion( !tarch::parallel::Node::getInstance().isGlobalMaster());
  #endif
  
  logInfo( "restart(...)", "start node for subdomain " << domainOffset << "x" << domainSize << " on level " << domainLevel << " with master " << tarch::parallel::NodePool::getInstance().getMasterRank() );
  
  assertion( _repositoryState.getAction() == exahype::records::RepositoryState::Terminate );

  _vertexStack.clear();
  _cellStack.clear();

  _gridWithMeshRefinement.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPredictionAndFusedTimeSteppingInitialisation.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot2d.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithGridErasing.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithADERDGTimeStep.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPlotAndADERDGTimeStep.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithLimiterStatusSpreading.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithReinitialisation.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithLocalRecomputationAndTimeStepSizeComputation.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithNeighbourDataMerging.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithSolutionUpdate.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithTimeStepSizeComputation.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPrediction.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPredictionAndPlot.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithPredictionAndPlot2d.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithFinaliseMeshRefinementAndTimeStepSizeComputation.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithMergeTimeStepData.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithMergeTimeStepDataDropFaceData.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);
  _gridWithFinaliseMeshRefinementAndReinitialisation.restart(domainSize,domainOffset,domainLevel, positionOfCentralElementWithRespectToCoarserRemoteLevel);


  _solverState.restart();

  logTraceOut( "restart(...)" );
}


void exahype::repositories::RepositorySTDStack::terminate() {
  logTraceIn( "terminate()" );
  
  _repositoryState.setAction( exahype::records::RepositoryState::Terminate );
  
  #ifdef Parallel
  if (tarch::parallel::Node::getInstance().isGlobalMaster()) {
    tarch::parallel::NodePool::getInstance().broadcastToWorkingNodes(
      _repositoryState,
      peano::parallel::SendReceiveBufferPool::getInstance().getIterationManagementTag()
    );
  }
  peano::parallel::SendReceiveBufferPool::getInstance().terminate();
  #endif

  _gridWithMeshRefinement.terminate();
  _gridWithPredictionAndFusedTimeSteppingInitialisation.terminate();
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot.terminate();
  _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot2d.terminate();
  _gridWithGridErasing.terminate();
  _gridWithADERDGTimeStep.terminate();
  _gridWithPlotAndADERDGTimeStep.terminate();
  _gridWithLimiterStatusSpreading.terminate();
  _gridWithReinitialisation.terminate();
  _gridWithLocalRecomputationAndTimeStepSizeComputation.terminate();
  _gridWithNeighbourDataMerging.terminate();
  _gridWithSolutionUpdate.terminate();
  _gridWithTimeStepSizeComputation.terminate();
  _gridWithPrediction.terminate();
  _gridWithPredictionAndPlot.terminate();
  _gridWithPredictionAndPlot2d.terminate();
  _gridWithFinaliseMeshRefinementAndTimeStepSizeComputation.terminate();
  _gridWithMergeTimeStepData.terminate();
  _gridWithMergeTimeStepDataDropFaceData.terminate();
  _gridWithFinaliseMeshRefinementAndReinitialisation.terminate();


  logTraceOut( "terminate()" );
}


exahype::State& exahype::repositories::RepositorySTDStack::getState() {
  return _solverState;
}


const exahype::State& exahype::repositories::RepositorySTDStack::getState() const {
  return _solverState;
}


void exahype::repositories::RepositorySTDStack::iterate(int numberOfIterations, bool exchangeBoundaryVertices) {
  tarch::timing::Watch watch( "exahype::repositories::RepositorySTDStack", "iterate(bool)", false);
  
  #ifdef Parallel
  if (tarch::parallel::Node::getInstance().isGlobalMaster()) {
    _repositoryState.setNumberOfIterations(numberOfIterations);
    _repositoryState.setExchangeBoundaryVertices(exchangeBoundaryVertices);
    tarch::parallel::NodePool::getInstance().broadcastToWorkingNodes(
      _repositoryState,
      peano::parallel::SendReceiveBufferPool::getInstance().getIterationManagementTag()
    );
  }
  else {
    assertionEquals( numberOfIterations, 1 );
    numberOfIterations = _repositoryState.getNumberOfIterations();
  }
  
  peano::parallel::SendReceiveBufferPool::getInstance().exchangeBoundaryVertices(_repositoryState.getExchangeBoundaryVertices());

  if ( numberOfIterations > 1 && _solverState.isInvolvedInJoinOrFork() ) {
    logWarning( "iterate()", "iterate invoked for multiple traversals though load balancing still does redistribute data" );
  }
  bool switchedLoadBalancingTemporarilyOff = false;
  if ( numberOfIterations > 1 && peano::parallel::loadbalancing::Oracle::getInstance().isLoadBalancingActivated() ) {
    switchedLoadBalancingTemporarilyOff = true;
    peano::parallel::loadbalancing::Oracle::getInstance().activateLoadBalancing(false);
  }

  peano::datatraversal::autotuning::Oracle::getInstance().switchToOracle(_repositoryState.getAction());

  peano::parallel::loadbalancing::Oracle::getInstance().switchToOracle(_repositoryState.getAction());
  
  _solverState.currentlyRunsMultipleIterations(_repositoryState.getNumberOfIterations()>1);
  #else
  peano::datatraversal::autotuning::Oracle::getInstance().switchToOracle(_repositoryState.getAction());
  #endif
  
  for (int i=0; i<numberOfIterations; i++) {
    switch ( _repositoryState.getAction()) {
      case exahype::records::RepositoryState::UseAdapterMeshRefinement: watch.startTimer(); _gridWithMeshRefinement.iterate(); watch.stopTimer(); _measureMeshRefinementCPUTime.setValue( watch.getCPUTime() ); _measureMeshRefinementCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisation: watch.startTimer(); _gridWithPredictionAndFusedTimeSteppingInitialisation.iterate(); watch.stopTimer(); _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.setValue( watch.getCPUTime() ); _measurePredictionAndFusedTimeSteppingInitialisationCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot: watch.startTimer(); _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot.iterate(); watch.stopTimer(); _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.setValue( watch.getCPUTime() ); _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot2d: watch.startTimer(); _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot2d.iterate(); watch.stopTimer(); _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.setValue( watch.getCPUTime() ); _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterGridErasing: watch.startTimer(); _gridWithGridErasing.iterate(); watch.stopTimer(); _measureGridErasingCPUTime.setValue( watch.getCPUTime() ); _measureGridErasingCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterADERDGTimeStep: watch.startTimer(); _gridWithADERDGTimeStep.iterate(); watch.stopTimer(); _measureADERDGTimeStepCPUTime.setValue( watch.getCPUTime() ); _measureADERDGTimeStepCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPlotAndADERDGTimeStep: watch.startTimer(); _gridWithPlotAndADERDGTimeStep.iterate(); watch.stopTimer(); _measurePlotAndADERDGTimeStepCPUTime.setValue( watch.getCPUTime() ); _measurePlotAndADERDGTimeStepCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterLimiterStatusSpreading: watch.startTimer(); _gridWithLimiterStatusSpreading.iterate(); watch.stopTimer(); _measureLimiterStatusSpreadingCPUTime.setValue( watch.getCPUTime() ); _measureLimiterStatusSpreadingCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterReinitialisation: watch.startTimer(); _gridWithReinitialisation.iterate(); watch.stopTimer(); _measureReinitialisationCPUTime.setValue( watch.getCPUTime() ); _measureReinitialisationCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterLocalRecomputationAndTimeStepSizeComputation: watch.startTimer(); _gridWithLocalRecomputationAndTimeStepSizeComputation.iterate(); watch.stopTimer(); _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.setValue( watch.getCPUTime() ); _measureLocalRecomputationAndTimeStepSizeComputationCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterNeighbourDataMerging: watch.startTimer(); _gridWithNeighbourDataMerging.iterate(); watch.stopTimer(); _measureNeighbourDataMergingCPUTime.setValue( watch.getCPUTime() ); _measureNeighbourDataMergingCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterSolutionUpdate: watch.startTimer(); _gridWithSolutionUpdate.iterate(); watch.stopTimer(); _measureSolutionUpdateCPUTime.setValue( watch.getCPUTime() ); _measureSolutionUpdateCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterTimeStepSizeComputation: watch.startTimer(); _gridWithTimeStepSizeComputation.iterate(); watch.stopTimer(); _measureTimeStepSizeComputationCPUTime.setValue( watch.getCPUTime() ); _measureTimeStepSizeComputationCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPrediction: watch.startTimer(); _gridWithPrediction.iterate(); watch.stopTimer(); _measurePredictionCPUTime.setValue( watch.getCPUTime() ); _measurePredictionCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPredictionAndPlot: watch.startTimer(); _gridWithPredictionAndPlot.iterate(); watch.stopTimer(); _measurePredictionAndPlotCPUTime.setValue( watch.getCPUTime() ); _measurePredictionAndPlotCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterPredictionAndPlot2d: watch.startTimer(); _gridWithPredictionAndPlot2d.iterate(); watch.stopTimer(); _measurePredictionAndPlot2dCPUTime.setValue( watch.getCPUTime() ); _measurePredictionAndPlot2dCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterFinaliseMeshRefinementAndTimeStepSizeComputation: watch.startTimer(); _gridWithFinaliseMeshRefinementAndTimeStepSizeComputation.iterate(); watch.stopTimer(); _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.setValue( watch.getCPUTime() ); _measureFinaliseMeshRefinementAndTimeStepSizeComputationCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterMergeTimeStepData: watch.startTimer(); _gridWithMergeTimeStepData.iterate(); watch.stopTimer(); _measureMergeTimeStepDataCPUTime.setValue( watch.getCPUTime() ); _measureMergeTimeStepDataCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterMergeTimeStepDataDropFaceData: watch.startTimer(); _gridWithMergeTimeStepDataDropFaceData.iterate(); watch.stopTimer(); _measureMergeTimeStepDataDropFaceDataCPUTime.setValue( watch.getCPUTime() ); _measureMergeTimeStepDataDropFaceDataCalendarTime.setValue( watch.getCalendarTime() ); break;
      case exahype::records::RepositoryState::UseAdapterFinaliseMeshRefinementAndReinitialisation: watch.startTimer(); _gridWithFinaliseMeshRefinementAndReinitialisation.iterate(); watch.stopTimer(); _measureFinaliseMeshRefinementAndReinitialisationCPUTime.setValue( watch.getCPUTime() ); _measureFinaliseMeshRefinementAndReinitialisationCalendarTime.setValue( watch.getCalendarTime() ); break;

      case exahype::records::RepositoryState::Terminate:
        assertionMsg( false, "this branch/state should never be reached" ); 
        break;
      case exahype::records::RepositoryState::NumberOfAdapters:
        assertionMsg( false, "this branch/state should never be reached" ); 
        break;
      case exahype::records::RepositoryState::RunOnAllNodes:
        assertionMsg( false, "this branch/state should never be reached" ); 
        break;
      case exahype::records::RepositoryState::ReadCheckpoint:
        assertionMsg( false, "not implemented yet" );
        break;
      case exahype::records::RepositoryState::WriteCheckpoint:
        assertionMsg( false, "not implemented yet" );
        break;
    }
    #ifdef Parallel
    if ( switchedLoadBalancingTemporarilyOff && i==numberOfIterations-1) {
      peano::parallel::loadbalancing::Oracle::getInstance().activateLoadBalancing(true);
    }
    #endif
  }
  
  #ifdef Parallel
  if (_solverState.isJoiningWithMaster()) {
    _repositoryState.setAction( exahype::records::RepositoryState::Terminate );
  }
  #endif
}

 void exahype::repositories::RepositorySTDStack::switchToMeshRefinement() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterMeshRefinement); }
 void exahype::repositories::RepositorySTDStack::switchToPredictionAndFusedTimeSteppingInitialisation() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisation); }
 void exahype::repositories::RepositorySTDStack::switchToPredictionAndFusedTimeSteppingInitialisationAndPlot() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot); }
 void exahype::repositories::RepositorySTDStack::switchToPredictionAndFusedTimeSteppingInitialisationAndPlot2d() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot2d); }
 void exahype::repositories::RepositorySTDStack::switchToGridErasing() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterGridErasing); }
 void exahype::repositories::RepositorySTDStack::switchToADERDGTimeStep() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterADERDGTimeStep); }
 void exahype::repositories::RepositorySTDStack::switchToPlotAndADERDGTimeStep() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPlotAndADERDGTimeStep); }
 void exahype::repositories::RepositorySTDStack::switchToLimiterStatusSpreading() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterLimiterStatusSpreading); }
 void exahype::repositories::RepositorySTDStack::switchToReinitialisation() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterReinitialisation); }
 void exahype::repositories::RepositorySTDStack::switchToLocalRecomputationAndTimeStepSizeComputation() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterLocalRecomputationAndTimeStepSizeComputation); }
 void exahype::repositories::RepositorySTDStack::switchToNeighbourDataMerging() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterNeighbourDataMerging); }
 void exahype::repositories::RepositorySTDStack::switchToSolutionUpdate() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterSolutionUpdate); }
 void exahype::repositories::RepositorySTDStack::switchToTimeStepSizeComputation() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterTimeStepSizeComputation); }
 void exahype::repositories::RepositorySTDStack::switchToPrediction() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPrediction); }
 void exahype::repositories::RepositorySTDStack::switchToPredictionAndPlot() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPredictionAndPlot); }
 void exahype::repositories::RepositorySTDStack::switchToPredictionAndPlot2d() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterPredictionAndPlot2d); }
 void exahype::repositories::RepositorySTDStack::switchToFinaliseMeshRefinementAndTimeStepSizeComputation() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterFinaliseMeshRefinementAndTimeStepSizeComputation); }
 void exahype::repositories::RepositorySTDStack::switchToMergeTimeStepData() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterMergeTimeStepData); }
 void exahype::repositories::RepositorySTDStack::switchToMergeTimeStepDataDropFaceData() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterMergeTimeStepDataDropFaceData); }
 void exahype::repositories::RepositorySTDStack::switchToFinaliseMeshRefinementAndReinitialisation() { _repositoryState.setAction(exahype::records::RepositoryState::UseAdapterFinaliseMeshRefinementAndReinitialisation); }



 bool exahype::repositories::RepositorySTDStack::isActiveAdapterMeshRefinement() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterMeshRefinement; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPredictionAndFusedTimeSteppingInitialisation() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisation; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot2d() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot2d; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterGridErasing() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterGridErasing; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterADERDGTimeStep() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterADERDGTimeStep; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPlotAndADERDGTimeStep() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPlotAndADERDGTimeStep; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterLimiterStatusSpreading() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterLimiterStatusSpreading; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterReinitialisation() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterReinitialisation; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterLocalRecomputationAndTimeStepSizeComputation() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterLocalRecomputationAndTimeStepSizeComputation; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterNeighbourDataMerging() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterNeighbourDataMerging; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterSolutionUpdate() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterSolutionUpdate; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterTimeStepSizeComputation() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterTimeStepSizeComputation; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPrediction() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPrediction; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPredictionAndPlot() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPredictionAndPlot; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterPredictionAndPlot2d() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterPredictionAndPlot2d; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterFinaliseMeshRefinementAndTimeStepSizeComputation() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterFinaliseMeshRefinementAndTimeStepSizeComputation; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterMergeTimeStepData() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterMergeTimeStepData; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterMergeTimeStepDataDropFaceData() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterMergeTimeStepDataDropFaceData; }
 bool exahype::repositories::RepositorySTDStack::isActiveAdapterFinaliseMeshRefinementAndReinitialisation() const { return _repositoryState.getAction() == exahype::records::RepositoryState::UseAdapterFinaliseMeshRefinementAndReinitialisation; }



peano::grid::Checkpoint<exahype::Vertex, exahype::Cell>* exahype::repositories::RepositorySTDStack::createEmptyCheckpoint() {
  return new peano::grid::Checkpoint<exahype::Vertex, exahype::Cell>();
} 


void exahype::repositories::RepositorySTDStack::writeCheckpoint(peano::grid::Checkpoint<exahype::Vertex, exahype::Cell> * const checkpoint) {
  _solverState.writeToCheckpoint( *checkpoint );
  _vertexStack.writeToCheckpoint( *checkpoint );
  _cellStack.writeToCheckpoint( *checkpoint );
} 


void exahype::repositories::RepositorySTDStack::readCheckpoint( peano::grid::Checkpoint<exahype::Vertex, exahype::Cell> const * const checkpoint ) {
  assertionMsg( checkpoint->isValid(), "checkpoint has to be valid if you call this operation" );

  _solverState.readFromCheckpoint( *checkpoint );
  _vertexStack.readFromCheckpoint( *checkpoint );
  _cellStack.readFromCheckpoint( *checkpoint );
}


void exahype::repositories::RepositorySTDStack::setMaximumMemoryFootprintForTemporaryRegularGrids(double value) {
  _regularGridContainer.setMaximumMemoryFootprintForTemporaryRegularGrids(value);
}


#ifdef Parallel
void exahype::repositories::RepositorySTDStack::runGlobalStep() {
  assertion(tarch::parallel::Node::getInstance().isGlobalMaster());

  exahype::records::RepositoryState intermediateStateForWorkingNodes;
  intermediateStateForWorkingNodes.setAction( exahype::records::RepositoryState::RunOnAllNodes );
  
  tarch::parallel::NodePool::getInstance().broadcastToWorkingNodes(
    intermediateStateForWorkingNodes,
    peano::parallel::SendReceiveBufferPool::getInstance().getIterationManagementTag()
  );
  tarch::parallel::NodePool::getInstance().activateIdleNodes();
}


exahype::repositories::RepositorySTDStack::ContinueCommand exahype::repositories::RepositorySTDStack::continueToIterate() {
  logTraceIn( "continueToIterate()" );

  assertion( !tarch::parallel::Node::getInstance().isGlobalMaster());

  ContinueCommand result;
  if ( _solverState.hasJoinedWithMaster() ) {
    result = Terminate;
  }
  else {
    int masterNode = tarch::parallel::Node::getInstance().getGlobalMasterRank();
    assertion( masterNode != -1 );

    _repositoryState.receive( masterNode, peano::parallel::SendReceiveBufferPool::getInstance().getIterationManagementTag(), true, ReceiveIterationControlMessagesBlocking );

    result = Continue;
    if (_repositoryState.getAction()==exahype::records::RepositoryState::Terminate) {
      result = Terminate;
    } 
    if (_repositoryState.getAction()==exahype::records::RepositoryState::RunOnAllNodes) {
      result = RunGlobalStep;
    } 
  }
   
  logTraceOutWith1Argument( "continueToIterate()", result );
  return result;
}
#endif


void exahype::repositories::RepositorySTDStack::logIterationStatistics(bool logAllAdapters) const {
  logInfo( "logIterationStatistics()", "|| adapter name \t || iterations \t || total CPU time [t]=s \t || average CPU time [t]=s \t || total user time [t]=s \t || average user time [t]=s  || CPU time properties  || user time properties " );  
   if (logAllAdapters || _measureMeshRefinementCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| MeshRefinement \t |  " << _measureMeshRefinementCPUTime.getNumberOfMeasurements() << " \t |  " << _measureMeshRefinementCPUTime.getAccumulatedValue() << " \t |  " << _measureMeshRefinementCPUTime.getValue()  << " \t |  " << _measureMeshRefinementCalendarTime.getAccumulatedValue() << " \t |  " << _measureMeshRefinementCalendarTime.getValue() << " \t |  " << _measureMeshRefinementCPUTime.toString() << " \t |  " << _measureMeshRefinementCalendarTime.toString() );
   if (logAllAdapters || _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| PredictionAndFusedTimeSteppingInitialisation \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.getValue()  << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCalendarTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCalendarTime.getValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.toString() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationCalendarTime.toString() );
   if (logAllAdapters || _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| PredictionAndFusedTimeSteppingInitialisationAndPlot \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.getValue()  << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCalendarTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCalendarTime.getValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.toString() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCalendarTime.toString() );
   if (logAllAdapters || _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| PredictionAndFusedTimeSteppingInitialisationAndPlot2d \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.getValue()  << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCalendarTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCalendarTime.getValue() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.toString() << " \t |  " << _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCalendarTime.toString() );
   if (logAllAdapters || _measureGridErasingCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| GridErasing \t |  " << _measureGridErasingCPUTime.getNumberOfMeasurements() << " \t |  " << _measureGridErasingCPUTime.getAccumulatedValue() << " \t |  " << _measureGridErasingCPUTime.getValue()  << " \t |  " << _measureGridErasingCalendarTime.getAccumulatedValue() << " \t |  " << _measureGridErasingCalendarTime.getValue() << " \t |  " << _measureGridErasingCPUTime.toString() << " \t |  " << _measureGridErasingCalendarTime.toString() );
   if (logAllAdapters || _measureADERDGTimeStepCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| ADERDGTimeStep \t |  " << _measureADERDGTimeStepCPUTime.getNumberOfMeasurements() << " \t |  " << _measureADERDGTimeStepCPUTime.getAccumulatedValue() << " \t |  " << _measureADERDGTimeStepCPUTime.getValue()  << " \t |  " << _measureADERDGTimeStepCalendarTime.getAccumulatedValue() << " \t |  " << _measureADERDGTimeStepCalendarTime.getValue() << " \t |  " << _measureADERDGTimeStepCPUTime.toString() << " \t |  " << _measureADERDGTimeStepCalendarTime.toString() );
   if (logAllAdapters || _measurePlotAndADERDGTimeStepCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| PlotAndADERDGTimeStep \t |  " << _measurePlotAndADERDGTimeStepCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePlotAndADERDGTimeStepCPUTime.getAccumulatedValue() << " \t |  " << _measurePlotAndADERDGTimeStepCPUTime.getValue()  << " \t |  " << _measurePlotAndADERDGTimeStepCalendarTime.getAccumulatedValue() << " \t |  " << _measurePlotAndADERDGTimeStepCalendarTime.getValue() << " \t |  " << _measurePlotAndADERDGTimeStepCPUTime.toString() << " \t |  " << _measurePlotAndADERDGTimeStepCalendarTime.toString() );
   if (logAllAdapters || _measureLimiterStatusSpreadingCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| LimiterStatusSpreading \t |  " << _measureLimiterStatusSpreadingCPUTime.getNumberOfMeasurements() << " \t |  " << _measureLimiterStatusSpreadingCPUTime.getAccumulatedValue() << " \t |  " << _measureLimiterStatusSpreadingCPUTime.getValue()  << " \t |  " << _measureLimiterStatusSpreadingCalendarTime.getAccumulatedValue() << " \t |  " << _measureLimiterStatusSpreadingCalendarTime.getValue() << " \t |  " << _measureLimiterStatusSpreadingCPUTime.toString() << " \t |  " << _measureLimiterStatusSpreadingCalendarTime.toString() );
   if (logAllAdapters || _measureReinitialisationCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| Reinitialisation \t |  " << _measureReinitialisationCPUTime.getNumberOfMeasurements() << " \t |  " << _measureReinitialisationCPUTime.getAccumulatedValue() << " \t |  " << _measureReinitialisationCPUTime.getValue()  << " \t |  " << _measureReinitialisationCalendarTime.getAccumulatedValue() << " \t |  " << _measureReinitialisationCalendarTime.getValue() << " \t |  " << _measureReinitialisationCPUTime.toString() << " \t |  " << _measureReinitialisationCalendarTime.toString() );
   if (logAllAdapters || _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| LocalRecomputationAndTimeStepSizeComputation \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.getNumberOfMeasurements() << " \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.getAccumulatedValue() << " \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.getValue()  << " \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCalendarTime.getAccumulatedValue() << " \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCalendarTime.getValue() << " \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.toString() << " \t |  " << _measureLocalRecomputationAndTimeStepSizeComputationCalendarTime.toString() );
   if (logAllAdapters || _measureNeighbourDataMergingCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| NeighbourDataMerging \t |  " << _measureNeighbourDataMergingCPUTime.getNumberOfMeasurements() << " \t |  " << _measureNeighbourDataMergingCPUTime.getAccumulatedValue() << " \t |  " << _measureNeighbourDataMergingCPUTime.getValue()  << " \t |  " << _measureNeighbourDataMergingCalendarTime.getAccumulatedValue() << " \t |  " << _measureNeighbourDataMergingCalendarTime.getValue() << " \t |  " << _measureNeighbourDataMergingCPUTime.toString() << " \t |  " << _measureNeighbourDataMergingCalendarTime.toString() );
   if (logAllAdapters || _measureSolutionUpdateCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| SolutionUpdate \t |  " << _measureSolutionUpdateCPUTime.getNumberOfMeasurements() << " \t |  " << _measureSolutionUpdateCPUTime.getAccumulatedValue() << " \t |  " << _measureSolutionUpdateCPUTime.getValue()  << " \t |  " << _measureSolutionUpdateCalendarTime.getAccumulatedValue() << " \t |  " << _measureSolutionUpdateCalendarTime.getValue() << " \t |  " << _measureSolutionUpdateCPUTime.toString() << " \t |  " << _measureSolutionUpdateCalendarTime.toString() );
   if (logAllAdapters || _measureTimeStepSizeComputationCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| TimeStepSizeComputation \t |  " << _measureTimeStepSizeComputationCPUTime.getNumberOfMeasurements() << " \t |  " << _measureTimeStepSizeComputationCPUTime.getAccumulatedValue() << " \t |  " << _measureTimeStepSizeComputationCPUTime.getValue()  << " \t |  " << _measureTimeStepSizeComputationCalendarTime.getAccumulatedValue() << " \t |  " << _measureTimeStepSizeComputationCalendarTime.getValue() << " \t |  " << _measureTimeStepSizeComputationCPUTime.toString() << " \t |  " << _measureTimeStepSizeComputationCalendarTime.toString() );
   if (logAllAdapters || _measurePredictionCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| Prediction \t |  " << _measurePredictionCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePredictionCPUTime.getAccumulatedValue() << " \t |  " << _measurePredictionCPUTime.getValue()  << " \t |  " << _measurePredictionCalendarTime.getAccumulatedValue() << " \t |  " << _measurePredictionCalendarTime.getValue() << " \t |  " << _measurePredictionCPUTime.toString() << " \t |  " << _measurePredictionCalendarTime.toString() );
   if (logAllAdapters || _measurePredictionAndPlotCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| PredictionAndPlot \t |  " << _measurePredictionAndPlotCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePredictionAndPlotCPUTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndPlotCPUTime.getValue()  << " \t |  " << _measurePredictionAndPlotCalendarTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndPlotCalendarTime.getValue() << " \t |  " << _measurePredictionAndPlotCPUTime.toString() << " \t |  " << _measurePredictionAndPlotCalendarTime.toString() );
   if (logAllAdapters || _measurePredictionAndPlot2dCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| PredictionAndPlot2d \t |  " << _measurePredictionAndPlot2dCPUTime.getNumberOfMeasurements() << " \t |  " << _measurePredictionAndPlot2dCPUTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndPlot2dCPUTime.getValue()  << " \t |  " << _measurePredictionAndPlot2dCalendarTime.getAccumulatedValue() << " \t |  " << _measurePredictionAndPlot2dCalendarTime.getValue() << " \t |  " << _measurePredictionAndPlot2dCPUTime.toString() << " \t |  " << _measurePredictionAndPlot2dCalendarTime.toString() );
   if (logAllAdapters || _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| FinaliseMeshRefinementAndTimeStepSizeComputation \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.getNumberOfMeasurements() << " \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.getAccumulatedValue() << " \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.getValue()  << " \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCalendarTime.getAccumulatedValue() << " \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCalendarTime.getValue() << " \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.toString() << " \t |  " << _measureFinaliseMeshRefinementAndTimeStepSizeComputationCalendarTime.toString() );
   if (logAllAdapters || _measureMergeTimeStepDataCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| MergeTimeStepData \t |  " << _measureMergeTimeStepDataCPUTime.getNumberOfMeasurements() << " \t |  " << _measureMergeTimeStepDataCPUTime.getAccumulatedValue() << " \t |  " << _measureMergeTimeStepDataCPUTime.getValue()  << " \t |  " << _measureMergeTimeStepDataCalendarTime.getAccumulatedValue() << " \t |  " << _measureMergeTimeStepDataCalendarTime.getValue() << " \t |  " << _measureMergeTimeStepDataCPUTime.toString() << " \t |  " << _measureMergeTimeStepDataCalendarTime.toString() );
   if (logAllAdapters || _measureMergeTimeStepDataDropFaceDataCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| MergeTimeStepDataDropFaceData \t |  " << _measureMergeTimeStepDataDropFaceDataCPUTime.getNumberOfMeasurements() << " \t |  " << _measureMergeTimeStepDataDropFaceDataCPUTime.getAccumulatedValue() << " \t |  " << _measureMergeTimeStepDataDropFaceDataCPUTime.getValue()  << " \t |  " << _measureMergeTimeStepDataDropFaceDataCalendarTime.getAccumulatedValue() << " \t |  " << _measureMergeTimeStepDataDropFaceDataCalendarTime.getValue() << " \t |  " << _measureMergeTimeStepDataDropFaceDataCPUTime.toString() << " \t |  " << _measureMergeTimeStepDataDropFaceDataCalendarTime.toString() );
   if (logAllAdapters || _measureFinaliseMeshRefinementAndReinitialisationCPUTime.getNumberOfMeasurements()>0) logInfo( "logIterationStatistics()", "| FinaliseMeshRefinementAndReinitialisation \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCPUTime.getNumberOfMeasurements() << " \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCPUTime.getAccumulatedValue() << " \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCPUTime.getValue()  << " \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCalendarTime.getAccumulatedValue() << " \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCalendarTime.getValue() << " \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCPUTime.toString() << " \t |  " << _measureFinaliseMeshRefinementAndReinitialisationCalendarTime.toString() );

}


void exahype::repositories::RepositorySTDStack::clearIterationStatistics() {
   _measureMeshRefinementCPUTime.erase();
   _measurePredictionAndFusedTimeSteppingInitialisationCPUTime.erase();
   _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime.erase();
   _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime.erase();
   _measureGridErasingCPUTime.erase();
   _measureADERDGTimeStepCPUTime.erase();
   _measurePlotAndADERDGTimeStepCPUTime.erase();
   _measureLimiterStatusSpreadingCPUTime.erase();
   _measureReinitialisationCPUTime.erase();
   _measureLocalRecomputationAndTimeStepSizeComputationCPUTime.erase();
   _measureNeighbourDataMergingCPUTime.erase();
   _measureSolutionUpdateCPUTime.erase();
   _measureTimeStepSizeComputationCPUTime.erase();
   _measurePredictionCPUTime.erase();
   _measurePredictionAndPlotCPUTime.erase();
   _measurePredictionAndPlot2dCPUTime.erase();
   _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime.erase();
   _measureMergeTimeStepDataCPUTime.erase();
   _measureMergeTimeStepDataDropFaceDataCPUTime.erase();
   _measureFinaliseMeshRefinementAndReinitialisationCPUTime.erase();

   _measureMeshRefinementCalendarTime.erase();
   _measurePredictionAndFusedTimeSteppingInitialisationCalendarTime.erase();
   _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCalendarTime.erase();
   _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCalendarTime.erase();
   _measureGridErasingCalendarTime.erase();
   _measureADERDGTimeStepCalendarTime.erase();
   _measurePlotAndADERDGTimeStepCalendarTime.erase();
   _measureLimiterStatusSpreadingCalendarTime.erase();
   _measureReinitialisationCalendarTime.erase();
   _measureLocalRecomputationAndTimeStepSizeComputationCalendarTime.erase();
   _measureNeighbourDataMergingCalendarTime.erase();
   _measureSolutionUpdateCalendarTime.erase();
   _measureTimeStepSizeComputationCalendarTime.erase();
   _measurePredictionCalendarTime.erase();
   _measurePredictionAndPlotCalendarTime.erase();
   _measurePredictionAndPlot2dCalendarTime.erase();
   _measureFinaliseMeshRefinementAndTimeStepSizeComputationCalendarTime.erase();
   _measureMergeTimeStepDataCalendarTime.erase();
   _measureMergeTimeStepDataDropFaceDataCalendarTime.erase();
   _measureFinaliseMeshRefinementAndReinitialisationCalendarTime.erase();

}
