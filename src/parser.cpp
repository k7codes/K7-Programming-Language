#include "k7/parser.h"

#include <cstring>
#include <set>

namespace k7 {

namespace {

const std::set<std::string>& assignOps() {
    static const std::set<std::string> ops = {
        "=", "+=", "-=", "*=", "/=", "//=", "%=", "**=",
        "&=", "|=", "^=", "<<=", ">>="
    };
    return ops;
}

bool isCmpTok(const Token& t) {
    if (t.kind == Tok::Op) {
        return t.text == "==" || t.text == "!=" || t.text == "<" ||
               t.text == "<=" || t.text == ">" || t.text == ">=";
    }
    if (t.kind == Tok::Ident) return t.text == "in" || t.text == "notin";
    return false;
}

std::string cmpOpText(const Token& t) {
    return t.kind == Tok::Op ? t.text : t.text;
}

} // namespace

NodePtr cloneNode(const Node* n) {
    if (!n) return nullptr;
    auto c = std::make_unique<Node>(n->kind, n->line);
    c->ival = n->ival;
    c->fval = n->fval;
    c->str = n->str;
    c->typeName = n->typeName;
    c->paramTypes = n->paramTypes;
    c->returnType = n->returnType;
    c->flag = n->flag;
    c->flag2 = n->flag2;
    c->names = n->names;
    for (const auto& s : n->segs) {
        Node::StrSeg seg;
        seg.isExpr = s.isExpr;
        seg.text = s.text;
        seg.expr = cloneNode(s.expr.get());
        c->segs.push_back(std::move(seg));
    }
    for (const auto& k : n->kids) c->kids.push_back(cloneNode(k.get()));
    c->body = cloneNode(n->body.get());
    return c;
}

// ===============================================================
// Kurucu / yardimcilar
// ===============================================================

Parser::Parser(const std::string& source, const std::string& filename)
    : filename_(filename) {
    Lexer lx(source, filename);
    toks_ = lx.tokenize();
    for (const auto& e : lx.errors())
        errors_.push_back({filename + ":" + std::to_string(e.line) + ":" +
                           std::to_string(e.col) + ": " + e.msg, e.line, e.col});
}

void Parser::fail(const std::string& m) {
    std::string where = cur().kind == Tok::End ? "dosya sonu" : cur().text;
    errors_.push_back({filename_ + ":" + std::to_string(cur().line) + ":" +
                       std::to_string(cur().col) + ": " + m,
                       cur().line, cur().col});
    throw ParseError{errors_.back().msg, cur().line, cur().col};
    (void)where;
}

void Parser::softFail(const std::string& m) {
    errors_.push_back({filename_ + ":" + std::to_string(cur().line) + ":" +
                       std::to_string(cur().col) + ": " + m, cur().line, cur().col});
}

bool Parser::expectOp(const char* s) {
    if (eatOp(s)) return true;
    fail(std::string("'") + s + "' bekleniyordu");
    return false;
}

bool Parser::expectKw(const char* s) {
    if (eatKw(s)) return true;
    fail(std::string("'") + s + "' bekleniyordu");
    return false;
}

void Parser::skipNewlinesAndCont() { eatNewlines(); }

void Parser::skipSeparators() {
    while (at(Tok::NewLine) || at(Tok::Indent) || at(Tok::Dedent) || atOp(";")) p_++;
}

NodePtr Parser::parse() {
    try {
        return parseProgram();
    } catch (const ParseError&) {
        return std::make_unique<Node>(NK::Program, 0);
    }
}

// ===============================================================
// Program
// ===============================================================

NodePtr Parser::parseProgram() {
    auto prog = mk(NK::Program, 1);
    skipSeparators();
    while (!at(Tok::End) && errors_.empty()) {
        NodePtr s = parseStatement();
        if (s) prog->add(std::move(s));
        if (at(Tok::Dedent)) { p_++; continue; }
        if (at(Tok::End)) break;
        if (!at(Tok::NewLine) && !atOp(";")) {
            softFail("deyim sonunda beklenmeyen '" + cur().text + "'");
            while (!at(Tok::NewLine) && !at(Tok::Dedent) && !at(Tok::End)) p_++;
        }
        skipSeparators();
    }
    return prog;
}

NodePtr Parser::parseBlock() {
    int ln = cur().line;
    auto blk = mk(NK::Block, ln);
    expectOp("{");
    int savedLoop = loopDepth_;
    while (!atOp("}") && !at(Tok::End) && errors_.empty()) {
        skipSeparators();
        if (atOp("}") || at(Tok::End)) break;
        NodePtr s = parseStatement();
        if (s) blk->add(std::move(s));
        if (atOp("}") || at(Tok::End)) break;
        // Deyim sonunda satir sonu, girinti veya ';' beklenir
        if (!at(Tok::NewLine) && !at(Tok::Indent) && !at(Tok::Dedent) && !atOp(";"))
            softFail("blokta beklenmeyen '" + cur().text + "'");
    }
    loopDepth_ = savedLoop;
    if (atOp("}")) p_++;
    return blk;
}

// ===============================================================
// Deyimler
// ===============================================================

NodePtr Parser::parseStatement() {
    int ln = cur().line;

    if (at(Tok::NewLine) || at(Tok::Indent) || at(Tok::Dedent)) { p_++; return nullptr; }
    if (atOp(";")) { p_++; return nullptr; }
    if (atOp("{")) return parseBlock();

    if (atKw("if"))     return parseIf();
    if (atKw("while"))  return parseWhile();
    if (atKw("for"))    return parseFor();
    if (atKw("fn"))     return parseFnDecl();
    if (atKw("class"))  return parseClassDecl();
    if (atKw("enum"))   return parseEnumDecl();
    if (atKw("import")) return parseImport();
    if (atKw("match"))  return parseMatch();
    if (atKw("try"))    return parseTry();
    if (atKw("var") || atKw("let") || atKw("const")) return parseVarDecl();
    if (at(Tok::Ident) && peekTok().kind == Tok::Ident) return parseTypedDecl();

    if (atKw("loop")) {
        p_++;
        auto n = mk(NK::Loop, ln);
        loopDepth_++;
        n->add(parseBlock());
        loopDepth_--;
        return n;
    }
    if (atKw("return")) {
        p_++;
        auto n = mk(NK::Return, ln);
        if (!at(Tok::NewLine) && !at(Tok::End) && !atOp("}") &&
            !atOp(";") && !at(Tok::Dedent))
            n->add(parseExpr());
        return n;
    }
    if (atKw("break")) {
        p_++;
        if (loopDepth_ == 0) softFail("'break' yalnizca dongu icinde kullanilabilir");
        auto n = mk(NK::Break, ln);
        if (at(Tok::Ident) && cur().text == "loop") { p_++; n->flag = true; }
        return n;
    }
    if (atKw("continue")) {
        p_++;
        if (loopDepth_ == 0) softFail("'continue' yalnizca dongu icinde kullanilabilir");
        return mk(NK::Continue, ln);
    }
    if (atKw("throw")) {
        p_++;
        auto n = mk(NK::Throw, ln);
        n->add(parseExpr());
        return n;
    }
    if (atKw("assert")) {
        p_++;
        auto n = mk(NK::Assert, ln);
        n->add(parseArg());
        if (eatOp(",")) n->add(parseArg());
        return n;
    }
    if (atKw("defer")) {
        p_++;
        auto n = mk(NK::ExprStmt, ln);
        n->str = "defer";
        n->add(parseExpr());
        return n;
    }

    auto st = mk(NK::ExprStmt, ln);
    st->add(parseExpr());
    return st;
}

NodePtr Parser::parseVarDecl() {
    int ln = cur().line;
    std::string kw = cur().text;
    p_++;
    auto n = mk(NK::VarDecl, ln);
    n->str = kw;

    // Coklu hedef destesi:  var a, b = 1, 2   /   var (a, b) = (1, 2)
    bool paren = false;
    if (atOp("(")) { paren = true; p_++; }

    // Hedef listesi
    std::vector<NodePtr> targets;
    while (true) {
        if (at(Tok::Ident)) {
            targets.push_back(mkIdent(cur().text, cur().line));
            n->names.push_back(cur().text);
            p_++;
        } else if (atOp("[")) {   // var x[0] = ...
            auto target = parsePostfix();
            targets.push_back(std::move(target));
        } else if (atOp(".")) {
            auto base = mkIdent("?", ln);
            targets.push_back(std::move(base));
        }
        if (eatOp(",")) continue;
        break;
    }
    if (paren) expectOp(")");

    if (eatOp(":")) parseExpr();   // tip ipucu (goz ardindan)

    if (eatOp("=")) {
        auto val = parseExpr();
        if (targets.size() == 1) {
            n->add(std::move(val));
        } else {
            auto tup = mk(NK::TupleLit, ln);
            if (val) tup->add(std::move(val));
            n->add(std::move(tup));
        }
    }
    return n;
}

NodePtr Parser::parseTypedDecl() {
    const int ln = cur().line;
    const std::string declaredType = cur().text;
    p_++;
    if (!at(Tok::Ident)) fail("tanim adi bekleniyordu");
    const std::string name = cur().text;
    p_++;

    if (eatOp("(")) {
        auto fn = mk(NK::FnDecl, ln);
        fn->str = name;
        fn->returnType = declaredType;
        parseParams(*fn);
        if (eatOp("->")) {
            if (!at(Tok::Ident)) fail("donus turu bekleniyordu");
            fn->returnType = cur().text;
            p_++;
        }
        fn->body = parseBlock();
        return fn;
    }

    auto decl = mk(NK::VarDecl, ln);
    decl->typeName = declaredType;
    decl->names.push_back(name);
    if (eatOp("=")) decl->add(parseExpr());
    return decl;
}

// if <kosul> { ... } elif ... else { ... }
// if <kosul> then <ifade> else <ifade>
NodePtr Parser::parseIf() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::If, ln);
    n->add(parseExpr(false));
    n->add(parseBranchBody(ln));

    size_t afterBody = p_;
    skipSeparators();
    if (atKw("elif")) {
        n->add(parseIf());
        return n;
    }
    if (atKw("else")) {
        p_++;
        if (atKw("if")) { n->add(parseIf()); return n; }
        n->add(parseBranchBody(ln));
        return n;
    }
    p_ = afterBody;
    return n;
}

