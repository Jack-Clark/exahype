package eu.exahype.solvers;

import java.io.BufferedWriter;
import java.io.IOException;
import java.util.Set;

import eu.exahype.io.IOUtils;
import eu.exahype.io.SourceTemplate;

class GenericFiniteVolumesInC implements Solver {
  private String _type;
  private int _dimensions;
  private int _numberOfVariables;
  private int _numberOfParameters;
  private int _patchSize;
  private int _ghostLayerWidth;
  private Set<String> _namingSchemeNames;

  private boolean _enableProfiler;
  private boolean _hasConstants;

  public GenericFiniteVolumesInC(String type, int dimensions, int numberOfVariables, int numberOfParameters, Set<String> namingSchemeNames, int patchSize,
      int ghostLayerWidth, boolean enableProfiler, boolean hasConstants) {
    _type=type;
    _dimensions = dimensions;
    _numberOfVariables = numberOfVariables;
    _numberOfParameters = numberOfParameters;
    _namingSchemeNames = namingSchemeNames;
    _patchSize = patchSize;
    _ghostLayerWidth=ghostLayerWidth;
    _enableProfiler = enableProfiler;
    _hasConstants = hasConstants;
  }

  public void writeHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/GenericFiniteVolumesSolverHeader.template");

    content.put("Project", projectName);
    content.put("Solver", solverName);

    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler"; 
    }
    if (_hasConstants) {
      solverConstructorSignatureExtension += ", exahype::Parser::ParserView constants"; // TODO(Dominic): Why pass by value? 
    }


    content.put("NumberOfVariables", String.valueOf(_numberOfVariables));
    content.put("NumberOfParameters",String.valueOf( _numberOfParameters));
    content.put("Dimensions",String.valueOf( _dimensions));
    //content.put("Order", String.valueOf(_order)); // Goudonov is 2nd order or so. Should probably tell here.

    content.put("ProfilerInclude",profilerInclude);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);

    writer.write(content.toString());
  }
  
  public void writeUserImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
            "eu/exahype/solvers/templates/GenericFiniteVolumesSolverInCUserCode.template");
    
    content.put("Project", projectName);
    content.put("Solver", solverName);
    
    content.put("Elements",  String.valueOf( _numberOfParameters+_numberOfVariables));
    content.put("Dimensions",String.valueOf(_dimensions));
    
    //    String SolverInitSignatureExtension = "";
    String SolverInitSignatureExtension = "";
    if (_hasConstants) {
      SolverInitSignatureExtension = ", exahype::Parser::ParserView& constants";
    }
    content.put("SolverInitSignatureExtension", SolverInitSignatureExtension);
    
    // 
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
    String SolverInitCallExtension             = "";
    if (_hasConstants) {
       SolverInitCallExtension = ", constants";
    }
    
    content.put("SolverInitCallExtension",SolverInitCallExtension);

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
      boundaryValues += "  stateOutside[" + String.format("%" + digits + "d", i) + "] = 0.0;";
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

  /**
   * @deprecated This will be removed after the optimised kernel code generation
   * supports the abstract solvers.
   */
  public void writeGeneratedImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
   // do nothing
  }
  
  @Override
  public void writeAbstractHeader(BufferedWriter writer, String solverName,
      String projectName) throws IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/solvers/templates/AbstractGenericFiniteVolumesSolverHeader.template");

    content.put("Project", projectName);
    content.put("Solver", solverName);

    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler"; 
    }

    content.put("ProfilerInclude",profilerInclude);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
    
    content.put("NumberOfVariables", String.valueOf(_numberOfVariables));
    content.put("NumberOfParameters",String.valueOf( _numberOfParameters));
    content.put("Dimensions",String.valueOf( _dimensions));
    content.put("PatchSize", String.valueOf(_patchSize));
    content.put("GhostLayerWidth",String.valueOf(_ghostLayerWidth));
    
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
        "eu/exahype/solvers/templates/AbstractGenericFiniteVolumesSolverInCImplementation.template");

    content.put("Project", projectName);
    content.put("Solver", solverName);
    
    content.put("FiniteVolumesType", _type);
    //
    String profilerInclude                     = "";
    String solverConstructorSignatureExtension = "";
    String solverConstructorArgumentExtension  = "";
    if (_enableProfiler) {
      profilerInclude                        = "#include \"exahype/profilers/Profiler.h\"";
      solverConstructorSignatureExtension += ", std::unique_ptr<exahype::profilers::Profiler> profiler";
      solverConstructorArgumentExtension  += ", std::move(profiler)";
    }
    if (_hasConstants) {
      solverConstructorSignatureExtension += ", exahype::Parser::ParserView constants"; // TODO(Dominic): Why pass by value? 
    }
    content.put("ProfilerInclude",profilerInclude);
    content.put("SolverConstructorSignatureExtension", solverConstructorSignatureExtension);
    content.put("SolverConstructorArgumentExtension", solverConstructorArgumentExtension);
    //
    content.put("NumberOfVariables", String.valueOf(_numberOfVariables));
    content.put("NumberOfParameters",String.valueOf( _numberOfParameters));

    // TODO(Dominic): Add profilers
    
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
  
  @Override
  public boolean supportsVariables() {
    return true;
  }
}
