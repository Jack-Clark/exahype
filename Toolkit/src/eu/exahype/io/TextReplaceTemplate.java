package eu.exahype.io;

import eu.exahype.io.IOUtils;
import eu.exahype.io.SourceTemplate;

/**
 * The SourceTemplate class supersedes IOUtils and hides a template engine.
 *
 * This class is introduced for the migration to a fully featured templating engine.
 * At first, it will just supersede what IOUtils did before.
 *
 * Todo: Rename this class to something like TextTemplate and then make an
 *   abstract interface.
 *
 *
 **/
public class TextReplaceTemplate extends SourceTemplate  {
  String tpl;

  public TextReplaceTemplate(String template) {
    // for the beginning, just masquerade what a String would do
    this.tpl = template;
  }

  public void put(String key, Object value) {
    this.tpl = this.tpl.replaceAll("\\{\\{"+key+"\\}\\}", (String)value);
  }

  public String toString() {
    return tpl;
  }
}
