package eu.exahype.io;

import eu.exahype.io.IOUtils;
import eu.exahype.io.SourceTemplate;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

import java.util.regex.Pattern;
import java.util.regex.Matcher;

import eu.exahype.io.TextReplaceLanguage.Rewriter;
import eu.exahype.io.TextReplaceLanguage.TemplateGrammarException;


/**
 * A self-written naive and simple templating library with no dependencies.
 *
 * We have the abstract SourceTemplate interface in order to be able to include
 * much more sophisticated template languages such as JTwig or Moustache.
 * Here, we write our own template language because... we want to keep the
 * dependencies small.
 *
 *
 **/
public class TextReplaceTemplate extends SourceTemplate  {
  String tpl;
  Map<String,Object> values;

  public TextReplaceTemplate(String template) {
    // for the beginning, just masquerade what a String would do
    this.tpl = template;
  }

  public void put(String key, Object value) {
    this.tpl = this.tpl.replaceAll("\\{\\{"+key+"\\}\\}", (String)value);
  }

  public String evaluateConditionals(String unevaluatedTpl) throws TemplateGrammarException {
    // Regex parts
    String ows = "\\s*"; // optional whitespace
    String mws = "\\s+"; // mandatory whitespace
    String Ropen = "\\{%";
    String Rclose = "%\\}";
    String Rvarname = "([a-zA-Z0-9-_]+)";
    String Rif = Ropen +ows+ "if" +mws+ Rvarname +ows+ Rclose;
    String Rblock = "(.+)"; // TODO: Look behind/etc. for making sure no other {% block is there.
    String Rend = Ropen +ows+ "endif" +ows+ Rclose;

    String replacedTpl = new Rewriter(Rif + Rblock + Rend) {
      public String replacement() {
	String ifCheckVariable = group(1);
	String ifBlock = group(2);
	
        if(!values.containsKey(ifCheckVariable))
	  throw new TemplateGrammarException("Found if statement testing variable '"+ifCheckVariable+"' which is however not present in this template: "+tpl);
        boolean ifValue = (Boolean)values.get(ifCheckVariable);

        // evaluate the if:
        if(ifValue) {
          return ifBlock;
        } else {
          return ""; // todo: Also capture {% else %} and so on
        }
      }
    }.rewrite(unevaluatedTpl);

    // Todo: Make a loop around the replacement, thus replacing nested if
    // statements recursively from the innerst to the outerst.

    return replacedTpl;
  }

  public String toString() {
    // check if there are any {{ brackets }} left over
    Matcher m = Pattern.compile("\\{\\{[a-zA-z-_0-9]+\\}\\}").matcher(tpl);
    if(m.find()) {
        throw new RuntimeException("There is a leftover variable '"+m.group()+"' which has not yet been replaced in template: "+tpl);
    }
  
    // Here we could insert the evaluateConditionals(...) call:
    // tpl = evaluateConditionals(tpl);
    return tpl;
  }
}
