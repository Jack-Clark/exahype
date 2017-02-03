package eu.exahype.variables;

import java.io.BufferedWriter;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import eu.exahype.IOUtils;
import eu.exahype.node.AAderdgSolver;
import eu.exahype.node.AFiniteVolumesSolver;
import eu.exahype.node.ALimitingAderdgSolver;
import eu.exahype.node.AVariables;
import eu.exahype.node.AWithNameVariable;
import eu.exahype.node.AWithoutNameVariable;
import eu.exahype.node.PSolver;
import eu.exahype.node.PVariable;

public class Variables {
  int                 _dimensions;
  Map<String,Integer> _variablesMap;
  int                 _numberOfVariables;
  Map<String,Integer> _parametersMap;
  int                 _numberOfParameters;
  Map<String,Integer> _primitivesMap;
  int                 _numberOfPrimitives;
  
  public Map<String, Integer> getVariablesMap() {
    return _variablesMap;
  }

  public int getNumberOfVariables() {
    return _numberOfVariables;
  }

  public Map<String, Integer> getParametersMap() {
    return _parametersMap;
  }

  public int getNumberOfParameters() {
    return _numberOfParameters;
  }

  private static List<PVariable> getVariablesAsList(PSolver node) {
    List<PVariable> variablesAsList = null;
    if (node instanceof AAderdgSolver) {
      variablesAsList = ((AVariables)((AAderdgSolver) node).getVariables()).getVariable();
    } else if (node instanceof ALimitingAderdgSolver) {
      variablesAsList = ((AVariables)((ALimitingAderdgSolver) node).getVariables()).getVariable();
    } else if (node instanceof AFiniteVolumesSolver) {
      variablesAsList = ((AVariables)((AFiniteVolumesSolver) node).getVariables()).getVariable();
    } 
    return variablesAsList;
  }
  
  private static List<PVariable> getParametersAsList(PSolver node) {
    List<PVariable> parametersAsList = null;
    if (node instanceof AAderdgSolver) {
      parametersAsList = ((AVariables)((AAderdgSolver) node).getParameters()).getVariable();
    } else if (node instanceof ALimitingAderdgSolver) {
      parametersAsList = ((AVariables)((ALimitingAderdgSolver) node).getParameters()).getVariable();
    } else if (node instanceof AFiniteVolumesSolver) {
      parametersAsList = ((AVariables)((AFiniteVolumesSolver) node).getParameters()).getVariable();
    } 
    return parametersAsList;
  }
  
  private static List<PVariable> getPrimitivesAsList(PSolver node) {
    List<PVariable> primivitesAsList = null;
    if (node instanceof AAderdgSolver) {
      primivitesAsList = ((AVariables)((AAderdgSolver) node).getPrimitives()).getVariable();
    } else if (node instanceof ALimitingAderdgSolver) {
      primivitesAsList = ((AVariables)((ALimitingAderdgSolver) node).getPrimitives()).getVariable();
    } else if (node instanceof AFiniteVolumesSolver) {
      primivitesAsList = ((AVariables)((AFiniteVolumesSolver) node).getPrimitives()).getVariable();
    } 
    return primivitesAsList;
  }
  
  private static Map<String,Integer> parseVariables(List<PVariable> variablesAsList) {
    Map<String, Integer> map = new LinkedHashMap<String, Integer>(variablesAsList.size());
    
    for (PVariable pVariable :  variablesAsList) {
      if (pVariable instanceof AWithNameVariable) {
        AWithNameVariable variable = (AWithNameVariable) pVariable;
        String name       = variable.getName().getText();
        int multiplicity  = Integer.parseInt(variable.getMultiplicity().getText());
        
        if (multiplicity>0) {
          map.put(name, multiplicity);
        }
      } else if (pVariable instanceof AWithoutNameVariable) {
        AWithoutNameVariable variable = (AWithoutNameVariable) pVariable;
        String name       = "Q";
        int multiplicity  = Integer.parseInt(variable.getMultiplicity().getText());
        
        if (multiplicity > 0) {
          map.put(name, multiplicity);
        }
      } else {
        System.out.println("ERROR: I do not know how to handle variable type "+pVariable.getClass().toString()+"!");
        System.exit(1);
        return null;
      }
    }
    return map;
  }
  
