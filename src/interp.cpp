#include "k7/interp.h"

#include "k7/parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace k7 {

std::string fmtFloatK7(double d);

Value* Env::lookup(const std::string& n) {
    Env* e = this;
    int guard = 0;
    while (e && guard++ < 4096) {
        auto it = e->vars.find(n);
        if (it != e->vars.end()) return &it->second;
        e = e->parent.get();
    }
    return nullptr;
}

std::string* Env::lookupType(const std::string& n) {
    Env* e = this;
    int guard = 0;
    while (e && guard++ < 4096) {
        auto it = e->declaredTypes.find(n);
        if (it != e->declaredTypes.end()) return &it->second;
        e = e->parent.get();
    }
    return nullptr;
}

Interp::~Interp() = default;

Interp::Interp() {
    globals_ = std::make_shared<Env>();
    globals_->isGlobal = true;
    cur_ = globals_;
    envStack_.push_back(globals_);
}

void Interp::pushEnv(std::shared_ptr<Env> parent) {
    envStack_.push_back(std::move(parent));
}

void Interp::popEnv() {
    if (envStack_.size() > 1) envStack_.pop_back();
}

Interp::EnvScope::EnvScope(Interp& i) : in(i) {
    in.pushEnv(std::make_shared<Env>(in.envStack_.back()));
    ref = in.envStack_.back().get();
}

Interp::EnvScope::~EnvScope() { in.popEnv(); }

std::string Interp::currentLoc() const {
    std::string out;
    for (size_t i = stack_.size(); i-- > 0;) {
        if (!out.empty()) out += "\n    ";
        out += stack_[i];
    }
    return out;
}

void Interp::error(const std::string& kind, const std::string& msg, int line) {
    ErrorObj* e = new ErrorObj();
    e->kind = kind;
    e->msg = msg;
    e->trace = stack_;
    (void)line;
    ThrowValue tv;
    tv.err = Value::obj(std::shared_ptr<ErrorObj>(e));
    throw tv;
}

void Interp::throwValue(const Value& v, int line) {
    (void)line;
    ThrowValue tv;
    tv.err = v.isErr() ? v : mkError("Error", valueToString(v));
    throw tv;
}

std::string Interp::typeNameOf(const Value& v) {
    if (v.t == VT::Object) {
        auto* o = asObject(v);
        return o->className.empty() ? "object" : o->className;
    }
    return typeName(v.t);
}

void Interp::checkValueType(const std::string& expected, const Value& value,
                            const std::string& context, int line) {
    if (expected.empty()) return;
    std::string type = expected;
    if (!type.empty() && type.back() == '?') {
        if (value.isNil()) return;
        type.pop_back();
    }
    std::string normalized = type;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool ok = normalized == "any" || normalized == "dynamic";
    if (normalized == "int" || normalized == "integer" || normalized == "long" ||
        normalized == "short" || normalized == "byte") ok = value.isInt();
    else if (normalized == "float" || normalized == "double" || normalized == "number")
        ok = value.isNum();
    else if (normalized == "bool" || normalized == "boolean") ok = value.isBool();
    else if (normalized == "string" || normalized == "str") ok = value.isStr();
    else if (normalized == "char") ok = value.isStr() && asStr(value)->s.size() == 1;
    else if (normalized == "list" || normalized == "array") ok = value.isList();
    else if (normalized == "tuple") ok = value.isTuple();
    else if (normalized == "map" || normalized == "dict") ok = value.isMap();
    else if (normalized == "set") ok = value.isSet();
    else if (normalized == "range") ok = value.isRange();
    else if (normalized == "function" || normalized == "fn") ok = value.isFn();
    else if (normalized == "void") ok = value.isNil();
    else if (normalized == "object") ok = value.isObj();
    else {
        ok = false;
        if (value.isObj()) {
            auto cls = asObject(value)->cls;
            while (cls) {
                if (cls->name == type) { ok = true; break; }
                cls = cls->super;
            }
        }
    }
    if (!ok) {
        const std::string label = context.empty() ? "deger" : context;
        error("TipHatasi", label + " icin '" + type + "' bekleniyordu, '" +
                              typeNameOf(value) + "' geldi", line);
    }
}

// ===============================================================
// Metot kaydi
// ===============================================================

void Interp::registerMethod(VT t, const std::string& name, NativeFn fn) {
    Value v = mkNative(t == VT::Str ? ("str." + name) : std::string(typeName(t)) + "." + name,
                       std::move(fn));
    methods_[t][name] = v;
}

Value Interp::findBuiltinMethod(const Value& obj, const std::string& name) const {
    auto it = methods_.find(obj.t);
    if (it == methods_.end()) return Value::nil();
    auto f = it->second.find(name);
    if (f == it->second.end()) return Value::nil();
    return f->second;
}

// ===============================================================
// Uygulama girisi
// ===============================================================

