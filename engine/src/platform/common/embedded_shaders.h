#pragma once

struct EmbeddedShader {
    const char* name;
    const unsigned char* data;
    unsigned size;
};

const EmbeddedShader* embedded_shader(const char* name);
