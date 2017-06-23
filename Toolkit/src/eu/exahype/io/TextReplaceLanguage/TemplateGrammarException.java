package eu.exahype.io.TextReplaceLanguage;

/**
 * This is an unchecked exception for when template parsing fails.
 *
 **/
public class TemplateGrammarException extends RuntimeException {
  public TemplateGrammarException(String message){
     super(message);
  }
}