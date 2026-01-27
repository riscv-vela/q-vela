#include "qemu/osdep.h"
#include "qemu/log.h"
#include "gemmini.h"
#include "gemmini_manager.h"

GemminiRegistry gemmini_registry = {NULL, NULL, 0, 0};

void gemmini_registry_init(void) {
    if (gemmini_registry.instances == NULL) {
        gemmini_registry.capacity = 8;
        gemmini_registry.instances = g_malloc(sizeof(GemminiState *) * gemmini_registry.capacity);
        gemmini_registry.ids = g_malloc(sizeof(int) * gemmini_registry.capacity);
        gemmini_registry.count = 0;
    }
}

void gemmini_registry_add(GemminiState *s, int id) {
    gemmini_registry_init();
    
    // Check if ID already exists
    for (int i = 0; i < gemmini_registry.count; i++) {
        if (gemmini_registry.ids[i] == id) {
            qemu_log("Gemmini: Warning - ID %d already exists, replacing\n", id);
            gemmini_registry.instances[i] = s;
            return;
        }
    }
    
    // Expand if needed
    if (gemmini_registry.count >= gemmini_registry.capacity) {
        gemmini_registry.capacity *= 2;
        gemmini_registry.instances = g_realloc(gemmini_registry.instances, 
                                               sizeof(GemminiState *) * gemmini_registry.capacity);
        gemmini_registry.ids = g_realloc(gemmini_registry.ids, 
                                         sizeof(int) * gemmini_registry.capacity);
    }
    
    gemmini_registry.instances[gemmini_registry.count] = s;
    gemmini_registry.ids[gemmini_registry.count] = id;
    gemmini_registry.count++;
    
    qemu_log("Gemmini: Added instance with ID %d (total: %d)\n", id, gemmini_registry.count);
}

bool gemmini_registry_delete(int id) {
    for (int i = 0; i < gemmini_registry.count; i++) {
        if (gemmini_registry.ids[i] == id) {
            // Shift remaining elements
            for (int j = i; j < gemmini_registry.count - 1; j++) {
                gemmini_registry.instances[j] = gemmini_registry.instances[j + 1];
                gemmini_registry.ids[j] = gemmini_registry.ids[j + 1];
            }
            gemmini_registry.count--;
            qemu_log("Gemmini: Deleted instance with ID %d\n", id);
            return true;
        }
    }
    qemu_log("Gemmini: Warning - ID %d not found for deletion\n", id);
    return false;
}

uint32_t gemmini_registry_count(void) {
    return gemmini_registry.count;
}

GemminiState *gemmini_registry_get(int id) {
    for (int i = 0; i < gemmini_registry.count; i++) {
        if (gemmini_registry.ids[i] == id) {
            return gemmini_registry.instances[i];
        }
    }
    return NULL;
}