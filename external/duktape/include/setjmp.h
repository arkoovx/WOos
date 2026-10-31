#ifndef _SETJMP_H_
#define _SETJMP_H_
typedef int jmp_buf[16];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
