#include "k7/modules.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace k7 {

namespace fs = std::filesystem;

namespace {

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("fs.") + name, std::move(fn), mn, mx);
}

std::string p1(Interp& I, std::vector<Value>& a, size_t i = 0) {
    if (a.size() <= i) I.error("TipHatasi", "fs fonksiyonu dosya yolu argumani ister");
    return I.strOf(a[i], 0);
}

Value entryToValue(Interp& I, const fs::directory_entry& e) {
    std::error_code ec;
    auto m = mkMap();
    asMap(m)->items.emplace_back(mkStr("name"), mkStr(e.path().filename().string()));
    asMap(m)->items.emplace_back(mkStr("path"), mkStr(e.path().string()));
    asMap(m)->items.emplace_back(mkStr("is_dir"), Value::boolean(e.is_directory(ec)));
    asMap(m)->items.emplace_back(mkStr("is_file"), Value::boolean(e.is_regular_file(ec)));
    uintmax_t sz = 0;
    std::string ext = e.path().extension().string();
    if (e.is_regular_file(ec)) sz = e.file_size(ec);
    asMap(m)->items.emplace_back(mkStr("size"), Value::integer(static_cast<int64_t>(sz)));
    asMap(m)->items.emplace_back(mkStr("ext"), mkStr(ext.empty() ? "" : ext.substr(1)));
    asMap(m)->reindex();
    return m;
}

} // namespace

void registerFsModule(Interp& I) {
    I.registerModule("fs", [&](ModuleObj& m) {
        add(m, "read", [](Interp& I, std::vector<Value>& a) {
            std::ifstream in(p1(I, a), std::ios::binary);
            if (!in) I.error("IOHatasi", "dosya okunamadi: " + p1(I, a));
            std::ostringstream ss;
            ss << in.rdbuf();
            return mkStr(ss.str());
        }, 1, 1);
        add(m, "lines", [](Interp& I, std::vector<Value>& a) {
            std::ifstream in(p1(I, a));
            if (!in) I.error("IOHatasi", "dosya okunamadi: " + p1(I, a));
            std::vector<Value> out;
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                out.push_back(mkStr(line));
            }
            return mkList(std::move(out));
        }, 1, 1);
        add(m, "write", [](Interp& I, std::vector<Value>& a) {
            std::ofstream out(p1(I, a), std::ios::binary);
            if (!out) I.error("IOHatasi", "dosya yazilamadi: " + p1(I, a));
            out << I.strOf(a.size() > 1 ? a[1] : Value::nil(), 0);
            return Value::nil();
        }, 2, 2);
        add(m, "append", [](Interp& I, std::vector<Value>& a) {
            std::ofstream out(p1(I, a), std::ios::binary | std::ios::app);
            if (!out) I.error("IOHatasi", "dosya yazilamadi: " + p1(I, a));
            out << I.strOf(a.size() > 1 ? a[1] : Value::nil(), 0);
            return Value::nil();
        }, 2, 2);
        add(m, "exists", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            return Value::boolean(fs::exists(fs::path(p1(I, a)), ec));
        }, 1, 1);
        add(m, "is_dir", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            return Value::boolean(fs::is_directory(fs::path(p1(I, a)), ec));
        }, 1, 1);
        add(m, "is_file", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            return Value::boolean(fs::is_regular_file(fs::path(p1(I, a)), ec));
        }, 1, 1);
        add(m, "size", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            auto sz = fs::file_size(fs::path(p1(I, a)), ec);
            if (ec) I.error("IOHatasi", "boyut alinamadi: " + p1(I, a));
            return Value::integer(static_cast<int64_t>(sz));
        }, 1, 1);
        add(m, "mkdir", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            fs::create_directories(fs::path(p1(I, a)), ec);
            return Value::nil();
        }, 1, 1);
        add(m, "remove", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            bool ok = fs::remove(fs::path(p1(I, a)), ec);
            if (!ok) I.error("IOHatasi", "silinemedi: " + p1(I, a));
            return Value::nil();
        }, 1, 1);
        add(m, "rename", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            fs::rename(fs::path(p1(I, a, 0)), fs::path(p1(I, a, 1)), ec);
            if (ec) I.error("IOHatasi", "yeniden adlandirilamadi: " + ec.message());
            return Value::nil();
        }, 2, 2);
        add(m, "copy", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            fs::copy_file(fs::path(p1(I, a, 0)), fs::path(p1(I, a, 1)),
                          fs::copy_options::overwrite_existing, ec);
            if (ec) I.error("IOHatasi", "kopyalanamadi: " + ec.message());
            return Value::nil();
        }, 2, 2);
        add(m, "list", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            fs::path dir = a.empty() ? fs::current_path(ec) : fs::path(p1(I, a));
            if (!fs::is_directory(dir, ec))
                I.error("IOHatasi", "dizin degil: " + dir.string());
            std::vector<Value> out;
            for (const auto& e : fs::directory_iterator(dir, ec))
                out.push_back(entryToValue(I, e));
            std::sort(out.begin(), out.end(), [](const Value& x, const Value& y) {
                const Value* nx = asMap(x)->get(mkStr("name"));
                const Value* ny = asMap(y)->get(mkStr("name"));
                if (!nx || !ny) return false;
                return asStr(*nx)->s < asStr(*ny)->s;
            });
            return mkList(std::move(out));
        }, 0, 1);
        add(m, "walk", [](Interp& I, std::vector<Value>& a) {
            std::error_code ec;
            fs::path root = a.empty() ? fs::current_path(ec) : fs::path(p1(I, a));
            std::vector<Value> out;
            for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
                if (ec) break;
                out.push_back(entryToValue(I, e));
            }
            return mkList(std::move(out));
        }, 0, 1);
        add(m, "cwd", [](Interp&, std::vector<Value>&) {
            std::error_code ec;
            return mkStr(fs::current_path(ec).string());
        }, 0, 0);
        add(m, "join", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "fs.join(...) arguman ister");
            fs::path p = fs::path(I.strOf(a[0], 0));
            for (size_t i = 1; i < a.size(); ++i) p /= I.strOf(a[i], 0);
            return mkStr(p.string());
        }, 1, -1);
        add(m, "name", [](Interp& I, std::vector<Value>& a) {
            return mkStr(fs::path(p1(I, a)).filename().string());
        }, 1, 1);
        add(m, "stem", [](Interp& I, std::vector<Value>& a) {
            return mkStr(fs::path(p1(I, a)).stem().string());
        }, 1, 1);
        add(m, "ext", [](Interp& I, std::vector<Value>& a) {
            std::string e = fs::path(p1(I, a)).extension().string();
            return mkStr(e.empty() ? "" : e.substr(1));
        }, 1, 1);
        add(m, "temp_dir", [](Interp&, std::vector<Value>&) {
            std::error_code ec;
            return mkStr(fs::temp_directory_path(ec).string());
        }, 0, 0);
    });
}

} // namespace k7