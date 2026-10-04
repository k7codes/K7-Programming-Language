#include "k7/lexer.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace k7 {

namespace {

const std::unordered_set<std::string>& keywords() {
    static const std::unordered_set<std::string> kw = {
        "fn", "var", "let", "const", "return", "if", "elif", "else",
        "while", "for", "in", "break", "continue", "class", "self",
        "this", "true", "false", "nil", "null", "and", "or", "not",
        "import", "as", "match", "case", "try", "catch", "throw",
        "loop", "defer", "enum", "do", "then", "yield", "super",
        "static", "use"
    };
    return kw;
}

// Coklu karakterli operatorler, uzun olandan kisa oana.
const char* const kOps[] = {
    "**=", "<<=", ">>=", "...", "#{", "++", "--",
    "**", "==", "!=", "<=", ">=", "->", "=>", "|>", "?.", "??",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>",
    "+", "-", "*", "/", "%", "=", "<", ">", "(", ")", "[", "]",
    "{", "}", ",", ":", ".", ";", "!", "&", "|", "^", "~", "?", "@", "$",
    nullptr
};

bool identStart(int c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool identCont(int c)  { return std::isalnum(c) || c == '_' || c >= 0x80; }

} // namespace

Lexer::Lexer(const std::string& source, const std::string& filename)
    : src_(source), filename_(filename) {
    // UTF-8 BOM'u atla
    if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB && static_cast<unsigned char>(src_[2]) == 0xBF)
        pos_ = 3;
    indents_.push_back(0);
}

int Lexer::peek(int off) const {
    size_t p = pos_ + static_cast<size_t>(off);
    return p < src_.size() ? static_cast<unsigned char>(src_[p]) : -1;
}

int Lexer::advance() {
    if (pos_ >= src_.size()) return -1;
    char c = src_[pos_++];
    if (c == '\n') { line_++; lineStart_ = static_cast<int>(pos_); }
    return static_cast<unsigned char>(c);
}

bool Lexer::match(char c) {
    if (cur() == c) { advance(); return true; }
    return false;
}

bool Lexer::matchStr(const char* s) {
    size_t n = std::strlen(s);
    if (src_.compare(pos_, n, s) == 0) {
        for (size_t i = 0; i < n; ++i) advance();
        return true;
    }
    return false;
}

void Lexer::err(const std::string& m, int l, int c) {
    errors_.push_back({m, l, c});
}

