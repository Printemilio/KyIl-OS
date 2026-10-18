#include <unos/network_state.h>
#include <unos/kernel_guard.h>
static kernel_gate_t network_gate=KERNEL_GATE_INIT;
uint64_t network_state_lock(void) {
    kernel_guard_t guard=kernel_guard_enter(&network_gate);return guard.flags;
}
void network_state_unlock(uint64_t flags) {
    kernel_guard_t guard={&network_gate,flags};kernel_guard_leave(&guard);
}
