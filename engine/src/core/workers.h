#pragma once

namespace workers {

using Fn = void (*)();

bool start(bool (*launch)(Fn), Fn fn);
bool live();
void leave();
void stop();

}
