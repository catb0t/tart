/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/CFG/Expr.h"
#include "tart/CFG/TypeDefn.h"
#include "tart/CFG/Constant.h"
#include "tart/CFG/CompositeType.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/EnumType.h"
#include "tart/CFG/NativeType.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/Module.h"
#include "tart/Gen/CodeGenerator.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Common/SourceFile.h"
#include "tart/Objects/Intrinsic.h"
#include "tart/Objects/Builtins.h"

namespace tart {

using namespace llvm;

namespace {

// -------------------------------------------------------------------
// Often intrinsics need to dereference input params
const Expr * derefMacroParam(const Expr * in) {
  if (const LValueExpr * lval = dyn_cast<LValueExpr>(in)) {
    if (lval->value()->defnType() == Defn::Let) {
      const VariableDefn * defn = static_cast<const VariableDefn *>(lval->value());
      if (defn->initValue() != NULL) {
        return defn->initValue();
      }
    }
  }

  return in;
}

}

// -------------------------------------------------------------------
// Intrinsic

StringMap<Intrinsic *> Intrinsic::intrinsicMap;

llvm::Value * Intrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  DFAIL("IllegalState");
}

Intrinsic * Intrinsic::get(const char * name) {
  static bool init = false;
  if (!init) {
    init = true;
  }

  llvm::StringMap<Intrinsic *>::const_iterator it = intrinsicMap.find(name);
  if (it != intrinsicMap.end()) {
    return it->second;
  }

  diag.fatal() << "Unknown intrinsic function '" << name << "'";
  return NULL;
}

// -------------------------------------------------------------------
// StringifyIntrinsic
StringifyIntrinsic StringifyIntrinsic::instance;

Value * StringifyIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  std::stringstream sstream;
  FormatStream fs(sstream);

  const Expr * arg = derefMacroParam(call->arg(0));
  fs << arg;

  DASSERT_OBJ(!sstream.str().empty(), arg);
  return cg.genStringLiteral(sstream.str());
}

// -------------------------------------------------------------------
// PrimitiveToStringIntrinsic
PrimitiveToStringIntrinsic PrimitiveToStringIntrinsic::instance;

Value * PrimitiveToStringIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  const FunctionDefn * fn = call->function();
  const Expr * self = call->selfArg();
  const Expr * formatString = call->arg(0);

  Value * selfArg = cg.genExpr(self);
  Value * formatStringArg = cg.genExpr(formatString);

  const PrimitiveType * ptype = cast<PrimitiveType>(dealias(self->type()));
  TypeId id = ptype->typeId();

  if (functions_[id] == NULL) {
    char funcName[32];
    snprintf(funcName, sizeof funcName, "%s_toString", ptype->typeDefn()->name());

    const llvm::Type * funcType = fn->type()->irType();
    functions_[id] = llvm::Function::Create(cast<llvm::FunctionType>(funcType),
        Function::ExternalLinkage, funcName, cg.irModule());
  }

  return cg.builder().CreateCall2(functions_[id], selfArg, formatStringArg);
}

// -------------------------------------------------------------------
// LocationOfIntrinsic
LocationOfIntrinsic LocationOfIntrinsic::instance;

Value * LocationOfIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  std::stringstream sstream;

  const Expr * arg = derefMacroParam(call->arg(0));
  SourceLocation loc = arg->location();
  if (loc.file != NULL) {
    TokenPosition pos = loc.file->tokenPosition(loc);
    sstream << loc.file->getFilePath() << ":" << pos.beginLine + 1 << ":";
  }

  return cg.genStringLiteral(sstream.str());
}

// -------------------------------------------------------------------
// VAllocIntrinsic
VAllocIntrinsic VAllocIntrinsic::instance;

Value * VAllocIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  const Type * retType = dealias(call->type());
  return cg.genVarSizeAlloc(call->location(), retType, call->arg(0));
}

// -------------------------------------------------------------------
// PVAllocIntrinsic
PVAllocIntrinsic PVAllocIntrinsic::instance;

Value * PVAllocIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  const Type * retType = dealias(call->type());
  return cg.genVarSizeAlloc(call->location(), retType, call->arg(0));
}

// -------------------------------------------------------------------
// ZeroPtrIntrinsic
ZeroPtrIntrinsic ZeroPtrIntrinsic::instance;

Value * ZeroPtrIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  const Type * retType = dealias(call->type());
  const llvm::Type * type = retType->irType();
  return ConstantPointerNull::get(PointerType::getUnqual(type));
}

// -------------------------------------------------------------------
// AddressOfIntrinsic
AddressOfIntrinsic AddressOfIntrinsic::instance;

Value * AddressOfIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  DASSERT(call->argCount() == 1);
  Value * argVal = cg.genLValueAddress(call->arg(0));
  return argVal;
}

// -------------------------------------------------------------------
// PointerDiffIntrinsic
PointerDiffIntrinsic PointerDiffIntrinsic::instance;

