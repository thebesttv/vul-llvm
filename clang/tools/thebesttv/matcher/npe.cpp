#include "npe.h"
#include "DumpPath.h"

bool NpeSourceMatcher::isNullPointerConstant(const Expr *expr) {
    if (!expr || !isPointerType(expr))
        return false;
    expr = uncast(expr);
    const auto &valueDependence =
        Expr::NullPointerConstantValueDependence::NPC_ValueDependentIsNotNull;
    auto result = expr->isNullPointerConstant(*Context, valueDependence);
    return result != Expr::NullPointerConstantKind::NPCK_NotNull;
}

const Expr *NpeSourceMatcher::getProperVar(const Expr *E) {
    E = uncast(E);
    if (const auto *expr = dyn_cast<MemberExpr>(E)) {
        ordered_json j;
        if (!saveLocationInfo(*Context, expr->getSourceRange(), j, false)) {
            return expr->getBase();
        }
    }
    return E;
}

std::optional<SrcWeakPtr> //
NpeGoodSourceVisitor::saveNpeSuspectedSources(
    const SourceRange &range, const std::optional<SourceRange> &varRange) {
    return saveSuspectedSource(range, varRange, Global.npeSuspectedSources);
}

void NpeGoodSourceVisitor::checkFormPEqNullOrFoo(
    const SourceRange &range, const Expr *rhs,
    const std::optional<SourceRange> &varRange) {
    if (!rhs || !isPointerType(rhs))
        return;

    if (isNullPointerConstant(rhs)) {
        // p = NULL
        saveNpeSuspectedSources(range, varRange);
    } else if (const FunctionDecl *calleeDecl = getDirectCallee(rhs)) {
        // p = foo() && foo() = { ...; return NULL; }
        auto it = saveNpeSuspectedSources(range, varRange);
        if (it) {
            // callee 可能还没被处理过，记录 signature，而不是 fid
            std::string callee = getFullSignature(calleeDecl);
            Global.npeSuspectedSourcesFunMap[callee].push_back(it.value());
        }
    }
}

bool NpeGoodSourceVisitor::VisitVarDecl(VarDecl *D) {
    // 加入 NPE 可疑的 source 中

    // must be declared within a function
    if (!D->getParentFunctionOrMethod())
        return true;

    // D->getLocation() 对应变量位置，会自动转化为 SourceRange
    // 见 https://stackoverflow.com/a/9054913/11938767
    checkFormPEqNullOrFoo(D->getSourceRange(), D->getInit(), D->getLocation());

    return true;
}

bool NpeGoodSourceVisitor::VisitBinaryOperator(BinaryOperator *S) {
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
                                  getProperSourceRange(S->getLHS()));
        }
    } else if (S->getOpcode() == BO_EQ || S->getOpcode() == BO_NE) {
        // 两边形如 p == NULL 或 p != NULL
        auto inFormPNeNull = [this](const Expr *l, const Expr *r) {
            return isPointerType(l) && !isNullPointerConstant(l) &&
                   isNullPointerConstant(r);
        };

        // p != NULL 或 NULL != p
        if (inFormPNeNull(S->getLHS(), S->getRHS())) {
            saveNpeSuspectedSources(S->getSourceRange(),
                                    getProperSourceRange(S->getLHS()));
        } else if (inFormPNeNull(S->getRHS(), S->getLHS())) {
            saveNpeSuspectedSources(S->getSourceRange(),
                                    getProperSourceRange(S->getRHS()));
        }
    }

    return true;
}

bool NpeGoodSourceVisitor::VisitReturnStmt(ReturnStmt *S) {
    Expr *value = S->getRetValue();
    if (isNullPointerConstant(value)) {
        // return null
        Global.returnGraph.addNullFunction(fid);
    } else if (const FunctionDecl *calleeDecl = getDirectCallee(value)) {
        // return foo()
        std::string callee = getFullSignature(calleeDecl);
        Global.returnGraph.addEdge(fid, callee);
    }
    return true;
}
