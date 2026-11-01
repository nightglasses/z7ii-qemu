# Finite PS0 Power Management for the pinned native NVMe controller.
# NPSS remains zero; this models an always-operational endpoint, not camera power.
function(nvme_replace variable before after)
  string(LENGTH "${${variable}}" original)
  string(LENGTH "${before}" expected)
  string(REPLACE "${before}" "" without "${${variable}}")
  string(LENGTH "${without}" remaining)
  math(EXPR removed "${original} - ${remaining}")
  if(NOT removed EQUAL expected)
    message(FATAL_ERROR "Fixed upstream NVMe source does not match the unique PS0 seam")
  endif()
  string(REPLACE "${before}" "${after}" updated "${${variable}}")
  set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

file(READ "${QEMU_UPSTREAM}/hw/nvme/nvme.h" content)
nvme_replace(content [=[        uint32_t                async_config;]=]
  [=[        uint32_t                power_management;
        uint32_t                async_config;]=])
native_file("hw/nvme/nvme.h" "${content}")

file(READ "${QEMU_UPSTREAM}/hw/nvme/ctrl.c" content)
nvme_replace(content [=[static const uint32_t nvme_feature_cap[NVME_FID_MAX] = {]=]
  [=[static const uint32_t nvme_feature_cap[NVME_FID_MAX] = {
    [NVME_POWER_MANAGEMENT]         = NVME_FEAT_CAP_CHANGE,]=])
nvme_replace(content [=[    switch (fid) {
    case NVME_TEMPERATURE_THRESHOLD:
        result = 0;

        /*]=]
  [=[    switch (fid) {
    case NVME_POWER_MANAGEMENT:
        result = n->features.power_management;
        goto out;
    case NVME_TEMPERATURE_THRESHOLD:
        result = 0;

        /*]=])
nvme_replace(content [=[    switch (fid) {
    case NVME_TEMPERATURE_THRESHOLD:
        if (NVME_TEMP_TMPSEL(dw11) != NVME_TEMP_TMPSEL_COMPOSITE) {]=]
  [=[    switch (fid) {
    case NVME_POWER_MANAGEMENT:
        /* The native endpoint advertises NPSS=0. Legal workload hints are
         * retained, without inventing physical power or transition latency.
         */
        if ((dw11 & ~0xffu) || (dw11 & 0x1f) > n->id_ctrl.npss ||
            ((dw11 >> 5) & 7) > 2) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
        n->features.power_management = dw11;
        break;
    case NVME_TEMPERATURE_THRESHOLD:
        if (NVME_TEMP_TMPSEL(dw11) != NVME_TEMP_TMPSEL_COMPOSITE) {]=])
nvme_replace(content [=[static const VMStateDescription nvme_vmstate = {]=]
  [=[static bool nvme_power_management_needed(void *opaque)
{
    NvmeCtrl *n = opaque;

    return n->features.power_management != 0;
}

static const VMStateDescription nvme_vmstate_power_management = {
    .name = "nvme/power-management",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = nvme_power_management_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(features.power_management, NvmeCtrl),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription nvme_vmstate = {]=])
nvme_replace(content [=[    n->qs_created = false;

    n->dn = n->params.atomic_dn;]=]
  [=[    n->qs_created = false;
    n->features.power_management = 0;

    n->dn = n->params.atomic_dn;]=])
nvme_replace(content [=[        VMSTATE_STRUCT(atomic, NvmeCtrl, 0, nvme_vmstate_atomic, NvmeAtomic),

        VMSTATE_END_OF_LIST()
    },
};]=]
  [=[        VMSTATE_STRUCT(atomic, NvmeCtrl, 0, nvme_vmstate_atomic, NvmeAtomic),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &nvme_vmstate_power_management,
        NULL
    },
};]=])
native_file("hw/nvme/ctrl.c" "${content}")
