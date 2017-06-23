package eu.exahype.io.tests;

import java.io.BufferedWriter;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.util.LinkedHashMap;
import java.util.Map;

import eu.exahype.io.TextReplaceTemplate;
import eu.exahype.io.TextReplaceLanguage.TemplateGrammarException;


/**
 * Tests the selfwritten replacement system.
 *
 **/
public class TestTextReplacementLanguage {
  
  public static void testTemplateLanguage() {
    String nl = "\n";
    String template = "Inline test template:"
    +nl+ "Variable replacement: foo={{foo}}"
    +nl+ "Variable replacement: bar={{bar}}"
    +nl+ "Variable replacement: foobar={{foo}}{{bar}}"
    +nl+ "If statements: {%if testcond %}"
    +nl+ "  the  if block with foo={{foo}}."
    +nl+ "{% endif %} End of the ifblock."
    ;

    TextReplaceTemplate t = new TextReplaceTemplate(template);

    try {
      System.out.println(t.toString());
    } catch (TemplateGrammarException e) {
      e.printStackTrace();
    }
  }

  public static void main(String[] args) {
    testTemplateLanguage();
  }
}
