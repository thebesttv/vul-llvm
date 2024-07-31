#include "base.h"
#include "../DumpPath.h"

const Expr *BaseMatcher::uncast(const Expr *E) {
    if (!E)
        return nullptr;
    while (const auto *CE = dyn_cast<CastExpr>(E)) {
        E = CE->getSubExpr();
    }
    return E;
}

const FunctionDecl *BaseMatcher::getDirectCallee(const Expr *E) {
    if (!E)
        return nullptr;
    E = uncast(E);
    if (const CallExpr *expr = dyn_cast<CallExpr>(E)) {
        return ::getDirectCallee(expr);
    }
    return nullptr;
}

bool BaseMatcher::isPointerType(const Expr *E) {
    if (!E)
        return false;
    auto type = E->getType().getTypePtrOrNull();
    return type && type->isAnyPointerType();
}

std::optional<ordered_json>
BaseMatcher::dumpSource(const SourceRange &range,
                        const std::optional<SourceRange> &varRange) {
    ordered_json loc;
    // something wrong with location
    if (!saveLocationInfo(*Context, range, loc))
        return std::nullopt;
    // source outside current project
    const std::string &file = loc["file"];
    if (!Global.isUnderProject(file))
        return std::nullopt;

    // 导出变量位置
    if (varRange.has_value()) {
        ordered_json varLoc;
        if (!saveLocationInfo(*Context, varRange.value(), varLoc))
            return std::nullopt;
        varLoc.erase("file"); // 肯定是同一个文件
        loc["variable"] = varLoc;
    }
    return loc;
}
