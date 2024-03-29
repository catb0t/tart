/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/Parse/Parser.h"
#include "tart/Parse/OperatorStack.h"
#include "tart/Common/Diagnostics.h"
#include "tart/AST/Stmt.h"
#include "tart/Type/PrimitiveType.h"
#include "tart/Type/NativeType.h"
#include "tart/Defn/TypeDefn.h"
#include "tart/Defn/Module.h"
#include <errno.h>

namespace tart {

namespace {
  // Convenience function to get the suffix of a string
  inline char lastChar(const std::string & str) {
    std::string::const_iterator begin = str.begin();
    std::string::const_iterator end = str.end();
    if (end > begin) {
      return *(end - 1);
    }
    return 0;
  }

  // Operator precedence levels.
  enum Precedence {
    Prec_Lowest = 0,
    Prec_LogicalOr = 5,
    Prec_LogicalAnd = 6,
    Prec_Contains = 7,
    Prec_IsType = 8,

    Prec_Relational = 10,

    Prec_BitOr = 20,
    Prec_BitXor = 21,
    Prec_BitAnd = 22,

    Prec_Shift = 25,
    Prec_AddSub = 30,
    Prec_MulDiv = 32,

    Prec_Exponent = 40,
    Prec_Range = 50,

    //Prec_Attr = 60,

    Prec_Highest
  };

