#ifndef TB_YAML_H
#define TB_YAML_H

#include <stddef.h>

#include "node.h"

Node* yaml_parse(const char* text, size_t len, char* err, size_t errlen);

#endif
