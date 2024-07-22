#pragma once

#include "VarLocResult.h"

class BaseMatcher {
  protected:
    ASTContext *Context;
    int fid; // 当前访问函数的 fid

    // 递归查找 CastExpr，直到找到非 CastExpr 为止
    const Expr *uncast(const Expr *expr);
    const FunctionDecl *getDirectCallee(const Expr *expr);
    static bool isPointerType(const Expr *expr);

    /**
     * - `range` 对应语句位置
     * - `varRange` 对应变量位置
     */
    std::optional<ordered_json>
    dumpNpeSource(const SourceRange &range,
                  const std::optional<SourceRange> &varRange);

  public:
    explicit BaseMatcher(ASTContext *Context, int fid)
        : Context(Context), fid(fid) {}
};