  /**
   * @note We rely on a linked hash map here that does not change the order of the variables.
   */
  public static Map<String, Integer> readVariables(PSolver node) {    
    List<PVariable> variablesAsList = getVariablesAsList(node);
    if (variablesAsList!=null) {
      return parseVariables(variablesAsList); 
    } else {
      System.out.println("ERROR: I do not know how to handle solver type "+node.getClass().toString()+"!");
      System.exit(1);
      return null;
    }
  }
  /**
   * @note We rely on a linked hash map here that does not change the order of the parameters.
   */
  public static Map<String, Integer> readParameters(PSolver node) {
    List<PVariable> parametersAsList = getParametersAsList(node);
    if (parametersAsList!=null) {
      return parseVariables(parametersAsList); 
    } else {
      System.out.println("ERROR: I do not know how to handle solver type "+node.getClass().toString()+"!");
      System.exit(1);
      return null;
    }
  }
  
  /**
   * @note We rely on a linked hash map here that does not change the order of the parameters.
   */
  public static Map<String, Integer> readPrimitives(PSolver node) {
    List<PVariable> parametersAsList = getPrimitivesAsList(node);
    if (parametersAsList!=null) {
      return parseVariables(parametersAsList); 
    } else {
      System.out.println("ERROR: I do not know how to handle solver type "+node.getClass().toString()+"!");
      System.exit(1);
      return null;
    }
  }
  
  public static int sumMultiplicities(Map<String,Integer> variables) {
    int sumOfMultiplicities = 0;
    for (String key : variables.keySet()) {
      sumOfMultiplicities += variables.get(key);
    }
    
    return sumOfMultiplicities;
  }
  
  public Variables(PSolver node, int dimensions) {
    _variablesMap       = readVariables(node);
    _parametersMap      = readParameters(node);
    _primitivesMap      = readPrimitives(node);
    _numberOfVariables  = sumMultiplicities(_variablesMap);
    _numberOfParameters = sumMultiplicities(_parametersMap);
    _numberOfPrimitives = sumMultiplicities(_primitivesMap);
    _dimensions         = dimensions;
    
    assert _numberOfPrimitives==_numberOfVariables;
  }
  
  public Variables(Map<String, Integer> variablesMap, Map<String, Integer> parametersMap, Map<String, Integer> primitivesMap, int dimensions) {
    _variablesMap       = variablesMap;
    _parametersMap      = parametersMap;
    _primitivesMap      = primitivesMap;
    _numberOfVariables  = sumMultiplicities(variablesMap);
    _numberOfParameters = sumMultiplicities(parametersMap);
    _numberOfPrimitives = sumMultiplicities(primitivesMap);
    _dimensions         = dimensions;
    
    assert _numberOfPrimitives==_numberOfVariables;
  }
  
  private String appendVariableGetters(String getters, String identifier, int multiplicity, int offset) {
    String indent  = "    ";
    
    assert multiplicity > 0;
    if (multiplicity==1) {
      // ex: double rho() const;
      getters += indent + "double "+identifier+"() const { return _Q["+offset+"]; }\n\n";
    } else {
      // ex: double v(int index) const;
      getters += indent + "double "+identifier+"(int index) const {\n"
              +  indent + "  assertion(index >= 0 && index<"+multiplicity+");\n"
              +  indent + "  return _Q["+offset+"+index];\n"
              +  indent + "}\n\n";
      // ex: tarch::la::Vector<3,double> v() const;
      getters += indent + "tarch::la::Vector<"+multiplicity+",double> "+identifier+"() const {\n"
              + indent + "  tarch::la::Vector<"+multiplicity+",double> values(";
      for (int i=0; i<multiplicity; i++) {
        getters += "_Q"+"["+(offset+i)+"]"+( i<multiplicity-1 ? "," : ");\n" );
      }
      getters += indent + "  return values;\n"
              +  indent + "}\n\n";
    }
    return getters;
  }
  
  /**
   * Generate the getters for the variables (and parameters).
   */
  private String createVariablesGetters() {
    String getters = "";
    
    int offset = 0;
    for (String identifier : _variablesMap.keySet()) {
      int multiplicity = _variablesMap.get(identifier);
      getters  = appendVariableGetters(getters,identifier,multiplicity,offset);
      offset  += multiplicity;
    }
    for (String identifier : _parametersMap.keySet()) {
      int multiplicity = _parametersMap.get(identifier);
      getters  = appendVariableGetters(getters,identifier,multiplicity,offset);
      offset  += multiplicity;
    }
    
    return getters;
  }
  
