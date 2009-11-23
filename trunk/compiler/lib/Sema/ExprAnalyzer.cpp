/* ================================================================ *
 TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/CFG/Module.h"
#include "tart/CFG/Constant.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/FunctionType.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/PropertyDefn.h"
#include "tart/CFG/NativeType.h"
#include "tart/CFG/UnionType.h"
#include "tart/CFG/TupleType.h"
#include "tart/CFG/Template.h"
#include "tart/Objects/Builtins.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Common/InternedString.h"
#include "tart/Sema/ExprAnalyzer.h"
#include "tart/Sema/TypeInference.h"
#include "tart/Sema/TypeAnalyzer.h"
#include "tart/Sema/CallCandidate.h"
#include "tart/Sema/SpCandidate.h"
#include "tart/Sema/FinalizeTypesPass.h"
#include <llvm/DerivedTypes.h>

namespace tart {

/// -------------------------------------------------------------------
/// ExprAnalyzer

ExprAnalyzer::ExprAnalyzer(Module * mod, Scope * parent, FunctionDefn * currentFunction) :
  AnalyzerBase(mod, parent, currentFunction), currentFunction_(currentFunction) {
}

ExprAnalyzer::ExprAnalyzer(Module * mod, Scope * parent, Defn * subject) :
  AnalyzerBase(mod, parent, subject), currentFunction_(NULL) {
}

Expr * ExprAnalyzer::inferTypes(Defn * subject, Expr * expr, const Type * expected,
    bool tryCoerciveCasts) {
  if (isErrorResult(expr)) {
    return NULL;
  }

  // If it's a reference to a type, then just return it even if it's non-
  // singular.
  if (expr->exprType() == Expr::TypeLiteral) {
    return static_cast<TypeLiteralExpr *> (expr);
  }

  if (expr && !expr->isSingular()) {
    expr = TypeInferencePass::run(expr, expected);
  }

  expr = FinalizeTypesPass::run(subject, expr, tryCoerciveCasts);
  if (!expr->isSingular()) {
    diag.fatal(expr) << "Non-singular expression: " << expr;
    return NULL;
  }

  return expr;
}

Expr * ExprAnalyzer::reduceExpr(const ASTNode * ast, const Type * expected) {
  Expr * result = reduceExprImpl(ast, expected);
  if (result != NULL) {
    DASSERT(result->exprType() < Expr::TypeCount);
    if (result->type() == NULL) {
      diag.fatal() << "Expression '" << result << "' has no type.";
      DFAIL("MissingType");
    }
  }

  return result;
}

Expr * ExprAnalyzer::reduceExprImpl(const ASTNode * ast, const Type * expected) {
  switch (ast->nodeType()) {
    case ASTNode::Null:
      return reduceNull(ast);

    case ASTNode::LitInt:
      return reduceIntegerLiteral(static_cast<const ASTIntegerLiteral *> (ast));

    case ASTNode::LitFloat:
      return reduceFloatLiteral(static_cast<const ASTFloatLiteral *> (ast));

    case ASTNode::LitDouble:
      return reduceDoubleLiteral(static_cast<const ASTDoubleLiteral *> (ast));

    case ASTNode::LitString:
      return reduceStringLiteral(static_cast<const ASTStringLiteral *> (ast));

    case ASTNode::LitChar:
      return reduceCharLiteral(static_cast<const ASTCharLiteral *> (ast));

    case ASTNode::LitBool:
      return reduceBoolLiteral(static_cast<const ASTBoolLiteral *> (ast));

    case ASTNode::BuiltIn:
      return reduceBuiltInDefn(static_cast<const ASTBuiltIn *> (ast));

    case ASTNode::Id:
    case ASTNode::Member:
    case ASTNode::GetElement:
    case ASTNode::Specialize:
      // Specialize works here because lookupName() does explicit specialization
      // for us.
      return reduceLoadValue(ast);

    case ASTNode::Call:
      return reduceCall(static_cast<const ASTCall *> (ast), expected);

    case ASTNode::Assign:
      return reduceAssign(static_cast<const ASTOper *> (ast));

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
      return reduceAugmentedAssign(static_cast<const ASTOper *> (ast));

    case ASTNode::PostAssign:
      return reducePostAssign(static_cast<const ASTOper *> (ast));

    case ASTNode::Is:
    case ASTNode::IsNot:
      return reduceRefEqualityTest(static_cast<const ASTOper *> (ast));

    case ASTNode::In:
    case ASTNode::NotIn:
      return reduceContainsTest(static_cast<const ASTOper *> (ast));

    case ASTNode::IsInstanceOf:
      return reduceTypeTest(static_cast<const ASTOper *> (ast));

    case ASTNode::LogicalAnd:
    case ASTNode::LogicalOr:
      return reduceLogicalOper(static_cast<const ASTOper *> (ast));

    case ASTNode::LogicalNot:
    return reduceLogicalNot(static_cast<const ASTOper *> (ast));

    case ASTNode::ArrayLiteral:
      return reduceArrayLiteral(static_cast<const ASTOper *> (ast), expected);

    case ASTNode::AnonFn:
      return reduceAnonFn(static_cast<const ASTFunctionDecl *> (ast));

    case ASTNode::PatternVar:
      diag.error(ast) << "Pattern variable used outside of pattern.";
      return &Expr::ErrorVal;

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
      const ASTCall * call = static_cast<const ASTCall *> (ast);
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

  Expr * letConst = LValueExpr::constValue(expr);
  if (letConst != NULL) {
    expr = letConst;
  }

  if (!expr->isConstant()) {
    diag.error(expr) << "Non-constant expression: " << expr << " [" << exprTypeName(
        expr->exprType()) << "]";
    return NULL;
  }

  return cast<ConstantExpr> (expr);
}

Expr * ExprAnalyzer::reduceNull(const ASTNode * ast) {
  return new ConstantNull(ast->location());
}

Expr * ExprAnalyzer::reduceIntegerLiteral(const ASTIntegerLiteral * ast) {
  return new ConstantInteger(
      ast->location(),
      &UnsizedIntType::instance,
      llvm::ConstantInt::get(llvm::getGlobalContext(), ast->value()));
}

Expr * ExprAnalyzer::reduceFloatLiteral(const ASTFloatLiteral * ast) {
  return new ConstantFloat(
      ast->location(),
      &FloatType::instance,
      llvm::ConstantFP::get(llvm::getGlobalContext(), ast->value()));
}

Expr * ExprAnalyzer::reduceDoubleLiteral(const ASTDoubleLiteral * ast) {
  return new ConstantFloat(
      ast->location(),
      &DoubleType::instance,
      llvm::ConstantFP::get(llvm::getGlobalContext(), ast->value()));
}

Expr * ExprAnalyzer::reduceCharLiteral(const ASTCharLiteral * ast) {
  return new ConstantInteger(
      ast->location(),
      &CharType::instance,
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm::getGlobalContext()), ast->value(), false));
}

Expr * ExprAnalyzer::reduceStringLiteral(const ASTStringLiteral * ast) {
  return new ConstantString(ast->location(), ast->value());
}

Expr * ExprAnalyzer::reduceBoolLiteral(const ASTBoolLiteral * ast) {
  return ConstantInteger::getConstantBool(ast->location(), ast->value());
}

Expr * ExprAnalyzer::reduceBuiltInDefn(const ASTBuiltIn * ast) {
  if (TypeDefn * tdef = dyn_cast<TypeDefn>(ast->value())) {
    return tdef->asExpr();
  } else {
    DFAIL("Implement");
    //diag.fatal(ast) << "'" << def->name() << "' is not a type";
    //return &BadType::instance;
  }
}

Expr * ExprAnalyzer::reduceAnonFn(const ASTFunctionDecl * ast) {
  if (ast->body() != NULL) {
    DFAIL("Implement function literal");
  } else {
    // It's merely a function type declaration
    TypeAnalyzer ta(module, activeScope);
    FunctionType * ftype = ta.typeFromFunctionAST(ast);
    if (ftype != NULL) {
      if (!ftype->returnType().isDefined()) {
        ftype->setReturnType(&VoidType::instance);
      }
      return new TypeLiteralExpr(ast->location(), ftype);
    }
  }

  return &Expr::ErrorVal;
}

Expr * ExprAnalyzer::reduceAssign(const ASTOper * ast) {
  Expr * lhs = reduceValueRef(ast->arg(0), true);
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

Expr * ExprAnalyzer::reducePostAssign(const ASTOper * ast) {
  // PostAssign modifies the value, but returns the value before it was modified.
  Expr * lvalue = reduceValueRef(ast->arg(0), true);
  if (isErrorResult(lvalue)) {
    return &Expr::ErrorVal;
  }

  Expr * newValue = reduceExpr(ast->arg(1), lvalue->type());

  if (LValueExpr * lval = dyn_cast<LValueExpr>(lvalue)) {
    // If it's a property reference, convert it into a method call.
    if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
      Expr * setProp = reduceSetPropertyValue(ast->location(), lvalueBase(lval), prop, newValue);
      if (isErrorResult(setProp)) {
        return &Expr::ErrorVal;
      }

      DFAIL("Implement PostAssign of property");
    }
  } else if (CallExpr * call = dyn_cast<CallExpr>(lvalue)) {
    if (LValueExpr * lval = dyn_cast<LValueExpr>(call->function())) {
      if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
        Expr * setProp = reduceSetParamPropertyValue(ast->location(), call, newValue);
        if (isErrorResult(setProp)) {
          return &Expr::ErrorVal;
        }

        DFAIL("Implement PostAssign of indexed property");
      }
    }
  }

  return new AssignmentExpr(Expr::PostAssign, ast->location(), lvalue, newValue);
}

Expr * ExprAnalyzer::reduceAugmentedAssign(const ASTOper * ast) {
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
  Expr * callResult = callName(ast->location(), infixOperIdent, ast->args(), NULL, false);
  if (isErrorResult(callResult)) {
    return callResult;
  }

  Expr * lhs = reduceValueRef(ast->arg(0), true);
  if (isErrorResult(lhs)) {
    return &Expr::ErrorVal;
  }

  return reduceStoreValue(ast->location(), lhs, callResult);
}

Expr * ExprAnalyzer::reduceLoadValue(const ASTNode * ast) {
  Expr * lvalue = reduceValueRef(ast, false);
  if (isErrorResult(lvalue)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(lvalue->type() != NULL, lvalue);

  if (LValueExpr * lval = dyn_cast<LValueExpr>(lvalue)) {
    // If it's a property reference, convert it into a method call.
    if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
      checkAccess(ast->location(), prop);
      return reduceGetPropertyValue(ast->location(), lvalueBase(lval), prop);
    }
  } else if (CallExpr * call = dyn_cast<CallExpr>(lvalue)) {
    if (LValueExpr * lval = dyn_cast<LValueExpr>(call->function())) {
      if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lval->value())) {
        checkAccess(ast->location(), prop);
        return reduceGetParamPropertyValue(ast->location(), call);
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
      checkAccess(loc, prop);
      return reduceSetPropertyValue(loc, lvalueBase(lval), prop, rvalue);
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

Expr * ExprAnalyzer::reduceRefEqualityTest(const ASTOper * ast) {
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

Expr * ExprAnalyzer::reduceContainsTest(const ASTOper * ast) {
  bool invert = (ast->nodeType() == ASTNode::NotIn);

  // Otherwise call the regular infix operator and assign to the same location.
  Expr * callResult = callName(
      ast->location(), &ASTIdent::operatorContains, ast->args(), &BoolType::instance, false);
  if (isErrorResult(callResult)) {
    return callResult;
  }

  if (invert) {
    callResult = new UnaryExpr(Expr::Not, ast->location(), &BoolType::instance, callResult);
  }

  return callResult;
}

Expr * ExprAnalyzer::reduceTypeTest(const ASTOper * ast) {
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
  if (const UnionType * ut = dyn_cast<UnionType>(value->type())) {
    return new InstanceOfExpr(ast->location(), value, type);
  }

  diag.debug(ast) << "Unsupported isa test to type " << type;
  DFAIL("IllegalState");
}

Expr * ExprAnalyzer::reduceLogicalOper(const ASTOper * ast) {
  ASTNode::NodeType op = ast->nodeType();
  ASTIdent * opIdent = (op == ASTNode::LogicalAnd ?
      &ASTIdent::operatorLogicalAnd : &ASTIdent::operatorLogicalOr);

  ASTNodeList args;
  args.push_back(const_cast<ASTNode *>(ast->arg(0)));
  args.push_back(const_cast<ASTNode *>(ast->arg(1)));
  return callName(ast->location(), opIdent, args, NULL, true);
}

Expr * ExprAnalyzer::reduceLogicalNot(const ASTOper * ast) {
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

Expr * ExprAnalyzer::reduceArrayLiteral(const ASTOper * ast, const Type * expected) {
  DefnList fnlist;
  if (lookupTemplateMember(fnlist, Builtins::typeArray->typeDefn(), "of", ast->location())) {
    DASSERT(fnlist.size() == 1);
    CallExpr * call = new CallExpr(Expr::Call, ast->location(), NULL);
    call->setExpectedReturnType(expected);
    addOverload(call, Builtins::typeArray->typeDefn()->asExpr(),
        cast<FunctionDefn>(fnlist.front()), ast->args());
    if (!reduceArgList(ast->args(), call)) {
      return &Expr::ErrorVal;
    }

    call->setType(reduceReturnType(call));
    return call;
  }

  DFAIL("Array type not found for array literal");
}

Expr * ExprAnalyzer::reduceValueRef(const ASTNode * ast, bool store) {
  if (ast->nodeType() == ASTNode::GetElement) {
    return reduceElementRef(static_cast<const ASTOper *>(ast), store);
  } else {
    return reduceSymbolRef(ast, store);
  }
}

Expr * ExprAnalyzer::reduceSymbolRef(const ASTNode * ast, bool store) {
  ExprList values;
  lookupName(values, ast);

  if (values.size() == 0) {
    diag.error(ast) << "Undefined symbol " << ast;
    if (ast->nodeType() != ASTNode::Member) {
      diag.writeLnIndent("Scopes searched:");
      dumpScopeHierarchy();
    }

    return &Expr::ErrorVal;
  } else if (values.size() > 1) {
    diag.error(ast) << "Multiply defined symbol " << ast;
    return &Expr::ErrorVal;
  } else {
    Expr * value = values.front();
    if (LValueExpr * lval = dyn_cast<LValueExpr>(value)) {
      return reduceLValueExpr(lval, store);
    } else if (TypeLiteralExpr * typeExpr = dyn_cast<TypeLiteralExpr>(value)) {
      return typeExpr;
    } else if (ScopeNameExpr * scopeName = dyn_cast<ScopeNameExpr>(value)) {
      return scopeName;
    } else {
      DFAIL("Not an LValue");
    }
  }
}

Expr * ExprAnalyzer::reduceElementRef(const ASTOper * ast, bool store) {
  // TODO: We might want to support more than 1 array index.
  DASSERT_OBJ(ast->count() >= 1, ast);
  if (ast->count() == 1) {
    Expr * elementExpr = reduceExpr(ast->arg(0), NULL);
    if (TypeLiteralExpr * elementType = dyn_cast_or_null<TypeLiteralExpr>(elementExpr)) {
      return new TypeLiteralExpr(ast->location(), getArrayTypeForElement(elementType->value()));
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
      // For type names and function names, brackets always mean specialize, not index.
      bool isTypeOrFunc = false;
      for (ExprList::iterator it = values.begin(); it != values.end(); ++it) {
        if (isa<TypeLiteralExpr>(*it)) {
          isTypeOrFunc = true;
          break;
        } else if (LValueExpr * lv = dyn_cast<LValueExpr>(*it)) {
          if (isa<FunctionDefn>(lv->value())) {
            isTypeOrFunc = true;
            break;
          }
        }
      }

      if (isTypeOrFunc) {
        ASTNodeList args;
        args.insert(args.begin(), ast->args().begin() + 1, ast->args().end());
        Expr * specResult = specialize(ast->location(), values, args);
        if (specResult != NULL) {
          return specResult;
        } else {
          return &Expr::ErrorVal;
        }
      }

      if (values.size() > 1) {
        diag.error(base) << "Multiply defined symbol " << base;
        return &Expr::ErrorVal;
      }

      arrayExpr = values.front();
      if (LValueExpr * lval = dyn_cast<LValueExpr>(arrayExpr)) {
        arrayExpr = reduceLValueExpr(lval, store);
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

  const Type * arrayType = dealias(arrayExpr->type());
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

  // Memory address type
  if (const AddressType * maType = dyn_cast<AddressType>(arrayType)) {
    TypeRef elemType = maType->typeParam(0);
    if (!AnalyzerBase::analyzeType(elemType, Task_PrepTypeComparison)) {
      return &Expr::ErrorVal;
    }

    DASSERT_OBJ(elemType.isDefined(), maType);

    if (args.size() != 1) {
      diag.fatal(ast) << "Incorrect number of array index dimensions";
    }

    // TODO: Attempt to cast arg to an integer type of known size.

    // First dereference the pointer and then get the element.
    //arrayExpr = new UnaryExpr(Expr::PtrDeref, arrayExpr->location(), elemType, arrayExpr);
    return new BinaryExpr(Expr::ElementRef, ast->location(), elemType.type(), arrayExpr, args[0]);
  }

  // Do automatic pointer dereferencing
  /*if (NativePointerType * npt = dyn_cast<NativePointerType>(arrayType)) {
   if (!AnalyzerBase::analyzeType(npt, Task_PrepTypeComparison)) {
   return &Expr::ErrorVal;
   }

   arrayType = npt->typeParam(0);
   arrayExpr = new UnaryExpr(Expr::PtrDeref, arrayExpr->location(), arrayType, arrayExpr);
   }*/

  // Handle native arrays.
  if (const NativeArrayType * naType = dyn_cast<NativeArrayType>(arrayType)) {
    if (!AnalyzerBase::analyzeType(naType, Task_PrepTypeComparison)) {
      return &Expr::ErrorVal;
    }

    TypeRef elemType = naType->typeParam(0);
    DASSERT_OBJ(elemType.isDefined(), naType);

    if (args.size() != 1) {
      diag.fatal(ast) << "Incorrect number of array index dimensions";
    }

    // TODO: Attempt to cast arg to an integer type of known size.
    return new BinaryExpr(Expr::ElementRef, ast->location(), elemType.type(), arrayExpr, args[0]);
  }

  // See if the type has any indexers defined.
  DefnList indexers;
  if (arrayType->typeDefn() != NULL) {
    if (!AnalyzerBase::analyzeType(arrayType, Task_PrepMemberLookup)) {
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
  if (!analyzeDefn(indexer, Task_PrepTypeComparison)) {
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
    PropertyDefn * prop) {

  if (!analyzeValueDefn(prop, Task_PrepTypeComparison)) {
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

  Expr::ExprType callType = Expr::FnCall;
  if (basePtr->type()->typeClass() == Type::Interface ||
      (basePtr->type()->typeClass() == Type::Class && !getter->isFinal())) {
    callType = Expr::VTableCall;
  }

  FnCallExpr * getterCall = new FnCallExpr(callType, loc, getter, basePtr);
  getterCall->setType(prop->type());
  module->addSymbol(getter);
  return getterCall;
}

Expr * ExprAnalyzer::reduceSetPropertyValue(const SourceLocation & loc,
    Expr * basePtr, PropertyDefn * prop, Expr * value) {

  DASSERT(value != NULL);
  if (!analyzeValueDefn(prop, Task_PrepTypeComparison)) {
    return &Expr::ErrorVal;
  }

  FunctionDefn * setter = prop->setter();
  if (setter == NULL) {
    diag.error(loc) << "Attempt to set value of read-only property '" << prop << "'";
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(setter->isSingular(), prop);

  if (!analyzeValueDefn(setter, Task_PrepTypeComparison)) {
    return &Expr::ErrorVal;
  }

  //ExprList callingArgs;
  //callingArgs.push_back(value);

  // TODO: Compile-time evaluation if possible.
  /*Expr * expr = getter->eval(in->location(), callingArgs);
   if (expr != NULL) {
   return expr;
   }*/

  Expr::ExprType callType = Expr::FnCall;
  if (basePtr->type()->typeClass() == Type::Interface ||
      (basePtr->type()->typeClass() == Type::Class && !setter->isFinal())) {
    callType = Expr::VTableCall;
  }

  FnCallExpr * setterCall = new FnCallExpr(callType, loc, setter, basePtr);
  setterCall->setType(prop->type());
  setterCall->appendArg(value);
  module->addSymbol(setter);
  return setterCall;
}

Expr * ExprAnalyzer::reduceGetParamPropertyValue(const SourceLocation & loc, CallExpr * call) {

  LValueExpr * lval = cast<LValueExpr>(call->function());
  PropertyDefn * prop = cast<PropertyDefn>(lval->value());
  Expr * basePtr = lvalueBase(lval);
  //const ASTNodeList & args = call->args();

  if (!analyzeValueDefn(prop, Task_PrepTypeComparison)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(prop->getter()->isSingular(), prop);

  FunctionDefn * getter = prop->getter();
  if (getter == NULL) {
    diag.fatal(prop) << "No getter for property '" << prop->name() << "'";
    return &Expr::ErrorVal;
  }

  if (!analyzeValueDefn(getter, Task_PrepTypeComparison)) {
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(getter->returnType().isEqual(prop->type()), getter);
  DASSERT_OBJ(getter->returnType().isNonVoidType(), getter);

#if 0

  CallExpr * call = new CallExpr(Expr::Call, loc, NULL);
  call->setExpectedReturnType(prop->type());
  if (!addOverload(call, basePtr, getter, args)) {
    return &Expr::ErrorVal;
  }

  if (!reduceArgList(args, call)) {
    return &Expr::ErrorVal;
  }

  if (call->candidates().empty()) {
    // Generate the calling signature in a buffer.
    std::stringstream callsig;
    FormatStream fs(callsig);
    fs << Format_Dealias << basePtr << "[";
    formatExprTypeList(fs, call->args());
    fs << "]";
    fs << " -> " << prop->type();

    diag.error(loc) << "No matching method for call to " << callsig.str() << ", candidates are:";
    /*for (ExprList::iterator it = results.begin(); it != results.end(); ++it) {
     Expr * resultMethod = *it;
     if (LValueExpr * lval = dyn_cast<LValueExpr>(*it)) {
     diag.info(lval->value()) << Format_Type << lval->value();
     } else {
     diag.info(*it) << *it;
     }
     }*/

    return &Expr::ErrorVal;
  }

  call->setType(reduceReturnType(call));
  return call;

#else

  // TODO: Type check args against function signature.

  ExprList castArgs;
  size_t argCount = call->args().size();
  for (size_t i = 0; i < argCount; ++i) {
    TypeRef paramType = getter->functionType()->param(i)->type();
    Expr * arg = call->arg(i);
    arg = inferTypes(subject(), arg, paramType.type());
    Expr * castArg = paramType.implicitCast(arg->location(), arg);
    if (isErrorResult(castArg)) {
      return &Expr::ErrorVal;
    }

    castArgs.push_back(castArg);
  }

  Expr::ExprType callType = Expr::FnCall;
  if (basePtr->type()->typeClass() == Type::Interface ||
      (basePtr->type()->typeClass() == Type::Class && !getter->isFinal())) {
    callType = Expr::VTableCall;
  }

  FnCallExpr * getterCall = new FnCallExpr(callType, loc, getter, basePtr);
  getterCall->setType(prop->type());
  getterCall->args().append(castArgs.begin(), castArgs.end());
  module->addSymbol(getter);
  return getterCall;
#endif
}

Expr * ExprAnalyzer::reduceSetParamPropertyValue(const SourceLocation & loc, CallExpr * call,
    Expr * value) {

  LValueExpr * lval = cast<LValueExpr>(call->function());
  PropertyDefn * prop = cast<PropertyDefn>(lval->value());
  Expr * basePtr = lvalueBase(lval);

  if (!analyzeValueDefn(prop, Task_PrepTypeComparison)) {
    return &Expr::ErrorVal;
  }

  FunctionDefn * setter = prop->setter();
  if (setter == NULL) {
    diag.error(loc) << "Attempt to set value of read-only property '" << prop << "'";
    return &Expr::ErrorVal;
  }

  DASSERT_OBJ(prop->isSingular(), prop);
  DASSERT_OBJ(setter->isSingular(), prop);

  if (!analyzeValueDefn(setter, Task_PrepTypeComparison)) {
    return &Expr::ErrorVal;
  }

  // TODO: Type check args against function signature.

  ExprList castArgs;
  size_t argCount = call->args().size();
  for (size_t i = 0; i < argCount; ++i) {
    Expr * arg = call->arg(i);
    Expr * castArg = setter->functionType()->param(i)->type().implicitCast(arg->location(), arg);
    if (isErrorResult(castArg)) {
      return &Expr::ErrorVal;
    }

    castArgs.push_back(castArg);
  }

  Expr::ExprType callType = Expr::FnCall;
  if (basePtr->type()->typeClass() == Type::Interface ||
      (basePtr->type()->typeClass() == Type::Class && !setter->isFinal())) {
    callType = Expr::VTableCall;
  }

  // TODO: Handle multiple overloads of the same property.
  // TODO: Change this to a CallExpr for type inference.
  FnCallExpr * setterCall = new FnCallExpr(callType, loc, setter, basePtr);
  setterCall->setType(prop->type());
  setterCall->args().append(castArgs.begin(), castArgs.end());
  // TODO: Remove this cast when we do the above.
  if (!value->isSingular()) {
    value = inferTypes(subject(), value, prop->type().type());
    if (isErrorResult(value)) {
      return value;
    }
  }

  value = prop->type().implicitCast(loc, value);
  if (value != NULL) {
    setterCall->appendArg(value);
  }

  module->addSymbol(setter);
  return setterCall;
}

Expr * ExprAnalyzer::reduceLValueExpr(LValueExpr * lvalue, bool store) {
  DASSERT(lvalue->value() != NULL);
  analyzeValueDefn(lvalue->value(), Task_PrepTypeComparison);
  DASSERT(lvalue->value()->type().isDefined());
  lvalue->setType(lvalue->value()->type());
  if (ParameterDefn * param = dyn_cast<ParameterDefn>(lvalue->value())) {
    lvalue->setType(param->internalType());
  }

  checkAccess(lvalue->location(), lvalue->value());
  switch (lvalue->value()->storageClass()) {
    case Storage_Global:
    case Storage_Static:
    case Storage_Local:
      lvalue->setBase(NULL);
      break;

    case Storage_Instance: {
      Expr * base = lvalueBase(lvalue);
      if (base == NULL || base->exprType() == Expr::ScopeName) {
        diag.error(lvalue) << "Attempt to reference non-static member " <<
        lvalue->value()->name() << " with no object";
        return &Expr::ErrorVal;
      }

      // TODO: Handle type names and such

      break;
    }

    case Storage_Class:
    case Storage_Param:
    case Storage_Closure:
    default:
      DFAIL("Invalid storage class");
  }

  if (lvalue->base() != NULL && lvalue->base()->type()->typeClass() == Type::NPointer) {
    lvalue->setBase(new UnaryExpr(Expr::PtrDeref, lvalue->base()->location(),
        lvalue->base()->type()->typeParam(0), lvalue->base()));
  }

  // If it's not a store, and it's a property access, then dereference into getter calls.
  // (If it is a store, then do nothing - that can only be handled once we know what it
  // is we are storing.)
  if (!store) {
    if (PropertyDefn * prop = dyn_cast<PropertyDefn>(lvalue->value())) {
      return reduceGetPropertyValue(lvalue->location(), lvalueBase(lvalue), prop);
    }
  }

  return lvalue;
}

/** Given an LValue, return the base expression. */
Expr * ExprAnalyzer::lvalueBase(LValueExpr * lval) {
  Expr * base = lval->base();
  if (LValueExpr * lvbase = dyn_cast_or_null<LValueExpr>(base)) {
    return reduceLValueExpr(lvbase, false);
  }

  return base;
}

Expr * ExprAnalyzer::doImplicitCast(Expr * in, const TypeRef & toType) {
  return doImplicitCast(in, toType.type());
}

Expr * ExprAnalyzer::doImplicitCast(Expr * in, const Type * toType, bool tryCoerce) {
  DASSERT(in != NULL);
  if (isErrorResult(toType)) {
    return in;
  }

  in = LValueExpr::constValue(in);
  if (!AnalyzerBase::analyzeType(toType, Task_PrepTypeComparison)) {
    return in;
  }

  Expr * castExpr = NULL;
  ConversionRank rank = toType->convert(Conversion(in, &castExpr));
  DASSERT(rank == Incompatible || castExpr != NULL);

  if (rank == Incompatible && tryCoerce) {
    // Try a coercive cast. Note that we don't do this in 'convert' because it
    // can't handle building the actual call expression.
    castExpr = tryCoerciveCast(in, toType);
    if (castExpr != NULL) {
      Expr * result = inferTypes(subject_, castExpr, toType, false);
      return result;
    }
  }

  compatibilityWarning(in->location(), rank, in, toType);
  if (isErrorResult(castExpr)) {
    return in;
  }

  return castExpr;
}

Defn * ExprAnalyzer::findBestSpecialization(SpecializeExpr * spe) {
  const SpCandidateList & candidates = spe->candidates();
  ConversionRank bestRank = Incompatible;
  for (SpCandidateList::const_iterator it = candidates.begin(); it != candidates.end(); ++it) {
    SpCandidate * sp = *it;
    bestRank = std::max(bestRank, sp->updateConversionRank());
  }

  if (bestRank == Incompatible) {
    SpCandidate * front = *candidates.begin();
    diag.error(spe) << "No candidate found for '" << front->def()->name() <<
        "' which matches template arguments [" << spe->args() << "]:";
    diag.info(spe) << "Candidates are:";
    for (SpCandidateList::const_iterator it = candidates.begin(); it != candidates.end(); ++it) {
      SpCandidate * sp = *it;
      sp->updateConversionRank(); // Helps with debugging.
      diag.info(sp->def()) << Format_Type << sp->def() << " [" << sp->conversionRank() << "]";
    }
    return NULL;
  }

  SpCandidateList bestCandidates;
  for (SpCandidateList::const_iterator it = candidates.begin(); it != candidates.end(); ++it) {
    SpCandidate * sp = *it;
    if (sp->conversionRank() == bestRank) {
      bool addNew = true;
      for (SpCandidateList::iterator bc = bestCandidates.begin(); bc != bestCandidates.end();) {
        bool newIsBetter = sp->isMoreSpecific(*bc);
        bool oldIsBetter = (*bc)->isMoreSpecific(sp);

        if (newIsBetter) {
          if (!oldIsBetter) {
            /*if (ShowInference) {
              diag.debug() << Format_Type << "Culling [" << (*ms)->method() <<
                  "] because [" << call->method() << "] is more specific";
            }*/
            bc = bestCandidates.erase(bc);
            continue;
          }
        } else if (oldIsBetter) {
          /*if (ShowInference) {
            diag.debug() << Format_Type << "Culling [" << call->method() << "] because [" <<
                (*ms)->method() << "] is more specific";
          }*/

          addNew = false;
        }

        ++bc;
      }

      if (addNew) {
        bestCandidates.push_back(sp);
      }
    }
  }

  if (bestCandidates.size() > 1) {
    SpCandidate * front = *candidates.begin();
    diag.error(spe) << "Ambiguous matches for '" << front->def()->name() <<
        "' which matches template arguments [" << spe->args() << "]:";
    diag.info(spe) << "Candidates are:";
    for (SpCandidateList::const_iterator it = bestCandidates.begin(); it != bestCandidates.end();
        ++it) {
      SpCandidate * sp = *it;
      diag.info(sp->def()) << Format_Type << sp->def() << " [" << sp->conversionRank() << "]";
    }
    DFAIL("Implement better culling of SpCandidates");
  }

  SpCandidate * spBest = *bestCandidates.begin();
  if (spBest->def()->hasUnboundTypeParams()) {
    return spBest->def()->templateSignature()->instantiate(spe->location(), spBest->env());
  }

  return spBest->def();
}

Expr * ExprAnalyzer::doUnboxCast(Expr * in, const Type * toType) {
  FunctionDefn * valueOfMethod = getUnboxFn(in->location(), toType);
  if (valueOfMethod == NULL) {
    return NULL;
  }

  FnCallExpr * call = new FnCallExpr(Expr::FnCall, in->location(), valueOfMethod, NULL);
  call->appendArg(doImplicitCast(in, Builtins::typeObject));
  call->setType(valueOfMethod->returnType());
  return call;
}

FunctionDefn * ExprAnalyzer::getUnboxFn(SLC & loc, const Type * toType) {
  ExprList methods;
  TypePair conversionKey(Builtins::typeObject.get(), toType);
  ConverterMap::iterator it = module->converters().find(conversionKey);
  if (it != module->converters().end()) {
    return it->second;
  }

  //diag.debug(loc) << Format_Type << "Defining unbox function for " << toType;
  analyzeDefn(Builtins::typeRef->typeDefn(), Task_PrepMemberLookup);
  findInScope(methods, "valueOf", Builtins::typeRef->memberScope(), NULL, loc);
  DASSERT(!methods.empty());
  Expr * valueOf = specialize(loc, methods, TupleType::get(toType));
  FunctionDefn * valueOfMethod;
  if (SpecializeExpr * spe = dyn_cast<SpecializeExpr>(valueOf)) {
    valueOfMethod = cast_or_null<FunctionDefn>(findBestSpecialization(spe));
    if (valueOfMethod == NULL) {
      return NULL;
    }
  } else if (LValueExpr * lval = dyn_cast<LValueExpr>(valueOf)) {
    valueOfMethod = cast<FunctionDefn>(lval->value());
  } else {
    diag.error(loc) << "Unknown expression " << valueOf;
    DFAIL("IllegalState");
  }

  analyzeDefn(valueOfMethod, Task_PrepTypeComparison);
  module->converters()[conversionKey] = valueOfMethod;
  //diag.debug(loc) << Format_Type << "Defining unbox function: " << valueOfMethod;
  return valueOfMethod;
}

}
