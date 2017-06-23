// This file is part of the Peano project. For conditions of distribution and 
// use, please see the copyright notice at www.peano-framework.org
#ifndef _EXAHYPE_REPOSITORIES_REPOSITORY_ARRAY_STD_H_ 
#define _EXAHYPE_REPOSITORIES_REPOSITORY_ARRAY_STD_H_ 


#include "exahype/repositories/Repository.h"
#include "exahype/records/RepositoryState.h"

#include "exahype/State.h"
#include "exahype/Vertex.h"
#include "exahype/Cell.h"

#include "peano/grid/Grid.h"
#include "peano/stacks/CellSTDStack.h"
#include "peano/stacks/VertexSTDStack.h"


 #include "exahype/adapters/MeshRefinement.h" 
 #include "exahype/adapters/PredictionAndFusedTimeSteppingInitialisation.h" 
 #include "exahype/adapters/PredictionAndFusedTimeSteppingInitialisationAndPlot.h" 
 #include "exahype/adapters/PredictionAndFusedTimeSteppingInitialisationAndPlot2d.h" 
 #include "exahype/adapters/GridErasing.h" 
 #include "exahype/adapters/ADERDGTimeStep.h" 
 #include "exahype/adapters/PlotAndADERDGTimeStep.h" 
 #include "exahype/adapters/LimiterStatusSpreading.h" 
 #include "exahype/adapters/Reinitialisation.h" 
 #include "exahype/adapters/LocalRecomputationAndTimeStepSizeComputation.h" 
 #include "exahype/adapters/NeighbourDataMerging.h" 
 #include "exahype/adapters/SolutionUpdate.h" 
 #include "exahype/adapters/TimeStepSizeComputation.h" 
 #include "exahype/adapters/Prediction.h" 
 #include "exahype/adapters/PredictionAndPlot.h" 
 #include "exahype/adapters/PredictionAndPlot2d.h" 
 #include "exahype/adapters/FinaliseMeshRefinementAndTimeStepSizeComputation.h" 
 #include "exahype/adapters/MergeTimeStepData.h" 
 #include "exahype/adapters/MergeTimeStepDataDropFaceData.h" 
 #include "exahype/adapters/FinaliseMeshRefinementAndReinitialisation.h" 



namespace exahype {
      namespace repositories {
        class RepositorySTDStack;  
      }
}


class exahype::repositories::RepositorySTDStack: public exahype::repositories::Repository {
  private:
    static tarch::logging::Log _log;
  
    peano::geometry::Geometry& _geometry;
    
    typedef peano::stacks::CellSTDStack<exahype::Cell>       CellStack;
    typedef peano::stacks::VertexSTDStack<exahype::Vertex>   VertexStack;

    CellStack    _cellStack;
    VertexStack  _vertexStack;
    exahype::State          _solverState;
    peano::grid::RegularGridContainer<exahype::Vertex,exahype::Cell>  _regularGridContainer;
    peano::grid::TraversalOrderOnTopLevel                                         _traversalOrderOnTopLevel;

    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::MeshRefinement> _gridWithMeshRefinement;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::PredictionAndFusedTimeSteppingInitialisation> _gridWithPredictionAndFusedTimeSteppingInitialisation;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::PredictionAndFusedTimeSteppingInitialisationAndPlot> _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::PredictionAndFusedTimeSteppingInitialisationAndPlot2d> _gridWithPredictionAndFusedTimeSteppingInitialisationAndPlot2d;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::GridErasing> _gridWithGridErasing;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::ADERDGTimeStep> _gridWithADERDGTimeStep;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::PlotAndADERDGTimeStep> _gridWithPlotAndADERDGTimeStep;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::LimiterStatusSpreading> _gridWithLimiterStatusSpreading;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::Reinitialisation> _gridWithReinitialisation;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::LocalRecomputationAndTimeStepSizeComputation> _gridWithLocalRecomputationAndTimeStepSizeComputation;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::NeighbourDataMerging> _gridWithNeighbourDataMerging;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::SolutionUpdate> _gridWithSolutionUpdate;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::TimeStepSizeComputation> _gridWithTimeStepSizeComputation;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::Prediction> _gridWithPrediction;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::PredictionAndPlot> _gridWithPredictionAndPlot;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::PredictionAndPlot2d> _gridWithPredictionAndPlot2d;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::FinaliseMeshRefinementAndTimeStepSizeComputation> _gridWithFinaliseMeshRefinementAndTimeStepSizeComputation;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::MergeTimeStepData> _gridWithMergeTimeStepData;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::MergeTimeStepDataDropFaceData> _gridWithMergeTimeStepDataDropFaceData;
    peano::grid::Grid<exahype::Vertex,exahype::Cell,exahype::State,VertexStack,CellStack,exahype::adapters::FinaliseMeshRefinementAndReinitialisation> _gridWithFinaliseMeshRefinementAndReinitialisation;

     
   exahype::records::RepositoryState               _repositoryState;
   
