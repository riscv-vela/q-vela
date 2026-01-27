#ifndef GEMMINI_MANAGER_H
#define GEMMINI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    GemminiState **instances;
    int *ids;
    int count;
    int capacity;
} GemminiRegistry;

void gemmini_registry_init(void);
void gemmini_registry_add(GemminiState *s, int id);
GemminiState *gemmini_registry_get(int id);
bool gemmini_registry_delete(int id);
uint32_t gemmini_registry_count(void);
//void gemmini_realize(DeviceState *dev, Error **errp);

// static bool gemmini_registry_delete(int id) {
//     for (int i = 0; i < gemmini_registry.count; i++) {
//         if (gemmini_registry.ids[i] == id) {
//             // Shift remaining elements
//             for (int j = i; j < gemmini_registry.count - 1; j++) {
//                 gemmini_registry.instances[j] = gemmini_registry.instances[j + 1];
//                 gemmini_registry.ids[j] = gemmini_registry.ids[j + 1];
//             }
//             gemmini_registry.count--;
//             qemu_log("Gemmini: Deleted instance with ID %d\n", id);
//             return true;
//         }
//     }
//     qemu_log("Gemmini: Warning - ID %d not found for deletion\n", id);
//     return false;
// }

#endif // GEMMINI_MANAGER_H


