package eu.exahype.solvers;

import java.io.BufferedWriter;
import java.io.IOException;
import java.util.Set;

import eu.exahype.io.IOUtils;
import eu.exahype.io.SourceTemplate;

public class GenericADERDG implements Solver {
  public static final String Identifier = "generic::fluxes";

  private int _dimensions;
  private int _numberOfVariables;
  private int _numberOfParameters;
  private Set<String> _namingSchemeNames;
  private int _order;
//  private int _patchSize;
  private boolean _enableProfiler;
  private boolean _hasConstants;
  private boolean _isLinear;
  private boolean _isFortran;

  public GenericADERDG(int dimensions, int numberOfVariables, int numberOfParameters, Set<String> namingSchemeNames,
      int order, boolean enableProfiler, boolean hasConstants, boolean isLinear, boolean isFortran) {
    _dimensions         = dimensions;
    _numberOfVariables  = numberOfVariables;
    _numberOfParameters = numberOfParameters;
    _namingSchemeNames  = namingSchemeNames;
    _order              = order;
//    _patchSize = patchSize;
    _enableProfiler     = enableProfiler;
    _hasConstants       = hasConstants;
    _isLinear           = isLinear;
    _isFortran          = isFortran;
  }

  @Override
  public void writeHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/GenericADERDGSolverHeader.template");

    content.put("Project", projectName);
    content.put("Solver", solverName);

    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    String SolverInitSignatureExtension        = "";
    String ParserInclude                       = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler"; 
    }
    if (_hasConstants) {
      SolverInitSignatureExtension = ", exahype::Parser::ParserView& constants";
      ParserInclude = "#include \"exahype/Parser.h\"";
      solverConstructorSignatureExtension += SolverInitSignatureExtension;
    }
    content.put("SolverInitSignatureExtension", SolverInitSignatureExtension);
    content.put("ParserInclude", ParserInclude);
    content.put("ProfilerInclude",profilerInclude);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);

    content.put("NumberOfVariables", String.valueOf(_numberOfVariables));
    content.put("NumberOfParameters",String.valueOf( _numberOfParameters));
    content.put("Dimensions",String.valueOf( _dimensions));
    content.put("Order", String.valueOf(_order));

    writer.write(content.toString());
  }
  
  /**
   * @deprecated This will be removed soon.
   */
  @Override
  public void writeGeneratedImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
    // do nothing
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
      eigenvalues += "  lambda[" + String.format("%" + digits + "d", i) + "] = 1.0;";
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
  
  @Override
  public void writeAbstractHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/AbstractGenericADERDGSolverHeader.template");

    content.put("Project", projectName);
    content.put("Solver", solverName);

    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    String SolverInitSignatureExtension        = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler"; 
    }
    content.put("SolverInitSignatureExtension", SolverInitSignatureExtension);
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
  public void writeAbstractImplementation(BufferedWriter writer,
      String solverName, String projectName) throws IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/AbstractGenericADERDGSolverImplementation.template");
    
    content.put("Project", projectName);
    content.put("Solver", solverName);
    //
    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    String solverConstructorArgumentExtension  = "";
    String SolverInitCallExtension             = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler";
      solverConstructorArgumentExtension  += ", std::move(profiler)";
      
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
      content.put("BeforePointSource", "  _profiler->start(\"pointSource\");"); //TODO KD adapt name
      content.put("AfterPointSource", "  _profiler->stop(\"pointSource\");");
    } else {
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
      content.put("BeforePointSource", ""); //TODO KD adapt name
      content.put("AfterPointSource", "");
    }
    if (_hasConstants) {
      solverConstructorSignatureExtension += ", exahype::Parser::ParserView constants"; // TODO(Dominic): Why pass by value?
       SolverInitCallExtension = ", constants";
      
    }

    content.put("SolverInitCallExtension",SolverInitCallExtension);
    content.put("ProfilerInclude",profilerInclude);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
    content.put("SolverConstructorArgumentExtension", solverConstructorArgumentExtension);
    
    //TODO JMG move this to template when using template engine
    //     SK: I just moved the logic to C++ level. There is no reason this could not stay there.
    //     If we have a decent template engine, you can move it from C++ to template level, thought.
    String linearStr = _isLinear ? "Linear" : "Nonlinear";
    content.put("NonlinearOrLinear", linearStr);
    content.put("defineLinear", _isLinear ? "#define isLinear" : "/* #define isLinear */");
    content.put("defineFortran", _isFortran ? "#define isFortran" : "/* #define isFortran */");
    content.put("Language", _isFortran ? "fortran" : "c");
    
    writer.write(content.toString());
  }

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

  public boolean supportsVariables() {
    return true;
  }
}