  ASTCall * callOperator(ASTNode * opFunc, const SourceLocation loc) {
    return new ASTCall(loc, opFunc, ASTNodeList());
  }

#if 0
  ASTCall * callOperatorMethod(char * opname, const SourceLocation loc, ASTNode * base,
      ASTNode * arg) {

    ASTMemberRef * method = new ASTMemberRef(loc, base, opname);
    ASTCall * result = new ASTCall(loc, method, ASTNodeList());
    result->append(arg);
    return result;
  }
#endif
}

Parser::Parser(ProgramSource * src, Module * m)
    : module(m)
    , lexer(src)
    , function(NULL) {
  token = lexer.next();
}

// -------------------------------------------------------------------
// Token matching functions
// -------------------------------------------------------------------

void Parser::next() {
  tokenLoc = lexer.tokenLocation();
  token = lexer.next();
}

inline bool Parser::match(TokenType tok) {
  if (token == tok) {
    next();
    return true;
  }
  return false;
}

StringRef Parser::matchIdent() {
  if (token == Token_Ident) {
    // Save the token value as a string
    StringRef value = module->internString(lexer.tokenValue());

    // Get the next token
    next();
    return value;
  } else if (token == Token_Get) {
    // 'get' and 'set' are allowed as identifiers except in accessor lists.
    next();
    return module->internString("get");
  } else if (token == Token_Set) {
    next();
    return module->internString("set");
  }
  return StringRef();
}

void Parser::skipToNextOpenDelim() {
  for (;;) {
    if (token == Token_End || token == Token_LParen || token == Token_LBrace ||
        token == Token_LBracket) {
      return;
    }

    next();
  }
}

void Parser::skipToRParen() {
  for (;;) {
    if (token == Token_RParen) {
      next();
      return;
    } else if (token == Token_LParen) {
      next();
      skipToRParen();
      return;
    } else if (token == Token_End || token == Token_LBrace ||
        token == Token_LBracket) {
      return;
    }

    next();
  }
}

// -------------------------------------------------------------------
// High-level entry points
// -------------------------------------------------------------------

bool Parser::parse() {
  int errorCount = diag.getErrorCount();

  // Parse imports
  parseImports(module->imports());

  bool foundDecl = declarationList(module->astMembers(), DeclModifiers());
  if (token != Token_End || !foundDecl) {
    expectedDeclaration();
    return false;
  }

  // Return false if parsing this module introduced any new errors.
  return diag.getErrorCount() == errorCount;
}

bool Parser::parseImports(ASTNodeList & out) {
  // Parse imports
  int errorCount = diag.getErrorCount();
  while (match(Token_Import)) {
    importStmt(out);
  }
  return diag.getErrorCount() == errorCount;
}

// -------------------------------------------------------------------
// Declarations
// -------------------------------------------------------------------

bool Parser::declarationList(ASTDeclList & dlist, DeclModifiers mods) {
  bool foundDecl = false;
  while (declaration(dlist, mods)) {
    foundDecl = true;
  }
  return foundDecl;
}

ASTDecl * Parser::declaration() {
  // This method is used for unit tests
  ASTDeclList declList;
  if (declaration(declList, DeclModifiers())) {
    if (declList.size() == 1) {
      return declList[0];
    }
  }

  return NULL;
}

bool Parser::declaration(ASTDeclList & dlist, DeclModifiers mods) {

  // TODO: Declaration comments
  /*      comment = self.doc_comment
          self.doc_comment = ""
  */

  // Static 'if'
  if (match(Token_If)) {
    ASTNode * testExpr = expression();
    if (testExpr == NULL) {
      expectedExpression();
      return true;
    }

    DeclModifiers declMods(mods);
    if (mods.condition == NULL) {
      declMods.condition = testExpr;
    } else {
      ASTOper * combined = new ASTOper(ASTNode::LogicalAnd, testExpr->location());
      combined->append(mods.condition);
      combined->append(testExpr);
      declMods.condition = combined;
    }

    if (match(Token_LBrace)) {
      while (!match(Token_RBrace)) {
        if (!declaration(dlist, declMods)) {
          expectedDeclaration();
        }
      }
    } else if (match(Token_Colon)) {
      if (!declaration(dlist, declMods)) {
        expectedDeclaration();
      }
    }

    if (match(Token_Else)) {
      ASTNode * elseExpr = new ASTOper(ASTNode::LogicalNot, testExpr);
      if (mods.condition == NULL) {
        declMods.condition = elseExpr;
      } else {
        ASTOper * combined = new ASTOper(ASTNode::LogicalAnd, testExpr->location());
        combined->append(mods.condition);
        combined->append(elseExpr);
        declMods.condition = combined;
      }

      if (match(Token_LBrace)) {
        while (!match(Token_RBrace)) {
          if (!declaration(dlist, declMods)) {
            expectedDeclaration();
          }
        }
      } else {
        if (!declaration(dlist, declMods)) {
          expectedDeclaration();
        }
      }
    }

    return true;
  }

  // Parse attributes
  ASTNodeList attributes;
  if (!attributeList(attributes)) {
    return false;
  }

  bool accessType = accessTypeModifiers(mods);
  bool modifier = false;
  for (;;) {
    if (match(Token_Static)) {
      modifier = true;
      mods.flags |= Static;
    } else if (match(Token_Final)) {
      mods.flags |= Final;
      modifier = true;
    } else if (match(Token_Readonly)) {
      if (mods.flags & (Mutable|Immutable)) {
        diag.error(tokenLoc) << "Conflicting type modifiers";
      }
      mods.flags |= ReadOnly;
      modifier = true;
    } else if (match(Token_Mutable)) {
      if (mods.flags & (ReadOnly|Immutable)) {
        diag.error(tokenLoc) << "Conflicting type modifiers";
      }
      mods.flags |= Mutable;
      modifier = true;
    } else if (match(Token_Immutable)) {
      if (mods.flags & (ReadOnly|Mutable)) {
        diag.error(tokenLoc) << "Conflicting type modifiers";
      }
      mods.flags |= Immutable;
      modifier = true;
    } else if (match(Token_Adopted)) {
      diag.error(tokenLoc) << "'adopted' is cannot be used as a declaration modifier";
      modifier = true;
    } else if (match(Token_Abstract)) {
      mods.flags |= Abstract;
      modifier = true;
    } else {
      break;
    }
  }

  // Block beginning with 'private', etc.
  if ((accessType || modifier) && match(Token_LBrace)) {
    // TODO: No attributes...
    declarationList(dlist, mods);
    if (!match(Token_RBrace)) {
      expectedDeclaration();
    }
    return true;
  }

  // Grab the doc comment if there is one.
  DocComment docComment;
  lexer.takeDocComment(docComment);

  ASTDecl * decl = declarator(mods);
  if (decl == NULL) {
    if (modifier) {
      diag.error(lexer.tokenLocation()) << "Expected declaration after modifier";
    }
    return false;
  } else if (decl->isInvalid()) {
    return true;
  }

  decl->docComment().take(docComment);
  lexer.takeDocComment(decl->docComment(), Lexer::BACKWARD);

  decl->attributes().append(attributes.begin(), attributes.end());
  if (ASTTemplate * templ = dyn_cast<ASTTemplate>(decl)) {
    templ->body()->attributes().append(attributes.begin(), attributes.end());
    templ->body()->docComment().take(templ->docComment());
  }

  dlist.push_back(decl);
  return true;
}

bool Parser::importStmt(ASTNodeList & out) {
  SourceLocation loc = lexer.tokenLocation();

  bool unpack = false;
  if (match(Token_Namespace)) {
    unpack = true;
  }

  // Parse imports
  StringRef importName = matchIdent();
  if (importName.empty()) {
    expectedImportPath();
    return false;
  }

  ASTNode * path = new ASTIdent(tokenLoc, importName);
  while (match(Token_Dot)) {
    importName = matchIdent();
    if (importName.empty()) {
      expectedImportPath();
      return false;
    }

    path = new ASTMemberRef(tokenLoc, path, importName);
  }

  StringRef asName = importName;
  if (match(Token_As)) {
    if (unpack) {
      diag.error(loc) << "Import statement cannot have both 'from' and 'as'";
    }

    asName = matchIdent();
    if (asName.empty()) {
      expectedIdentifier();
    }
  }

  if (!match(Token_Semi)) {
    expectedSemicolon();
  }

  out.push_back(new ASTImport(loc, path, asName, unpack));
  return true;
}

ASTDecl * Parser::declarator(const DeclModifiers & mods) {
  TokenType tok = token;
  if (match(Token_Var) || match(Token_Let)) {
    return declareVariable(mods, tok);
  } else if (match(Token_Def) || match(Token_Undef) || match(Token_Override)) {
    return declareDef(mods, tok);
  } else if (match(Token_Macro)) {
    return declareMacro(mods, tok);
  } else if (match(Token_Class) || match(Token_Struct) ||
      match(Token_Interface) || match(Token_Protocol)) {
    return declareType(mods, tok);
  } else if (match(Token_Namespace)) {
    return declareNamespace(mods, tok);
  } else if (match(Token_Enum)) {
    return declareEnum(mods);
  } else if (match(Token_Typealias)) {
    return declareTypealias(mods);
  } else {
    return NULL;
  }
}

ASTDecl * Parser::declareVariable(const DeclModifiers & mods, TokenType tok) {
  SourceLocation loc;
  ASTDeclList vars;
  ASTNode::NodeType varKind = (tok == Token_Let ? ASTNode::Let : ASTNode::Var);

  if (mods.flags & Final) {
    diag.warn(lexer.tokenLocation()) << "Values are always 'final'";
  }

  if (tok == Token_Let) {
    if (mods.flags & (ReadOnly | Immutable)) {
      diag.warn(lexer.tokenLocation()) << "'let' declarations are always immutable";
    } else if (mods.flags & Mutable) {
      diag.error(lexer.tokenLocation()) << "'let' declarations cannot be declared 'mutable'";
    }
  } else {
    if (mods.flags & (ReadOnly | Immutable)) {
      diag.error(lexer.tokenLocation()) << "Use 'let' to declare an immutable property";
    }
  }

  do {
    // One or more variable declarations.
    ASTNode * declType = NULL;
    StringRef declName = matchIdent();
    loc = tokenLoc;
    if (declName.empty()) {
      expectedIdentifier();
      return (ASTDecl *)&ASTNode::INVALID;
    }

    if (match(Token_Colon)) {
      declType = typeExpression();
    }

    vars.push_back(new ASTVarDecl(varKind, loc, declName, declType, NULL, mods));
  } while (match(Token_Comma));

  ASTNode * declValue = NULL;
  if (match(Token_Assign)) {
    declValue = expressionList();
    if (declValue == NULL) {
      expectedExpression();
      return (ASTDecl *)&ASTNode::INVALID;
    }
  }

  if (!needSemi()) {
    return (ASTDecl *)&ASTNode::INVALID;
  }

  ASTVarDecl * var;
  if (vars.size() > 1) {
    // Multiple variables declared with a single defn.
    var = new ASTVarDecl(ASTNode::VarList, loc, "vlist", NULL, declValue, mods);
    for (ASTDeclList::const_iterator it = vars.begin(); it != vars.end(); ++it) {
      ASTVarDecl * v = static_cast<ASTVarDecl *>(*it);
      if (v->type() == NULL && declValue == NULL) {
        diag.fatal(lexer.tokenLocation()) << "Can't infer type for '" << v->name() << "'";
        return (ASTDecl *)&ASTNode::INVALID;
      }

      var->addMember(v);
    }
  } else {
    // A single variable declaration.
    var = static_cast<ASTVarDecl *>(vars.front());
    if (var->type() == NULL && declValue == NULL) {
      diag.fatal(lexer.tokenLocation()) << "Can't infer type for '" << var->name() << "'";
      return (ASTDecl *)&ASTNode::INVALID;
    }

    var->setValue(declValue);
  }

  return var;
}

ASTDecl * Parser::declareDef(const DeclModifiers & mods, TokenType tok) {
  if (match(Token_LBracket)) {
    SourceLocation loc = tokenLoc;
    ASTNode * returnType = NULL;
    ASTParamList params;

    if (mods.flags & (ReadOnly|Immutable|Mutable)) {
      diag.error(loc) << "type modifiers are not allowed on indexers " <<
          "(hint: put them on the getter / setter methods)";
    }

    if (!formalArgumentList(params, Token_RBracket)) {
      return (ASTDecl *)&ASTNode::INVALID;
    }

    if (params.empty()) {
      diag.error(lexer.tokenLocation()) << "Indexer must have at least one argument";
      return (ASTDecl *)&ASTNode::INVALID;
    }

    // See if there's a return type declared
    if (match(Token_Colon)) {
      returnType = typeExpression();
    }

    ASTPropertyDecl * indexer = new ASTPropertyDecl(ASTDecl::Idx, loc, "$index", returnType, mods);
    indexer->params().append(params.begin(), params.end());
    if (match(Token_LBrace)) {
      // Parse accessors for indexer
      if (!accessorMethodList(indexer, params, mods)) {
        return (ASTDecl *)&ASTNode::INVALID;
      }
    }

    if (tok == Token_Undef) {
      indexer->modifiers().flags |= Undef;
    } else if (tok == Token_Override) {
      indexer->modifiers().flags |= Override;
    }

    return indexer;
  }

  StringRef declName = matchIdent();
  if (declName.empty()) {
    if (token == Token_LParen) {
      declName = "$call";
    } else {
      diag.error(lexer.tokenLocation()) << "Expected method or property name";
      declName = "";
    }
  }

  SourceLocation loc = tokenLoc;
  if (match(Token_Colon)) {
    if (mods.flags & (ReadOnly|Immutable|Mutable)) {
      diag.error(loc) << "type modifiers are not allowed on properties " <<
          "(hint: put them on the getter / setter methods)";
    }

    // It's a property
    ASTNode * declType = typeExpression();
    if (declType == NULL) {
      return (ASTDecl *)&ASTNode::INVALID;
    }

    ASTParamList params;
    ASTPropertyDecl * prop = new ASTPropertyDecl(loc, declName, declType, mods);
    if (match(Token_LBrace)) {
      // Parse accessors
      if (!accessorMethodList(prop, params, mods)) {
        return (ASTDecl *)&ASTNode::INVALID;
      }
    }

    if (tok == Token_Undef) {
      prop->modifiers().flags |= Undef;
    } else if (tok == Token_Override) {
      prop->modifiers().flags |= Override;
    }

    return prop;
  } else {
    if (mods.flags & (Immutable|Mutable)) {
      diag.error(loc) << "invalid type modifier for method definition";
    }

    // Get template params.
    ASTNodeList templateParams;
    ASTNodeList requirements;
    templateParamList(templateParams);

    ASTParamList params;
    if (match(Token_LParen)) {
      // Argument list
      if (!formalArgumentList(params, Token_RParen)) {
        return (ASTDecl *)&ASTNode::INVALID;
      }

    } else {
      // Check for single argument (it's optional)
      formalArgument(params, 0);
    }

    // Function type.
    ASTNode * returnType = functionReturnType();
    ASTFunctionDecl * fd = new ASTFunctionDecl(
        ASTDecl::Function, loc, declName, params, returnType, mods);

    if (tok == Token_Undef) {
      fd->modifiers().flags |= Undef;
    } else if (tok == Token_Override) {
      fd->modifiers().flags |= Override;
    }

    if (!templateParams.empty()) {
      if (match(Token_Where)) {
        templateRequirements(requirements);
      }
    }

    Stmt * body = NULL;
    ASTFunctionDecl * saveFunction = function;
    function = fd;
    if (!match(Token_Semi)) {
      body = bodyStmt();
      if (body == NULL) {
        diag.error(loc) << "Function definition with no body";
        return (ASTDecl *)&ASTNode::INVALID;
      }
      fd->setBody(body);
    }

    function = saveFunction;

    // If there were type parameters, create a template
    if (!templateParams.empty()) {
      return new ASTTemplate(fd, templateParams, requirements);
    }

    return fd;
  }
}

ASTDecl * Parser::declareMacro(const DeclModifiers & mods, TokenType tok) {
  if (mods.flags & Final) {
    diag.error(lexer.tokenLocation()) << "Macros are always final";
  }

  StringRef declName = matchIdent();
  if (declName.empty()) {
    expectedIdentifier();
    return (ASTDecl *)&ASTNode::INVALID;
  }

  SourceLocation loc = tokenLoc;
  ASTNodeList templateParams;
  ASTNodeList requirements;
  templateParamList(templateParams);

  ASTFunctionDecl * fd = functionDeclaration(ASTNode::Macro, declName, mods);

  if (!templateParams.empty()) {
    if (match(Token_Where)) {
      templateRequirements(requirements);
    }
  }

  Stmt * body = NULL;
  ASTFunctionDecl * saveFunction = function;
  function = fd;
  body = bodyStmt();
  if (body == NULL) {
    diag.error(loc) << "Macro definition requires a body";
    return (ASTDecl *)&ASTNode::INVALID;
  }
  function = saveFunction;
  fd->setBody(body);

  // If there were type parameters, create a template
  if (!templateParams.empty()) {
    return new ASTTemplate(fd, templateParams, requirements);
  }

  return fd;
}

ASTDecl * Parser::declareType(const DeclModifiers & mods, TokenType tok) {
  StringRef declName = matchIdent();
  SourceLocation loc = tokenLoc;
  if (declName.empty()) {
    expectedIdentifier();
    skipToNextOpenDelim();
    declName = "#ERROR";
  }

  // Parse any type parameters
  ASTNodeList templateParams;
  ASTNodeList requirements;
  templateParamList(templateParams);

  ASTNodeList bases;
  if (match(Token_Colon)) {
    // Superclass list.
    for (;;) {
      ASTNode * baseType = typeExpression();
      if (baseType == NULL) {
        skipToNextOpenDelim();
      }

      bases.push_back(baseType);
      if (!match(Token_Comma)) {
        break;
      }
    }
  }

  ASTNode::NodeType kind = ASTNode::Class;
  if (tok == Token_Struct) {
    kind = ASTNode::Struct;
  } else if (tok == Token_Interface) {
    kind = ASTNode::Interface;
  } else if (tok == Token_Protocol) {
    kind = ASTNode::Protocol;
  }

  // TODO: If there were no errors

  ASTTypeDecl * typeDecl = new ASTTypeDecl(kind, loc, declName, bases, mods);

  if (!templateParams.empty()) {
    if (match(Token_Where)) {
      templateRequirements(requirements);
    }
  }

  if (!match(Token_LBrace)) {
    expected("class body");
    // Recovery: Look for a '{'
    // Recovery: Look for a '{'
    // Recovery: skip nested '{}' and look for a name...
    return typeDecl;
  }

  parseImports(typeDecl->imports());
  declarationList(typeDecl->members(), DeclModifiers());

  if (!match(Token_RBrace)) {
    expected("declaration or '}'");
    // Recovery: Look for a '}'
    // Recovery: Look for a '{'
  }

  // If there were type parameters, create a template
  if (!templateParams.empty()) {
    return new ASTTemplate(typeDecl, templateParams, requirements);
  }

  return typeDecl;
}

ASTDecl * Parser::declareNamespace(DeclModifiers mods, TokenType tok) {
  if (mods.flags & Abstract) {
    diag.error(tokenLoc) << "Namespaces cannot be declared abstract";
  }
  if (mods.flags & Final) {
    diag.error(tokenLoc) << "Namespaces cannot be declared final";
  }
  if (mods.flags & (ReadOnly|Mutable|Immutable)) {
    diag.error(tokenLoc) << "Type modifiers not permitted on namespace declaration";
  }

  StringRef declName = matchIdent();
  if (declName.empty()) {
    expectedIdentifier();
    skipToNextOpenDelim();
    declName = "#ERROR";
  }

  ASTNamespace * nsDef = new ASTNamespace(tokenLoc, declName);

  if (!match(Token_LBrace)) {
    expected("namespace body");
    // Recovery: Look for a '{'
    // Recovery: Look for a '{'
    // Recovery: skip nested '{}' and look for a name...
    return nsDef;
  }

  mods.visibility = Public;
  parseImports(nsDef->imports());
  declarationList(nsDef->members(), mods);

  if (!match(Token_RBrace)) {
    expectedDeclaration();
    // Recovery: Look for a '}'
    // Recovery: Look for a '{'
  }

  return nsDef;
}

ASTDecl * Parser::declareEnum(const DeclModifiers & mods) {
  if (mods.flags & Abstract) {
    diag.error(tokenLoc) << "Enumerations cannot be declared abstract";
  }
  if (mods.flags & Final) {
    diag.error(tokenLoc) << "Enumerations cannot be declared final";
  }
  if (mods.flags & (ReadOnly|Mutable|Immutable)) {
    diag.error(tokenLoc) << "Type modifiers not permitted on enum declaration";
  }

  StringRef declName = matchIdent();
  SourceLocation loc = tokenLoc;
  if (declName.empty()) {
    expectedIdentifier();
    skipToNextOpenDelim();
    declName = "#ERROR";
  }

  // Enum superclass.
  ASTNodeList bases;
  if (match(Token_Colon)) {
    ASTNode * enumBase = typeExpression();
    if (enumBase != NULL) {
      bases.push_back(enumBase);
    }
  }

  DeclModifiers enumMods(mods);
  enumMods.flags |= Static;
  ASTTypeDecl * enumDef = new ASTTypeDecl(ASTNode::Enum, loc, declName, bases, enumMods);

  if (!match(Token_LBrace)) {
    diag.error(lexer.tokenLocation()) << "Expecting enumeration constants";
    // Recovery: Look for a '{'
    // Recovery: Look for a '{'
    // Recovery: skip nested '{}' and look for a name...
    return enumDef;
  }

  enumMods.visibility = Public;
  for (;;) {
    // Parse attributes
    ASTNodeList attributes;
    if (!attributeList(attributes)) {
      return NULL;
    }

    // Save doc comments
    DocComment docComment;
    lexer.takeDocComment(docComment);

    // Get the enum constant name.
    StringRef ecName = matchIdent();
    if (ecName.empty()) {
      break;
    }

    loc = tokenLoc;
    ASTNode * ecValue = NULL;
    if (match(Token_Assign)) {
      ecValue = expression();
    }

    ASTDecl * ecDecl = new ASTVarDecl(ASTNode::Let, loc, ecName, NULL, ecValue, enumMods);
    ecDecl->attributes().append(attributes.begin(), attributes.end());
    enumDef->addMember(ecDecl);

    bool comma = match(Token_Comma);

    // Grab doc comments
    ecDecl->docComment().take(docComment);
    lexer.takeDocComment(ecDecl->docComment(), Lexer::BACKWARD);

    // OK to have extra comma.
    if (!comma) {
      break;
    }
  }

  if (!match(Token_RBrace)) {
    expected("'}'");
    // Recovery: Look for a '}'
    // Recovery: Look for a '{'
  }

  return enumDef;
}

ASTDecl * Parser::declareTypealias(const DeclModifiers & mods) {
  SourceLocation loc = tokenLoc;
  StringRef declName = matchIdent();
  if (declName.empty()) {
    expectedIdentifier();
    skipToNextOpenDelim();
    declName = "#ERROR";
  }

  if (mods.flags & Final) {
    diag.warn(lexer.tokenLocation()) << "Modifier 'final' not applicable to typealias.";
  }

  if (mods.flags & Abstract) {
    diag.warn(lexer.tokenLocation()) << "Modifier 'abstract' not applicable to typealias.";
  }

  if (!match(Token_Colon)) {
    expected("':'");
  }

  ASTNode * type = typeExpression();
  if (type == NULL) {
    return NULL;
  }

  if (!match(Token_Semi)) {
    expectedSemicolon();
  }

  ASTNodeList bases;
  bases.push_back(type);
  return new ASTTypeDecl(ASTNode::TypeAlias, loc, declName, bases, mods);
}

bool Parser::accessorMethodList(ASTPropertyDecl * parent,
    ASTParamList & accessorParams, DeclModifiers mods) {
  for (;;) {
    if (match(Token_RBrace)) {
      return true;
    }

    // Parse attributes
    ASTNodeList attributes;
    if (!attributeList(attributes)) {
      return false;
    }

    for (;;) {
      if (match(Token_Final)) {
        mods.flags |= Final;
      } else if (match(Token_Abstract)) {
        mods.flags |= Abstract;
        //} else if (match(Token_Extern)) {
        //    mods.flags |= Extern;
      } else {
        break;
      }
    }

    TokenType tok = token;
    if (match(Token_Get) || match(Token_Set)) {
      StringRef accessorName;
      if (tok == Token_Get) {
        accessorName = "get";
      } else {
        accessorName = "set";
      }

      SourceLocation loc = lexer.tokenLocation();

      ASTParamList params;
      if (tok == Token_Set) {
        ASTParamList setterParams;
        if (match(Token_LParen)) {
          // Argument list
          formalArgumentList(setterParams, Token_RParen);
        } else {
          // Check for single argument (it's optional)
          formalArgument(setterParams, 0);
        }

        if (setterParams.size() > 1) {
          diag.error(loc) << "Setter can have 0 or 1 parameters only.";
        }

        params.append(setterParams.begin(), setterParams.end());
      }

      ASTFunctionDecl * fc = new ASTFunctionDecl(ASTNode::Function, loc,
          module->internString(accessorName), params, (ASTNode *)NULL, mods);
      fc->attributes().append(attributes.begin(), attributes.end());

#if 0
      if (funcType->returnType() != NULL) {
        diag.error(loc, "Accessor shouldn't declare a return type.");
      } else if (tok == Token_Get) {
        funcType->setReturnType(propType /*->clone()*/);
      } else {
        funcType->setReturnType(&PrimitiveType::VoidType);
      }
#endif

      Stmt * body = NULL;
      ASTFunctionDecl * saveFunction = function;
      function = fc;
      if (!match(Token_Semi)) {
        body = bodyStmt();
        if (body == NULL) {
          diag.error(loc) << "Function body or ';' expected.";
          return true;
        }
        fc->setBody(body);
      }
      function = saveFunction;
      //fc->parentScope = parent;

      if (tok == Token_Get) {
        parent->setGetter(fc);
      } else {
        parent->setSetter(fc);
      }
    } else {
      diag.error(lexer.tokenLocation()) << "'get' or 'set' expected in property definition";
      return false;
    }
  }
}

bool Parser::attributeList(ASTNodeList & attributes) {
  // Parse attributes
  while (match(Token_AtSign)) {
    StringRef ident = matchIdent();
    if (ident.empty()) {
      expectedIdentifier();
      return false;
    }

    ASTNode * attrExpr = new ASTIdent(tokenLoc, ident);
    for (;;) {
      SourceLocation loc = lexer.tokenLocation();
      if (match(Token_LParen)) {
        ASTNodeList argList;
        if (!parseArgumentList(argList))
          return NULL;
        attrExpr = new ASTCall(loc, attrExpr, argList);
      } else if (match(Token_LBracket)) {
        // Template specialization
        ASTNodeList templateArgs;
        templateArgList(templateArgs);
        attrExpr = new ASTSpecialize(attrExpr->location(), attrExpr, templateArgs);
      } else if (match(Token_Dot)) {
        // Member dereference
        StringRef ident = matchIdent();
        if (ident.empty()) {
          expectedIdentifier();
        }

        attrExpr = new ASTMemberRef(loc | attrExpr->location(), attrExpr, ident);
      } else {
        break;
      }
    }

    attributes.push_back(attrExpr);
  }

  return true;
}

bool Parser::accessTypeModifiers(DeclModifiers & mods) {
  if (match(Token_Public)) {
    mods.visibility = Public;
    return true;
  } else if (match(Token_Protected)) {
    mods.visibility = Protected;
    return true;
  } else if (match(Token_Private)) {
    mods.visibility = Private;
    return true;
  } else if (match(Token_Internal)) {
    mods.visibility = Internal;
    return true;
  }
  return false;
}

ASTNode * Parser::typeExpression() {
  return typeExprBinary();
}

ASTNode * Parser::typeExprBinary() {
  ASTNode * e0 = typeExprPrimary();
  if (e0 == NULL)
    return NULL;

  OperatorStack opstack(e0);
  for (;;) {
    std::string tokenVal = lexer.tokenValue();

    switch (token) {
      // Disjoint type
      case Token_LogicalOr:
        opstack.pushOperator(
          new ASTOper(ASTNode::LogicalOr, lexer.tokenLocation()),
          Prec_LogicalOr, Left);
        next();
        break;

      default:
        if (!opstack.reduceAll()) {
          return e0;
        }

        return opstack.getExpression();
    }

    ASTNode * e1 = typeExprPrimary();
    if (e1 == NULL) {
      diag.error(lexer.tokenLocation()) << "type expression expected after '"
          << tokenVal << "'";
      return NULL;
    }

    opstack.pushOperand(e1);
  }
}

ASTNode * Parser::typeExprPrimary() {
  ASTNode * result;
  SourceLocation loc = lexer.tokenLocation();
  if (match(Token_LParen)) {
    result = typeExpression();

    // Handle tuple type.
    if (match(Token_Comma)) {
      ASTOper * tuple = new ASTOper(ASTNode::Tuple, result);
      do {
        result = typeExpression();
        if (result == NULL) {
          return NULL;
        }

        tuple->append(result);
      } while (match(Token_Comma)) ;

      result = tuple;
    }

    if (!match(Token_RParen)) {
      expectedCloseParen();
      return NULL;
    }

  } else if (match(Token_Percent)) {
    // Pattern variable.
    ASTNode * declType = NULL;
    StringRef pvarName = matchIdent();
    if (pvarName.empty()) {
      expectedIdentifier();
      return NULL;
    }

    ASTTypeVariable::ConstraintType constraint = ASTTypeVariable::IS_INSTANCE;
    ASTNode * paramDefault = NULL;
    if (match(Token_Colon)) {
      declType = typeExpression();
      if (declType == NULL) {
        expected("type expression after ':'");
      } else {
        if (match(Token_Assign)) {
          paramDefault = expression();
          if (paramDefault == NULL) {
            expectedExpression();
            return NULL;
          }
        }
      }
    } else if (match(Token_IsSubclass)) {
      constraint = ASTTypeVariable::IS_SUBTYPE;
      declType = typeExpression();
      if (declType == NULL) {
        expected("type expression after '<:'");
      }
    } else if (match(Token_IsSuperclass)) {
      constraint = ASTTypeVariable::IS_SUPERTYPE;
      declType = typeExpression();
      if (declType == NULL) {
        expected("type expression after '>:'");
      }
    } else if (match(Token_LParen)) {
      constraint = ASTTypeVariable::IS_QUALIFIER;
      if (!match(Token_QMark)) {
        expected("?");
      }
      if (!match(Token_RParen)) {
        expectedCloseParen();
      }
    } else if (match(Token_LBracket)) {
      constraint = ASTTypeVariable::IS_TYPE_CTOR;
      if (!match(Token_QMark)) {
        expected("?");
      }
      if (!match(Token_RBracket)) {
        expected("]");
      }
    }

    bool isVariadic = match(Token_Ellipsis);

    result = new ASTTypeVariable(tokenLoc, pvarName, declType, constraint, isVariadic);
    if (paramDefault != NULL) {
      result = new ASTOper(ASTNode::Assign, result, paramDefault);
    }
//  } else if (match(Token_Optional)) {
//    result = typeExprPrimary();
//    if (result != NULL) {
//      result = new ASTOper(ASTNode::LogicalOr, result, &NullType::biDef);
//    }
//
//    return result;
  } else if (match(Token_Static)) {
    if (match(Token_Function)) {
      // Function type.
      result = functionDeclaration(ASTNode::AnonFn, "", DeclModifiers(Static));
    } else {
      expected("function type after 'static'");
      return NULL;
    }
  } else if (match(Token_Function)) {
    // Function type.
    result = functionDeclaration(ASTNode::AnonFn, "", DeclModifiers());
  } else if (match(Token_Readonly)) {
    result = modifiedType(ASTNode::TypeModReadOnly);
  } else if (match(Token_Mutable)) {
    result = modifiedType(ASTNode::TypeModMutable);
  } else if (match(Token_Immutable)) {
    result = modifiedType(ASTNode::TypeModImmutable);
  } else if (match(Token_Adopted)) {
    result = modifiedType(ASTNode::TypeModAdopted);
  } else if (match(Token_Volatile)) {
    result = modifiedType(ASTNode::TypeModVolatile);
  } else {
    // Identifier or declaration
    result = typeName();
    if (result == NULL) {
      expected("type expression");
      return NULL;
    }
  }

  result = typeSuffix(result);
  return result;
}

ASTNode * Parser::modifiedType(ASTNode::NodeType nt) {
  SourceLocation loc = lexer.tokenLocation();

  if (!match(Token_LParen)) {
    expected("'(' after type modifier name");
    return NULL;
  }

  ASTNode * base = typeExpression();
  if (base == NULL) {
    return NULL;
  }

  if (!match(Token_RParen)) {
    expectedCloseParen();
  }

  loc |= lexer.tokenLocation();
  return new ASTOper(nt, loc, base);
}

ASTNode * Parser::typeName() {
  ASTNode * result;
  SourceLocation loc = lexer.tokenLocation();
  if (token == Token_Ident) {
    StringRef typeName = matchIdent();
    result = new ASTIdent(loc, typeName);
  } else if (token >= Token_BoolType && token <= Token_UIntpType) {
    TokenType t = token;
    next();
    result = builtInTypeName(t);
  } else {
    return NULL;
  }

  while (match(Token_Dot)) {
    StringRef typeName = matchIdent();
    if (typeName.empty()) {
      expected("type name after '.'");
      return NULL;
    }

    result = new ASTMemberRef(loc | tokenLoc, result, typeName);
    //result = typeSuffix(result);
    if (result == NULL) {
      return NULL;
    }
  }

  return result;
}

ASTNode * Parser::typeSuffix(ASTNode * result) {
  if (result == NULL) {
    return NULL;
  }

  for (;;) {
    if (match(Token_LBracket)) {
      ASTNodeList templateArgs;
      if (!templateArgList(templateArgs)) {
        return NULL;
      }

      if (templateArgs.empty()) {
        result = new ASTOper(ASTNode::Array, result->location() | tokenLoc, result);
      } else {
        result = new ASTSpecialize(tokenLoc | result->location(), result, templateArgs);
      }
    } else if (match(Token_LParen)) {
      SourceLocation loc = lexer.tokenLocation();
      ASTNode * arg = typeExpression();
      if (arg == NULL) {
        diag.error(loc) << "Template argument expected";
        skipToRParen();
        return NULL;
      }

      if (!match(Token_RParen)) {
        expectedCloseParen();
        return NULL;
      }

      ASTOper * qop = new ASTOper(ASTNode::Qualify, result->location() | arg->location(), result);
      qop->append(arg);
      result = qop;
    } else if (match(Token_Caret)) {
      ASTNodeList templateArgs;
      templateArgs.push_back(result);
      result = new ASTSpecialize(tokenLoc | result->location(), &AddressType::biDef, templateArgs);
    } else if (match(Token_QMark)) {
      result = new ASTOper(ASTNode::LogicalOr, result, &NullType::biDef);
    } else {
      return result;
    }
  }
}

ASTNode * Parser::builtInTypeName(TokenType t) {
  switch (t) {
    case Token_BoolType:
      return &BoolType::biDef;

    case Token_CharType:
      return &CharType::biDef;

    case Token_ByteType:
      return &Int8Type::biDef;

    case Token_ShortType:
      return &Int16Type::biDef;

    case Token_IntType:
      return &Int32Type::biDef;

    case Token_LongType:
      return &Int64Type::biDef;
      //if (match(Token_DoubleType))
      //  return &LongDoubleType::biDef;

    case Token_UByteType:
      return &UInt8Type::biDef;

    case Token_UShortType:
      return &UInt16Type::biDef;

    case Token_UIntType:
      return &UInt32Type::biDef;

    case Token_ULongType:
      return &UInt64Type::biDef;

    case Token_FloatType:
      return &FloatType::biDef;

    case Token_DoubleType:
      return &DoubleType::biDef;

    case Token_VoidType:
      return &VoidType::biDef;

    case Token_IntpType:
      return &PrimitiveType::intDef;

    case Token_UIntpType:
      return &PrimitiveType::uintDef;

    default:
      DASSERT(false);
      return NULL;
  }
}

ASTFunctionDecl * Parser::functionDeclaration(ASTNode::NodeType nt, StringRef name,
    const DeclModifiers & mods) {

  SourceLocation loc = tokenLoc;

  ASTParamList params;
  if (match(Token_LParen)) {
    // Argument list
    formalArgumentList(params, Token_RParen);
  } else {
    // Check for single argument (it's optional)
    formalArgument(params, 0);
  }

  // See if there's a return type declared
  ASTNode * returnType = functionReturnType();

  // Function type.
  return new ASTFunctionDecl(nt, loc, name, params, returnType, mods);
}

ASTNode * Parser::functionReturnType() {
  if (match(Token_ReturnType)) {
    return typeExpression();
  }

  return NULL;
}

void Parser::templateParamList(ASTNodeList & templateParams) {
  if (match(Token_LBracket)) {
    if (match(Token_RBracket)) {
      diag.error(lexer.tokenLocation()) << "Empty template parameter list";
      return;
    }

    for (;;) {
      if (!templateParam(templateParams)) {
        // TODO: Skip to RBracket
        return;
      }

      if (match(Token_RBracket)) {
        break;
      } else if (!match(Token_Comma)) {
        unexpectedToken();
        break;
      }
    }
  }
}

bool Parser::templateParam(ASTNodeList & templateParams) {
  ASTNode * param = typeExpression();
  if (param) {
    if (match(Token_Assign)) {
      ASTNode * paramDefault = typeExpression();
      if (paramDefault == NULL) {
        expectedExpression();
        return false;
      }

      param = new ASTOper(ASTNode::Assign, param, paramDefault);
    }

    templateParams.push_back(param);
    return true;
  }

  return false;
}

bool Parser::templateArgList(ASTNodeList & templateArgs) {
  if (match(Token_RBracket)) {
    return true;
  }

  for (;;) {
    SourceLocation loc = lexer.tokenLocation();
    ASTNode * arg = templateArg();
    arg = typeSuffix(arg);
    if (arg == NULL) {
      diag.error(loc) << "Template argument expected";
      skipToRParen();
      return false;
    } else {
      templateArgs.push_back(arg);
    }

    if (match(Token_RBracket)) {
      return true;
    } else if (!match(Token_Comma)) {
      unexpectedToken();
      return false;
    }
  }
}

ASTNode * Parser::templateArg() {
  return expression();
}

void Parser::templateRequirements(ASTNodeList & requirements) {
  // TODO: Implement or delete.
}

bool Parser::formalArgumentList(ASTParamList & params, TokenType endDelim) {
  int paramFlags = 0;
  if (match(endDelim))
    return true;

  // Handle an initial semicolon if all params are keyword-only.
  if (match(Token_Semi)) {
    paramFlags |= Param_KeywordOnly;
  }

  if (!formalArgument(params, paramFlags)) {
    expected("formal argument");
    return false;
  }

  for (;;) {
    if (match(endDelim)) {
      return true;
    } else if (match(Token_Comma)) {
      // Fall through
    } else if (match(Token_Semi)) {
      if (paramFlags & Param_KeywordOnly) {
        diag.error(lexer.tokenLocation()) << "Only one ';' allowed in argument list";
      } else {
        paramFlags |= Param_KeywordOnly;
      }
      // Fall through
    } else {
      unexpectedToken();
      break;
    }

    if (!formalArgument(params, paramFlags)) {
      diag.error(lexer.tokenLocation()) << "Formal argument expected after ','";
      break;
    }

    // Check for duplicate argument names.
    ASTParameter * fa = params.back();
    for (ASTParamList::const_iterator it = params.begin(); it != params.end() - 1; ++it) {
      ASTParameter * pp = *it;
      if (!fa->name().empty() && fa->name() == pp->name()) {
        diag.error(lexer.tokenLocation()) << "Duplicate argument name '" << fa->name() << "'";
      }
    }
  }

  return false;
}

bool Parser::formalArgument(ASTParamList & params, int paramFlags) {
  // TODO: Check for attributes
  // TODO: Check for modifiers

  SourceLocation argLoc = lexer.tokenLocation();
  StringRef argName = matchIdent();
  ASTNode * argType = NULL;
  if (match(Token_Colon)) {
    if (match(Token_Star)) {
      paramFlags |= Param_Star;
    }
    argType = typeExpression();
  }

  // If there's no name, and no argument, then there's no param
  if (argName.empty() && argType == NULL) {
    return false;
  }

  if (match(Token_Ellipsis)) {
    paramFlags |= Param_Variadic;
  }

  ASTNode * defaultValue = NULL;
  if (match(Token_Assign)) {
    defaultValue = expression();
  }

  params.push_back(new ASTParameter(argLoc, argName, argType, defaultValue,
      paramFlags));
  return true;
}

// -------------------------------------------------------------------
// Statements
// -------------------------------------------------------------------

Stmt * Parser::statement() {
  switch (token) {
    case Token_LBrace:
      next();
      return blockStmt();

    case Token_If:
      next();
      return ifStmt();

    case Token_While:
      next();
      return whileStmt();

    case Token_Do:
      next();
      return doWhileStmt();

    case Token_For:
      next();
      return forStmt();

    case Token_Repeat:
      next();
      return repeatStmt();

    case Token_Switch:
      next();
      return switchStmt();

    case Token_Match:
      next();
      return matchStmt();

    case Token_Return:
      next();
      return returnStmt();

    case Token_Yield:
      next();
      return yieldStmt();

    case Token_Break:
      next();
      return breakStmt();

    case Token_Continue:
      next();
      return continueStmt();

    case Token_Throw:
      next();
      return throwStmt();

    case Token_Try:
      next();
      return tryStmt();

    case Token_Let:
    case Token_Var:
    case Token_Def:
    case Token_Class:
    case Token_Struct:
      return declStmt();

    //case Token_Ident:
      //if (lexer)

    default: {
      ASTNode * expr = assignmentExpression();
      if (!expr) {
        expectedStatement();
        return NULL;
      }

      SourceLocation loc = lexer.tokenLocation();
      needSemi();

      return new ExprStmt(Stmt::Expression, expr->location() | loc, expr);
    }
  }

  return NULL;
}

Stmt * Parser::blockStmt() {
  BlockStmt * block = new BlockStmt(lexer.tokenLocation());
  SourceLocation loc = lexer.tokenLocation();
  while (!match(Token_RBrace)) {
    Stmt * st = statement();
    if (st == NULL) {
      expectedStatement();
      return NULL;
    }

    block->append(st);
    loc = lexer.tokenLocation();
  }

  block->setFinalLocation(loc);
  return block;
}

Stmt * Parser::returnStmt() {
  SourceLocation loc = lexer.tokenLocation();
  ASTNode * expr = expressionList();
  Stmt * st = new ReturnStmt(loc, expr);
  //st = postCondition(st);
  needSemi();
  return st;
}

Stmt * Parser::yieldStmt() {
  ASTNode * expr = expressionList();
  if (expr == NULL) {
    expectedExpression();
  }

  Stmt * st = new YieldStmt(expr->location(), expr);
  st = postCondition(st);
  needSemi();
  return st;
}

Stmt * Parser::breakStmt() {
  Stmt * st = new Stmt(Stmt::Break, lexer.tokenLocation());
  st = postCondition(st);
  needSemi();
  return st;
}

Stmt * Parser::continueStmt() {
  Stmt * st = new Stmt(Stmt::Continue, lexer.tokenLocation());
  st = postCondition(st);
  needSemi();
  return st;
}

Stmt * Parser::throwStmt() {
  ASTNode * expr = expression();
  Stmt * st = new ThrowStmt(expr->location(), expr);
  st = postCondition(st);
  needSemi();
  return st;
}

Stmt * Parser::tryStmt() {
  Stmt * st = bodyStmt();
  if (st == NULL) {
    expectedStatement();
    return NULL;
  }

  TryStmt * tst = new TryStmt(lexer.tokenLocation(), st);
  while (match(Token_Catch)) {

    // A 'catch-all' block.
    SourceLocation loc = lexer.tokenLocation();
    if (token == Token_LBrace) {
      st = bodyStmt();
      if (st == NULL) {
        expectedStatement();
        return NULL;
      }

      tst->catchList().push_back(new CatchStmt(loc | st->location(), NULL, st));
    } else {
      bool parens = match(Token_LParen); // Optional parens
      loc = lexer.tokenLocation();

      // Parse attributes
      ASTNodeList attributes;
      if (!attributeList(attributes)) {
        return NULL;
      }

      StringRef exceptName = matchIdent();
      if (exceptName.empty()) {
        exceptName = "$anon";
      }

      ASTNode * exceptType = NULL;
      if (match(Token_Colon)) {
        exceptType = typeExpression();
      } else {
        expected("exception type");
        return NULL;
      }

      if (parens && !match(Token_RParen)) {
        expectedCloseParen();
      }

      DeclModifiers mods;
      mods.visibility = Public;
      ASTDecl * exceptDecl = new ASTVarDecl(ASTNode::Let, loc, exceptName, exceptType,
          NULL, mods);
      if (!attributes.empty()) {
        exceptDecl->attributes().swap(attributes);
      }

      st = bodyStmt();
      if (st == NULL) {
        expectedStatement();
        return NULL;
      }

      tst->catchList().push_back(new CatchStmt(loc | st->location(), exceptDecl, st));
    }
  }

  if (match(Token_Else)) {
    st = bodyStmt();
    if (st == NULL) {
      expectedStatement();
      return NULL;
    }
    tst->setElseSt(st);
  }

  if (match(Token_Finally)) {
    st = bodyStmt();
    if (st == NULL) {
      expectedStatement();
      return NULL;
    }
    tst->setFinallySt(st);
  }

  return tst;
}

Stmt * Parser::declStmt() {
  ASTDecl * decl = declarator(DeclModifiers());
  if (decl == NULL || decl->isInvalid()) {
    return NULL;
  }

  return new DeclStmt(decl->location(), decl);
}

Stmt * Parser::ifStmt() {
  SourceLocation loc = tokenLoc;
  ASTNode * testExpr = testOrDecl();
  if (testExpr == NULL)
    return NULL;

  Stmt * thenSt = bodyStmt();
  if (thenSt == NULL)
    return NULL;

  Stmt * elseSt = NULL;
  if (match(Token_Else)) {
    if (match(Token_If)) {
      elseSt = ifStmt();
    } else {
      elseSt = bodyStmt();
    }
  }

  return new IfStmt(loc, testExpr, thenSt, elseSt);
}

Stmt * Parser::whileStmt() {
  SourceLocation loc = tokenLoc;
  ASTNode * testExpr = testOrDecl();
  if (testExpr == NULL)
    return NULL;

  Stmt * bodySt = bodyStmt();
  if (bodySt == NULL)
    return NULL;

  return new WhileStmt(loc, testExpr, bodySt);
}

Stmt * Parser::doWhileStmt() {
  SourceLocation loc = tokenLoc;
  Stmt * bodySt = bodyStmt();
  if (bodySt == NULL)
    return NULL;

  if (!match(Token_While)) {
    expected("'while' at end of do-while body");
    return NULL;
  }

  ASTNode * testExpr = testOrDecl();
  if (testExpr == NULL)
    return NULL;

  needSemi();
  return new DoWhileStmt(loc, testExpr, bodySt);
}

Stmt * Parser::forStmt() {
  SourceLocation loc = tokenLoc;
  ASTNodeList initVars;

  ASTNode * initExpr = NULL;
  if (!match(Token_Semi)) {
    // If it's a non-empty initialization clause, then it must declare
    // one or more iteration variables.
    ASTVarDecl * loopVar = localDeclList(ASTNode::Var);
    if (match(Token_In)) {
      // It's a 'for-in' statement.
      ASTNode * iterable = expressionList();
      if (iterable == NULL) {
        diag.error(lexer.tokenLocation()) << "Expression expected after 'in'";
        return NULL;
      }

      Stmt * body = bodyStmt();
      return new ForEachStmt(loc, loopVar, iterable, body);
    }

    // It's a 'for' statement.
    initExpr = loopVar;
    if (loopVar && match(Token_Assign)) {
      ASTNode * rhs = expressionList();
      if (rhs == NULL) {
        diag.error(lexer.tokenLocation()) << "Expression expected after '='";
      }

      if (ASTVarDecl * vdef = dyn_cast<ASTVarDecl>(loopVar)) {
        vdef->setValue(rhs);
      } else {
        initExpr = new ASTOper(ASTNode::Assign, loopVar, rhs);
      }
    }

    if (!needSemi()) {
      return NULL;
    }
    // Fall through
  }

  // At this point, we've just eaten a semicolon, which succeeded either
  // an initialization expression or nothing.

  // It's OK for test to be NULL.
  ASTNode * test = expression();
  if (!needSemi()) {
    return NULL;
  }

  // It's OK for incr to be NULL
  ASTNode * incr = assignmentExpression();
  Stmt * body = bodyStmt();
  return new ForStmt(loc, initExpr, test, incr, body);
}

Stmt * Parser::repeatStmt() {
  SourceLocation loc = tokenLoc;
  Stmt * bodySt = bodyStmt();
  if (bodySt == NULL)
    return NULL;

  return new WhileStmt(loc, new ASTBoolLiteral(loc, true), bodySt);
}

Stmt * Parser::switchStmt() {
  SourceLocation loc = tokenLoc;
  bool parens = match(Token_LParen);

  // It's a plain expression
  ASTNode * test = expressionList();
  if (test == NULL) {
    expectedExpression();
  }

  if (parens && !match(Token_RParen)) {
    expectedCloseParen();
    return NULL;
  }

  SwitchStmt * st = new SwitchStmt(loc, test);
  if (!match(Token_LBrace)) {
    expected("'{'");
    return NULL;
  }

  while (!match(Token_RBrace)) {
    if (match(Token_Case)) {
      Stmt * caseSt = caseStmt();
      if (caseSt != NULL) {
        st->caseList().push_back(caseSt);
      }
    } else {
      expected("'case' statement");
      return NULL;
    }
  }

  return st;
}

Stmt * Parser::caseStmt() {
  SourceLocation loc = tokenLoc;
  if (match(Token_Star)) {
    return bodyStmt();
  } else {
    ASTNode * test = expression();
    if (test != NULL) {
      ASTNodeList exprs;
      exprs.push_back(test);
      while (match(Token_Case)) {
        ASTNode * test = expression();
        if (test == NULL) {
          expected("expression after 'case'");
          return NULL;
        }

        exprs.push_back(test);
      }

      Stmt * body = bodyStmt();
      if (body != NULL) {
        return new CaseStmt(loc, exprs, body);
      }
    }
  }

  return NULL;
}

Stmt * Parser::matchStmt() {
  SourceLocation loc = tokenLoc;
  bool parens = match(Token_LParen);

  // It's a plain expression
  ASTNode * test = expression();
  if (test == NULL) {
    expectedExpression();
  }

  if (parens && !match(Token_RParen)) {
    expectedCloseParen();
    return NULL;
  }

  MatchStmt * st = new MatchStmt(loc, test);

  if (match(Token_As)) {
    Stmt * asSt = asStmt();
    if (asSt == NULL) {
      return NULL;
    }

    st->caseList().push_back(asSt);
    if (match(Token_Else)) {
      Stmt * elseBody = bodyStmt();
      if (elseBody == NULL) {
        return NULL;
      }

      st->caseList().push_back(elseBody);
    }
  } else if (match(Token_LBrace)) {
    while (!match(Token_RBrace)) {
      if (match(Token_As)) {
        Stmt * asSt = asStmt();
        if (asSt != NULL) {
          st->caseList().push_back(asSt);
        }
      } else if (match(Token_Else)) {
        Stmt * elseBody = bodyStmt();
        if (elseBody == NULL) {
          return NULL;
        }

        st->caseList().push_back(elseBody);
      } else {
        expected("'as' or 'else' statement");
        return NULL;
      }
    }
  } else {
    expected("'{'");
    return NULL;
  }

  return st;
}

Stmt * Parser::asStmt() {
  StringRef declName = matchIdent();
  SourceLocation loc = tokenLoc;
  ASTNode * declType = NULL;

  if (declName.empty()) {
    expectedIdentifier();
    return NULL;
  }

  if (match(Token_Colon)) {
    declType = typeExpression();
  }

  if (declType == NULL) {
    expected("type expression");
  }

  ASTVarDecl * var = new ASTVarDecl(ASTNode::Let, loc, declName, declType, NULL, DeclModifiers());
  Stmt * body = bodyStmt();
  if (body == NULL) {
    return NULL;
  }

  return new MatchAsStmt(loc, var, body);
}

Stmt * Parser::bodyStmt() {
  if (token != Token_LBrace) {
    expected("'{'");
  }

  Stmt * st = statement();
  if (st == NULL) {
    expectedStatement();
  }

  return st;
}

Stmt * Parser::postCondition(Stmt * st) {
  if (match(Token_If)) {
    SourceLocation loc = tokenLoc;
    ASTNode * testExpr = expression();
    if (testExpr == NULL) {
      expectedExpression();
      return NULL;
    }

    return new IfStmt(loc, testExpr, st, NULL);
  }
  return st;
}

ASTNode * Parser::testOrDecl() {
  TokenType tok = token;
  if (match(Token_Let) || match(Token_Var)) {
    // It's a declaration - for example "if let x = f() { ... }"
    ASTVarDecl * decl = localDeclList(tok == Token_Let ? ASTNode::Let : ASTNode::Var);
    if (!decl->isInvalid()) {

      // We require an initializer expression in this instance
      if (!match(Token_Assign)) {
        expected("'='");
        return NULL;
      }

      ASTNode * init = expressionList();
      if (init == NULL) {
        expectedExpression();
      } else {
        if (ASTVarDecl * var = dyn_cast<ASTVarDecl>(decl)) {
          var->setValue(init);
          return var;
        } else {
          return new ASTOper(ASTNode::Assign, decl, init);
        }
      }
    }

    return decl;
  } else {
    // It's a plain expression
    ASTNode * test = expression();
    if (test == NULL) {
      expectedExpression();
    }

    return test;
  }
}

ASTVarDecl * Parser::localDeclList(ASTNode::NodeType nt) {
  DeclModifiers mods;
  mods.visibility = Public;

  ASTDeclList decls;
  do {
    StringRef declName = matchIdent();
    SourceLocation loc = tokenLoc;
    ASTNode * declType = NULL;

    if (declName.empty()) {
      expectedIdentifier();
      return static_cast<ASTVarDecl *>(&ASTNode::INVALID);
    }

    if (match(Token_Colon)) {
      declType = typeExpression();
    }

    decls.push_back(new ASTVarDecl(nt, loc, declName, declType, NULL, mods));
  } while (match(Token_Comma));

  DASSERT(!decls.empty());
  if (decls.size() == 1) {
    return static_cast<ASTVarDecl *>(decls.front());
  } else {
    ASTVarDecl * var = new ASTVarDecl(ASTNode::VarList, tokenLoc, "vlist", NULL, NULL, mods);
    for (ASTDeclList::const_iterator it = decls.begin(); it != decls.end(); ++it) {
      var->addMember(static_cast<ASTVarDecl *>(*it));
    }

    return var;
  }
}

// -------------------------------------------------------------------
// Expressions
// -------------------------------------------------------------------

ASTNode * Parser::expressionList() {
  ASTNode * e = binaryOperator();
  if (e == NULL)
    return NULL;

  if (!match(Token_Comma)) {
    return e;
  }

  ASTNodeList elist;
  elist.push_back(e);

  for (;;) {
    e = binaryOperator();
    if (e == NULL) {
      expectedExpression();
      return NULL;
    }

    elist.push_back(e);
    if (!match(Token_Comma)) {
      break;
    }
  }

  return new ASTOper(ASTNode::Tuple, elist);
}

ASTNode * Parser::expression() {
  return binaryOperator();
}

ASTNode * Parser::assignmentExpression() {
  ASTNode * expr = expressionList();
  if (!expr) {
    return NULL;
  }

  TokenType tok = token;
  switch (tok) {
    // Normal assignment
    case Token_Assign: {
      next();
      ASTNode * rhs = assignmentExpression();
      if (rhs == NULL) {
        expectedExpression();
      }

      return new ASTOper(ASTNode::Assign, expr, rhs);
    }

    // Augmented assignment
    case Token_AssignPlus:
    case Token_AssignMinus:
    case Token_AssignStar:
    case Token_AssignSlash:
    case Token_AssignPercent:
    case Token_AssignAmpersand:
    case Token_AssignBar:
    case Token_AssignCaret:
    case Token_AssignRShift:
    case Token_AssignLShift: {
      SourceLocation loc = lexer.tokenLocation();
      next();
      ASTNode * rhs = binaryOperator();
      if (rhs == NULL) {
        expectedExpression();
      }

      ASTNode::NodeType opType;
      switch (int(tok)) {
        case Token_AssignPlus:      opType = ASTNode::AssignAdd; break;
        case Token_AssignMinus:     opType = ASTNode::AssignSub; break;
        case Token_AssignStar:      opType = ASTNode::AssignMul; break;
        case Token_AssignSlash:     opType = ASTNode::AssignDiv; break;
        case Token_AssignPercent:   opType = ASTNode::AssignMod; break;
        case Token_AssignAmpersand: opType = ASTNode::AssignBitAnd; break;
        case Token_AssignBar:       opType = ASTNode::AssignBitOr; break;
        case Token_AssignCaret:     opType = ASTNode::AssignBitXor; break;
        case Token_AssignRShift:    opType = ASTNode::AssignRSh; break;
        case Token_AssignLShift:    opType = ASTNode::AssignLSh; break;
      }

      return new ASTOper(opType, expr, rhs);
    }

    // Just an expression
    default:
      return expr;
  }
}

ASTNode * Parser::binaryOperator() {
  ASTNode * e0 = unaryOperator();
  if (e0 == NULL)
    return NULL;

  OperatorStack opstack(e0);
  for (;;) {
    TokenType operatorToken = token;

    switch (token) {
      case Token_Plus:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorAdd, lexer.tokenLocation()), Prec_AddSub, Left);
        next();
        break;

      case Token_Minus:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorSub, lexer.tokenLocation()), Prec_AddSub, Left);
        next();
        break;

      case Token_Star:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorMul, lexer.tokenLocation()), Prec_MulDiv, Left);
        next();
        break;

      case Token_Slash:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorDiv, lexer.tokenLocation()), Prec_MulDiv, Left);
        next();
        break;

      case Token_Percent:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorMod, lexer.tokenLocation()), Prec_MulDiv, Left);
        next();
        break;

      case Token_Ampersand:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorBitAnd, lexer.tokenLocation()), Prec_BitAnd, Left);
        next();
        break;

      case Token_Bar:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorBitOr, lexer.tokenLocation()), Prec_BitOr, Left);
        next();
        break;

      case Token_Caret:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorBitXor, lexer.tokenLocation()), Prec_BitXor, Left);
        next();
        break;

      case Token_LogicalAnd:
        opstack.pushOperator(
          new ASTOper(ASTNode::LogicalAnd, lexer.tokenLocation()),
          Prec_LogicalAnd, Left);
        next();
        break;

      case Token_LogicalOr:
        opstack.pushOperator(
          new ASTOper(ASTNode::LogicalOr, lexer.tokenLocation()),
          Prec_LogicalOr, Left);
        next();
        break;

      case Token_LShift:
        opstack.pushOperator(callOperator(
            &ASTIdent::operatorLSh, lexer.tokenLocation()), Prec_Shift, Left);
        next();
        break;

      case Token_RShift:
        opstack.pushOperator(callOperator(
            &ASTIdent::operatorRSh, lexer.tokenLocation()), Prec_Shift, Left);
        next();
        break;

      case Token_Range:
        opstack.pushOperator(
          new ASTOper(ASTNode::Range, lexer.tokenLocation()),
              Prec_Range, Left);
        next();
        break;

      case Token_Equal:
        opstack.pushOperator(callOperator(
            &ASTIdent::operatorEq, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_NotEqual:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorNe, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      /*case Token_RefEqual:
        opstack.pushOperator(
          new ASTOper(ASTNode::ReferenceEq, lexer.tokenLocation()),
          Prec_Relational, Left);
        next();
        break;*/

      case Token_Less:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorLT, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_Greater:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorGT, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_LessEqual:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorLE, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_GreaterEqual:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorGE, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_PossLess:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorPLT, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_PossGreater:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorPGT, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_PossLessEqual:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorPLE, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

      case Token_PossGreaterEqual:
        opstack.pushOperator(callOperator(
              &ASTIdent::operatorPGE, lexer.tokenLocation()), Prec_Relational, Left);
        next();
        break;

#if 0
      case Token_As: {
        TokenType tok = token;
        SourceLocation loc = lexer.tokenLocation();
        next();

        ASTOper * op = new ASTOper(ASTNode::AsType, lexer.tokenLocation());
        opstack.pushOperator(op, Prec_IsType, Left);

        // For the 'as' operator the right-hand side is a type literal,
        // not an expression.
        loc = lexer.tokenLocation();
        ASTNode * type = typeExpression();
        if (type == NULL) {
          return NULL;
        }
        opstack.pushOperand(type);
        continue;
      }
#endif

      case Token_Is: {
        SourceLocation loc = lexer.tokenLocation();
        next();

        ASTOper * op;
        if (match(Token_LogicalNot)) {
          op = new ASTOper(ASTNode::IsNot, loc);
        } else {
          op = new ASTOper(ASTNode::Is, loc);
        }

        opstack.pushOperator(op, Prec_IsType, Left);
        break;
      }

      case Token_In:
        opstack.pushOperator(new ASTOper(ASTNode::In,
            lexer.tokenLocation()), Prec_Contains, Left);
        next();
        break;

      case Token_Isa:
        opstack.pushOperator(new ASTOper(ASTNode::IsInstanceOf,
            lexer.tokenLocation()), Prec_IsType, Left);
        next();
        break;

      case Token_LogicalNot: {
        // Negated operators
        next();
        SourceLocation loc = lexer.tokenLocation();
        if (match(Token_In)) {
          opstack.pushOperator(
            new ASTOper(ASTNode::NotIn, loc), Prec_Contains, Left);
        } else {
          diag.error(lexer.tokenLocation()) << "'in' expected after 'not'";
        }
        break;
      }

      //case Token_DoubleAmp:
      //case Token_DoubleBar:
      //    break;
      default:
        goto done;
        /*if (!opstack.reduceAll()) {
          return e0;
        }

        return opstack.getExpression();*/
    }

    ASTNode * e1 = unaryOperator();
    if (e1 == NULL) {
      // Special case for pointer declaration.

      diag.error(lexer.tokenLocation()) << "value expected after " << GetTokenName(operatorToken);
      return NULL;
    }
    opstack.pushOperand(e1);
  }

