#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Member;

struct Value {
    enum Type : uint8_t { Null, Bool, Int, Num, Str, Arr, Obj, Bytes };
    Type t = Null;
    uint32_t n = 0;
    union {
        bool b;
        int32_t i;
        const double* dp;
        const char* s;
        const Value* a;
        const Member* o;
        const uint8_t* bytes;
    };
    Value() : dp(nullptr) {}
    double dbl() const { return dp ? *dp : 0.0; }

    bool is_null() const { return t == Null; }
    bool is_arr() const { return t == Arr; }
    bool is_obj() const { return t == Obj; }
    bool is_str() const { return t == Str; }
    bool is_num() const { return t == Int || t == Num; }
    size_t size() const { return (t == Arr || t == Obj || t == Str || t == Bytes) ? n : 0; }
    const Value& operator[](size_t idx) const;
    const Value& operator[](int idx) const { return (*this)[(size_t)(idx < 0 ? 0 : idx)]; }
    const Value& operator[](const char* key) const;
    const Value* find(const char* key) const;
    int as_int(int def = 0) const;
    double num(double def = 0) const;
    bool truthy() const;
    const char* str(const char* def = "") const;
    std::string string() const { return std::string(str()); }
    const Member* begin_members() const;
    const Member* end_members() const;
    static const Value& null_value();
};

struct Member {
    const char* key;
    Value v;
};

inline const Member* Value::begin_members() const { return t == Obj ? o : nullptr; }
inline const Member* Value::end_members() const { return t == Obj ? o + n : nullptr; }

class Document {
public:
    bool load_data(const std::string& rel);
    bool load_memory(std::vector<uint8_t>&& data);
    const Value& root() const { return root_; }
    size_t memory() const { return data_.size() + body_.size() + arena_bytes_; }

private:
    std::vector<uint8_t> data_;
    std::vector<const char*> strings_;
    std::vector<std::unique_ptr<uint8_t[]>> blocks_;
    size_t block_used_ = 0;
    size_t block_size_ = 0;
    size_t arena_bytes_ = 0;
    Value root_;
    void* arena(size_t bytes);
    bool parse(size_t& pos, Value& out);
    std::vector<Value> refs_;
    std::vector<uint8_t> body_;
    const std::vector<uint8_t>* src_ = nullptr;
    bool has_bytes_ = false;
};
