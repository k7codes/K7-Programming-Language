#pragma once

#include "k7/interp.h"

namespace k7 {

void registerMathModule(Interp& I);
void registerTimeModule(Interp& I);
void registerJsonModule(Interp& I);
void registerFsModule(Interp& I);
void registerRandomModule(Interp& I);
void registerOsModule(Interp& I);
void registerStrModule(Interp& I);
void registerRegexModule(Interp& I);

} // namespace k7