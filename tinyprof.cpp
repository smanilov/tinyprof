#include <memory>
using std::unique_ptr;
#include <iostream>
using std::cout;
#include <iterator>
using std::istream_iterator;
#include <sstream>
using std::istringstream;
using std::stringstream;
#include <fstream>
using std::ifstream;
#include <string>
using std::string;
using std::getline;
using std::stoi;
#include <vector>
using std::vector;
#include <algorithm>
using std::transform;

#include "llvm/Support/CommandLine.h"
#include "clang/Tooling/CommonOptionsParser.h"
using clang::tooling::CommonOptionsParser;
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

string getLibFileHeader(unsigned n_loops) {
  stringstream ss;
  ss << "#include <stdio.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "extern uint64_t loop_start["
     << n_loops << "];\n"
                   "extern uint64_t loop_total["
     << n_loops
     << "];\n"
        "\n"
        "static inline uint64_t tp_rdtsc(){\n"
        "    unsigned int lo,hi;\n"
        "    __asm__ __volatile__ (\"rdtsc\" : \"=a\" (lo), \"=d\" (hi));\n"
        "    return ((uint64_t)hi << 32) | lo;\n"
        "}\n";
  return ss.str();
}

string getMainFileHeader(unsigned n_loops) {
  stringstream ss;

  ss << "#include <stdio.h>\n"
        "#include <stdint.h>\n"
        "\n"
        "uint64_t start, total;\n";
  ss << "const uint32_t n_loops = " << n_loops << ";\n";
  ss << "uint64_t loop_start[n_loops];\n"
        "uint64_t loop_total[n_loops];\n"
        "\n"
        "static inline uint64_t tp_rdtsc(){\n"
        "    unsigned int lo,hi;\n"
        "    __asm__ __volatile__ (\"rdtsc\" : \"=a\" (lo), \"=d\" (hi));\n"
        "    return ((uint64_t)hi << 32) | lo;\n"
        "}\n";
  return ss.str();
}

string getMainFileFooter() {
  stringstream ss;
  ss << "total = tp_rdtsc() - start;\n"
        "  uint64_t sum = 0;\n"
        "  fprintf(stderr, \"Total runtime: %lu\\n\", total);\n"
        "  for (unsigned i = 0; i < n_loops; ++i) {\n"
        "    fprintf(stderr, \"Time for loop %u: %lu / %lf%%\\n\",\n"
        "            i,\n"
        "            loop_total[i],\n"
        "            (double)loop_total[i] / total * 100);\n"
        "  sum += loop_total[i];\n"
        "  }\n"
        "  fprintf(stderr, \"Unaccounted for runtime: %lu / %lf%%\\n\", \n"
        "          (total - sum),\n"
        "          (double)(total - sum) / total * 100);\n";
  return ss.str();
}

class InsertProfHeaderAndStart : public MatchFinder::MatchCallback {
private:
  Replacements *Replace;

  unsigned n_loops;

public:
  InsertProfHeaderAndStart(Replacements *Replace, unsigned n_loops)
      : Replace(Replace), n_loops(n_loops) {}

  void insertIncludeBefore(const FunctionDecl *MainFunction,
                           const MatchFinder::MatchResult &Result) {
    auto insertBefore = MainFunction->getLocStart();

    string header = getMainFileHeader(n_loops);
    Replacement Rep(*(Result.SourceManager), insertBefore, 0, header);
    Replace->insert(Rep);
  }

  void insertStartProf(const FunctionDecl *MainFunction,
                       const MatchFinder::MatchResult &Result) {
    const CompoundStmt *Body = (const CompoundStmt *)MainFunction->getBody();
    const Stmt *firstInst = Body->body_front();
    auto insertBefore = firstInst->getLocStart();
    Replacement Rep(*(Result.SourceManager), insertBefore, 0,
                    "start = tp_rdtsc();\n  ");
    Replace->insert(Rep);
  }

  virtual void run(const MatchFinder::MatchResult &Result) {
    const FunctionDecl *MainFunc =
        Result.Nodes.getNodeAs<clang::FunctionDecl>("main");
    insertIncludeBefore(MainFunc, Result);
    insertStartProf(MainFunc, Result);
  }
};

class InsertProfEnd : public MatchFinder::MatchCallback {
private:
  Replacements *Replace;

  unsigned n_returns = 0;

public:
  InsertProfEnd(Replacements *Replace) : Replace(Replace) {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    const ReturnStmt *ret = Result.Nodes.getNodeAs<ReturnStmt>("return");
    auto insertBefore = ret->getLocStart();

    string footer = getMainFileFooter();
    Replacement Rep(*(Result.SourceManager), insertBefore, 0, footer);
    Replace->insert(Rep);
    ++n_returns;
  }

  unsigned getNumberOfReturns() { return n_returns; }
};

class LoopLocs {
private:
  vector<string> filenames;
  vector<vector<unsigned>> lines;

  bool valid;
  unsigned n_loops;

public:
  LoopLocs() : valid(false), n_loops(0) {
    ifstream fin("locs");
    if (!fin) {
      llvm::outs() << "Couldn't find file 'locs'!\n";
      return;
    }

    string line;
    // Each line is a loop descriptor in the format "<filename> <line_0> ...
    // <line_m>"
    while (getline(fin, line)) {
      ++n_loops;
      istringstream iss(line);
      vector<string> tokens{istream_iterator<string>{iss},
                            istream_iterator<string>{}};
      filenames.push_back(tokens[0]);
      lines.push_back(vector<unsigned>(tokens.size() - 1));
      transform(tokens.begin() + 1, tokens.end(), lines.back().begin(),
                [](string &s) { return stoi(s); });
    }

    valid = true;
  }

  int getLoopID(string filename, unsigned line) const {
    for (unsigned i = 0; i < filenames.size(); ++i) {
      if (filenames[i] != filename)
        continue;
      for (unsigned j = 0; j < lines[i].size(); ++j) {
        if (lines[i][j] != line)
          continue;
        return i;
      }
    }
    return -1;
  }

  bool isValid() { return valid; }

  unsigned getNumLoops() const { return n_loops; }
};

class InsertLoopProf : public MatchFinder::MatchCallback {
private:
  Replacements *Replace;
  const LoopLocs &ll;

public:
  InsertLoopProf(Replacements *Replace, const LoopLocs &ll)
      : Replace(Replace), ll(ll) {}

  void insertStartLoopProf(unsigned LoopID, const Stmt *stmt,
                           const MatchFinder::MatchResult &Result) {
    auto insertBefore = stmt->getLocStart();
    stringstream ss;
    ss << "loop_start[" << LoopID << "] = tp_rdtsc();\n  ";
    Replacement Rep(*(Result.SourceManager), insertBefore, 0, ss.str());

    Replace->insert(Rep);
  }

  void insertEndLoopProf(unsigned LoopID, const Stmt *stmt,
                         const MatchFinder::MatchResult &Result) {
    auto insertAfter = stmt->getLocEnd();
    stringstream ss;
    ss << "\n  loop_total[" << LoopID << "] += tp_rdtsc() - loop_start["
       << LoopID << "];";
    Replacement Rep(*(Result.SourceManager), insertAfter.getLocWithOffset(1), 0,
                    ss.str());
    Replace->insert(Rep);
  }

  void insertProf(const Stmt *stmt, const MatchFinder::MatchResult &Result) {
    SourceLocation loc = stmt->getLocStart();
    auto filename = Result.SourceManager->getFilename(loc).str();
    FileID fid = Result.SourceManager->getFileID(loc);
    unsigned pos = Result.SourceManager->getFileOffset(loc);
    bool invalid = false;
    unsigned line = Result.SourceManager->getLineNumber(fid, pos, &invalid);

    int id = ll.getLoopID(filename, line);

    if (id != -1) {
      insertStartLoopProf((unsigned)id, stmt, Result);
      insertEndLoopProf((unsigned)id, stmt, Result);

      auto sof = Result.SourceManager->getLocForStartOfFile(fid);
      auto libHeader = getLibFileHeader(ll.getNumLoops());
      Replacement Rep(*(Result.SourceManager), sof, 0, libHeader);
      Replace->insert(Rep);
    }
  }

  virtual void run(const MatchFinder::MatchResult &Result) {
    const DoStmt *doStmt = Result.Nodes.getNodeAs<clang::DoStmt>("do");
    const ForStmt *forStmt = Result.Nodes.getNodeAs<clang::ForStmt>("for");
    const WhileStmt *whileStmt =
        Result.Nodes.getNodeAs<clang::WhileStmt>("while");
    if (doStmt) {
      insertProf(doStmt, Result);
    } else if (forStmt) {
      insertProf(forStmt, Result);
    } else if (whileStmt) {
      insertProf(whileStmt, Result);
    }
  }
};

int main(int argc, const char *argv[]) {
  CommonOptionsParser op(argc, argv, ToolingSampleCategory);
  RefactoringTool Tool(op.getCompilations(), op.getSourcePathList());

  LoopLocs ll;
  if (!ll.isValid()) {
    return -1;
  }

  InsertProfHeaderAndStart ProfHeaderAndStartInserter(&Tool.getReplacements(),
                                                      ll.getNumLoops());
  InsertProfEnd ProfEndInserter(&Tool.getReplacements());

  InsertLoopProf DoLoopProfInserter(&Tool.getReplacements(), ll);
  InsertLoopProf ForLoopProfInserter(&Tool.getReplacements(), ll);
  InsertLoopProf WhileLoopProfInserter(&Tool.getReplacements(), ll);

  MatchFinder Finder;
  Finder.addMatcher(functionDecl(hasName("main")).bind("main"),
                    &ProfHeaderAndStartInserter);
  Finder.addMatcher(
      returnStmt(hasAncestor(functionDecl(hasName("main")))).bind("return"),
      &ProfEndInserter);

  // Only top-level loops: their parent (compound statement) has a parent that
  // is a function declaration.
  Finder.addMatcher(
      doStmt(hasParent(stmt(hasParent(functionDecl())))).bind("do"),
      &DoLoopProfInserter);
  Finder.addMatcher(
      forStmt(hasParent(stmt(hasParent(functionDecl())))).bind("for"),
      &ForLoopProfInserter);
  Finder.addMatcher(
      whileStmt(hasParent(stmt(hasParent(functionDecl())))).bind("while"),
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