done:
  if (!opstack.reduceAll()) {
    return e0;
  }

  return opstack.getExpression();
}

ASTNode * Parser::unaryOperator() {
  switch (token) {
    case Token_LogicalNot: {
      // Negated operators
      next();
      SourceLocation loc = lexer.tokenLocation();
      ASTNode * e1 = unaryOperator();
      if (e1 == NULL)
        return NULL;
      ASTOper * result = new ASTOper(ASTNode::LogicalNot, loc);
      result->append(e1);
      return result;
    }

    case Token_Tilde: {
      // Negated operators
      next();
      SourceLocation loc = lexer.tokenLocation();
      ASTNode * e1 = unaryOperator();
      if (e1 == NULL)
        return NULL;
      ASTOper * result = new ASTOper(ASTNode::Complement, loc);
      result->append(e1);
      return result;
    }

    case Token_Minus: {
      // Negated operators
      next();
      SourceLocation loc = lexer.tokenLocation();
      ASTNode * e1 = unaryOperator();
      if (e1 == NULL)
        return NULL;
      ASTCall * result = callOperator(&ASTIdent::operatorNegate, loc);
      result->append(e1);
      return result;
    }

    case Token_Increment:
    case Token_Decrement: {
      // Preincrement and predecrement
      TokenType tok = token;
      next();
      SourceLocation loc = lexer.tokenLocation();
      ASTNode * e1 = primaryExpression();
      if (e1 == NULL)
        return NULL;
      ASTCall * incDec = callOperator(
            tok == Token_Increment ? &ASTIdent::operatorSucc : &ASTIdent::operatorPred, loc);
      incDec->append(e1);
      return new ASTOper(ASTNode::Assign, e1, incDec);
    }

    default:
      return primaryExpression();
  }
}

