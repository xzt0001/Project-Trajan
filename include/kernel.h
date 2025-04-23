#ifndef _KERNEL_H
#define _KERNEL_H

// Declare kernel_main with all attributes to ensure linkage
void kernel_main(void) __attribute__((used, externally_visible, noinline, section(".text.boot.main")));

#endif // _KERNEL_H 