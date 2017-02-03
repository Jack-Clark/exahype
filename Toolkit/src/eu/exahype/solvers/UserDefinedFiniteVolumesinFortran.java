package eu.exahype.solvers;

public class UserDefinedFiniteVolumesinFortran implements Solver {
  public static final String Identifier = UserDefinedFiniteVolumesinC.Identifier;

  private int _numberOfVariables;
  private int _numberOfParameters;
  private int _patchSize;
  private boolean _hasConstants;

  public UserDefinedFiniteVolumesinFortran(int dimensions, int numberOfVariables, int numberOfParameters, int patchSize, boolean enableProfiler, boolean hasConstants) {
    _numberOfVariables  = numberOfVariables;
    _numberOfParameters = numberOfParameters;
    _patchSize = patchSize;
    _hasConstants = hasConstants;
  }

  public void writeHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    // @todo
    System.err.println("not implemented yet\n");
  }

  public void writeGeneratedImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
    // @todo Implement
    System.err.println("not implemented yet\n");
  }

  public void writeUserImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException {
    // @todo Implement
    System.err.println("not implemented yet\n");
  }
  public void writeUserPDE(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    // @todo Implement
    System.err.println("not implemented yet\n");
  }
  public void writeTypesDef(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    // @todo Implement
    System.err.println("not implemented yet\n");
  }
  
  @Override
  public boolean supportsVariables() {
    return false;
  }
}