ASTNode * Parser::primaryExpression() {
  ASTNode * result = NULL;

  switch (token) {
    case Token_LParen:
      next();
      result = expressionList();
      // Match generator expression here...
      if (!match(Token_RParen)) {
        expectedCloseParen();
        return NULL;
      }
      break;

    case Token_LBrace:
      next();
      result = blockStmt();
      break;

    case Token_Integer:
      result = parseIntegerLiteral();
      break;

    case Token_Float:
      result = parseFloatLiteral();
      break;

    case Token_Ident: {
      StringRef ident = matchIdent();
      result = new ASTIdent(tokenLoc, ident);
      /*if (match(Token_Colon)) {

      }*/
      break;
    }

    case Token_Super:
      next();
      result = new ASTOper(ASTNode::Super, lexer.tokenLocation());
      break;

    case Token_String:
      result = parseStringLiteral();
      break;

    case Token_Char:
      result = parseCharLiteral();
      break;

    case Token_True: {
      SourceLocation loc = lexer.tokenLocation();
      next();
      return new ASTBoolLiteral(loc, true);
    }

    case Token_False: {
      SourceLocation loc = lexer.tokenLocation();
      next();
      return new ASTBoolLiteral(loc, false);
    }

    case Token_Null: {
      SourceLocation loc = lexer.tokenLocation();
      next();
      return new ASTNode(ASTNode::Null, loc);
    }

    case Token_Function: {
      next();
      ASTFunctionDecl * fn = functionDeclaration(ASTNode::AnonFn, "$call", DeclModifiers());
      if (token == Token_LBrace) {
        ASTFunctionDecl * saveFunction = function;
        function = fn;
        Stmt * body = bodyStmt();
        function = saveFunction;
        fn->setBody(body);
      }

      result = fn;
      break;
    }

    case Token_LBracket:
      next();
      result = arrayLiteral();
      break;

    case Token_Optional:
    case Token_Static:
      return typeExprPrimary();

    case Token_If:
      next();
      return ifStmt();

    case Token_Switch:
      next();
      return switchStmt();

    case Token_Match:
      next();
      return matchStmt();

    case Token_Readonly:
      next();
      return modifiedType(ASTNode::TypeModReadOnly);

    case Token_Mutable:
      next();
      return modifiedType(ASTNode::TypeModMutable);

    case Token_Immutable:
      next();
      return modifiedType(ASTNode::TypeModImmutable);

    case Token_Adopted:
      next();
      return modifiedType(ASTNode::TypeModAdopted);

    case Token_Volatile:
      next();
      return modifiedType(ASTNode::TypeModVolatile);

    default:
      if (token >= Token_BoolType && token <= Token_UIntpType) {
        result = builtInTypeName(token);
        //result = new ASTIdent(lexer.tokenLocation(),
        //    module->internString(lexer.tokenValue().c_str()));
        next();
      }

      break;
  }

  // Suffix operators
  if (result) {
    for (;;) {
      SourceLocation loc = lexer.tokenLocation();
      if (match(Token_LParen)) {
        ASTNodeList argList;
        if (!parseArgumentList(argList))
          return NULL;
        result = new ASTCall(loc | result->location(), result, argList);
      } else if (match(Token_LBracket)) {
        // Array dereference
        ASTOper * indexop = new ASTOper(ASTNode::GetElement, result->location());
        indexop->append(result);
        if (!parseArrayIndices(indexop))
          return NULL;
        result = indexop;
      } else if (match(Token_Dot)) {
        // Member dereference
        StringRef ident = matchIdent();
        if (ident.empty()) {
          expectedIdentifier();
        }

        result = new ASTMemberRef(loc | result->location(), result, ident);
      } else if (match(Token_QMark)) {
        result = new ASTOper(ASTNode::LogicalOr, result, &NullType::biDef);
      } else {
        break;
      }
    }

    if (token == Token_Increment || token == Token_Decrement) {
      // Preincrement and predecrement
      TokenType tok = token;
      next();
      SourceLocation loc = lexer.tokenLocation();
      ASTCall * incDec = callOperator(
            tok == Token_Increment ? &ASTIdent::operatorSucc : &ASTIdent::operatorPred, loc);
      incDec->append(result);
      result = new ASTOper(ASTNode::PostAssign, result, incDec);
    }
  }

  return result;
}

