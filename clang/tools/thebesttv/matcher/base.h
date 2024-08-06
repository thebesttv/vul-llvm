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

    std::optional<typename std::set<ordered_json>::iterator>
    saveSuspectedSource(const SourceRange &range,
                        const std::optional<SourceRange> &varRange,
                        std::set<ordered_json> &suspectedSources,
                        int n = 100000) {
        auto loc = dumpSource(range, varRange);
        if (!loc)
            return std::nullopt;
        return reservoirSamplingAddElement(suspectedSources, loc.value(), n);
    }

  public:
    explicit BaseMatcher(ASTContext *Context, int fid)
        : Context(Context), fid(fid) {}
};
