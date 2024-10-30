#pragma once

#include "utils.h"

SourceRange getProperSourceRange(ASTContext &Context, const SourceRange &range,
                                 bool getExpansionLocation = true);

/**
 * `getExpansionLocation`: 如果为真（默认），则返回宏在代码中展开后的位置；
 * 否则，返回宏定义的位置。这主要是为了可以获取 source 的 variable 信息。
 */
bool saveLocationInfo(ASTContext &Context, const SourceRange &range,
                      ordered_json &j, bool getExpansionLocation = true);
