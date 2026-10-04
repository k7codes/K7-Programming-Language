// K7 Language - Dahili fonksiyonlar ve metotlar
#include "k7/interp.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace k7 {

// ===============================================================
// Yardimcilar
// ===============================================================

static Value arg(std::vector<Value>& a, size_t i) {
    return i < a.size() ? a[i] : Value::nil();
}

static void needArgs(Interp& I, std::vector<Value>& a, size_t n, const char* who,
                     const char* msg) {
    if (a.size() < n) I.error("TipHatasi", who, 0);
    (void)msg;
}

static std::vector<Value> toList(Interp& I, const Value& v) {
    std::vector<Value> out;
    if (v.isSeq() || v.isSet() || v.isMap() || v.isRange()) {
        I.iterate(v, [&](const Value& x) { out.push_back(x); });
        return out;
    }
    I.error("TipHatasi", I.typeNameOf(v) + " turu listeye cevrilemez", 0);
    return out;
}

static void outList(Interp& I, ModuleObj& m, const char* name,
                    std::vector<Value> items) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkList(std::move(items));
}

static void outFn(Interp& I, ModuleObj& m, const char* name, NativeFn fn,
                  int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(name, std::move(fn), mn, mx);
    (void)I;
}

// ===============================================================
// Ust duzey fonksiyonlar
// ===============================================================

static Value applyFn(Interp& I, const Value& fn, const std::vector<Value>& args) {
    if (!fn.isFn()) {
        I.error("TipHatasi", I.typeNameOf(fn) + " turu cagrilabilir degil", 0);
    }
    std::vector<Value> a(args.begin(), args.end());
    return I.callValue(fn, std::move(a), 0);
}

static int compareForSort(Interp& I, const Value& a, const Value& b) {
    int c = compareValues(a, b);
    if (c == -2) I.error("TipHatasi", I.typeNameOf(a) + " ile " + I.typeNameOf(b) +
                         " siralanamaz", 0);
    return c;
}