static void reportError(const std::string& filename, const Value& err) {
    auto* e = asErr(err);
    std::cerr << "\n" << filename << " -- " << e->kind << ": " << e->msg << "\n";
    for (size_t i = e->trace.size(); i-- > 0;)
        std::cerr << "    " << e->trace[i] << "\n";
    std::cerr << std::flush;
}

bool Interp::runSource(const std::string& source, const std::string& filename) {
    Parser p(source, filename);
    NodePtr prog = p.parse();
    if (p.hasErrors()) {
        for (const auto& e : p.errors()) std::cerr << e.msg << "\n";
        std::cerr << std::flush;
        return false;
    }

    std::filesystem::path sourcePath(filename);
    sourceDirs_.push_back(filename.empty() || filename[0] == '<'
                              ? std::string()
                              : (sourcePath.has_parent_path()
                                     ? sourcePath.parent_path().string()
                                     : std::string(".")));
    struct SourceDirPop {
        std::vector<std::string>& dirs;
        ~SourceDirPop() { dirs.pop_back(); }
    } sourceDirPop{sourceDirs_};

    try {
        evalNode(prog.get(), *globals_);
        return true;
    } catch (const ExitRequest& e) {
        std::exit(e.code);
    } catch (const QuitSignal&) {
        return true;
    } catch (const ThrowValue& tv) {
        reportError(filename, tv.err);
        return false;
    } catch (const BreakSignal&) {
        std::cerr << "\nHATA: 'break' dongu disinda\n" << std::flush;
        return false;
    } catch (const ContinueSignal&) {
        std::cerr << "\nHATA: 'continue' dongu disinda\n" << std::flush;
        return false;
    } catch (const ReturnValue&) {
        std::cerr << "\nHATA: 'return' fonksiyon disinda\n" << std::flush;
        return false;
    }
}

void Interp::runInteractive(const std::string& line) {
    Parser p(line, "<repl>");
    NodePtr prog = p.parse();
    if (p.hasErrors()) {
        for (const auto& e : p.errors()) std::cerr << e.msg << "\n";
        std::cerr << std::flush;
        return;
    }
    try {
        Value last = evalNode(prog.get(), *globals_);
        if (last.t != VT::Nil) {
            // Yalnizca deyim disinda deger goster
            if (prog->kids.size() == 1 && prog->kids[0]->kind == NK::ExprStmt)
                std::cout << valueToRepr(last) << "\n";
        }
    } catch (const ExitRequest&) {
        throw;
    } catch (const ThrowValue& tv) {
        reportError("<repl>", tv.err);
    } catch (const BreakSignal&) {
        std::cerr << "HATA: 'break' dongu disinda\n";
    } catch (const ContinueSignal&) {
        std::cerr << "HATA: 'continue' dongu disinda\n";
    } catch (const ReturnValue&) {
        std::cerr << "HATA: 'return' fonksiyon disinda\n";
    }
}

// ===============================================================
// DOGRULAMA
// ===============================================================

void Interp::execBlock(const Node* n, Env& env) {
    (void)env;
    EnvScope scope(*this);
    for (const auto& k : n->kids) execStmt(k.get(), *scope.ref);
}