bool Parser::parseArgumentList(ASTNodeList & args) {

  // Check for empty argument list
  if (match(Token_RParen))
    return true;

  // Parse individual arguments
  bool ok = true;
  for (;;) {
    ASTNode * arg = expression();
    if (arg == NULL) {
      expected("expression or closing ')'");
      return NULL;
    }

    // Check for keyword argument
    if (match(Token_Assign)) {
      // Keyword argument
      ASTNode * kwarg = expression();
      if (arg->nodeType() != ASTNode::Id) {
        diag.error(arg->location()) << "invalid keyword expression";
        ok = false;
      } else {
        StringRef kwname = ((ASTIdent *)arg)->value();
        arg = new ASTKeywordArg(arg->location() | kwarg->location(), kwarg, kwname);
      }
    }

    args.push_back(arg);
    if (match(Token_RParen))
      break;

    if (!match(Token_Comma)) {
      expected("',' or ')'");
      return NULL;
    }
  }

  if (ok) {
    // Validate keyword arguments
    bool kwArg = false;
    for (ASTNodeList::const_iterator it = args.begin(); it != args.end();
        ++it) {
      const ASTNode * arg = *it;
      if (arg->nodeType() == ASTNode::Keyword) {
        kwArg = true;
      } else if (kwArg) {
        diag.error(arg->location()) << "positional arguments must come before all keyword args";
        return NULL;
      }
    }

    return true;
  }

  return false;
}

