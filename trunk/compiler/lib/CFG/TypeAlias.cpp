/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/CFG/TypeAlias.h"
#include "tart/Common/Diagnostics.h"

namespace tart {

// -------------------------------------------------------------------
// TypeAlias

TypeAlias::TypeAlias(const TypeRef & val)
  : Type(Alias)
  , value_(val)
{
}

const llvm::Type * TypeAlias::irType() const {
  DASSERT(value_.isDefined());
  return value_.irType();
}

const llvm::Type * TypeAlias::irEmbeddedType() const {
  return value_.irEmbeddedType();
}

const llvm::Type * TypeAlias::irParameterType() const {
  return value_.irParameterType();
}

ConversionRank TypeAlias::convertImpl(const Conversion & conversion) const {
  DASSERT(value_.isDefined());
  return value_.type()->convertImpl(conversion);
}

void TypeAlias::format(FormatStream & out) const {
  DASSERT(value_.isDefined());
  return value_.type()->format(out);
}

void TypeAlias::trace() const {
  value_.trace();
}

} // namespace tart