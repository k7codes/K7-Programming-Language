// K7 Language - Cagri altyapisi ve moduller
#include "k7/interp.h"
#include "k7/parser.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace k7 {

namespace fs = std::filesystem;

// ===============================================================
// printf benzeri bicimleme ("%s %d" % (a, b))
// ===============================================================

std::string Interp::formatString(const std::string& fmt, const Value& arg, int line) {
    std::vector<Value> args;
    iterate(arg, [&](const Value& v) { args.push_back(v); });
    if (args.size() == 1 && !arg.isSeq()) args.push_back(arg);

    std::string out;
    size_t ai = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') { out += '%'; ++i; continue; }

        std::string spec = "%";
        ++i;
        while (i < fmt.size() && std::strchr("-+ #0123456789.", fmt[i])) spec += fmt[i++];
        if (i >= fmt.size()) { out += spec; break; }
        char conv = fmt[i];
        Value v = (ai < args.size()) ? args[ai++] : Value::nil();

        char buf[64];
        switch (conv) {
            case 'd': case 'i':
                spec += "lld";
                std::snprintf(buf, sizeof(buf), spec.c_str(),
                              static_cast<long long>(toInt(v, line).i));
                out += buf;
                break;
            case 'x': case 'X': case 'o':
                spec += "ll";
                spec += conv;
                std::snprintf(buf, sizeof(buf), spec.c_str(),
                              static_cast<long long>(toInt(v, line).i));
                out += buf;
                break;
            case 'f': case 'e': case 'g':
                spec += conv;
                std::snprintf(buf, sizeof(buf), spec.c_str(), toFloat(v, line).d);
                out += buf;
                break;
            case 's':
                spec += 's';
                {
                    std::string s = valueToString(v);
                    std::vector<char> tmp(s.begin(), s.end());
                    tmp.push_back('\0');
                    std::snprintf(buf, sizeof(buf), spec.c_str(), tmp.data());
                    out += buf;
                }
                break;
            case 'r':
                out += valueToRepr(v);
                break;
            default:
                out += spec + conv;
                break;
        }
    }
    return out;
}

// ===============================================================
// Kullanici fonksiyonu cagrisi
// ===============================================================

Value Interp::callFunction(const FnObj* fn, std::vector<Value>& args,
                           const Value& self, bool hasSelf, int line) {
    size_t required = 0;
    for (size_t i = 0; i < fn->params.size(); ++i)
        if (i >= fn->defaults.size() || !fn->defaults[i]) required = i + 1;

    size_t maxP = fn->params.size() + (fn->variadic ? 1 : 0);
    if (args.size() < required || args.size() > maxP) {
        std::string msg = fn->name + "() " + std::to_string(required) + "..";
        if (fn->variadic) msg += "n";
        else if (maxP > required) msg += std::to_string(maxP);
        msg += " parametre bekleniyordu, " + std::to_string(args.size()) + " verildi";
        error("TipHatasi", msg, line);
    }

    auto local = std::make_shared<Env>(fn->closure);
    std::vector<std::string> instanceFields;
    if (hasSelf) {
        if (self.isObj()) {
            for (const auto& field : asObject(self)->fields) {
                const bool shadowed = std::find(fn->params.begin(), fn->params.end(),
                                                field.first) != fn->params.end();
                if (!shadowed && field.first != "self" && field.first != "this" &&
                    field.first != "__fn__") {
                    local->define(field.first, field.second);
                    instanceFields.push_back(field.first);
                }
            }
        }
        local->define("self", self);
        local->define("this", self);
    }
    local->define("__fn__", mkStr(fn->name));
    local->vars.reserve(fn->params.size() + 2);

    for (size_t i = 0; i < fn->params.size(); ++i) {
        const std::string paramType = i < fn->paramTypes.size()
                                          ? fn->paramTypes[i] : std::string();
        if (i < args.size()) {
            if (!paramType.empty())
                checkValueType(paramType, args[i],
                               "parametre " + fn->params[i], line);
            local->define(fn->params[i], args[i], paramType);
        } else if (i < fn->defaults.size() && fn->defaults[i]) {
            EnvScope s(*this);
            Value value = evalNode(fn->defaults[i].get(), *s.ref);
            checkValueType(paramType, value, "parametre " + fn->params[i], line);
            local->define(fn->params[i], value, paramType);
        } else {
            local->define(fn->params[i], Value::nil(), paramType);
        }
    }
    // *args / **kwargs
    if (fn->variadic && args.size() > fn->params.size()) {
        std::vector<Value> extra(args.begin() +
                                     static_cast<ptrdiff_t>(fn->params.size()), args.end());
        local->define("args", mkTuple(std::move(extra)));
    } else {
        local->define("args", mkTuple({}));
    }

    auto syncInstanceFields = [&]() {
        if (!self.isObj()) return;
        auto* object = asObject(self);
        for (const auto& name : instanceFields) {
            auto it = local->vars.find(name);
            if (it != local->vars.end()) setMember(self, name, it->second, line);
        }
    };

    pushEnv(local);
    try {
        EnvScope blockScope(*this);
        Value r = Value::nil();
        bool explicitReturn = false;
        try {
            r = evalNode(fn->body.get(), *blockScope.ref);
        } catch (const ReturnValue& rv) {
            r = rv.v;                       // acik return: degeri dondur
            explicitReturn = true;
        }
        if (fn->returnType == "void" && !explicitReturn) r = Value::nil();
        syncInstanceFields();
        checkValueType(fn->returnType, r, "fonksiyon " + fn->name + " donusu", line);
        popEnv();
        return r;
    } catch (...) {
        syncInstanceFields();
        popEnv();
        throw;
    }
}

