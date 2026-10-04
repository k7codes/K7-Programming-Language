// K7 Language - Deger sistemi ve nesne modeli
#pragma once

#include "k7/ast.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace k7 {

class Interp;
class Env;

enum class VT : uint8_t {
    Nil,
    Bool,
    Int,
    Float,
    Str,
    List,
    Tuple,
    Map,
    Set,
    Range,
    Fn,
    Native,
    Bound,     // metoda baglanmis fonksiyon
    Object,
    Class,
    Module,
    Error,
    Cell,
};

const char* typeName(VT t);

struct Object {
    VT vt;
    explicit Object(VT t) : vt(t) {}
    virtual ~Object() = default;
};

struct Value {
    VT t = VT::Nil;
    union {
        bool b;
        int64_t i;
        double d;
    };
    std::shared_ptr<Object> o;

    Value() : t(VT::Nil), i(0) {}

    bool isNil()   const { return t == VT::Nil; }
    bool isBool()  const { return t == VT::Bool; }
    bool isInt()   const { return t == VT::Int; }
    bool isFloat() const { return t == VT::Float; }
    bool isNum()   const { return t == VT::Int || t == VT::Float; }
    bool isStr()   const { return t == VT::Str; }
    bool isList()  const { return t == VT::List; }
    bool isTuple() const { return t == VT::Tuple; }
    bool isSeq()   const { return t == VT::List || t == VT::Tuple || t == VT::Str; }
    bool isMap()   const { return t == VT::Map; }
    bool isSet()   const { return t == VT::Set; }
    bool isFn()    const { return t == VT::Fn || t == VT::Native || t == VT::Bound; }
    bool isObj()   const { return t == VT::Object; }
    bool isClass() const { return t == VT::Class; }
    bool isModule()const { return t == VT::Module; }
    bool isErr()   const { return t == VT::Error; }
    bool isRange() const { return t == VT::Range; }
    bool isCell()  const { return t == VT::Cell; }
    bool isCallable() const { return isFn(); }

    double asNum() const { return t == VT::Int ? static_cast<double>(i) : d; }

    static Value nil()   { return Value(); }
    static Value boolean(bool v) { Value x; x.t = VT::Bool; x.b = v; return x; }
    static Value integer(int64_t v) { Value x; x.t = VT::Int; x.i = v; return x; }
    static Value number(double v) { Value x; x.t = VT::Float; x.d = v; return x; }
    static Value obj(std::shared_ptr<Object> p) {
        Value x;
        x.t = p ? p->vt : VT::Nil;      // once etiket okunur, sonra tasinir
        x.o = std::move(p);
        if (x.t == VT::Int || x.t == VT::Float || x.t == VT::Bool) x.o.reset();
        return x;
    }
};

// ---------------------------------------------------------------
// Somut nesneler
// ---------------------------------------------------------------

struct StrObj : Object {
    std::string s;
    explicit StrObj(std::string v) : Object(VT::Str), s(std::move(v)) {}
};

struct ListObj : Object {
    std::vector<Value> items;
    ListObj() : Object(VT::List) {}
    explicit ListObj(std::vector<Value> v)
        : Object(VT::List), items(std::move(v)) {}
};

struct TupleObj : Object {
    std::vector<Value> items;
    TupleObj() : Object(VT::Tuple) {}
    explicit TupleObj(std::vector<Value> v)
        : Object(VT::Tuple), items(std::move(v)) {}
};

struct MapObj;
using MapEntries = std::vector<std::pair<Value, Value>>;

// Sikis sirali harita: ekleme sirasi korunur, hash indeksi ile O(1) arama.
struct MapObj : Object {
    MapEntries items;
    std::unordered_map<uint64_t, std::vector<uint32_t>> index;
    bool indexed = false;

    MapObj() : Object(VT::Map) {}

    void reindex();
    size_t findKey(const Value& k) const;
    void set(const Value& k, const Value& v);
    bool erase(const Value& k);
    const Value* get(const Value& k) const;
    bool has(const Value& k) const { return findKey(k) != static_cast<size_t>(-1); }
};

struct SetObj : Object {
    std::vector<Value> items;
    SetObj() : Object(VT::Set) {}
    bool contains(const Value& k) const;
    void add(const Value& k);
    bool erase(const Value& k);
};

struct RangeObj : Object {
    int64_t start = 0, stop = 0, step = 1;
    bool hasStart = true;
    RangeObj() : Object(VT::Range) {}
    int64_t length() const;
    int64_t at(int64_t i) const;
    bool contains(int64_t v) const;
};

class Env;
struct ClassObj;
struct ModuleObj;
struct ObjectObj;
struct BoundObj;
struct CellObj;
struct ErrorObj;

struct NativeObj : Object {
    std::string name;
    std::function<Value(Interp&, std::vector<Value>&)> fn;
    int minArgs = 0;
    int maxArgs = -1;   // -1 = sinirsiz
    NativeObj() : Object(VT::Native) {}
};

