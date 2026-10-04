#include "k7/value.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace k7 {

// ---------------------------------------------------------------
// Metin havuzu (intern)
// ---------------------------------------------------------------

const std::string& internStr(const std::string& s) {
    static std::unordered_set<std::string> pool;
    return *pool.insert(s).first;
}

Value mkStr(const std::string& s) {
    return Value::obj(std::make_shared<StrObj>(internStr(s)));
}

Value mkStrShared(std::shared_ptr<StrObj> s) { return Value::obj(std::move(s)); }

Value mkList(std::vector<Value> items) {
    return Value::obj(std::make_shared<ListObj>(std::move(items)));
}

Value mkTuple(std::vector<Value> items) {
    return Value::obj(std::make_shared<TupleObj>(std::move(items)));
}

Value mkMap()  { return Value::obj(std::make_shared<MapObj>()); }
Value mkSet()  { return Value::obj(std::make_shared<SetObj>()); }

Value mkError(const std::string& kind, const std::string& msg) {
    auto e = std::make_shared<ErrorObj>();
    e->kind = kind;
    e->msg = msg;
    return Value::obj(std::move(e));
}

Value mkNative(const std::string& name,
               std::function<Value(Interp&, std::vector<Value>&)> fn,
               int minArgs, int maxArgs) {
    auto n = std::make_shared<NativeObj>();
    n->name = name;
    n->fn = std::move(fn);
    n->minArgs = minArgs;
    n->maxArgs = maxArgs;
    return Value::obj(std::move(n));
}

const char* typeName(VT t) {
    switch (t) {
        case VT::Nil:    return "nil";
        case VT::Bool:   return "bool";
        case VT::Int:    return "int";
        case VT::Float:  return "float";
        case VT::Str:    return "str";
        case VT::List:   return "list";
        case VT::Tuple:  return "tuple";
        case VT::Map:    return "map";
        case VT::Set:    return "set";
        case VT::Range:  return "range";
        case VT::Fn:     return "fn";
        case VT::Native: return "fn";
        case VT::Bound:  return "method";
        case VT::Object: return "object";
        case VT::Class:  return "class";
        case VT::Module: return "module";
        case VT::Error:  return "error";
        case VT::Cell:   return "cell";
    }
    return "bilinmeyen";
}

// ---------------------------------------------------------------
// Hash & esitlik
// ---------------------------------------------------------------

static uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ULL) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static uint64_t hashCombine(uint64_t h, uint64_t v) {
    return fnv1a(&v, sizeof(v), h);
}

uint64_t hashValue(const Value& v) {
    uint64_t h = 1469598103934665603ULL;
    uint8_t tag = static_cast<uint8_t>(v.t);
    h = hashCombine(h, tag);
    switch (v.t) {
        case VT::Nil:   break;
        case VT::Bool:  h = hashCombine(h, static_cast<uint64_t>(v.b ? 1 : 0)); break;
        case VT::Int:   h = hashCombine(h, static_cast<uint64_t>(v.i)); break;
        case VT::Float: {
            double d = (v.d == 0.0) ? 0.0 : v.d;   // -0.0 == 0.0
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(bits));
            h = hashCombine(h, bits);
            break;
        }
        case VT::Str: {
            const std::string& s = asStr(v)->s;
            h = hashCombine(h, s.size());
            h = fnv1a(s.data(), s.size(), h);
            break;
        }
        case VT::Range: {
            auto* r = asRange(v);
            h = hashCombine(h, static_cast<uint64_t>(r->start));
            h = hashCombine(h, static_cast<uint64_t>(r->stop));
            h = hashCombine(h, static_cast<uint64_t>(r->step));
            break;
        }
        default: {
            // Konteyner turleri: sirali hash (deep)
            switch (v.t) {
                case VT::List:
                case VT::Tuple: {
                    const std::vector<Value>* items =
                        (v.t == VT::List) ? &asList(v)->items : &asTuple(v)->items;
                    h = hashCombine(h, items->size());
                    for (const auto& it : *items) h = hashCombine(h, hashValue(it));
                    break;
                }
                case VT::Map: {
                    auto* m = asMap(v);
                    h = hashCombine(h, m->items.size());
                    for (const auto& kv : m->items) {
                        h = hashCombine(h, hashValue(kv.first));
                        h = hashCombine(h, hashValue(kv.second));
                    }
                    break;
                }
                case VT::Set: {
                    auto* s = asSet(v);
                    h = hashCombine(h, s->items.size());
                    for (const auto& it : s->items) h = hashCombine(h, hashValue(it));
                    break;
                }
                default:
                    h = hashCombine(h, static_cast<uint64_t>(
                        reinterpret_cast<uintptr_t>(v.o.get())));
                    break;
            }
            break;
        }
    }
    return h;
}