void Lexer::pushOp(Token& t, const char* text, int l, int c) {
    t.kind = Tok::Op;
    t.text = text;
    t.line = l;
    t.col = c;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    bool prevSignificant = false;

    // Satir basinda INDENT/DEDENT uretimi
    auto lineStartEmit = [&](int level) {
        if (level > indents_.back()) {
            indents_.push_back(level);
            Token t; t.kind = Tok::Indent; t.line = line_; t.col = 1;
            out.push_back(t);
        } else {
            while (level < indents_.back()) {
                indents_.pop_back();
                Token t; t.kind = Tok::Dedent; t.line = line_; t.col = 1;
                out.push_back(t);
            }
            if (level != indents_.back()) {
                // Tutarsiz girinti -> en yakina yuvarla, hata bildir
                err("Tutarsiz girinti seviyesi", line_, 1);
                indents_.push_back(level);
            }
        }
        prevSignificant = false;
    };

    while (true) {
        if (atEnd()) break;

        if (atLineStart_ && parenDepth_ == 0) {
            // Satir basindaki bosluklari olc
            int level = 0;
            while (!atEnd() && (cur() == ' ' || cur() == '\t')) {
                if (cur() == '\t') level += 4 - (level % 4);
                else ++level;
                advance();
            }
            if ((cur() == '#' && peek(1) != '{') ||
                (cur() == '/' && (peek(1) == '*' || peek(1) == '/'))) {
                skipToEol();  // yorum satiri girinti uretmez
                continue;
            }
            if (cur() == '\n') { advance(); continue; }  // bos satir
            if (cur() == '\r') { advance(); continue; }
            if (atEnd()) break;
            lineStartEmit(level);
            atLineStart_ = false;
        }

        int c = cur();
        if (c < 0) break;

        // Yorumlar
        if (c == '#' && peek(1) != '{') { skipToEol(); continue; }
        if (c == '/' && peek(1) == '/') { skipToEol(); continue; }
        if (c == '/' && peek(1) == '*') {
            int sl = line_, sc = col();
            advance(); advance();
            int depth = 1;
            while (!atEnd() && depth > 0) {
                if (cur() == '/' && peek(1) == '*') { advance(); advance(); ++depth; }
                else if (cur() == '*' && peek(1) == '/') { advance(); advance(); --depth; }
                else advance();
            }
            if (depth > 0) err("Kapatilmamis blok yorum", sl, sc);
            continue;
        }

        // Satir sonu
        if (c == '\n') {
            int l = line_, cc = col();
            advance();
            if (parenDepth_ == 0) {
                atLineStart_ = true;
                // Anlamli token'dan sonra bosluk/indentation varsa NewLine uret
                // (bos satirlar ve yorum satirlari atlanir)
                bool hasNext = false;
                for (size_t p = pos_; p < src_.size(); ++p) {
                    int q = static_cast<unsigned char>(src_[p]);
                    if (q == '\r' || q == ' ' || q == '\t') continue;
                    if (q == '\n') continue;                       // bos satir
                    if (q == '#' && !(p + 1 < src_.size() && src_[p + 1] == '{')) {
                        while (p < src_.size() && src_[p] != '\n') ++p;   // yorum satiri
                        --p;
                        continue;
                    }
                    if (q == '/' && p + 1 < src_.size() && src_[p + 1] == '/') {
                        while (p < src_.size() && src_[p] != '\n') ++p;
                        --p;
                        continue;
                    }
                    if (q == '/' && p + 1 < src_.size() && src_[p + 1] == '*') {
                        p += 2;
                        while (p + 1 < src_.size() && !(src_[p] == '*' && src_[p + 1] == '/')) ++p;
                        ++p;
                        continue;
                    }
                    hasNext = true;
                    break;
                }
                if (hasNext) {
                    Token t; t.kind = Tok::NewLine; t.line = l; t.col = cc;
                    out.push_back(t);
                }
            }
            continue;
        }
        if (c == '\r') { advance(); continue; }
        if (c == ' ' || c == '\t') { advance(); continue; }
        if (c == '\\' && peek(1) == '\n') { advance(); advance(); continue; }  // line cont.

        prevSignificant = true;
        (void)prevSignificant;

        // Sayilar
        if (std::isdigit(c) || (c == '.' && std::isdigit(peek(1)))) {
            Token t;
            if (!readNumber(t)) continue;
            out.push_back(std::move(t));
            continue;
        }

        // String'ler
        if (c == '"' || c == '\'') {
            Token t;
            if (!readString(t)) continue;
            out.push_back(std::move(t));
            continue;
        }
        if ((c == 'r' || c == 'R') && (peek(1) == '"' || peek(1) == '\'')) {
            Token t;
            advance();
            readString(t);
            t.line = line_;
            out.push_back(std::move(t));
            continue;
        }

        // Kimlikler / anahtar sozcukler
        if (identStart(c)) {
            Token t;
            if (!readIdent(t)) continue;
            out.push_back(std::move(t));
            continue;
        }

        // Operatorler
        bool matched = false;
        int l = line_, cc = col();
        for (int i = 0; kOps[i]; ++i) {
            if (matchStr(kOps[i])) {
                Token t;
                pushOp(t, kOps[i], l, cc);
                if (t.text == "(" || t.text == "[") ++parenDepth_;
                else if (t.text == ")" || t.text == "]") {
                    if (parenDepth_ > 0) --parenDepth_;
                } else if (t.text == "{") ++braceDepth_;
                else if (t.text == "}") { if (braceDepth_ > 0) --braceDepth_; }
                out.push_back(std::move(t));
                matched = true;
                break;
            }
        }
        if (!matched) {
            std::string ch(1, static_cast<char>(c));
            err("Bilinmeyen karakter: '" + ch + "'", l, cc);
            advance();
        }
    }

    // Dosya sonu: bekleyen girintileri kapat
    if (!atLineStart_ && parenDepth_ == 0) {
        Token t; t.kind = Tok::NewLine; t.line = line_; t.col = col();
        out.push_back(t);
    }
    for (size_t i = indents_.size(); i > 1; --i) {
        Token t; t.kind = Tok::Dedent; t.line = line_; t.col = col();
        out.push_back(t);
    }
    Token end; end.kind = Tok::End; end.line = line_; end.col = col();
    out.push_back(end);
    return out;
}

