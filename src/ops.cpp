// K7 Language - Runtime operatorleri, erisim ve sekil islemleri
#include "k7/interp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace k7 {

// ===============================================================
// Donusumler
// ===============================================================

Value Interp::toStr(const Value& v, int line) {
    if (v.isStr()) return v;
    if (v.isObj()) {
        Value m = getMember(v, "__str__", line);
        if (m.isFn()) return toStr(callMethod(v, "__str__", {}, line), line);
    }
    return mkStr(valueToString(v));
}

Value Interp::toInt(const Value& v, int line) {
    if (v.isInt()) return v;
    if (v.isBool()) return Value::integer(v.b ? 1 : 0);
    if (v.isFloat()) return Value::integer(static_cast<int64_t>(v.d));
    if (v.isStr()) {
        const std::string& s = asStr(v)->s;
        std::string t;
        for (char c : s) if (!std::isspace(static_cast<unsigned char>(c))) t += c;
        if (t.empty()) return Value::integer(0);
        size_t pos = 0;
        try {
            long long r = std::stoll(t, &pos, 0);
            if (pos == t.size()) return Value::integer(r);
        } catch (const std::exception&) {}
        try {
            double d = std::stod(t, &pos);
            if (pos == t.size()) return Value::integer(static_cast<int64_t>(d));
        } catch (const std::exception&) {}
        error("DegerHatasi", "'" + s + "' tam sayiya cevrilemedi", line);
    }
    error("TipHatasi", typeNameOf(v) + " degeri int'e cevrilemedi", line);
}

Value Interp::toFloat(const Value& v, int line) {
    if (v.isFloat()) return v;
    if (v.isInt()) return Value::number(static_cast<double>(v.i));
    if (v.isBool()) return Value::number(v.b ? 1.0 : 0.0);
    if (v.isStr()) {
        const std::string& s = asStr(v)->s;
        std::string t;
        for (char c : s) if (!std::isspace(static_cast<unsigned char>(c))) t += c;
        if (t.empty()) return Value::number(0.0);
        size_t pos = 0;
        try {
            double d = std::stod(t, &pos);
            if (pos == t.size()) return Value::number(d);
        } catch (const std::exception&) {}
        error("DegerHatasi", "'" + s + "' float'a cevrilemedi", line);
    }
    error("TipHatasi", typeNameOf(v) + " degeri float'a cevrilemedi", line);
}

// ===============================================================
// Sekil islemleri
// ===============================================================

int64_t Interp::seqLength(const Value& v) {
    switch (v.t) {
        case VT::Str:   return static_cast<int64_t>(asStr(v)->s.size());
        case VT::List:  return static_cast<int64_t>(asList(v)->items.size());
        case VT::Tuple: return static_cast<int64_t>(asTuple(v)->items.size());
        case VT::Map:   return static_cast<int64_t>(asMap(v)->items.size());
        case VT::Set:   return static_cast<int64_t>(asSet(v)->items.size());
        case VT::Range: return asRange(v)->length();
        default:
            error("TipHatasi", typeNameOf(v) + " turu indekslenemez");
    }
}

Value Interp::seqAt(const Value& v, int64_t i, int line) {
    switch (v.t) {
        case VT::List:  return asList(v)->items[static_cast<size_t>(i)];
        case VT::Tuple: return asTuple(v)->items[static_cast<size_t>(i)];
        case VT::Str:   return mkStr(std::string(1, asStr(v)->s[static_cast<size_t>(i)]));
        case VT::Map:   return asMap(v)->items[static_cast<size_t>(i)].first;
        case VT::Set:   return asSet(v)->items[static_cast<size_t>(i)];
        case VT::Range: return Value::integer(asRange(v)->at(i));
        default:
            (void)line;
            error("TipHatasi", typeNameOf(v) + " turu indekslenemez");
    }
}

