#include "k7/modules.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace k7 {

namespace {

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("str.") + name, std::move(fn), mn, mx);
}

std::string s0(Interp& I, std::vector<Value>& a) {
    if (a.empty()) I.error("TipHatasi", "str fonksiyonu metin argumani ister");
    return I.strOf(a[0], 0);
}

} // namespace

void registerStrModule(Interp& I) {
    I.registerModule("str", [&](ModuleObj& m) {
        add(m, "format", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "str.format(sekil, ...) arguman ister");
            std::vector<Value> rest(a.begin() + 1, a.end());
            return mkStr(I.formatString(I.strOf(a[0], 0), mkList(std::move(rest)), 0));
        }, 1, -1);
        add(m, "upper", [](Interp& I, std::vector<Value>& a) {
            std::string s = s0(I, a);
            for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return mkStr(s);
        }, 1, 1);
        add(m, "lower", [](Interp& I, std::vector<Value>& a) {
            std::string s = s0(I, a);
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return mkStr(s);
        }, 1, 1);
        add(m, "join", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.join(ayrac, dizi) arguman ister");
            std::string sep = I.strOf(a[0], 0);
            std::string out;
            bool first = true;
            I.seqIterate(a[1], [&](const Value& v) {
                if (!first) out += sep;
                first = false;
                out += I.strOf(v, 0);
            });
            return mkStr(out);
        }, 2, 2);
        add(m, "repeat", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.repeat(metin, n) arguman ister");
            std::string s = s0(I, a);
            int64_t n = I.toInt(a[1], 0).i;
            std::string out;
            for (int64_t i = 0; i < n; ++i) out += s;
            return mkStr(out);
        }, 2, 2);
        add(m, "pad_start", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.pad_start(metin, n[, dolgu]) arguman ister");
            std::string s = s0(I, a);
            std::string pad = a.size() > 2 ? I.strOf(a[2], 0) : " ";
            size_t n = static_cast<size_t>(I.toInt(a[1], 0).i);
            if (pad.empty()) return mkStr(s);
            while (s.size() < n) s.insert(0, pad);
            return mkStr(s);
        }, 2, 3);
        add(m, "pad_end", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.pad_end(metin, n[, dolgu]) arguman ister");
            std::string s = s0(I, a);
            std::string pad = a.size() > 2 ? I.strOf(a[2], 0) : " ";
            size_t n = static_cast<size_t>(I.toInt(a[1], 0).i);
            if (pad.empty()) return mkStr(s);
            while (s.size() < n) s += pad;
            return mkStr(s);
        }, 2, 3);
        add(m, "center", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.center(metin, n[, dolgu]) arguman ister");
            std::string s = s0(I, a);
            std::string pad = a.size() > 2 ? I.strOf(a[2], 0) : " ";
            size_t n = static_cast<size_t>(I.toInt(a[1], 0).i);
            if (pad.empty() || s.size() >= n) return mkStr(s);
            size_t total = n - s.size();
            size_t left = total / 2;
            std::string ls, rs;
            while (ls.size() < left) ls += pad;
            while (rs.size() < total - left) rs += pad;
            return mkStr(ls.substr(0, left) + s + rs.substr(0, total - left));
        }, 2, 3);
        add(m, "truncate", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.truncate(metin, n[, ek]) arguman ister");
            std::string s = s0(I, a);
            size_t n = static_cast<size_t>(I.toInt(a[1], 0).i);
            std::string ell = a.size() > 2 ? I.strOf(a[2], 0) : "...";
            if (s.size() <= n) return mkStr(s);
            if (ell.size() >= n) return mkStr(s.substr(0, n));
            return mkStr(s.substr(0, n - ell.size()) + ell);
        }, 2, 3);
        add(m, "title_case", [](Interp& I, std::vector<Value>& a) {
            std::string s = s0(I, a);
            bool cap = true;
            for (auto& c : s) {
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    c = static_cast<char>(cap ? std::toupper(static_cast<unsigned char>(c))
                                              : std::tolower(static_cast<unsigned char>(c)));
                    cap = false;
                } else cap = true;
            }
            return mkStr(s);
        }, 1, 1);
        add(m, "wrap", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.wrap(metin, genislik[, ayirici]) arguman ister");
            const std::string s = s0(I, a);
            size_t w = static_cast<size_t>(I.toInt(a[1], 0).i);
            std::string sep = a.size() > 2 ? I.strOf(a[2], 0) : " ";
            std::vector<Value> out;
            std::string cur;
            std::istringstream is(s);
            std::string word;
            while (is >> word) {
                if (cur.empty()) {
                    cur = word;
                } else if (cur.size() + 1 + word.size() <= w) {
                    cur += sep + word;
                } else {
                    out.push_back(mkStr(cur));
                    cur = word;
                }
            }
            if (!cur.empty()) out.push_back(mkStr(cur));
            return mkList(std::move(out));
        }, 2, 3);
        add(m, "levenshtein", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "str.levenshtein(a, b) arguman ister");
            std::string x = s0(I, a), y = I.strOf(a[1], 0);
            std::vector<size_t> prev(y.size() + 1), cur(y.size() + 1);
            for (size_t j = 0; j <= y.size(); ++j) prev[j] = j;
            for (size_t i = 1; i <= x.size(); ++i) {
                cur[0] = i;
                for (size_t j = 1; j <= y.size(); ++j) {
                    size_t cost = (x[i - 1] == y[j - 1]) ? 0 : 1;
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
                }
                prev = cur;
            }
            return Value::integer(static_cast<int64_t>(prev[y.size()]));
        }, 2, 2);
    });
}

} // namespace k7