void Lexer::skipToEol() {
    while (!atEnd() && cur() != '\n') advance();
}

bool Lexer::readNumber(Token& t) {
    int l = line_, cc = col();
    std::string raw;
    bool isFloat = false;

    // Base prefix'leri
    if (cur() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        advance(); advance();
        std::string hex;
        while (!atEnd() && (std::isxdigit(cur()) || cur() == '_')) {
            if (cur() != '_') hex += static_cast<char>(cur());
            advance();
        }
        if (hex.empty()) { err("Gecersiz onaltilik sayi", l, cc); return false; }
        t.kind = Tok::Int;
        t.ival = static_cast<int64_t>(std::strtoull(hex.c_str(), nullptr, 16));
        t.line = l; t.col = cc;
        return true;
    }
    if (cur() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        advance(); advance();
        std::string bin;
        while (!atEnd() && (cur() == '0' || cur() == '1' || cur() == '_')) {
            if (cur() != '_') bin += static_cast<char>(cur());
            advance();
        }
        if (bin.empty()) { err("Gecersiz ikilik sayi", l, cc); return false; }
        t.kind = Tok::Int;
        t.ival = static_cast<int64_t>(std::strtoull(bin.c_str(), nullptr, 2));
        t.line = l; t.col = cc;
        return true;
    }
    if (cur() == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
        advance(); advance();
        std::string oct;
        while (!atEnd() && ((cur() >= '0' && cur() <= '7') || cur() == '_')) {
            if (cur() != '_') oct += static_cast<char>(cur());
            advance();
        }
        if (oct.empty()) { err("Gecersiz sekizlik sayi", l, cc); return false; }
        t.kind = Tok::Int;
        t.ival = static_cast<int64_t>(std::strtoull(oct.c_str(), nullptr, 8));
        t.line = l; t.col = cc;
        return true;
    }

    while (!atEnd() && (std::isdigit(cur()) || cur() == '_')) {
        if (cur() != '_') raw += static_cast<char>(cur());
        advance();
    }
    if (cur() == '.' && std::isdigit(peek(1))) {
        isFloat = true;
        raw += '.';
        advance();
        while (!atEnd() && (std::isdigit(cur()) || cur() == '_')) {
            if (cur() != '_') raw += static_cast<char>(cur());
            advance();
        }
    }
    if (cur() == 'e' || cur() == 'E') {
        int nx = peek(1);
        if (std::isdigit(nx) || ((nx == '+' || nx == '-') && std::isdigit(peek(2)))) {
            isFloat = true;
            raw += 'e';
            advance();
            if (cur() == '+' || cur() == '-') { raw += static_cast<char>(cur()); advance(); }
            while (!atEnd() && std::isdigit(cur())) { raw += static_cast<char>(cur()); advance(); }
        }
    }
    if (raw.empty()) { err("Gecersiz sayi", l, cc); return false; }

    t.line = l; t.col = cc;
    if (isFloat) {
        t.kind = Tok::Float;
        t.fval = std::strtod(raw.c_str(), nullptr);
    } else {
        t.kind = Tok::Int;
        errno = 0;
        char* endp = nullptr;
        unsigned long long v = std::strtoull(raw.c_str(), &endp, 10);
        if (errno == ERANGE) {
            t.kind = Tok::Float;
            t.fval = std::strtod(raw.c_str(), nullptr);
        } else {
            t.ival = static_cast<int64_t>(v);
        }
    }
    return true;
}

bool Lexer::readIdent(Token& t) {
    int l = line_, cc = col();
    std::string s;
    while (!atEnd() && identCont(cur())) { s += static_cast<char>(advance()); }
    t.kind = Tok::Ident;
    t.text = s;
    t.line = l; t.col = cc;
    return true;
}

void Lexer::readTripleQuoted(Token& t) {
    int l = line_, cc = col();
    advance(); advance(); advance();  // """
    readInterpolated(t, '"', 3);
    t.kind = Tok::Str;
    t.line = l; t.col = cc;
}