void Interp::execStmt(const Node* n, Env& env) {
    if (!n) return;
    lastLine = n->line;
    switch (n->kind) {
        case NK::Block:
            execBlock(n, env);
            return;
        case NK::VarDecl: {
            Value v = n->kid(0) ? evalNode(n->kid(0), env) : Value::nil();
            if (n->kids.size() == 1 && n->kid(0) &&
                n->kid(0)->kind == NK::TupleLit && n->names.size() > 1) {
                Value a, b;
                if (!unpackPair(v, a, b, n->line))
                    error("Hata", "coklu atama icin " + std::to_string(n->names.size()) +
                                  " deger bekleniyordu", n->line);
                env.define(n->names[0], a);
                if (n->names.size() > 1) env.define(n->names[1], b);
                return;
            }
            if (n->kid(0) && !n->names.empty())
                checkValueType(n->typeName, v, "degisken " + n->names[0], n->line);
            if (!n->names.empty()) env.define(n->names[0], v, n->typeName);
            return;
        }
        case NK::ExprStmt: {
            if (n->str == "defer") { env.define("__defer", Value::nil()); }
            evalNode(n->kid(0), env);
            return;
        }
        case NK::FnDecl: {
            auto fn = std::make_shared<FnObj>();
            fn->name = n->str;
            fn->params = n->names;
            fn->paramTypes = n->paramTypes;
            fn->returnType = n->returnType;
            fn->defaults.reserve(n->kids.size());
            for (const auto& d : n->kids) fn->defaults.push_back(cloneNode(d.get()));
            fn->body = cloneNode(n->body.get());
            fn->closure = std::make_shared<Env>(envStack_.back());
            fn->variadic = n->flag2;
            env.define(n->str, Value::obj(fn));
            return;
        }
        case NK::ClassDecl:
            execClass(n, env);
            return;
        case NK::EnumDecl: {
            auto cls = std::make_shared<ClassObj>();
            cls->name = n->str;
            auto obj = makeObject(cls);
            for (size_t i = 0; i < n->kids.size(); ++i) {
                const Node* m = n->kid(i);
                Value mv = m->kid(0) ? evalNode(m->kid(0), env) : Value::integer(static_cast<int64_t>(i));
                cls->methods[m->str] = mv;
                cls->methodOrder.push_back(m->str);
                asObject(obj)->fields[m->str] = mv;
            }
            env.define(n->str, obj);
            return;
        }
        case NK::If: {
            Value c = evalNode(n->kid(0), env);
            if (valueTruthy(c)) execStmt(n->kid(1), env);
            else if (n->kid(2)) execStmt(n->kid(2), env);
            return;
        }
        case NK::While:
            evalWhile(n, env);
            return;
        case NK::Loop:
            evalLoop(n, env);
            return;
        case NK::For:
            evalFor(n, env);
            return;
        case NK::Break:    throw BreakSignal{};
        case NK::Continue: throw ContinueSignal{};
        case NK::Return:    throw ReturnValue{ n->kid(0) ? evalNode(n->kid(0), env) : Value::nil() };
        case NK::Assert: {
            Value c = evalNode(n->kid(0), env);
            if (!valueTruthy(c)) {
                std::string msg = "varsayilan dogrulama hatasi";
                if (n->kid(1)) msg = valueToString(evalNode(n->kid(1), env));
                error("AssertionError", msg, n->line);
            }
            return;
        }
        case NK::Throw:
            throwValue(evalNode(n->kid(0), env), n->line);
            return;
        case NK::Try: {
            try {
                execStmt(n->kid(0), env);
            } catch (const ThrowValue& tv) {
                if (n->kid(1)) {
                    EnvScope scope(*this);
                    if (!n->names.empty()) scope.ref->define(n->names[0], tv.err);
                    for (const auto& k : n->kid(1)->kids) execStmt(k.get(), *scope.ref);
                }
                return;
            }
            return;
        }
        case NK::ImportDecl: {
            Value mod;
            std::string bind;                       // baglanacak isim
            if (n->flag && n->kid(1)) {             // import ad = ifade
                mod = evalNode(n->kid(1), env);
                bind = n->str;
            } else if (n->flag2) {                  // import "yol.k7" [as ad]
                mod = importPath(n->str, n->line);
                bind = n->str;
                if (!mod.isModule()) {
                    // dosya adi govde: ornek.k7 -> ornek
                    size_t slash = bind.find_last_of("/\\");
                    if (slash != std::string::npos) bind = bind.substr(slash + 1);
                    size_t dot = bind.find_last_of('.');
                    if (dot != std::string::npos) bind = bind.substr(0, dot);
                }
                if (!n->names.empty()) bind = n->names[0];
            } else {                                // import a.b.c [as ad]
                std::string full;
                const auto& parts = n->kid(0)->names;
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (i) full += ".";
                    full += parts[i];
                }
                mod = importModule(full, n->line);
                bind = parts.empty() ? "" : parts.back();
                if (!n->str.empty()) bind = n->str;
            }

            if (!mod.isModule()) {
                if (bind.empty())
                    error("ModulHatasi", "import edilen sey modul degil", n->line);
                env.define(bind, mod);
                return;
            }
            if (bind.empty()) {
                auto* m = asModule(mod);
                for (auto& name : m->order) env.define(name, m->members[name]);
                return;
            }
            env.define(bind, mod);
            return;
        }
        default:
            evalNode(n, env);
            return;
    }
}

Value Interp::execAssign(const Node* n, Env& env) {
    const Node* lhs = n->kid(0);
    const Node* rhs = n->kid(1);

    if (n->str == "=") {
        Value v = evalNode(rhs, env);

        // Coklu hedef: (a, b) = ...
        if (lhs->kind == NK::TupleLit) {
            size_t cnt = lhs->kids.size();
            std::vector<Value> items;
            if (v.isList()) {
                items = asList(v)->items;
            } else if (v.isTuple()) {
                items = asTuple(v)->items;
            } else if (cnt == 2) {
                Value a, b;
                if (unpackPair(v, a, b, n->line)) items = {a, b};
            }
            if (items.size() != cnt)
                error("Hata", "coklu atamada " + std::to_string(cnt) +
                              " deger bekleniyordu, " + std::to_string(items.size()) +
                              " bulundu", n->line);
            for (size_t i = 0; i < cnt; ++i) execAssignOne(lhs->kid(i), env, items[i]);
            return v;
        }
        execAssignOne(lhs, env, v);
        return v;
    }

    // Bilesik atama
    Value cur = evalNode(lhs, env);
    Value b = evalNode(rhs, env);
    std::string op = n->str.substr(0, n->str.size() - 1);
    Value v = binaryOp(op, cur, b, n->line);
    execAssignOne(lhs, env, v);
    return v;
}

