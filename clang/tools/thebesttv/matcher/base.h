#pragma once

#include "VarLocResult.h"

class BaseMatcher {
  protected:
    ASTContext *Context;
    int fid; // 当前访问函数的 fid

    const FunctionDecl *getDirectCallee(const Expr *expr);

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
