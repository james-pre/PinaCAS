#pragma once

#ifndef COMPILE_PC

#include "../typeset.hxx"
#include "../work.hxx"

namespace viewer {

/*Measures b for draw*/
void measure(ts::Box *b);
/*Draws a measured box with its baseline at baseline*/
void draw(ts::Box *b, int x, int baseline);

/*Shows the steps in w full screen until the user leaves. Expects graphx to be running.*/
void show(work::Record *w, const char *title);

} // namespace viewer

#endif