    tarch::timing::Measurement _measureMeshRefinementCPUTime;
    tarch::timing::Measurement _measurePredictionAndFusedTimeSteppingInitialisationCPUTime;
    tarch::timing::Measurement _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCPUTime;
    tarch::timing::Measurement _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCPUTime;
    tarch::timing::Measurement _measureGridErasingCPUTime;
    tarch::timing::Measurement _measureADERDGTimeStepCPUTime;
    tarch::timing::Measurement _measurePlotAndADERDGTimeStepCPUTime;
    tarch::timing::Measurement _measureLimiterStatusSpreadingCPUTime;
    tarch::timing::Measurement _measureReinitialisationCPUTime;
    tarch::timing::Measurement _measureLocalRecomputationAndTimeStepSizeComputationCPUTime;
    tarch::timing::Measurement _measureNeighbourDataMergingCPUTime;
    tarch::timing::Measurement _measureSolutionUpdateCPUTime;
    tarch::timing::Measurement _measureTimeStepSizeComputationCPUTime;
    tarch::timing::Measurement _measurePredictionCPUTime;
    tarch::timing::Measurement _measurePredictionAndPlotCPUTime;
    tarch::timing::Measurement _measurePredictionAndPlot2dCPUTime;
    tarch::timing::Measurement _measureFinaliseMeshRefinementAndTimeStepSizeComputationCPUTime;
    tarch::timing::Measurement _measureMergeTimeStepDataCPUTime;
    tarch::timing::Measurement _measureMergeTimeStepDataDropFaceDataCPUTime;
    tarch::timing::Measurement _measureFinaliseMeshRefinementAndReinitialisationCPUTime;

    tarch::timing::Measurement _measureMeshRefinementCalendarTime;
    tarch::timing::Measurement _measurePredictionAndFusedTimeSteppingInitialisationCalendarTime;
    tarch::timing::Measurement _measurePredictionAndFusedTimeSteppingInitialisationAndPlotCalendarTime;
    tarch::timing::Measurement _measurePredictionAndFusedTimeSteppingInitialisationAndPlot2dCalendarTime;
    tarch::timing::Measurement _measureGridErasingCalendarTime;
    tarch::timing::Measurement _measureADERDGTimeStepCalendarTime;
    tarch::timing::Measurement _measurePlotAndADERDGTimeStepCalendarTime;
    tarch::timing::Measurement _measureLimiterStatusSpreadingCalendarTime;
    tarch::timing::Measurement _measureReinitialisationCalendarTime;
    tarch::timing::Measurement _measureLocalRecomputationAndTimeStepSizeComputationCalendarTime;
    tarch::timing::Measurement _measureNeighbourDataMergingCalendarTime;
    tarch::timing::Measurement _measureSolutionUpdateCalendarTime;
    tarch::timing::Measurement _measureTimeStepSizeComputationCalendarTime;
    tarch::timing::Measurement _measurePredictionCalendarTime;
    tarch::timing::Measurement _measurePredictionAndPlotCalendarTime;
    tarch::timing::Measurement _measurePredictionAndPlot2dCalendarTime;
    tarch::timing::Measurement _measureFinaliseMeshRefinementAndTimeStepSizeComputationCalendarTime;
    tarch::timing::Measurement _measureMergeTimeStepDataCalendarTime;
    tarch::timing::Measurement _measureMergeTimeStepDataDropFaceDataCalendarTime;
    tarch::timing::Measurement _measureFinaliseMeshRefinementAndReinitialisationCalendarTime;

   
  public:
    RepositorySTDStack(
      peano::geometry::Geometry&                   geometry,
      const tarch::la::Vector<DIMENSIONS,double>&  domainSize,
      const tarch::la::Vector<DIMENSIONS,double>&  computationalDomainOffset
    );
    
