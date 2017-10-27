#ifndef IXY_PCI_H
#define IXY_PCI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t* pci_map_resource(const char* bus_id);

#ifdef __cplusplus
}
#endif

#endif //IXY_PCI_H