void Interp::iterate(const Value& v, const std::function<void(const Value&)>& fn) {
    iterateIndexed(v, [&](const Value& item, const Value&, int64_t) { fn(item); });
}

void Interp::iterateIndexed(
    const Value& v, const std::function<void(const Value&, const Value&, int64_t)>& fn) {
    switch (v.t) {
        case VT::List:
            for (auto& x : asList(v)->items)
                fn(x, Value::nil(), static_cast<int64_t>(&x - asList(v)->items.data()));
            return;
        case VT::Tuple:
            for (auto& x : asTuple(v)->items)
                fn(x, Value::nil(), static_cast<int64_t>(&x - asTuple(v)->items.data()));
            return;
        case VT::Str: {
            const std::string s = asStr(v)->s;
            for (size_t i = 0; i < s.size(); ++i)
                fn(mkStr(std::string(1, s[i])), Value::nil(), static_cast<int64_t>(i));
            return;
        }
        case VT::Map: {
            auto* m = asMap(v);
            for (size_t i = 0; i < m->items.size(); ++i)
                fn(m->items[i].first, m->items[i].second, static_cast<int64_t>(i));
            return;
        }
        case VT::Set: {
            auto* s = asSet(v);
            for (size_t i = 0; i < s->items.size(); ++i)
                fn(s->items[i], Value::nil(), static_cast<int64_t>(i));
            return;
        }
        case VT::Range: {
            auto* r = asRange(v);
            int64_t len = r->length();
            for (int64_t i = 0; i < len; ++i)
                fn(Value::integer(r->at(i)), Value::nil(), i);
            return;
        }
        default:
            error("TipHatasi", typeNameOf(v) + " turu gezilemez (for/pipe/map/fold)");
    }
}

void Interp::seqIterate(const Value& v, const std::function<void(const Value&)>& fn) {
    switch (v.t) {
        case VT::List:  for (auto& x : asList(v)->items) fn(x); return;
        case VT::Tuple: for (auto& x : asTuple(v)->items) fn(x); return;
        case VT::Map:   for (auto& kv : asMap(v)->items) fn(kv.first); return;
        case VT::Set:   for (auto& x : asSet(v)->items) fn(x); return;
        case VT::Range: {
            auto* r = asRange(v);
            for (int64_t i = 0, len = r->length(); i < len; ++i)
                fn(Value::integer(r->at(i)));
            return;
        }
        case VT::Str: {
            const std::string s = asStr(v)->s;
            for (char c : s) fn(mkStr(std::string(1, c)));
            return;
        }
        default:
            error("TipHatasi", std::string(typeName(v.t)) + " turu gezilemez");
    }
}

bool Interp::unpackPair(const Value& v, Value& a, Value& b, int line) {
    if (v.isTuple() && asTuple(v)->items.size() == 2) {
        a = asTuple(v)->items[0]; b = asTuple(v)->items[1];
        return true;
    }
    if (v.isList() && asList(v)->items.size() == 2) {
        a = asList(v)->items[0]; b = asList(v)->items[1];
        return true;
    }
    (void)line;
    return false;
}

// ===============================================================
// Indeksleme
// ===============================================================

Value Interp::indexGet(const Value& obj, const Value& key, int line) {
    switch (obj.t) {
        case VT::List:
        case VT::Tuple:
        case VT::Str: {
            int64_t raw = toInt(key, line).i;
            int64_t len = seqLength(obj);
            int64_t i = raw < 0 ? raw + len : raw;
            if (i < 0 || i >= len)
                error("IndexHatasi", "indis " + std::to_string(raw) +
                                     " gecersiz (gecerli: -" + std::to_string(len) +
                                     ".." + std::to_string(len - 1) + ")", line);
            return seqAt(obj, i, line);
        }
        case VT::Map: {
            const Value* v = asMap(obj)->get(key);
            return v ? *v : Value::nil();
        }
        case VT::Set:
            return Value::boolean(asSet(obj)->contains(key));
        case VT::Range: {
            int64_t raw = toInt(key, line).i;
            int64_t len = seqLength(obj);
            int64_t i = raw < 0 ? raw + len : raw;
            if (i < 0 || i >= len) error("IndexHatasi", "aralik indeksi gecersiz", line);
            return Value::integer(asRange(obj)->at(i));
        }
        default:
            if (obj.isObj()) {
                Value m = getMember(obj, "__getitem__", line);
                if (m.isFn()) return callMethod(obj, "__getitem__", {key}, line);
            }
            error("TipHatasi", typeNameOf(obj) + " turu indekslenemez", line);
    }
}

