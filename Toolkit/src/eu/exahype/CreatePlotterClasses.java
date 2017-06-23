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
import eu.exahype.io.SourceTemplate;

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
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/plotters/templates/UserOnTheFlyPostProcessingHeader.template");

    content.put("Project", _projectName);
    content.put("plotterName", plotterName);

    String FurtherIncludes = "";
    String FurtherClasses = "";
    String PlotterConstructorSignature = "";
    if (_isForLimitingADERDGSolver) {
      FurtherIncludes += "#include \"exahype/solvers/LimitingADERDGSolver.h\"\n";
    }
    if (!_isForLimitingADERDGSolver) {
      FurtherClasses += "/* Forward declaration: */\n";
      FurtherClasses += "class "+_solverName+"; \n";
      FurtherClasses += "\n";
    }
    if (!_isForLimitingADERDGSolver) {
       PlotterConstructorSignature = _solverName + "&  solver";
    } else {
      PlotterConstructorSignature = "exahype::solvers::LimitingADERDGSolver&  solver";
    }
    content.put("FurtherIncludes", FurtherIncludes);
    content.put("FurtherClasses", FurtherClasses);
    content.put("PlotterConstructorSignature", PlotterConstructorSignature);

    writer.write(content.toString());
  }
 
  private void writePlotterImplementation( java.io.BufferedWriter writer, int unknowns, String plotterName ) throws java.io.IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/plotters/templates/UserOnTheFlyPostProcessingImplementation.template");

    content.put("Project", _projectName);
    content.put("plotterName", plotterName);

    // ExaHyPE toolkit authors never heard of https://en.wikipedia.org/wiki/Don%27t_repeat_yourself
    String PlotterConstructorSignature = "";
    if (!_isForLimitingADERDGSolver) {
       PlotterConstructorSignature = _solverName + "&  solver";
    } else {
      PlotterConstructorSignature = "exahype::solvers::LimitingADERDGSolver&  solver";
    }
    content.put("PlotterConstructorSignature", PlotterConstructorSignature);
    content.put("writtenUnknowns", Integer.toString(unknowns));

    writer.write(content.toString());
  }
  
  private void tryWriteDeviceHeader(Writer headerWriter,String plotterName) throws IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
        "eu/exahype/plotters/templates/UserDefinedDeviceHeader.template");

    content.put("Project", _projectName);
    content.put("Device", plotterName);
    content.put("SolverType", _solverType);
    headerWriter.write(content.toString());
    
    System.out.println("create header file for abstract solver superclass Abstract" + plotterName + " ... ok");
    headerWriter.close();
  }
  
  private void tryWriteDeviceImplementation(Writer deviceWriter,String plotterName) throws IOException {
    SourceTemplate content = SourceTemplate.fromRessourceContent(
       "eu/exahype/plotters/templates/UserDefinedDeviceImplementation.template");

    content.put("Project", _projectName);
    content.put("Device", plotterName);
    content.put("SolverType", _solverType);
    deviceWriter.write(content.toString());
    
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
