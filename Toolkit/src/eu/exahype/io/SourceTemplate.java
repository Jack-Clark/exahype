package eu.exahype.io;

import eu.exahype.io.IOUtils;

/**
 * The SourceTemplate class supersedes IOUtils and hides a template engine.
 *
 * Again as with JTwig, this compiles, but due to missing dependencies I cannot
 * get it running. This is the shit with Maven.
 *
 * Created the 400KB Jar with:
 *
 * git clone https://github.com/jknack/handlebars.java.git
 * cd handlebars/
 * mvn -DskiptTests clean install
 *
 * But Maven does not include the dependencies into the JAR.
 *
 * So to keep everything sane, I disable this plugin for the time being.
 *
 **/
abstract public class SourceTemplate {
  abstract public void put(String key, Object value);
  abstract public String toString();
  
  public static SourceTemplate getDefaultImplementation(String inline_template) {
    // here we choose about the default implementation
    return new TextReplaceTemplate(inline_template);
    //return new MoustacheTemplate(inline_template);
    //return new HandlebarsTemplate(inline_template);
  }

  public static SourceTemplate fromRessourceContent(String relativePath) {
    String template = IOUtils.convertRessourceContentToString(relativePath);
    return getDefaultImplementation(template);
  }
}
