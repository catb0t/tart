/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/Expr/Exprs.h"
#include "tart/Expr/StmtExprs.h"
#include "tart/Defn/FunctionDefn.h"
#include "tart/Type/FunctionType.h"
#include "tart/Type/CompositeType.h"
#include "tart/Defn/TypeDefn.h"
#include "tart/Type/NativeType.h"
#include "tart/Type/TupleType.h"
#include "tart/Type/PrimitiveType.h"
#include "tart/Type/UnionType.h"
#include "tart/Type/UnitType.h"
#include "tart/Expr/Constant.h"
#include "tart/Sema/EvalPass.h"
#include "tart/Sema/AnalyzerBase.h"
#include "tart/Common/Diagnostics.h"

namespace tart {

static ConstantNull nullValue = ConstantNull(SourceLocation());

/// -------------------------------------------------------------------
/// EvalPass

Expr * EvalPass::eval(Module * module, Expr * in, bool allowPartial) {
  allowPartial = false;
  return EvalPass(module, allowPartial).evalExpr(in);
}

Expr * EvalPass::evalExpr(Expr * in) {
  if (in == NULL) {
    return NULL;
  }

  switch (in->exprType()) {
    case Expr::Invalid:
      return &Expr::ErrorVal;

    case Expr::TypeCount:
      DFAIL("Invalid");

    case Expr::NoOp:
      return in;

    case Expr::ConstInt:
    case Expr::ConstFloat:
    case Expr::ConstString:
    case Expr::ConstNull:
    case Expr::ConstObjRef:
    case Expr::ConstEmptyArray:
    case Expr::TypeLiteral:
      return in;

#if 0
    case Expr::Instance:
      return evalInstance(static_cast<ConstantObjectRef *>(in));
#endif

    case Expr::LValue:
      return evalLValue(static_cast<LValueExpr *>(in));

#if 0
    case Expr::BoundMethod:
      return evalBoundMethod(static_cast<BoundMethodExpr *>(in));

    case Expr::ElementRef:
      return evalElementRef(static_cast<BinaryExpr *>(in));
#endif

    case Expr::Assign:
      return evalAssign(static_cast<AssignmentExpr *>(in));

#if 0
    case Expr::PostAssign:
      return evalPostAssign(static_cast<AssignmentExpr *>(in));

    case Expr::MultiAssign:
      ??

    case Expr::Call:
    case Expr::SuperCall:
    //case Expr::ICall:
    case Expr::Construct:
      return evalCall(static_cast<CallExpr *>(in));
#endif

    case Expr::FnCall:
    case Expr::CtorCall:
      return evalFnCall(static_cast<FnCallExpr *>(in));

    case Expr::New:
      return evalNew(static_cast<NewExpr *>(in));

#if 0
    case Expr::Instantiate:
      return evalInstantiate(static_cast<InstantiateExpr *>(in));

    case Expr::ImplicitCast:
    case Expr::ExplicitCast:
    case Expr::TryCast:
    case Expr::DynamicCast:
    case Expr::Truncate:
    case Expr::SignExtend:
    case Expr::ZeroExtend:
    case Expr::IntToFloat:
    case Expr::BitCast:
    case Expr::UnionMemberCast:
      return evalCast(static_cast<CastExpr *>(in));
#endif
    case Expr::UpCast:
    case Expr::QualCast:
      return evalExpr(static_cast<CastExpr *>(in)->arg());

    case Expr::UnionCtorCast:
      return evalUnionCtorCast(static_cast<CastExpr *>(in));

    case Expr::BinaryOpcode:
      return evalBinaryOpcode(static_cast<BinaryOpcodeExpr *>(in));

    case Expr::Compare:
      return evalCompare(static_cast<CompareExpr *>(in));

#if 0
    case Expr::InstanceOf:
      return evalInstanceOf(static_cast<InstanceOfExpr *>(in));

    case Expr::RefEq:
      return evalRefEq(static_cast<BinaryExpr *>(in));

    case Expr::PtrDeref:
      return evalPtrDeref(static_cast<UnaryExpr *>(in));
#endif

    case Expr::Not:
      return evalNot(static_cast<UnaryExpr *>(in));

    case Expr::Complement:
      return evalComplement(static_cast<UnaryExpr *>(in));

#if 0
    case Expr::InitVar:
      return evalInitVar(static_cast<InitVarExpr *>(in));

    case Expr::Prog2:
      return evalProg2(static_cast<BinaryExpr *>(in));
#endif

    case Expr::ArrayLiteral:
      return evalArrayLiteral(static_cast<ArrayLiteralExpr *>(in));

#if 0
    case Expr::IRValue:
      return in;

    case Expr::PatternVar:
      DFAIL("PatternVar");

    case Expr::PtrCall:
      DFAIL("PtrCall");
#endif

    case Expr::ClearVar:
      return in;

    case Expr::Seq:
      return evalSeq(static_cast<SeqExpr *>(in));

    case Expr::Return:
      return evalReturn(static_cast<ReturnExpr *>(in));

    default:
      break;
  }

  diag.error(in) << "Expr type not handled in eval: " <<
      exprTypeName(in->exprType()) << " : " << in;
  showCallStack();
  DFAIL("Fall through");
}

ConstantExpr * EvalPass::evalConstantExpr(Expr * in) {
  Expr * e = evalExpr(in);
  if (!isErrorResult(e)) {
    if (ConstantExpr * ce = dyn_cast<ConstantExpr>(e)) {
      return ce;
    } else if (!allowPartial_) {
      diag.error(in) << "Not a constant: " << in;
      showCallStack();
    }
  }

  return NULL;
}

#if 0
bool EvalPass::evalBlocks(BlockList & blocks) {
  Block * block = blocks.front();

  for (;;) {
    for (ExprList::iterator it = block->exprs().begin(); it != block->exprs().end(); ++it) {
      if (evalExpr(*it) == NULL) {
        return false;
      }
    }

    switch (block->terminator()) {
      case BlockTerm_None:
        DFAIL("Invalid block term");
        break;

      case BlockTerm_Branch:
        block = block->succs()[0];
        break;

      case BlockTerm_Return: {
        if (block->termValue() != NULL) {
          callFrame_->setReturnVal(evalExpr(block->termValue()));
        } else {
          callFrame_->setReturnVal(&nullValue);
        }

        return true;
      }

      case BlockTerm_Conditional: {
        BooleanResult bresult = asConstBoolean(block->termValue());
        if (bresult == BOOLEAN_ERROR) {
          return false;
        }

        int branch = 0;
        if (bresult == BOOLEAN_FALSE) {
          branch = 1;
        }

        block = block->succs()[branch];
        break;
      }

      case BlockTerm_Switch:
      case BlockTerm_Throw:
      case BlockTerm_ResumeUnwind:
      case BlockTerm_LocalReturn:
      case BlockTerm_Catch:
      case BlockTerm_TraceCatch:
        DFAIL("block term not handled");
        break;
    }
  }
}
#endif

Expr * EvalPass::evalFnCall(FnCallExpr * in) {
  FunctionDefn * func = in->function();
  AnalyzerBase::analyzeFunction(func, Task_PrepEvaluation);
  CallFrame frame(callFrame_);
  frame.setFunction(func);
  frame.setCallLocation(in->location());
  frame.setReturnVal(&Expr::VoidVal);
  if (in->selfArg() != NULL) {
    Expr * selfArg = evalExpr(in->selfArg());
    if (selfArg == NULL) {
      return NULL;
    }

    frame.setSelfArg(selfArg);
  }

  for (ExprList::iterator it = in->args().begin(); it != in->args().end(); ++it) {
    Expr * arg = evalExpr(*it);
    if (arg == NULL) {
      return NULL;
    }

    frame.args().push_back(arg);
  }

  Expr * evalResult = func->eval(in->location(), module_, frame.selfArg(), frame.args());
  if (evalResult != NULL) {
    return evalResult;
  }

  if (!func->hasBody()) {
    DASSERT_OBJ(func->passes().isFinished(FunctionDefn::ControlFlowPass), func);
    if (!allowPartial_) {
      diag.error(in) << "Cannot evaluate function " << func << " at compile time";
    }

    return NULL;
  }

  if (!AnalyzerBase::analyzeFunction(func, Task_PrepEvaluation)) {
    return NULL;
  }

  DASSERT_OBJ(func->body() != NULL, func);

  CallFrame * prevFrame = setCallFrame(&frame);
  evalExpr(func->body());
  setCallFrame(prevFrame);

  if (in->exprType() == Expr::CtorCall) {
    return frame.selfArg();
  }

  return frame.returnVal();
}

Expr * EvalPass::evalLValue(LValueExpr * in) {
  if (ParameterDefn * param = dyn_cast<ParameterDefn>(in->value())) {
    FunctionType * ftype = callFrame_->function()->functionType();
    if (param == ftype->selfParam()) {
      return callFrame_->selfArg();
    }

    for (size_t i = 0; i < ftype->params().size(); ++i) {
      if (ftype->params()[i] == param) {
        return callFrame_->args()[i];
      }
    }

    DFAIL("Couldn't locate param");
  } else  if (VariableDefn * var = dyn_cast<VariableDefn>(in->value())) {
    if (var->defnType() == Defn::Let) {
      if (var->storageClass() == Storage_Global || var->storageClass() == Storage_Static) {
        if (var->type()->isReferenceType()) {
          return in;
        }
        return evalExpr(var->initValue());
      }
    }

    switch (var->storageClass()) {
      case Storage_Global:
        DFAIL("IMPLEMENT Storage_Global");
        break;

      case Storage_Instance: {
        DASSERT(in->base() != NULL);
        Expr * base = evalExpr(in->base());
        if (base == NULL) {
          return NULL;
        }

        if (ConstantObjectRef * inst = dyn_cast<ConstantObjectRef>(base)) {
          return inst->getMemberValue(var);
        } else {
          diag.fatal(base) << "Base not handled " << base;
        }
        break;
      }

      case Storage_Class:
        DFAIL("IMPLEMENT Storage_Class");
        break;

      case Storage_Static:
        if (!allowPartial_) {
          diag.error(in) << "Not a constant: " << var;
          showCallStack();
        }

        return NULL;

      case Storage_Local:
        if (var->defnType() == Defn::MacroArg) {
          return evalExpr(var->initValue());
        }

        return callFrame_->getLocal(var);
    }
  }

  DFAIL("Not an LValue");
}

Expr * EvalPass::evalNew(NewExpr * in) {
  const CompositeType * type = cast<CompositeType>(in->type().type());
  if (!AnalyzerBase::analyzeTypeDefn(type->typeDefn(), Task_PrepEvaluation)) {
    return NULL;
  }

  return new ConstantObjectRef(in->location(), type);
}

Expr * EvalPass::evalAssign(AssignmentExpr * in) {
  Expr * from = evalExpr(in->fromExpr());
  if (from == NULL) {
    return NULL;
  }

  store(from, in->toExpr());
  return in->toExpr();
}

void EvalPass::store(Expr * value, Expr * dest) {
  if (dest->exprType() == Expr::LValue) {
    LValueExpr * lvalue = static_cast<LValueExpr *>(dest);
    if (VariableDefn * var = dyn_cast<VariableDefn>(lvalue->value())) {
      if (var->defnType() == Defn::Let) {
        if (var->storageClass() == Storage_Global || var->storageClass() == Storage_Static) {
          DFAIL("Invalid assignment to constant");
          return;
        }
      }

      switch (var->storageClass()) {
        case Storage_Global:
          DFAIL("IMPLEMENT Storage_Global");
          break;

        case Storage_Instance: {
          DASSERT(lvalue->base() != NULL);
          Expr * base = evalExpr(lvalue->base());
          if (base == NULL) {
            return;
          }

          if (ConstantObjectRef * inst = dyn_cast<ConstantObjectRef>(base)) {
            inst->setMemberValue(var, value);
          } else {
            diag.fatal(base) << "Base not handled " << base;
          }
          break;
        }

        case Storage_Class:
          DFAIL("IMPLEMENT Storage_Class");
          break;

        case Storage_Static:
          if (!allowPartial_) {
            diag.fatal(dest) << "Not a constant: " << var;
          }

          diag.debug(var) << var;
          DFAIL("IMPLEMENT Storage_Static");
          break;

        case Storage_Local:
          callFrame_->setLocal(var, value);
          break;
      }
    } else if (ParameterDefn * param = dyn_cast<ParameterDefn>(lvalue->value())) {
      (void)param;
      diag.debug() << dest;
      DFAIL("Implement assign to param");
    }
  } else {
    diag.debug() << dest;
    DFAIL("Implement assign to non-lvalue");
  }
}

Expr * EvalPass::evalSeq(SeqExpr * in) {
  Expr * result = NULL;
  for (SeqExpr::const_iterator it = in->begin(), itEnd = in->end(); it != itEnd; ++it) {
    result = evalExpr(*it);
    if (result == NULL || callFrame_->runState() != RUNNING) {
      break;
    }
  }
  return result;
}

Expr * EvalPass::evalReturn(ReturnExpr * in) {
  if (in->arg() != NULL) {
    callFrame_->setReturnVal(evalExpr(in->arg()));
  } else {
    callFrame_->setReturnVal(&nullValue);
  }
  callFrame_->setRunState(RETURN);
  return in;
}

Expr * EvalPass::evalNot(UnaryExpr * in) {
  BooleanResult bresult = asConstBoolean(in->arg());
  if (bresult == BOOLEAN_ERROR) {
    return NULL;
  } else if (bresult == BOOLEAN_TRUE) {
    return ConstantInteger::getConstantBool(in->location(), false);
  } else {
    return ConstantInteger::getConstantBool(in->location(), true);
  }
}

Expr * EvalPass::evalComplement(UnaryExpr * in) {
  llvm::Constant * value = asConstNumber(evalConstantExpr(in->arg()));
  if (value == NULL) {
    return NULL;
  }

  value = llvm::ConstantExpr::getNot(value);
  if (llvm::ConstantInt * cint = dyn_cast<llvm::ConstantInt>(value)) {
    return ConstantInteger::get(in->location(), in->type().unqualified(), cint);
  }

  diag.debug(in) << in;
  DFAIL("Implement");
}

Expr * EvalPass::evalArrayLiteral(ArrayLiteralExpr * in) {
  QualifiedType arrayType = in->type();
  if (!AnalyzerBase::analyzeType(arrayType, Task_PrepEvaluation)) {
    return NULL;
  }

  QualifiedType elementType = arrayType->typeParam(0);
  QualifiedTypeList naTypeArgs;
  naTypeArgs.push_back(elementType);
  naTypeArgs.push_back(UnitType::get(
      ConstantInteger::get(in->location(), &UInt64Type::instance, in->args().size())));
  ConstantNativeArray * arrayData =
      new ConstantNativeArray(
          in->location(),
          NativeArrayType::get(TupleType::get(naTypeArgs)));
  for (ExprList::iterator it = in->args().begin(); it != in->args().end(); ++it) {
    Expr * element = elementType->implicitCast((*it)->location(), evalExpr(*it));
    if (element == NULL) {
      // Return NULL if any of the elements in the array are not constants.
      return NULL;
    }

    arrayData->elements().push_back(element);
  }

  if (in->type()->typeClass() == Type::NArray) {
    return arrayData;
  }

  // If it's empty, then prepare the empty list singleton.
  const CompositeType * arrayClass = cast<CompositeType>(arrayType.type());
  if (arrayData->elements().empty()) {
    return new ConstantEmptyArray(in->location(), arrayClass);
  }

  // Constant array objects are special because of their variable size.
  ConstantObjectRef * arrayObj = new ConstantObjectRef(in->location(), arrayClass);
  arrayObj->setMemberValue("_size",
      ConstantInteger::getUInt(arrayData->elements().size())->at(in->location()));
  arrayObj->setMemberValue("_data", arrayData);
  return arrayObj;
}

Expr * EvalPass::evalUnionCtorCast(CastExpr *in) {
  const Type * fromType = in->arg()->type().unqualified();
  const Type * toType = in->type().unqualified();
  Expr * value = NULL;

  if (!fromType->isVoidType()) {
    value = evalExpr(in->arg());
    if (value == NULL) {
      return NULL;
    }
  }

  if (toType != NULL) {
    const UnionType * utype = cast<UnionType>(toType);
    if (!utype->hasRefTypesOnly()) {
      int index = utype->getTypeIndex(fromType);
      if (index < 0) {
        diag.error() << "Can't convert " << fromType << " to " << utype;
      }
      DASSERT(index >= 0);

      DFAIL("Implement");
#if 0
      Value * indexVal = ConstantInt::get(utype->irType()->getContainedType(0), index);
      Value * uvalue = builder_.CreateAlloca(utype->irType());
      builder_.CreateStore(indexVal, builder_.CreateConstInBoundsGEP2_32(uvalue, 0, 0));
      if (value != NULL) {
        llvm::Type * fieldType = fromType->irEmbeddedType();
        builder_.CreateStore(value,
            builder_.CreateBitCast(
                builder_.CreateConstInBoundsGEP2_32(uvalue, 0, 1),
                fieldType->getPointerTo()));
      }

      return builder_.CreateLoad(uvalue);
#endif
    } else {
      in->setArg(value);
      return in;
    }
  }

  return NULL;
}

Expr * EvalPass::evalBinaryOpcode(BinaryOpcodeExpr *in) {
  llvm::Constant * c0 = asConstNumber(evalConstantExpr(in->first()));
  llvm::Constant * c1 = asConstNumber(evalConstantExpr(in->second()));
  if (c0 == NULL || c1 == NULL) {
    return NULL;
  }

  llvm::Constant * cresult = llvm::ConstantExpr::get(in->opCode(), c0, c1);
  if (llvm::ConstantInt * cint = dyn_cast<llvm::ConstantInt>(cresult)) {
    return ConstantInteger::get(in->location(), in->type().unqualified(), cint);
  }

  switch (in->opCode()) {
    case llvm::Instruction::Add:
      break;

    default:
      break;
  }

  diag.debug(in) << in;
  DFAIL("Implement");
}

Expr * EvalPass::evalCompare(CompareExpr *in) {
  Expr * first = evalExpr(in->first());
  Expr * second = evalExpr(in->second());
  if (first == NULL || second == NULL) {
    return NULL;
  } else if (ConstantInteger * c1 = dyn_cast<ConstantInteger>(first)) {
    if (ConstantInteger * c2 = dyn_cast<ConstantInteger>(second)) {
      llvm::Constant * cr = llvm::ConstantExpr::getCompare(
          in->predicate(), c1->value(), c2->value());
      return ConstantInteger::get(in->location(), &BoolType::instance, cast<llvm::ConstantInt>(cr));
    }
  } else if (ConstantFloat * c1 = dyn_cast<ConstantFloat>(first)) {
    if (ConstantFloat * c2 = dyn_cast<ConstantFloat>(second)) {
      llvm::Constant * cr = llvm::ConstantExpr::getCompare(
          in->predicate(), c1->value(), c2->value());
      return ConstantInteger::get(in->location(), &BoolType::instance, cast<llvm::ConstantInt>(cr));
    }
  }

  if (!allowPartial_) {
    diag.fatal(in) << "Invalid comparison between " << first << " and " << second;
  }

  return NULL;
}

llvm::Constant * EvalPass::asConstNumber(ConstantExpr * e) {
  if (e == NULL) {
    return NULL;
  } else if (ConstantInteger * ci = dyn_cast<ConstantInteger>(e)) {
    return ci->value();
  } else if (ConstantFloat * cf = dyn_cast<ConstantFloat>(e)) {
    return cf->value();
  } else if (allowPartial_) {
    return NULL;
  } else {
    diag.error(e) << "Not a number " << e;
    return NULL;
  }
}

EvalPass::BooleanResult EvalPass::asConstBoolean(Expr * in) {
  Expr * e = evalExpr(in);
  if (e == NULL) {
    return BOOLEAN_ERROR;
  } else if (ConstantInteger * ci = dyn_cast<ConstantInteger>(e)) {
    return ci->value()->isNullValue() ? BOOLEAN_FALSE : BOOLEAN_TRUE;
  } else if (!allowPartial_) {
    diag.error(in) << "Constant boolean expected: " << in;
  }

  return BOOLEAN_ERROR;
}

Expr * EvalPass::CallFrame::getLocal(VariableDefn * var) {
  VariableMap::const_iterator it = locals_.find(var);
  if (it == locals_.end()) {
    diag.error(var) << "Uninitialized variable: " << var;
    return NULL;
  }

  return it->second;
}

void EvalPass::CallFrame::setLocal(VariableDefn * var, Expr * value) {
  locals_[var] = value;
}

void EvalPass::showCallStack() {
  // TODO: Show previous frames as well.
  for (CallFrame * cf = callFrame_; cf != NULL; cf = cf->prev()) {
    diag.info(cf->callLocation()) << "Called from here.";
  }
}

} // namespace tart