void Interp::registerBuiltins() {
    Env& G = *globals_;

    // ---------- Cikti ----------
    G.define("print", mkNative("print", [](Interp& I, std::vector<Value>& a) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (i) std::cout << " ";
            std::cout << I.strOf(a[i], 0);
        }
        std::cout << "\n" << std::flush;
        return Value::nil();
    }, 0, -1));

    G.define("printn", mkNative("printn", [](Interp& I, std::vector<Value>& a) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (i) std::cout << " ";
            std::cout << I.strOf(a[i], 0);
        }
        std::cout << std::flush;
        return Value::nil();
    }, 0, -1));

    G.define("echo", mkNative("echo", [](Interp& I, std::vector<Value>& a) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (i) std::cout << " ";
            std::cout << valueToRepr(a[i]);
        }
        std::cout << "\n" << std::flush;
        return Value::nil();
    }, 0, -1));

    G.define("input", mkNative("input", [](Interp& I, std::vector<Value>& a) {
        if (!a.empty()) { std::cout << I.strOf(a[0], 0) << std::flush; }
        std::string line;
        if (!std::getline(std::cin, line)) throw QuitSignal{};
        return mkStr(line);
    }, 0, -1));

    // ---------- Tur fonksiyonlari ----------
    G.define("len", mkNative("len", [](Interp& I, std::vector<Value>& a) {
        if (a.empty()) I.error("TipHatasi", "len() en az 1 arguman ister", 0);
        return Value::integer(I.seqLength(a[0]));
    }, 1, 1));

    G.define("type", mkNative("type", [](Interp& I, std::vector<Value>& a) {
        return mkStr(I.typeNameOf(a.empty() ? Value::nil() : a[0]));
    }, 0, 1));

    G.define("str", mkNative("str", [](Interp& I, std::vector<Value>& a) {
        if (a.empty()) return mkStr(std::string());
        return mkStr(I.strOf(a[0], 0));
    }, 0, 1));

    G.define("repr", mkNative("repr", [](Interp& I, std::vector<Value>& a) {
        return mkStr(valueToRepr(arg(a, 0)));
    }, 0, 1));

    G.define("int", mkNative("int", [](Interp& I, std::vector<Value>& a) {
        return I.toInt(arg(a, 0), 0);
    }, 0, 1));

    G.define("float", mkNative("float", [](Interp& I, std::vector<Value>& a) {
        return I.toFloat(arg(a, 0), 0);
    }, 0, 1));

    G.define("bool", mkNative("bool", [](Interp& I, std::vector<Value>& a) {
        return Value::boolean(valueTruthy(arg(a, 0)));
    }, 0, 1));

    G.define("list", mkNative("list", [](Interp& I, std::vector<Value>& a) {
        if (a.empty()) return mkList();
        return mkList(toList(I, a[0]));
    }, 0, 1));

    G.define("tuple", mkNative("tuple", [](Interp& I, std::vector<Value>& a) {
        if (a.empty()) return mkTuple({});
        return mkTuple(toList(I, a[0]));
    }, 0, 1));

    G.define("set", mkNative("set", [](Interp& I, std::vector<Value>& a) {
        auto s = mkSet();
        if (!a.empty())
            I.iterate(a[0], [&](const Value& v) { asSet(s)->add(v); });
        return s;
    }, 0, 1));

    G.define("dict", mkNative("dict", [](Interp& I, std::vector<Value>& a) {
        auto m = mkMap();
        if (a.empty() || a[0].isNil()) return m;
        if (a[0].isMap()) {
            for (auto& kv : asMap(a[0])->items) asMap(m)->set(kv.first, kv.second);
            return m;
        }
        auto* mp = asMap(m);
        I.iterate(a[0], [&](const Value& pair) {
            Value k, v;
            if (I.unpackPair(pair, k, v, 0)) mp->items.emplace_back(k, v);
        });
        mp->reindex();
        return m;
    }, 0, 1));

    G.define("range", mkNative("range", [](Interp& I, std::vector<Value>& a) {
        auto r = std::make_shared<RangeObj>();
        r->hasStart = true;
        if (a.empty()) I.error("TipHatasi", "range() en az 1 arguman ister", 0);
        if (a.size() == 1) {
            r->stop = I.toInt(a[0], 0).i;
        } else {
            r->start = I.toInt(a[0], 0).i;
            r->stop = I.toInt(a[1], 0).i;
            if (a.size() > 2) r->step = I.toInt(a[2], 0).i;
        }
        if (r->step == 0) I.error("Hata", "range() adimi 0 olamaz", 0);
        return Value::obj(std::move(r));
    }, 1, 3));

    G.define("exit", mkNative("exit", [](Interp&, std::vector<Value>& a) -> Value {
        throw ExitRequest{ a.empty() ? 0 : static_cast<int>(a[0].isInt() ? a[0].i : 0) };
    }, 0, 1));

    G.define("id", mkNative("id", [](Interp&, std::vector<Value>& a) {
        if (a.empty() || !a[0].o)
            return Value::integer(reinterpret_cast<int64_t>(&a[0]));
        return Value::integer(reinterpret_cast<int64_t>(a[0].o.get()));
    }, 0, 1));

    // ---------- Yuksek dereceli fonksiyonlar ----------
    G.define("map", mkNative("map", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "map(fn, seq)", "");
        std::vector<Value> out;
        if (a[1].isMap()) {
            for (auto& kv : asMap(a[1])->items) out.push_back(applyFn(I, a[0], {kv.second}));
        } else if (a[1].isStr()) {
            const std::string s = asStr(a[1])->s;
            for (size_t i = 0; i < s.size(); ++i)
                out.push_back(applyFn(I, a[0], {mkStr(std::string(1, s[i]))}));
        } else {
            I.iterate(a[1], [&](const Value& v) { out.push_back(applyFn(I, a[0], {v})); });
        }
        return mkList(std::move(out));
    }, 2, 2));

    G.define("filter", mkNative("filter", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "filter(fn, seq)", "");
        std::vector<Value> out;
        I.iterate(a[1], [&](const Value& v) {
            if (valueTruthy(applyFn(I, a[0], {v}))) out.push_back(v);
        });
        return mkList(std::move(out));
    }, 2, 2));

    G.define("reduce", mkNative("reduce", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "reduce(fn, seq)", "");
        Value acc;
        bool first = true;
        I.iterate(a[1], [&](const Value& v) {
            if (first) { acc = v; first = false; return; }
            acc = applyFn(I, a[0], {acc, v});
        });
        if (first) I.error("Hata", "reduce() bos diziye uygulanamaz", 0);
        return acc;
    }, 2, 2));

    G.define("fold", mkNative("fold", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 3, "fold(init, fn, seq)", "");
        Value acc = a[0];
        I.iterate(a[2], [&](const Value& v) { acc = applyFn(I, a[1], {acc, v}); });
        return acc;
    }, 3, 3));

    G.define("each", mkNative("each", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "each(seq)", "");
        Value r = Value::nil();
        I.iterate(a[0], [&](const Value& v) { r = applyFn(I, arg(a, 1), {v}); });
        return r;
    }, 1, 2));

    G.define("any", mkNative("any", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "any(seq)", "");
        bool found = false;
        I.iterate(a[0], [&](const Value& v) {
            if (!found && (a.size() < 2 ? valueTruthy(v) : valueTruthy(applyFn(I, a[1], {v}))))
                found = true;
        });
        return Value::boolean(found);
    }, 1, 2));

    G.define("all", mkNative("all", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "all(seq)", "");
        bool ok = true;
        I.iterate(a[0], [&](const Value& v) {
            if (ok && !(a.size() < 2 ? valueTruthy(v) : valueTruthy(applyFn(I, a[1], {v}))))
                ok = false;
        });
        return Value::boolean(ok);
    }, 1, 2));

    G.define("count", mkNative("count", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "count(seq)", "");
        int64_t n = 0;
        I.iterate(a[0], [&](const Value& v) {
            if (a.size() < 2 || valueEquals(v, a[1])) ++n;
        });
        return Value::integer(n);
    }, 1, 2));

    G.define("sum", mkNative("sum", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "sum(seq)", "");
        if (a[0].isStr()) {
            std::string out;
            I.iterate(a[0], [&](const Value& v) { out += I.strOf(v, 0); });
            return mkStr(out);
        }
        bool isFloat = a[0].isSet();
        int64_t isum = 0;
        double fsum = 0;
        I.iterate(a[0], [&](const Value& v) {
            Value x = (a.size() > 1 && a[1].isFn()) ? applyFn(I, a[1], {v}) : v;
            if (!x.isNum()) I.error("TipHatasi", "sum() sadece sayilarla calisir", 0);
            if (x.isFloat()) { isFloat = true; fsum += x.d; }
            else { isum += x.i; fsum += static_cast<double>(x.i); }
        });
        return isFloat ? Value::number(fsum) : Value::integer(isum);
    }, 1, 2));

    G.define("min", mkNative("min", [](Interp& I, std::vector<Value>& a) {
        if (a.size() > 1 && a[0].isSeq()) {
            Value best;
            bool first = true;
            I.iterate(a[0], [&](const Value& v) {
                Value x = (a[1].isFn()) ? applyFn(I, a[1], {v}) : v;
                if (first || compareForSort(I, x, best) < 0) { best = x; first = false; }
            });
            if (first) I.error("Hata", "min() bos diziye uygulanamaz", 0);
            return best;
        }
        if (a.size() > 1) {
            Value best = a[0];
            for (size_t i = 1; i < a.size(); ++i)
                if (compareForSort(I, a[i], best) < 0) best = a[i];
            return best;
        }
        needArgs(I, a, 1, "min(seq)", "");
        Value best;
        bool first = true;
        I.iterate(a[0], [&](const Value& v) {
            if (first || compareForSort(I, v, best) < 0) { best = v; first = false; }
        });
        if (first) I.error("Hata", "min() bos diziye uygulanamaz", 0);
        return best;
    }, 1, -1));

    G.define("max", mkNative("max", [](Interp& I, std::vector<Value>& a) {
        if (a.size() > 1 && a[0].isSeq()) {
            Value best;
            bool first = true;
            I.iterate(a[0], [&](const Value& v) {
                Value x = (a[1].isFn()) ? applyFn(I, a[1], {v}) : v;
                if (first || compareForSort(I, x, best) > 0) { best = x; first = false; }
            });
            if (first) I.error("Hata", "max() bos diziye uygulanamaz", 0);
            return best;
        }
        if (a.size() > 1) {
            Value best = a[0];
            for (size_t i = 1; i < a.size(); ++i)
                if (compareForSort(I, a[i], best) > 0) best = a[i];
            return best;
        }
        needArgs(I, a, 1, "max(seq)", "");
        Value best;
        bool first = true;
        I.iterate(a[0], [&](const Value& v) {
            if (first || compareForSort(I, v, best) > 0) { best = v; first = false; }
        });
        if (first) I.error("Hata", "max() bos diziye uygulanamaz", 0);
        return best;
    }, 1, -1));

    G.define("sort", mkNative("sort", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "sort(seq)", "");
        std::vector<Value> items = toList(I, a[0]);
        Value key = arg(a, 1);
        std::stable_sort(items.begin(), items.end(),
            [&](const Value& x, const Value& y) {
                Value u = key.isFn() ? applyFn(I, key, {x}) : x;
                Value v = key.isFn() ? applyFn(I, key, {y}) : y;
                return compareForSort(I, u, v) < 0;
            });
        if (a[0].isList()) { asList(a[0])->items = std::move(items); return a[0]; }
        return mkList(std::move(items));
    }, 1, 2));

    G.define("sorted", mkNative("sorted", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "sorted(seq)", "");
        std::vector<Value> items = toList(I, a[0]);
        Value key = arg(a, 1);
        std::stable_sort(items.begin(), items.end(),
            [&](const Value& x, const Value& y) {
                Value u = key.isFn() ? applyFn(I, key, {x}) : x;
                Value v = key.isFn() ? applyFn(I, key, {y}) : y;
                return compareForSort(I, u, v) < 0;
            });
        return mkList(std::move(items));
    }, 1, 2));

    G.define("reverse", mkNative("reverse", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "reverse(seq)", "");
        if (a[0].isStr()) {
            std::string s = asStr(a[0])->s;
            std::reverse(s.begin(), s.end());
            return mkStr(s);
        }
        std::vector<Value> items = toList(I, a[0]);
        std::reverse(items.begin(), items.end());
        if (a[0].isList()) { asList(a[0])->items = items; return a[0]; }
        return mkList(std::move(items));
    }, 1, 1));

    G.define("join", mkNative("join", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "join(seq, sep)", "");
        std::string sep = I.strOf(a[1], 0);
        std::string out;
        bool first = true;
        I.iterate(a[0], [&](const Value& v) {
            if (!first) out += sep;
            first = false;
            out += I.strOf(v, 0);
        });
        return mkStr(out);
    }, 2, 2));

    G.define("split", mkNative("split", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "split(str)", "");
        const std::string& s = I.strOf(a[0], 0);
        std::vector<Value> out;
        std::string sep = a.size() > 1 ? I.strOf(a[1], 0) : std::string();
        if (sep.empty()) {
            std::istringstream is(s);
            std::string w;
            while (is >> w) out.push_back(mkStr(w));
        } else {
            size_t pos = 0, p2;
            while ((p2 = s.find(sep, pos)) != std::string::npos) {
                out.push_back(mkStr(s.substr(pos, p2 - pos)));
                pos = p2 + sep.size();
            }
            out.push_back(mkStr(s.substr(pos)));
        }
        return mkList(std::move(out));
    }, 1, 2));

    G.define("unique", mkNative("unique", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "unique(seq)", "");
        auto s = mkSet();
        I.iterate(a[0], [&](const Value& v) { asSet(s)->add(v); });
        return mkList(asSet(s)->items);
    }, 1, 1));

    G.define("flatten", mkNative("flatten", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "flatten(seq)", "");
        std::vector<Value> out;
        I.iterate(a[0], [&](const Value& v) {
            if (v.isSeq() && !v.isStr()) I.iterate(v, [&](const Value& x) { out.push_back(x); });
            else out.push_back(v);
        });
        return mkList(std::move(out));
    }, 1, 1));

    G.define("zip", mkNative("zip", [](Interp& I, std::vector<Value>& a) {
        std::vector<std::vector<Value>> cols;
        for (auto& x : a) cols.push_back(toList(I, x));
        size_t n = cols.empty() ? 0 : cols[0].size();
        for (auto& c : cols) n = std::min(n, c.size());
        std::vector<Value> out;
        for (size_t i = 0; i < n; ++i) {
            std::vector<Value> row;
            for (auto& c : cols) row.push_back(c[i]);
            out.push_back(mkTuple(std::move(row)));
        }
        return mkList(std::move(out));
    }, 1, -1));

    G.define("enumerate", mkNative("enumerate", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "enumerate(seq)", "");
        std::vector<Value> out;
        int64_t start = a.size() > 1 ? I.toInt(a[1], 0).i : 0;
        int64_t i = start;
        I.iterate(a[0], [&](const Value& v) {
            out.push_back(mkTuple({Value::integer(i++), v}));
        });
        return mkList(std::move(out));
    }, 1, 2));

    G.define("take", mkNative("take", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "take(n, seq)", "");
        int64_t n = I.toInt(a[0], 0).i;
        std::vector<Value> out;
        int64_t i = 0;
        I.iterate(a[1], [&](const Value& v) {
            if (i++ < n) out.push_back(v);
        });
        return mkList(std::move(out));
    }, 2, 2));

    G.define("skip", mkNative("skip", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "skip(n, seq)", "");
        int64_t n = I.toInt(a[0], 0).i;
        std::vector<Value> out;
        int64_t i = 0;
        I.iterate(a[1], [&](const Value& v) {
            if (i++ >= n) out.push_back(v);
        });
        return mkList(std::move(out));
    }, 2, 2));

    G.define("keys", mkNative("keys", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "keys(map)", "");
        std::vector<Value> out;
        I.iterateIndexed(a[0], [&](const Value& k, const Value&, int64_t) { out.push_back(k); });
        return mkList(std::move(out));
    }, 1, 1));

    G.define("values", mkNative("values", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "values(map)", "");
        std::vector<Value> out;
        I.iterateIndexed(a[0], [&](const Value&, const Value& v, int64_t) { out.push_back(v); });
        return mkList(std::move(out));
    }, 1, 1));

    G.define("items", mkNative("items", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "items(map)", "");
        std::vector<Value> out;
        I.iterateIndexed(a[0], [&](const Value& k, const Value& v, int64_t) {
            out.push_back(mkTuple({k, v}));
        });
        return mkList(std::move(out));
    }, 1, 1));

    G.define("group_by", mkNative("group_by", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "group_by(fn, seq)", "");
        auto m = mkMap();
        I.iterate(a[1], [&](const Value& v) {
            Value k = applyFn(I, a[0], {v});
            const Value* cur = asMap(m)->get(k);
            if (!cur) {
                Value nl = mkList({v});
                asMap(m)->set(k, nl);
            } else {
                asList(*cur)->items.push_back(v);
            }
        });
        return m;
    }, 2, 2));

    G.define("apply", mkNative("apply", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "apply(fn, args)", "");
        std::vector<Value> args = toList(I, a[1]);
        return applyFn(I, a[0], args);
    }, 2, 2));

    // ---------- Hata yardimcilari ----------
    G.define("error", mkNative("error", [](Interp& I, std::vector<Value>& a) -> Value {
        I.throwValue(mkError("Error", I.strOf(arg(a, 0), 0)), 0);
        return Value::nil();
    }, 0, 2));

    G.define("abs", mkNative("abs", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "abs(x)", "");
        Value x = a[0];
        if (x.isInt()) return Value::integer(x.i < 0 ? -x.i : x.i);
        if (x.isFloat()) return Value::number(x.d < 0 ? -x.d : x.d);
        I.error("TipHatasi", "abs() sadece sayilarla calisir", 0);
    }, 1, 1));

    G.define("round", mkNative("round", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 1, "round(x)", "");
        double d = I.toFloat(a[0], 0).d;
        int64_t digits = a.size() > 1 ? I.toInt(a[1], 0).i : 0;
        double m = std::pow(10.0, static_cast<double>(digits));
        double r = std::round(d * m) / m;
        return Value::number(r);
    }, 1, 2));

    G.define("pow", mkNative("pow", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "pow(x, y)", "");
        return Value::number(std::pow(I.toFloat(a[0], 0).d, I.toFloat(a[1], 0).d));
    }, 2, 2));

    G.define("clock", mkNative("clock", [](Interp&, std::vector<Value>&) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return Value::number(std::chrono::duration<double>(now).count());
    }, 0, 0));

    // =====================================================================
    //  str metotlari
    // =====================================================================
    registerMethod(VT::Str, "upper", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        (void)I; return mkStr(s);
    });
    registerMethod(VT::Str, "lower", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        (void)I; return mkStr(s);
    });
    registerMethod(VT::Str, "title", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        bool cap = true;
        for (auto& c : s) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                c = static_cast<char>(cap ? std::toupper(static_cast<unsigned char>(c))
                                          : std::tolower(static_cast<unsigned char>(c)));
                cap = false;
            } else cap = true;
        }
        (void)I; return mkStr(s);
    });
    registerMethod(VT::Str, "capitalize", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        if (!s.empty()) {
            s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
            for (size_t i = 1; i < s.size(); ++i)
                s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        }
        (void)I; return mkStr(s);
    });
    registerMethod(VT::Str, "strip", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        if (a.size() > 1) {
            std::string cut = I.strOf(a[1], 0);
            while (!s.empty() && s.find_first_of(cut) == 0) s.erase(0, 1);
            while (!s.empty() && s.find_last_of(cut) == s.size() - 1) s.pop_back();
        } else {
            auto b = s.find_first_not_of(" \t\n\r\f\v");
            auto e = s.find_last_not_of(" \t\n\r\f\v");
            s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
        }
        return mkStr(s);
    });
    registerMethod(VT::Str, "lstrip", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        std::string cut = a.size() > 1 ? I.strOf(a[1], 0) : std::string(" \t\n\r\f\v");
        while (!s.empty() && s.find_first_of(cut) == 0) s.erase(0, 1);
        return mkStr(s);
    });
    registerMethod(VT::Str, "rstrip", [](Interp& I, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        std::string cut = a.size() > 1 ? I.strOf(a[1], 0) : std::string(" \t\n\r\f\v");
        while (!s.empty() && s.find_last_of(cut) == s.size() - 1) s.pop_back();
        return mkStr(s);
    });
    registerMethod(VT::Str, "split", [](Interp& I, std::vector<Value>& a) -> Value {
        const Value* sp = I.globals()->lookup("split");
        if (!sp) I.error("Hata", "split tanimli degil", 0);
        return I.callValue(*sp, a, 0);
    });
    registerMethod(VT::Str, "lines", [](Interp& I, std::vector<Value>& a) {
        (void)I;
        const std::string& s = asStr(a[0])->s;
        std::vector<Value> out;
        std::string cur;
        for (char c : s) {
            if (c == '\n') { out.push_back(mkStr(cur)); cur.clear(); }
            else cur += c;
        }
        out.push_back(mkStr(cur));
        return mkList(std::move(out));
    });
    registerMethod(VT::Str, "join", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "join(sep, seq)", "");
        std::string sep = asStr(a[0])->s;
        std::string out;
        bool first = true;
        I.iterate(a[1], [&](const Value& v) {
            if (!first) out += sep;
            first = false;
            out += I.strOf(v, 0);
        });
        return mkStr(out);
    });
    registerMethod(VT::Str, "replace", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 3, "replace(s, from, to)", "");
        std::string s = asStr(a[0])->s, from = I.strOf(a[1], 0), to = I.strOf(a[2], 0);
        if (from.empty()) return mkStr(s);
        std::string out;
        size_t pos = 0, p2;
        while ((p2 = s.find(from, pos)) != std::string::npos) {
            out += s.substr(pos, p2 - pos);
            out += to;
            pos = p2 + from.size();
        }
        out += s.substr(pos);
        return mkStr(out);
    });
    registerMethod(VT::Str, "find", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "find(s, sub)", "");
        std::string s = asStr(a[0])->s, sub = I.strOf(a[1], 0);
        size_t pos = a.size() > 2 ? static_cast<size_t>(I.toInt(a[2], 0).i) : 0;
        size_t r = s.find(sub, pos);
        return Value::integer(r == std::string::npos ? -1 : static_cast<int64_t>(r));
    });
    registerMethod(VT::Str, "rfind", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "rfind(s, sub)", "");
        std::string s = asStr(a[0])->s, sub = I.strOf(a[1], 0);
        size_t r = s.rfind(sub);
        return Value::integer(r == std::string::npos ? -1 : static_cast<int64_t>(r));
    });
    registerMethod(VT::Str, "index", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "index(s, sub)", "");
        std::string s = asStr(a[0])->s, sub = I.strOf(a[1], 0);
        size_t r = s.find(sub);
        if (r == std::string::npos)
            I.error("DegerHatasi", "'" + sub + "' icinde bulunamadi", 0);
        return Value::integer(static_cast<int64_t>(r));
    });
    registerMethod(VT::Str, "contains", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "contains(s, sub)", "");
        return Value::boolean(asStr(a[0])->s.find(I.strOf(a[1], 0)) != std::string::npos);
    });
    registerMethod(VT::Str, "startswith", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "startswith(s, p)", "");
        const std::string& s = asStr(a[0])->s;
        return Value::boolean(s.rfind(I.strOf(a[1], 0), 0) == 0);
    });
    registerMethod(VT::Str, "endswith", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "endswith(s, p)", "");
        const std::string& s = asStr(a[0])->s;
        std::string p = I.strOf(a[1], 0);
        return Value::boolean(s.size() >= p.size() &&
                              s.compare(s.size() - p.size(), p.size(), p) == 0);
    });
    registerMethod(VT::Str, "count", [](Interp& I, std::vector<Value>& a) {
        const std::string& s = asStr(a[0])->s;
        if (a.size() < 2)
            return Value::integer(static_cast<int64_t>(s.size()));
        std::string sub = I.strOf(a[1], 0);
        int64_t n = 0;
        size_t pos = 0, p2;
        while (sub.size() && (p2 = s.find(sub, pos)) != std::string::npos) {
            ++n;
            pos = p2 + sub.size();
        }
        return Value::integer(n);
    });
    registerMethod(VT::Str, "chars", [](Interp&, std::vector<Value>& a) {
        std::vector<Value> out;
        for (char c : asStr(a[0])->s) out.push_back(mkStr(std::string(1, c)));
        return mkList(std::move(out));
    });
    registerMethod(VT::Str, "pad_start", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "pad_start(s, n)", "");
        std::string s = asStr(a[0])->s;
        std::string pad = a.size() > 2 ? I.strOf(a[2], 0) : " ";
        size_t n = static_cast<size_t>(I.toInt(a[1], 0).i);
        if (pad.empty()) return mkStr(s);
        while (s.size() < n) s.insert(0, pad);
        return mkStr(s);
    });
    registerMethod(VT::Str, "pad_end", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "pad_end(s, n)", "");
        std::string s = asStr(a[0])->s;
        std::string pad = a.size() > 2 ? I.strOf(a[2], 0) : " ";
        size_t n = static_cast<size_t>(I.toInt(a[1], 0).i);
        if (pad.empty()) return mkStr(s);
        while (s.size() < n) s += pad;
        return mkStr(s);
    });
    registerMethod(VT::Str, "repeat", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "repeat(s, n)", "");
        std::string s = asStr(a[0])->s;
        int64_t n = I.toInt(a[1], 0).i;
        std::string out;
        for (int64_t i = 0; i < n; ++i) out += s;
        return mkStr(out);
    });
    registerMethod(VT::Str, "is_digit", [](Interp&, std::vector<Value>& a) {
        const std::string& s = asStr(a[0])->s;
        if (s.empty()) return Value::boolean(false);
        for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c)))
            return Value::boolean(false);
        return Value::boolean(true);
    });
    registerMethod(VT::Str, "is_alpha", [](Interp&, std::vector<Value>& a) {
        const std::string& s = asStr(a[0])->s;
        if (s.empty()) return Value::boolean(false);
        for (char c : s) if (!std::isalpha(static_cast<unsigned char>(c)))
            return Value::boolean(false);
        return Value::boolean(true);
    });
    registerMethod(VT::Str, "is_space", [](Interp&, std::vector<Value>& a) {
        const std::string& s = asStr(a[0])->s;
        if (s.empty()) return Value::boolean(false);
        for (char c : s) if (!std::isspace(static_cast<unsigned char>(c)))
            return Value::boolean(false);
        return Value::boolean(true);
    });
    registerMethod(VT::Str, "is_upper", [](Interp&, std::vector<Value>& a) {
        const std::string& s = asStr(a[0])->s;
        bool has = false;
        for (char c : s) {
            if (!std::isalpha(static_cast<unsigned char>(c))) continue;
            has = true;
            if (!std::isupper(static_cast<unsigned char>(c))) return Value::boolean(false);
        }
        return Value::boolean(has);
    });
    registerMethod(VT::Str, "is_lower", [](Interp&, std::vector<Value>& a) {
        const std::string& s = asStr(a[0])->s;
        bool has = false;
        for (char c : s) {
            if (!std::isalpha(static_cast<unsigned char>(c))) continue;
            has = true;
            if (!std::islower(static_cast<unsigned char>(c))) return Value::boolean(false);
        }
        return Value::boolean(has);
    });
    registerMethod(VT::Str, "format", [](Interp& I, std::vector<Value>& a) {
        return mkStr(I.formatString(asStr(a[0])->s,
                                    a.size() > 1 ? a[1] : Value::nil(), 0));
    });
    registerMethod(VT::Str, "reverse", [](Interp&, std::vector<Value>& a) {
        std::string s = asStr(a[0])->s;
        std::reverse(s.begin(), s.end());
        return mkStr(s);
    });
    registerMethod(VT::Str, "char_at", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "char_at(s, i)", "");
        const std::string& s = asStr(a[0])->s;
        int64_t i = I.toInt(a[1], 0).i;
        if (i < 0) i += static_cast<int64_t>(s.size());
        if (i < 0 || i >= static_cast<int64_t>(s.size()))
            I.error("IndexHatasi", "karakter indeksi gecersiz", 0);
        return mkStr(std::string(1, s[static_cast<size_t>(i)]));
    });
    registerMethod(VT::Str, "slice", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "slice(s, a, b)", "");
        const std::string& s = asStr(a[0])->s;
        int64_t lo = I.toInt(a[1], 0).i;
        int64_t hi = a.size() > 2 ? I.toInt(a[2], 0).i : static_cast<int64_t>(s.size());
        if (lo < 0) lo += static_cast<int64_t>(s.size());
        if (hi < 0) hi += static_cast<int64_t>(s.size());
        lo = std::max<int64_t>(0, std::min<int64_t>(lo, static_cast<int64_t>(s.size())));
        hi = std::max<int64_t>(lo, std::min<int64_t>(hi, static_cast<int64_t>(s.size())));
        return mkStr(s.substr(static_cast<size_t>(lo), static_cast<size_t>(hi - lo)));
    });

    // =====================================================================
    //  list / tuple metotlari
    // =====================================================================
    for (VT t : {VT::List, VT::Tuple}) {
        registerMethod(t, "append", [t](Interp& I, std::vector<Value>& a) {
            if (t == VT::Tuple) I.error("Hata", "tuple degistirilemez", 0);
            needArgs(I, a, 2, "append(l, x)", "");
            asList(a[0])->items.push_back(arg(a, 1));
            return Value::nil();
        });
        registerMethod(t, "extend", [t](Interp& I, std::vector<Value>& a) {
            if (t == VT::Tuple) I.error("Hata", "tuple degistirilemez", 0);
            needArgs(I, a, 2, "extend(l, other)", "");
            I.iterate(a[1], [&](const Value& v) { asList(a[0])->items.push_back(v); });
            return Value::nil();
        });
        registerMethod(t, "pop", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "pop(l)", "");
            auto* l = asList(a[0]);
            if (l->items.empty()) I.error("IndexHatasi", "bos listeden pop yapilamaz", 0);
            int64_t i = a.size() > 1 ? I.toInt(a[1], 0).i
                                     : static_cast<int64_t>(l->items.size()) - 1;
            if (i < 0) i += static_cast<int64_t>(l->items.size());
            if (i < 0 || i >= static_cast<int64_t>(l->items.size()))
                I.error("IndexHatasi", "pop indeksi gecersiz", 0);
            Value v = l->items[static_cast<size_t>(i)];
            l->items.erase(l->items.begin() + static_cast<ptrdiff_t>(i));
            return v;
        });
        registerMethod(t, "insert", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "insert(l, i, x)", "");
            auto* l = asList(a[0]);
            int64_t i = I.toInt(a[1], 0).i;
            if (i < 0) i += static_cast<int64_t>(l->items.size());
            if (i < 0 || i > static_cast<int64_t>(l->items.size()))
                I.error("IndexHatasi", "insert indeksi gecersiz", 0);
            l->items.insert(l->items.begin() + static_cast<ptrdiff_t>(i), arg(a, 2));
            return Value::nil();
        });
        registerMethod(t, "remove", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "remove(l, x)", "");
            auto* l = asList(a[0]);
            for (size_t i = 0; i < l->items.size(); ++i) {
                if (valueEquals(l->items[i], arg(a, 1))) {
                    l->items.erase(l->items.begin() + static_cast<ptrdiff_t>(i));
                    return Value::nil();
                }
            }
            I.error("DegerHatasi", "silinecek deger bulunamadi", 0);
        });
        registerMethod(t, "clear", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "clear(l)", "");
            asList(a[0])->items.clear();
            return Value::nil();
        });
        registerMethod(t, "reverse", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "reverse(l)", "");
            std::reverse(asList(a[0])->items.begin(), asList(a[0])->items.end());
            return Value::nil();
        });
        registerMethod(t, "sort", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "sort(l)", "");
            Value key = arg(a, 1);
            std::stable_sort(asList(a[0])->items.begin(), asList(a[0])->items.end(),
                [&](const Value& x, const Value& y) {
                    Value u = key.isFn() ? applyFn(I, key, {x}) : x;
                    Value v = key.isFn() ? applyFn(I, key, {y}) : y;
                    int c = compareValues(u, v);
                    if (c == -2) I.error("TipHatasi", "ogeler siralanamaz", 0);
                    return c < 0;
                });
            return Value::nil();
        });
        registerMethod(t, "contains", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "contains(l, x)", "");
            bool found = false;
            I.iterate(a[0], [&](const Value& v) {
                if (!found && valueEquals(v, arg(a, 1))) found = true;
            });
            return Value::boolean(found);
        });
        registerMethod(t, "index", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "index(l, x)", "");
            std::vector<Value> items = toList(I, a[0]);
            for (size_t i = 0; i < items.size(); ++i)
                if (valueEquals(items[i], arg(a, 1)))
                    return Value::integer(static_cast<int64_t>(i));
            I.error("DegerHatasi", "deger listede yok", 0);
        });
        registerMethod(t, "count", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "count(l, x)", "");
            int64_t n = 0;
            I.iterate(a[0], [&](const Value& v) {
                if (valueEquals(v, arg(a, 1))) ++n;
            });
            return Value::integer(n);
        });
        registerMethod(t, "join", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "join(l, sep)", "");
            std::string sep = I.strOf(a[1], 0), out;
            bool first = true;
            I.iterate(a[0], [&](const Value& v) {
                if (!first) out += sep;
                first = false;
                out += I.strOf(v, 0);
            });
            return mkStr(out);
        });
        registerMethod(t, "map", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "map(l, fn)", "");
            std::vector<Value> out;
            I.iterate(a[0], [&](const Value& v) { out.push_back(applyFn(I, arg(a, 1), {v})); });
            return mkList(std::move(out));
        });
        registerMethod(t, "filter", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "filter(l, fn)", "");
            std::vector<Value> out;
            I.iterate(a[0], [&](const Value& v) {
                if (valueTruthy(applyFn(I, arg(a, 1), {v}))) out.push_back(v);
            });
            return mkList(std::move(out));
        });
        registerMethod(t, "reduce", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "reduce(l, fn)", "");
            Value acc;
            bool first = true;
            I.iterate(a[0], [&](const Value& v) {
                if (first) { acc = v; first = false; return; }
                acc = applyFn(I, arg(a, 1), {acc, v});
            });
            if (first) I.error("Hata", "reduce() bos diziye uygulanamaz", 0);
            return acc;
        });
        registerMethod(t, "each", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "each(l, fn)", "");
            Value r = Value::nil();
            I.iterate(a[0], [&](const Value& v) { r = applyFn(I, arg(a, 1), {v}); });
            return r;
        });
        registerMethod(t, "sum", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "sum(l)", "");
            bool isFloat = false;
            int64_t isum = 0;
            double fsum = 0;
            I.iterate(a[0], [&](const Value& v) {
                if (!v.isNum()) I.error("TipHatasi", "sum() sadece sayilarla calisir", 0);
                if (v.isFloat()) { isFloat = true; fsum += v.d; }
                else { isum += v.i; fsum += static_cast<double>(v.i); }
            });
            return isFloat ? Value::number(fsum) : Value::integer(isum);
        });
        registerMethod(t, "min", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "min(l)", "");
            Value best;
            bool first = true;
            I.iterate(a[0], [&](const Value& v) {
                if (first || compareForSort(I, v, best) < 0) { best = v; first = false; }
            });
            if (first) I.error("Hata", "min() bos diziye uygulanamaz", 0);
            return best;
        });
        registerMethod(t, "max", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "max(l)", "");
            Value best;
            bool first = true;
            I.iterate(a[0], [&](const Value& v) {
                if (first || compareForSort(I, v, best) > 0) { best = v; first = false; }
            });
            if (first) I.error("Hata", "max() bos diziye uygulanamaz", 0);
            return best;
        });
        registerMethod(t, "copy", [](Interp&, std::vector<Value>& a) {
            return mkList(asList(a[0])->items);
        });
        registerMethod(t, "first", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "first(l)", "");
            auto* l = asList(a[0]);
            if (l->items.empty()) return Value::nil();
            return l->items.front();
        });
        registerMethod(t, "last", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 1, "last(l)", "");
            auto* l = asList(a[0]);
            if (l->items.empty()) return Value::nil();
            return l->items.back();
        });
        registerMethod(t, "unique", [](Interp&, std::vector<Value>& a) {
            auto s = mkSet();
            for (auto& v : asList(a[0])->items) asSet(s)->add(v);
            return mkList(asSet(s)->items);
        });
        registerMethod(t, "flat_map", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "flat_map(l, fn)", "");
            std::vector<Value> out;
            I.iterate(a[0], [&](const Value& v) {
                Value r = applyFn(I, arg(a, 1), {v});
                if (r.isSeq() && !r.isStr()) I.iterate(r, [&](const Value& x) { out.push_back(x); });
                else out.push_back(r);
            });
            return mkList(std::move(out));
        });
        registerMethod(t, "slice", [](Interp& I, std::vector<Value>& a) {
            needArgs(I, a, 2, "slice(l, a, b)", "");
            auto* l = asList(a[0]);
            int64_t lo = I.toInt(a[1], 0).i;
            int64_t hi = a.size() > 2 ? I.toInt(a[2], 0).i : static_cast<int64_t>(l->items.size());
            if (lo < 0) lo += static_cast<int64_t>(l->items.size());
            if (hi < 0) hi += static_cast<int64_t>(l->items.size());
            lo = std::max<int64_t>(0, std::min<int64_t>(lo, static_cast<int64_t>(l->items.size())));
            hi = std::max<int64_t>(lo, std::min<int64_t>(hi, static_cast<int64_t>(l->items.size())));
            std::vector<Value> out(l->items.begin() + static_cast<ptrdiff_t>(lo),
                                   l->items.begin() + static_cast<ptrdiff_t>(hi));
            return mkList(std::move(out));
        });
    }

    // =====================================================================
    //  map (sozluk) metotlari
    // =====================================================================
    registerMethod(VT::Map, "keys", [](Interp& I, std::vector<Value>& a) {
        std::vector<Value> out;
        for (auto& kv : asMap(a[0])->items) out.push_back(kv.first);
        (void)I; return mkList(std::move(out));
    });
    registerMethod(VT::Map, "values", [](Interp& I, std::vector<Value>& a) {
        std::vector<Value> out;
        for (auto& kv : asMap(a[0])->items) out.push_back(kv.second);
        (void)I; return mkList(std::move(out));
    });
    registerMethod(VT::Map, "items", [](Interp&, std::vector<Value>& a) {
        std::vector<Value> out;
        for (auto& kv : asMap(a[0])->items) out.push_back(mkTuple({kv.first, kv.second}));
        return mkList(std::move(out));
    });
    registerMethod(VT::Map, "get", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "get(d, key)", "");
        const Value* v = asMap(a[0])->get(a[1]);
        if (v) return *v;
        return arg(a, 2);
    });
    registerMethod(VT::Map, "has", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "has(d, key)", "");
        return Value::boolean(asMap(a[0])->get(a[1]) != nullptr);
    });
    registerMethod(VT::Map, "remove", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "remove(d, key)", "");
        if (!asMap(a[0])->erase(a[1])) I.error("AnahtarHatasi", "anahtar bulunamadi", 0);
        return Value::nil();
    });
    registerMethod(VT::Map, "clear", [](Interp&, std::vector<Value>& a) {
        auto* m = asMap(a[0]);
        m->items.clear();
        m->index.clear();
        m->indexed = false;
        return Value::nil();
    });
    registerMethod(VT::Map, "merge", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "merge(d, other)", "");
        I.iterateIndexed(a[1], [&](const Value& k, const Value& v, int64_t) {
            asMap(a[0])->set(k, v);
        });
        return a[0];
    });
    registerMethod(VT::Map, "copy", [](Interp&, std::vector<Value>& a) {
        auto m = mkMap();
        asMap(m)->items = asMap(a[0])->items;
        return m;
    });

    // =====================================================================
    //  set metotlari
    // =====================================================================
    registerMethod(VT::Set, "add", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "add(s, x)", "");
        asSet(a[0])->add(a[1]);
        return Value::nil();
    });
    registerMethod(VT::Set, "remove", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "remove(s, x)", "");
        if (!asSet(a[0])->erase(a[1])) I.error("DegerHatasi", "deger kumede yok", 0);
        return Value::nil();
    });
    registerMethod(VT::Set, "contains", [](Interp& I, std::vector<Value>& a) {
        needArgs(I, a, 2, "contains(s, x)", "");
        return Value::boolean(asSet(a[0])->contains(a[1]));
    });
    registerMethod(VT::Set, "clear", [](Interp&, std::vector<Value>& a) {
        asSet(a[0])->items.clear();
        return Value::nil();
    });
    registerMethod(VT::Set, "to_list", [](Interp&, std::vector<Value>& a) {
        return mkList(asSet(a[0])->items);
    });
}

} // namespace k7