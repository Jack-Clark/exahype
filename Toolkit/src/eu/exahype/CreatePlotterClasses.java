package eu.exahype;

import java.io.IOException;
import java.io.Writer;

import eu.exahype.analysis.DepthFirstAdapter;
import eu.exahype.node.AAderdgSolver;
import eu.exahype.node.AFiniteVolumesSolver;
import eu.exahype.node.ALimitingAderdgSolver;
import eu.exahype.node.APlotSolution;
import eu.exahype.node.AProject;
import eu.exahype.io.FileSearch;
import eu.exahype.io.IOUtils;

public class CreatePlotterClasses extends DepthFirstAdapter {
  public Boolean valid = true;

  private DirectoryAndPathChecker _directoryAndPathChecker;

  private String                  _projectName;
  private int                     _dimensions;
  private String                  _solverName;
  private int                     _plotterCounter;
  private boolean                 _isForLimitingADERDGSolver;
  private String                  _solverType;

  
  public CreatePlotterClasses(DirectoryAndPathChecker directoryAndPathChecker) {
    _directoryAndPathChecker = directoryAndPathChecker;
  }

  @Override
  public void inAProject(AProject node) {
    _projectName     = node.getName().getText();
  }

  @Override
  public void inAAderdgSolver(AAderdgSolver node) {
    _isForLimitingADERDGSolver = false;
    _solverName                = node.getName().getText();
    _plotterCounter            = -1;
    _solverType                = "ADERDG";
  }

  @Override
  public void inAFiniteVolumesSolver(AFiniteVolumesSolver node) {
    _isForLimitingADERDGSolver = false;
    _solverName                = node.getName().getText();
    _plotterCounter            = -1;
    _solverType                = "FiniteVolumes";
  }
  
  @Override
  public void inALimitingAderdgSolver(ALimitingAderdgSolver node) {
    _isForLimitingADERDGSolver = true;
    _solverName                = node.getName().getText();
    _plotterCounter            = -1;
    _solverType                = "ADERDG"; // TODO(Dominic): We currently use the ADERDG degrees of freedom as the plotter input for the LimitingADERDGSolver
  }
  
  private void writePlotterHeader( java.io.BufferedWriter writer, int unknowns, String plotterName ) throws java.io.IOException {
  eu.exahype.solvers.Helpers.writeHeaderCopyright(writer);
  writer.write( "#include \"exahype/plotters/Plotter.h\"\n" );
  if (_isForLimitingADERDGSolver) {
    writer.write( "#include \"exahype/solvers/LimitingADERDGSolver.h\"\n" );
  }
  
    writer.write( "namespace " + _projectName + "{\n");
    writer.write( "  class " + plotterName + ";\n");
  if (!_isForLimitingADERDGSolver) {
    writer.write( "\n" );
    writer.write( "  /**\n" );
    writer.write( "   * Forward declaration\n" );
    writer.write( "   */\n" );
    writer.write( "  class " + _solverName + ";\n" );
  }
    writer.write("}\n\n\n");
    writer.write( "\n" );
    writer.write( "\n" );
    writer.write( "class " + _projectName + "::" + plotterName + ": public exahype::plotters::Plotter::UserOnTheFlyPostProcessing{\n" );
    writer.write( "  public:\n" );
    if (!_isForLimitingADERDGSolver) {
      writer.write( "  " + plotterName + "(" + _solverName + "&  solver);\n" );
    } else {
      writer.write( "  " + plotterName + "(exahype::solvers::LimitingADERDGSolver&  solver);\n" );
    }
    writer.write( "  virtual ~" + plotterName + "();\n" );
    writer.write( "  void startPlotting(double time) override;\n");
    writer.write( "  void finishPlotting() override;\n");
    writer.write( "  void mapQuantities(\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, double>& x,\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, int>&    pos,\n" );
    writer.write( "    double* Q,\n" );
    writer.write( "    double* outputQuantities,\n" );
    writer.write( "    double timeStamp) override;\n" );
    writer.write( "};\n" );
  }
 
