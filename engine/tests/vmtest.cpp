#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/core/value.h"
#include "../src/js/vm.h"

extern "C" {
#include "js.h"
#include "node.h"
#include "util.h"
}

int main(int argc, char** argv) {
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) all.append(buf, n);
    std::vector<std::string> cases;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find("\n====\n", pos);
        if (e == std::string::npos) e = all.size();
        cases.push_back(all.substr(pos, e - pos));
        pos = e + 6;
    }
    js::Vm& vm = js::vm();
    std::vector<std::string> out;
    vm.set_global("print", js::make_native([&](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
        std::string s;
        for (size_t i = 0; i < a.size(); i++) {
            if (i) s += " ";
            s += a[i].is_obj() && !a[i].is_function() ? js::json_stringify(a[i]) : a[i].to_str();
        }
        out.push_back(s);
        return js::Val();
    }));
    for (auto& c : cases) {
        char err[256];
        Node* ast = js_compile(c.data(), c.size(), err, sizeof(err));
        if (!ast) {
            printf("COMPILE ERROR %s\n----\n", err);
            continue;
        }
        Buf b = {0};
        vbin_dump(ast, &b);
        node_free(ast);
        Document doc;
        std::vector<uint8_t> data(b.data, b.data + b.len);
        buf_free(&b);
        doc.load_memory(std::move(data));
        out.clear();
        js::Val result;
        auto scope = std::make_shared<js::Scope>();
        if (!vm.run(doc.root(), js::Val(), scope, &result)) out.push_back("THROW " + vm.last_error);
        for (auto& l : out) printf("%s\n", l.c_str());
        printf("----\n");
    }
    return 0;
}

#include <cstdarg>
#include <chrono>
namespace plat {
void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
double time_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
namespace fs {
bool read_data(const std::string&, std::vector<uint8_t>&) { return false; }
}