  private String appendVariableSetters(String setters, String identifier, int multiplicity, int offset) {
    String indent  = "    ";
    
    assert multiplicity > 0;
    if (multiplicity==1) {
      setters += indent + "double& "+identifier+"() { return _Q["+offset+"]; }\n\n";
    } else {
      // ex: double& v(int index);
      setters += indent +"double& "+identifier+"(int index) { return _Q["+offset+"+index]; }\n\n";
      
      // ex: void v(const tarch::la::Vector<3,double>& values);
      setters += indent +"void "+identifier+"(const tarch::la::Vector<"+multiplicity+",double>& values) {\n";
      for (int i=0; i<multiplicity; i++) {
        setters += indent +"  "+"*(_Q+"+(offset+i)+")=values["+i+"];\n";
      }
      setters += indent +"}\n\n";
      
      // ex: void v(double v0, double v1, double v2);
      setters += indent +"void "+identifier+"(";
      for (int i=0; i<multiplicity; i++) {
        setters += "double "+identifier+i+( i<multiplicity-1 ? "," : ") {\n" );
      }
      for (int i=0; i<multiplicity; i++) {
        setters += indent +"  "+"*(_Q+"+(offset+i)+")="+identifier+i+";\n";
      }
      setters += indent + "}\n\n";
    }
    return setters;
  }
  
  /**
   * Generate the setters for the variables (and parameters).
   */
  private String createVariablesSetters() {
    String setters = "";
    
    int offset = 0;
    for (String identifier : _variablesMap.keySet()) {
      int multiplicity = _variablesMap.get(identifier);
      setters = appendVariableSetters(setters, identifier, multiplicity, offset);
      offset += multiplicity;
    }
    for (String identifier : _parametersMap.keySet()) {
      int multiplicity = _parametersMap.get(identifier);
      setters = appendVariableSetters(setters, identifier, multiplicity, offset);
      offset += multiplicity;
    }
    
    return setters;
  }
  
  /**
   * Generate the getters for the primitives.
   */
  private String createPrimitivesGetters() {
    String getters = "";
    
    int offset = 0;
    for (String identifier : _primitivesMap.keySet()) {
      int multiplicity = _primitivesMap.get(identifier);
      getters  = appendVariableGetters(getters,identifier,multiplicity,offset);
      offset  += multiplicity;
    }
    
    return getters;
  }
  
  /**
   * Generate the setters for the primitives.
   */
  private String createPrimitivesSetters() {
    String setters = "";
    
    int offset = 0;
    for (String identifier : _primitivesMap.keySet()) {
      int multiplicity = _primitivesMap.get(identifier);
      setters = appendVariableSetters(setters, identifier, multiplicity, offset);
      offset += multiplicity;
    }
    
    return setters;
  }
  
  /**
   * @note The current implementation assumes a column major memory layout
   * of the fluxes.
   */
  private String appendFluxesGetters(String getters, String identifier,int multiplicity, int offset) {
    String indent  = "    ";
    
    assert multiplicity > 0;
    if (multiplicity==1) {
      // ex: double rho(int column) const;  
      getters += indent + "double "+identifier+"(int column) const {\n"
              +  indent + "  assertion(column >= 0 && column<DIMENSIONS);\n"
              +  indent + "  return _F[column]["+offset+"];\n"
              +  indent + "}\n\n";
      // ex: tarch::la::Vector<DIMENSIONS,double> rho() const;
      getters += indent + "tarch::la::Vector<DIMENSIONS,double> "+identifier+"() const {\n"
              +  indent + "  tarch::la::Vector<DIMENSIONS,double> values(";
              for (int i=0; i<_dimensions; i++) {
                getters += "_F["+i+"]["+offset+"]" + ( i<_dimensions-1 ? "," : ");\n" );
              }
      getters += indent + "  return values;\n"
              +  indent + "}\n\n";
      
    } else {
      // ex: double v(int row, int column) const;  
      getters += indent + "double "+identifier+"(int row, int column) const {\n"
              +  indent + "  assertion(row >= 0 && row<"+multiplicity+");\n"
              +  indent + "  assertion(column >= 0 && column<DIMENSIONS);\n"
              +  indent + "  return _F[column]["+offset+"+row];\n"
              +  indent + "}\n\n";
      
      // tarch::la::Vector<3,double> v(int row) const;
      getters += indent + "tarch::la::Vector<DIMENSIONS,double> "+identifier+"(int row) const {\n"
              +  indent + "  assertion(row >= 0 && row<"+multiplicity+");\n"
              +  indent + "  tarch::la::Vector<DIMENSIONS,double> values(";
      for (int i=0; i<_dimensions; i++) {
        getters += "_F["+i+"]["+offset+"+row]" + ( i<_dimensions-1 ? "," : ");\n" );
      }
      getters += indent + "  return values;\n"
              +  indent + "}\n\n";
      
      // ex: tarch::la::Matrix<3,3,double> v() const;
      getters += indent + "tarch::la::Matrix<"+multiplicity+",DIMENSIONS,double> "+identifier+"() const {\n"
              +  indent + "  tarch::la::Matrix<"+multiplicity+",DIMENSIONS,double> values;\n";
      getters += indent + "  values = ";      
      for (int i=0; i<multiplicity; i++) {
        for (int j=0; j<_dimensions; j++) {
          getters += "_F["+j+"]["+(offset+i)+"]" + ( j<_dimensions-1 ? "," : "" );
        }
        getters += ( i<multiplicity-1 ? ",\n"+indent + "           " : ";\n" );
      }
      getters += indent + "  return values;\n"
              +  indent + "}\n\n";
      
    }
    return getters;
  }
  