void Interp::indexSet(const Value& obj, const Value& key, const Value& val, int line) {
    switch (obj.t) {
        case VT::List: {
            auto* l = asList(obj);
            int64_t raw = toInt(key, line).i;
            int64_t len = static_cast<int64_t>(l->items.size());
            int64_t i = raw < 0 ? raw + len : raw;
            if (i < 0 || i >= len)
                error("IndexHatasi", "liste indeksi " + std::to_string(raw) + " gecersiz", line);
            l->items[static_cast<size_t>(i)] = val;
            return;
        }
        case VT::Map:
            asMap(obj)->set(key, val);
            return;
        default:
            if (obj.isObj()) {
                Value m = getMember(obj, "__setitem__", line);
                if (m.isFn()) { callMethod(obj, "__setitem__", {key, val}, line); return; }
            }
            error("TipHatasi", typeNameOf(obj) + " turune indeksle atama yapilamaz", line);
    }
}

// ===============================================================
// Uyelik erisimi
// ===============================================================

Value Interp::getMember(const Value& obj, const std::string& name, int line) {
    switch (obj.t) {
        case VT::Module: {
            auto* m = asModule(obj);
            auto it = m->members.find(name);
            return it == m->members.end() ? Value::nil() : it->second;
        }
        case VT::Map: {
            const Value* v = asMap(obj)->get(mkStr(name));
            return v ? *v : Value::nil();
        }
        case VT::Class: {
            Value f = asClass(obj)->findMethod(name);
            if (!f.isNil()) return f;
            error("AttributeHatasi", asClass(obj)->name + " sinifinda '" + name +
                                    "' uyesi yok", line);
        }
        case VT::Object: {
            auto* o = asObject(obj);
            auto it = o->fields.find(name);
            if (it != o->fields.end()) return it->second;
            if (o->cls) {
                Value m = o->cls->findMethod(name);
                if (!m.isNil()) return m;
            }
            return findBuiltinMethod(obj, name);
        }
        case VT::Str:
            if (name == "length" || name == "size" || name == "count")
                return Value::integer(static_cast<int64_t>(asStr(obj)->s.size()));
            return findBuiltinMethod(obj, name);
        case VT::List:
        case VT::Tuple:
        case VT::Set:
        case VT::Range:
            if (name == "length" || name == "size" || name == "count")
                return Value::integer(seqLength(obj));
            return findBuiltinMethod(obj, name);
        default:
            (void)line;
            return Value::nil();
    }
}

void Interp::setMember(const Value& obj, const std::string& name, const Value& v, int line) {
    switch (obj.t) {
        case VT::Object: {
            auto* object = asObject(obj);
            for (auto cls = object->cls; cls; cls = cls->super) {
                auto typed = cls->fieldTypes.find(name);
                if (typed != cls->fieldTypes.end()) {
                    checkValueType(typed->second, v, "alan " + name, line);
                    break;
                }
            }
            object->fields[name] = v;
            return;
        }
        case VT::Map:
            asMap(obj)->set(mkStr(name), v);
            return;
        case VT::Module: {
            auto* m = asModule(obj);
            if (!m->members.count(name)) m->order.push_back(name);
            m->members[name] = v;
            return;
        }
        default:
            error("TipHatasi", typeNameOf(obj) + " turunde '" + name + "' alani atanamaz", line);
    }
}