// { ... } govde ya da "then <ifade>" / else <ifade> ifadesi
NodePtr Parser::parseBranchBody(int ln) {
    eatKw("then");
    if (atOp("{")) return parseBlock();
    auto blk = mk(NK::Block, ln);
    auto st = mk(NK::ExprStmt, cur().line);
    st->add(parseExpr());
    blk->add(std::move(st));
    return blk;
}

NodePtr Parser::parseWhile() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::While, ln);
    n->add(parseExpr(false));
    loopDepth_++;
    n->add(parseBlock());
    loopDepth_--;
    return n;
}

NodePtr Parser::parseFor() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::For, ln);
    if (eatOp("(")) {
        n->flag = true;
        NodePtr init;
        NodePtr condition;
        NodePtr step;
        if (!atOp(";")) {
            if (atKw("var") || atKw("let") || atKw("const") ||
                (at(Tok::Ident) && peekTok().kind == Tok::Ident)) {
                init = parseStatement();
            } else {
                auto stmt = mk(NK::ExprStmt, cur().line);
                stmt->add(parseExpr(false));
                init = std::move(stmt);
            }
        }
        expectOp(";");
        if (!atOp(";")) condition = parseExpr(false);
        expectOp(";");
        if (!atOp(")")) step = parseExpr(false);
        expectOp(")");
        n->add(std::move(init));
        n->add(std::move(condition));
        n->add(std::move(step));
        loopDepth_++;
        n->add(parseBlock());
        loopDepth_--;
        return n;
    }
    if (!at(Tok::Ident)) fail("dongu degiskeni bekleniyordu");
    n->names.push_back(cur().text);
    p_++;
    if (eatOp(",")) {
        if (!at(Tok::Ident)) fail("ikinci dongu degiskeni bekleniyordu");
        n->names.push_back(cur().text);
        p_++;
    }
    expectKw("in");
    n->add(parseExpr(false));
    loopDepth_++;
    n->add(parseBlock());
    loopDepth_--;
    return n;
}

