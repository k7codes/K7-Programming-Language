#include "k7/modules.h"

#include <cmath>
#include <sstream>

namespace k7 {

namespace {

struct JsonParser {
    const std::string& s;
    size_t p = 0;
    Interp& I;

    JsonParser(const std::string& src, Interp& in) : s(src), I(in) {}

    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
            ++p;
    }

    [[noreturn]] void fail(const char* msg) {
        I.error("JsonHatasi",
                std::string(msg) + " (konum " + std::to_string(p) + ")");
    }

    char peek() { return p < s.size() ? s[p] : '\0'; }

    Value parse() {
        ws();
        Value v = value();
        ws();
        if (p != s.size()) fail("JSON sonunda fazladan karakter");
        return v;
    }

    Value value() {
        ws();
        if (p >= s.size()) fail("JSON bitti");
        char c = s[p];
        switch (c) {
            case '{': return object();
            case '[': return array();
            case '"': return mkStr(string());
            case 't':
                if (s.compare(p, 4, "true") == 0) { p += 4; return Value::boolean(true); }
                fail("gecersiz JSON");
            case 'f':
                if (s.compare(p, 5, "false") == 0) { p += 5; return Value::boolean(false); }
                fail("gecersiz JSON");
            case 'n':
                if (s.compare(p, 4, "null") == 0) { p += 4; return Value::nil(); }
                fail("gecersiz JSON");
            default: return number();
        }
    }

    Value number() {
        size_t st = p;
        if (peek() == '-' || peek() == '+') ++p;
        bool isFloat = false;
        while (p < s.size()) {
            char c = s[p];
            if (std::isdigit(static_cast<unsigned char>(c))) { ++p; continue; }
            if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                isFloat = true; ++p; continue;
            }
            break;
        }
        if (p == st) fail("gecersiz sayi");
        std::string num = s.substr(st, p - st);
        if (isFloat) return Value::number(std::strtod(num.c_str(), nullptr));
        return Value::integer(static_cast<int64_t>(std::strtoll(num.c_str(), nullptr, 10)));
    }

    std::string string() {
        if (peek() != '"') fail("metin bekleniyordu");
        ++p;
        std::string out;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\') {
                ++p;
                if (p >= s.size()) fail("kapanmamis kacis");
                char e = s[p++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        if (p + 4 > s.size()) fail("gecersiz unicode kacisi");
                        unsigned cp = static_cast<unsigned>(
                            std::strtoul(s.substr(p, 4).c_str(), nullptr, 16));
                        p += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= s.size() &&
                            s[p] == '\\' && s[p + 1] == 'u') {
                            unsigned lo = static_cast<unsigned>(
                                std::strtoul(s.substr(p + 2, 4).c_str(), nullptr, 16));
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                p += 6;
                            }
                        }
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xF0 | (cp >> 18));
                            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail("bilinmeyen kacis");
                }
            } else {
                out += s[p++];
            }
        }
        if (p >= s.size()) fail("kapanmamis metin");
        ++p;
        return out;
    }

    Value array() {
        ++p;   // [
        std::vector<Value> items;
        ws();
        if (peek() == ']') { ++p; return mkList(std::move(items)); }
        while (true) {
            items.push_back(value());
            ws();
            if (peek() == ',') { ++p; continue; }
            if (peek() == ']') { ++p; break; }
            fail("',' veya ']' bekleniyordu");
        }
        return mkList(std::move(items));
    }

    Value object() {
        ++p;   // {
        auto m = mkMap();
        auto* mp = asMap(m);
        ws();
        if (peek() == '}') { ++p; return m; }
        while (true) {
            ws();
            Value k = mkStr(string());
            ws();
            if (peek() != ':') fail("':' bekleniyordu");
            ++p;
            mp->items.emplace_back(k, value());
            ws();
            if (peek() == ',') { ++p; continue; }
            if (peek() == '}') { ++p; break; }
            fail("',' veya '}' bekleniyordu");
        }
        mp->reindex();
        return m;
    }
};

