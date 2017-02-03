package eu.exahype.solvers;

public interface Solver {
 
  /**
   * @return true if the solver supports generation of Variables classes.
   */
  public boolean supportsVariables();
  
  public void writeHeader(java.io.BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException;
  
  public void writeGeneratedImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException;
  
  public void writeUserImplementation(java.io.BufferedWriter writer, String solverName,
      String projectName) throws java.io.IOException;
  
}
