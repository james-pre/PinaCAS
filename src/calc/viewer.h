#pragma once

#ifndef COMPILE_PC

#include "../typeset.h"
#include "../work.h"

/*Measures b for viewer_Draw*/
void viewer_Measure(ts_box_t *b);
/*Draws a measured box with its baseline at baseline*/
void viewer_Draw(ts_box_t *b, int x, int baseline);

/*Shows the steps in w full screen until the user leaves. Expects graphx to be running.*/
void viewer_Show(pcas_work_t *w, const char *title);

#endif