void Interp::execAssignOne(const Node* lhs, Env& env, const Value& v) {
    switch (lhs->kind) {
        case NK::Ident: {
            Value* slot = env.lookup(lhs->str);
            if (slot) {
                std::string* declared = env.lookupType(lhs->str);
                if (declared)
                    checkValueType(*declared, v, "degisken " + lhs->str, lhs->line);
                *slot = v;
            }
            else env.define(lhs->str, v);
            return;
        }
        case NK::Get: {
            Value o = evalNode(lhs->kid(0), env);
            setMember(o, lhs->str, v, lhs->line);
            return;
        }
        case NK::Index: {
            Value o = evalNode(lhs->kid(0), env);
            Value k = evalNode(lhs->kid(1), env);
            indexSet(o, k, v, lhs->line);
            return;
        }
        default:
            error("Hata", "gecersiz atama hedefi", lhs->line);
    }
}

void Interp::evalWhile(const Node* n, Env& env) {
    const Node* cond = n->kid(0);
    const Node* body = n->kid(1);
    long long guard = 0;
    while (valueTruthy(evalNode(cond, env))) {
        try {
            execStmt(body, env);
        } catch (const BreakSignal&) { break; }
        catch (const ContinueSignal&) { continue; }
        if (!replMode && ++guard > 20000000000LL)
            error("ZamanAsimi", "sonsuz dongu tespit edildi", n->line);
    }
}

void Interp::evalLoop(const Node* n, Env& env) {
    long long guard = 0;
    while (true) {
        try {
            execStmt(n->kid(0), env);
        } catch (const BreakSignal&) { break; }
        catch (const ContinueSignal&) { continue; }
        if (!replMode && ++guard > 20000000000LL)
            error("ZamanAsimi", "sonsuz dongu tespit edildi", n->line);
    }
}

void Interp::evalFor(const Node* n, Env& env) {
    if (n->flag) {
        EnvScope scope(*this);
        Env& e = *scope.ref;
        if (n->kid(0)) {
            if (n->kid(0)->kind == NK::ExprStmt) evalNode(n->kid(0), e);
            else execStmt(n->kid(0), e);
        }

        // Sık kullanılan, yan etkisiz sayisal toplama dongusu icin yorumlayici
        // hiz yolu. AST dugumlerini her turda evalNode/Env/hash-map uzerinden
        // dolastirmak yerine ayni dinamik long degerlerini dogrudan ilerletir.
        // Yalnizca yapisi kesin olarak taninan, turu belirtilmis long degiskenli
        // `for (long i = 0; i < SABIT; i++) { toplam += i; }` bicimine uygulanir.
        const Node* init = n->kid(0);
        const Node* cond = n->kid(1);
        const Node* step = n->kid(2);
        const Node* body = n->kid(3);
        const Node* statement = body && body->kind == NK::Block && body->kids.size() == 1
                                   ? body->kid(0) : nullptr;
        const Node* add = statement && statement->kind == NK::ExprStmt
                              ? statement->kid(0) : nullptr;
        bool fastSum = init && init->kind == NK::VarDecl && init->names.size() == 1 &&
                       init->typeName == "long" && init->kid(0) &&
                       init->kid(0)->kind == NK::IntLit && cond &&
                       cond->kind == NK::Binary && cond->str == "<" &&
                       cond->kid(0) && cond->kid(0)->kind == NK::Ident &&
                       cond->kid(0)->str == init->names[0] && cond->kid(1) &&
                       cond->kid(1)->kind == NK::IntLit && step &&
                       step->kind == NK::Assign && step->str == "+=" &&
                       step->kid(0) && step->kid(0)->kind == NK::Ident &&
                       step->kid(0)->str == init->names[0] && step->kid(1) &&
                       step->kid(1)->kind == NK::IntLit && step->kid(1)->ival == 1 &&
                       add && add->kind == NK::Assign && add->str == "+=" &&
                       add->kid(0) && add->kid(0)->kind == NK::Ident &&
                       add->kid(1) && add->kid(1)->kind == NK::Ident &&
                       add->kid(1)->str == init->names[0] &&
                       add->kid(0)->str != init->names[0];
        if (fastSum) {
            const std::string& indexName = init->names[0];
            const std::string& totalName = add->kid(0)->str;
            const int64_t first = init->kid(0)->ival;
            const int64_t limit = cond->kid(1)->ival;
            Value* totalValue = e.lookup(totalName);
            std::string* indexType = e.lookupType(indexName);
            std::string* totalType = e.lookupType(totalName);
            const long double count = limit > first
                                          ? static_cast<long double>(limit) - first : 0.0L;
            const long double last = count > 0.0L
                                         ? static_cast<long double>(limit) - 1.0L : 0.0L;
            const long double added = count *
                                      (static_cast<long double>(first) + last) / 2.0L;
            fastSum = first >= 0 && limit >= first && totalValue && totalValue->isInt() &&
                      indexType && *indexType == "long" && totalType &&
                      *totalType == "long" && added >= 0.0L &&
                      added <= static_cast<long double>(std::numeric_limits<int64_t>::max()) &&
                      static_cast<long double>(totalValue->i) + added <=
                          static_cast<long double>(std::numeric_limits<int64_t>::max());
            if (fastSum) {
                volatile int64_t total = totalValue->i;
                for (int64_t i = first; i < limit; ++i) total = total + i;
                *totalValue = Value::integer(total);
                if (Value* indexValue = e.lookup(indexName))
                    *indexValue = Value::integer(limit);
                return;
            }
        }

        long long guard = 0;
        while (!n->kid(1) || valueTruthy(evalNode(n->kid(1), e))) {
            bool stop = false;
            try {
                execStmt(n->kid(3), e);
            } catch (const BreakSignal&) {
                stop = true;
            } catch (const ContinueSignal&) {
                // C-style for still evaluates its step expression.
            }
            if (stop) break;
            if (n->kid(2)) evalNode(n->kid(2), e);
            if (!replMode && ++guard > 20000000000LL)
                error("ZamanAsimi", "sonsuz dongu tespit edildi", n->line);
        }
        return;
    }
    Value seq = evalNode(n->kid(0), env);
    const Node* body = n->kid(1);
    bool two = n->names.size() > 1;
    const std::string vname = n->names[0];
    const std::string kname = two ? n->names[1] : std::string();

    EnvScope scope(*this);
    Env& e = *scope.ref;

    bool stop = false;
    long long guard = 0;
    iterateIndexed(seq, [&](const Value& item, const Value& key, int64_t idx) {
        if (stop) return;
        Value val = item;
        Value kv = key;
        if (two) {
            if (item.isMap()) {
                auto* m = asMap(item);
                if (static_cast<size_t>(idx) < m->items.size()) {
                    kv = m->items[idx].first;
                    val = m->items[idx].second;
                }
            } else if (item.isList()) {
                auto* l = asList(item);
                if (l->items.size() == 2) { kv = l->items[0]; val = l->items[1]; }
                else error("Hata", "cift dongude eleman 2 olmali", n->line);
            } else {
                error("Hata", "cift dongu sadece map uzerinde calisir", n->line);
            }
        }
        e.define(vname, val);
        if (two) e.define(kname, kv);
        try {
            execStmt(body, e);
        } catch (const BreakSignal&) {
            stop = true;
        } catch (const ContinueSignal&) {
            // sonraki ogeye gec
        }
        if (!replMode && ++guard > 20000000000LL) {
            stop = true;
            error("ZamanAsimi", "sonsuz dongu tespit edildi", n->line);
        }
    });
}

