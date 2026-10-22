#include "value.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bytes.h"
#include "fs.h"

static const Value g_null;

const Value& Value::null_value() { return g_null; }

const Value& Value::operator[](size_t idx) const {
    if (t != Arr || idx >= n) return g_null;
    return a[idx];
}

const Value* Value::find(const char* key) const {
    if (t != Obj) return nullptr;
    uint32_t own = templated ? object_template(*this).own : n;
    for (uint32_t k = 0; k < own; k++) {
        if (std::strcmp(o[k].key, key) == 0) return &o[k].v;
    }
    return templated ? object_template(*this).base->find(key) : nullptr;
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
    if (bytes > block_hint_ / 2 && !blocks_.empty()) {
        blocks_.emplace(blocks_.end() - 1, new uint8_t[bytes]);
        arena_bytes_ += bytes;
        return blocks_[blocks_.size() - 2].get();
    }
    if (blocks_.empty() || block_used_ + bytes > block_size_) {
        size_t size = bytes > block_hint_ ? bytes : block_hint_;
        blocks_.emplace_back(new uint8_t[size]);
        block_size_ = size;
        block_used_ = 0;
        arena_bytes_ += size;
    }
    void* p = blocks_.back().get() + block_used_;
    block_used_ += bytes;
    return p;
}

static size_t block_hint(size_t body) { return std::min<size_t>(32768, std::max<size_t>(1024, (body * 4 + 7) & ~(size_t)7)); }

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
                *slot = bytes::le_double(&(*src_)[pos]);
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
            out.n = (uint32_t)v;
            if (copy_bytes_) {
                uint8_t* copy = (uint8_t*)arena(v ? (size_t)v : 1);
                std::memcpy(copy, &(*src_)[pos], (size_t)v);
                out.bytes = copy;
            } else {
                out.bytes = &(*src_)[pos];
            }
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
        case 12: {
            Value* base = (Value*)arena(sizeof(Value));
            new (base) Value();
            if (!parse(pos, *base) || base->t != Value::Obj || base->templated) return false;
            if (!read_varint((*src_), pos, v)) return false;
            Value* arr = v ? (Value*)arena(sizeof(Value) * v) : nullptr;
            for (uint64_t k = 0; k < v; k++) {
                new (&arr[k]) Value();
                if (pos < (*src_).size() && (*src_)[pos] == 13) {
                    pos++;
                    if (!parse_templated(pos, base, arr[k])) return false;
                } else if (!parse(pos, arr[k])) {
                    return false;
                }
            }
            out.t = Value::Arr;
            out.n = (uint32_t)v;
            out.a = arr;
            return true;
        }
    }
    return false;
}

bool Document::parse_templated(size_t& pos, const Value* base, Value& out) {
    uint64_t own = 0;
    if (!read_varint((*src_), pos, own) || own > base->n) return false;
    ObjectTemplate* head = (ObjectTemplate*)arena(sizeof(ObjectTemplate) + sizeof(Member) * own);
    head->base = base;
    head->own = (uint32_t)own;
    Member* mem = (Member*)(head + 1);
    uint32_t at = 0;
    for (uint64_t k = 0; k < own; k++) {
        uint64_t key = 0;
        if (!read_varint((*src_), pos, key) || key >= strings_.size()) return false;
        const char* name = strings_[key];
        while (at < base->n && base->o[at].key != name) at++;
        if (at == base->n) return false;
        at++;
        mem[k].key = name;
        new (&mem[k].v) Value();
        if (!parse(pos, mem[k].v)) return false;
    }
    out.t = Value::Obj;
    out.templated = 1;
    out.n = base->n;
    out.o = mem;
    return true;
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
    table_len = bytes::le32(&data_[8]);
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
    block_hint_ = block_hint(data_.size() - pos);
    copy_bytes_ = false;
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
    arena_bytes_ = 0;
    block_used_ = block_size_ = 0;
    root_ = Value();
    uint8_t head[12];
    bool ok = fs::data_read(f, head, 12) == 12 && std::memcmp(head, "VBIN", 4) == 0;
    uint32_t table_len = 0;
    if (ok) table_len = bytes::le32(&head[8]);
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
    block_hint_ = block_hint(body.size());
    copy_bytes_ = true;
    src_ = &body;
    size_t bpos = 0;
    ok = parse(bpos, root_);
    src_ = nullptr;
    copy_bytes_ = false;
    std::vector<Value>().swap(refs_);
    std::vector<const char*>().swap(strings_);
    return ok;
}
