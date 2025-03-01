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
    static int index = 0;
    return saveSuspectedSource(range, varRange, Global.npeSuspectedSources,
                               index);
}

void NpeGoodSourceVisitor::saveNpeSuspectedSourcesWithCallee(
    const SourceRange &range, const std::optional<SourceRange> &varRange,
    const FunctionDecl *calleeDecl) {
    auto it = saveNpeSuspectedSources(range, varRange);
    if (it) {
        // callee 可能还没被处理过，记录 signature，而不是 fid
        std::string callee = getFullSignature(calleeDecl);
        Global.npeSuspectedSourcesFunMap[callee].push_back(it.value());
    }
}

void NpeGoodSourceVisitor::saveNpeSuspectedSourcesWithCallee(
    const Stmt *S, const FunctionDecl *calleeDecl) {
    saveNpeSuspectedSourcesWithCallee(
        S->getSourceRange(), calleeDecl->getNameInfo().getSourceRange(),
        calleeDecl);
}

/**
 * 对于形如 p = NULL 或 p = foo() 的情况，加入 NPE 可疑的 source 中。
 * 来源的语句可以是 VarDecl，或 BinaryOperator 中的赋值语句。
 *
 * @param range 表达式的位置
 * @param rhs RHS，根据它是 NULL 还是 foo() 行为不同
 * @param varRange LHS变量的位置，用于标记变量名
 */
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
        saveNpeSuspectedSourcesWithCallee(range, varRange, calleeDecl);
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

bool NpeGoodSourceVisitor::VisitUnaryOperator(UnaryOperator *S) {
    if (S->getOpcode() == UO_Deref) {
        auto *E = S->getSubExpr();
        if (isPointerType(E)) {
            // *E
            if (const FunctionDecl *calleeDecl = getDirectCallee(E)) {
                // *foo()
                saveNpeSuspectedSourcesWithCallee(S, calleeDecl);
            }
        }
    }

    return true;
}

bool NpeGoodSourceVisitor::VisitArraySubscriptExpr(ArraySubscriptExpr *S) {
    auto *E = S->getBase();
    if (isPointerType(E)) {
        // E[i]
        if (const FunctionDecl *calleeDecl = getDirectCallee(E)) {
            // foo()[i]
            saveNpeSuspectedSourcesWithCallee(S, calleeDecl);
        }
    }

    return true;
}

bool NpeGoodSourceVisitor::VisitMemberExpr(MemberExpr *S) {
    auto *E = S->getBase();
    if (isPointerType(E)) {
        // E->x
        if (const FunctionDecl *calleeDecl = getDirectCallee(E)) {
            // foo()->x
            saveNpeSuspectedSourcesWithCallee(S, calleeDecl);
        }
    }

    return true;
}

bool NpeGoodSourceVisitor::VisitCallExpr(CallExpr *expr) {
    for (int i = 0; i < expr->getNumArgs(); i++) {
        auto arg = uncast(expr->getArg(i));
        if (const FunctionDecl *calleeDecl = getDirectCallee(arg)) {
            // foo() 作为函数入参
            saveNpeSuspectedSourcesWithCallee(arg, calleeDecl);
        } else if (isNullPointerConstant(arg)) {
            // nullptr 作为函数入参
            saveNpeSuspectedSources(arg->getSourceRange(), std::nullopt);
        }
    }

    return true;
}