void Parser::parseParams(Node& fn) {
    skipNewlinesAndCont();
    while (!atOp(")") && !at(Tok::End) && errors_.empty()) {
        skipNewlinesAndCont();
        if (atOp("**")) { p_++; fn.flag = true; fn.flag2 = true; }   // **kwargs
        else if (atOp("&")) { p_++; }                                // & by-ref
        if (atOp("*")) { p_++; fn.flag2 = true; }                    // *args

        if (!at(Tok::Ident)) fail("parametre adi bekleniyordu");
        std::string ptype;
        std::string pname;
        if (peekTok().kind == Tok::Ident) {
            ptype = cur().text;
            p_++;
            pname = cur().text;
            p_++;
        } else {
            pname = cur().text;
            p_++;
        }
        if (eatOp(":")) parseTernary();          // tip ipucu
        if (eatOp("=")) fn.kids.push_back(parseTernary());
        else fn.kids.push_back(nullptr);
        fn.names.push_back(pname);
        fn.paramTypes.push_back(ptype);
        skipNewlinesAndCont();
        if (!eatOp(",")) break;
    }
    expectOp(")");
}

NodePtr Parser::parseFnDecl() {
    int ln = cur().line;
    p_++;  // 'fn'
    auto n = mk(NK::FnDecl, ln);
    if (at(Tok::Ident)) { n->str = cur().text; p_++; }
    else n->str = "<anonim>";

    if (eatOp("<")) {                 // generic parametreler
        while (!atOp(">") && !at(Tok::End)) p_++;
        expectOp(">");
    }
    if (atOp("(")) { p_++; parseParams(*n); }
    if (eatOp("->")) {
        if (!at(Tok::Ident)) fail("donus turu bekleniyordu");
        n->returnType = cur().text;
        p_++;
    } else if (atOp(":")) {
        p_++;
        if (at(Tok::Ident)) { n->returnType = cur().text; p_++; }
        else parseTernary();
    }

    if (atOp("=")) {                  // tek satirlik govde
        p_++;
        auto blk = mk(NK::Block, ln);
        blk->add(parseTernary());
        n->body = std::move(blk);
        return n;
    }
    n->body = parseBlock();
    return n;
}

NodePtr Parser::parseClassDecl() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::ClassDecl, ln);
    if (!at(Tok::Ident)) fail("sinif adi bekleniyordu");
    n->str = cur().text;
    p_++;

    if (atOp("<")) {
        // <T> generic mi, <Super> extends mi?  '{' oncesi eslesen '>' yoksa extends.
        bool generic = false;
        int depth = 0;
        for (size_t j = p_; j < toks_.size(); ++j) {
            const Token& t = toks_[j];
            if (t.kind != Tok::Op) {
                if (t.kind == Tok::NewLine || t.kind == Tok::End) break;
                continue;
            }
            if (t.text == "<") ++depth;
            else if (t.text == ">") { if (--depth == 0) { generic = true; break; } }
            else if (t.text == "{" || t.text == "(") break;
        }
        if (generic) {
            p_++;
            while (!atOp(">") && !at(Tok::End)) p_++;
            expectOp(">");
        } else {
            p_++;
            do {
                if (!at(Tok::Ident)) { fail("super sinif adi bekleniyordu"); break; }
                n->add(mkIdent(cur().text, cur().line));
                p_++;
            } while (eatOp(","));
        }
    } else if (atOp("(")) {
        fail("sinif super'i '<Super>' ile belirtilir, orn: class A < B");
    }

    skipNewlinesAndCont();
    if (!atOp("{")) fail("sinif govdesi '{' bekleniyordu");
    p_++;
    skipSeparators();

    while (!atOp("}") && !at(Tok::End) && errors_.empty()) {
        skipSeparators();
        if (atOp("}")) break;

        bool isStatic = false;
        if (atKw("static")) { p_++; isStatic = true; skipSeparators(); }

        if (atKw("fn") || atKw("var") || atKw("let") || atKw("const") ||
            (at(Tok::Ident) && peekTok().kind == Tok::Ident)) {
            NodePtr m = parseStatement();
            if (m) { m->flag = isStatic; n->add(std::move(m)); }
        } else if (at(Tok::Ident) && peekTok().kind == Tok::Ident) {
            p_++;                       // alan adi
            if (eatOp(":")) parseExpr();
        } else if (atOp("@")) {        // dekorator: @deco
            while (!at(Tok::NewLine) && !atOp("}") && !at(Tok::End)) p_++;
        } else {
            softFail("sinif govdesinde 'fn' veya alan bekleniyordu, '" + cur().text +
                     "' bulundu");
            while (!at(Tok::NewLine) && !atOp("}") && !at(Tok::End)) p_++;
        }
        skipSeparators();
    }
    if (atOp("}")) p_++;
    return n;
}

