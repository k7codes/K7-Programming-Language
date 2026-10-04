// Gecici arac: token akisini gosterir (derleme denetimi icin)
#include "k7/lexer.h"

#include <iostream>
#include <string>

static const char* kindName(k7::Tok t) {
    switch (t) {
        case k7::Tok::End:     return "END";
        case k7::Tok::NewLine: return "NL";
        case k7::Tok::Indent:  return "IND";
        case k7::Tok::Dedent:  return "DED";
        case k7::Tok::Ident:   return "ID";
        case k7::Tok::Int:     return "INT";
        case k7::Tok::Float:   return "FLT";
        case k7::Tok::Str:     return "STR";
        case k7::Tok::Op:      return "OP";
    }
    return "?";
}

int main() {
    std::string src, line;
    while (std::getline(std::cin, line)) { src += line; src += "\n"; }
    k7::Lexer lx(src, "<stdin>");
    auto toks = lx.tokenize();
    for (const auto& t : toks)
        std::cout << t.line << ":" << t.col << " " << kindName(t.kind)
                  << " '" << t.text << "'\n";
    for (const auto& e : lx.errors())
        std::cout << "HATA " << e.line << ":" << e.col << " " << e.msg << "\n";
    return 0;
}