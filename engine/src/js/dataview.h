#pragma once
#include <memory>

#include "../core/value.h"
#include "value.h"

namespace js {

Val data_view(const ::Value& v, std::shared_ptr<const void> keep, bool permanent = false);
const ::Value* data_of(const Val& v);
Val value_to_js(const ::Value& v);

}
