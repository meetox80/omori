#pragma once
#include <memory>
#include <string>

#include "../core/value.h"
#include "value.h"

struct Node;

namespace js {

std::shared_ptr<Document> program_doc(Node* program);
std::shared_ptr<Document> compile_doc(const std::string& src, std::string& err);
std::shared_ptr<const ::Value> compile_program(const std::string& src, std::string& err);
std::shared_ptr<const ::Value> compile_cached(const std::string& src);
void install_runtime_eval();
Node* val_to_node(const Val& v);
Node* value_to_node(const ::Value& v);
std::shared_ptr<Document> route_doc(const Val& route);
std::shared_ptr<Document> list_doc(const Val& list, const char* where);

}
