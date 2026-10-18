#include <unos/power.h>
#include <unos/scheduler.h>
#include <unos/security.h>
#include <unos/users.h>
#include <unos/smp.h>
#include <unos/sched_clock.h>
#include <unos/boot_console.h>
#include <unos/debug_log.h>
#include <unos/app_surface.h>
#include <unos/compat.h>
#include <unos/fs.h>
#include <unos/driver_manager.h>
#include <unos/netdev.h>
#include <unos/hardware_resources.h>
#include <unos/tty.h>
#include <unos/kernel_guard.h>
#include <uacpi/uacpi.h>
#include <uacpi/sleep.h>
static uint32_t power_tid;
static volatile uint8_t initialized,request_pending;
static unsigned operation;
static int acpi_ready,last_error;
static kernel_gate_t request_gate=KERNEL_GATE_INIT;
static void status(const char *text){debug_log_puts(text);tty_puts(text);}
void power_info(void){status(acpi_ready?"power: ACPI AML sleep/reset backend initialized\n":"power: ACPI backend unavailable\n");}
int power_request(unsigned op) {
    if((op!=POWER_OFF && op!=POWER_REBOOT) || !scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN) ||
        !user_has_capability(VEX_CAP_SYSTEM_ADMIN))return -1;
    if(!__atomic_load_n(&initialized,__ATOMIC_ACQUIRE) || !power_tid || (op==POWER_OFF && !acpi_ready))return -2;
    KERNEL_GUARD(request_gate);
    if(__atomic_load_n(&request_pending,__ATOMIC_ACQUIRE))return -3;
    operation=op;__atomic_store_n(&request_pending,1,__ATOMIC_RELEASE);process_wake(power_tid);return 0;
}
static unsigned active_users(int terminate) {
    unsigned count=0;uint64_t index=0;scheduler_process_info_t p;
    while(!scheduler_process_info_at(index++,&p)) {
        if(p.user_mode && p.state!=PROC_ZOMBIE) {
            ++count;if(terminate)process_kill_group(p.process_id,0);
        }
    }
    return count;
}
static void stop_busmaster(const hardware_pci_info_t *p,void *context) {
    (void)context;
    uint32_t command=hardware_pci_read32(p->bus,p->slot,p->function,4)&0xffff;
    if(command&4)hardware_pci_write32(p->bus,p->slot,p->function,4,command&~4u);
}
static int stop_native_network(void *context){(void)context;return netdev_native_suspend();}
static void shutdown_order_end(int *active){if(*active)scheduler_shutdown_order_begin(0);}
static void transition(void) {
    uint64_t close_errors=vex_fd_close_failures();
    status(operation==POWER_OFF?"power: closing applications for shutdown\n":"power: closing applications for reboot\n");
    int ordered __attribute__((cleanup(shutdown_order_end)))=1;scheduler_shutdown_order_begin(1);
    uint32_t pid,tid;uint64_t until;
    while((pid=scheduler_shutdown_next(&tid))!=0){
        if(!app_surface_request_close_owner(pid))vex_event_post(pid,tid,VEX_EVENT_TERMINATE);
        until=sched_clock_ns()+3000000000ull;
        while(scheduler_process_active(pid) && sched_clock_ns()<until)process_sleep_ticks(1);
        if(scheduler_process_active(pid)){
            process_kill_group(pid,0);until=sched_clock_ns()+3000000000ull;
            while(scheduler_process_active(pid) && sched_clock_ns()<until)process_sleep_ticks(1);
            if(scheduler_process_active(pid)){status("power: ordered process stop failed; transition cancelled\n");return;}
        }
    }
    if(vex_fd_sync_all()!=0){status("power: file flush failed; power transition cancelled\n");return;}
    (void)active_users(1);
    until=sched_clock_ns()+3000000000ull;
    while(active_users(0) && sched_clock_ns()<until)process_sleep_ticks(1);
    if(active_users(0)){status("power: user contexts did not stop; transition cancelled\n");return;}
    if(vex_fd_close_failures()!=close_errors || vex_fd_sync_all()!=0 || fs_sync()!=0){status("power: storage/last-close sync failed; transition cancelled\n");return;}
    fs_storage_info_t storage;
    if(!fs_get_storage_info(&storage) && !storage.disk_backed)
        status("power: RAM-only filesystem; user files are NOT persistent\n");
    if(operation==POWER_OFF) {
        uacpi_status rc=uacpi_enter_acpi_mode();
        if(!rc)rc=uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
        if(rc){last_error=rc;status("power: ACPI S5 preparation failed; transition cancelled\n");return;}
    }
    if(driver_manager_stop_all()!=0 || netdev_control_transaction(stop_native_network,NULL)!=0) {
        status("power: device quiescence failed; transition cancelled\n");return;
    }
    hardware_pci_enumerate(stop_busmaster,NULL);
    status("power: files synced, devices stopped; parking other CPUs\n");
    if(smp_stop_others(2000000000ull)!=0) {
        boot_console_fail("Power: CPUs did not acknowledge safe stop");
    }
    __asm__ volatile("cli;wbinvd":::"memory");
    if(operation==POWER_OFF) {
        last_error=uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
        boot_console_fail("Power: ACPI S5 returned without power-off");
    }
    last_error=uacpi_reboot();
    if(!last_error){uint64_t start=sched_clock_ns();while(sched_clock_ns()-start<500000000ull)smp_relax();}
    /* Standards-defined 8042 CPU reset fallback, not a hypervisor port. */
    uint64_t start=sched_clock_ns();uint8_t value;
    do{__asm__ volatile("inb $0x64,%0":"=a"(value));}while((value&2) && sched_clock_ns()-start<1000000000ull);
    if(!(value&2)){value=0xfe;__asm__ volatile("outb %0,$0x64"::"a"(value));}
    start=sched_clock_ns();while(sched_clock_ns()-start<1000000000ull)smp_relax();
    boot_console_fail("Power: firmware/8042 reset failed");
}
static void power_worker(void) {
    extern int acpi_host_start(void);
    uacpi_status rc=acpi_host_start()?UACPI_STATUS_OUT_OF_MEMORY:uacpi_initialize(UACPI_FLAG_NO_ACPI_MODE|UACPI_FLAG_BAD_CSUM_FATAL|UACPI_FLAG_BAD_TBL_SIGNATURE_FATAL);
    if(!rc)rc=uacpi_namespace_load();
    if(!rc)rc=uacpi_namespace_initialize();
    acpi_ready=rc==0;last_error=rc;
    boot_console_puts(acpi_ready?"  ACPI AML backend ready\n":"  ACPI AML backend unavailable: ");
    if(rc){boot_console_puts(uacpi_status_to_string(rc));boot_console_puts("\n");}
    debug_log_puts(acpi_ready?"power: ACPI AML ready (BIOS mode retained until S5)\n":"power: ACPI initialization failed\n");
    __atomic_store_n(&initialized,1,__ATOMIC_RELEASE);
    for(;;) {
        process_wait_event(&request_pending);
        if(__atomic_load_n(&request_pending,__ATOMIC_ACQUIRE)!=1)continue;
        transition();__atomic_store_n(&request_pending,0,__ATOMIC_RELEASE);
    }
}
void power_start(void) {
    power_tid=process_create_with_stack(power_worker,1,128u*1024u);
    if(!power_tid){debug_log_puts("power: worker allocation failed\n");return;}
    scheduler_set_process_name(power_tid,"power-manager");
    while(!__atomic_load_n(&initialized,__ATOMIC_ACQUIRE))process_yield_to(power_tid);
}