NodePtr Parser::parseEnumDecl() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::EnumDecl, ln);
    if (!at(Tok::Ident)) fail("enum adi bekleniyordu");
    n->str = cur().text;
    p_++;
    expectOp("{");
    skipNewlinesAndCont();
    while (!atOp("}") && !at(Tok::End) && errors_.empty()) {
        skipNewlinesAndCont();
        if (atOp("}")) break;
        if (!at(Tok::Ident)) { fail("enum uyesi bekleniyordu"); break; }
        auto m = mk(NK::EnumMember, cur().line);
        m->str = cur().text;
        n->names.push_back(cur().text);
        p_++;
        if (eatOp("=")) m->add(parseExpr());
        n->add(std::move(m));
        if (!eatOp(",")) skipNewlinesAndCont();
    }
    if (atOp("}")) p_++;
    return n;
}

NodePtr Parser::parseImport() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::ImportDecl, ln);

    // import "yol.k7" [as ad]  /  import std.math [as ad]  /  import ad = ifade
    if (at(Tok::Str) && cur().parts.size() == 1) {
        n->str = cur().parts[0].text;
        n->flag2 = true;   // dosya yolu
        p_++;
        if (eatKw("as")) {
            if (!at(Tok::Ident)) fail("takma ad bekleniyordu");
            n->names.push_back(cur().text);   // takma ad
            p_++;
        }
    } else {
        auto path = mk(NK::ListLit, ln);
        while (at(Tok::Ident) && cur().text != "as") {
            path->names.push_back(cur().text);
            p_++;
            if (!eatOp(".")) break;
        }
        n->add(std::move(path));
        if (eatKw("as")) {
            if (!at(Tok::Ident)) fail("takma ad bekleniyordu");
            n->str = cur().text;
            p_++;
        }
    }
    if (eatOp("=")) { n->flag = true; n->add(parseExpr()); }
    return n;
}

NodePtr Parser::parseTry() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::Try, ln);
    n->add(parseBlock());
    skipSeparators();
    if (atKw("catch")) {
        p_++;
        if (at(Tok::Ident)) { n->names.push_back(cur().text); p_++; }
        n->add(parseBlock());
    } else {
        n->add(nullptr);
    }
    return n;
}

// ===============================================================
// match
// ===============================================================

NodePtr Parser::parseMatch() {
    int ln = cur().line;
    p_++;
    auto n = mk(NK::Match, ln);
    n->add(parseExpr(false));
    skipNewlinesAndCont();
    expectOp("{");
    skipSeparators();

    while (!atOp("}") && !at(Tok::End) && errors_.empty()) {
        skipSeparators();
        if (atOp("}")) break;

        auto clause = mk(NK::CaseClause, cur().line);
        eatKw("case");
        skipSeparators();

        auto pat = mk(NK::CasePattern, cur().line);
        do {
            pat->add(parseOr());
            skipSeparators();
        } while (eatOp("|"));
        clause->add(std::move(pat));

        if (atKw("if")) { p_++; clause->add(parseExpr(false)); }
        else clause->add(nullptr);

        skipSeparators();
        if (!expectOp("->")) break;
        clause->add(parseExpr());
        n->add(std::move(clause));
        if (!eatOp(",")) skipSeparators();
    }
    if (atOp("}")) p_++;
    return n;
}

// ===============================================================
// Ifade zinciri
// ===============================================================

NodePtr Parser::parseExpr(bool allowBareLambda) {
    if (!allowBareLambda) noLambda_++;
    auto first = parseAssign();
    if (!allowBareLambda) noLambda_--;

    if (!first) return nullptr;

    if (atOp(",")) {
        auto t = mk(NK::TupleLit, first->line);
        t->add(std::move(first));
        while (eatOp(",")) {
            if (at(Tok::NewLine) || atOp("}") || atOp(")") || atOp("]") || at(Tok::End))
                break;
            if (!allowBareLambda) noLambda_++;
            t->add(parseAssign());
            if (!allowBareLambda) noLambda_--;
        }
        return t;
    }
    return first;
}