#if 0
// TODO - implement
ASTNode * Parser::anonClass(ASTNode * expr) {
  ASTCall * call = dyn_cast<ASTCall>(expr);
  ASTNodeList bases;
  bases.push_back(call->func());
  ASTTypeDecl * typeDecl = new ASTTypeDecl(ASTNode::AnonClass, expr->location(), NULL,
      bases, DeclModifiers());
  parseImports(typeDecl->imports());
  declarationList(typeDecl->members(), DeclModifiers(Storage_Instance));

  if (!match(Token_RBrace)) {
    expected("declaration or '}'");
    // Recovery: Look for a '}'
    // Recovery: Look for a '{'
  }

  return typeDecl;
}
#endif

ASTNode * Parser::arrayLiteral() {
  ASTOper * arglist = new ASTOper(ASTNode::ArrayLiteral, lexer.tokenLocation());

  // Check for empty argument list
  if (match(Token_RBracket))
    return arglist;

  // Parse individual arguments
  for (;;) {
    ASTNode * arg = expression();
    if (arg == NULL) {
      expectedCloseBracket();
      return NULL;
    }

    arglist->append(arg);
    if (match(Token_RBracket))
      break;

    if (!match(Token_Comma)) {
      expected("',' or ')'");
      return NULL;
    }
  }

  return arglist;
}