// ===============================================================
// Icerik denetimi
// ===============================================================

Value Interp::containsOp(const Value& c, const Value& item, int line) {
    switch (c.t) {
        case VT::Str:
            if (!item.isStr())
                error("TipHatasi", "'in' sag tarafi str olmali", line);
            return Value::boolean(asStr(c)->s.find(asStr(item)->s) != std::string::npos);
        case VT::Map:
            return Value::boolean(asMap(c)->get(item) != nullptr);
        case VT::Set:
            return Value::boolean(asSet(c)->contains(item));
        default: {
            bool found = false;
            iterate(c, [&](const Value& x) {
                if (!found && valueEquals(x, item)) found = true;
            });
            return Value::boolean(found);
        }
    }
}

// ===============================================================
// Ikili operatorler
// ===============================================================

static std::string repeatStr(const std::string& s, int64_t n) {
    std::string out;
    if (n <= 0) return out;
    out.reserve(s.size() * static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) out += s;
    return out;
}

// Iki tabanli tamamlama taşması denetimi (MSVC uyumlu)
static bool addOverflow(int64_t a, int64_t b, int64_t& out) {
    uint64_t ua = static_cast<uint64_t>(a);
    uint64_t ub = static_cast<uint64_t>(b);
    uint64_t ur = ua + ub;
    out = static_cast<int64_t>(ur);
    return ur < ua;   // tasma varsa sonuc kuculmus olur
}