Value * PointerDiffIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  DASSERT(call->argCount() == 2);
  const Expr * firstPtr = call->arg(0);
  const Expr * lastPtr = call->arg(1);
  // TODO: Throw an exception if it won't fit...
  // TODO: Should use uintptr_t instead of int32.

  DASSERT_OBJ(firstPtr->type()->isEqual(lastPtr->type()), call);
  Type * elemType = cast<NativePointerType>(firstPtr->type())->typeParam(0);
  Value * firstVal = cg.genExpr(firstPtr);
  Value * lastVal = cg.genExpr(lastPtr);
  Value * diffVal = cg.builder().CreatePtrDiff(lastVal, firstVal, "ptrDiff");
  return cg.builder().CreateTrunc(diffVal, cg.builder().getInt32Ty());
}

// -------------------------------------------------------------------
// PointerComparisonIntrinsic
template<llvm::CmpInst::Predicate pred>
Expr * PointerComparisonIntrinsic<pred>::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 2);
  return new CompareExpr(loc, pred, args[0], args[1]);
}

template<>
PointerComparisonIntrinsic<CmpInst::ICMP_EQ>
    PointerComparisonIntrinsic<CmpInst::ICMP_EQ>::instance("infixEQ");

template<>
PointerComparisonIntrinsic<CmpInst::ICMP_NE>
    PointerComparisonIntrinsic<CmpInst::ICMP_NE>::instance("infixNE");

// -------------------------------------------------------------------
// LogicalAndIntrinsic
LogicalAndIntrinsic LogicalAndIntrinsic::instance;

Expr * LogicalAndIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 2);
  Expr * first = args[0];
  Expr * second = args[1];

  enum {
    Unknown,
    True,
    False,
  };

  int constFirst = Unknown;
  int constSecond = Unknown;

  if (ConstantInteger * cint = dyn_cast<ConstantInteger>(first)) {
    constFirst = cint->value() != 0 ? True : False;
  }

  if (ConstantInteger * cint = dyn_cast<ConstantInteger>(second)) {
    constSecond = cint->value() != 0 ? True : False;
  }

  if (constSecond == False || constSecond == False) {
    return ConstantInteger::getConstantBool(loc, false);
  }

  return new BinaryExpr(Expr::And, loc, &BoolType::instance, first, second);
}

// -------------------------------------------------------------------
// LogicalOrIntrinsic
LogicalOrIntrinsic LogicalOrIntrinsic::instance;

Expr * LogicalOrIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 2);
  Expr * first = args[0];
  Expr * second = args[1];

  enum {
    Unknown,
    True,
    False,
  };

  int constFirst = Unknown;
  int constSecond = Unknown;

  if (ConstantInteger * cint = dyn_cast<ConstantInteger>(first)) {
    constFirst = cint->value() != 0 ? True : False;
  }

  if (ConstantInteger * cint = dyn_cast<ConstantInteger>(second)) {
    constSecond = cint->value() != 0 ? True : False;
  }

  if (constSecond == True || constSecond == True) {
    return ConstantInteger::getConstantBool(loc, true);
  }

  return new BinaryExpr(Expr::Or, loc, &BoolType::instance, first, second);
}

// -------------------------------------------------------------------
// ArrayCopyIntrinsic
ArrayCopyIntrinsic ArrayCopyIntrinsic::copyInstance(
    "tart.core.Memory.arrayCopy", llvm::Intrinsic::memcpy);
ArrayCopyIntrinsic ArrayCopyIntrinsic::moveInstance(
    "tart.core.Memory.arrayMove", llvm::Intrinsic::memmove);

Value * ArrayCopyIntrinsic::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  DASSERT(call->argCount() == 5);
  const Expr * dstArray = call->arg(0);
  const Expr * dstOffset = call->arg(1);
  const Expr * srcArray = call->arg(2);
  const Expr * srcOffset = call->arg(3);
  const Expr * count = call->arg(4);

  DASSERT_OBJ(srcArray->type()->isEqual(dstArray->type()), call);
  Type * elemType = cast<NativeArrayType>(srcArray->type())->typeParam(0);
  Value * srcPtr = cg.genLValueAddress(srcArray);
  Value * dstPtr = cg.genLValueAddress(dstArray);
  Value * srcIndex = cg.genExpr(srcOffset);
  Value * dstIndex = cg.genExpr(dstOffset);
  Value * length = cg.genExpr(count);

  Value * elemSize = cg.builder().CreateTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(elemType->irEmbeddedType()),
      length->getType());

  const llvm::Type * types[1];
  types[0] = length->getType();
  Function * intrinsic = llvm::Intrinsic::getDeclaration(cg.irModule(), _id, types, 1);

  Value * idx[2];
  idx[0] = ConstantInt::getSigned(length->getType(), 0);
  idx[1] = dstIndex;

  Value * args[4];
  args[0] = cg.builder().CreatePointerCast(
      cg.builder().CreateInBoundsGEP(dstPtr, &idx[0], &idx[2], "dst"),
      llvm::PointerType::getUnqual(cg.builder().getInt8Ty()));

  idx[1] = srcIndex;
  args[1] = cg.builder().CreatePointerCast(
      cg.builder().CreateInBoundsGEP(srcPtr, &idx[0], &idx[2], "src"),
      llvm::PointerType::getUnqual(cg.builder().getInt8Ty()));

  args[2] = cg.builder().CreateMul(length, elemSize);
  args[3] = cg.getInt32Val(0);

  return cg.builder().CreateCall(intrinsic, &args[0], &args[4]);
}

