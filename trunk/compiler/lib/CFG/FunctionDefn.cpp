/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/Common/Diagnostics.h"
#include "tart/CFG/TypeDefn.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/FunctionType.h"
#include "tart/CFG/PrimitiveType.h"
#include "tart/CFG/NativeType.h"
#include "tart/CFG/Block.h"
#include "tart/Objects/Intrinsic.h"
#include "tart/Objects/Builtins.h"

#include <ostream>
#include <iostream>

namespace tart {

namespace {
  bool isOverloadable(Defn::DefnType dt) {
    return dt == Defn::Function || dt == Defn::Macro;
  }
}

// -------------------------------------------------------------------
// ParameterDefn

void ParameterDefn::trace() const {
  VariableDefn::trace();
  //type_.trace();
  internalType_.trace();
  //safeMark(defaultValue_);
}

void ParameterDefn::format(FormatStream & out) const {
  // Parameters are allowed to be unnamed (for function type declarations.)
  if (!qname_.empty() && out.getShowQualifiedName()) {
    out << qname_;
  } else if (name_ != NULL) {
    out << name_;
  }

  if (out.getShowType() && type().isDefined()) {
    out << ":" << type();
  }

  if (out.isVerbose() && initValue()) {
    out << "=" << initValue();
  }
}

// -------------------------------------------------------------------
// FunctionDefn
TypeRef FunctionDefn::type() const {
  return type_;
}

const TypeRef & FunctionDefn::returnType() const {
  return type_->returnType();
}

TypeRef & FunctionDefn::returnType() {
  return type_->returnType();
}

bool FunctionDefn::hasBody() const {
  return (ast_ != NULL && functionDecl()->body() != NULL) || !blocks_.empty();
}

Expr * FunctionDefn::eval(const SourceLocation & loc, Expr * self, const ExprList & args) const {
  if (intrinsic_ != NULL) {
    return intrinsic_->eval(loc, self, args, NULL);
  }

  return NULL;
}

void FunctionDefn::trace() const {
  ValueDefn::trace();
  parameterScope_.trace();
  safeMark(type_);
  markList(blocks_.begin(), blocks_.end());
  markList(localScopes_.begin(), localScopes_.end());
}

bool FunctionDefn::isOverrideOf(const FunctionDefn * baseFunction) {
  if (this == baseFunction) {
    return true;
  }

  for (FunctionSet::iterator it = overriddenMethods_.begin(); it != overriddenMethods_.end();
      ++it) {
    if ((*it)->isOverrideOf(baseFunction)) {
      return true;
    }
  }

  return false;
}

const std::string & FunctionDefn::linkageName() const {
  if (lnkName.empty()) {
    Defn::linkageName();

    if (!isExtern()) {
      if (!type_->params().empty()) {
        lnkName.append("(");
        for (ParameterList::iterator it = type_->params().begin(); it != type_->params().end();
            ++it) {
          if (it != type_->params().begin()) {
            lnkName.append(",");
          }
          typeLinkageName(lnkName, (*it)->type());
          if ((*it)->getFlag(ParameterDefn::Variadic)) {
            lnkName.append("...");
          }
        }
        lnkName.append(")");
      }

      if (type_->returnType().isNonVoidType()) {
        lnkName.append("->");
        typeLinkageName(lnkName, type_->returnType());
      }
    }
  }

  return lnkName;
}

void FunctionDefn::format(FormatStream & out) const {
  if (out.isVerbose()) {
    out << "def ";
  }

  if (out.getShowQualifiedName() && !qname_.empty()) {
    if (out.getShowType() && parentDefn()) {
      out << parentDefn() << "." << name_;
    } else {
      out << qname_;
    }
  } else {
    out << name_;
  }

  if (out.getShowType() && type_ != NULL) {
    out << "(";
    formatParameterList(out, type_->params());
    out << ")";
    if (type_->returnType().isNonVoidType()) {
      out << " -> " << type_->returnType();
    }
  }
}

void FunctionDefn::dumpBlocks() {
  FormatStream stream(std::cout);

  // Number all blocks.
  int index = 0;
  for (BlockList::iterator bi = blocks_.begin(); bi != blocks_.end(); ++bi) {
    (*bi)->setIndex(++index);
  }

  // Print out blocks.
  stream << "def " << this << ":\n";
  for (BlockList::iterator bi = blocks_.begin(); bi != blocks_.end(); ++bi) {
    Block * blk = *bi;
    if (blk->unwindTarget()) {
      stream << "  " << blk << " (unwind " << blk->unwindTarget() << "):\n";
    } else {
      stream << "  " << blk << ":\n";
    }
    for (ExprList::iterator ei = blk->exprs().begin(); ei != blk->exprs().end(); ++ei) {
      stream << "    " << *ei << "\n";
    }

    switch (blk->terminator()) {
      case BlockTerm_None:
        stream << "    **no terminator**\n";
        break;

      case BlockTerm_Branch:
        stream << "    br " << blk->succs().front() << "\n";
        break;

      case BlockTerm_Conditional:
        stream << "    if (" << blk->termValue() << ") br " << blk->succs()[0] <<
            " else br " << blk->succs()[1] << "\n";
        break;

      case BlockTerm_Return:
        if (blk->termValue() != NULL) {
          stream << "    return " << blk->termValue() << "\n";
        } else {
          stream << "    return\n";
        }
        break;

      case BlockTerm_Switch: {
        break;
      }

      case BlockTerm_Throw:
        if (blk->termValue() != NULL) {
          stream << "    throw " << blk->termValue() << "\n";
        } else {
          stream << "    throw\n";
        }
        break;

      case BlockTerm_ResumeUnwind:
        stream << "    resume unwind\n";
        break;

      case BlockTerm_LocalReturn:
        stream << "    local return\n";
        break;

      case BlockTerm_Catch: {
        size_t numSelectors = blk->termExprs().size() - 1;
        for (size_t i = 0; i < numSelectors; ++i) {
          Expr * catchExpr = blk->termExprs()[i + 1];
          Block * catchBody = blk->succs()[i];
          if (catchExpr) {
            stream << "    catch " << catchExpr << ": " << catchBody << "\n";
          } else {
            stream << "    catch *: " << catchBody << "\n";
          }
        }

        break;
      }
    }
  }
  stream << "\n";
}

} // namespace tart
