#ifndef UTILS_H
#define UTILS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <queue>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include "ICFG.h"

#include "lib/indicators.hpp"
#include "lib/json.hpp"

// doesn't seem useful
// #include "spdlog/fmt/ostr.h"
#include "spdlog/spdlog.h"
#include <fmt/printf.h>
#include <fmt/ranges.h>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

using namespace clang;
using namespace clang::tooling;
namespace fs = std::filesystem;

struct FunctionInfo;
typedef std::map<std::string, std::set<std::unique_ptr<FunctionInfo>>> fif;

/*****************************************************************
 * Global Variables
 *****************************************************************/

struct GlobalStat;
extern GlobalStat Global;

extern spdlog::logger &logger;

/*****************************************************************
 * fmt::formatter for std::filesystem::path
 *****************************************************************/

template <>
struct fmt::formatter<std::filesystem::path> : fmt::formatter<std::string> {
    auto format(const std::filesystem::path &p,
                format_context &ctx) const -> decltype(ctx.out()) {
        return formatter<std::string>::format(p.string(), ctx);
    }
};

template <> struct fmt::formatter<json> : fmt::formatter<std::string> {
    auto format(const json &j,
                format_context &ctx) const -> decltype(ctx.out()) {
        return formatter<std::string>::format(
            j.dump(4, ' ', false, json::error_handler_t::replace), ctx);
    }
};

template <> struct fmt::formatter<ordered_json> : fmt::formatter<std::string> {
    auto format(const ordered_json &j,
                format_context &ctx) const -> decltype(ctx.out()) {
        return formatter<std::string>::format(
            j.dump(4, ' ', false, json::error_handler_t::replace), ctx);
    }
};

/*****************************************************************
 * Utility functions
 *****************************************************************/

void requireTrue(bool condition, std::string message = "");

/**
 * 获取函数完整签名。主要用于 call graph 构造
 *
 * 包括 namespace、函数名、参数类型
 */
std::string getFullSignature(const FunctionDecl *D);

/**
 * 从完整签名中获取函数名
 *
 * 例如：`foo`, `A::foo`
 */
std::string getNameFromFullSignature(const std::string &fullSignature);

/**
 * 如果 `D` 是模板函数的 specialization，那么返回原始的模板函数。
 * 否则返回 `D`。
 */
const FunctionDecl *getPossibleOriginalTemplate(const FunctionDecl *D);

/**
 * 如果 callee 是模板函数的 specialization，那么返回原始的模板函数。
 * 否则返回 callee。
 */
const FunctionDecl *getDirectCallee(const CallExpr *E);

void dumpSourceLocation(const std::string &msg, const ASTContext &Context,
                        const SourceLocation &loc);

void dumpStmt(const ASTContext &Context, const Stmt *S);

struct Location {
    std::string file; // absolute path
    int line;
    int column;

    Location() : Location("", -1, -1) {}

    Location(const json &j)
        : file(j["file"]), line(j["line"]), column(j["column"]) {}

    Location(const std::string &file, int line, int column)
        : file(file), line(line), column(column) {}

    bool operator==(const Location &other) const {
        return file == other.file && line == other.line &&
               column == other.column;
    }

    static std::unique_ptr<Location>
    fromSourceLocation(const ASTContext &Context, SourceLocation loc);
};

struct NamedLocation : public Location {
    std::string name;

    NamedLocation() : NamedLocation("", -1, -1, "") {}

    NamedLocation(const std::string &file, int line, int column,
                  const std::string &name)
        : Location(file, line, column), name(name) {}

    NamedLocation(const Location &loc, const std::string &name)
        : Location(loc), name(name) {}

    bool operator==(const NamedLocation &other) const {
        return Location::operator==(other) && name == other.name;
    }
};

// 用于维护 xxx-good-source 的一些类型
using SrcPtr = std::shared_ptr<ordered_json>; // 集合元素
using SrcWeakPtr = std::weak_ptr<ordered_json>; // 记录集合元素，用于删除
struct SrcPtrCompare { // 集合的比较器，比较指针内容，而不是指针本身
    bool operator()(const SrcPtr &lhs, const SrcPtr &rhs) const {
        // 集合需要确保指针解引用一定是成功的
        return *lhs < *rhs;
    }
};
// 用于存放 SuspectedSources 的容器。因为中途需要频繁增删，改用数组
using SrcSet = std::vector<SrcPtr>;