static bool seqEquals(const std::vector<Value>& a, const std::vector<Value>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!valueEquals(a[i], b[i])) return false;
    return true;
}

bool valueEquals(const Value& a, const Value& b) {
    // Sayisal esitlik: int 1 == float 1.0
    if (a.isNum() && b.isNum()) {
        if (a.isInt() && b.isInt()) return a.i == b.i;
        return a.asNum() == b.asNum();
    }
    if (a.t != b.t) return false;
    switch (a.t) {
        case VT::Nil:   return true;
        case VT::Bool:  return a.b == b.b;
        case VT::Int:   return a.i == b.i;
        case VT::Float: return a.d == b.d;
        case VT::Str:   return asStr(a)->s == asStr(b)->s;
        case VT::List:  return seqEquals(asList(a)->items, asList(b)->items);
        case VT::Tuple: return seqEquals(asTuple(a)->items, asTuple(b)->items);
        case VT::Set: {
            auto* x = asSet(a);
            auto* y = asSet(b);
            if (x->items.size() != y->items.size()) return false;
            for (auto& it : x->items) if (!y->contains(it)) return false;
            return true;
        }
        case VT::Range: {
            auto* x = asRange(a);
            auto* y = asRange(b);
            return x->start == y->start && x->stop == y->stop && x->step == y->step;
        }
        case VT::Map: {
            auto* x = asMap(a);
            auto* y = asMap(b);
            if (x->items.size() != y->items.size()) return false;
            for (auto& kv : x->items) {
                const Value* other = y->get(kv.first);
                if (!other || !valueEquals(kv.second, *other)) return false;
            }
            return true;
        }
        default: return a.o == b.o;
    }
}

bool strictEquals(const Value& a, const Value& b) {
    if (a.t != b.t) return false;
    return valueEquals(a, b);
}

