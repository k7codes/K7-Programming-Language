#include "k7/modules.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <thread>

namespace k7 {

namespace {

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("time.") + name, std::move(fn), mn, mx);
}

int64_t toMS(std::vector<Value>& a, size_t i) {
    if (i >= a.size()) return 0;
    if (a[i].isInt()) return a[i].i;
    if (a[i].isFloat()) return static_cast<int64_t>(a[i].d * 1000.0);
    return 0;
}

std::tm toTm(const Value& v) {
    std::tm t{};
    if (!v.isInt()) return t;
    int64_t secs = v.i;
    std::time_t tt = static_cast<std::time_t>(secs);
#ifdef _WIN32
    localtime_s(&t, &tt);
#else
    localtime_r(&tt, &t);
#endif
    return t;
}

Value nowSeconds() {
    return Value::integer(static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));
}

std::string fmtTime(const Value& v, const std::string& f) {
    std::tm t = toTm(v);
    char buf[256];
    std::size_t n = std::strftime(buf, sizeof(buf), f.c_str(), &t);
    return std::string(buf, n);
}

} // namespace

void registerTimeModule(Interp& I) {
    I.registerModule("time", [&](ModuleObj& m) {
        add(m, "now", [](Interp& I, std::vector<Value>& a) {
            auto d = std::chrono::system_clock::now().time_since_epoch();
            return Value::integer(static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(d).count()));
        }, 0, 0);
        add(m, "now_ms", [](Interp&, std::vector<Value>&) {
            auto d = std::chrono::system_clock::now().time_since_epoch();
            return Value::integer(static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(d).count()));
        }, 0, 0);
        add(m, "mono", [](Interp&, std::vector<Value>&) {
            auto d = std::chrono::steady_clock::now().time_since_epoch();
            return Value::number(std::chrono::duration<double>(d).count());
        }, 0, 0);
        add(m, "sleep", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "time.sleep(ms) arguman ister");
            double s = a[0].isInt() ? static_cast<double>(a[0].i) / 1000.0 : a[0].asNum();
            std::this_thread::sleep_for(std::chrono::duration<double>(s));
            return Value::nil();
        }, 1, 1);
        add(m, "year", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_year + 1900);
        }, 0, 1);
        add(m, "month", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_mon + 1);
        }, 0, 1);
        add(m, "day", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_mday);
        }, 0, 1);
        add(m, "hour", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_hour);
        }, 0, 1);
        add(m, "minute", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_min);
        }, 0, 1);
        add(m, "second", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_sec);
        }, 0, 1);
        add(m, "weekday", [](Interp&, std::vector<Value>& a) {
            std::tm t = toTm(a.empty() ? nowSeconds() : a[0]);
            return Value::integer(t.tm_wday);      // 0 = Pazar
        }, 0, 1);
        add(m, "format", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "time.format(sekil[, zaman]) arguman ister");
            std::string f = I.strOf(a[0], 0);
            Value t = a.size() > 1 ? a[1]
                     : Value::integer(static_cast<int64_t>(
                           std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count()));
            return mkStr(fmtTime(t, f));
        }, 1, 2);
        add(m, "strftime", [](Interp& I, std::vector<Value>& a) {
            return mkStr(fmtTime(a.size() > 1 ? a[1] : a[0], I.strOf(a[0], 0)));
        }, 1, 2);
    });
}

} // namespace k7