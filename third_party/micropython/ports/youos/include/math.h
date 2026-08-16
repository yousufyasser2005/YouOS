#pragma once
// Declarations only, never defined. Confirmed by inspecting every
// #include <math.h> site in py/ core: all but one are behind
// "#if MICROPY_PY_BUILTINS_FLOAT" / "#if MICROPY_FLOAT_IMPL != ...NONE" /
// "#if MICROPY_PY_BUILTINS_COMPLEX" guards, all false for this int-only
// build (constraint #1, no FPU/SSE on YouOS). The one exception,
// py/emitcommon.c, includes math.h unconditionally but never calls a
// single math function (grep confirmed) -- it just needs the header to
// parse. If the linker ever reports an undefined reference to one of
// these, that assumption was wrong for that symbol -- investigate rather
// than add a body.
double floor(double x);
double ceil(double x);
double sqrt(double x);
double pow(double x, double y);
double fabs(double x);
double log(double x);
double log2(double x);
double log10(double x);
double exp(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double fmod(double x, double y);
double trunc(double x);
double round(double x);
double ldexp(double x, int exp);
double frexp(double x, int *exp);
double modf(double x, double *iptr);
double copysign(double x, double y);
int isnan(double x);
int isinf(double x);
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