int compareValues(const Value& a, const Value& b) {
    if (a.isNum() && b.isNum()) {
        if (a.isInt() && b.isInt()) return a.i < b.i ? -1 : (a.i > b.i ? 1 : 0);
        double x = a.asNum(), y = b.asNum();
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (a.isStr() && b.isStr()) {
        int c = asStr(a)->s.compare(asStr(b)->s);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    if ((a.isList() || a.isTuple()) && (b.isList() || b.isTuple())) {
        const std::vector<Value>* x =
            a.isList() ? &asList(a)->items : &asTuple(a)->items;
        const std::vector<Value>* y =
            b.isList() ? &asList(b)->items : &asTuple(b)->items;
        size_t n = std::min(x->size(), y->size());
        for (size_t i = 0; i < n; ++i) {
            int c = compareValues((*x)[i], (*y)[i]);
            if (c != 0) return c;
        }
        return x->size() < y->size() ? -1 : (x->size() > y->size() ? 1 : 0);
    }
    return -2;   // karsilastirilamaz
}

// ---------------------------------------------------------------
// Harita
// ---------------------------------------------------------------

void MapObj::reindex() {
    index.clear();
    index.reserve(items.size() * 2);
    for (uint32_t i = 0; i < items.size(); ++i)
        index[hashValue(items[i].first)].push_back(i);
    indexed = true;
}

size_t MapObj::findKey(const Value& k) const {
    if (!indexed) const_cast<MapObj*>(this)->reindex();
    auto it = index.find(hashValue(k));
    if (it == index.end()) return static_cast<size_t>(-1);
    for (uint32_t pos : it->second) {
        if (valueEquals(items[pos].first, k)) return pos;
    }
    return static_cast<size_t>(-1);
}

const Value* MapObj::get(const Value& k) const {
    size_t i = findKey(k);
    return i == static_cast<size_t>(-1) ? nullptr : &items[i].second;
}

void MapObj::set(const Value& k, const Value& v) {
    size_t i = findKey(k);
    if (i != static_cast<size_t>(-1)) {
        items[i].second = v;
        return;
    }
    items.emplace_back(k, v);
    if (indexed) index[hashValue(k)].push_back(static_cast<uint32_t>(items.size() - 1));
}

bool MapObj::erase(const Value& k) {
    size_t i = findKey(k);
    if (i == static_cast<size_t>(-1)) return false;
    items.erase(items.begin() + static_cast<ptrdiff_t>(i));
    reindex();
    return true;
}

// ---------------------------------------------------------------
// Kume
// ---------------------------------------------------------------

bool SetObj::contains(const Value& k) const {
    for (auto& it : items) if (valueEquals(it, k)) return true;
    return false;
}

void SetObj::add(const Value& k) {
    if (!contains(k)) items.push_back(k);
}

bool SetObj::erase(const Value& k) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (valueEquals(items[i], k)) {
            items.erase(items.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------
// Aralik
// ---------------------------------------------------------------

int64_t RangeObj::length() const {
    if (step == 0) return 0;
    if (step > 0) return stop > start ? (stop - start + step - 1) / step : 0;
    return start > stop ? (start - stop - step - 1) / (-step) : 0;
}

int64_t RangeObj::at(int64_t i) const { return start + i * step; }

bool RangeObj::contains(int64_t v) const {
    if (length() == 0) return false;
    int64_t d = v - start;
    if (step > 0 ? (d < 0 || d >= stop - start) : (d > 0 || d <= stop - start))
        return false;
    return d % step == 0;
}

// ---------------------------------------------------------------
// Sinif metotlari
// ---------------------------------------------------------------

Value ClassObj::findMethod(const std::string& name) const {
    const ClassObj* c = this;
    while (c) {
        auto it = c->methods.find(name);
        if (it != c->methods.end()) return it->second;
        c = c->super.get();
    }
    return Value::nil();
}

Value ClassObj::fieldDefault(const std::string& name) const {
    const ClassObj* c = this;
    while (c) {
        auto it = c->fieldDefaults.find(name);
        if (it != c->fieldDefaults.end()) return it->second;
        c = c->super.get();
    }
    return Value::nil();
}

// ---------------------------------------------------------------
// Dogruluk & kopyalama
// ---------------------------------------------------------------

bool valueTruthy(const Value& v) {
    switch (v.t) {
        case VT::Nil:   return false;
        case VT::Bool:  return v.b;
        case VT::Int:   return v.i != 0;
        case VT::Float: return v.d != 0.0 && !std::isnan(v.d);
        case VT::Str:   return !asStr(v)->s.empty();
        case VT::List:   return !asList(v)->items.empty();
        case VT::Tuple:  return !asTuple(v)->items.empty();
        case VT::Map:   return !asMap(v)->items.empty();
        case VT::Set:   return !asSet(v)->items.empty();
        case VT::Range: return asRange(v)->length() > 0;
        default:        return true;
    }
}

Value deepCopy(const Value& v) {
    switch (v.t) {
        case VT::List: {
            auto* l = asList(v);
            std::vector<Value> out;
            out.reserve(l->items.size());
            for (auto& it : l->items) out.push_back(deepCopy(it));
            return mkList(std::move(out));
        }
        case VT::Tuple: {
            std::vector<Value> out;
            for (auto& it : asTuple(v)->items) out.push_back(deepCopy(it));
            return mkTuple(std::move(out));
        }
        case VT::Map: {
            auto m = mkMap();
            auto* mp = asMap(m);
            mp->items.reserve(asMap(v)->items.size());
            for (auto& kv : asMap(v)->items)
                mp->items.emplace_back(deepCopy(kv.first), deepCopy(kv.second));
            return m;
        }
        case VT::Set: {
            auto s = mkSet();
            for (auto& it : asSet(v)->items) asSet(s)->add(deepCopy(it));
            return s;
        }
        case VT::Object: {
            auto* o = asObject(v);
            auto nv = std::make_shared<ObjectObj>();
            nv->className = o->className;
            nv->fields = o->fields;
            for (auto& kv : nv->fields)
                if (kv.second.isList() || kv.second.isMap() || kv.second.isSet() ||
                    kv.second.isTuple())
                    kv.second = deepCopy(kv.second);
            return Value::obj(std::move(nv));
        }
        default: return v;
    }
}

// ---------------------------------------------------------------
// Sayi bicimlendirme
// ---------------------------------------------------------------

std::string fmtFloat(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
    if (d == static_cast<int64_t>(d) && std::fabs(d) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld.0", static_cast<long long>(d));
        return buf;
    }
    char buf[40];
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    return buf;
}

// ---------------------------------------------------------------
// Gosterim
// ---------------------------------------------------------------

static void escapeInto(const std::string& s, std::string& out, char quote) {
    out += quote;
    for (unsigned char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '\0': out += "\\0"; break;
            default:
                if (c == static_cast<unsigned char>(quote)) {
                    out += '\\'; out += quote;
                } else if (c < 0x20 || c == 0x7F) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\x%02x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += quote;
}

static std::string reprOf(const Value& v, bool quoteStr);

static std::string objectRepr(const ObjectObj* o) {
    std::string out = o->className.empty() ? std::string("object") : o->className;
    out += "(";
    bool first = true;
    for (const auto& kv : o->fields) {
        if (!first) out += ", ";
        first = false;
        out += kv.first + "=";
        out += reprOf(kv.second, true);
    }
    out += ")";
    return out;
}

static std::string reprOf(const Value& v, bool quoteStr) {
    switch (v.t) {
        case VT::Nil:   return "nil";
        case VT::Bool:  return v.b ? "true" : "false";
        case VT::Int:   return std::to_string(v.i);
        case VT::Float: return fmtFloat(v.d);
        case VT::Str: {
            if (!quoteStr) return asStr(v)->s;
            std::string out;
            escapeInto(asStr(v)->s, out, '"');
            return out;
        }
        case VT::List: {
            std::string out = "[";
            auto* l = asList(v);
            for (size_t i = 0; i < l->items.size(); ++i) {
                if (i) out += ", ";
                out += reprOf(l->items[i], true);
            }
            return out + "]";
        }
        case VT::Tuple: {
            std::string out = "(";
            auto* l = asTuple(v);
            for (size_t i = 0; i < l->items.size(); ++i) {
                if (i) out += ", ";
                out += reprOf(l->items[i], true);
            }
            if (l->items.size() == 1) out += ",";
            return out + ")";
        }
        case VT::Map: {
            std::string out = "{";
            auto* m = asMap(v);
            for (size_t i = 0; i < m->items.size(); ++i) {
                if (i) out += ", ";
                out += reprOf(m->items[i].first, true);
                out += ": ";
                out += reprOf(m->items[i].second, true);
            }
            return out + "}";
        }
        case VT::Set: {
            std::string out = "#{";
            auto* s = asSet(v);
            for (size_t i = 0; i < s->items.size(); ++i) {
                if (i) out += ", ";
                out += reprOf(s->items[i], true);
            }
            return out + "}";
        }
        case VT::Range: {
            auto* r = asRange(v);
            std::string out;
            if (r->hasStart) out += std::to_string(r->start);
            out += "..";
            out += std::to_string(r->stop);
            if (r->step != 1) {
                out += ":";
                out += std::to_string(r->step);
            }
            return out;
        }
        case VT::Fn:     return "<fn " + asFn(v)->name + ">";
        case VT::Native: return "<fn " + asNative(v)->name + ">";
        case VT::Bound:  return "<method>";
        case VT::Class:  return "<class " + asClass(v)->name + ">";
        case VT::Module: return "<module " + asModule(v)->name + ">";
        case VT::Error: {
            auto* e = asErr(v);
            return e->kind + ": " + e->msg;
        }
        case VT::Object: return objectRepr(asObject(v));
        default:         return "<?>";
    }
}

std::string valueToRepr(const Value& v) { return reprOf(v, true); }

std::string valueToString(const Value& v) {
    switch (v.t) {
        case VT::Str: return asStr(v)->s;
        default:      return reprOf(v, false);
    }
}

} // namespace k7