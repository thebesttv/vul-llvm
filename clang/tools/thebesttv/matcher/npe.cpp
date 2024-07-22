#include "npe.h"

bool NpeSourceMatcher::isNullPointerConstant(const Expr *expr) {
    if (!expr)
        return false;
    expr = uncast(expr);
    const auto &valueDependence =
        Expr::NullPointerConstantValueDependence::NPC_ValueDependentIsNotNull;
    auto result = expr->isNullPointerConstant(*Context, valueDependence);
    return result != Expr::NullPointerConstantKind::NPCK_NotNull;
}

std::optional<typename std::set<ordered_json>::iterator>
NpeGoodSourceVisitor::saveNpeSuspectedSources(
    const SourceRange &range, const std::optional<SourceRange> &varRange) {
    return saveSuspectedSource(range, varRange, Global.npeSuspectedSources);
}

void NpeSourceMatcher::checkFormPEqNullOrFoo(
    const SourceRange &range, const Expr *rhs,
    const std::optional<SourceRange> &varRange) {
    if (!rhs || !isPointerType(rhs))
        return;

    if (isNullPointerConstant(rhs)) {
        // p = NULL
        handleFormPEqNull(range, varRange);
    } else if (const FunctionDecl *calleeDecl = getDirectCallee(rhs)) {
        // p = foo()
        handleFormPEqFoo(calleeDecl, range, varRange);
    }
}

bool NpeSourceMatcher::VisitVarDecl(VarDecl *D) {
    // 加入 NPE 可疑的 source 中

    // must be declared within a function
    if (!D->getParentFunctionOrMethod())
        return true;

    // D->getLocation() 对应变量位置，会自动转化为 SourceRange
    // 见 https://stackoverflow.com/a/9054913/11938767
    checkFormPEqNullOrFoo(D->getSourceRange(), D->getInit(), D->getLocation());

    return true;
}

bool NpeSourceMatcher::VisitBinaryOperator(BinaryOperator *S) {
    // 加入 NPE 可疑的 source 中

    /**
     * p = NULL / p = foo()
     *
     * S->getOpcode() == BO_Assign 等价于
     *   !(S->isAssignmentOp() && !S->isCompoundAssignmentOp())
     * i.e. is assignment & not compound assignment (e.g. +=, -=, *=)
     */
    if (S->getOpcode() == BO_Assign) {
        if (isPointerType(S)) {
            checkFormPEqNullOrFoo(S->getSourceRange(), S->getRHS(),
                                  S->getLHS()->getSourceRange());
        }
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
        } else if (inFormPNeNull(S->getRHS(), S->getLHS())) {
            handleFormPNeNull(S->getSourceRange(),
                              S->getRHS()->getSourceRange());
        }
    }

    return true;
}

bool NpeSourceMatcher::VisitReturnStmt(ReturnStmt *S) {
    Expr *value = S->getRetValue();
    if (isNullPointerConstant(value)) {
        handleFormReturnNull(S->getSourceRange());
    }
    return true;
}

void NpeGoodSourceVisitor::handleFormPEqNull(
    const SourceRange &range, const std::optional<SourceRange> &varRange) {
    // p = null
    saveNpeSuspectedSources(range, varRange);
}

void NpeGoodSourceVisitor::handleFormPEqFoo(
    const FunctionDecl *calleeDecl, const SourceRange &range,
    const std::optional<SourceRange> &varRange) {
    // p = foo() && foo() = { ...; return NULL; }
    auto it = saveNpeSuspectedSources(range, varRange);
    if (it) {
        // callee 可能还没被处理过，记录 signature，而不是 fid
        std::string callee = getFullSignature(calleeDecl);
        Global.npeSuspectedSourcesItMap[callee].push_back(it.value());
    }
}

void NpeGoodSourceVisitor::handleFormPNeNull(
    const SourceRange &range, const std::optional<SourceRange> &varRange) {
    // p != null
    saveNpeSuspectedSources(range, varRange);
}

void NpeGoodSourceVisitor::handleFormReturnNull(const SourceRange &range) {
    Global.functionReturnsNull[fid] = true;
}
