/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "module.h"

int main(int argc, char **argv)
{
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GError) error = NULL;
    const char *data;
    gsize size;

    if (argc != 3 || strcmp(argv[1], NIKON_MODULE_PROFILE) || !*argv[2]) {
        fprintf(stderr, "usage: %s %s OUTPUT\n", argv[0], NIKON_MODULE_PROFILE);
        return 2;
    }
    bytes = nikon_module_cold_neutral();
    data = g_bytes_get_data(bytes, &size);
    if (!g_file_set_contents(argv[2], data, size, &error)) {
        fprintf(stderr, "cannot write module input '%s': %s\n", argv[2],
                error->message);
        return 3;
    }
    return 0;
}