  private String createFluxesGetters() {
    String getters = "";
    
    int offset = 0;
    for (String identifier : _variablesMap.keySet()) {
      int multiplicity = _variablesMap.get(identifier);
      getters  = appendFluxesGetters(getters,identifier,multiplicity,offset);
      offset  += multiplicity;
    }
    
    return getters;
  }

  private String appendFluxesSetters(String setters, String identifier,
      int multiplicity, int offset) {
    String indent  = "    ";

    assert multiplicity > 0;
    if (multiplicity==1) {
      // ex: double& v(int column);
      setters += indent + "double& "+identifier+"(int column) {\n" 
          +  indent + "  assertion(column >= 0 && column<DIMENSIONS);\n"
          +  indent + "  return _F[column]["+offset+"];\n" 
          +  indent + "}\n\n";

      // ex: void rho(tarch::la::Vector<DIMENSIONS,double>& values); 3D and 2D
      setters += indent +"void "+identifier+"(const tarch::la::Vector<DIMENSIONS,double>& values) {\n";
      for (int j=0; j<2; j++) {
        setters += indent +"  "+"_F["+j+"]["+offset+"]=values["+j+"];\n";
      }
      setters += indent + "  #if DIMENSIONS==3\n";
      setters += indent + "  _F[2]["+offset+"]=values[2];\n";
      setters += indent + "  #endif\n";
      setters += indent + "}\n";
      // ex: void rho(tarch::la::Vector<DIMENSIONS,double>& values); 2.5D
      setters += indent + "#if DIMENSIONS==2\n";
      setters += indent +"/** Setter for 2.5D calculations. Third vector element is ignored.*/\n";
      setters += indent +"void "+identifier+"(const tarch::la::Vector<3,double>& values) {\n";
      for (int j=0; j<2; j++) {
        setters += indent +"  "+"_F["+j+"]["+offset+"]=values["+j+"];\n";
      }
      setters += indent + "}\n";
      setters += indent + "#endif\n\n";

      // ex: void v(double v0, double v1, double v2);  3D and 2.5D
      setters += indent +"/** Setter for 3D and 2.5D calculations. Third argument is ignored for the latter.*/\n";
      setters += indent +"void "+identifier+"(";
      for (int j=0; j<3; j++) {
        setters += "double v"+j+( j<2 ? "," : ") {\n" );
      }
      for (int j=0; j<2; j++) {
        setters += indent +"  _F["+j+"]["+offset+"]=v"+j+";\n";
      }
      setters += indent + "  #if DIMENSIONS==3\n";
      setters += indent + "  _F[2]["+offset+"]=v2;\n";
      setters += indent + "  #endif\n";
      setters += indent + "}\n";
      // ex: void v(double v0, double v1);  2D
      setters += indent + "#if DIMENSIONS==2\n";
      setters += indent + "void "+identifier+"(";
      for (int j=0; j<2; j++) {
        setters += "double v"+j+( j<2-1 ? "," : ") {\n" );
      }
      for (int j=0; j<2; j++) {
        setters += indent +"  "+"_F["+j+"]["+offset+"]=v"+j+";\n";
      }
      setters += indent + "}\n";
      setters += indent + "#endif\n\n";
    } else {
      // ex: double& v(int row, int column);
      setters += indent + "double& "+identifier+"(int row, int column) {\n"
              +  indent + "  assertion(row >= 0 && row<"+multiplicity+");\n"
              +  indent + "  assertion(column >= 0 && column<DIMENSIONS);\n"
              +  indent + "  return _F[column]["+offset+"+row];\n"
              +  indent + "}\n\n";

      // ex: void v(int row, const tarch::la::Vector<3,double>& values); 2D and 3D
      setters += indent +"void "+identifier+"(int row, const tarch::la::Vector<DIMENSIONS,double>& values) {\n"
              +  indent + "  assertion(row >= 0 && row<"+multiplicity+");\n";
      for (int j=0; j<2; j++) {
        setters += indent +"  _F["+j+"]["+offset+"+row]=values["+j+"];\n";
      }
      setters += indent + "  #if DIMENSIONS==2\n";
      setters += indent +"  _F[2]["+offset+"+row]=values[2];\n";
      setters += indent + "  #endif\n";
      setters += indent +"}\n";
      setters += indent + "#if DIMENSIONS==2\n";
      setters += indent +"/** Setter for 2.5D calculations. Third vector element is ignored.*/\n";
      setters += indent +"void "+identifier+"(int row, const tarch::la::Vector<3,double>& values) {\n"
          +  indent + "  assertion(row >= 0 && row<"+multiplicity+");\n";
      for (int j=0; j<2; j++) {
        setters += indent +"  _F["+j+"]["+offset+"+row]=values["+j+"];\n";
      }
      setters += indent +"}\n";
      setters += indent +"#endif\n\n";
      
      // ex: void v(const tarch::la::Matrix<3,DIMENSIONS,double>& values);  2D and 3D
      setters += indent +"void "+identifier+"(const tarch::la::Matrix<"+multiplicity+",DIMENSIONS,double>& values) {\n";
      for (int j=0; j<2; j++) {
        for (int i=0; i<multiplicity; i++) {
          setters += indent +"  "+"_F["+j+"]["+(offset+i)+"]=values("+i+","+j+");\n";
        }
      }
      setters += indent + "  #if DIMENSIONS==3\n";
      for (int i=0; i<multiplicity; i++) {
        setters += indent +"  "+"_F[2]["+(offset+i)+"]=values("+i+",2);\n";
      }
      setters += indent + "  #endif\n";
      setters += indent +"}\n";
      // ex: void v(const tarch::la::Matrix<3,DIMENSIONS,double>& values);  2.5
      setters += indent + "#if DIMENSIONS==2\n";
      setters += indent +"/** Setter for 2.5D calculations. Third matrix column is ignored.*/\n";
      setters += indent +"void "+identifier+"(const tarch::la::Matrix<"+multiplicity+",3,double>& values) {\n";
      for (int j=0; j<2; j++) {
        for (int i=0; i<multiplicity; i++) {
          setters += indent +"  "+"_F["+j+"]["+(offset+i)+"]=values("+i+","+j+");\n";
        }
      }
      setters += indent +"}\n";
      setters += indent +"#endif\n\n";
      
      // ex: void v(int row, double v0, double v1, double v2);
      setters += indent +"/** Setter for 3D and 2.5D calculations. Third argument is ignored for the latter.*/\n";
      setters += indent +"void "+identifier+"(int row, ";
      for (int j=0; j<3; j++) {
        setters += "double v"+j+( j<2 ? "," : ") {\n" );
      }
      setters += indent + "  assertion(row >= 0 && row<"+multiplicity+");\n";
      for (int j=0; j<2; j++) {
        setters += indent +"  _F["+j+"]["+offset+"+row]=v"+j+";\n";
      }
      setters += indent +"  #if DIMENSIONS==3\n";
      setters += indent +"  _F[2]["+offset+"+row]=v2;\n";
      setters += indent +"  #endif\n";
      setters += indent +"}\n";
      setters += indent +"#if DIMENSIONS==2\n";
      setters += indent +"/** Setter for 2D calculations.*/\n";
      setters += indent +"void "+identifier+"(int row, ";
      for (int j=0; j<2; j++) {
        setters += "double v"+j+( j<1 ? "," : ") {\n" );
      }
      setters += indent + "  assertion(row >= 0 && row<"+multiplicity+");\n";
      for (int j=0; j<2; j++) {
        setters += indent +"  "+"_F["+j+"]["+offset+"+row]=v"+j+";\n";
      }
      setters += indent +"}\n";
      setters += indent +"#endif\n\n";

      // ex: void v(double v00, double v01, double v02, double v10, double v11, double v12, double v20, double v21, double v22);  2.5D and 3D
      int argIndentLength = ( indent +"void "+identifier+"(" ).length();
      String argIndent = new String(new char[argIndentLength]).replace("\0", " ");
      setters += indent +"/** Setter for 3D and 2.5D calculations. Third column values are ignored for the latter.*/\n";
      setters += indent +"void "+identifier+"(";
      for (int i=0; i<multiplicity; i++) {
        for (int j=0; j<3; j++) {
          setters += "double v"+i+j+( j<2 ? ", " : "" );
        }
        setters += ( i<multiplicity-1 ? ",\n"+argIndent : ") {\n" );
      }
      for (int j=0; j<2; j++) {
        for (int i=0; i<multiplicity; i++) {
          setters += indent +"  "+"_F["+j+"]["+(offset+i)+"]=v"+i+j+";\n";
        }
      }
      setters += indent +"  #if DIMENSIONS==3\n";
      for (int i=0; i<multiplicity; i++) {
        setters += indent +"  "+"_F[2]["+(offset+i)+"]=v"+i+"2;\n";
      }
      setters += indent +"  #endif\n";
      setters += indent +"}\n";
      // ex: void v(double v00, double v01, double v02, double v10, double v11, double v12, double v20, double v21, double v22);  2D
      setters += indent +"#if DIMENSIONS==2\n";
      setters += indent +"void "+identifier+"(";
      for (int i=0; i<multiplicity; i++) {
        for (int j=0; j<2; j++) {
          setters += "double v"+i+j+( j<1 ? ", " : "" );
        }
        setters += ( i<multiplicity-1 ? ",\n"+argIndent : ") {\n" );
      }
      for (int j=0; j<2; j++) {
        for (int i=0; i<multiplicity; i++) {
          setters += indent +"  "+"_F["+j+"]["+(offset+i)+"]=v"+i+j+";\n";
        }
      }
      setters += indent +"}\n";
      setters += indent +"#endif\n\n";
    }
    return setters;
  }
  
