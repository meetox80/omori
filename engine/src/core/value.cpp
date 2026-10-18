#include "value.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fs.h"

static const Value g_null;

const Value& Value::null_value() { return g_null; }

const Value& Value::operator[](size_t idx) const {
    if (t != Arr || idx >= n) return g_null;
    return a[idx];
}

const Value* Value::find(const char* key) const {
    if (t != Obj) return nullptr;
    for (uint32_t k = 0; k < n; k++) {
        if (std::strcmp(o[k].key, key) == 0) return &o[k].v;
    }
    return nullptr;
}

const Value& Value::operator[](const char* key) const {
    const Value* v = find(key);
    return v ? *v : g_null;
}

int Value::as_int(int def) const {
    switch (t) {
        case Int: return i;
        case Num: return (int)dbl();
        case Bool: return b ? 1 : 0;
        case Str: return std::atoi(s);
        default: return def;
    }
}

double Value::num(double def) const {
    switch (t) {
        case Int: return (double)i;
        case Num: return dbl();
        case Bool: return b ? 1.0 : 0.0;
        case Str: return std::atof(s);
        default: return def;
    }
}

bool Value::truthy() const {
    switch (t) {
        case Null: return false;
        case Bool: return b;
        case Int: return i != 0;
        case Num: return dbl() != 0.0;
        case Str: return n > 0;
        default: return true;
    }
}

const char* Value::str(const char* def) const { return t == Str ? s : def; }

void* Document::arena(size_t bytes) {
    bytes = (bytes + 7) & ~(size_t)7;
    if (blocks_.empty() || block_used_ + bytes > block_size_) {
        size_t size = bytes > 32768 ? bytes : 32768;
        blocks_.emplace_back(new uint8_t[size]);
        block_size_ = size;
        block_used_ = 0;
        arena_bytes_ += size;
    }
    void* p = blocks_.back().get() + block_used_;
    block_used_ += bytes;
    return p;
}

