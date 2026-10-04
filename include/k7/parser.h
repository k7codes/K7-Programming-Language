#pragma once

#include "k7/ast.h"
#include "k7/lexer.h"

#include <string>
#include <vector>

namespace k7 {

struct ParseError {
    std::string msg;
    int line;
    int col;
};

class Parser {
public:
    Parser(const std::string& source, const std::string& filename = "<stdin>");

    NodePtr parse();

    const std::vector<ParseError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }
    const std::string& filename() const { return filename_; }

private:
    std::string src_;
    std::string filename_;
    std::vector<Token> toks_;
    size_t p_ = 0;
    std::vector<ParseError> errors_;
    int loopDepth_ = 0;
    int noLambda_ = 0;      // >0 iken bare lambda (x > ...) kapatilir

    const Token& cur() const { return toks_[p_]; }
    const Token& peekTok(size_t n = 1) const {
        size_t i = p_ + n;
        return i < toks_.size() ? toks_[i] : toks_.back();
    }
    const Token& prevTok() const { return p_ > 0 ? toks_[p_ - 1] : toks_[0]; }
    bool at(Tok k) const { return cur().kind == k; }
    bool atOp(const char* s) const {
        return cur().kind == Tok::Op && cur().text == s;
    }
    bool atKw(const char* s) const {
        return cur().kind == Tok::Ident && cur().text == s;
    }
    bool eatOp(const char* s) { if (atOp(s)) { p_++; return true; } return false; }
    bool eatKw(const char* s) { if (atKw(s)) { p_++; return true; } return false; }
    bool eatNewlines() {
        bool any = false;
        while (at(Tok::NewLine)) { p_++; any = true; }
        return any;
    }
    bool expectOp(const char* s);
    bool expectKw(const char* s);
    void skipNewlinesAndCont();
    [[noreturn]] void fail(const std::string& m);
    void softFail(const std::string& m);

    // --- Deyimler ---
    NodePtr parseProgram();
    NodePtr parseStatement();
    NodePtr parseBlock();
    NodePtr parseIf();
NodePtr parseBranchBody(int line);   // { ... } veya then <ifade>
    NodePtr parseWhile();
    NodePtr parseFor();
    NodePtr parseFnDecl();
    NodePtr parseClassDecl();
    NodePtr parseEnumDecl();
    NodePtr parseImport();
    NodePtr parseMatch();
    NodePtr parseVarDecl();
    NodePtr parseTypedDecl();
    NodePtr parseTry();

    // --- Ifadeler ---
    NodePtr parseExpr(bool allowBareLambda = true);
    // Virgulle ayrilmis eleman/arguman pozisyonlari: virgul tuple'a gitmez
    NodePtr parseArg() { return parseAssign(); }
    NodePtr parseAssign();
    NodePtr parseTernary();
    NodePtr parsePipe();
    NodePtr parseOr();
    NodePtr parseAnd();
    NodePtr parseNot();
    NodePtr parseCompare();
    NodePtr parseBitOr();
    NodePtr parseBitXor();
    NodePtr parseBitAnd();
    NodePtr parseShift();
    NodePtr parseAdd();
    NodePtr parseMul();
    NodePtr parseUnary();
    NodePtr parsePower();
    NodePtr parsePostfix();
    NodePtr parsePrimary();
    NodePtr parseLambdaBody(const std::vector<std::string>& params, int line);
    NodePtr parseBracketLit();          // [ ] { } #{}
    NodePtr parseRange(int line, NodePtr start);
    NodePtr parseStringLit();
    NodePtr parseStrSegment(const StrPart& part);

    bool looksLikeLambda() const;
    bool looksLikeLambdaAt(size_t idx) const;
    void skipSeparators();              // NewLine / Indent / Dedert yut
    void parseParams(Node& fn);
};

} // namespace k7
