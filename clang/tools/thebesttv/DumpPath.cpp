#include "DumpPath.h"

bool saveLocationInfo(ASTContext &Context, const SourceRange &range,
                      ordered_json &j, bool getExpansionLocation) {
    bool allGood = true;
    SourceManager &SM = Context.getSourceManager();

    SourceLocation b = range.getBegin();
    if (b.isMacroID()) {
        b = getExpansionLocation ? SM.getExpansionLoc(b) : SM.getSpellingLoc(b);
    }
    auto bLoc = Location::fromSourceLocation(Context, b);
    if (bLoc) {
        j["file"] = bLoc->file;
        j["beginLine"] = bLoc->line;
        j["beginColumn"] = bLoc->column;
    } else {
        j["file"] = "!!! begin loc invalid !!!";
        j["beginLine"] = -1;
        j["beginColumn"] = -1;
        allGood = false;
    }

    /**
     * SourceRange中，endLoc是最后一个token的起始位置，所以需要找到这个token的结束位置
     * See:
     * https://stackoverflow.com/a/11154162/11938767
     * https://discourse.llvm.org/t/problem-with-retrieving-the-binaryoperator-rhs-end-location/51897
     * https://clang.llvm.org/docs/InternalsManual.html#sourcerange-and-charsourcerange
     */
    SourceLocation _e = range.getEnd();
    if (_e.isMacroID()) {
        // _e = SM.getExpansionLoc(_e);
        _e = getExpansionLocation ? getEndOfMacroExpansion(_e, Context)
                                  : SM.getSpellingLoc(_e);
    }
    SourceLocation e =
        Lexer::getLocForEndOfToken(_e, 0, SM, Context.getLangOpts());
    if (e.isInvalid())
        e = _e;
    auto eLoc = Location::fromSourceLocation(Context, e);

    if (eLoc) {
        j["endLine"] = eLoc->line;
        j["endColumn"] = eLoc->column;
        if (bLoc && bLoc->file != eLoc->file) {
            /**
             * 由于宏，begin & end 可能在不同文件中
             *
             * 例如，对于以下宏定义
             *   #define setToNull(p) p->data = nullptr
             * 在使用时，有
             *   setToNull(o);
             * 那么 begin 是 o，end 是 data，两个可能在不同文件中。
             */
            j["endColumnFile"] = eLoc->file;
            allGood = false;
            // 直接返回，没必要继续了，用不了这个 dump
            return allGood;
        }
    } else {
        j["endLine"] = -1;
        j["endColumn"] = -1;
        allGood = false;
    }

    std::string content;
    if (b.isValid() && e.isValid()) {
        const char *cb = SM.getCharacterData(b);
        const char *ce = SM.getCharacterData(e);
        auto length = ce - cb;
        if (length < 0) {
            content = "!!! length < 0 !!!";
            allGood = false;
        } else {
            if (length > 80) {
                content = std::string(cb, 80);
                content += " ...";
            } else {
                content = std::string(cb, length);
            }
        }
    }
    j["content"] = content;

    return allGood;
}

/**
 * 以下是没用到的
 */

std::string getSourceCode(SourceManager &SM, const SourceRange &range) {
    const auto &_b = range.getBegin();
    const auto &_e = range.getEnd();
    const char *b = SM.getCharacterData(_b);
    const char *e = SM.getCharacterData(_e);

    return std::string(b, e - b);
}
