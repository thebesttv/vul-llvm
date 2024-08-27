#pragma once

#include "base.h"

/**
 * 加入以下情况：
 * - p = foo()，其中 foo 通过 input.json 的 "mayMalloc" 指定
 *   - 默认添加 malloc
 * - p = new A()
 */
class ResourceLeakGoodSourceVisitor
    : public RecursiveASTVisitor<ResourceLeakGoodSourceVisitor>,
      public BaseMatcher {
  private:
    bool isMalloc(const Expr *E) {
        if (!E)
            return false;

        // 必须是指针类型
        auto type = E->getType().getTypePtrOrNull();
        if (!type || !type->isPointerType())
            return false;

        E = uncast(E);

        // new A(...)
        if (isa<CXXNewExpr>(E)) {
            return true;
        }

        // foo(...)
        if (const FunctionDecl *calleeDecl = getDirectCallee(E)) {
            auto name = calleeDecl->getQualifiedNameAsString();
            return Global.mayMallocFunctions.count(name);
        }

        return false;
    }

    std::optional<SrcWeakPtr>
    saveSuspectedSource(const SourceRange &range,
                        const std::optional<SourceRange> &varRange) {
        static int index = 0;
        return BaseMatcher::saveSuspectedSource(
            range, varRange, Global.resourceLeakSuspectedSources, index);
    }

    void checkFormPEqMalloc(const SourceRange &range, const Expr *rhs,
                            const std::optional<SourceRange> &varRange) {
        if (!rhs || !isMalloc(rhs))
            return;
        saveSuspectedSource(range, varRange);
    }

  public:
    explicit ResourceLeakGoodSourceVisitor(ASTContext *Context, int fid)
        : BaseMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D) {
        // must be declared within a function
        if (!D->getParentFunctionOrMethod())
            return true;

        checkFormPEqMalloc(D->getSourceRange(), D->getInit(), D->getLocation());

        return true;
    }

    bool VisitBinaryOperator(BinaryOperator *S) {
        if (S->getOpcode() == BO_Assign) {
            if (S->getLHS() && S->getRHS()) {
                checkFormPEqMalloc(S->getSourceRange(), S->getRHS(),
                                   S->getLHS()->getSourceRange());
            }
        }
        return true;
    }
};
