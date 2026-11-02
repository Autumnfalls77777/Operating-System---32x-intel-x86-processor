/* ============================================================================
 * include/pci.h  -  PCI configuration-space enumerator
 * ============================================================================ */

#ifndef NOVA_PCI_H
#define NOVA_PCI_H

#include "types.h"

#define PCI_MAX_DEVICES 32

typedef struct {
    u8  bus;
    u8  slot;
    u8  func;
    u8  revision;
    u16 vendor;
    u16 device;
    u8  class_code;
    u8  subclass;
    u8  prog_if;
    u8  header_type;
} pci_device_t;

u32  pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
u16  pci_read16(u8 bus, u8 slot, u8 func, u8 offset);
u8   pci_read8(u8 bus, u8 slot, u8 func, u8 offset);
int  pci_scan(pci_device_t *out, int max);
const char *pci_class_name(u8 class_code, u8 subclass);

#endif /* NOVA_PCI_H */
