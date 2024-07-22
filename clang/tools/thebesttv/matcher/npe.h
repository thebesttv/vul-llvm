#pragma once

#include "VarLocResult.h"

class NpeSourceMatcher {
  protected:
    ASTContext *Context;
    int fid; // 当前访问函数的 fid

    static bool isPointerType(const Expr *E);
    bool isNullPointerConstant(const Expr *expr);
    const FunctionDecl *getDirectCallee(const Expr *expr);

    /**
     * - `range` 对应语句位置
     * - `varRange` 对应变量位置
     */
    std::optional<ordered_json>
    dumpNpeSource(const SourceRange &range,
                  const std::optional<SourceRange> &varRange);

    virtual void
    handleFormPEqNull(const SourceRange &range,
                      const std::optional<SourceRange> &varRange) = 0;
    virtual void
    handleFormPEqFoo(const FunctionDecl *calleeDecl, const SourceRange &range,
                     const std::optional<SourceRange> &varRange) = 0;
    virtual void
    handleFormPNeNull(const SourceRange &range,
                      const std::optional<SourceRange> &varRange) = 0;
    virtual void handleFormReturnNull(const SourceRange &range) = 0;

    void checkFormPEqNullOrFoo(const SourceRange &range, const Expr *rhs,
                               const std::optional<SourceRange> &varRange);

  public:
    explicit NpeSourceMatcher(ASTContext *Context, int fid)
        : Context(Context), fid(fid) {}

    bool VisitVarDecl(VarDecl *D);
    bool VisitBinaryOperator(BinaryOperator *S);
    bool VisitReturnStmt(ReturnStmt *S);
};

/**
 * source 的保存策略：
 * 1. p = NULL
 * 2. p = foo() && foo() = { ...; return NULL; }
 *    其中第二个判断（foo() 中包含 return NULL）在之后才会做。
 *    目前放到 main() 里做，在 generateICFG() 之后
 * 3. p = foo() && 在 input.json 中指定 foo 可能返回 NULL
 *    同样，判断在 main() 里做
 * 4. p != NULL
 */
class NpeGoodSourceVisitor : public RecursiveASTVisitor<NpeGoodSourceVisitor>,
                             public NpeSourceMatcher {

    std::optional<typename std::set<ordered_json>::iterator>
    saveNpeSuspectedSources(const SourceRange &range,
                            const std::optional<SourceRange> &varRange);

  protected:
    void handleFormPEqNull(const SourceRange &range,
                           const std::optional<SourceRange> &varRange) override;
    void handleFormPEqFoo(const FunctionDecl *calleeDecl,
                          const SourceRange &range,
                          const std::optional<SourceRange> &varRange) override;
    void handleFormPNeNull(const SourceRange &range,
                           const std::optional<SourceRange> &varRange) override;
    void handleFormReturnNull(const SourceRange &range) override;

  public:
    explicit NpeGoodSourceVisitor(ASTContext *Context, int fid)
        : NpeSourceMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D) { return NpeSourceMatcher::VisitVarDecl(D); }
    bool VisitBinaryOperator(BinaryOperator *S) {
        return NpeSourceMatcher::VisitBinaryOperator(S);
    }
    bool VisitReturnStmt(ReturnStmt *S) {
        return NpeSourceMatcher::VisitReturnStmt(S);
    }
};