struct GlobalStat {
    std::unique_ptr<CompilationDatabase> cb;
    std::set<std::string> allFiles; // cc.json 中的所有文件，不包括头文件

    // 输入 input.json 的绝对路径
    std::string inputJsonPath;
    // 项目所在绝对目录。所有不在这个目录下的函数都会被排除
    std::string projectDirectory;

    bool isUnderProject(const std::string &file) {
        return file.compare(0, projectDirectory.size(), projectDirectory) == 0;
    }

    std::map<std::string, std::set<std::string>> callGraph;

    int functionCnt;
    std::map<std::string, std::map<std::string, int>> fileAndIdOfFunction;
    std::vector<NamedLocation> functionLocations;
    std::vector<Location> functionEndLocations;

    /**
     * 获取函数的 fid
     *
     * 目前的mapping是：signature -> {file -> fid}
     * 其中 file 是函数定义所在的文件名，这是为了防止同名函数的冲突，
     * 例如不同文件中的 main() 函数。
     *
     * 如果不提供函数的定义位置，就会选择第一个找到的函数。
     */
    int getIdOfFunction(const std::string &signature,
                        const std::string &file = "") {
        auto _it = fileAndIdOfFunction.find(signature);
        if (_it == fileAndIdOfFunction.end()) {
            return -1;
        }
        // 获取 signature 对应的 file -> fid 的 mapping
        std::map<std::string, int> &fidOfFile = _it->second;
        requireTrue(fidOfFile.size() > 0);
        if (file.empty()) {
            // 默认用第一个找到的函数
            auto it = fidOfFile.begin();
            if (fidOfFile.size() > 1) {
                const auto &sourceFile = it->first;
                auto fid = it->second;
                logger.warn("Function {} is defined in multiple files and no "
                            "specific one is chosen!",
                            signature);
                logger.warn("  Using the one in {} with fid {}", sourceFile,
                            fid);
            }
            return it->second;
        } else {
            auto it = fidOfFile.find(file);
            if (it == fidOfFile.end()) {
                logger.error("No record of function {} in file {}!", signature,
                             file);
                return -1;
            }
            return it->second;
        }
    }
    /**
     * 返回 `fid` 对应函数的 signature
     */
    const std::string &getSignatureOfFunction(int fid) {
        requireTrue(fid >= 0 && fid < functionLocations.size(), "Invalid fid");
        return functionLocations[fid].name;
    }

    ICFG icfg;

    std::string clangPath, clangppPath;

    /**
     * 返回图，类似 Call Graph，但只有 return foo() 算作函数调用。
     */
    class ReturnGraph {
      private:
        // 由于 G 中可能包含外部函数，所以用 signature 作为 key，而不是 fid
        std::map<std::string, std::set<std::string>> G;
        // 被传播到的函数都是可能返回 null 的
        std::map<std::string, bool> visited;
        // 把初始可能返回 null 的函数加入队列，然后在图上传播
        std::queue<std::string> q;

        /**
         * 处理 input.json 中指定的可能返回 NULL 的函数
         *
         * 遍历 G 中有出度的函数（即 return foo() 中的 foo），
         * 如果函数名在 input.json 的 mayNull 中指定，就加入队列。
         */
        void
        processMayNullFunctions(const std::set<std::string> &mayNullFunctions) {
            for (const auto &it : G) {
                const auto &signature = it.first;
                const auto &name = getNameFromFullSignature(signature);
                if (mayNullFunctions.find(name) != mayNullFunctions.end()) {
                    logger.info("  MayNull from input.json: {}", signature);
                    q.push(signature);
                }
            }
        }

      public:
        /**
         * `fid` 对应的函数在 return 时调用了 `callee`
         *
         * 添加从 `callee` 到 `fid`(签名) 的反向边
         */
        void addEdge(int fid, const std::string &callee) {
            G[callee].insert(Global.getSignatureOfFunction(fid));
        }

        /**
         * `fid` 对应的函数直接返回 NULL。有两种可能：
         * 1. 函数体中包含了 return null 语句
         * 2. 在 input.json 的 mayNull 中指定了该函数可能返回 NULL
         */
        void addNullFunction(int fid) {
            const auto &signature = Global.getSignatureOfFunction(fid);
            logger.trace("  Direct mayNull: {} (F{})", signature, fid);
            q.push(signature);
        }

