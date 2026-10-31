// The native Switch 2 SDK can link JCMS clients into main rather than call the
// public nn::hid entry points. Serve those audited requests as actual resources.
// Protocol facts: https://switchbrew.org/wiki/HID_services#InitializeJcms .
// No frontend currently publishes Joy-Con 2 optical samples: its 16 rings are
// initialized with capacity 17 and count 0, rather than claiming a mouse exists.
#include "hle.h"
#include "hle_memory.h"
#include "hle_hid_service.h"
#include <pthread.h>
#include <time.h>

enum { HID_BAD_ARGUMENT = 0xd401, HID_NOT_INITIALIZED = 0xfa01, HID_ALREADY_INITIALIZED = 0xfa01 };
enum { JCMS_SIZE = 0x4000, JCMS_RINGS = 16, JCMS_STRIDE = 0x2c8 };
static struct { uint64_t aruid; dm_memory_object *memory; uint16_t started; } g_jcms[16];
static pthread_mutex_t g_hid_service_lock = PTHREAD_MUTEX_INITIALIZER;
// Applet resources belong to the application ARUID, rather than an individual
// client session. Closing an IAppletResource does not unregister its application
// or leave an already-mapped shared-memory view without its producer.
static struct { uint64_t aruid; dm_memory_object *memory; } g_hid_resources[16];
static pthread_once_t g_hid_publisher_once = PTHREAD_ONCE_INIT;
static void *hid_publish_thread(void *unused) {
    (void)unused;
    for (uint64_t sequence = 2;; ++sequence) {
        pthread_mutex_lock(&g_hid_service_lock);
        for (unsigned i = 0; i < 16; ++i) if (g_hid_resources[i].memory)
            dm_hid_write_shared(dm_memory_data(g_hid_resources[i].memory), sequence);
        pthread_mutex_unlock(&g_hid_service_lock);
        struct timespec delay = {0, 8000000}; nanosleep(&delay, NULL);
    }
    return NULL;
}
static void hid_start_publisher(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, hid_publish_thread, NULL)) die("HID shared-memory publisher");
    pthread_detach(thread);
}

uint32_t dm_hid_service_request(unsigned object, uint64_t resource_aruid, uint32_t command,
                               const void *arguments, size_t size,
                               const uint32_t *copies, unsigned copy_count,
                               dm_hid_reply *reply) {
    memset(reply, 0, sizeof *reply);
    if (command == 0 && (object == 0 || object == 1)) {
        uint64_t aruid = resource_aruid;
        if (object == 0) {
            if (size < 8 || copy_count) return HID_BAD_ARGUMENT;
            memcpy(&aruid, arguments, 8);
        } else if (copy_count) return HID_BAD_ARGUMENT;
        if (!aruid) return HID_BAD_ARGUMENT;
        pthread_mutex_lock(&g_hid_service_lock);
        unsigned i = 0; while (i < 16 && (!g_hid_resources[i].memory || g_hid_resources[i].aruid != aruid)) ++i;
        if (i == 16 && object == 0) {
            i = 0; while (i < 16 && g_hid_resources[i].memory) ++i;
            if (i < 16) {
                dm_memory_object *memory = dm_memory_create_shared(0x40000);
                if (memory) {
                    g_hid_resources[i].aruid = aruid; g_hid_resources[i].memory = memory;
                    dm_hid_write_shared(dm_memory_data(memory), 1);
                    pthread_once(&g_hid_publisher_once, hid_start_publisher);
                } else i = 16;
            }
        }
        uint32_t result = i == 16 ? HID_NOT_INITIALIZED : 0;
        if (!result) {
            if (object == 0) { reply->object = 1; reply->aruid = aruid; }
            else { reply->handle = dm_memory_copy_handle(g_hid_resources[i].memory); if (!reply->handle) result = 0xce01; }
        }
        pthread_mutex_unlock(&g_hid_service_lock);
        return result;
    }
    if (object || command < 3110 || command > 3113) {
        LOG("[RED] hid: unaudited object %u command %u refused", object, command);
        return RES_FAIL;
    }
    if (size < (command == 3111 ? 8u : 16u)) return HID_BAD_ARGUMENT;
    uint64_t aruid = 0, value = 0;
    memcpy(&aruid, arguments, 8);
    if (command != 3111) memcpy(&value, (const uint8_t *)arguments + 8, 8);
    if (!aruid || (command == 3110 ? copy_count != 1 || value != JCMS_SIZE : copy_count != 0)) return HID_BAD_ARGUMENT;
    if (command >= 3112 && ((value & ~UINT64_C(0x107)) || (value & 0xff) >= 8)) return HID_BAD_ARGUMENT;
    pthread_mutex_lock(&g_hid_service_lock);
    unsigned i = 0; while (i < 16 && (!g_jcms[i].memory || g_jcms[i].aruid != aruid)) ++i;
    uint32_t result = 0;
    if (command == 3110) {
        if (i != 16) result = HID_ALREADY_INITIALIZED;
        else {
            i = 0; while (i < 16 && g_jcms[i].memory) ++i;
            dm_memory_object *memory = i == 16 ? NULL : dm_memory_retain_transfer(copies[0], JCMS_SIZE, 1);
            if (!memory) result = HID_BAD_ARGUMENT;
            else {
                uint8_t *data = dm_memory_data(memory);
                dm_guest_write_prepare(data, JCMS_SIZE);
                memset(data, 0, JCMS_SIZE);
                // SDK's GetJcmsStates: 0x2c8 stride, header +8 capacity,
                // +0x10 tail, +0x18 count; 17 guarded 0x28-byte entries.
                for (unsigned k = 0; k < JCMS_RINGS; ++k) *(uint64_t *)(data + k * JCMS_STRIDE + 8) = 17;
                g_jcms[i].aruid = aruid; g_jcms[i].memory = memory; g_jcms[i].started = 0;
                LOG("hid: JCMS transfer resource initialized (%#x bytes, 16 optical devices absent)", JCMS_SIZE);
            }
        }
    } else if (i == 16) result = HID_NOT_INITIALIZED;
    else if (command == 3111) {
        dm_memory_release(g_jcms[i].memory); memset(&g_jcms[i], 0, sizeof g_jcms[i]);
    } else {
        unsigned index = (value & 7) * 2 + ((value >> 8) & 1);
        if (command == 3112) g_jcms[i].started |= 1u << index;
        else g_jcms[i].started &= ~(1u << index);
    }
    pthread_mutex_unlock(&g_hid_service_lock);
    return result;
}
