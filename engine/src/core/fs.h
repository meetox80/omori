#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fs {

bool read_file(const std::string& path, std::vector<uint8_t>& out);
bool write_file(const std::string& path, const void* data, size_t size);
bool exists(const std::string& path);
bool remove_file(const std::string& path);
std::string save_path(const std::string& rel);

bool has_pack(const std::string& path);
bool init_data(const std::string& source);
bool data_packed();
bool read_data(const std::string& rel, std::vector<uint8_t>& out);
bool data_exists(const std::string& rel);

struct DataFile;
DataFile* open_data(const std::string& rel);
DataFile* open_data_memory(const std::string& rel, size_t max_bytes);
void drop_idle_handles();
size_t data_read(DataFile* f, void* dst, size_t size);
int data_seek(DataFile* f, int64_t off, int whence);
int64_t data_size(DataFile* f);
int64_t data_tell(DataFile* f);
void data_close(DataFile* f);

}
