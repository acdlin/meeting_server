#pragma once
#include <signal.h>

typedef  void Sigfunc(int);


Sigfunc* Signal(int signo , Sigfunc* func);
