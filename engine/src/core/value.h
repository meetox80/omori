#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Member;

struct Value {
    enum Type : uint8_t { Null, Bool, Int, Num, Str, Arr, Obj, Bytes };
    Type t : 4;
    uint32_t templated : 1;
    uint32_t n : 27;
    union {
        bool b;
        int32_t i;
        const double* dp;
        const char* s;
        const Value* a;
        const Member* o;
        const uint8_t* bytes;
    };
    Value() : t(Null), templated(0), n(0), dp(nullptr) {}
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
    class Members members() const;
    static const Value& null_value();
};

struct Member {
    const char* key;
    Value v;
};

struct ObjectTemplate {
    const Value* base;
    uint32_t own;
};

inline const ObjectTemplate& object_template(const Value& v) { return reinterpret_cast<const ObjectTemplate*>(v.o)[-1]; }

class MemberIterator {
public:
    MemberIterator(const Member* own, uint32_t own_n, const Member* base, uint32_t at, uint32_t n) : own_(own), base_(base), own_n_(own_n), at_(at), n_(n) { settle(); }
    const Member& operator*() const { return *cur_; }
    const Member* operator->() const { return cur_; }
    MemberIterator& operator++() {
        if (k_ < own_n_ && cur_ == &own_[k_]) k_++;
        at_++;
        settle();
        return *this;
    }
    bool operator!=(const MemberIterator& o) const { return at_ != o.at_; }

private:
    const Member* own_;
    const Member* base_;
    uint32_t own_n_;
    uint32_t at_;
    uint32_t n_;
    uint32_t k_ = 0;
    const Member* cur_ = nullptr;
    void settle() {
        if (at_ >= n_) return;
        if (!base_) cur_ = own_ + at_;
        else cur_ = k_ < own_n_ && own_[k_].key == base_[at_].key ? &own_[k_] : &base_[at_];
    }
};

class Members {
public:
    explicit Members(const Value& v) : v_(v) {}
    MemberIterator begin() const {
        if (v_.t != Value::Obj || !v_.n) return end();
        if (!v_.templated) return MemberIterator(v_.o, v_.n, nullptr, 0, v_.n);
        const ObjectTemplate& t = object_template(v_);
        return MemberIterator(v_.o, t.own, t.base->o, 0, v_.n);
    }
    MemberIterator end() const {
        uint32_t n = v_.t == Value::Obj ? v_.n : 0;
        return MemberIterator(nullptr, 0, nullptr, n, n);
    }

private:
    const Value& v_;
};

inline Members Value::members() const { return Members(*this); }

static_assert(sizeof(Value) == 4 + sizeof(void*) || sizeof(void*) > 4, "Value packs into 8 bytes on 32-bit consoles");

class Document {
public:
    bool load_data(const std::string& rel);
    bool load_memory(std::vector<uint8_t>&& data);
    const Value& root() const { return root_; }
    size_t memory() const { return data_.size() + arena_bytes_; }

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
    bool parse_templated(size_t& pos, const Value* base, Value& out);
    std::vector<Value> refs_;
    const std::vector<uint8_t>* src_ = nullptr;
    size_t block_hint_ = 32768;
    bool copy_bytes_ = false;
};