bool Parser::parseArrayIndices(ASTOper * arrayExpr) {
  if (match(Token_RBracket))
    return true;

  for (;;) {
    ASTNode * arg = expression();
    if (arg == NULL) {
      expectedCloseBracket();
      return NULL;
    }

    arrayExpr->append(arg);
    if (match(Token_RBracket))
      return true;

    if (!match(Token_Comma)) {
      expected("',' or ']'");
      return NULL;
    }
  }
  return false;
}

ASTNode * Parser::parseIntegerLiteral() {
  int numberBase = 10;

  // Copy to narrow string cause that's what LLVM uses.
  std::string tokenVal = lexer.tokenValue();
  SourceLocation loc = lexer.tokenLocation();
  next();

  // Check for hex number
  if (tokenVal.size() >= 2 && tokenVal[0] == '0' &&
      (tokenVal[1] == 'x' || tokenVal[1] == 'X')) {
    tokenVal.erase(tokenVal.begin(), tokenVal.begin() + 2);
    numberBase = 16;
  }

  // Figure out how many bits we need.
  uint32_t bits = llvm::APInt::getBitsNeeded(tokenVal, numberBase) + 1;
  if (bits <= 32) {
    bits = 32;
  } else if (bits <= 64) {
    bits = 64;
  } else if (bits <= 128) {
    bits = 128;
  } else {
    diag.error(loc) << "Integer constant > 128 bits: (" << bits << " bits)";
  }

  return new ASTIntegerLiteral(loc, llvm::APInt(bits, tokenVal, numberBase));
}

