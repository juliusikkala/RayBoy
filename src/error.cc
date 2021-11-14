#include "error.hh"
#include <cstdlib>
#include <cstdarg>
#include <cstdio>

void panic(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);

    abort();
}

void check_error(bool condition, const char* message, ...)
{
    if(!condition) return;

    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);

    abort();
}
