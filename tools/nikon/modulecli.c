/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "module.h"

int main(int argc, char **argv)
{
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GBytes) source = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *input = NULL;
    const char *output;
    const char *data;
    gsize size;

    if (argc == 3 && !strcmp(argv[1], NIKON_MODULE_PROFILE) && *argv[2]) {
        bytes = nikon_module_cold_neutral();
        output = argv[2];
    } else if (argc == 4 && !strcmp(argv[1], NIKON_MODULE_REGIONS_PROFILE) &&
               *argv[2] && *argv[3]) {
        if (!g_file_get_contents(argv[2], &input, &size, &error)) {
            fprintf(stderr, "cannot read descriptor17 input '%s': %s\n",
                    argv[2], error->message);
            return 3;
        }
        source = g_bytes_new_take(g_steal_pointer(&input), size);
        bytes = nikon_module_neutral_regions(source, &error);
        if (!bytes) {
            fprintf(stderr, "invalid descriptor17 input '%s': %s\n",
                    argv[2], error->message);
            return 3;
        }
        output = argv[3];
    } else {
        fprintf(stderr, "usage: %s %s OUTPUT\n"
                "       %s %s INPUT_DESCRIPTOR17 OUTPUT\n",
                argv[0], NIKON_MODULE_PROFILE,
                argv[0], NIKON_MODULE_REGIONS_PROFILE);
        return 2;
    }
    data = g_bytes_get_data(bytes, &size);
    if (!g_file_set_contents(output, data, size, &error)) {
        fprintf(stderr, "cannot write module input '%s': %s\n", output,
                error->message);
        return 3;
    }
    return 0;
}