void Interp::execClass(const Node* n, Env& env) {
    auto cls = std::make_shared<ClassObj>();
    cls->name = n->str;
    cls->classEnv = std::make_shared<Env>(envStack_.back());

    // Super sinif / karisimlar govdenin basinda, metotlardan once gelir.
    size_t i = 0;
    while (i < n->kids.size()) {
        const Node* k = n->kid(i);
        if (!k || k->kind == NK::FnDecl || k->kind == NK::VarDecl) break;
        Value base = evalNode(k, env);
        if (!base.isClass()) error("TipHatasi", "super sinif bir sinif olmali", n->line);
        auto sc = asShared<ClassObj>(base);
        if (!cls->super) cls->super = sc;                 // ilk super
        // Ek karisimlar: metotlari ve alan varsayilanlarini kopyala
        for (const auto& mn : sc->methodOrder) {
            if (!cls->methods.count(mn)) {
                cls->methods[mn] = sc->methods[mn];
                cls->methodOrder.push_back(mn);
            }
        }
        for (const auto& fv : sc->fieldDefaults)
            if (!cls->fieldDefaults.count(fv.first)) cls->fieldDefaults[fv.first] = fv.second;
        for (const auto& ft : sc->fieldTypes)
            if (!cls->fieldTypes.count(ft.first)) cls->fieldTypes[ft.first] = ft.second;
        ++i;
    }

    Env& ce = *cls->classEnv;
    ce.define("__cls__", Value::obj(cls));
    for (; i < n->kids.size(); ++i) {
        const Node* m = n->kid(i);
        if (!m || m->kind == NK::ExprStmt) continue;
        if (m->kind == NK::FnDecl) {
            auto fn = std::make_shared<FnObj>();
            fn->name = m->str;
            fn->params = m->names;
            fn->paramTypes = m->paramTypes;
            fn->returnType = m->returnType;
            for (const auto& d : m->kids) fn->defaults.push_back(cloneNode(d.get()));
            fn->body = cloneNode(m->body.get());
            fn->variadic = m->flag2;
            fn->closure = std::make_shared<Env>(cls->classEnv);
            cls->methods[m->str] = Value::obj(fn);
            cls->methodOrder.push_back(m->str);
            ce.define(m->str, Value::obj(fn));
        } else if (m->kind == NK::VarDecl) {
            Value v = m->kid(0) ? evalNode(m->kid(0), ce) : Value::nil();
            if (m->kid(0))
                checkValueType(m->typeName, v, "alan " + m->names[0], m->line);
            cls->fieldDefaults[m->names[0]] = v;
            if (!m->typeName.empty()) cls->fieldTypes[m->names[0]] = m->typeName;
            ce.define(m->names[0], v);
        }
    }
    env.define(n->str, Value::obj(cls));
}