void Lexer::readInterpolated(Token& t, char quote, int triple) {
    // Icinde ${expr} veya $ident bulunan string icerigi
    std::string lit;
    int litLine = line_;
    if (!triple) t.parts.push_back(StrPart{});  // ilk bos literal yer tutucu

    auto atEnd_ = [&]() {
        if (triple) return cur() == quote && peek(1) == quote && peek(2) == quote;
        return cur() == quote;
    };

    auto flushLit = [&]() {
        if (!lit.empty()) {
            StrPart p; p.kind = StrPartKind::Text; p.text = lit; p.line = litLine;
            t.parts.push_back(std::move(p));
            lit.clear();
            litLine = line_;
        }
    };

    while (!atEnd() && !atEnd_()) {
        if (cur() == '\\') {
            int l = line_, c = col();
            advance();
            int e = advance();
            switch (e) {
                case 'n': lit += '\n'; break;
                case 't': lit += '\t'; break;
                case 'r': lit += '\r'; break;
                case '0': lit += '\0'; break;
                case '\\': lit += '\\'; break;
                case '"': lit += '"'; break;
                case '\'': lit += '\''; break;
                case '$': lit += '$'; break;
                case '{': lit += '{'; break;
                case '\n': break;  // satir devam
                case 'u': {
                    std::string hex;
                    for (int i = 0; i < 4 && !atEnd(); ++i) {
                        if (std::isxdigit(cur())) hex += static_cast<char>(advance());
                        else advance();
                    }
                    if (hex.size() == 4) {
                        unsigned cp = static_cast<unsigned>(std::strtoul(hex.c_str(), nullptr, 16));
                        // UTF-8 encode
                        if (cp < 0x80) lit += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            lit += static_cast<char>(0xC0 | (cp >> 6));
                            lit += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            lit += static_cast<char>(0xE0 | (cp >> 12));
                            lit += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            lit += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default:
                    err(std::string("Bilinmeyen kacis: \\") + static_cast<char>(e), l, c);
                    break;
            }
            continue;
        }
        if (cur() == '$' && (peek(1) == '{')) {
            flushLit();
            advance(); advance();
            int el = line_, ec = col();
            std::string expr;
            int depth = 1;
            while (!atEnd() && depth > 0) {
                int ch = cur();
                if (ch == '{') ++depth;
                else if (ch == '}') { --depth; if (depth == 0) { advance(); break; } }
                else if (ch == '"' || ch == '\'') {
                    char q = static_cast<char>(advance());
                    expr += q;
                    while (!atEnd() && cur() != q) {
                        if (cur() == '\\') expr += static_cast<char>(advance());
                        expr += static_cast<char>(advance());
                    }
                }
                expr += static_cast<char>(advance());
            }
            StrPart p; p.kind = StrPartKind::Expr; p.text = expr; p.line = el;
            t.parts.push_back(std::move(p));
            litLine = line_;
            continue;
        }
        if (cur() == '$' && identStart(peek(1))) {
            flushLit();
            advance();
            std::string name;
            while (!atEnd() && identCont(cur())) name += static_cast<char>(advance());
            StrPart p; p.kind = StrPartKind::Expr; p.text = name; p.line = line_;
            t.parts.push_back(std::move(p));
            litLine = line_;
            continue;
        }
        lit += static_cast<char>(advance());
    }
    if (triple) {
        if (!atEnd()) { advance(); advance(); advance(); }
    } else if (cur() == quote) {
        advance();
    }
    flushLit();
    if (!t.parts.empty() && t.parts[0].kind == StrPartKind::Text && t.parts[0].text.empty())
        t.parts.erase(t.parts.begin());
}

bool Lexer::readString(Token& t) {
    int l = line_, cc = col();

    // Uclu tirnak: cok satirli
    if (cur() == '"' && peek(1) == '"' && peek(2) == '"') { readTripleQuoted(t); return true; }
    if (cur() == '\'' && peek(1) == '\'' && peek(2) == '\'') { readTripleQuoted(t); return true; }

    char q = static_cast<char>(advance());
    readInterpolated(t, q);
    t.kind = Tok::Str;
    t.line = l;
    t.col = cc;
    return true;
}

} // namespace k7
