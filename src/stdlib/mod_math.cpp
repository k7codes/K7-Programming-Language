#include "k7/modules.h"

#include <cmath>
#include <limits>

namespace k7 {

namespace {

double need(Interp& I, std::vector<Value>& a, size_t i, const char* who) {
    if (a.size() <= i || !a[i].isNum())
        I.error("TipHatasi", std::string("math.") + who + "() sayi argumani ister");
    return a[i].asNum();
}

int64_t needI(Interp& I, std::vector<Value>& a, size_t i, const char* who) {
    if (a.size() <= i || !a[i].isNum())
        I.error("TipHatasi", std::string("math.") + who + "() sayi argumani ister");
    return a[i].isInt() ? a[i].i : static_cast<int64_t>(a[i].d);
}

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("math.") + name, std::move(fn), mn, mx);
}

void const_(ModuleObj& m, const char* name, double v) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = Value::number(v);
}

} // namespace

void registerMathModule(Interp& I) {
    I.registerModule("math", [&](ModuleObj& m) {
        add(m, "sqrt", [](Interp& I, std::vector<Value>& a) {
            double x = need(I, a, 0, "sqrt");
            if (x < 0) I.error("DegerHatasi", "math.sqrt() negatif arguman alamaz");
            return Value::number(std::sqrt(x));
        }, 1, 1);
        add(m, "cbrt", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::cbrt(need(I, a, 0, "cbrt")));
        }, 1, 1);
        add(m, "abs", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::fabs(need(I, a, 0, "abs")));
        }, 1, 1);
        add(m, "floor", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::floor(need(I, a, 0, "floor")));
        }, 1, 1);
        add(m, "ceil", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::ceil(need(I, a, 0, "ceil")));
        }, 1, 1);
        add(m, "round", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::round(need(I, a, 0, "round")));
        }, 1, 1);
        add(m, "trunc", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::trunc(need(I, a, 0, "trunc")));
        }, 1, 1);
        add(m, "pow", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::pow(need(I, a, 0, "pow"), need(I, a, 1, "pow")));
        }, 2, 2);
        add(m, "exp", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::exp(need(I, a, 0, "exp")));
        }, 1, 1);
        add(m, "log", [](Interp& I, std::vector<Value>& a) {
            double x = need(I, a, 0, "log");
            if (x <= 0) I.error("DegerHatasi", "math.log() pozitif arguman ister");
            if (a.size() > 1) {
                double b = need(I, a, 1, "log");
                return Value::number(std::log(x) / std::log(b));
            }
            return Value::number(std::log(x));
        }, 1, 2);
        add(m, "log2", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::log2(need(I, a, 0, "log2")));
        }, 1, 1);
        add(m, "log10", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::log10(need(I, a, 0, "log10")));
        }, 1, 1);
        add(m, "sin",   [](Interp& I, std::vector<Value>& a) { return Value::number(std::sin(need(I, a, 0, "sin"))); }, 1, 1);
        add(m, "cos",   [](Interp& I, std::vector<Value>& a) { return Value::number(std::cos(need(I, a, 0, "cos"))); }, 1, 1);
        add(m, "tan",   [](Interp& I, std::vector<Value>& a) { return Value::number(std::tan(need(I, a, 0, "tan"))); }, 1, 1);
        add(m, "asin",  [](Interp& I, std::vector<Value>& a) { return Value::number(std::asin(need(I, a, 0, "asin"))); }, 1, 1);
        add(m, "acos",  [](Interp& I, std::vector<Value>& a) { return Value::number(std::acos(need(I, a, 0, "acos"))); }, 1, 1);
        add(m, "atan",  [](Interp& I, std::vector<Value>& a) { return Value::number(std::atan(need(I, a, 0, "atan"))); }, 1, 1);
        add(m, "atan2", [](Interp& I, std::vector<Value>& a) {
            return Value::number(std::atan2(need(I, a, 0, "atan2"), need(I, a, 1, "atan2")));
        }, 2, 2);
        add(m, "sinh", [](Interp& I, std::vector<Value>& a) { return Value::number(std::sinh(need(I, a, 0, "sinh"))); }, 1, 1);
        add(m, "cosh", [](Interp& I, std::vector<Value>& a) { return Value::number(std::cosh(need(I, a, 0, "cosh"))); }, 1, 1);
        add(m, "tanh", [](Interp& I, std::vector<Value>& a) { return Value::number(std::tanh(need(I, a, 0, "tanh"))); }, 1, 1);
        add(m, "gcd", [](Interp& I, std::vector<Value>& a) {
            int64_t x = needI(I, a, 0, "gcd"), y = needI(I, a, 1, "gcd");
            if (x < 0) x = -x;
            if (y < 0) y = -y;
            while (y) { int64_t t = x % y; x = y; y = t; }
            return Value::integer(x);
        }, 2, 2);
        add(m, "lcm", [](Interp& I, std::vector<Value>& a) {
            int64_t x = needI(I, a, 0, "lcm"), y = needI(I, a, 1, "lcm");
            if (x < 0) x = -x;
            if (y < 0) y = -y;
            if (x == 0 || y == 0) return Value::integer(0);
            int64_t p = x, q = y;
            while (q) { int64_t t = p % q; p = q; q = t; }
            return Value::integer(x / p * y);
        }, 2, 2);
        add(m, "factorial", [](Interp& I, std::vector<Value>& a) {
            int64_t n = needI(I, a, 0, "factorial");
            if (n < 0) I.error("DegerHatasi", "math.factorial() negatif arguman alamaz");
            int64_t r = 1;
            for (int64_t i = 2; i <= n; ++i) r *= i;
            return Value::integer(r);
        }, 1, 1);
        add(m, "is_nan", [](Interp& I, std::vector<Value>& a) {
            return Value::boolean(std::isnan(need(I, a, 0, "is_nan")));
        }, 1, 1);
        add(m, "is_inf", [](Interp& I, std::vector<Value>& a) {
            return Value::boolean(std::isinf(need(I, a, 0, "is_inf")));
        }, 1, 1);
        add(m, "is_finite", [](Interp& I, std::vector<Value>& a) {
            return Value::boolean(std::isfinite(need(I, a, 0, "is_finite")));
        }, 1, 1);
        add(m, "clamp", [](Interp& I, std::vector<Value>& a) {
            double x = need(I, a, 0, "clamp"), lo = need(I, a, 1, "clamp"),
                   hi = need(I, a, 2, "clamp");
            return Value::number(x < lo ? lo : (x > hi ? hi : x));
        }, 3, 3);
        add(m, "lerp", [](Interp& I, std::vector<Value>& a) {
            double x = need(I, a, 0, "lerp"), y = need(I, a, 1, "lerp"),
                   t = need(I, a, 2, "lerp");
            return Value::number(x + (y - x) * t);
        }, 3, 3);

        const_(m, "pi",  3.14159265358979323846);
        const_(m, "e",   2.71828182845904523536);
        const_(m, "tau", 6.28318530717958647692);
        const_(m, "phi", 1.61803398874989484820);
        const_(m, "inf", std::numeric_limits<double>::infinity());
        const_(m, "nan", std::numeric_limits<double>::quiet_NaN());
    });
}

} // namespace k7