// ===============================================================
// DESENSLEME
// ===============================================================

Value Interp::evalNode(const Node* n, Env& env) {
    if (!n) return Value::nil();
    lastLine = n->line;
    switch (n->kind) {
        case NK::Program: {
            Value last = Value::nil();
            for (const auto& k : n->kids) {
                if (k->kind == NK::ExprStmt) last = evalNode(k->kid(0), env);
                else execStmt(k.get(), env);
            }
            return last;
        }
        case NK::IntLit:   return Value::integer(n->ival);
        case NK::FloatLit: return Value::number(n->fval);
        case NK::BoolLit:  return Value::boolean(n->flag);
        case NK::NilLit:   return Value::nil();
        case NK::Block: {
            EnvScope scope(*this);
            Value last = Value::nil();
            for (const auto& k : n->kids) {
                if (k->kind == NK::ExprStmt) last = evalNode(k->kid(0), *scope.ref);
                else execStmt(k.get(), *scope.ref);
            }
            return last;
        }
        case NK::StrLit: {
            if (n->segs.empty()) return mkStr(n->str);
            std::string out;
            for (const auto& s : n->segs) {
                if (s.isExpr) out += valueToString(evalNode(s.expr.get(), env));
                else out += s.text;
            }
            return mkStr(out);
        }
        case NK::ListLit: {
            std::vector<Value> items;
            items.reserve(n->kids.size());
            for (const auto& k : n->kids) items.push_back(evalNode(k.get(), env));
            return mkList(std::move(items));
        }
        case NK::SetLit: {
            auto s = mkSet();
            for (const auto& k : n->kids) asSet(s)->add(evalNode(k.get(), env));
            return s;
        }
        case NK::TupleLit: {
            std::vector<Value> items;
            items.reserve(n->kids.size());
            for (const auto& k : n->kids) items.push_back(evalNode(k.get(), env));
            return mkTuple(std::move(items));
        }
        case NK::MapLit: {
            auto m = mkMap();
            auto* mp = asMap(m);
            for (size_t i = 0; i + 1 < n->kids.size(); i += 2) {
                mp->items.emplace_back(evalNode(n->kid(i), env),
                                       evalNode(n->kid(i + 1), env));
            }
            mp->reindex();
            return m;
        }
        case NK::RangeLit: {
            auto r = std::make_shared<RangeObj>();
            r->hasStart = n->flag2;
            r->start = (n->kid(0) && n->flag2) ? intOf(evalNode(n->kid(0), env), n->line) : 0;
            r->stop  = n->kid(1) ? intOf(evalNode(n->kid(1), env), n->line) : 0;
            r->step  = n->kid(2) ? intOf(evalNode(n->kid(2), env), n->line) : 1;
            if (r->step == 0) error("Hata", "aralik adimi 0 olamaz", n->line);
            if (n->flag) r->stop -= (r->step > 0 ? 1 : -1);
            return Value::obj(std::move(r));
        }
        case NK::ThisExpr: {
            Value* s = env.lookup("self");
            if (!s) error("Hata", "'this' yalnizca metot icinde kullanilabilir", n->line);
            return *s;
        }
        case NK::Ident: {
            if (n->str == "super") {
                Value* s = env.lookup("self");
                if (!s) error("Hata", "'super' yalnizca metot icinde kullanilabilir", n->line);
                return *s;
            }
            Value* v = env.lookup(n->str);
            if (!v) {
                if (n->str == "argv" || n->str == "__name__") return Value::nil();
                error("AdHatasi", "'" + n->str + "' tanimli degil", n->line);
            }
            return *v;
        }
        case NK::Unary:     return unaryOp(n->str, evalNode(n->kid(0), env), n->line);
        case NK::Binary:    return evalBinary(n, env);
        case NK::Logical:   return evalLogical(n, env);
        case NK::Ternary: {
            Value c = evalNode(n->kid(0), env);
            return valueTruthy(c) ? evalNode(n->kid(1), env) : evalNode(n->kid(2), env);
        }
        case NK::Assign:
            return execAssign(n, env);
        case NK::Call:      return evalCall(n, env);
        case NK::MethodCall: return evalMethodCall(n, env);
        case NK::Get: {
            Value o = evalNode(n->kid(0), env);
            return getMember(o, n->str, n->line);
        }
        case NK::SafeGet: {
            Value o = evalNode(n->kid(0), env);
            if (o.isNil()) return Value::nil();
            if (o.isModule() || o.isMap()) {
                return getMember(o, n->str, n->line);
            }
            return getMember(o, n->str, n->line);
        }
        case NK::Index: {
            Value o = evalNode(n->kid(0), env);
            Value k = evalNode(n->kid(1), env);
            return indexGet(o, k, n->line);
        }
        case NK::Slice: {
            Value o = evalNode(n->kid(0), env);
            Value lo = n->kid(1) ? evalNode(n->kid(1), env) : Value::nil();
            Value hi = n->kid(2) ? evalNode(n->kid(2), env) : Value::nil();
            Value st = n->kid(3) ? evalNode(n->kid(3), env) : Value::nil();
            int64_t len = seqLength(o);
            int64_t stp = st.isNil() ? 1 : intOf(st, n->line);
            if (stp == 0) error("Hata", "dilim adimi 0 olamaz", n->line);
            const bool reverse = stp < 0;
            auto normalize = [&](int64_t v) {
                if (v < 0) v += len;
                if (reverse) return std::clamp<int64_t>(v, -1, len - 1);
                return std::clamp<int64_t>(v, 0, len);
            };
            int64_t l = lo.isNil() ? (reverse ? len - 1 : 0)
                                   : normalize(intOf(lo, n->line));
            int64_t h = hi.isNil() ? (reverse ? -1 : len)
                                   : normalize(intOf(hi, n->line));
            std::vector<Value> out;
            for (int64_t i = l; reverse ? i > h : i < h;) {
                out.push_back(seqAt(o, i, n->line));
                if ((stp > 0 && i > INT64_MAX - stp) ||
                    (stp < 0 && i < INT64_MIN - stp)) break;
                i += stp;
            }
            if (o.isStr()) {
                std::string result;
                result.reserve(out.size());
                for (const auto& ch : out) result += asStr(ch)->s;
                return mkStr(result);
            }
            return mkList(std::move(out));
        }
        case NK::FnDecl: {
            auto fn = std::make_shared<FnObj>();
            fn->name = n->str;
            fn->params = n->names;
            fn->paramTypes = n->paramTypes;
            fn->returnType = n->returnType;
            for (const auto& d : n->kids) fn->defaults.push_back(cloneNode(d.get()));
            fn->body = cloneNode(n->body.get());
            fn->variadic = n->flag2;
            fn->closure = std::make_shared<Env>(envStack_.back());
            return Value::obj(fn);
        }
        case NK::ClassDecl:
            execClass(n, env);
            return *env.lookup(n->str);
        case NK::If: {
            Value c = evalNode(n->kid(0), env);
            const Node* branch = valueTruthy(c) ? n->kid(1) : n->kid(2);
            if (!branch) return Value::nil();
            return evalNode(branch, env);
        }
        case NK::Match: return evalMatch(n, env);
        case NK::Try: {
            try {
                return evalNode(n->kid(0), env);
            } catch (const ThrowValue& tv) {
                if (n->kid(1)) {
                    EnvScope scope(*this);
                    if (!n->names.empty()) scope.ref->define(n->names[0], tv.err);
                    Value last = Value::nil();
                    for (const auto& k : n->kid(1)->kids) last = evalNode(k.get(), *scope.ref);
                    return last;
                }
                throw;
            }
        }
        case NK::Throw:
            throwValue(evalNode(n->kid(0), env), n->line);
        case NK::VarDecl: {
            execStmt(n, env);
            return *env.lookup(n->names[0]);
        }
        default:
            execStmt(n, env);
            return Value::nil();
    }
}

