package eu.exahype.solvers;

import java.io.BufferedWriter;
import java.io.IOException;
import java.util.Set;

import eu.exahype.io.IOUtils;
import eu.exahype.io.SourceTemplate;

public class OptimisedADERDG implements Solver {
  public static final String Identifier = "optimised::fluxes::nonlinear";

  private int     _dimensions;
  private int     _numberOfVariables;
  private int     _numberOfParameters;
  private Set<String> _namingSchemeNames;
  private int     _order;
//  private int   _patchSize;
  private String  _microarchitecture;
  private String  _pathToLibxsmm;
  private boolean _enableProfiler;
  private boolean _enableDeepProfiler;
  private boolean _hasConstants;
  private boolean _isLinear;
  private boolean _isFortran;

  public OptimisedADERDG(int dimensions, int numberOfVariables, int numberOfParameters, Set<String> namingSchemeNames,
      int order,String microarchitecture, String pathToLibxsmm, boolean enableProfiler, boolean enableDeepProfiler, boolean hasConstants,boolean isLinear) {
    _dimensions         = dimensions;
    _numberOfVariables  = numberOfVariables;
    _numberOfParameters = numberOfParameters;
    _namingSchemeNames  = namingSchemeNames;
    _order              = order;
//    _patchSize = patchSize;
    _microarchitecture  = microarchitecture;
    _pathToLibxsmm      = pathToLibxsmm;
    _enableProfiler     = enableProfiler;
    _enableDeepProfiler = enableDeepProfiler;
    _hasConstants       = hasConstants;
    _isLinear           = isLinear;
  }
  
  private String getAbstractSolverName(String solverName) {
    return "Abstract"+solverName;
  }
  
  @Override
  public void writeHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
	  SourceTemplate content = SourceTemplate.fromRessourceContent(
			  "eu/exahype/solvers/templates/OptimisedADERDGSolverHeader.template");

	  content.put("Project", projectName);
	  content.put("Solver", solverName);
    content.put("AbstractSolver", getAbstractSolverName(solverName));

	  String profilerInclude                     = "";
    String parserInclude                       = "";
	  String solverConstructorSignatureExtension = "";
    String solverInitSignatureExtension        = "";
	  if (_enableProfiler) {
		  profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
		  solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler"; 
	  }
	  if (_hasConstants) {
      solverInitSignatureExtension = ", exahype::Parser::ParserView& constants";
      parserInclude = "#include \"exahype/Parser.h\"";
      solverConstructorSignatureExtension += solverInitSignatureExtension;
	  }
	  content.put("ProfilerInclude",profilerInclude);
    content.put("ParserInclude", parserInclude);
	  content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
    content.put("SolverInitSignatureExtension", solverInitSignatureExtension);

	  content.put("NumberOfVariables", String.valueOf(_numberOfVariables));
	  content.put("NumberOfParameters",String.valueOf( _numberOfParameters));
	  content.put("Dimensions",String.valueOf( _dimensions));
	  content.put("Order", String.valueOf(_order));
    