    /**
     * Parallel Constructor
     *
     * Used in parallel mode only where the size of the domain is not known 
     * when the type of repository is determined.  
     */
    RepositorySTDStack(
      peano::geometry::Geometry&                   geometry
    );
    
    virtual ~RepositorySTDStack();

    virtual void restart(
      const tarch::la::Vector<DIMENSIONS,double>&  domainSize,
      const tarch::la::Vector<DIMENSIONS,double>&  domainOffset,
      int                                          domainLevel,
      const tarch::la::Vector<DIMENSIONS,int>&     positionOfCentralElementWithRespectToCoarserRemoteLevel
    );
         
    virtual void terminate();
        
    virtual exahype::State& getState();
    virtual const exahype::State& getState() const;
	
    virtual void iterate(int numberOfIterations=1, bool exchangeBoundaryVertices=true);

    virtual void writeCheckpoint(peano::grid::Checkpoint<exahype::Vertex, exahype::Cell> * const checkpoint); 
    virtual void readCheckpoint( peano::grid::Checkpoint<exahype::Vertex, exahype::Cell> const * const checkpoint );
    virtual peano::grid::Checkpoint<exahype::Vertex, exahype::Cell>* createEmptyCheckpoint(); 

    virtual void switchToMeshRefinement();    
    virtual void switchToPredictionAndFusedTimeSteppingInitialisation();    
    virtual void switchToPredictionAndFusedTimeSteppingInitialisationAndPlot();    
    virtual void switchToPredictionAndFusedTimeSteppingInitialisationAndPlot2d();    
    virtual void switchToGridErasing();    
    virtual void switchToADERDGTimeStep();    
    virtual void switchToPlotAndADERDGTimeStep();    
    virtual void switchToLimiterStatusSpreading();    
    virtual void switchToReinitialisation();    
    virtual void switchToLocalRecomputationAndTimeStepSizeComputation();    
    virtual void switchToNeighbourDataMerging();    
    virtual void switchToSolutionUpdate();    
    virtual void switchToTimeStepSizeComputation();    
    virtual void switchToPrediction();    
    virtual void switchToPredictionAndPlot();    
    virtual void switchToPredictionAndPlot2d();    
    virtual void switchToFinaliseMeshRefinementAndTimeStepSizeComputation();    
    virtual void switchToMergeTimeStepData();    
    virtual void switchToMergeTimeStepDataDropFaceData();    
    virtual void switchToFinaliseMeshRefinementAndReinitialisation();    

    virtual bool isActiveAdapterMeshRefinement() const;
    virtual bool isActiveAdapterPredictionAndFusedTimeSteppingInitialisation() const;
    virtual bool isActiveAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot() const;
    virtual bool isActiveAdapterPredictionAndFusedTimeSteppingInitialisationAndPlot2d() const;
    virtual bool isActiveAdapterGridErasing() const;
    virtual bool isActiveAdapterADERDGTimeStep() const;
    virtual bool isActiveAdapterPlotAndADERDGTimeStep() const;
    virtual bool isActiveAdapterLimiterStatusSpreading() const;
    virtual bool isActiveAdapterReinitialisation() const;
    virtual bool isActiveAdapterLocalRecomputationAndTimeStepSizeComputation() const;
    virtual bool isActiveAdapterNeighbourDataMerging() const;
    virtual bool isActiveAdapterSolutionUpdate() const;
    virtual bool isActiveAdapterTimeStepSizeComputation() const;
    virtual bool isActiveAdapterPrediction() const;
    virtual bool isActiveAdapterPredictionAndPlot() const;
    virtual bool isActiveAdapterPredictionAndPlot2d() const;
    virtual bool isActiveAdapterFinaliseMeshRefinementAndTimeStepSizeComputation() const;
    virtual bool isActiveAdapterMergeTimeStepData() const;
    virtual bool isActiveAdapterMergeTimeStepDataDropFaceData() const;
    virtual bool isActiveAdapterFinaliseMeshRefinementAndReinitialisation() const;

   
    #ifdef Parallel
    virtual ContinueCommand continueToIterate();
    virtual void runGlobalStep();
    #endif

    virtual void setMaximumMemoryFootprintForTemporaryRegularGrids(double value);
    virtual void logIterationStatistics(bool logAllAdapters) const;
    virtual void clearIterationStatistics();
};


#endif