Value Interp::callBound(const Value& callee, std::vector<Value> args, int line) {
    auto* b = asBound(callee);
    args.insert(args.begin(), b->self);
    return callValue(b->fn, std::move(args), line);
}

Value Interp::callValueOn(const Value& fn, const Value& self, std::vector<Value> args,
                          int line) {
    if (fn.t == VT::Fn) {
        auto* f = asFn(fn);
        return callFunction(f, args, self, true, line);
    }
    return callValue(fn, std::move(args), line);
}

// ===============================================================
// Genel cagri
// ===============================================================

Value Interp::callValue(const Value& callee, std::vector<Value> args, int line,
                        const std::string& desc) {
    switch (callee.t) {
        case VT::Fn: {
            if (stack_.size() > 2000)
                error("AsamaLimit", "cagri derinligi asildi (olası sonsuz ozyineleme)",
                      line);
            std::string label = asFn(callee)->name;
            stack_.push_back(label + " (satir " + std::to_string(line) + ")");
            try {
                return callFunction(asFn(callee), args, Value::nil(), false, line);
            } catch (...) {
                if (!stack_.empty()) stack_.pop_back();
                throw;
            }
        }
        case VT::Native: {
            auto* nat = asNative(callee);
            if (static_cast<int>(args.size()) < nat->minArgs)
                error("TipHatasi", nat->name + "() en az " + std::to_string(nat->minArgs) +
                                  " parametre bekleniyordu, " + std::to_string(args.size()) +
                                  " verildi", line);
            if (nat->maxArgs >= 0 && static_cast<int>(args.size()) > nat->maxArgs)
                error("TipHatasi", nat->name + "() en fazla " + std::to_string(nat->maxArgs) +
                                  " parametre kabul eder", line);
            return nat->fn(*this, args);
        }
        case VT::Bound:
            return callBound(callee, std::move(args), line);
        case VT::Class:
            return instantiate(asShared<ClassObj>(callee), std::move(args), line);
        default:
            error("TipHatasi",
                  "'" + valueToRepr(callee) + "' turu cagrilabilir degil" +
                      (desc.empty() ? "" : " (" + desc + ")"), line);
    }
}

Value Interp::invoke(const Value& cls, std::vector<Value> args, int line) {
    if (!cls.isClass()) error("TipHatasi", "sinif bekleniyordu", line);
    auto c = asShared<ClassObj>(cls);
    return instantiate(c, std::move(args), line);
}

// ===============================================================
// Metot cagrisi
// ===============================================================

Value Interp::callMethod(const Value& obj, const std::string& name,
                         std::vector<Value> args, int line) {
    // 1) Kullanici sinif metotlari
    if (obj.isObj()) {
        auto* o = asObject(obj);
        if (o->cls) {
            Value m = o->cls->findMethod(name);
            if (!m.isNil()) return callValueOn(m, obj, std::move(args), line);
        }
        // Alanda saklanan fonksiyon (orn. kapatma/geri cagirma)
        auto fit = o->fields.find(name);
        if (fit != o->fields.end() && fit->second.isFn())
            return callValue(fit->second, std::move(args), line);
    }

    // 2) Dahili tur metotlari (str/list/map/set)
    Value m = findBuiltinMethod(obj, name);
    if (!m.isNil()) {
        std::vector<Value> a2;
        a2.reserve(args.size() + 1);
        a2.push_back(obj);
        for (auto& a : args) a2.push_back(a);
        return callValue(m, std::move(a2), line);
    }

    // 3) Modul uyesi bir fonksiyona isaret ediyorsa cagir
    if (obj.isModule()) {
        auto* mo = asModule(obj);
        auto it = mo->members.find(name);
        if (it != mo->members.end()) return callValue(it->second, std::move(args), line);
        error("AttributeHatasi", "modulde '" + name + "' uyesi yok", line);
    }
    if (obj.isClass()) {
        Value f = asClass(obj)->findMethod(name);
        if (!f.isNil()) return callValue(f, std::move(args), line);
        error("AttributeHatasi", asClass(obj)->name + " sinifinda '" + name +
                                "' metodu yok", line);
    }

    // 4) Alan deger olarak bir fonksiyon isaret ediyorsa
    if (obj.isObj()) {
        auto* o = asObject(obj);
        auto it = o->fields.find(name);
        if (it != o->fields.end()) return callValue(it->second, std::move(args), line);
        error("AttributeHatasi", o->className + " nesnesinde '" + name + "' uyesi yok",
              line);
    }

    error("AttributeHatasi", typeNameOf(obj) + " turunde '" + name + "' metodu yok", line);
}

