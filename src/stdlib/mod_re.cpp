#include "k7/modules.h"

#include <cstring>
#include <regex>

namespace k7 {

namespace {

using RePtr = std::shared_ptr<std::regex>;

// Regex nesneleri GC destegi olmayan bir kutu icinde tutulur.
std::unordered_map<std::string, RePtr>& registry() {
    static std::unordered_map<std::string, RePtr> r;
    return r;
}

RePtr makeRe(Interp& I, std::vector<Value>& a, size_t i) {
    std::string pat = I.strOf(a[i], 0);
    auto& reg = registry();
    auto it = reg.find(pat);
    if (it != reg.end()) return it->second;
    try {
        auto r = std::make_shared<std::regex>(
            pat, std::regex::ECMAScript | std::regex::optimize);
        reg[pat] = r;
        return r;
    } catch (const std::regex_error& e) {
        I.error("RegexHatasi", "gecersiz regex: " + std::string(e.what()));
    }
}

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("re.") + name, std::move(fn), mn, mx);
}

} // namespace

void registerRegexModule(Interp& I) {
    I.registerModule("re", [&](ModuleObj& m) {
        add(m, "compile", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "re.compile(sekil) arguman ister");
            makeRe(I, a, 0);
            return mkStr(I.strOf(a[0], 0));
        }, 1, 1);
        add(m, "match", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "re.match(sekil, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string s = I.strOf(a[1], 0);
            std::smatch mm;
            if (!std::regex_search(s, mm, *r, std::regex_constants::match_continuous))
                return Value::nil();
            Value out = mkStr(mm.str());
            return out;
        }, 2, 2);
        add(m, "search", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "re.search(sekil, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string s = I.strOf(a[1], 0);
            std::smatch mm;
            if (!std::regex_search(s, mm, *r)) return Value::nil();
            return mkStr(mm.str());
        }, 2, 2);
        add(m, "fullmatch", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "re.fullmatch(sekil, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string s = I.strOf(a[1], 0);
            std::smatch mm;
            if (!std::regex_match(s, mm, *r)) return Value::nil();
            return mkStr(mm.str());
        }, 2, 2);
        add(m, "find_all", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "re.find_all(sekil, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string s = I.strOf(a[1], 0);
            std::vector<Value> out;
            for (auto it = std::sregex_iterator(s.begin(), s.end(), *r);
                 it != std::sregex_iterator(); ++it)
                out.push_back(mkStr(it->str()));
            return mkList(std::move(out));
        }, 2, 2);
        add(m, "replace", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 3) I.error("TipHatasi", "re.replace(sekil, degistirici, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string rep = I.strOf(a[1], 0);
            std::string s = I.strOf(a[2], 0);
            return mkStr(std::regex_replace(s, *r, rep,
                                            std::regex_constants::format_first_only));
        }, 3, 3);
        add(m, "replace_all", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 3) I.error("TipHatasi", "re.replace_all(sekil, degistirici, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string rep = I.strOf(a[1], 0);
            std::string s = I.strOf(a[2], 0);
            return mkStr(std::regex_replace(s, *r, rep));
        }, 3, 3);
        add(m, "split", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "re.split(sekil, metin) arguman ister");
            auto r = makeRe(I, a, 0);
            std::string s = I.strOf(a[1], 0);
            std::vector<Value> out;
            std::sregex_token_iterator it(s.begin(), s.end(), *r, -1), end;
            for (; it != end; ++it) out.push_back(mkStr(*it));
            return mkList(std::move(out));
        }, 2, 2);
        add(m, "escape", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "re.escape(metin) arguman ister");
            std::string s = I.strOf(a[0], 0);
            std::string out;
            for (char c : s) {
                if (std::strchr(".^$|()[]{}*+?\\/", c)) out += '\\';
                out += c;
            }
            return mkStr(out);
        }, 1, 1);
    });
}

} // namespace k7
