#pragma once

#include "VarLocResult.h"

class BaseMatcher {
  protected:
    ASTContext *Context;
    int fid; // 当前访问函数的 fid

    /**
     * 递归去除 cast 和括号，包括以下情况：
     * - CastExpr
     * - ParenExpr
     */
    const Expr *uncast(const Expr *expr);
    const FunctionDecl *getDirectCallee(const Expr *expr);
    static bool isPointerType(const Expr *expr);

    /**
     * - `range` 对应语句位置
     * - `varRange` 对应变量位置
     */
    std::optional<ordered_json>
    dumpSource(const SourceRange &range,
               const std::optional<SourceRange> &varRange);

    std::optional<SrcWeakPtr>
    saveSuspectedSource(const SourceRange &range,
                        const std::optional<SourceRange> &varRange,
                        SrcSet &suspectedSources, //
                        int n = 100000) {
        auto loc = dumpSource(range, varRange);
        if (!loc)
            return std::nullopt;
        SrcPtr p = std::make_shared<ordered_json>(loc.value());
        if (reservoirSamplingAddElement(suspectedSources, p, n)) {
            return p; // 插入成功，则返回对应的 weak_ptr
        }
        return std::nullopt;
    }

  public:
    explicit BaseMatcher(ASTContext *Context, int fid)
        : Context(Context), fid(fid) {}
};
