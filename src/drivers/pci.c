/* ============================================================================
 * src/drivers/pci.c  -  PCI configuration space enumeration
 * ============================================================================ */

#include "kernel.h"
#include "pci.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 address = 0x80000000u
                | ((u32)bus << 16)
                | ((u32)slot << 11)
                | ((u32)func << 8)
                | (offset & 0xFCu);
    port_outl(PCI_CONFIG_ADDR, address);
    return port_inl(PCI_CONFIG_DATA);
}

u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 v = pci_read32(bus, slot, func, (u8)(offset & 0xFC));
    return (u16)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 v = pci_read32(bus, slot, func, (u8)(offset & 0xFC));
    return (u8)((v >> ((offset & 3) * 8)) & 0xFF);
}

int pci_scan(pci_device_t *out, int max)
{
    if (!out || max <= 0) return 0;
    int count = 0;

    for (int bus = 0; bus < 8 && count < max; bus++) {
        for (int slot = 0; slot < 32 && count < max; slot++) {
            for (int func = 0; func < 8 && count < max; func++) {
                u16 vendor = pci_read16((u8)bus, (u8)slot, (u8)func, 0x00);
                if (vendor == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }

                u16 device = pci_read16((u8)bus, (u8)slot, (u8)func, 0x02);
                u32 classreg = pci_read32((u8)bus, (u8)slot, (u8)func, 0x08);
                u8 header = pci_read8((u8)bus, (u8)slot, (u8)func, 0x0E);

                out[count].bus = (u8)bus;
                out[count].slot = (u8)slot;
                out[count].func = (u8)func;
                out[count].revision = (u8)(classreg & 0xFF);
                out[count].vendor = vendor;
                out[count].device = device;
                out[count].prog_if = (u8)((classreg >> 8) & 0xFF);
                out[count].subclass = (u8)((classreg >> 16) & 0xFF);
                out[count].class_code = (u8)((classreg >> 24) & 0xFF);
                out[count].header_type = header;
                count++;

                if (func == 0 && !(header & 0x80)) break;
            }
        }
    }
    return count;
}

const char *pci_class_name(u8 class_code, u8 subclass)
{
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01:
        switch (subclass) {
        case 0x01: return "IDE controller";
        case 0x06: return "SATA controller";
        case 0x08: return "NVMe controller";
        default:   return "Mass storage";
        }
    case 0x02:
        return (subclass == 0x00) ? "Ethernet" : "Network";
    case 0x03:
        return (subclass == 0x00) ? "VGA display" : "Display";
    case 0x04: return "Multimedia";
    case 0x05: return "Memory controller";
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host bridge";
        case 0x01: return "ISA bridge";
        case 0x04: return "PCI bridge";
        default:   return "Bridge";
        }
    case 0x07: return "Communication";
    case 0x08: return "System peripheral";
    case 0x09: return "Input device";
    case 0x0A: return "Docking station";
    case 0x0B: return "Processor";
    case 0x0C:
        switch (subclass) {
        case 0x03: return "USB controller";
        case 0x05: return "SMBus controller";
        default:   return "Serial bus";
        }
    case 0x0D: return "Wireless";
    case 0x0E: return "Intelligent I/O";
    case 0x0F: return "Satellite";
    case 0x10: return "Encryption";
    case 0x11: return "Signal processing";
    default:   return "Unknown";
    }
}
