#ifndef KIRILLOS_KPANIC_H
#define KIRILLOS_KPANIC_H

/* Kirill Panic: IDT for every CPU exception and a detailed crash screen */

#define KPANIC_SOFT_VECTOR 0x4B   /* 'K': raised by kirill_panic() */

void kpanic_init(void);
void kirill_panic(const char* message) __attribute__((noreturn));

/* Shell 'panic' command: "" = software panic, or "div", "ud", "gp", "int3" */
void kpanic_test(const char* kind);

#endif