  private void writePlotterImplementation( java.io.BufferedWriter writer, int unknowns, String plotterName ) throws java.io.IOException {
    writer.write( "#include \"" + plotterName + ".h\"\n" );
    writer.write( "\n" );
    writer.write( "\n" );
    if (!_isForLimitingADERDGSolver) {
      writer.write( _projectName + "::" + plotterName + "::" + plotterName + "(" + _solverName + "&  solver) {\n" );
    } else {
      writer.write( _projectName + "::" + plotterName + "::" + plotterName + "(exahype::solvers::LimitingADERDGSolver&  solver) {\n" );
    }
    writer.write( "  // @todo Please insert your code here\n" );
    writer.write( "}\n" );
    writer.write( "\n" );
    writer.write( "\n" );
    writer.write( _projectName + "::" + plotterName + "::~" + plotterName + "() {\n" );
    writer.write( "  // @todo Please insert your code here\n" );
    writer.write( "}\n" );
    writer.write( "\n" );
    writer.write( "\n" );
    writer.write( "void " + _projectName + "::" + plotterName + "::startPlotting(double time) {\n" );
    writer.write( "  // @todo Please insert your code here\n" );
    writer.write( "}\n" );
    writer.write( "\n" );
    writer.write( "\n" );
    writer.write( "void " + _projectName + "::" + plotterName + "::finishPlotting() {\n" );
    writer.write( "  // @todo Please insert your code here\n" );
    writer.write( "}\n" );
    writer.write( "\n" );
    writer.write( "\n" );
    writer.write( "void " + _projectName + "::" + plotterName + "::mapQuantities(\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, double>& offsetOfPatch,\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, double>& sizeOfPatch,\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, double>& x,\n" );
    writer.write( "    const tarch::la::Vector<DIMENSIONS, int>&    pos,\n" );
    writer.write( "    double* Q,\n" );
    writer.write( "    double* outputQuantities,\n" );
    writer.write( "    double timeStamp\n" );
    writer.write( ") {\n" );
    writer.write( "  for (int i=0; i<" + unknowns + "; i++){ \n" );
    writer.write( "    outputQuantities[i] = Q[i];\n" );
    writer.write( "  }\n" );
    writer.write( "}\n" );
    writer.write( "\n" );
    writer.write( "\n" );
  }
  
  private void tryWriteDeviceHeader(Writer headerWriter,String plotterName) throws IOException {
    String content = IOUtils.convertRessourceContentToString(
        "eu/exahype/plotters/templates/UserDefinedDeviceHeader.template");
    content = content.replaceAll("\\{\\{Project\\}\\}",_projectName);
    content = content.replaceAll("\\{\\{Device\\}\\}",plotterName);
    content = content.replaceAll("\\{\\{SolverType\\}\\}",_solverType);
    headerWriter.write(content);
    
    System.out.println("create header file for abstract solver superclass Abstract" + plotterName + " ... ok");
    headerWriter.close();
  }
  
  private void tryWriteDeviceImplementation(Writer deviceWriter,String plotterName) throws IOException {
    String content = IOUtils.convertRessourceContentToString(
        "eu/exahype/plotters/templates/UserDefinedDeviceImplementation.template");
    content = content.replaceAll("\\{\\{Project\\}\\}",_projectName);
    content = content.replaceAll("\\{\\{Device\\}\\}",plotterName);
    content = content.replaceAll("\\{\\{SolverType\\}\\}",_solverType);
    deviceWriter.write(content);
    
    System.out.println("create implementation file for user defined plotter " + plotterName + " ... ok");
    deviceWriter.close();
  }
 
  @Override
  public void inAPlotSolution(APlotSolution node) {
    _plotterCounter++;
    try {
//      String plotterName = _solverName + "_Plotter" + Integer.toString(_plotterCounter);
      String plotterName = node.getName().getText();
      String plotterType = node.getPlotterType().getText();
       
      java.io.File header         = new java.io.File(_directoryAndPathChecker.outputDirectory.getAbsolutePath() + "/" + plotterName + ".h");
      java.io.File implementation = new java.io.File(_directoryAndPathChecker.outputDirectory.getAbsolutePath() + "/" + plotterName + ".cpp");
      
      header = FileSearch.relocate(header);
      implementation = FileSearch.relocate(implementation);

      java.io.BufferedWriter headerWriter         = null;
      java.io.BufferedWriter implementationWriter = null;
          
      if (header.exists()) {
        System.err.println( "WARNING: file " + header.getName() + " already exists. Is not overwritten" );  
      }
      else {
        headerWriter = new java.io.BufferedWriter(new java.io.FileWriter(header.getAbsoluteFile()));
      }

      if (implementation.exists()) {
        System.err.println( "WARNING: file " + implementation.getName() + " already exists. Is not overwritten" );  
      }
      else {
        implementationWriter = new java.io.BufferedWriter(new java.io.FileWriter(implementation.getAbsoluteFile()));
      }
      
      int unknowns = java.lang.Integer.parseInt( node.getVariables().getText() );

      
      if (headerWriter!=null && plotterType.equals("user::defined")) { // write the Device
        if ( headerWriter!=null ) {
          tryWriteDeviceHeader(headerWriter,plotterName);
          headerWriter.close();
        }
        if ( implementationWriter!=null ) {
          tryWriteDeviceImplementation(implementationWriter,plotterName);
          implementationWriter.close();
        }
      } else { // write the UserOnTheFlyPostProcessing files
        if ( headerWriter!=null ) {
          writePlotterHeader( headerWriter, unknowns, plotterName );
          headerWriter.close();
        }
        if ( implementationWriter!=null ) {
          writePlotterImplementation( implementationWriter, unknowns, plotterName );
          implementationWriter.close();
        }
      }
    } catch (Exception exc) {
      System.err.println("ERROR: " + exc.toString());
      exc.printStackTrace();
      valid = false;
      // Should throw/pass exception instead.
    }
  }
}