// -------------------------------------------------------------------
// MathIntrinsic1
template<llvm::Intrinsic::ID id>
inline Value * MathIntrinsic1<id>::generate(CodeGenerator & cg, const FnCallExpr * call) const {
  const Expr * arg = call->arg(0);
  const PrimitiveType * argType = cast<PrimitiveType>(dealias(arg->type()));
  Value * argVal = cg.genExpr(arg);
  const Type * retType = dealias(call->type());

  if (argType->typeId() != TypeId_Float && argType->typeId() != TypeId_Double) {
    diag.fatal(arg->location()) << "Bad intrinsic type.";
    return NULL;
  }

  const llvm::Type * types[1];
  types[1] = argType->irType();
  Function * intrinsic = llvm::Intrinsic::getDeclaration(cg.irModule(), id, types, 1);
  return cg.builder().CreateCall(intrinsic, argVal);
}

template<>
MathIntrinsic1<llvm::Intrinsic::sin>
    MathIntrinsic1<llvm::Intrinsic::sin>::instance("tart.core.Math.sin");

template<>
MathIntrinsic1<llvm::Intrinsic::cos>
    MathIntrinsic1<llvm::Intrinsic::cos>::instance("tart.core.Math.cos");

template<>
MathIntrinsic1<llvm::Intrinsic::sqrt>
    MathIntrinsic1<llvm::Intrinsic::sqrt>::instance("tart.core.Math.sqrt");

// -------------------------------------------------------------------
// FlagsApplyIntrinsic
FlagsApplyIntrinsic FlagsApplyIntrinsic::instance;

Expr * FlagsApplyIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 1);
  ConstantType * ctype = cast<ConstantType>(args[0]);
  EnumType * enumType = cast<EnumType>(ctype->value());
  enumType->setIsFlags(true);
  return args[0];
}

// -------------------------------------------------------------------
// ExternApplyIntrinsic
ExternApplyIntrinsic ExternApplyIntrinsic::instance;

Expr * ExternApplyIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 1);
  ConstantObjectRef * selfObj = cast<ConstantObjectRef>(self);
  const ConstantString * extName = dyn_cast<ConstantString>(selfObj->members()[1]);
  DASSERT_OBJ(extName != NULL, self);

  LValueExpr * lval = cast<LValueExpr>(args[0]);
  lval->value()->addTrait(Defn::Extern);
  lval->value()->setLinkageName(extName->value());
  return self;
}

// -------------------------------------------------------------------
// LinkageNameApplyIntrinsic
LinkageNameApplyIntrinsic LinkageNameApplyIntrinsic::instance;

Expr * LinkageNameApplyIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 1);
  ConstantObjectRef * selfObj = cast<ConstantObjectRef>(self);
  const ConstantString * linkName = cast<ConstantString>(selfObj->members()[1]);
  LValueExpr * lval = cast<LValueExpr>(args[0]);
  lval->value()->setLinkageName(linkName->value());
  return self;
}

// -------------------------------------------------------------------
// EntryPointApplyIntrinsic
EntryPointApplyIntrinsic EntryPointApplyIntrinsic::instance;

Expr * EntryPointApplyIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 1);
  ConstantObjectRef * selfObj = cast<ConstantObjectRef>(self);
  LValueExpr * lval = cast<LValueExpr>(args[0]);
  FunctionDefn * fn = cast<FunctionDefn>(lval->value());
  Module * module = fn->module();
  if (module->entryPoint() != NULL) {
    diag.error(fn) << "@EntryPoint attribute conflicts with earlier entry point: " <<
        module->entryPoint();
  } else {
    module->setEntryPoint(fn);
  }

  return self;
}

// -------------------------------------------------------------------
// AnnexApplyIntrinsic
AnnexApplyIntrinsic AnnexApplyIntrinsic::instance;

Expr * AnnexApplyIntrinsic::eval(const SourceLocation & loc, Expr * self,
    const ExprList & args, Type * expectedReturn) const {
  assert(args.size() == 1);
  ConstantType * ctype = cast<ConstantType>(args[0]);
  Builtins::registerAnnexType(ctype->value());
  return ctype;
}

} // namespace tart
