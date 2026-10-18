#pragma once
#include <string>
#include <vector>

#include "value.h"

namespace js {

class Vm;

Val make_regexp(Vm& vm, const std::string& source, const std::string& flags);
bool is_regexp(const Val& v);
Val regexp_exec(Vm& vm, const Val& re, const std::string& s);
Val regexp_test(Vm& vm, const Val& re, const std::string& s);
Val string_match(Vm& vm, const std::string& s, const Val& re);
Val string_replace(Vm& vm, const std::string& s, const Val& pattern, const Val& replacement);
Val string_split_regex(Vm& vm, const std::string& s, const Val& re, long limit);
Val string_search(Vm& vm, const std::string& s, const Val& re);
void install_regexp(Vm& vm);
Val regexp_static(uint32_t key, bool& found);

}
