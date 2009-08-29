/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/CFG/Module.h"
#include "tart/CFG/Constant.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/FunctionType.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/NativeType.h"
#include "tart/CFG/UnionType.h"
#include "tart/CFG/Template.h"
#include "tart/Objects/Builtins.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Common/InternedString.h"
#include "tart/Sema/ExprAnalyzer.h"
#include "tart/Sema/TypeInference.h"
#include "tart/Sema/TypeAnalyzer.h"
#include "tart/Sema/CallCandidate.h"
#include "tart/Sema/FinalizeTypesPass.h"
#include <llvm/DerivedTypes.h>

namespace tart {

/// -------------------------------------------------------------------
/// ExprAnalyzer

Expr * ExprAnalyzer::inferTypes(Expr * expr, Type * expected) {
  if (isErrorResult(expr)) {
    return NULL;
  }

  // If it's a reference to a type, then just return it even if it's non-
  // singular.
  if (expr->exprType() == Expr::ConstType) {
    return static_cast<ConstantType *>(expr);
  }

  if (expr && !expr->isSingular()) {
    expr = TypeInferencePass::run(expr, expected);
  }

  expr = FinalizeTypesPass::run(expr);
  if (!expr->isSingular()) {
    diag.fatal(expr) << "Non-singular expression: " << expr;
    return NULL;
  }

  return expr;
}

Expr * ExprAnalyzer::reduceExpr(const ASTNode * ast, Type * expected) {
  Expr * result = reduceExprImpl(ast, expected);
  if (result != NULL) {
    if (result->type() == NULL) {
      diag.fatal() << "Expression '" << result << "' has no type.";
      DFAIL("MissingType");
    }
  }

  return result;
}

Expr * ExprAnalyzer::reduceExprImpl(const ASTNode * ast, Type * expected) {
  switch (ast->nodeType()) {
    case ASTNode::Null:
      return reduceNull(ast, expected);

    case ASTNode::LitInt:
      return reduceIntegerLiteral(static_cast<const ASTIntegerLiteral *>(ast),
          expected);

    case ASTNode::LitFloat:
      return reduceFloatLiteral(static_cast<const ASTFloatLiteral *>(ast),
          expected);

    case ASTNode::LitString:
      return reduceStringLiteral(static_cast<const ASTStringLiteral *>(ast),
          expected);

    case ASTNode::LitChar:
      return reduceCharLiteral(static_cast<const ASTCharLiteral *>(ast),
          expected);

    case ASTNode::LitBool:
      return reduceBoolLiteral(static_cast<const ASTBoolLiteral *>(ast),
          expected);

    case ASTNode::BuiltIn:
      return reduceBuiltInDefn(static_cast<const ASTBuiltIn *>(ast),
          expected);

    case ASTNode::Id:
    case ASTNode::Member:
    case ASTNode::GetElement:
    case ASTNode::Specialize:
      // Specialize works here because lookupName() does explicit specialization
      // for us.
      return reduceLoadValue(ast, expected);

    case ASTNode::Call:
      return reduceCall(static_cast<const ASTCall *>(ast), expected);

    case ASTNode::Assign:
      return reduceAssign(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::AssignAdd:
    case ASTNode::AssignSub:
    case ASTNode::AssignMul:
    case ASTNode::AssignDiv:
    case ASTNode::AssignMod:
    case ASTNode::AssignBitAnd:
    case ASTNode::AssignBitOr:
    case ASTNode::AssignBitXor:
    case ASTNode::AssignRSh:
    case ASTNode::AssignLSh:
      return reduceAugmentedAssign(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::PostAssign:
      return reducePostAssign(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::Is:
    case ASTNode::IsNot:
      return reduceRefEqualityTest(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::In:
    case ASTNode::NotIn:
      return reduceContainsTest(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::IsInstanceOf:
      return reduceTypeTest(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::LogicalNot:
      return reduceLogicalNot(static_cast<const ASTOper *>(ast), expected);

    case ASTNode::AnonFn:
      return reduceAnonFn(static_cast<const ASTFunctionDecl *>(ast), expected);

    case ASTNode::PatternVar:
      return reducePatternVar(static_cast<const ASTPatternVar *>(ast));

    default:
      diag.fatal(ast) << "Unimplemented expression type: '" << nodeTypeName(ast->nodeType()) << "'";
      DFAIL("Unimplemented");
  }
}

Expr * ExprAnalyzer::reduceAttribute(const ASTNode * ast) {
  switch (ast->nodeType()) {
  case ASTNode::Id:
  case ASTNode::Member:
    return callName(ast->location(), ast, ASTNodeList(), NULL);

  case ASTNode::Call: {
    const ASTCall * call = static_cast<const ASTCall *>(ast);
    const ASTNode * callable = call->func();
    const ASTNodeList & args = call->args();
    if (callable->nodeType() == ASTNode::Id || callable->nodeType() == ASTNode::Member) {
      return callName(call->location(), callable, args, NULL);
    }

    diag.fatal(call) << "Invalid attribute expression " << call;
    return &Expr::ErrorVal;
  }

  default:
    return reduceExpr(ast, NULL);
  }
}

ConstantExpr * ExprAnalyzer::reduceConstantExpr(const ASTNode * ast, Type * expected) {
  Expr * expr = analyze(ast, expected);
  if (isErrorResult(expr)) {
    return NULL;
  }

  if (!expr->isConstant()) {
    diag.error(expr) << "Non-constant expression: " << expr << " [" <<
        exprTypeName(expr->exprType()) << "]";
    return NULL;
  }

  return cast<ConstantExpr>(expr);
}

Expr * ExprAnalyzer::reducePattern(const ASTNode * ast, TemplateSignature * ts) {
  DASSERT(tsig == NULL);
  tsig = ts;

  Expr * expr = reduceExprImpl(ast, NULL);
  if (isErrorResult(expr)) {
    return NULL;
  }

  tsig = NULL;
  return expr;
}

Expr * ExprAnalyzer::reduceNull(const ASTNode * ast, Type * expected) {
  return new ConstantNull(ast->location());
}

Expr * ExprAnalyzer::reduceIntegerLiteral(const ASTIntegerLiteral * ast, Type * expected) {
  return new ConstantInteger(
      ast->location(),
      &UnsizedIntType::instance,
      llvm::ConstantInt::get(llvm::getGlobalContext(), ast->value()));
}

Expr * ExprAnalyzer::reduceFloatLiteral(const ASTFloatLiteral * ast, Type * expected) {
  return new ConstantFloat(
      ast->location(),
      &DoubleType::instance,
      llvm::ConstantFP::get(llvm::getGlobalContext(), ast->value()));
}

Expr * ExprAnalyzer::reduceCharLiteral(const ASTCharLiteral * ast, Type * expected) {
  return new ConstantInteger(
      ast->location(),
      &CharType::instance,
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm::getGlobalContext()), ast->value(), false));
}

Expr * ExprAnalyzer::reduceStringLiteral(const ASTStringLiteral * ast, Type * expected) {
  return new ConstantString(ast->location(), ast->value());
}

Expr * ExprAnalyzer::reduceBoolLiteral(const ASTBoolLiteral * ast, Type * expected) {
  return ConstantInteger::getConstantBool(ast->location(), ast->value());
}

Expr * ExprAnalyzer::reduceBuiltInDefn(const ASTBuiltIn * ast, Type * expected) {
  if (TypeDefn * tdef = dyn_cast<TypeDefn>(ast->value())) {
    return tdef->asExpr();
  } else {
    DFAIL("Implement");
    //diag.fatal(ast) << "'" << def->name() << "' is not a type";
    //return &BadType::instance;
  }
}

Expr * ExprAnalyzer::reduceAnonFn(const ASTFunctionDecl * ast, Type * expected) {
  if (ast->body() != NULL) {
    DFAIL("Implement function literal");
  } else {
    // It's merely a function type declaration
    TypeAnalyzer ta(module, activeScope);
    FunctionType * ftype = ta.typeFromFunctionAST(ast);
    if (ftype != NULL) {
      if (ftype->returnType() == NULL) {
        ftype->setReturnType(&VoidType::instance);
      }
      return new ConstantType(ast->location(), ftype);
    }
  }

  return &Expr::ErrorVal;
}

Expr * ExprAnalyzer::reducePatternVar(const ASTPatternVar * ast) {
  if (tsig == NULL) {
    diag.error(ast) << "Pattern variable used outside of pattern.";
    return &Expr::ErrorVal;
  }

  // See if the pattern var wants a type:
  Type * type = NULL; // Builtins::typeType;
  if (ast->type() != NULL) {
    DFAIL("Implement");
  }

  PatternVar * pvar = tsig->patternVar(ast->name());
  if (pvar != NULL) {
    if (type != NULL) {
      if (pvar->valueType() == NULL) {
        pvar->setValueType(type);
      } else if (!pvar->valueType()->isEqual(type)) {
        diag.error(ast) << "Conflicting type declaration for pattern variable '" <<
            ast->name() << "'";
      }
    }

    return new ConstantType(ast->location(), pvar);
  } else {
    return new ConstantType(ast->location(),
        tsig->addPatternVar(ast->location(), ast->name(), type));
  }
}

Expr * ExprAnalyzer::reduceAssign(const ASTOper * ast, Type * expected) {
  Expr * lhs = reduceValueRef(ast->arg(0), expected, true);
  if (isErrorResult(lhs)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(lhs->type() != NULL, lhs);
  Expr * rhs = reduceExpr(ast->arg(1), lhs->type());
  if (isErrorResult(rhs)) {
    return &Expr::ErrorVal;
  }

  return reduceStoreValue(ast->location(), lhs, rhs);
}

Expr * ExprAnalyzer::reducePostAssign(const ASTOper * ast, Type * expected) {
  // PostAssign gets the value before assignment, and then modifies the value.
  Expr * lvalue = reduceValueRef(ast->arg(0), expected, true);
  if (isErrorResult(lvalue)) {
    return &Expr::ErrorVal;
  }

  // TODO: Insure that it is actually an LValue.

  Expr * newValue = reduceExpr(ast->arg(1), lvalue->type());
  return new AssignmentExpr(Expr::PostAssign, ast->location(), lvalue, newValue);
}

Expr * ExprAnalyzer::reduceAugmentedAssign(const ASTOper * ast, Type * expected) {
  const char * assignOperName;
  ASTIdent * infixOperIdent;
  switch (int(ast->nodeType())) {
    case ASTNode::AssignAdd:
      assignOperName = "assignAdd";
      infixOperIdent = &ASTIdent::operatorAdd;
      break;

    case ASTNode::AssignSub:
      assignOperName = "assignSubtract";
      infixOperIdent = &ASTIdent::operatorSub;
      break;

    case ASTNode::AssignMul:
      assignOperName = "assignMultiply";
      infixOperIdent = &ASTIdent::operatorMul;
      break;

    case ASTNode::AssignDiv:
      assignOperName = "assignDivide";
      infixOperIdent = &ASTIdent::operatorDiv;
      break;

    case ASTNode::AssignMod:
      assignOperName = "assignModulus";
      infixOperIdent = &ASTIdent::operatorMod;
      break;

    case ASTNode::AssignBitAnd:
      assignOperName = "assignBitAnd";
      infixOperIdent = &ASTIdent::operatorBitAnd;
      break;

    case ASTNode::AssignBitOr:
      assignOperName = "assignBitOr";
      infixOperIdent = &ASTIdent::operatorBitOr;
      break;

    case ASTNode::AssignBitXor:
      assignOperName = "assignBitXor";
      infixOperIdent = &ASTIdent::operatorBitXor;
      break;

    case ASTNode::AssignRSh:
      assignOperName = "assignRShift";
      infixOperIdent = &ASTIdent::operatorRSh;
      break;

    case ASTNode::AssignLSh:
      assignOperName = "assignLShift";
      infixOperIdent = &ASTIdent::operatorLSh;
      break;
  }

  // Attempt to call augmented assignment operator as member function
  ASTMemberRef augMethodRef(ast->location(), const_cast<ASTNode *>(ast->arg(0)), assignOperName);
  ASTNodeList args;
  args.push_back(const_cast<ASTNode *>(ast->arg(1)));
  Expr * callExpr = callName(ast->location(), &augMethodRef, args, NULL, true);
  if (callExpr != NULL) {
    return callExpr;
  }

  // Otherwise call the regular infix operator and assign to the same location.
  Expr * callResult = callName(ast->location(), infixOperIdent, ast->args(), expected, false);
  if (isErrorResult(callResult)) {
    return callResult;
  }

  Expr * lhs = reduceValueRef(ast->arg(0), expected, true);
  if (isErrorResult(lhs)) {
    return &Expr::ErrorVal;
  }

  return reduceStoreValue(ast->location(), lhs, callResult);
}

Expr * ExprAnalyzer::reduceLoadValue(const ASTNode * ast, Type * expected) {
  Expr * lvalue = reduceValueRef(ast, expected, false);
  if (isErrorResult(lvalue)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(lvalue->type() != NULL, lvalue);

  if (LValueExpr * lval = dyn_cast<LValueExpr>(lvalue)) {
    // If it's a property reference, convert it into a method call.
    if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
      return reduceGetPropertyValue(ast->location(), lval->base(), prop, expected);
    }
  } else if (CallExpr * call = dyn_cast<CallExpr>(lvalue)) {
    if (LValueExpr * lval = dyn_cast<LValueExpr>(call->function())) {
      if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
        return reduceGetParamPropertyValue(ast->location(), call, expected);
      }
    }
  }

  // TODO: Check for indexer...

  return lvalue;
}

Expr * ExprAnalyzer::reduceStoreValue(const SourceLocation & loc, Expr * lvalue, Expr * rvalue) {
  if (LValueExpr * lval = dyn_cast<LValueExpr>(lvalue)) {
    // If it's a property reference, convert it into a method call.
    if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
      return reduceSetPropertyValue(loc, lval->base(), prop, rvalue);
    }
  } else if (CallExpr * call = dyn_cast<CallExpr>(lvalue)) {
    if (LValueExpr * lval = dyn_cast<LValueExpr>(call->function())) {
      if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
        return reduceSetParamPropertyValue(loc, call, rvalue);
      }
    }
  }

  return new AssignmentExpr(Expr::Assign, loc, lvalue, rvalue);
}

Expr * ExprAnalyzer::reduceRefEqualityTest(const ASTOper * ast, Type * expected) {
  bool invert = (ast->nodeType() == ASTNode::IsNot);
  Expr * first = reduceExpr(ast->arg(0), NULL);
  Expr * second = reduceExpr(ast->arg(1), NULL);

  if (first != NULL && second != NULL) {
    DASSERT_OBJ(first->type() != NULL, first);
    DASSERT_OBJ(second->type() != NULL, second);

    Expr * result = new BinaryExpr(Expr::RefEq, ast->location(),
        &BoolType::instance, first, second);
    if (invert) {
      result = new UnaryExpr(Expr::Not, ast->location(), &BoolType::instance, result);
    }

    return result;
  }

  return NULL;
}

Expr * ExprAnalyzer::reduceContainsTest(const ASTOper * ast, Type * expected) {
  bool invert = (ast->nodeType() == ASTNode::NotIn);

  DFAIL("Implement");
}

Expr * ExprAnalyzer::reduceTypeTest(const ASTOper * ast, Type * expected) {
  Expr * value = reduceExpr(ast->arg(0), NULL);
  TypeAnalyzer ta(module, activeScope);
  Type * type = ta.typeFromAST(ast->arg(1));
  if (type == NULL) {
    return &Expr::ErrorVal;
  }

  // TODO: Test result could be a constant.
  DASSERT_OBJ(value->type() != NULL, value);
  DASSERT_OBJ(value->isSingular(), value);

  if (value->type()->isEqual(type)) {
    return ConstantInteger::getConstantBool(ast->location(), true);
  }

#if 0
  // TODO: Might not want to do this for primitive types.
  if (value->type()->isSubtype(type)) {
    DFAIL("Implement");
    return new ConstantInteger(ast->location(), &BoolType::instance,
        llvm::ConstantInt::getTrue());
  }
#endif

  if (CompositeType * ctd = dyn_cast<CompositeType>(type)) {
    DASSERT_OBJ(value->type() != NULL, value);
    return new InstanceOfExpr(ast->location(), value, ctd);
  }

  // See if the value is a union.
  if (UnionType * ut = dyn_cast<UnionType>(value->type())) {
    //ConversionRank rank =
    //DFAIL("Implement");
    return new InstanceOfExpr(ast->location(), value, type);
  }

  diag.debug(ast) << "Unsupported isa test to type " << type;
  DFAIL("IllegalState");
}

Expr * ExprAnalyzer::reduceLogicalNot(const ASTOper * ast, Type * expected) {
  Expr * value = reduceExpr(ast->arg(0), &BoolType::instance);

  if (isErrorResult(value)) {
    return value;
  }

  if (ConstantExpr * cval = dyn_cast<ConstantExpr>(value)) {
    if (ConstantInteger * cint = dyn_cast<ConstantInteger>(cval)) {
      return ConstantInteger::getConstantBool(ast->location(), cint->value() == 0);
    } else if (ConstantNull * cnull = dyn_cast<ConstantNull>(cval)) {
      return ConstantInteger::getConstantBool(ast->location(), true);
    }

    diag.error(ast) << "Invalid argument for logical 'not' expression";
    return &Expr::ErrorVal;
  }

  value = BoolType::instance.implicitCast(ast->location(), value);
  if (isErrorResult(value)) {
    return value;
  }

  return new UnaryExpr(
      Expr::Not, ast->location(), &BoolType::instance, value);
}

Expr * ExprAnalyzer::reduceValueRef(const ASTNode * ast, Type * expected, bool store) {
  if (ast->nodeType() == ASTNode::GetElement) {
    return reduceElementRef(static_cast<const ASTOper *>(ast), expected, store);
  } else {
    return reduceSymbolRef(ast, expected);
  }
}

Expr * ExprAnalyzer::reduceSymbolRef(const ASTNode * ast, Type * expected) {
  ExprList values;
  lookupName(values, ast);

  if (values.size() == 0) {
    diag.error(ast) << "Undefined symbol " << ast;
    diag.writeLnIndent("Scopes searched:");
    dumpScopeHierarchy();
    return &Expr::ErrorVal;
  } else if (values.size() > 1) {
    diag.error(ast) << "Multiply defined symbol " << ast;
    return &Expr::ErrorVal;
  } else {
    Expr * value = values.front();
    if (LValueExpr * lval = dyn_cast<LValueExpr>(value)) {
      return reduceLValueExpr(lval, expected);
    } else if (ConstantType * typeExpr = dyn_cast<ConstantType>(value)) {
      return typeExpr;
    } else if (ScopeNameExpr * scopeName = dyn_cast<ScopeNameExpr>(value)) {
      return scopeName;
    } else {
      DFAIL("Not an LValue");
    }
  }

#if 0
  // Don't refer to idents that aren't yet defined in this scope.
  // TODO: Move this to GFC analysis possibly.
  const char * name = result->name();
  if (!result->isAvailable()) {
    diag.fatal(ident->location(),
        "Attempt to access variable '%s' before definition", name);
    return &BadResult;
  }

  analyze(de);

  // It may have been an instance variable that we found, in which
  // case we want to convert this into a getMember expression.
  if (de->storageClass() == Storage_Instance) {
    // Look for an outer scope that has a 'self' parameter matching
    // the class type of the field.
    if (implicitSelf == NULL) {
      diag.fatal(ident->location(),
          "Attempt to access instance member '%s' from static function", name);
      return &BadResult;
    } else if (implicitSelf->getCanonicalType()->getDeclaration() !=
        result->getParentScope()) {
      diag.fatal(ident->location(),
          "Instance member '%s' can't be accessed via 'self' from here", name);
      return &BadResult;
    }
  }

#endif
}

Expr * ExprAnalyzer::reduceElementRef(const ASTOper * ast, Type * expected, bool store) {
  // TODO: We might want to support more than 1 array index.
  DASSERT_OBJ(ast->count() >= 1, ast);
  if (ast->count() == 1) {
    Expr * elementExpr = reduceExpr(ast->arg(0), NULL);
    if (ConstantType * elementType = dyn_cast_or_null<ConstantType>(elementExpr)) {
      return new ConstantType(ast->location(), getArrayTypeForElement(elementType->value()));
    }

    diag.error(ast) << "Type expression expected before []";
    return &Expr::ErrorVal;
  }

  // If it's a name, see if it's a specializable name.
  const ASTNode * base = ast->arg(0);
  Expr * arrayExpr;
  if (base->nodeType() == ASTNode::Id || base->nodeType() == ASTNode::Member) {
    ExprList values;
    lookupName(values, base);

    if (values.size() == 0) {
      diag.error(base) << "Undefined symbol " << base;
      diag.writeLnIndent("Scopes searched:");
      dumpScopeHierarchy();
      return &Expr::ErrorVal;
    } else {
      ASTNodeList args;
      args.insert(args.begin(), ast->args().begin() + 1, ast->args().end());
      Expr * specResult = resolveSpecialization(ast->location(), values, args);
      if (specResult != NULL) {
        return specResult;
      }

      if (values.size() > 1) {
        diag.error(base) << "Multiply defined symbol " << base;
        return &Expr::ErrorVal;
      }

      arrayExpr = values.front();
      if (LValueExpr * lval = dyn_cast<LValueExpr>(arrayExpr)) {
        arrayExpr = reduceLValueExpr(lval, expected);
      } else {
        diag.error(base) << "Expression " << base << " is neither an array type nor a template";
        return &Expr::ErrorVal;
      }
    }
  } else {
    // TODO: We might want to support more than 1 array index.
    DASSERT_OBJ(ast->count() == 2, ast);
    arrayExpr = reduceExpr(base, NULL);
  }

  if (isErrorResult(arrayExpr)) {
    return &Expr::ErrorVal;
  }

  // What we want is to determine whether or not the expression is a template. Or even a type.

  Type * arrayType = dealias(arrayExpr->type());
  DASSERT_OBJ(arrayType != NULL, arrayExpr);

  // Reduce all of the arguments
  ExprList args;
  for (size_t i = 1; i < ast->count(); ++i) {
    Expr * arg = reduceExpr(ast->arg(i), NULL);
    if (isErrorResult(arg)) {
      return &Expr::ErrorVal;
    }

    args.push_back(arg);
  }

  if (args.empty()) {
    diag.fatal(ast) << "Array element access with no arguments";
    return &Expr::ErrorVal;
  }

  // Do automatic pointer dereferencing
  if (NativePointerType * npt = dyn_cast<NativePointerType>(arrayType)) {
    if (!AnalyzerBase::analyzeTypeDefn(npt->typeDefn(), Task_InferType)) {
      return &Expr::ErrorVal;
    }

    arrayType = npt->typeParam(0);
    arrayExpr = new UnaryExpr(Expr::PtrDeref, arrayExpr->location(), arrayType, arrayExpr);
  }

  // Handle native arrays.
  if (NativeArrayType * naType = dyn_cast<NativeArrayType>(arrayType)) {
    if (!AnalyzerBase::analyzeTypeDefn(naType->typeDefn(), Task_InferType)) {
      return &Expr::ErrorVal;
    }

    Type * elemType = naType->typeParam(0);
    DASSERT_OBJ(elemType != NULL, naType);

    if (args.size() != 1) {
      diag.fatal(ast) << "Incorrect number of array index dimensions";
    }

    // TODO: Attempt to cast arg to an integer type of known size.
    return new BinaryExpr(Expr::ElementRef, ast->location(), elemType, arrayExpr, args[0]);
  }

  // See if the type has any indexers defined.
  DefnList indexers;
  if (arrayType->typeDefn() != NULL) {
    if (!AnalyzerBase::analyzeTypeDefn(arrayType->typeDefn(), Task_PrepMemberLookup)) {
      return &Expr::ErrorVal;
    }
  }

  if (arrayType->memberScope() == NULL ||
      !arrayType->memberScope()->lookupMember(istrings.idIndex, indexers, true)) {
    diag.fatal(ast) << "Type " << arrayType << " does not support element reference operator";
    return &Expr::ErrorVal;
  }

  DASSERT(!indexers.empty());

  // For now, we only allow a single indexer to be defined. Later we will allow
  // overloading both by argument type and return type.
  if (indexers.size() > 1) {
    diag.fatal(ast) << "Multiple indexer definitions are currently not supported";
    return &Expr::ErrorVal;
  }

  PropertyDefn * indexer = cast<PropertyDefn>(indexers.front());
  if (!analyzeDefn(indexer, Task_PrepCallOrUse)) {
    return &Expr::ErrorVal;
  }

  // CallExpr type used to hold the array reference and parameters.
  LValueExpr * callable = new LValueExpr(ast->location(), arrayExpr, indexer);
  CallExpr * call = new CallExpr(Expr::Call, ast->location(), callable);
  call->args().append(args.begin(), args.end());
  call->setType(indexer->type());
  return call;
}

Expr * ExprAnalyzer::reduceGetPropertyValue(const SourceLocation & loc, Expr * basePtr,
    PropertyDefn * prop, Type * expected) {

  if (!analyzeValueDefn(prop, Task_PrepCallOrUse)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(prop->getter()->isSingular(), prop);

  FunctionDefn * getter = prop->getter();
  if (getter == NULL) {
    diag.fatal(prop) << "No getter for property '" << prop->name() << "'";
    return &Expr::ErrorVal;
  }

  if (!analyzeValueDefn(getter, Task_PrepMemberLookup)) {
    return &Expr::ErrorVal;
  }

  //ExprList callingArgs;
  // TODO: Compile-time evaluation if possible.
  /*Expr * expr = getter->eval(in->location(), callingArgs);
  if (expr != NULL) {
    return expr;
  }*/

  FnCallExpr * getterCall = new FnCallExpr(Expr::FnCall, loc, getter, basePtr);
  getterCall->setType(prop->type());
  module->addSymbol(getter);
  analyzeLater(getter);
  return getterCall;
}

Expr * ExprAnalyzer::reduceSetPropertyValue(const SourceLocation & loc,
    Expr * basePtr, PropertyDefn * prop, Expr * value) {

  if (!analyzeValueDefn(prop, Task_PrepCallOrUse)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(prop->setter()->isSingular(), prop);

  FunctionDefn * setter = prop->setter();
  if (setter == NULL) {
    diag.fatal(prop) << "No setter for property '" << prop->name() << "'";
    return &Expr::ErrorVal;
  }

  if (!analyzeValueDefn(setter, Task_PrepOverloadSelection)) {
    return &Expr::ErrorVal;
  }

  //ExprList callingArgs;
  //callingArgs.push_back(value);

  // TODO: Compile-time evaluation if possible.
  /*Expr * expr = getter->eval(in->location(), callingArgs);
  if (expr != NULL) {
    return expr;
  }*/

  FnCallExpr * setterCall = new FnCallExpr(Expr::FnCall, loc, setter, basePtr);
  setterCall->setType(prop->type());
  setterCall->appendArg(value);
  module->addSymbol(setter);
  analyzeLater(setter);
  return setterCall;
}

Expr * ExprAnalyzer::reduceGetParamPropertyValue(const SourceLocation & loc, CallExpr * call,
    Type * expected) {

  LValueExpr * lval = cast<LValueExpr>(call->function());
  PropertyDefn * prop = cast<PropertyDefn>(lval->value());
  Expr * basePtr = lval->base();

  if (!analyzeValueDefn(prop, Task_PrepCallOrUse)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(prop->getter()->isSingular(), prop);

  FunctionDefn * getter = prop->getter();
  if (getter == NULL) {
    diag.fatal(prop) << "No getter for property '" << prop->name() << "'";
    return &Expr::ErrorVal;
  }

  if (!analyzeValueDefn(getter, Task_PrepOverloadSelection)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(getter->returnType()->isEqual(prop->type()), getter);
  DASSERT_OBJ(!getter->returnType()->isEqual(&VoidType::instance), getter);

  //ExprList callingArgs;
  // TODO: Compile-time evaluation if possible.
  /*Expr * expr = getter->eval(in->location(), callingArgs);
  if (expr != NULL) {
    return expr;
  }*/

  // TODO: Type check args against function signature.

  FnCallExpr * getterCall = new FnCallExpr(Expr::FnCall, loc, getter, basePtr);
  getterCall->setType(prop->type());
  getterCall->args().append(call->args().begin(), call->args().end());
  module->addSymbol(getter);
  analyzeLater(getter);
  return getterCall;
}

Expr * ExprAnalyzer::reduceSetParamPropertyValue(const SourceLocation & loc, CallExpr * call,
    Expr * value) {

  LValueExpr * lval = cast<LValueExpr>(call->function());
  PropertyDefn * prop = cast<PropertyDefn>(lval->value());
  Expr * basePtr = lval->base();

  if (!analyzeValueDefn(prop, Task_PrepCallOrUse)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(prop->setter()->isSingular(), prop);

  FunctionDefn * setter = prop->setter();
  if (setter == NULL) {
    diag.fatal(prop) << "No setter for property '" << prop->name() << "'";
    return &Expr::ErrorVal;
  }

  if (!analyzeValueDefn(setter, Task_PrepOverloadSelection)) {
    return &Expr::ErrorVal;
  }

//  DASSERT_OBJ(setter->returnType()->isEqual(prop->type()), setter);

  //ExprList callingArgs;
  //callingArgs.push_back(value);

  // TODO: Compile-time evaluation if possible.
  /*Expr * expr = getter->eval(in->location(), callingArgs);
  if (expr != NULL) {
    return expr;
  }*/

  // TODO: Type check args against function signature.

  FnCallExpr * setterCall = new FnCallExpr(Expr::FnCall, loc, setter, basePtr);
  setterCall->setType(prop->type());
  setterCall->args().append(call->args().begin(), call->args().end());
  setterCall->appendArg(value);
  module->addSymbol(setter);
  analyzeLater(setter);
  return setterCall;
}

Expr * ExprAnalyzer::reduceLValueExpr(LValueExpr * lvalue, Type * expected) {
  DASSERT(lvalue->value() != NULL);
  analyzeValueDefn(lvalue->value(), Task_PrepCallOrUse);
  DASSERT(lvalue->value()->type() != NULL);
  lvalue->setType(lvalue->value()->type());
  if (ParameterDefn * param = dyn_cast<ParameterDefn>(lvalue->value())) {
    lvalue->setType(param->internalType());
  }

  switch (lvalue->value()->storageClass()) {
    case Storage_Global:
    case Storage_Static:
    case Storage_Local:
      lvalue->setBase(NULL);
      return lvalue;

    case Storage_Instance: {
      Expr * base = lvalue->base();
      if (base == NULL || base->exprType() == Expr::ScopeName) {
        diag.error(lvalue) << "Attempt to reference non-static member " <<
          lvalue->value()->name() << " with no object";
        return &Expr::ErrorVal;
      }

      // TODO: Handle type names and such

      return lvalue;
    }

    case Storage_Class:
    case Storage_Param:
    case Storage_Closure:
    default:
      DFAIL("Invalid storage class");
  }
}

}