const ClassObj* Interp::curClass() const {
    if (envStack_.empty()) return nullptr;
    Value* v = envStack_.back()->lookup("__cls__");
    if (!v || !v->isClass()) return nullptr;
    return asClass(*v);
}

Value Interp::callSuper(const std::string& method, std::vector<Value> args, int line,
                        const Value& self) {
    if (!self.isObj()) error("Hata", "super cagrisi nesne disinda", line);
    std::shared_ptr<ClassObj> cls;
    if (self.isObj() && asObject(self)->cls) cls = asObject(self)->cls;
    if (!cls || !cls->super)
        error("Hata", "'" + (self.isObj() ? asObject(self)->className : "?") +
                      "' sinifinin ust sinifi yok", line);

    std::string mname = method.empty() ? std::string("init") : method;
    Value m = cls->super->findMethod(mname);
    if (m.isNil()) return Value::nil();
    return callValueOn(m, self, std::move(args), line);
}

// ===============================================================
// Moduller
// ===============================================================

void Interp::registerModule(const std::string& name,
                            std::function<void(ModuleObj&)> init) {
    if (modules_.count(name)) return;
    auto mod = std::make_shared<ModuleObj>();
    mod->name = name;
    modules_[name] = Value::obj(mod);
    init(*mod);
}

Value Interp::importModule(const std::string& name, int line) {
    auto it = modules_.find(name);
    if (it != modules_.end()) return it->second;
    error("ModuleHatasi", "'" + name + "' modulu bulunamadi (tanili moduller: " +
                      std::string("math, time, json, fs, random, os, str, re") + ")", line);
}

Value Interp::importPath(const std::string& file, int line) {
    fs::path requested(file);
    fs::path p = requested;
    std::error_code ec;
    bool found = requested.is_absolute() && fs::exists(requested, ec);
    if (!found && requested.is_relative() && !sourceDirs_.empty() &&
        !sourceDirs_.back().empty()) {
        p = fs::path(sourceDirs_.back()) / requested;
        found = fs::exists(p, ec);
    }
    if (!found) {
        p = requested.is_absolute() ? requested : fs::current_path(ec) / requested;
        found = fs::exists(p, ec);
    }
    if (!found) error("ModuleHatasi", "dosya bulunamadi: " + file, line);

    ec.clear();
    p = fs::weakly_canonical(p, ec);
    if (ec) {
        ec.clear();
        p = fs::absolute(p, ec).lexically_normal();
    }
    const std::string key = p.generic_string();
    auto cached = pathModules_.find(key);
    if (cached != pathModules_.end()) return cached->second;

    std::ifstream in(p, std::ios::binary);
    if (!in) error("ModuleHatasi", "dosya acilamadi: " + file, line);
    std::stringstream ss;
    ss << in.rdbuf();

    auto mod = std::make_shared<ModuleObj>();
    mod->name = p.stem().string();
    Value modVal = Value::obj(mod);
    pathModules_.emplace(key, modVal); // dongusel importlarda ayni modul nesnesini dondur

    // Modul kendi kapsaminda calisir; disa aktarim export ile yapilir
    auto moduleEnv = std::make_shared<Env>(globals_);
    moduleEnv->define("__name__", mkStr(mod->name));
    moduleEnv->define("__file__", mkStr(file));
    moduleEnv->define("export", mkNative("export", [this](Interp&, std::vector<Value>& args) {
        return Value::nil();
    }, 0, -1));

    bool envPushed = false;
    bool sourcePushed = false;
    try {
        Parser parser(ss.str(), file);
        NodePtr prog = parser.parse();
        if (parser.hasErrors()) {
            for (const auto& e : parser.errors()) std::cerr << e.msg << "\n";
            error("ModuleHatasi", "modul ayristirilamadi: " + file, line);
        }
        pushEnv(moduleEnv);
        envPushed = true;
        sourceDirs_.push_back(p.parent_path().string());
        sourcePushed = true;
        evalNode(prog.get(), *moduleEnv);
        sourceDirs_.pop_back();
        sourcePushed = false;
        popEnv();
        envPushed = false;
    } catch (...) {
        if (sourcePushed) sourceDirs_.pop_back();
        if (envPushed) popEnv();
        pathModules_.erase(key);
        throw;
    }

    // export isaretlenen uyeler harici her sey disari acilir
    Value exp = moduleEnv->lookup("export") ? *moduleEnv->lookup("export") : Value::nil();
    for (auto& kv : moduleEnv->vars) {
        if (!kv.first.empty() && kv.first[0] == '_') continue;
        if (kv.first == "export") continue;
        mod->order.push_back(kv.first);
        mod->members[kv.first] = kv.second;
    }
    (void)exp;
    return modVal;
}

} // namespace k7