ASTNode * Parser::parseFloatLiteral() {
  // TODO: Handle long doubles.
  std::string tokenVal = lexer.tokenValue();
  SourceLocation loc = lexer.tokenLocation();
  next();

  char lastCh = lastChar(tokenVal);
  bool isSingle = false;
  if (lastCh == 'f' || lastCh == 'F') {
    isSingle = true;
    tokenVal.erase(tokenVal.end() - 1, tokenVal.end());
  }

  llvm::APFloat value(
      isSingle ? llvm::APFloat::IEEEsingle : llvm::APFloat::IEEEdouble,
      llvm::APFloat::fcZero, false);
  llvm::APFloat::opStatus status = value.convertFromString(tokenVal.c_str(),
      llvm::APFloat::rmNearestTiesToEven);

  if (status != llvm::APFloat::opOK) {
    diag.warn(lexer.tokenLocation()) << "conversion error";
  }

  if (isSingle) {
    return new ASTFloatLiteral(loc, value);
  } else {
    return new ASTDoubleLiteral(loc, value);
  }
}

ASTNode * Parser::parseStringLiteral() {
  ASTNode * result = new ASTStringLiteral(lexer.tokenLocation(), lexer.tokenValue());
  next();
  return result;
}

ASTNode * Parser::parseCharLiteral() {
  uint32_t charVal;
  if (lexer.tokenValue().size() == 1) {
    charVal = lexer.tokenValue()[0];
  } else {
    charVal = strtoul(lexer.tokenValue().c_str(), 0, 16);
  }
  ASTNode * result = new ASTCharLiteral(lexer.tokenLocation(), charVal);
  next();
  return result;
}

void Parser::unexpectedToken() {
  diag.error(lexer.tokenLocation()) << "Unexpected token " << GetTokenName(token);
}

bool Parser::needSemi() {
  if (!match(Token_Semi)) {
    if (token != Token_RBrace) {
      expectedSemicolon();
      return false;
    }
  }
  return true;
}

void Parser::expectedImportPath() {
  expected("import path");
}

void Parser::expectedDeclaration() {
  expected("declaration");
}

void Parser::expectedExpression() {
  expected("expression");
}

void Parser::expectedSemicolon() {
  expected("semicolon");
}

void Parser::expectedStatement() {
  expected("statement");
}

void Parser::expectedIdentifier() {
  expected("identifier");
}

void Parser::expectedCloseParen() {
  expected("closing ')'");
}

void Parser::expectedCloseBracket() {
  expected("closing ']'");
}

void Parser::expected(const char * what) {
  const SourceLocation & loc = lexer.tokenLocation();

  if (token == Token_Error) {
    switch (lexer.errorCode()) {
      case Lexer::ILLEGAL_CHAR:
        diag.error(loc) << "Illegal character: " << lexer.tokenValue();
        break;

      case Lexer::UNTERMINATED_COMMENT:
        diag.error(loc) << "Unterminated comment";
        break;

      case Lexer::UNTERMINATED_STRING:
        diag.error(loc) << "Unterminated string";
        break;

      case Lexer::MALFORMED_ESCAPE_SEQUENCE:
        diag.error(loc) << "Malformed string escape sequence";
        break;

      case Lexer::INVALID_UNICODE_CHAR:
        diag.error(loc) << "Invalid unicode character";
        break;

      case Lexer::EMPTY_CHAR_LITERAL:
        diag.error(loc) << "Empty character literal";
        break;

      case Lexer::MULTI_CHAR_LITERAL:
        diag.error(loc) << "Multiple character literal";
        break;

      default:
        break;
    }
  } else {
    diag.error(loc) << "Expected " << what << ", not " << GetTokenName(token);
  }
}

}