class NpeBugSourceVisitor : public RecursiveASTVisitor<NpeBugSourceVisitor>,
                            public NpeSourceMatcher {

    int sid;
    std::vector<std::pair<int, ordered_json>> npeSources, nullConstants;

    enum {
        // return null, p = xxx, p != null
        // 额外匹配foo()，因为 Infer 可能报告 ld = foo()，然后再进入 foo()
        RETURN_NULL_OR_NPE_GOOD_SOURCE,
        // nullptr, NULL, 0
        NULL_CONSTANT
    } currentStage;
    bool isMatch = false;
    bool dumpJson = false;

    ordered_json dumpedJson;

    void setMatchAndMaybeDumpJson(const SourceRange &range,
                                  const std::optional<SourceRange> &varRange) {
        isMatch = true;
        if (dumpJson) {
            auto json = dumpNpeSource(range, varRange);
            if (json) {
                dumpedJson = json.value();
            }
        }
    }

  protected:
    void
    handleFormPEqNull(const SourceRange &range,
                      const std::optional<SourceRange> &varRange) override {
        setMatchAndMaybeDumpJson(range, varRange);
    }

    void handleFormPEqFoo(const FunctionDecl *calleeDecl,
                          const SourceRange &range,
                          const std::optional<SourceRange> &varRange) override {
        setMatchAndMaybeDumpJson(range, varRange);
    }

    void
    handleFormPNeNull(const SourceRange &range,
                      const std::optional<SourceRange> &varRange) override {
        setMatchAndMaybeDumpJson(range, varRange);
    }

    void handleFormReturnNull(const SourceRange &range) override {
        setMatchAndMaybeDumpJson(range, std::nullopt);
    }

    std::vector<VarLocResult>
    traverseAndMatch(const std::vector<VarLocResult> original,
                     const decltype(FunctionInfo::G) &G) {
        std::vector<VarLocResult> result;
        for (const auto &varLoc : original) {
            const Stmt *stmt = G[varLoc.bid][varLoc.sid];
            isMatch = false;
            this->TraverseStmt(const_cast<Stmt *>(stmt));
            if (isMatch) {
                result.push_back(varLoc);
            }
        }
        return result;
    }

  public:
    explicit NpeBugSourceVisitor(ASTContext *Context, int fid)
        : NpeSourceMatcher(Context, fid) {}

    bool VisitVarDecl(VarDecl *D) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;

        // must be declared within a function
        if (!D->getParentFunctionOrMethod())
            return true;

        if (D->hasInit()) {
            setMatchAndMaybeDumpJson(D->getSourceRange(), D->getLocation());
            return false;
        }

        return true;
    }
    bool VisitBinaryOperator(BinaryOperator *S) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;

        if (S->getOpcode() == BO_Assign) {
            std::optional<SourceRange> varRange = std::nullopt;
            if (S->getLHS()) {
                varRange = S->getLHS()->getSourceRange();
            }
            setMatchAndMaybeDumpJson(S->getSourceRange(), varRange);
            return false;
        } else if (S->getOpcode() == BO_NE) {
            // 两边形如 p != NULL
            auto inFormPNeNull = [this](const Expr *l, const Expr *r) {
                return isPointerType(l) && !isNullPointerConstant(l) &&
                       isNullPointerConstant(r);
            };

            // p != NULL 或 NULL != p
            if (inFormPNeNull(S->getLHS(), S->getRHS())) {
                handleFormPNeNull(S->getSourceRange(),
                                  S->getLHS()->getSourceRange());
                return false;
            } else if (inFormPNeNull(S->getRHS(), S->getLHS())) {
                handleFormPNeNull(S->getSourceRange(),
                                  S->getRHS()->getSourceRange());
                return false;
            }
        }

        return true;
    }
    bool VisitReturnStmt(ReturnStmt *S) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;
        Expr *value = S->getRetValue();
        if (isNullPointerConstant(value)) {
            setMatchAndMaybeDumpJson(S->getSourceRange(), std::nullopt);
            return false;
        }
        return true;
    }

    bool VisitCallExpr(CallExpr *expr) {
        if (currentStage != RETURN_NULL_OR_NPE_GOOD_SOURCE)
            return true;
        setMatchAndMaybeDumpJson(expr->getSourceRange(), std::nullopt);
        return false;
    }

    bool VisitExpr(Expr *E) {
        if (currentStage != NULL_CONSTANT)
            return true;
        if (isNullPointerConstant(E)) {
            setMatchAndMaybeDumpJson(E->getSourceRange(), std::nullopt);
            return false;
        }
        return true;
    }

    std::vector<VarLocResult>
    transform(const std::vector<VarLocResult> original,
              const decltype(FunctionInfo::G) &G) {
        dumpJson = false;

        logger.info("In NpeBugSourceVisitor::transform");

        currentStage = RETURN_NULL_OR_NPE_GOOD_SOURCE;
        std::vector<VarLocResult> result = traverseAndMatch(original, G);
        logger.info("> stage1: {}", fmt::join(result, ", "));

        if (result.empty()) {
            currentStage = NULL_CONSTANT;
            result = traverseAndMatch(original, G);
            logger.info("> stage2: {}", fmt::join(result, ", "));
        }

        // return result.empty() ? original : result;
        return result;
    }

    void dump(const Stmt *stmt, ordered_json &j) {
        dumpJson = true;
        dumpedJson.clear();
        currentStage = RETURN_NULL_OR_NPE_GOOD_SOURCE;
        isMatch = false;
        if (const DeclStmt *declStmt = dyn_cast<DeclStmt>(stmt)) {
            TraverseDeclStmt(const_cast<DeclStmt *>(declStmt));
        } else if (const BinaryOperator *binOp =
                       dyn_cast<BinaryOperator>(stmt)) {
            VisitBinaryOperator(const_cast<BinaryOperator *>(binOp));
        }

        if (isMatch && dumpedJson.contains("variable")) {
            j["variable"] = dumpedJson["variable"];
        }
    }
};
