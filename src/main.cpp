// K7 Language - Giris noktasi (CLI + REPL)
#include "k7/interp.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using k7::Interp;
using k7::Value;
using k7::mkList;
using k7::mkStr;

const char* const kVersion = "K7 0.1.0";

void printHelp() {
    std::cout <<
        "K7 - modern, hizli programlama dili\n"
        "\n"
        "Kullanim:\n"
        "  k7                      Etkilesimli kabuk (REPL) baslatir\n"
        "  k7 <dosya.k7> [args]    Betigi calistirir\n"
        "  k7 -c \"<kod>\"          Kodu dogrudan calistirir\n"
        "  k7 -e <dosya.k7>        Dosyayi calistirir, sonra REPL acar\n"
        "  k7 -v, --version        Surum bilgisi\n"
        "  k7 -h, --help           Bu yardim metni\n"
        "\n"
        "Ornekler:\n"
        "  k7 merhaba.k7\n"
        "  k7 -c \"print('Merhaba K7')\"\n";
}

// Bir metnin parantez/susak/acik parantez dengesi ve acik string durumu.
// REPL'in cok satirli girdi toplamasini saglar.
struct Balance {
    int depth = 0;
    bool openString = false;
    char quote = 0;
    bool raw = false;
    int braceDepth = 0;
};

Balance scanBalance(const std::string& s) {
    Balance b;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (b.openString) {
            if (c == '\\' && !b.raw) { ++i; continue; }
            if (c == b.quote) b.openString = false;
            continue;
        }
        if (c == '#' && (i + 1 < s.size() && s[i + 1] == '{')) {
            ++b.depth; ++b.braceDepth; ++i;
            continue;
        }
        if (c == '#') {
            while (i < s.size() && s[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            int d = 1; i += 2;
            while (i + 1 < s.size() && d > 0) {
                if (s[i] == '*' && s[i + 1] == '/') { --d; ++i; }
                ++i;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            bool raw = (i > 0 && (s[i - 1] == 'r' || s[i - 1] == 'R'));
            // Uclu tirnak
            if (i + 2 < s.size() && s[i + 1] == c && s[i + 2] == c) {
                i += 3;
                while (i + 2 < s.size() &&
                       !(s[i] == c && s[i + 1] == c && s[i + 2] == c)) ++i;
                i += 2;
                continue;
            }
            b.openString = true; b.quote = c; b.raw = raw;
            continue;
        }
        if (c == '(' || c == '[') ++b.depth;
        else if (c == ')' || c == ']') --b.depth;
        else if (c == '{') { ++b.depth; ++b.braceDepth; }
        else if (c == '}') { --b.depth; --b.braceDepth; }
    }
    return b;
}

bool needsMore(const std::string& s) {
    Balance b = scanBalance(s);
    if (b.openString) return true;
    return b.depth > 0;
}

void replLoop(Interp& I) {
    std::cout << kVersion << "\n";
    std::cout << "Cikis icin 'exit', 'quit' veya Ctrl-D yazin.\n\n";

    std::string buffer;
    while (true) {
        if (buffer.empty()) std::cout << "k7> " << std::flush;
        else               std::cout << "... " << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            if (!buffer.empty()) {
                try { I.runInteractive(buffer); } catch (...) {}
            }
            return;
        }

        // REPL komutlari
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\r'))
            trimmed.pop_back();
        size_t b = trimmed.find_first_not_of(" \t");
        trimmed = (b == std::string::npos) ? "" : trimmed.substr(b);

        if (buffer.empty()) {
            if (trimmed == "exit" || trimmed == "quit") return;
            if (trimmed == "help") { printHelp(); std::cout << "\n"; continue; }
            if (trimmed == "clear") { std::cout << "\033[2J\033[H"; continue; }
        }

        if (!buffer.empty()) buffer += "\n";
        buffer += line;

        if (needsMore(buffer)) continue;

        if (!buffer.empty()) {
            try {
                I.runInteractive(buffer);
            } catch (const k7::ExitRequest&) {
                return;
            } catch (...) {
                std::cerr << "HATA: beklenmeyen hata\n";
            }
        }
        buffer.clear();
    }
}

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

void setupArgv(Interp& I, const std::vector<std::string>& args) {
    std::vector<Value> v;
    v.reserve(args.size());
    for (const auto& s : args) v.push_back(mkStr(s));
    I.globals()->define("argv", mkList(std::move(v)));
}

} // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    std::vector<std::string> args(argv + 1, argv + argc);

    bool wantRepl = false;
    std::string script;
    std::vector<std::string> scriptArgs;
    std::string inlineCode;
    bool hasInlineCode = false;
    std::string entryDir = ".";

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") { printHelp(); return 0; }
        if (a == "-v" || a == "--version") { std::cout << kVersion << "\n"; return 0; }
        if (a == "-c" || a == "--code") {
            if (i + 1 >= args.size()) {
                std::cerr << "HATA: -c icin kod metni gerekli\n";
                return 2;
            }
            inlineCode = args[++i];
            hasInlineCode = true;
            continue;
        }
        if (a == "-e") {
            if (i + 1 >= args.size()) {
                std::cerr << "HATA: -e icin dosya yolu gerekli\n";
                return 2;
            }
            script = args[++i];
            wantRepl = true;
            continue;
        }
        if (a == "--") {
            scriptArgs.assign(args.begin() + static_cast<long>(i) + 1, args.end());
            break;
        }
        if (!a.empty() && a[0] == '-') {
            std::cerr << "HATA: bilinmeyen secenek: " << a << "\n";
            return 2;
        }
        if (script.empty()) {
            script = a;
            for (size_t j = i + 1; j < args.size(); ++j) scriptArgs.push_back(args[j]);
            break;
        }
    }

    Interp I;
    I.registerBuiltins();
    I.registerModules();

    if (hasInlineCode) {
        I.replMode = false;
        std::vector<std::string> none;
        setupArgv(I, none);
        return I.runSource(inlineCode, "<command line>") ? 0 : 1;
    }

    if (!script.empty()) {
        I.replMode = false;
        std::vector<std::string> full;
        full.push_back(script);
        for (const auto& s : scriptArgs) full.push_back(s);
        setupArgv(I, full);
        entryDir = fs::path(script).parent_path().string();
        if (entryDir.empty()) entryDir = ".";

        std::error_code ec;
        if (!fs::exists(fs::path(script), ec)) {
            std::cerr << "HATA: dosya bulunamadi: " << script << "\n";
            return 2;
        }
        bool ok = false;
        std::string src = readFile(script, ok);
        if (!ok) {
            std::cerr << "HATA: dosya okunamadi: " << script << "\n";
            return 2;
        }
        bool good = I.runSource(src, script);
        if (!wantRepl) return good ? 0 : 1;
        if (!good) std::cerr << "\n";
        std::cout << "\n[REPL] " << script << " sonrasi etkilesimli mod\n";
        I.replMode = true;
        replLoop(I);
        return 0;
    }

    I.replMode = true;
    setupArgv(I, args);
    replLoop(I);
    return 0;
}
