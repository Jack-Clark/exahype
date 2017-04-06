package eu.exahype.solvers;

import java.util.Set;

/**
 * @author Dominic E. Charrier
 */
public class GenericFiniteVolumesGodunovInC extends GenericFiniteVolumesInC {
    public static final String Identifier = "generic::godunov";
    
    public GenericFiniteVolumesGodunovInC(
            int dimensions, int numberOfVariables, int numberOfParameters, Set<String> namingSchemeNames, int patchSize, boolean enableProfiler, boolean hasConstants) {
        super("godunov",dimensions,numberOfVariables,numberOfParameters,namingSchemeNames,patchSize,1/*ghostLayerWidth*/,enableProfiler,hasConstants);
    }
}

