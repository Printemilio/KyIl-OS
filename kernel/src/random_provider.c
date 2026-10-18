#include <unos/random.h>
#include <unos/kernel_guard.h>
#include <unos/smp.h>
#include <unos/debug_log.h>
#include <unos/boot_console.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>
#include "jitterentropy.h"
#include <string.h>
#include <stdio.h>

/* Trusted bootstrap providers: archives never acquire arbitrary CPU/MSR access.
 * This single service supplies capabilities, AT_RANDOM, VSL and TLS. */
enum { RNG_RDRAND = 1u, RNG_RDSEED = 2u };
static kernel_gate_t random_gate = KERNEL_GATE_INIT;
static mbedtls_ctr_drbg_context drbg;
static struct rand_data *jitter;
static int ready;
static const char *provider = "uninitialized";
void kernel_random_cpu_init(void) {
    uint32_t a,b,c,d,maximum,flags=0;
    __asm__ volatile("cpuid":"=a"(maximum),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0));
    if(maximum>=1u) {
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
        if(c&(1u<<30u))flags|=RNG_RDRAND;
    }
    if(maximum>=7u) {
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(7),"c"(0));
        if(b&(1u<<18u))flags|=RNG_RDSEED;
    }
    vex_cpu_t *cpu=smp_current_cpu();if(cpu)cpu->random_capabilities=flags;
}
static int hardware_word(uint64_t *value) {
    vex_cpu_t *cpu=smp_current_cpu();uint32_t flags=cpu?cpu->random_capabilities:0u;
    for(unsigned retry=0;retry<64u;++retry) {
        unsigned char ok=0;
        if(flags&RNG_RDSEED)
            __asm__ volatile("rdseed %0; setc %1":"=r"(*value),"=qm"(ok)::"cc");
        else if(flags&RNG_RDRAND)
            __asm__ volatile("rdrand %0; setc %1":"=r"(*value),"=qm"(ok)::"cc");
        else return -1;
        if(ok)return 0;__asm__ volatile("pause");
    }
    return -1;
}
static int entropy(void *context,unsigned char *output,size_t length) {
    (void)context;size_t position=0;
    while(position<length) {
        uint64_t word;if(hardware_word(&word))break;
        size_t take=length-position;if(take>sizeof(word))take=sizeof(word);
        memcpy(output+position,&word,take);mbedtls_platform_zeroize(&word,sizeof(word));position+=take;
    }
    if(position==length)return 0;
    /* Upstream noise collection + SHA3 + startup/continuous RCT/APT tests.
     * Neither a clock value nor a bypass of a health failure is a seed. */
    if(!jitter) {
        boot_console_stage("RNG: jitter startup health tests",0);
        int error=jent_entropy_init();
        if(error) {
            char line[80];snprintf(line,sizeof(line),"rng: jitter startup rejected (%d)\n",error);
            debug_log_puts(line);boot_console_puts(line);return -1;
        }
        boot_console_stage("RNG: jitter health tests",1);
        boot_console_stage("RNG: allocate noise collector",0);
        jitter=jent_entropy_collector_alloc(0u,0u);if(!jitter)return -1;
        boot_console_stage("RNG: noise collector ready",1);
    }
    boot_console_stage("RNG: collecting seed entropy",0);
    if(jent_read_entropy(jitter,(char *)output+position,length-position)!=(ssize_t)(length-position))return -1;
    boot_console_stage("RNG: seed entropy collected",1);
    provider="jitter/SHA3 + CTR-DRBG";return 0;
}
int kernel_random_init(void) {
    KERNEL_GUARD(random_gate);
    if(ready)return 0;
    kernel_random_cpu_init();vex_cpu_t *cpu=smp_current_cpu();
    provider=cpu && cpu->random_capabilities&RNG_RDSEED ? "RDSEED + CTR-DRBG" :
             cpu && cpu->random_capabilities&RNG_RDRAND ? "RDRAND + CTR-DRBG" : "jitter/SHA3 + CTR-DRBG";
    mbedtls_ctr_drbg_init(&drbg);
    static const unsigned char label[]="KyIlOS system RNG v1";
    if(mbedtls_ctr_drbg_seed(&drbg,entropy,NULL,label,sizeof(label)-1u)) {
        mbedtls_ctr_drbg_free(&drbg);
        if(jitter){jent_entropy_collector_free(jitter);jitter=NULL;}
        provider="unavailable";return -1;
    }
    ready=1;debug_log_puts("rng: ready provider=");debug_log_puts(provider);debug_log_puts("\n");return 0;
}
const char *kernel_random_provider(void) {return provider;}
int kernel_random_try_fill(void *buffer,size_t length) {
    kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter_irq_safe(&random_gate);
    if((!buffer && length)||!ready)return -1;
    unsigned char *bytes=buffer;
    size_t total=length;
    while(length) {
        size_t take=length>MBEDTLS_CTR_DRBG_MAX_REQUEST?MBEDTLS_CTR_DRBG_MAX_REQUEST:length;
        if(mbedtls_ctr_drbg_random(&drbg,bytes,take)) {
            mbedtls_platform_zeroize(buffer,total);return -1;
        }
        bytes+=take;length-=take;
    }
    return 0;
}
