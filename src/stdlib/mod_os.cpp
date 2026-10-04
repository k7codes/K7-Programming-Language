#include "k7/modules.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

namespace k7 {

namespace {

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("os.") + name, std::move(fn), mn, mx);
}

} // namespace

void registerOsModule(Interp& I) {
    I.registerModule("os", [&](ModuleObj& m) {
        add(m, "exit", [](Interp&, std::vector<Value>& a) -> Value {
            throw ExitRequest{ a.empty() ? 0 : static_cast<int>(a[0].isInt() ? a[0].i : 0) };
        }, 0, 1);
        add(m, "getenv", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) return mkStr("");
            const char* v = std::getenv(I.strOf(a[0], 0).c_str());
            return mkStr(v ? std::string(v) : std::string(""));
        }, 0, 1);
        add(m, "setenv", [](Interp& I, std::vector<Value>& a) {
            if (a.size() < 2) I.error("TipHatasi", "os.setenv(ad, deger) arguman ister");
#ifdef _WIN32
            _putenv_s(I.strOf(a[0], 0).c_str(), I.strOf(a[1], 0).c_str());
#else
            setenv(I.strOf(a[0], 0).c_str(), I.strOf(a[1], 0).c_str(), 1);
#endif
            return Value::nil();
        }, 2, 2);
        add(m, "args", [](Interp& I, std::vector<Value>&) {
            Value* v = I.globals()->lookup("argv");
            return v ? *v : mkList();
        }, 0, 0);
        add(m, "cpu_count", [](Interp&, std::vector<Value>&) {
            unsigned hc = std::thread::hardware_concurrency();
            return Value::integer(hc ? static_cast<int64_t>(hc) : 1);
        }, 0, 0);
        add(m, "platform", [](Interp&, std::vector<Value>&) {
#if defined(_WIN32)
            return mkStr("windows");
#elif defined(__APPLE__)
            return mkStr("macos");
#else
            return mkStr("linux");
#endif
        }, 0, 0);
        add(m, "version", [](Interp&, std::vector<Value>&) {
            return mkStr("K7 0.1 (MSVC)");
        }, 0, 0);
        add(m, "abort", [](Interp&, std::vector<Value>&) -> Value {
            std::abort();
        }, 0, 0);
    });
}

} // namespace k7