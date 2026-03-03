#include <stdint.h>

// Provide the symbols required by -fstack-protector builds when linking -nostdlib.
uintptr_t __stack_chk_guard = 0xdeadbeefUL;

__attribute__((noreturn))
void __stack_chk_fail(void)
{
  // In a freestanding runtime, just trap/hang.
  while (1) { }
}

