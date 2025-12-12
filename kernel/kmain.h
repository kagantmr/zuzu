#ifndef KMAIN_H
#define KMAIN_H

void kmain(void);


static inline uint32_t cpu_mode_from_cpsr(uint32_t cpsr) {
    return cpsr & 0x1F;
}

static inline const char* cpu_mode_str(uint32_t mode) {
    switch (mode) {
        case 0x10: return "User";
        case 0x11: return "FIQ";
        case 0x12: return "IRQ";
        case 0x13: return "Supervisor";
        case 0x17: return "Abort";
        case 0x1B: return "Undefined";
        case 0x1F: return "System";
        default:   return "Unknown";
    }
}

#endif