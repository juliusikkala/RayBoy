#ifndef RAYBOY_ERROR_HH
#define RAYBOY_ERROR_HH

[[noreturn]] void panic(const char* message, ...);
void check_error(bool condition, const char* message, ...);

#endif