static bool read_varint(const std::vector<uint8_t>& d, size_t& pos, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (pos < d.size()) {
        uint8_t b = d[pos++];
        out |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

bool Document::parse(size_t& pos, Value& out) {
    if (pos >= (*src_).size()) return false;
    uint8_t tag = (*src_)[pos++];
    uint64_t v = 0;
    switch (tag) {
        case 0: out.t = Value::Null; return true;
        case 1: out.t = Value::Bool; out.b = false; return true;
        case 2: out.t = Value::Bool; out.b = true; return true;
        case 3:
            if (!read_varint((*src_), pos, v)) return false;
            out.t = Value::Int;
            out.i = (int32_t)((v >> 1) ^ (~(v & 1) + 1));
            return true;
        case 4:
            if (pos + 8 > (*src_).size()) return false;
            out.t = Value::Num;
            {
                double* slot = (double*)arena(sizeof(double));
                std::memcpy(slot, &(*src_)[pos], 8);
                out.dp = slot;
            }
            pos += 8;
            return true;
        case 5:
            if (!read_varint((*src_), pos, v) || v >= strings_.size()) return false;
            out.t = Value::Str;
            out.s = strings_[v];
            out.n = (uint32_t)std::strlen(out.s);
            return true;
        case 8:
            if (!read_varint((*src_), pos, v) || pos + v > (*src_).size()) return false;
            out.t = Value::Bytes;
            has_bytes_ = true;
            out.n = (uint32_t)v;
            out.bytes = &(*src_)[pos];
            pos += v;
            return true;
        case 6:
        case 10: {
            bool shared = tag == 10;
            if (!read_varint((*src_), pos, v)) return false;
            Value* arr = v ? (Value*)arena(sizeof(Value) * v) : nullptr;
            for (uint64_t k = 0; k < v; k++) {
                new (&arr[k]) Value();
                if (!parse(pos, arr[k])) return false;
            }
            out.t = Value::Arr;
            out.n = (uint32_t)v;
            out.a = arr;
            if (shared) refs_.push_back(out);
            return true;
        }
        case 7:
        case 11: {
            bool shared = tag == 11;
            if (!read_varint((*src_), pos, v)) return false;
            Member* mem = v ? (Member*)arena(sizeof(Member) * v) : nullptr;
            for (uint64_t k = 0; k < v; k++) {
                uint64_t key = 0;
                if (!read_varint((*src_), pos, key) || key >= strings_.size()) return false;
                mem[k].key = strings_[key];
                new (&mem[k].v) Value();
                if (!parse(pos, mem[k].v)) return false;
            }
            out.t = Value::Obj;
            out.n = (uint32_t)v;
            out.o = mem;
            if (shared) refs_.push_back(out);
            return true;
        }
        case 9:
            if (!read_varint((*src_), pos, v) || v >= refs_.size()) return false;
            out = refs_[v];
            return true;
    }
    return false;
}

bool Document::load_memory(std::vector<uint8_t>&& data) {
    data_ = std::move(data);
    blocks_.clear();
    strings_.clear();
    arena_bytes_ = 0;
    block_used_ = block_size_ = 0;
    root_ = Value();
    if (data_.size() < 12 || std::memcmp(data_.data(), "VBIN", 4) != 0) return false;
    uint32_t table_len = 0;
    std::memcpy(&table_len, &data_[8], 4);
    size_t pos = 12;
    uint64_t count = 0;
    if (!read_varint(data_, pos, count)) return false;
    strings_.reserve(count);
    for (uint64_t k = 0; k < count; k++) {
        uint64_t len = 0;
        if (!read_varint(data_, pos, len) || pos + len + 1 > data_.size()) return false;
        strings_.push_back((const char*)&data_[pos]);
        pos += len + 1;
    }
    if (pos != 12 + table_len) return false;
    refs_.clear();
    src_ = &data_;
    bool ok = parse(pos, root_);
    src_ = nullptr;
    std::vector<Value>().swap(refs_);
    std::vector<const char*>().swap(strings_);
    return ok;
}

bool Document::load_data(const std::string& rel) {
    fs::DataFile* f = fs::open_data(rel);
    if (!f) return false;
    blocks_.clear();
    strings_.clear();
    body_.clear();
    arena_bytes_ = 0;
    block_used_ = block_size_ = 0;
    root_ = Value();
    uint8_t head[12];
    bool ok = fs::data_read(f, head, 12) == 12 && std::memcmp(head, "VBIN", 4) == 0;
    uint32_t table_len = 0;
    if (ok) std::memcpy(&table_len, &head[8], 4);
    if (ok) {
        data_.assign(head, head + 12);
        data_.resize(12 + (size_t)table_len);
        ok = fs::data_read(f, data_.data() + 12, table_len) == table_len;
    }
    std::vector<uint8_t> body;
    if (ok) {
        int64_t start = fs::data_tell(f);
        fs::data_seek(f, 0, SEEK_END);
        int64_t end = fs::data_tell(f);
        fs::data_seek(f, start, SEEK_SET);
        if (end > start) {
            body.resize((size_t)(end - start));
            ok = fs::data_read(f, body.data(), body.size()) == body.size();
        }
    }
    fs::data_close(f);
    if (!ok) return false;
    size_t pos = 12;
    uint64_t count = 0;
    if (!read_varint(data_, pos, count)) return false;
    strings_.reserve(count);
    for (uint64_t k = 0; k < count; k++) {
        uint64_t len = 0;
        if (!read_varint(data_, pos, len) || pos + len + 1 > data_.size()) return false;
        strings_.push_back((const char*)&data_[pos]);
        pos += len + 1;
    }
    if (pos != 12 + table_len) return false;
    refs_.clear();
    has_bytes_ = false;
    src_ = &body;
    size_t bpos = 0;
    ok = parse(bpos, root_);
    src_ = nullptr;
    std::vector<Value>().swap(refs_);
    std::vector<const char*>().swap(strings_);
    if (has_bytes_) body_ = std::move(body);
    return ok;
}