  private String createFluxesSetters() {
    String setters = "";
    
    int offset = 0;
    for (String identifier : _variablesMap.keySet()) {
      int multiplicity = _variablesMap.get(identifier);
      setters = appendFluxesSetters(setters, identifier, multiplicity, offset);
      offset += multiplicity;
    }
    
    return setters;
  }

  public void writeHeader(BufferedWriter writer, String solverName, String projectName)
      throws java.io.IOException {
    String content = IOUtils.convertRessourceContentToString(
        "eu/exahype/variables/templates/VariablesHeader.template");
    
    content = content.replaceAll("\\{\\{Project\\}\\}", projectName);
    content = content.replaceAll("\\{\\{Solver\\}\\}",  solverName);
    
    content = content.replaceAll("\\{\\{NumberOfVariables\\}\\}",  String.valueOf(_numberOfVariables));
    content = content.replaceAll("\\{\\{NumberOfParameters\\}\\}", String.valueOf(_numberOfParameters));
    
    String variablesGetters = createVariablesGetters();
    String variablesSetters = createVariablesSetters();
    content = content.replaceAll("\\{\\{VariablesGetters\\}\\}", variablesGetters);
    content = content.replaceAll("\\{\\{VariablesSetters\\}\\}", variablesSetters);
    
    String fluxesGetters = createFluxesGetters();
    String fluxesSetters = createFluxesSetters();
    content = content.replaceAll("\\{\\{FluxesGetters\\}\\}", fluxesGetters);
    content = content.replaceAll("\\{\\{FluxesSetters\\}\\}", fluxesSetters);
    
    String primitivesGetters = createPrimitivesGetters().replaceAll("_Q","_V");
    String primitivesSetters = createPrimitivesSetters().replaceAll("_Q","_V");
    content = content.replaceAll("\\{\\{PrimitivesGetters\\}\\}", primitivesGetters);
    content = content.replaceAll("\\{\\{PrimitivesSetters\\}\\}", primitivesSetters);

    writer.write(content);
  }
}
