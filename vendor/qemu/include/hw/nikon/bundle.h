/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_BUNDLE_H
#define HW_NIKON_BUNDLE_H

#include "qapi/error.h"

typedef enum NikonComponent {
    NIKON_COMPONENT_EX,
    NIKON_COMPONENT_TPJ,
    NIKON_COMPONENT_MAS,
    NIKON_COMPONENT_VR,
    NIKON_COMPONENT_SLA,
    NIKON_COMPONENT_COUNT,
} NikonComponent;

typedef struct NikonBundle NikonBundle;

/* All validation completes before a component becomes visible to a machine. */
NikonBundle *nikon_bundle_parse(GBytes *container, Error **errp);
NikonBundle *nikon_bundle_load(const char *path, Error **errp);
void nikon_bundle_free(NikonBundle *bundle);
/* Returned immutable slice belongs to the caller. No CPU or RAM is changed. */
GBytes *nikon_bundle_component(NikonBundle *bundle, NikonComponent component);

/* Preserve record 0 separately; strip only the six-byte transport prefixes. */
GBytes *nikon_vr_body(GBytes *records, GBytes **metadata, Error **errp);

#endif
