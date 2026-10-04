// K7 Language - Lexer
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace k7 {

enum class Tok : uint8_t {
    End,        // dosya sonu
    NewLine,    // satir sonu (statement ayirici)
    Indent,     // indentation artisi
    Dedent,     // indentation azalisi
    Int,        // tam sayi literal
    Float,      // ondalik literal
    Str,        // string literal (parcalarla)
    Ident,      // kimlik / anahtar sozcuk
    Op,         // operator / sembol
};

enum class StrPartKind : uint8_t { Text, Expr };

struct StrPart {
    StrPartKind kind = StrPartKind::Text;
    std::string text;   // Text: literal parca, Expr: kaynak kod parcasi
    int line = 0;
};

struct Token {
    Tok kind = Tok::End;
    std::string text;               // Ident/Op icin deger
    int64_t ival = 0;
    double fval = 0;
    std::vector<StrPart> parts;     // Str icin parcalar
    int line = 1;
    int col = 1;
};

struct LexError {
    std::string msg;
    int line;
    int col;
};

class Lexer {
public:
    Lexer(const std::string& source, const std::string& filename = "<stdin>");

    // Tum token'lari uretir (sonunda Indent/Dedent/End dahil).
    std::vector<Token> tokenize();

    const std::vector<LexError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }
    const std::string& filename() const { return filename_; }

private:
    const std::string& src_;
    std::string filename_;
    size_t pos_ = 0;
    int line_ = 1;
    int lineStart_ = 0;
    int parenDepth_ = 0;   // ( [ icinde satir sonu/indent gecerli degil
int braceDepth_ = 0;   // { } icinde gecerli (blok satirleri ayri)
    std::vector<int> indents_;
    bool atLineStart_ = true;
    bool sawNewline_ = false;
    std::vector<LexError> errors_;

    int peek(int off = 0) const;
    int cur() const { return peek(0); }
    int col() const { return static_cast<int>(pos_) - lineStart_ + 1; }
    int advance();
    bool atEnd() const { return pos_ >= src_.size(); }
    bool match(char c);
    bool matchStr(const char* s);
    void err(const std::string& m, int l, int c);

    void skipToEol();
    void handleLineStart();
    void pushOp(Token& t, const char* text, int l, int c);
    bool readNumber(Token& t);
    bool readIdent(Token& t);
    bool readString(Token& t);
    void readTripleQuoted(Token& t);
    void readInterpolated(Token& t, char quote, int triple = 0);
};

} // namespace k7