struct FnObj : Object {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> paramTypes;
    std::string returnType;
    std::vector<NodePtr> defaults;      // null => gerekli
    NodePtr body;
    std::shared_ptr<Env> closure;
    bool variadic = false;
    FnObj() : Object(VT::Fn) {}
};

struct BoundObj : Object {
    Value fn;
    Value self;
    BoundObj() : Object(VT::Bound) {}
};

struct CellObj : Object {
    Value v;
    CellObj() : Object(VT::Cell) {}
};

struct ErrorObj : Object {
    std::string kind = "Error";
    std::string msg;
    Value extra;
    std::vector<std::string> trace;
    ErrorObj() : Object(VT::Error) {}
};

struct AttrEntry {
    Value value;
    bool isMethod = false;
};

struct StrObj; struct ListObj; struct TupleObj; struct MapObj; struct SetObj;
struct RangeObj; struct FnObj; struct NativeObj; struct BoundObj; struct CellObj;
struct ErrorObj; struct ObjectObj; struct ClassObj; struct ModuleObj;

struct ObjectObj : Object {
    std::string className;
    std::shared_ptr<ClassObj> cls;   // tanimlandigi sinif (metot cozumlemesi icin)
    std::unordered_map<std::string, Value> fields;
    ObjectObj() : Object(VT::Object) {}
};

struct ClassObj : Object {
    std::string name;
    std::shared_ptr<ClassObj> super;
    std::unordered_map<std::string, Value> methods;
    std::vector<std::string> methodOrder;
    std::unordered_map<std::string, Value> fieldDefaults;
    std::unordered_map<std::string, std::string> fieldTypes;
    std::shared_ptr<Env> classEnv;
    ClassObj() : Object(VT::Class) {}
    Value findMethod(const std::string& name) const;
    Value fieldDefault(const std::string& name) const;
};

struct ModuleObj : Object {
    std::string name;
    std::unordered_map<std::string, Value> members;
    std::vector<std::string> order;
    ModuleObj() : Object(VT::Module) {}
};

// ---------------------------------------------------------------
// Erisim yardimcilari
// ---------------------------------------------------------------

inline StrObj*      asStr(const Value& v)   { return static_cast<StrObj*>(v.o.get()); }
inline ListObj*     asList(const Value& v)  { return static_cast<ListObj*>(v.o.get()); }
inline TupleObj*    asTuple(const Value& v) { return static_cast<TupleObj*>(v.o.get()); }
inline MapObj*      asMap(const Value& v)   { return static_cast<MapObj*>(v.o.get()); }
inline SetObj*      asSet(const Value& v)   { return static_cast<SetObj*>(v.o.get()); }
inline RangeObj*    asRange(const Value& v) { return static_cast<RangeObj*>(v.o.get()); }
inline FnObj*       asFn(const Value& v)    { return static_cast<FnObj*>(v.o.get()); }
inline NativeObj*   asNative(const Value& v){ return static_cast<NativeObj*>(v.o.get()); }
inline BoundObj*    asBound(const Value& v) { return static_cast<BoundObj*>(v.o.get()); }
inline CellObj*     asCell(const Value& v)  { return static_cast<CellObj*>(v.o.get()); }
inline ErrorObj*    asErr(const Value& v)   { return static_cast<ErrorObj*>(v.o.get()); }
inline ObjectObj*   asObject(const Value& v){ return static_cast<ObjectObj*>(v.o.get()); }
inline ClassObj*    asClass(const Value& v) { return static_cast<ClassObj*>(v.o.get()); }
inline ModuleObj*   asModule(const Value& v){ return static_cast<ModuleObj*>(v.o.get()); }

// VT etiketi turun dogru oldugundan guvenli: ham isaretciyi paylasilan
// isaretciye cevirir (asClass vb. ile birlikte kullanilir).
template <class T>
std::shared_ptr<T> asShared(const Value& v) {
    return std::static_pointer_cast<T>(v.o);
}

Value mkStr(const std::string& s);
Value mkStrShared(std::shared_ptr<StrObj> s);
Value mkList(std::vector<Value> items = {});
Value mkTuple(std::vector<Value> items);
Value mkMap();
Value mkSet();
Value mkError(const std::string& kind, const std::string& msg);
Value mkNative(const std::string& name,
               std::function<Value(Interp&, std::vector<Value>&)> fn,
               int minArgs = 0, int maxArgs = -1);

// Hash / esitlik
uint64_t hashValue(const Value& v);
bool valueEquals(const Value& a, const Value& b);
bool strictEquals(const Value& a, const Value& b);

// Karsilastirma: -1, 0, 1 ; karsilastirilamazsa hata firlatilir (caller handle eder)
int compareValues(const Value& a, const Value& b);

// Gosterim
std::string valueToString(const Value& v);
std::string valueToRepr(const Value& v);
bool valueTruthy(const Value& v);

// Kopyalama
Value deepCopy(const Value& v);

const std::string& internStr(const std::string& s);

} // namespace k7