Value Interp::binaryOp(const std::string& op, const Value& a, const Value& b, int line) {
    switch (op[0]) {
        case '+': {
            if (a.isNum() && b.isNum()) {
                if (a.isInt() && b.isInt()) {
                    int64_t r = 0;
                    if (!addOverflow(a.i, b.i, r)) return Value::integer(r);
                    error("TasmaHatasi", "tamsayi toplamasi tasiyor", line);
                }
                return Value::number(a.asNum() + b.asNum());
            }
            if (a.isStr() && b.isStr()) return mkStr(asStr(a)->s + asStr(b)->s);
            // Metin + hata nesnesi  ->  "hata: mesaj"
            if (a.isStr() && b.isErr())
                return mkStr(asStr(a)->s + asErr(b)->msg);
            if (a.isErr() && b.isStr())
                return mkStr(asErr(a)->msg + asStr(b)->s);
            if (a.isList() && b.isList()) {
                std::vector<Value> out = asList(a)->items;
                out.insert(out.end(), asList(b)->items.begin(), asList(b)->items.end());
                return mkList(std::move(out));
            }
            if (a.isTuple() && b.isTuple()) {
                std::vector<Value> out = asTuple(a)->items;
                out.insert(out.end(), asTuple(b)->items.begin(), asTuple(b)->items.end());
                return mkTuple(std::move(out));
            }
            if (a.isSet() && b.isSet()) {
                auto s = mkSet();
                for (auto& x : asSet(a)->items) asSet(s)->add(x);
                for (auto& x : asSet(b)->items) asSet(s)->add(x);
                return s;
            }
            if (a.isStr() && b.isList()) {
                // "toplam: " + [1,2]  ->  "toplam: 1, 2"
                std::string out = asStr(a)->s;
                auto* l = asList(b);
                for (size_t i = 0; i < l->items.size(); ++i) {
                    if (i) out += ", ";
                    out += valueToString(l->items[i]);
                }
                return mkStr(out);
            }
            error("TipHatasi", typeNameOf(a) + " + " + typeNameOf(b) + " gecersiz", line);
        }
        case '-': {
            if (a.isNum() && b.isNum()) {
                if (a.isInt() && b.isInt()) return Value::integer(a.i - b.i);
                return Value::number(a.asNum() - b.asNum());
            }
            if (a.isSet() && b.isSet()) {
                auto s = mkSet();
                for (auto& x : asSet(a)->items)
                    if (!asSet(b)->contains(x)) asSet(s)->add(x);
                return s;
            }
            error("TipHatasi", typeNameOf(a) + " - " + typeNameOf(b) + " gecersiz", line);
        }
        case '*': {
            if (a.isNum() && b.isNum()) {
                if (a.isInt() && b.isInt()) return Value::integer(a.i * b.i);
                return Value::number(a.asNum() * b.asNum());
            }
            if (a.isStr() && b.isInt())
                return mkStr(repeatStr(asStr(a)->s, b.i));
            if (a.isInt() && b.isStr())
                return mkStr(repeatStr(asStr(b)->s, a.i));
            if (a.isList() && b.isInt()) {
                std::vector<Value> out;
                for (int64_t i = 0; i < b.i; ++i)
                    out.insert(out.end(), asList(a)->items.begin(), asList(a)->items.end());
                return mkList(std::move(out));
            }
            if (a.isInt() && b.isList()) {
                std::vector<Value> out;
                for (int64_t i = 0; i < a.i; ++i)
                    out.insert(out.end(), asList(b)->items.begin(), asList(b)->items.end());
                return mkList(std::move(out));
            }
            error("TipHatasi", typeNameOf(a) + " * " + typeNameOf(b) + " gecersiz", line);
        }
        case '/': {
            if (a.isNum() && b.isNum()) {
                if (b.asNum() == 0.0) error("SifiraBolme", "sifira bolme hatasi", line);
                return Value::number(a.asNum() / b.asNum());
            }
            error("TipHatasi", typeNameOf(a) + " / " + typeNameOf(b) + " gecersiz", line);
        }
        case '&': case '|': case '^': {
            if ((op == "&" || op == "|") && (a.isBool() || b.isBool())) {
                bool x = valueTruthy(a), y = valueTruthy(b);
                return Value::boolean(op == "&" ? (x && y) : (x || y));
            }
            if (a.isInt() && b.isInt()) {
                if (op == "&") return Value::integer(a.i & b.i);
                if (op == "|") return Value::integer(a.i | b.i);
                return Value::integer(a.i ^ b.i);
            }
            if (a.isSet()) {
                auto* sa = asSet(a);
                if (op == "|") {
                    auto s = mkSet();
                    for (auto& x : sa->items) asSet(s)->add(x);
                    if (b.isSet()) for (auto& x : asSet(b)->items) asSet(s)->add(x);
                    return s;
                }
                if (op == "&") {
                    auto s = mkSet();
                    for (auto& x : sa->items)
                        if (b.isSet() && asSet(b)->contains(x)) asSet(s)->add(x);
                    return s;
                }
                if (op == "^") {
                    auto s = mkSet();
                    for (auto& x : sa->items)
                        if (!b.isSet() || !asSet(b)->contains(x)) asSet(s)->add(x);
                    if (b.isSet())
                        for (auto& x : asSet(b)->items)
                            if (!sa->contains(x)) asSet(s)->add(x);
                    return s;
                }
            }
            error("TipHatasi", typeNameOf(a) + " " + op + " " + typeNameOf(b) + " gecersiz",
                  line);
        }
        default: break;
    }

    if (op == "//") {
        if (a.isNum() && b.isNum()) {
            double d = b.asNum();
            if (d == 0.0) error("SifiraBolme", "sifira bolme hatasi", line);
            return Value::number(std::floor(a.asNum() / d));
        }
        error("TipHatasi", "tam bolme sadece sayilar icin", line);
    }
    if (op == "%") {
        if (a.isNum() && b.isNum()) {
            double d = b.asNum();
            if (d == 0.0) error("SifiraBolme", "modulo hatasi", line);
            if (a.isInt() && b.isInt()) {
                int64_t r = a.i % b.i;
                if (r != 0 && ((r < 0) != (b.i < 0))) r += b.i;
                return Value::integer(r);
            }
            double m = std::fmod(a.asNum(), d);
            if (m != 0 && ((m < 0) != (d < 0))) m += d;
            return Value::number(m);
        }
        if (a.isStr()) return mkStr(formatString(asStr(a)->s, b, line));
        error("TipHatasi", typeNameOf(a) + " % " + typeNameOf(b) + " gecersiz", line);
    }
    if (op == "**") {
        if (a.isNum() && b.isNum()) {
            if (a.isInt() && b.isInt() && b.i >= 0) {
                int64_t acc = 1;
                for (int64_t e = 0; e < b.i; ++e) {
                    if (e >= 62) return Value::number(std::pow(a.asNum(), b.asNum()));
                    acc *= a.i;
                }
                return Value::integer(acc);
            }
            return Value::number(std::pow(a.asNum(), b.asNum()));
        }
        error("TipHatasi", typeNameOf(a) + " ** " + typeNameOf(b) + " gecersiz", line);
    }
    if (op == "==") return Value::boolean(valueEquals(a, b));
    if (op == "!=") return Value::boolean(!valueEquals(a, b));
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        int c = compareValues(a, b);
        if (c == -2)
            error("TipHatasi", typeNameOf(a) + " ile " + typeNameOf(b) +
                              " karsilastirilamaz", line);
        bool r = (op == "<") ? c < 0 : (op == "<=") ? c <= 0
                : (op == ">") ? c > 0 : c >= 0;
        return Value::boolean(r);
    }
    if (op == "in")     return containsOp(b, a, line);
    if (op == "notin")  return Value::boolean(!valueTruthy(containsOp(b, a, line)));
    if (op == "<<") {
        if (a.isInt() && b.isInt())
            return Value::integer((b.i < 0 || b.i > 63) ? 0 : (a.i << b.i));
        error("TipHatasi", "kaydirma sadece tam sayi icin", line);
    }
    if (op == ">>") {
        if (a.isInt() && b.isInt()) {
            if (b.i < 0 || b.i > 63) return Value::integer(a.i < 0 ? -1 : 0);
            return Value::integer(a.i >> b.i);
        }
        error("TipHatasi", "kaydirma sadece tam sayi icin", line);
    }
    error("Hata", "bilinmeyen operator: " + op, line);
}

