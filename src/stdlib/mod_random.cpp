#include "k7/modules.h"

#include <random>

namespace k7 {

namespace {

std::mt19937_64& engine() {
    static std::mt19937_64 g{std::random_device{}()};
    return g;
}

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("random.") + name, std::move(fn), mn, mx);
}

double num(Interp& I, std::vector<Value>& a, size_t i, double def) {
    if (a.size() <= i || a[i].isNil()) return def;
    if (!a[i].isNum()) I.error("TipHatasi", "random fonksiyonlari sayi ister");
    return a[i].asNum();
}

} // namespace

void registerRandomModule(Interp& I) {
    I.registerModule("random", [&](ModuleObj& m) {
        add(m, "seed", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "random.seed(t) arguman ister");
            engine().seed(a[0].isInt() ? static_cast<uint64_t>(a[0].i)
                                       : static_cast<uint64_t>(a[0].asNum()));
            return Value::nil();
        }, 1, 1);
        add(m, "int", [](Interp& I, std::vector<Value>& a) {
            int64_t lo = static_cast<int64_t>(num(I, a, 0, 0));
            int64_t hi = static_cast<int64_t>(num(I, a, 1, 100));
            if (hi < lo) std::swap(lo, hi);
            if (hi == lo) return Value::integer(lo);
            std::uniform_int_distribution<int64_t> d(lo, hi);
            return Value::integer(d(engine()));
        }, 0, 2);
        add(m, "float", [](Interp& I, std::vector<Value>& a) {
            double lo = num(I, a, 0, 0.0), hi = num(I, a, 1, 1.0);
            if (hi < lo) std::swap(lo, hi);
            std::uniform_real_distribution<double> d(lo, hi);
            return Value::number(d(engine()));
        }, 0, 2);
        add(m, "bool", [](Interp&, std::vector<Value>&) {
            std::bernoulli_distribution d(0.5);
            return Value::boolean(d(engine()));
        }, 0, 0);
        add(m, "choice", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "random.choice(dizi) arguman ister");
            std::vector<Value> items;
            I.iterate(a[0], [&](const Value& v) { items.push_back(v); });
            if (items.empty()) I.error("Hata", "random.choice() bos diziye uygulanamaz");
            std::uniform_int_distribution<size_t> d(0, items.size() - 1);
            return items[d(engine())];
        }, 1, 1);
        add(m, "choices", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "random.choices(dizi, n) arguman ister");
            std::vector<Value> items;
            I.iterate(a[0], [&](const Value& v) { items.push_back(v); });
            if (items.empty()) I.error("Hata", "random.choices() bos diziye uygulanamaz");
            int64_t n = I.toInt(a[1], 0).i;
            std::vector<Value> out;
            std::uniform_int_distribution<size_t> d(0, items.size() - 1);
            for (int64_t i = 0; i < n; ++i) out.push_back(items[d(engine())]);
            return mkList(std::move(out));
        }, 2, 2);
        add(m, "shuffle", [](Interp& I, std::vector<Value>& a) {
            if (a.empty() || !a[0].isList())
                I.error("TipHatasi", "random.shuffle(liste) arguman ister");
            auto& items = asList(a[0])->items;
            for (size_t i = items.size(); i > 1; --i) {
                std::uniform_int_distribution<size_t> d(0, i - 1);
                std::swap(items[i - 1], items[d(engine())]);
            }
            return a[0];
        }, 1, 1);
        add(m, "sample", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "random.sample(dizi, n) arguman ister");
            std::vector<Value> items;
            I.iterate(a[0], [&](const Value& v) { items.push_back(v); });
            int64_t n = I.toInt(a[1], 0).i;
            std::shuffle(items.begin(), items.end(), engine());
            if (n > static_cast<int64_t>(items.size())) n = static_cast<int64_t>(items.size());
            items.resize(static_cast<size_t>(n));
            return mkList(std::move(items));
        }, 2, 2);
        add(m, "normal", [](Interp& I, std::vector<Value>& a) {
            double mean = num(I, a, 0, 0.0), sd = num(I, a, 1, 1.0);
            std::normal_distribution<double> d(mean, sd);
            return Value::number(d(engine()));
        }, 0, 2);
        add(m, "uuid", [](Interp&, std::vector<Value>&) {
            static const char* hex = "0123456789abcdef";
            std::uniform_int_distribution<int> hd(0, 15);
            std::string s;
            for (int i = 0; i < 8; ++i) s += hex[hd(engine())];
            s += "-";
            for (int i = 0; i < 4; ++i) s += hex[hd(engine())];
            s += "-4";
            for (int i = 0; i < 3; ++i) s += hex[hd(engine())];
            s += "-a";
            for (int i = 0; i < 3; ++i) s += hex[hd(engine())];
            s += "-";
            for (int i = 0; i < 12; ++i) s += hex[hd(engine())];
            return mkStr(s);
        }, 0, 0);
    });
}

} // namespace k7