	  writer.write(content.toString());
  }

  
  @Override
  public void writeAbstractHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/AbstractOptimisedADERDGSolverHeader.template");

    content.put("Project", projectName);
    content.put("Solver", solverName);
    content.put("AbstractSolver", getAbstractSolverName(solverName));

    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    String solverInitSignatureExtension        = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler"; 
    }
    content.put("SolverInitSignatureExtension", solverInitSignatureExtension);
    content.put("ProfilerInclude",profilerInclude);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
    
    content.put("NumberOfVariables", String.valueOf(_numberOfVariables));
    content.put("NumberOfParameters",String.valueOf( _numberOfParameters));
    content.put("Dimensions",String.valueOf( _dimensions));
    content.put("Order", String.valueOf(_order));

    String namingSchemes = "";
    for (String name : _namingSchemeNames) {
      namingSchemes += "    " + "class "+name.substring(0, 1).toUpperCase() + name.substring(1) + ";\n";
    }
    content.put("NamingSchemes", namingSchemes);
    
    writer.write(content.toString());
  }
  
  @Override
  public void writeAbstractImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
        
    Helpers.invokeCodeGenerator(solverName, _numberOfVariables, _numberOfParameters, _order, _isLinear, _dimensions,
        _microarchitecture, _pathToLibxsmm, _enableDeepProfiler);
        
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/AbstractOptimisedADERDGSolverImplementation.template"); //OptimisedADERDGSolverInCGeneratedCode_withConverter for debug (can switch SpaceTimePredictor and RiemannSolver to generic if needed)
    
	  content.put("Project", projectName);
	  content.put("Solver", solverName);
    content.put("AbstractSolver", getAbstractSolverName(solverName));
	  //
	  String profilerInclude                     = "";
	  String solverConstructorSignatureExtension = "";
	  String solverConstructorArgumentExtension  = "";
    String solverInitCallExtension             = "";    
    
	  if (_enableProfiler) {
		  profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
		  solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler";
		  solverConstructorArgumentExtension  += ", std::move(profiler)";
      
      if(_enableDeepProfiler) {
        content.put("DeepProfilerArg", ", _profiler.get()");
      } else {
        content.put("DeepProfilerArg", "");  
      }
		  
      content.put("BeforeSpaceTimePredictor", "  _profiler->start(\"spaceTimePredictor\");");  
      content.put("AfterSpaceTimePredictor", "  _profiler->stop(\"spaceTimePredictor\");"); 
      content.put("BeforeSolutionUpdate", "  _profiler->start(\"solutionUpdate\");"); 
      content.put("AfterSolutionUpdate", "  _profiler->stop(\"solutionUpdate\");"); 
      content.put("BeforeVolumeIntegral", "  _profiler->start(\"volumeIntegral\");"); 
      content.put("AfterVolumeIntegral", "  _profiler->stop(\"volumeIntegral\");"); 
      content.put("BeforeSurfaceIntegral", "  _profiler->start(\"surfaceIntegral\");"); 
      content.put("AfterSurfaceIntegral", "  _profiler->stop(\"surfaceIntegral\");"); 
      content.put("BeforeRiemannSolver", "  _profiler->start(\"riemannSolver\");"); 
      content.put("AfterRiemannSolver", "  _profiler->stop(\"riemannSolver\");"); 
      content.put("BeforeBoundaryConditions", "  _profiler->start(\"boundaryConditions\");"); 
      content.put("AfterBoundaryConditions", "  _profiler->stop(\"boundaryConditions\");"); 
      content.put("BeforeStableTimeStepSize", "  _profiler->start(\"stableTimeStepSize\");"); 
      content.put("AfterStableTimeStepSize", "  _profiler->stop(\"stableTimeStepSize\");"); 
      content.put("BeforeSolutionAdjustment", "  _profiler->start(\"solutionAdjustment\");"); 
      content.put("AfterSolutionAdjustment", "  _profiler->stop(\"solutionAdjustment\");"); 
      content.put("BeforeFaceUnknownsProlongation", "  _profiler->start(\"faceUnknownsProlongation\");"); 
      content.put("AfterFaceUnknownsProlongation", "  _profiler->stop(\"faceUnknownsProlongation\");"); 
      content.put("BeforeFaceUnknownsRestriction", "  _profiler->start(\"faceUnknownsRestriction\");"); 
      content.put("AfterFaceUnknownsRestriction", "  _profiler->stop(\"faceUnknownsRestriction\");"); 
      content.put("BeforeVolumeUnknownsProlongation", "  _profiler->start(\"volumeUnknownsProlongation\");"); 
      content.put("AfterVolumeUnknownsProlongation", "  _profiler->stop(\"volumeUnknownsProlongation\");"); 
      content.put("BeforeVolumeUnknownsRestriction", "  _profiler->start(\"volumeUnknownsRestriction\");"); 
      content.put("AfterVolumeUnknownsRestriction", "  _profiler->stop(\"volumeUnknownsRestriction\");");
	  } else {
      content.put("DeepProfilerArg", "");  
      content.put("BeforeSpaceTimePredictor", "");  
      content.put("AfterSpaceTimePredictor", ""); 
      content.put("BeforeSolutionUpdate", ""); 
      content.put("AfterSolutionUpdate", ""); 
      content.put("BeforeVolumeIntegral", ""); 
      content.put("AfterVolumeIntegral", ""); 
      content.put("BeforeSurfaceIntegral", ""); 
      content.put("AfterSurfaceIntegral", ""); 
      content.put("BeforeRiemannSolver", ""); 
      content.put("AfterRiemannSolver", ""); 
      content.put("BeforeBoundaryConditions", ""); 
      content.put("AfterBoundaryConditions", ""); 
      content.put("BeforeStableTimeStepSize", ""); 
      content.put("AfterStableTimeStepSize", ""); 
      content.put("BeforeSolutionAdjustment", ""); 
      content.put("AfterSolutionAdjustment", ""); 
      content.put("BeforeFaceUnknownsProlongation", ""); 
      content.put("AfterFaceUnknownsProlongation", ""); 
      content.put("BeforeFaceUnknownsRestriction", ""); 
      content.put("AfterFaceUnknownsRestriction", ""); 
      content.put("BeforeVolumeUnknownsProlongation", ""); 
      content.put("AfterVolumeUnknownsProlongation", ""); 
      content.put("BeforeVolumeUnknownsRestriction", ""); 
      content.put("AfterVolumeUnknownsRestriction", "");
	  }
	  if (_hasConstants) {
		  solverConstructorSignatureExtension += ", exahype::Parser::ParserView constants"; // TODO(Dominic): Why pass by value? 
      solverInitCallExtension = ", constants";
	  }
    
	  content.put("SolverInitCallExtension",solverInitCallExtension);
	  content.put("ProfilerInclude",profilerInclude);
	  content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
	  content.put("SolverConstructorArgumentExtension", solverConstructorArgumentExtension);
	  
	  writer.write(content.toString());
  }
  
  @Override
  public void writeUserImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/GenericADERDGSolverInCUserCode.template");
    
    content.put("Project", projectName);
    content.put("Solver", solverName);
    
    content.put("Elements",  String.valueOf( _numberOfParameters+_numberOfVariables));
    content.put("Dimensions",String.valueOf(_dimensions));

    String SolverInitSignatureExtension = "";
    if (_hasConstants) {
        SolverInitSignatureExtension = ", exahype::Parser::ParserView& constants";
    }
    content.put("SolverInitSignatureExtension", SolverInitSignatureExtension);
    //
    String solverConstructorArgumentExtension  = "";
    String solverConstructorSignatureExtension = "";
    String SolverInitCallExtension             = "";
    if (_enableProfiler) {
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler";
      solverConstructorArgumentExtension  += ", std::move(profiler)";
    }
    if (_hasConstants) {
      solverConstructorSignatureExtension += ", exahype::Parser::ParserView& constants";
       SolverInitCallExtension = ", constants";
    }

    content.put("SolverInitCallExtension",SolverInitCallExtension);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
    content.put("SolverConstructorArgumentExtension", solverConstructorArgumentExtension);
    
    // user functions
    int digits = String.valueOf(_numberOfVariables + _numberOfParameters).length();
    
    String adjustSolution = "  // State variables:\n";
    for (int i = 0; i < _numberOfVariables; i++) {
      adjustSolution += "  Q[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables-1) adjustSolution += "\n";
    }
    if (_numberOfParameters>0) {
      adjustSolution += "  // Material parameters:\n";
      for (int i = 0; i < _numberOfParameters; i++) {
        adjustSolution += "  Q[" + String.format("%" + digits + "d", _numberOfVariables+i) + "] = 0.0;";
        if (i<_numberOfParameters-1) adjustSolution += "\n";
      }
    }

    String eigenvalues = "";
    for (int i = 0; i < _numberOfVariables; i++) {
      eigenvalues += "  lambda[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables-1) eigenvalues += "\n";
    }

    String flux = "";
    for (int d=0; d<_dimensions; ++d) {
      for (int i = 0; i < _numberOfVariables; i++) {
        flux += "  F["+d+"][" + String.format("%" + digits + "d", i) + "] = 0.0;";
        if (i<_numberOfVariables-1) flux += "\n";
      }
      if (d<_dimensions-1) {
        flux += "\n\n";    
      }
    }
    
    String source = "";
    for (int i = 0; i < _numberOfVariables; i++) {
      source += "  S[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables-1) source += "\n";
    }
    
    String boundaryValues = "";
    for (int i = 0; i < _numberOfVariables; i++) {
      boundaryValues += "  stateOut[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables-1) boundaryValues += "\n";
    }
    boundaryValues += "\n\n";
    for (int i = 0; i < _numberOfVariables; i++) {
      boundaryValues += "  fluxOut[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables-1) boundaryValues += "\n";
    }
    
    String ncp = "";
    for (int i = 0; i < _numberOfVariables; i++) {
      ncp += "  BgradQ[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables-1) ncp += "\n";
    }
    
    String matrixb = "";
    for (int i = 0; i < _numberOfVariables*_numberOfVariables; i++) {
      matrixb += "  Bn[" + String.format("%" + digits + "d", i) + "] = 0.0;";
      if (i<_numberOfVariables*_numberOfVariables-1) matrixb += "\n";
    }
    
    content.put("AdjustedSolutionValues",adjustSolution);
    content.put("Eigenvalues",eigenvalues);
    content.put("Flux",flux);
    content.put("Source",source);
    content.put("BoundaryValues",boundaryValues);
    content.put("NonConservativeProduct",ncp);
    content.put("MatrixB",matrixb);
    
    writer.write(content.toString());
  }
  
  @Deprecated
  @Override
  public void writeGeneratedImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) {}

  public void writeUserPDE(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    // @todo Implement
    System.err.println("C-style kernels do not have a PDF.f90.\n");
  }


  public void writeTypesDef(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    // @todo Implement
    System.err.println("C-style kernels do not have a typesDef.f90.\n");
  }
  
  @Override
  public boolean supportsVariables() {
    return true;
  }
}