        /**
         * 根据调用图计算函数是否可能返回 NULL
         */
        void propagate(const std::set<std::string> &mayNullFunctions) {
            logger.info("Propagating mayNull functions ...");
            processMayNullFunctions(mayNullFunctions);

            visited.clear();
            while (!q.empty()) {
                // 不能引用队头，因为马上就 pop 了
                const auto u = q.front();
                q.pop();

                if (visited[u])
                    continue;

                visited[u] = true;
                logger.trace("  MayNull from propagation: {}", u);

                for (const auto &v : G[u]) {
                    q.push(v);
                    logger.trace("    Add: {}", v);
                }
            }
        }

        /**
         * 判断函数是否可能返回 null
         */
        bool mayNull(const std::string &signature) {
            return visited[signature];
        }
    };

    SrcSet npeSuspectedSources; // 每个元素是可疑的 source
    /**
     * 对于加入 npeSuspectedSources 的 p = foo()，
     * 把 foo() map 到集合中的指针对应的 weak_ptr 上，用于之后的删除。
     *
     * 重点：使用 weak_ptr，保证 reservoirSamplingAddElement() 删除了
     * 集合中某个元素后，指针对应内存可以被及时释放，而不是因为这里的引用而留在内存中。
     */
    std::map<std::string, std::vector<SrcWeakPtr>> npeSuspectedSourcesFunMap;
    // 通过返回图计算函数是否可能返回 null
    ReturnGraph returnGraph;

    SrcSet resourceLeakSuspectedSources;      // 每个元素是可疑的 source
    std::set<std::string> mayMallocFunctions; // 类似 malloc 的函数

    int ASTPoolSize;
    int callDepth;
    int dfsTick;
    int dfsTimeout;
    bool keepAST;
    bool noNpeGoodSource;
    bool noNodes;
    bool debug;
};

SourceLocation getEndOfMacroExpansion(SourceLocation loc, ASTContext &Context);

/**
 * 打印指定范围内的源码
 */
void printSourceWithinRange(ASTContext &Context, SourceRange range);

/**
 * 判断两个 json 在给定的 `fields` 中，是否完全相同。
 *
 * 注意，即使两个 json 都没有某个 field `f`，也会返回 false。
 */
bool allFieldsMatch(const json &x, const json &y,
                    const std::set<std::string> &fields);

/**
 * 判断路径是否对应存在的目录
 *
 * 来自 https://stackoverflow.com/q/18100097/11938767
 */
bool dirExists(const std::string &path);

/**
 * 判断文件是否存在（并且只是普通文件，不是链接、目录等）
 */
bool fileExists(const std::string &path);

/**
 * 在指定目录运行程序，并返回程序的返回值
 */
int run_program(const std::vector<std::string> &args, const std::string &pwd);

/**
 * 设置用于生成 AST 的 clang & clang++ 编译器路径
 */
void setClangPath(const char *argv0);

/**
 * 生成一个在 [a, b] 范围内的均匀分布的随机数
 */
int randomInt(int a, int b);

/**
 * 水池采样。
 * 判断是否要将元素 element 加入集合 reservoir。
 * element 是当前第 i 个元素，k 是采样大小。
 *
 * 返回是否加入了集合。如果集合中已有该元素，返回 false。
 *
 * 算法：Simple: Algorithm R
 * https://en.wikipedia.org/wiki/Reservoir_sampling
 */
bool reservoirSamplingAddElement(int i, const SrcPtr &element, //
                                 int k, SrcSet &reservoir);

class ProgressBar {
  private:
    const int totalSize;
    indicators::BlockProgressBar bar;

  public:
    ProgressBar(std::string msg, int totalSize, int barWidth = 60)
        : totalSize(totalSize), //
          bar{
              indicators::option::BarWidth{barWidth},
              indicators::option::Start{"["},
              indicators::option::End{"]"},
              indicators::option::ShowElapsedTime{true},
              indicators::option::ShowRemainingTime{true},
              indicators::option::MaxProgress{totalSize},
              indicators::option::PrefixText{msg + " "},
          } {}

    void tick() {
        bar.tick();
        int current = bar.current();
        std::string postfix = fmt::format("{}/{}", current, totalSize);
        bar.set_option(indicators::option::PostfixText{postfix});
    }

    void done() { bar.mark_as_completed(); }
};

#endif