// "(a, b) = ..." coklu atama
static bool matchParenTargetList(const std::vector<Token>& toks, size_t start,
                                 size_t& endIdx) {
    if (start >= toks.size()) return false;
    if (!(toks[start].kind == Tok::Op && toks[start].text == "(")) return false;
    size_t i = start + 1;
    bool sawIdent = false;
    bool expectItem = true;
    while (i < toks.size()) {
        const Token& t = toks[i];
        if (t.kind == Tok::Op && t.text == ")") {
            if (expectItem && !sawIdent) return false;
            endIdx = i;
            return sawIdent;
        }
        if (expectItem) {
            if (t.kind != Tok::Ident) return false;
            sawIdent = true;
            expectItem = false;
        } else {
            if (!(t.kind == Tok::Op && t.text == ",")) return false;
            expectItem = true;
        }
        ++i;
    }
    return false;
}

NodePtr Parser::parseAssign() {
    int ln = cur().line;
    size_t endIdx = 0;
    if (atOp("(") && matchParenTargetList(toks_, p_, endIdx) &&
        endIdx + 1 < toks_.size() && toks_[endIdx + 1].kind == Tok::Op &&
        toks_[endIdx + 1].text == "=") {
        auto lhs = mk(NK::TupleLit, ln);
        size_t i = p_ + 1;
        while (i < endIdx) {
            lhs->add(mkIdent(toks_[i].text, toks_[i].line));
            i += 2;
        }
        p_ = endIdx + 2;
        auto n = mk(NK::Assign, ln);
        n->str = "=";
        n->add(std::move(lhs));
        n->add(parseExpr());
        return n;
    }

    auto lhs = parseTernary();
    if (!lhs) return nullptr;

    if (cur().kind == Tok::Op && assignOps().count(cur().text)) {
        std::string op = cur().text;
        p_++;
        auto n = mk(NK::Assign, ln);
        n->str = op;
        n->add(std::move(lhs));
        n->add(parseExpr());
        return n;
    }
    return lhs;
}

NodePtr Parser::parseTernary() {
    auto cond = parsePipe();
    if (!cond) return nullptr;

    if (atOp("??")) {
        p_++;
        auto n = mk(NK::Logical, cond->line);
        n->str = "??";
        n->add(std::move(cond));
        n->add(parseTernary());
        return n;
    }
    if (atOp("?")) {
        p_++;
        auto n = mk(NK::Ternary, cond->line);
        n->add(std::move(cond));
        n->add(parseTernary());
        expectOp(":");
        n->add(parseTernary());
        return n;
    }
    return cond;
}

NodePtr Parser::parsePipe() {
    auto left = parseOr();
    if (!left) return nullptr;
    while (atOp("|>")) {
        int ln = cur().line;
        p_++;
        auto fnExpr = parseOr();
        //  veri |> islem(a, b)  ==>  islem(a, b, veri)
        //  veri |> islem        ==>  islem(veri)
        if (fnExpr && fnExpr->kind == NK::Call) {
            fnExpr->add(std::move(left));
            left = std::move(fnExpr);
        } else {
            auto call = mk(NK::Call, ln);
            call->add(std::move(fnExpr));
            call->add(std::move(left));
            left = std::move(call);
        }
    }
    return left;
}

