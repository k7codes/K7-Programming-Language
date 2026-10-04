// K7 Language - AST
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace k7 {

enum class NK : uint8_t {
    // --- Degerler ---
    IntLit, FloatLit, StrLit, BoolLit, NilLit, Ident, SelfExpr, ThisExpr,

    // --- Koleksiyonlar ---
    ListLit, MapLit, TupleLit, RangeLit, SetLit,

    // --- Operatorler ---
    Unary, Binary, Logical, CompareChain, Assign, Pipe, Ternary,
    Call, MethodCall, Get, SafeGet, Index, Slice, Attr,

    // --- Kontrol akisi ---
    Block, If, While, Loop, For, Break, Continue, Return,
    Match, CasePattern, CaseClause,

    // --- Tanimlar ---
    VarDecl, FnDecl, ClassDecl, EnumDecl, EnumMember, ImportDecl, Assert,

    // --- Hata yonetimi ---
    Try, Throw,

    // --- Yardimci ---
    Program, ExprStmt
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NK kind;
    int line = 0;

    // Skaler yukler
    int64_t ival = 0;
    double fval = 0;
    std::string str;   // ad, operator, metot adi, string degeri
    std::string typeName;
    std::vector<std::string> paramTypes;
    std::string returnType;

    // Cocuklar
    std::vector<NodePtr> kids;

    // Govde (FnDecl icin; kids varsayilan degerler icin kullanilir)
    NodePtr body;

    // Coklu ad (fonksiyon parametreleri, enum uyeleri)
    std::vector<std::string> names;

    // String interpolasyon parcalari: null ise str kullanilir
    struct StrSeg {
        bool isExpr = false;
        NodePtr expr;
        std::string text;
    };
    std::vector<StrSeg> segs;

    // Kisa bayraklar
    bool flag = false;   // var/let, neg (defer), vb.
    bool flag2 = false;  // unary ! (not), vs.

    explicit Node(NK k, int ln = 0) : kind(k), line(ln) {}

    Node* kid(size_t i) const {
        return i < kids.size() ? kids[i].get() : nullptr;
    }
    void add(NodePtr n) { kids.push_back(std::move(n)); }
};

// Yardimci ureticiler
inline NodePtr mk(NK k, int line) { return std::make_unique<Node>(k, line); }
inline NodePtr mkInt(int64_t v, int line) {
    auto n = mk(NK::IntLit, line); n->ival = v; return n;
}
inline NodePtr mkFloat(double v, int line) {
    auto n = mk(NK::FloatLit, line); n->fval = v; return n;
}
inline NodePtr mkStr(const std::string& s, int line) {
    auto n = mk(NK::StrLit, line); n->str = s; return n;
}
inline NodePtr mkIdent(const std::string& s, int line) {
    auto n = mk(NK::Ident, line); n->str = s; return n;
}
inline NodePtr mkBool(bool b, int line) {
    auto n = mk(NK::BoolLit, line); n->flag = b; return n;
}
inline NodePtr mkNil(int line) { return mk(NK::NilLit, line); }

// Derin kopya (zincir karsilastirma gibi yerde ayni alt-ifadeyi birden fazla
// yerde kullanmak icin gerekli).
NodePtr cloneNode(const Node* n);

} // namespace k7