void escapeJson(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void dump(Interp& I, const Value& v, std::string& out, int indent, int depth) {
    std::string nl = indent > 0 ? "\n" : "";
    std::string pad = indent > 0 ? std::string(static_cast<size_t>(indent * (depth + 1)), ' ') : "";
    std::string padEnd = indent > 0 ? std::string(static_cast<size_t>(indent * depth), ' ') : "";

    switch (v.t) {
        case VT::Nil: out += "null"; return;
        case VT::Bool: out += v.b ? "true" : "false"; return;
        case VT::Int: out += std::to_string(v.i); return;
        case VT::Float: {
            double d = v.d;
            if (std::isnan(d) || std::isinf(d)) { out += "null"; return; }
            std::ostringstream os;
            os.precision(17);
            os << d;
            std::string t = os.str();
            if (t.find_first_of(".eE") == std::string::npos) t += ".0";
            out += t;
            return;
        }
        case VT::Str: escapeJson(asStr(v)->s, out); return;
        case VT::List:
        case VT::Tuple: {
            const std::vector<Value>* items;
            if (v.isList()) items = &asList(v)->items; else items = &asTuple(v)->items;
            if (items->empty()) { out += "[]"; return; }
            out += "[" + nl;
            for (size_t i = 0; i < items->size(); ++i) {
                out += pad;
                dump(I, (*items)[i], out, indent, depth + 1);
                if (i + 1 < items->size()) out += ",";
                out += nl;
            }
            out += padEnd + "]";
            return;
        }
        case VT::Map: {
            auto* m = asMap(v);
            if (m->items.empty()) { out += "{}"; return; }
            out += "{" + nl;
            for (size_t i = 0; i < m->items.size(); ++i) {
                out += pad;
                if (m->items[i].first.isStr()) escapeJson(asStr(m->items[i].first)->s, out);
                else dump(I, m->items[i].first, out, indent, depth + 1);
                out += indent > 0 ? ": " : ":";
                dump(I, m->items[i].second, out, indent, depth + 1);
                if (i + 1 < m->items.size()) out += ",";
                out += nl;
            }
            out += padEnd + "}";
            return;
        }
        case VT::Object: {
            auto* o = asObject(v);
            out += "{" + nl;
            bool first = true;
            for (auto& kv : o->fields) {
                if (kv.second.isFn()) continue;
                if (!first) out += ",";
                first = false;
                out += nl + pad;
                escapeJson(kv.first, out);
                out += indent > 0 ? ": " : ":";
                dump(I, kv.second, out, indent, depth + 1);
            }
            out += nl + padEnd + "}";
            return;
        }
        case VT::Class: {
            auto* c = asClass(v);
            out += "\"" + c->name + "\"";
            return;
        }
        default:
            I.error("TipHatasi", I.typeNameOf(v) + " JSON'a cevrilemez");
    }
}

void add(ModuleObj& m, const char* name, NativeFn fn, int mn = 0, int mx = -1) {
    if (!m.members.count(name)) m.order.push_back(name);
    m.members[name] = mkNative(std::string("json.") + name, std::move(fn), mn, mx);
}

} // namespace

void registerJsonModule(Interp& I) {
    I.registerModule("json", [&](ModuleObj& m) {
        add(m, "parse", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "json.parse(metin) arguman ister");
            std::string src = I.strOf(a[0], 0);
            JsonParser jp(src, I);
            return jp.parse();
        }, 1, 1);
        add(m, "stringify", [](Interp& I, std::vector<Value>& a) {
            if (a.empty()) I.error("TipHatasi", "json.stringify(deger) arguman ister");
            int indent = a.size() > 1 ? static_cast<int>(I.toInt(a[1], 0).i) : 0;
            std::string out;
            dump(I, a[0], out, indent, 0);
            return mkStr(out);
        }, 1, 2);
        add(m, "dump", [](Interp& I, std::vector<Value>& a) {
            int indent = a.size() > 1 ? static_cast<int>(I.toInt(a[1], 0).i) : 2;
            std::string out;
            dump(I, a[0], out, indent, 0);
            return mkStr(out);
        }, 1, 2);
    });
}

} // namespace k7