package eu.exahype.solvers;

import java.util.Set;

/**
 * @author Dominic E. Charrier
 */
public class GenericFiniteVolumesMUSCLHancockInC extends GenericFiniteVolumesInC {
    public static final String Identifier = "generic::musclhancock";
    
    public GenericFiniteVolumesMUSCLHancockInC(
            int dimensions, int numberOfVariables, int numberOfParameters, Set<String> namingSchemeNames, int patchSize, boolean enableProfiler, boolean hasConstants) {
        super("musclhancock",dimensions,numberOfVariables,numberOfParameters,namingSchemeNames,patchSize,2/*ghostLayerWidth*/,enableProfiler,hasConstants);
    }
}