Value Interp::evalBinary(const Node* n, Env& env) {
    const std::string& op = n->str;
    if (op == "and") return evalLogical(n, env);

    Value a = evalNode(n->kid(0), env);
    Value b = evalNode(n->kid(1), env);
    return binaryOp(op, a, b, n->line);
}

Value Interp::evalLogical(const Node* n, Env& env) {
    const std::string& op = n->str;
    Value a = evalNode(n->kid(0), env);

    if (op == "and") {
        if (!valueTruthy(a)) return a;
        return evalNode(n->kid(1), env);
    }
    if (op == "or") {
        if (valueTruthy(a)) return a;
        return evalNode(n->kid(1), env);
    }
    if (op == "??") {
        if (!a.isNil()) return a;
        return evalNode(n->kid(1), env);
    }
    Value b = evalNode(n->kid(1), env);
    return binaryOp(op, a, b, n->line);
}

Value Interp::evalCall(const Node* n, Env& env) {
    const Node* callee = n->kid(0);
    std::vector<Value> args;
    args.reserve(n->kids.size() - 1);
    bool unpack = false;

    if (callee->kind == NK::Ident && callee->str == "super") {
        Value* selfSlot = env.lookup("self");
        if (!selfSlot) error("Hata", "'super' yalnizca metot icinde kullanilabilir", n->line);
        Value* fnSlot = env.lookup("__fn__");
        std::string mname = fnSlot ? asStr(*fnSlot)->s : std::string("init");
        for (size_t i = 1; i < n->kids.size(); ++i) {
            const Node* a = n->kid(i);
            if (a->flag) { unpack = true; continue; }
            Value v = evalNode(a, env);
            if (unpack) iterate(v, [&](const Value& x) { args.push_back(x); });
            else args.push_back(v);
        }
        return callSuper(mname, std::move(args), n->line, *selfSlot);
    }

    Value fn = evalNode(callee, env);
    if (n->flag && fn.isNil()) return Value::nil();   // ?.() guvenli cagri
    for (size_t i = 1; i < n->kids.size(); ++i) {
        const Node* a = n->kid(i);
        if (!a) continue;
        if (a->flag) { unpack = true; continue; }
        Value v = evalNode(a, env);
        if (unpack) iterate(v, [&](const Value& x) { args.push_back(x); });
        else args.push_back(v);
    }
    return callValue(fn, std::move(args), n->line);
}

