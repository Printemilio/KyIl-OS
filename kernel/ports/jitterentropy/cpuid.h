#ifndef KYIL_JENT_CPUID_H
#define KYIL_JENT_CPUID_H
#define __cpuid_count(leaf,subleaf,a,b,c,d) \
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(leaf),"c"(subleaf))
static inline unsigned __get_cpuid_max(unsigned base,unsigned *signature) {
    unsigned a,b,c,d;__cpuid_count(base,0,a,b,c,d);
    if(signature)*signature=b;return a;
}
#endif
