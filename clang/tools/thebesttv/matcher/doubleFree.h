#pragma once

#include "base.h"

/**
 * 加入以下情况：
 * - foo(p)，其中 foo 通过 input.json 的 "mayFree" 指定
 *   - 默认添加 free
 *   - 默认第一个参数是变量
 *     - 如果没有第一个参数，就没有变量
 * - delete p, delete [] p
 *   - 变量是 p
 */
class DoubleFreeGoodSourceVisitor
    : public RecursiveASTVisitor<DoubleFreeGoodSourceVisitor>,
      public BaseMatcher {
  private:
    bool isFree(const Expr *E) {
        if (!E)
            return false;

        E = uncast(E);

        // foo(...)
        if (const FunctionDecl *calleeDecl = getDirectCallee(E)) {
            auto name = calleeDecl->getQualifiedNameAsString();
            return Global.mayFreeFunctions.count(name);
        }

        return false;
    }

    std::optional<SrcWeakPtr>
    saveSuspectedSource(const SourceRange &range,
                        const std::optional<SourceRange> &varRange) {
        static int index = 0;
        return BaseMatcher::saveSuspectedSource(
            range, varRange, Global.doubleFreeSuspectedSources, index);
    }

  public:
    explicit DoubleFreeGoodSourceVisitor(ASTContext *Context, int fid)
        : BaseMatcher(Context, fid) {}

    bool VisitCallExpr(CallExpr *E) {
        if (isFree(E)) {
            std::optional<SourceRange> varRange = std::nullopt;
            if (E->getNumArgs() > 0) {
                if (auto *arg = E->getArg(0)) {
                    varRange = arg->getSourceRange();
                }
            }
            saveSuspectedSource(E->getSourceRange(), varRange);
        }
        return true;
    }

    bool VisitCXXDeleteExpr(CXXDeleteExpr *E) {
        std::optional<SourceRange> varRange = std::nullopt;
        if (auto *arg = E->getArgument()) {
            varRange = arg->getSourceRange();
        }
        saveSuspectedSource(E->getSourceRange(), varRange);
        return true;
    }
};
