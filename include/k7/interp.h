// K7 Language - Yorumlayici
#pragma once

#include "k7/ast.h"
#include "k7/value.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace k7 {

using NativeFn = std::function<Value(Interp&, std::vector<Value>&)>;

// --- Kontur sinyalleri ---
struct ThrowValue { Value err; };     // K7 throw / calisma zamani hatasi
struct ReturnValue { Value v; };
struct BreakSignal {};
struct ContinueSignal {};
struct ExitRequest { int code; };
struct QuitSignal {};

// --- Kapsam ---
class Env {
public:
    std::shared_ptr<Env> parent;
    std::unordered_map<std::string, Value> vars;
    std::unordered_map<std::string, std::string> declaredTypes;
    bool isGlobal = false;

    Env() = default;
    explicit Env(std::shared_ptr<Env> p) : parent(std::move(p)) {}

    Value* lookup(const std::string& n);
    std::string* lookupType(const std::string& n);
    void define(const std::string& n, const Value& v, const std::string& type = {}) {
        vars[n] = v;
        if (!type.empty()) declaredTypes[n] = type;
    }
    bool has(const std::string& n) { return lookup(n) != nullptr; }
};

class Interp {
public:
    Interp();
    ~Interp();

    // --- Genel API ---
    void registerBuiltins();
    void registerModules();

    // Bir kaynagi calistirir. Hata olursa stderr'e yazar, false doner.
    bool runSource(const std::string& source, const std::string& filename);
    // REPL icin tek bir ifade/deyim calistirir
    void runInteractive(const std::string& line);

    Value evalNode(const Node* n, Env& env);

    // --- Yardimci cagrilar ---
    Value callValue(const Value& callee, std::vector<Value> args, int line,
                    const std::string& desc = "");
    Value callValueOn(const Value& fn, const Value& self, std::vector<Value> args,
                      int line);
    Value callMethod(const Value& obj, const std::string& name,
                     std::vector<Value> args, int line);
    Value getMember(const Value& obj, const std::string& name, int line);
    void  setMember(const Value& obj, const std::string& name, const Value& v, int line);
    Value invoke(const Value& cls, std::vector<Value> args, int line);

    Value indexGet(const Value& obj, const Value& key, int line);
    void  indexSet(const Value& obj, const Value& key, const Value& val, int line);
    Value binaryOp(const std::string& op, const Value& a, const Value& b, int line);
    Value unaryOp(const std::string& op, const Value& a, int line);
    Value containsOp(const Value& container, const Value& item, int line);

    Value toStr(const Value& v, int line);
    Value toInt(const Value& v, int line);
    Value toFloat(const Value& v, int line);
    void checkValueType(const std::string& expected, const Value& value,
                        const std::string& context, int line);

    // Donusturucu kisayollari (yerlesik kod icin)
    std::string strOf(const Value& v, int line = -1) {
        Value r = toStr(v, line);
        return r.isStr() ? asStr(r)->s : std::string();
    }
    int64_t intOf(const Value& v, int line = -1) {
        Value r = toInt(v, line);
        if (r.isInt()) return r.i;
        if (r.isFloat()) return static_cast<int64_t>(r.d);
        return 0;
    }
    double numOf(const Value& v, int line = -1) {
        Value r = toFloat(v, line);
        return r.isFloat() ? r.d : static_cast<double>(r.i);
    }

    // --- Sekiller ---
    int64_t  seqLength(const Value& v);
    Value    seqAt(const Value& v, int64_t i, int line);
    void     iterate(const Value& v, const std::function<void(const Value&)>& fn);
    void     iterateIndexed(const Value& v,
                            const std::function<void(const Value&, const Value&, int64_t)>& fn);
    void     seqIterate(const Value& v, const std::function<void(const Value&)>& fn);
    bool     unpackPair(const Value& v, Value& a, Value& b, int line);

    // --- Hata raporlama ---
    [[noreturn]] void error(const std::string& kind, const std::string& msg,
                            int line = -1);
    [[noreturn]] void throwValue(const Value& v, int line);
    std::string currentLoc() const;
    const std::vector<std::string>& callStack() const { return stack_; }

    // --- Metot kaydi ---
    void registerMethod(VT t, const std::string& name, NativeFn fn);
    Value findBuiltinMethod(const Value& obj, const std::string& name) const;

    // --- Moduller ---
    void registerModule(const std::string& name, std::function<void(ModuleObj&)> init);
    Value importModule(const std::string& name, int line);
    Value importPath(const std::string& file, int line);

    // --- Yardimcilar ---
    std::string typeNameOf(const Value& v);
    Value makeObject(std::shared_ptr<ClassObj> cls);
    Value callSuper(const std::string& method, std::vector<Value> args, int line,
                    const Value& self);
    std::string formatString(const std::string& fmt, const Value& arg, int line);
    const ClassObj* curClass() const;

    std::shared_ptr<Env> globals() { return globals_; }
    std::shared_ptr<Env> curEnv() { return cur_; }

    int lastLine = 0;
    bool replMode = false;

private:
    std::shared_ptr<Env> globals_;
    std::shared_ptr<Env> cur_;
    std::vector<std::string> stack_;

    std::unordered_map<VT, std::unordered_map<std::string, Value>> methods_;
    std::unordered_map<std::string, Value> modules_;
    std::unordered_map<std::string, Value> pathModules_;
    std::vector<std::string> sourceDirs_;

    //Ic yurutucu
    void  execBlock(const Node* n, Env& env);
    Value execAssign(const Node* n, Env& env);
    void  execAssignOne(const Node* lhs, Env& env, const Value& v);
    void  execStmt(const Node* n, Env& env);
    void  execClass(const Node* n, Env& env);
    Value evalCall(const Node* n, Env& env);
    Value evalMethodCall(const Node* n, Env& env);
    void  evalFor(const Node* n, Env& env);
    void  evalWhile(const Node* n, Env& env);
    void  evalLoop(const Node* n, Env& env);
    Value evalMatch(const Node* n, Env& env);
    bool  matchPattern(const Node* pat, const Value& v, Env& env, bool bind);

    Value callFunction(const FnObj* fn, std::vector<Value>& args, const Value& self,
                       bool hasSelf, int line);
    Value callBound(const Value& callee, std::vector<Value> args, int line);
    Value instantiate(std::shared_ptr<ClassObj> cls, std::vector<Value> args, int line);

    Value evalBinary(const Node* n, Env& env);
    Value evalLogical(const Node* n, Env& env);

public:
    std::unordered_map<std::string, Value>& modules() { return modules_; }

    // Ic ice kapsam yonetimi (RAII: return/break gibi durumlarda da temizlenir)
    struct EnvScope {
        Interp& in;
        Env* ref = nullptr;
        explicit EnvScope(Interp& i);
        ~EnvScope();
        EnvScope(const EnvScope&) = delete;
        EnvScope& operator=(const EnvScope&) = delete;
    };
    void pushEnv(std::shared_ptr<Env> parent);
    void popEnv();

private:
    std::vector<std::shared_ptr<Env>> envStack_;
};

std::string fmtFloatK7(double d);

} // namespace k7
