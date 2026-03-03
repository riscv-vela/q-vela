#include <stdint.h>

uintptr_t __stack_chk_guard = 0xdeadbeefUL;

__attribute__((noreturn))
void __stack_chk_fail(void)
{
  while (1) { }
}

