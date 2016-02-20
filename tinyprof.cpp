#include <memory>
using std::unique_ptr;
#include <iostream>
using std::cout;
#include <sstream>
using std::stringstream;

#include "llvm/Support/CommandLine.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Decl.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Frontend/FrontendAction.h"
#include "llvm/Support/Casting.h"

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::cl;
using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;

static OptionCategory ToolingSampleCategory("Tooling Sample");

class InsertProfEnd : public MatchFinder::MatchCallback {
private:
  unsigned n_returns = 0;
  Replacements *Replace;

public:
  InsertProfEnd(Replacements *Replace) : Replace(Replace) {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    stringstream ss;
    const unsigned n_loops = 3;
    ss << "total = rdtsc() - start;\n  "
          "cerr << \"Total runtime: \" << total << '\\n';\n  ";
    for (unsigned i = 0; i < n_loops; ++i) {
      ss << "cerr << \"Time for loop " << i << ": \" << loop_total[" << i
         << "] << \" = \" << (double)loop_total[" << i
         << "] / total * 100 << '%' << '\\n';\n  ";
    }
    const ReturnStmt *ret = Result.Nodes.getNodeAs<ReturnStmt>("return");
    auto insertBefore = ret->getLocStart();
    Replacement Rep(*(Result.SourceManager), insertBefore, 0, ss.str());
    Replace->insert(Rep);
    ++n_returns;
  }

  unsigned getNumberOfReturns() { return n_returns; }
};

class InsertProfHeaderAndStart : public MatchFinder::MatchCallback {
private:
  Replacements *Replace;

public:
  InsertProfHeaderAndStart(Replacements *Replace) : Replace(Replace) {}

  void insertIncludeBefore(const FunctionDecl *MainFunction,
                           const MatchFinder::MatchResult &Result) {
    auto insertBefore = MainFunction->getLocStart();
    Replacement Rep(*(Result.SourceManager), insertBefore, 0,
                    "#include \"tinyprof.h\"\nuint64_t start, total;\n"
                    "const uint32_t n_loops = 16;\n"
                    "uint64_t loop_start[n_loops];\n"
                    "uint64_t loop_total[n_loops];\n\n");
    Replace->insert(Rep);
  }

  void insertStartProf(const FunctionDecl *MainFunction,
                       const MatchFinder::MatchResult &Result) {
    const CompoundStmt *Body = (const CompoundStmt *)MainFunction->getBody();
    const Stmt *firstInst = Body->body_front();
    auto insertBefore = firstInst->getLocStart();
    Replacement Rep(*(Result.SourceManager), insertBefore, 0,
                    "start = rdtsc();\n  ");
    Replace->insert(Rep);
  }

  virtual void run(const MatchFinder::MatchResult &Result) {
    const FunctionDecl *MainFunc =
        Result.Nodes.getNodeAs<clang::FunctionDecl>("main");
    insertIncludeBefore(MainFunc, Result);
    insertStartProf(MainFunc, Result);
  }
};

class InsertLoopProf : public MatchFinder::MatchCallback {
private:
  Replacements *Replace;
  static unsigned n_loops;

public:
  InsertLoopProf(Replacements *Replace) : Replace(Replace) {}

  void insertStartLoopProf(const Stmt *stmt,
                           const MatchFinder::MatchResult &Result) {
    auto insertBefore = stmt->getLocStart();
    stringstream ss;
    ss << "loop_start[" << n_loops << "] = rdtsc();\n  ";
    Replacement Rep(*(Result.SourceManager), insertBefore, 0, ss.str());

    Replace->insert(Rep);
  }

  void insertEndLoopProf(const Stmt *stmt,
                         const MatchFinder::MatchResult &Result) {
    auto insertAfter = stmt->getLocEnd();
    stringstream ss;
    ss << "\n  loop_total[" << n_loops << "] += rdtsc() - loop_start["
       << n_loops << "];";
    Replacement Rep(*(Result.SourceManager), insertAfter.getLocWithOffset(1), 0,
                    ss.str());
    Replace->insert(Rep);
  }

  virtual void run(const MatchFinder::MatchResult &Result) {
    const ForStmt *forStmt = Result.Nodes.getNodeAs<clang::ForStmt>("for");
    const WhileStmt *whileStmt =
        Result.Nodes.getNodeAs<clang::WhileStmt>("while");
    if (forStmt) {
      insertStartLoopProf(forStmt, Result);
      insertEndLoopProf(forStmt, Result);
      ++n_loops;
    } else if (whileStmt) {
      insertStartLoopProf(whileStmt, Result);
      insertEndLoopProf(whileStmt, Result);
      ++n_loops;
    }
  }
};

unsigned InsertLoopProf::n_loops = 0;

int main(int argc, const char *argv[]) {
  CommonOptionsParser op(argc, argv, ToolingSampleCategory);
  RefactoringTool Tool(op.getCompilations(), op.getSourcePathList());

  InsertProfEnd ProfEndInserter(&Tool.getReplacements());
  InsertProfHeaderAndStart ProfHeaderAndStartInserter(&Tool.getReplacements());
  InsertLoopProf ForLoopProfInserter(&Tool.getReplacements());
  InsertLoopProf WhileLoopProfInserter(&Tool.getReplacements());

  MatchFinder Finder;
  Finder.addMatcher(functionDecl(hasName("main")).bind("main"),
                    &ProfHeaderAndStartInserter);
  Finder.addMatcher(
      returnStmt(hasAncestor(functionDecl(hasName("main")))).bind("return"),
      &ProfEndInserter);
  Finder.addMatcher(
      forStmt(hasAncestor(functionDecl(hasName("main")))).bind("for"),
      &ForLoopProfInserter);
  Finder.addMatcher(
      whileStmt(hasAncestor(functionDecl(hasName("main")))).bind("while"),
      &WhileLoopProfInserter);

  // Run the tool and collect a list of replacements. We could call
  // runAndSave, which would destructively overwrite the files with
  // their new contents. However, for demonstration purposes it's
  // interesting to show the replacements.
  if (int Result = Tool.runAndSave(newFrontendActionFactory(&Finder).get())) {
    return Result;
  }

  llvm::outs() << "Replacements collected by the tool:\n";
  for (auto &r : Tool.getReplacements()) {
    llvm::outs() << r.toString() << "\n";
  }

  return 0;
}
