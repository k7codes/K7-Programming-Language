#include "k7/interp.h"
#include "k7/modules.h"

namespace k7 {

void Interp::registerModules() {
    registerMathModule(*this);
    registerTimeModule(*this);
    registerJsonModule(*this);
    registerFsModule(*this);
    registerRandomModule(*this);
    registerOsModule(*this);
    registerStrModule(*this);
    registerRegexModule(*this);
}

} // namespace k7