#include <string.h>
#include "appfile.h"

_Static_assert(APP_OLD_BASE + APP_OLD_RAM == PX_APP_BASE + PX_APP_RAM, "an old app's RAM is the top of a new one's");

uint32_t app_base(const px_app_header_t *h) {
    return h->api_version >= 9 ? PX_APP_BASE : APP_OLD_BASE;
}

uint32_t app_ram(const px_app_header_t *h) {
    return h->api_version >= 9 ? PX_APP_RAM : APP_OLD_RAM;
}

const char *app_header_check(const px_app_header_t *h, uint32_t file_size) {
    if (file_size < sizeof(*h) || h->magic != PX_APP_MAGIC)
        return "Not a PX-42F app";
    if (h->api_version == 0)
        return "Not a PX-42F app";
    if (h->api_version > PX_APP_API_VERSION)
        return "Needs a newer firmware";
    if (h->image_size != file_size)
        return "The file is damaged";
    uint32_t base = app_base(h), ram = app_ram(h);
    if (h->image_size > ram || h->bss_size > ram - h->image_size)
        return ram == PX_APP_RAM ? "Too big (160 KB at most)" : "Too big (128 KB at most)";
    uint32_t entry = h->entry & ~1u;
    if (!(h->entry & 1) || entry < base + sizeof(*h) || entry >= base + h->image_size)
        return "Not a PX-42F app";
    return NULL;
}

uint32_t app_crc32(uint32_t crc, const void *data, size_t n) {
    static const uint32_t table[16] = {
            0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
            0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    const uint8_t *p = data;
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = table[crc & 15] ^ (crc >> 4);
        crc = table[crc & 15] ^ (crc >> 4);
    }
    return ~crc;
}

bool app_icon_from(const uint8_t *head, size_t n, uint8_t bits[sizeof(((px_app_icon_t *) 0)->bits)]) {
    px_app_header_t h;
    px_app_icon_t icon;
    if (n < sizeof(h) + sizeof(icon))
        return false;
    memcpy(&h, head, sizeof(h));
    memcpy(&icon, head + sizeof(h), sizeof(icon));
    if (h.magic != PX_APP_MAGIC || icon.magic != PX_APP_ICON_MAGIC)
        return false;
    memcpy(bits, icon.bits, sizeof(icon.bits));
    return true;
}