Value Interp::unaryOp(const std::string& op, const Value& a, int line) {
    if (op == "-" || op == "+") {
        if (!a.isNum())
            error("TipHatasi", typeNameOf(a) + " uzerinde unary '" + op + "' gecersiz", line);
        if (op == "+") return a;
        if (a.isInt()) return Value::integer(-a.i);
        return Value::number(-a.d);
    }
    if (op == "~") {
        if (a.isInt()) return Value::integer(~a.i);
        error("TipHatasi", "'~' sadece tam sayi icin", line);
    }
    if (op == "!" || op == "not") return Value::boolean(!valueTruthy(a));
    error("Hata", "bilinmeyen unary operator: " + op, line);
}

// ===============================================================
// Sinif / nesne olusturma
// ===============================================================

Value Interp::makeObject(std::shared_ptr<ClassObj> cls) {
    auto o = std::make_shared<ObjectObj>();
    o->className = cls->name;
    o->cls = cls;
    if (cls->super) {
        Value base = instantiate(cls->super, {}, -1);
        if (base.isObj()) {
            for (auto& kv : asObject(base)->fields) o->fields[kv.first] = kv.second;
        }
    }
    for (auto& kv : cls->fieldDefaults) o->fields[kv.first] = deepCopy(kv.second);
    return Value::obj(o);
}

Value Interp::instantiate(std::shared_ptr<ClassObj> cls, std::vector<Value> args,
                          int line) {
    Value obj = makeObject(cls);
    Value init = cls->findMethod("init");
    if (!init.isNil()) {
        callMethod(obj, "init", std::move(args), line);
    } else if (!args.empty()) {
        error("Hata", cls->name + " sinifinda 'init' metotu yok", line);
    }
    return obj;
}

} // namespace k7