Value Interp::evalMethodCall(const Node* n, Env& env) {
    const Node* base = n->kid(0);

    std::vector<Value> args;
    args.reserve(n->kids.size() - 1);
    for (size_t i = 1; i < n->kids.size(); ++i)
        args.push_back(evalNode(n->kid(i), env));

    // super.meth(...)
    if (base->kind == NK::Ident && base->str == "super") {
        Value* selfSlot = env.lookup("self");
        if (!selfSlot) error("Hata", "'super' yalnizca metot icinde kullanilabilir", n->line);
        return callSuper(n->str, std::move(args), n->line, *selfSlot);
    }

    Value obj = evalNode(base, env);
    if (obj.isNil() && n->flag) return Value::nil();
    return callMethod(obj, n->str, std::move(args), n->line);
}

// ===============================================================
// match
// ===============================================================

bool Interp::matchPattern(const Node* pat, const Value& v, Env& env, bool bind) {
    if (!pat) return false;
    switch (pat->kind) {
        case NK::IntLit:
        case NK::FloatLit:
        case NK::StrLit:
        case NK::BoolLit:
        case NK::NilLit: {
            Value pv = evalNode(pat, env);
            return valueEquals(pv, v);
        }
        case NK::Ident: {
            if (pat->str == "_") return true;
            if (bind) env.define(pat->str, v);
            return true;
        }
        case NK::Unary: {
            Value pv = evalNode(pat, env);
            return valueEquals(pv, v);
        }
        case NK::RangeLit: {
            Value pv = evalNode(pat, env);
            if (pv.isRange() && v.isNum()) {
                auto* r = asRange(pv);
                int64_t iv = v.isInt() ? v.i : static_cast<int64_t>(v.d);
                return r->contains(iv);
            }
            return false;
        }
        case NK::ListLit: {
            if (!v.isSeq()) return false;
            int64_t len = seqLength(v);
            if (len != static_cast<int64_t>(pat->kids.size())) return false;
            for (size_t i = 0; i < pat->kids.size(); ++i)
                if (!matchPattern(pat->kid(i), seqAt(v, static_cast<int64_t>(i), pat->line),
                                  env, bind))
                    return false;
            return true;
        }
        case NK::TupleLit: {
            if (!v.isTuple() && !v.isList()) return false;
            if (seqLength(v) != static_cast<int64_t>(pat->kids.size())) return false;
            for (size_t i = 0; i < pat->kids.size(); ++i)
                if (!matchPattern(pat->kid(i), seqAt(v, static_cast<int64_t>(i), pat->line),
                                  env, bind))
                    return false;
            return true;
        }
        case NK::MapLit: {
            if (!v.isMap()) return false;
            auto* m = asMap(v);
            for (size_t i = 0; i + 1 < pat->kids.size(); i += 2) {
                Value pk = evalNode(pat->kid(i), env);
                if (bind && pk.isStr() && !pk.isNil()) {
                    if (bind) env.define(asStr(pk)->s, Value::nil());
                }
                const Value* got = m->get(pk);
                if (!got) return false;
                if (bind && pk.isStr())
                    env.vars[asStr(pk)->s] = *got;
                if (!matchPattern(pat->kid(i + 1), *got, env, bind)) return false;
            }
            return true;
        }
        default: {
            Value pv = evalNode(pat, env);
            return valueEquals(pv, v);
        }
    }
}

Value Interp::evalMatch(const Node* n, Env& env) {
    Value subject = evalNode(n->kid(0), env);
    for (size_t i = 1; i < n->kids.size(); ++i) {
        const Node* clause = n->kid(i);
        EnvScope scope(*this);
        const Node* pats = clause->kid(0);
        bool matched = false;
        for (const auto& p : pats->kids) {
            if (matchPattern(p.get(), subject, *scope.ref, true)) { matched = true; break; }
        }
        if (!matched) continue;
        if (clause->kid(1)) {
            Value g = evalNode(clause->kid(1), *scope.ref);
            if (!valueTruthy(g)) continue;
        }
        return evalNode(clause->kid(2), *scope.ref);
    }
    return Value::nil();
}

} // namespace k7