NodePtr Parser::parseOr() {
    auto left = parseAnd();
    if (!left) return nullptr;
    while (atKw("or") || atOp("||")) {
        int ln = cur().line;
        p_++;
        auto n = mk(NK::Logical, ln);
        n->str = "or";
        n->add(std::move(left));
        n->add(parseAnd());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseAnd() {
    auto left = parseNot();
    if (!left) return nullptr;
    while (atKw("and") || atOp("&&")) {
        int ln = cur().line;
        p_++;
        auto n = mk(NK::Logical, ln);
        n->str = "and";
        n->add(std::move(left));
        n->add(parseNot());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseNot() {
    if (atKw("not") || atOp("!")) {
        int ln = cur().line;
        bool kw = atKw("not");
        p_++;
        auto n = mk(NK::Unary, ln);
        n->str = kw ? "not" : "!";
        n->flag2 = true;
        n->add(parseNot());
        return n;
    }
    return parseCompare();
}

NodePtr Parser::parseCompare() {
    auto left = parseBitOr();
    if (!left || !isCmpTok(cur())) return left;

    // Zincirleme karsilastirma:  1 < x < 10   ==>   (1<x) and (x<10)
    int ln = cur().line;
    std::string op = cmpOpText(cur());
    p_++;
    NodePtr b = parseBitOr();
    if (!b) return nullptr;

    auto first = mk(NK::Binary, ln);
    first->str = op;
    first->add(std::move(left));
    first->add(cloneNode(b.get()));

    NodePtr acc = std::move(first);
    NodePtr prevRight = std::move(b);

    while (isCmpTok(cur())) {
        int l2 = cur().line;
        std::string op2 = cmpOpText(cur());
        p_++;
        NodePtr c = parseBitOr();
        if (!c) break;
        NodePtr shared = std::move(prevRight);      // sonraki adimda da kullanilacak
        auto cmp2 = mk(NK::Binary, l2);
        cmp2->str = op2;
        cmp2->add(cloneNode(shared.get()));
        cmp2->add(std::move(c));
        auto conj = mk(NK::Logical, l2);
        conj->str = "and";
        conj->add(std::move(acc));
        conj->add(std::move(cmp2));
        acc = std::move(conj);
        prevRight = std::move(shared);
    }
    return acc;
}

NodePtr Parser::parseBitOr() {
    auto left = parseBitXor();
    while (left && atOp("|")) {
        int ln = cur().line; p_++;
        auto n = mk(NK::Binary, ln); n->str = "|";
        n->add(std::move(left)); n->add(parseBitXor());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseBitXor() {
    auto left = parseBitAnd();
    while (left && atOp("^")) {
        int ln = cur().line; p_++;
        auto n = mk(NK::Binary, ln); n->str = "^";
        n->add(std::move(left)); n->add(parseBitAnd());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseBitAnd() {
    auto left = parseShift();
    while (left && atOp("&")) {
        int ln = cur().line; p_++;
        auto n = mk(NK::Binary, ln); n->str = "&";
        n->add(std::move(left)); n->add(parseShift());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseShift() {
    auto left = parseAdd();
    while (left && (atOp("<<") || atOp(">>"))) {
        int ln = cur().line;
        std::string op = cur().text; p_++;
        auto n = mk(NK::Binary, ln); n->str = op;
        n->add(std::move(left)); n->add(parseAdd());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseAdd() {
    auto left = parseMul();
    while (left && (atOp("+") || atOp("-"))) {
        int ln = cur().line;
        std::string op = cur().text; p_++;
        auto n = mk(NK::Binary, ln); n->str = op;
        n->add(std::move(left)); n->add(parseMul());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseMul() {
    auto left = parseUnary();
    while (left && (atOp("*") || atOp("/") || atOp("//") || atOp("%"))) {
        int ln = cur().line;
        std::string op = cur().text; p_++;
        auto n = mk(NK::Binary, ln); n->str = op;
        n->add(std::move(left)); n->add(parseUnary());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseUnary() {
    int ln = cur().line;
    if (eatOp("++") || eatOp("--")) {
        const std::string op = prevTok().text == "++" ? "+=" : "-=";
        auto n = mk(NK::Assign, ln);
        n->str = op;
        n->add(parseUnary());
        n->add(mkInt(1, ln));
        return n;
    }
    if (atOp("-") || atOp("+") || atOp("~") || atOp("!") || atKw("not")) {
        std::string op = cur().text;
        p_++;
        auto n = mk(NK::Unary, ln);
        n->str = op;
        n->flag2 = (op == "!" || op == "not");
        n->add(parseUnary());
        return n;
    }
    return parsePower();
}

NodePtr Parser::parsePower() {
    auto base = parsePostfix();
    if (!base) return nullptr;
    if (atOp("**")) {
        int ln = cur().line; p_++;
        auto n = mk(NK::Binary, ln); n->str = "**";
        n->add(std::move(base));
        n->add(parseUnary());   // sag tabanli: 2 ** 3 ** 2
        return n;
    }
    return base;
}

// ===============================================================
// Postfix
// ===============================================================

NodePtr Parser::parsePostfix() {
    auto obj = parsePrimary();
    if (!obj) return nullptr;

    while (true) {
        int ln = cur().line;

        if (atOp("++") || atOp("--")) {
            const std::string op = cur().text == "++" ? "+=" : "-=";
            p_++;
            auto inc = mk(NK::Assign, ln);
            inc->str = op;
            inc->add(std::move(obj));
            inc->add(mkInt(1, ln));
            obj = std::move(inc);
            continue;
        }

        if (atOp("(")) {
            p_++;
            auto call = mk(NK::Call, ln);
            call->add(std::move(obj));
            skipNewlinesAndCont();
            if (!atOp(")")) {
                do {
                    bool spread = eatOp("...");
                    NodePtr a = parseArg();
                    if (a && spread) a->flag = true;
                    call->add(std::move(a));
                    skipNewlinesAndCont();
                } while (eatOp(",") && !atOp(")"));
            }
            expectOp(")");
            obj = std::move(call);
            continue;
        }

        if (atOp("[")) {
            p_++;
            skipNewlinesAndCont();
            NodePtr lo, hi, step;
            if (!atOp("]") && !atOp(":")) lo = parseArg();
            bool isSlice = false;
            if (atOp(":")) {
                isSlice = true;
                p_++;
                if (!atOp("]") && !atOp(":")) hi = parseArg();
                if (eatOp(":")) {
                    if (!atOp("]")) step = parseArg();
                }
            }
            expectOp("]");
            if (isSlice) {
                auto sl = mk(NK::Slice, ln);
                sl->add(std::move(obj));
                sl->add(std::move(lo));
                sl->add(std::move(hi));
                sl->add(std::move(step));
                obj = std::move(sl);
            } else {
                auto ix = mk(NK::Index, ln);
                ix->add(std::move(obj));
                ix->add(std::move(lo));
                obj = std::move(ix);
            }
            continue;
        }

        if (atOp("?.")) {
            p_++;
            if (!at(Tok::Ident)) fail("alan adi bekleniyordu");
            std::string field = cur().text;
            p_++;
            auto g = mk(NK::SafeGet, ln);
            g->str = field;
            g->add(std::move(obj));
            if (atOp("(")) {
                int cl = cur().line; p_++;
                auto call = mk(NK::Call, cl);
                call->flag = true;   // guvenli cagri
                call->add(std::move(g));
                if (!atOp(")")) {
                    do { call->add(parseArg()); } while (eatOp(","));
                }
                expectOp(")");
                obj = std::move(call);
            }
            continue;
        }

        if (atOp("..") || atOp("...")) {
            // son ifadeden sonra aralik:  a..b
            return parseRange(ln, std::move(obj));
        }

        if (atOp(".")) {
            p_++;
            if (!at(Tok::Ident)) fail("metot/alan adi bekleniyordu");
            std::string field = cur().text;
            int fl = cur().line;
            p_++;
            if (atOp("(")) {
                p_++;
                auto call = mk(NK::MethodCall, fl);
                call->str = field;
                call->add(std::move(obj));
                skipNewlinesAndCont();
                if (!atOp(")")) {
                    do {
                        call->add(parseArg());
                        skipNewlinesAndCont();
                    } while (eatOp(",") && !atOp(")"));
                }
                expectOp(")");
                obj = std::move(call);
            } else {
                auto g = mk(NK::Get, ln);
                g->str = field;
                g->add(std::move(obj));
                obj = std::move(g);
            }
            continue;
        }

        break;
    }
    return obj;
}

// ===============================================================
// Primary
// ===============================================================

bool Parser::looksLikeLambdaAt(size_t idx) const {
    size_t i = idx;
    if (i >= toks_.size()) return false;
    if (!(toks_[i].kind == Tok::Op && toks_[i].text == "(")) return false;
    ++i;
    bool sawIdent = false;
    bool expectItem = true;
    while (i < toks_.size()) {
        const Token& u = toks_[i];
        if (u.kind == Tok::Op && u.text == ")") {
            if (!sawIdent || expectItem) return false;
            ++i;
            break;
        }
        if (expectItem) {
            if (u.kind != Tok::Ident) return false;
            if (u.text == "and" || u.text == "or" || u.text == "not" ||
                u.text == "in" || u.text == "if") return false;
            sawIdent = true;
            expectItem = false;
        } else {
            if (!(u.kind == Tok::Op && u.text == ",")) return false;
            expectItem = true;
        }
        ++i;
    }
    return i < toks_.size() && toks_[i].kind == Tok::Op &&
           (toks_[i].text == ">" || toks_[i].text == "=>");
}

bool Parser::looksLikeLambda() const { return looksLikeLambdaAt(p_); }

NodePtr Parser::parsePrimary() {
    int ln = cur().line;
    const Token& t = cur();

    if (t.kind == Tok::Int) {
        auto n = mkInt(t.ival, ln);
        p_++;
        if (atOp("..") || atOp("...")) return parseRange(ln, std::move(n));
        return n;
    }
    if (t.kind == Tok::Float) {
        auto n = mkFloat(t.fval, ln);
        p_++;
        if (atOp("..") || atOp("...")) return parseRange(ln, std::move(n));
        return n;
    }
    if (t.kind == Tok::Str) return parseStringLit();

    // Ifade konumunda if / match
    if (atKw("if"))    return parseIf();
    if (atKw("match")) return parseMatch();

    // (x, y) > body  /  (x) > body
    if (looksLikeLambdaAt(p_)) {
        p_++;
        std::vector<std::string> params;
        while (!atOp(")") && !at(Tok::End)) {
            params.push_back(cur().text);
            p_++;
            if (!eatOp(",")) break;
        }
        expectOp(")");
        if (!eatOp("=>")) expectOp(">");
        return parseLambdaBody(params, ln);
    }

    if (t.kind == Tok::Ident) {
        const std::string kw = t.text;
        if (kw == "true")  { p_++; return mkBool(true, ln); }
        if (kw == "false") { p_++; return mkBool(false, ln); }
        if (kw == "nil" || kw == "null") { p_++; return mkNil(ln); }
        if (kw == "this" || kw == "self") { p_++; return mk(NK::ThisExpr, ln); }
        if (kw == "fn") return parseFnDecl();          // anonim fonksiyon
        if (kw == "super") { p_++; return mkIdent("super", ln); }

        static const std::set<std::string> reserved = {
            "and", "or", "not", "in", "notin", "if", "else", "elif", "while",
            "for", "return", "break", "continue", "var", "let", "const", "class",
            "enum", "import", "as", "match", "case", "try", "catch", "throw",
            "loop", "defer", "static", "yield"
        };
        const Token& nx = peekTok();
        // Bare lambda yalnizca ifade beklenen konumlarda:  map(x > x*2, ...)
        // "if x > 5" gibi kullanimlarda lambda olarak yorumlanmaz.
        bool contextOk = true;
        if (p_ > 0) {
            const Token& pv = prevTok();
            if (pv.kind == Tok::Ident) {
                static const std::set<std::string> noLambdaCtx = {
                    "if", "elif", "else", "while", "and", "or", "not", "in",
                    "for", "case", "match", "return"
                };
                if (noLambdaCtx.count(pv.text)) contextOk = false;
            }
        }
        if (noLambda_ > 0) contextOk = false;
        if (kw == "_") contextOk = false;             // joker: her zaman karsilastirma
        if (contextOk && nx.kind == Tok::Op &&
            (nx.text == ">" || nx.text == "=>") && !reserved.count(kw)) {
            std::vector<std::string> params{cur().text};
            p_ += 2;
            return parseLambdaBody(params, ln);
        }
        p_++;
        return mkIdent(kw, ln);
    }

    if (t.kind == Tok::Op) {
        if (t.text == "(") {
            p_++;
            skipNewlinesAndCont();
            if (atOp(")")) { p_++; return mkNil(ln); }
            auto e = parseExpr();
            skipNewlinesAndCont();
            if (atOp(",")) {
                auto tup = mk(NK::TupleLit, ln);
                if (e) tup->add(std::move(e));
                while (eatOp(",")) {
                    if (atOp(")")) break;
                    tup->add(parseExpr());
                    skipNewlinesAndCont();
                }
                expectOp(")");
                return tup;
            }
            expectOp(")");
            return e;
        }
        if (t.text == "[" || t.text == "{" || t.text == "#{")
            return parseBracketLit();
    }

    if (t.kind == Tok::End) return nullptr;
    fail("ifade bekleniyordu, '" + (t.text.empty() ? std::string("?") : t.text) +
         "' bulundu");
    return nullptr;
}

NodePtr Parser::parseLambdaBody(const std::vector<std::string>& params, int line) {
    auto fn = mk(NK::FnDecl, line);
    fn->names = params;
    fn->str = "<lambda>";
    auto blk = mk(NK::Block, line);
    // Govde virgulle bitmez: parseTernary en dusuk oncelik seviyesidir.
    auto e = parseTernary();
    auto st = mk(NK::ExprStmt, line);
    st->add(std::move(e));
    blk->add(std::move(st));
    fn->body = std::move(blk);
    return fn;
}

NodePtr Parser::parseStringLit() {
    int ln = cur().line;

    if (cur().parts.size() == 1 && cur().parts[0].kind == StrPartKind::Text) {
        std::string text = cur().parts[0].text;
        p_++;
        while (at(Tok::Str) && cur().parts.size() == 1 &&
               cur().parts[0].kind == StrPartKind::Text) {
            text += cur().parts[0].text;
            p_++;
        }
        return mkStr(text, ln);
    }

    auto n = mk(NK::StrLit, ln);
    auto append = [&](const StrPart& part) {
        Node::StrSeg seg;
        seg.isExpr = (part.kind == StrPartKind::Expr);
        seg.text = part.text;
        if (seg.isExpr) seg.expr = parseStrSegment(part);
        n->segs.push_back(std::move(seg));
    };
    for (const auto& part : cur().parts) append(part);
    p_++;
    while (at(Tok::Str)) {
        for (const auto& part : cur().parts) append(part);
        p_++;
    }
    return n;
}

NodePtr Parser::parseStrSegment(const StrPart& part) {
    std::string s = part.text;
    size_t i = s.find_first_not_of(" \t");
    s = (i == std::string::npos) ? std::string() : s.substr(i);
    if (s.empty()) return mkNil(part.line);

    Parser sub(s, filename_);
    NodePtr e = sub.parseExpr(false);
    for (const auto& err : sub.errors())
        errors_.push_back({filename_ + ":" + std::to_string(part.line) + ": " + err.msg,
                           part.line, 1});
    return e;
}

NodePtr Parser::parseBracketLit() {
    int ln = cur().line;
    std::string open = cur().text;

    if (open == "[") {
        p_++;
        auto n = mk(NK::ListLit, ln);
        skipNewlinesAndCont();
        if (!atOp("]")) {
            do {
                skipNewlinesAndCont();
                if (atOp("]")) break;
                n->add(parseArg());
                skipNewlinesAndCont();
            } while (eatOp(",") && !atOp("]"));
        }
        expectOp("]");
        return n;
    }

    if (open == "#{") {
        p_++;
        auto n = mk(NK::SetLit, ln);
        skipNewlinesAndCont();
        if (!atOp("}")) {
            do {
                skipNewlinesAndCont();
                if (atOp("}")) break;
                n->add(parseArg());
                skipNewlinesAndCont();
            } while (eatOp(",") && !atOp("}"));
        }
        expectOp("}");
        return n;
    }

    // { ... }  ->  map
    p_++;
    auto n = mk(NK::MapLit, ln);
    skipNewlinesAndCont();
    if (atOp("}")) { p_++; return n; }
    do {
        skipNewlinesAndCont();
        if (atOp("}")) break;
        NodePtr key;
        if (at(Tok::Ident) && peekTok().kind == Tok::Op && peekTok().text == ":") {
            key = mkStr(cur().text, cur().line);
            p_++;
        } else {
            key = parseArg();
        }
        expectOp(":");
        skipNewlinesAndCont();
        NodePtr val = parseArg();
        n->add(std::move(key));
        n->add(std::move(val));
        skipNewlinesAndCont();
    } while (eatOp(",") && !atOp("}"));
    expectOp("}");
    return n;
}

// start: alt sinir (nullptr ise sadece ust sinir verilmis)
NodePtr Parser::parseRange(int line, NodePtr start) {
    bool exclusive = atOp("...");
    p_++;
    auto n = mk(NK::RangeLit, line);
    n->flag = exclusive;
    n->flag2 = (start != nullptr);

    NodePtr hi, step;
    if (!atOp(":") && !at(Tok::NewLine) && !atOp(")") && !atOp("]") &&
        !atOp("}") && !atOp(",") && !atOp(";") && !at(Tok::End))
        hi = parseExpr(false);
    if (eatOp(":")) {
        if (!at(Tok::NewLine) && !atOp(")") && !atOp("]") && !atOp("}") &&
            !at(Tok::End))
            step = parseExpr(false);
    }
    n->add(std::move(start));
    n->add(std::move(hi));
    n->add(std::move(step));
    return n;
}

} // namespace k7
