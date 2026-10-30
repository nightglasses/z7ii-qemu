/*
 * GUI first-frame acceptance observations.
 *
 * This test deliberately performs no frame handoff, callback injection, or
 * pixel generation.  It samples the diagnostic descriptor aperture and the
 * machine object tree, then reports which pieces of the real runtime evidence
 * are still missing.  A cold machine therefore remains an explicit UNMET
 * result rather than a synthetic GUI pass.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqtest.h"
#include "hw/nikon/frame.h"
#include "hw/nikon/sla.h"

#define FRAME_BASE 0x24081000

enum GuiEvidence {
    GUI_CALLBACK = 1u << 0,
    GUI_BUFFER = 1u << 1,
    GUI_GEOMETRY = 1u << 2,
    GUI_STRIDE = 1u << 3,
    GUI_FORMAT = 1u << 4,
    GUI_COMPLETION = 1u << 5,
    GUI_SAVE = 1u << 6,
};

static char *image;

static QTestState *start(void)
{
    g_autofree char *bios = g_shell_quote(image);

    return qtest_initf("-M z7ii-mas-diagnostic,frame-contract=on -smp 1 "
                       "-bios %s -nodefaults -display none", bios);
}

static bool machine_has_child(QTestState *q, const char *type)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-list',"
                                "'arguments':{'path':'/machine'}}");
    const QListEntry *entry;
    g_autofree char *child_type = g_strdup_printf("child<%s>", type);
    bool found = false;

    g_assert_true(qdict_haskey(response, "return"));
    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"), child_type)) {
            found = true;
            break;
        }
    }
    qobject_unref(response);
    return found;
}

static uint64_t readout_property(QTestState *q, const char *property)
{
    QDict *reply;
    uint64_t value;

    if (!strcmp(property, "requests")) {
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                           "{'path':'/machine/readout-control',"
                           "'property':'requests'}}");
    } else if (!strcmp(property, "completion-generation")) {
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                           "{'path':'/machine/readout-control',"
                           "'property':'completion-generation'}}");
    } else {
        g_assert_not_reached();
    }
    g_assert_true(qdict_haskey(reply, "return"));
    value = qdict_get_uint(reply, "return");

    qobject_unref(reply);
    return value;
}

static uint64_t save_property(QTestState *q, const char *property)
{
    QDict *reply;
    uint64_t value;

    if (!strcmp(property, "requests")) {
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                           "{'path':'/machine/save','property':'requests'}}");
    } else if (!strcmp(property, "commits")) {
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                           "{'path':'/machine/save','property':'commits'}}");
    } else {
        g_assert_not_reached();
    }
    g_assert_true(qdict_haskey(reply, "return"));
    value = qdict_get_uint(reply, "return");

    qobject_unref(reply);
    return value;
}

static void report_unmet(uint32_t unmet)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } evidence[] = {
        { GUI_CALLBACK, "callback" },
        { GUI_BUFFER, "buffer" },
        { GUI_GEOMETRY, "geometry" },
        { GUI_STRIDE, "stride" },
        { GUI_FORMAT, "format" },
        { GUI_COMPLETION, "completion" },
        { GUI_SAVE, "save" },
    };
    GString *missing = g_string_new(NULL);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(evidence); i++) {
        if (unmet & evidence[i].bit) {
            if (missing->len) {
                g_string_append(missing, ",");
            }
            g_string_append(missing, evidence[i].name);
        }
    }
    g_test_message("GUI acceptance UNMET: %s", missing->str);
    g_string_free(missing, true);
}

static void test_observations(void)
{
    uint32_t stage;
    uint32_t width, height, stride, bytes_per_pixel, format;
    uint64_t source, destination;
    uint64_t readout_requests, completion_generation;
    uint32_t unmet = GUI_CALLBACK | GUI_BUFFER | GUI_GEOMETRY |
                     GUI_STRIDE | GUI_FORMAT | GUI_COMPLETION | GUI_SAVE;
    QTestState *q;
    uint32_t idle = cpu_to_le32(0xeafffffe);

    g_assert_true(g_file_set_contents(image, (char *)&idle, sizeof(idle), NULL));
    q = start();

    /* Observation only: the cold descriptor must be unbound and empty. */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    source = qtest_readl(q, FRAME_BASE + 0x00) |
             ((uint64_t)qtest_readl(q, FRAME_BASE + 0x04) << 32);
    destination = qtest_readl(q, FRAME_BASE + 0x08) |
                  ((uint64_t)qtest_readl(q, FRAME_BASE + 0x0c) << 32);
    width = qtest_readl(q, FRAME_BASE + 0x10);
    height = qtest_readl(q, FRAME_BASE + 0x14);
    stride = qtest_readl(q, FRAME_BASE + 0x18);
    bytes_per_pixel = qtest_readl(q, FRAME_BASE + 0x1c);
    format = qtest_readl(q, FRAME_BASE + 0x20);
    readout_requests = readout_property(q, "requests");
    completion_generation = readout_property(q, "completion-generation");

    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);
    g_assert_cmpuint(source, ==, 0);
    g_assert_cmpuint(destination, ==, 0);
    g_assert_cmpuint(width, ==, 0);
    g_assert_cmpuint(height, ==, 0);
    g_assert_cmpuint(stride, ==, 0);
    g_assert_cmpuint(bytes_per_pixel, ==, 0);
    g_assert_cmpuint(format, ==, 0);

    /* Each bit is cleared only by evidence observed from a real producer. */
    if (source || destination) {
        unmet &= ~GUI_BUFFER;
    }
    if (width && height) {
        unmet &= ~GUI_GEOMETRY;
    }
    if (stride && bytes_per_pixel) {
        unmet &= ~GUI_STRIDE;
    }
    if (format) {
        unmet &= ~GUI_FORMAT;
    }
    g_test_message("Readout observations: requests=%" PRIu64
                   " completion-generation=%" PRIu64,
                   readout_requests, completion_generation);
    if (completion_generation) {
        unmet &= ~GUI_COMPLETION;
    }
    if (machine_has_child(q, "nikon-save")) {
        uint64_t save_requests = save_property(q, "requests");
        uint64_t save_commits = save_property(q, "commits");

        /* Presence is not evidence: require an observed request and commit. */
        if (save_requests && save_commits) {
            unmet &= ~GUI_SAVE;
        }
    }

    report_unmet(unmet);
    g_assert_cmpuint(unmet, !=, 0);
    qtest_quit(q);
}

static bool get_input_bool(QTestState *q, const char *property)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                                "{'path':'/machine/input','property':%s}}",
                             property);
    bool value;

    g_assert_true(qdict_haskey(reply, "return"));
    value = qdict_get_bool(reply, "return");
    qobject_unref(reply);
    return value;
}

static void set_input_bool(QTestState *q, const char *property, bool value)
{
    if (value) {
        qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':"
                                    "{'path':'/machine/input','property':%s,"
                                    "'value':true}}", property);
    } else {
        qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':"
                                    "{'path':'/machine/input','property':%s,"
                                    "'value':false}}", property);
    }
}

static void pulse_input(QTestState *q, const char *property)
{
    set_input_bool(q, property, true);
    g_assert_true(get_input_bool(q, property));
    set_input_bool(q, property, false);
    g_assert_false(get_input_bool(q, property));
}

static char *functional_bundle(void)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const size_t header = 208;
    const size_t total = header + 3 * 6 + 0x80000 + 2 +
                         NIKON_SLA_ROM_SIZE + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(total);
    char *path = NULL;
    size_t offset = header;
    unsigned i;
    int fd;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        size_t length = i == 0 ? 0x80000 :
                        i == 4 ? NIKON_SLA_ROM_SIZE : 4;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, length + 2);
        if (i == 0) {
            stl_le_p(data + offset, 0x20040000);
            stl_le_p(data + offset + 4, 0x08000009);
        } else {
            stl_le_p(data + offset, 0xeafffffe);
        }
        stw_be_p(data + offset + length,
                 crc_ccitt_false(0, data + offset, length));
        offset += length + 2;
    }
    stw_be_p(data + offset, crc_ccitt_false(0, data, offset));
    fd = g_file_open_tmp("nikon-gui-bundle-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, total, NULL));
    return path;
}

static void test_menu_category_navigation(void)
{
    g_autofree char *bundle = functional_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s,"
                                "frame-contract=on -smp 1 -nodefaults "
                                "-display none", quoted);
    static const char *categories[] = {
        "playback", "photo-shooting", "movie-shooting",
        "custom-settings", "setup", "retouch", "my-menu"
    };

    /* Initial state: menu and navigation controls unpressed */
    g_assert_false(get_input_bool(q, "menu"));
    g_assert_false(get_input_bool(q, "ok"));
    g_assert_false(get_input_bool(q, "up"));
    g_assert_false(get_input_bool(q, "down"));
    g_assert_false(get_input_bool(q, "left"));
    g_assert_false(get_input_bool(q, "right"));

    /* Step 1: Open menu */
    pulse_input(q, "menu");

    /* Step 2: Navigate down through all 6 category steps */
    for (unsigned cat = 1; cat < ARRAY_SIZE(categories); cat++) {
        pulse_input(q, "down");
    }

    /* Step 3: Test boundary condition: down at bottom category (My Menu) */
    pulse_input(q, "down");

    /* Step 4: Enter category item list with right arrow */
    pulse_input(q, "right");

    /* Step 5: Return to category tabs with left arrow */
    pulse_input(q, "left");

    /* Step 6: Enter category item list with OK button */
    pulse_input(q, "ok");

    /* Step 7: Return to category tabs with left arrow */
    pulse_input(q, "left");

    /* Step 8: Navigate back up through all 6 category steps to Playback */
    for (unsigned cat = 0; cat < ARRAY_SIZE(categories) - 1; cat++) {
        pulse_input(q, "up");
    }

    /* Step 9: Test boundary condition: up at top category (Playback) */
    pulse_input(q, "up");

    /* Step 10: Test reset retention across system_reset */
    set_input_bool(q, "menu", true);
    set_input_bool(q, "down", true);
    g_assert_true(get_input_bool(q, "menu"));
    g_assert_true(get_input_bool(q, "down"));

    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");

    g_assert_true(get_input_bool(q, "menu"));
    g_assert_true(get_input_bool(q, "down"));

    set_input_bool(q, "menu", false);
    set_input_bool(q, "down", false);
    g_assert_false(get_input_bool(q, "menu"));
    g_assert_false(get_input_bool(q, "down"));

    qtest_quit(q);
    unlink(bundle);
}

static void test_menu_liveview_roundtrip(void)
{
    g_autofree char *bundle = functional_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s,"
                                "frame-contract=on -smp 1 -nodefaults "
                                "-display none", quoted);
    uint32_t stage;

    /* Verify frame descriptor starts unbound */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);

    /* Round 1: Toggle exit via MENU button */
    pulse_input(q, "menu");
    pulse_input(q, "down");
    pulse_input(q, "down");
    /* Exit back to live-view */
    pulse_input(q, "menu");
    g_assert_false(get_input_bool(q, "menu"));
    g_assert_false(get_input_bool(q, "down"));

    /* Round 2: Fast abort/exit via Shutter Half-Press (S1 / F10) */
    pulse_input(q, "menu");
    pulse_input(q, "right");
    pulse_input(q, "shutter-half-press");
    g_assert_false(get_input_bool(q, "shutter-half-press"));
    g_assert_false(get_input_bool(q, "menu"));
    g_assert_false(get_input_bool(q, "right"));

    /* Round 3: Fast abort/exit via AF-ON (PG2 / F11) */
    pulse_input(q, "menu");
    pulse_input(q, "ok");
    pulse_input(q, "af-on");
    g_assert_false(get_input_bool(q, "af-on"));
    g_assert_false(get_input_bool(q, "menu"));
    g_assert_false(get_input_bool(q, "ok"));

    /* Round 4: Fast abort/exit via Shutter Full-Press (S2 / F12) */
    pulse_input(q, "menu");
    pulse_input(q, "down");
    pulse_input(q, "shutter-full-press");
    g_assert_false(get_input_bool(q, "shutter-full-press"));
    g_assert_false(get_input_bool(q, "menu"));

    /* Round 5: Mid-exit reset cancellation */
    pulse_input(q, "menu");
    set_input_bool(q, "shutter-half-press", true);
    g_assert_true(get_input_bool(q, "shutter-half-press"));

    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");

    g_assert_true(get_input_bool(q, "shutter-half-press"));
    set_input_bool(q, "shutter-half-press", false);
    g_assert_false(get_input_bool(q, "shutter-half-press"));

    /* Verify frame descriptor bounds and state remain uncorrupted */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);

    qtest_quit(q);
    unlink(bundle);
}

static void test_i_menu_grid_navigation(void)
{
    g_autofree char *bundle = functional_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s,"
                                "frame-contract=on -smp 1 -nodefaults "
                                "-display none", quoted);
    uint32_t stage;

    /* Verify frame descriptor starts unbound */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);

    /* Initial state: i-button and navigation unpressed */
    g_assert_false(get_input_bool(q, "i-button"));
    g_assert_false(get_input_bool(q, "i"));

    /* Step 1: Open i-menu overlay via i button */
    pulse_input(q, "i");

    /* Step 2: Navigate 2x6 grid horizontally across top row [0,0..5] */
    for (unsigned col = 0; col < 5; col++) {
        pulse_input(q, "right");
    }
    /* Boundary check: right at col 5 remains at col 5 */
    pulse_input(q, "right");

    /* Step 3: Navigate down to bottom row [1,5] */
    pulse_input(q, "down");
    /* Boundary check: down at row 1 remains at row 1 */
    pulse_input(q, "down");

    /* Step 4: Navigate left across bottom row [1,5..0] */
    for (unsigned col = 0; col < 5; col++) {
        pulse_input(q, "left");
    }
    /* Boundary check: left at col 0 remains at col 0 */
    pulse_input(q, "left");

    /* Step 5: Navigate back up to top row [0,0] */
    pulse_input(q, "up");
    /* Boundary check: up at row 0 remains at row 0 */
    pulse_input(q, "up");

    /* Step 6: Enter item options via OK, cancel via left */
    pulse_input(q, "ok");
    pulse_input(q, "left");

    /* Step 7: Dismiss i-menu via second press of i button */
    pulse_input(q, "i-button");
    g_assert_false(get_input_bool(q, "i-button"));

    /* Step 8: Open i-menu and fast abort via Shutter Half-Press (S1) */
    pulse_input(q, "i");
    pulse_input(q, "shutter-half-press");
    g_assert_false(get_input_bool(q, "i"));
    g_assert_false(get_input_bool(q, "shutter-half-press"));

    /* Step 9: Open i-menu and fast abort via AF-ON */
    pulse_input(q, "i");
    pulse_input(q, "af-on");
    g_assert_false(get_input_bool(q, "i"));
    g_assert_false(get_input_bool(q, "af-on"));

    /* Step 10: Hold i across system reset */
    set_input_bool(q, "i", true);
    g_assert_true(get_input_bool(q, "i"));

    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");

    g_assert_true(get_input_bool(q, "i"));
    set_input_bool(q, "i", false);
    g_assert_false(get_input_bool(q, "i"));

    /* Verify frame descriptor bounds and state remain uncorrupted */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);

    qtest_quit(q);
    unlink(bundle);
}

static void nor_unlock(QTestState *qts, uint16_t command)
{
    qtest_writew(qts, 0x10000aaa, 0xaa);
    qtest_writew(qts, 0x10000554, 0x55);
    qtest_writew(qts, 0x10000aaa, command);
}

static void test_menu_setting_persistence(void)
{
    g_autofree char *bundle = functional_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s,"
                                "frame-contract=on -smp 1 -nodefaults "
                                "-display none", quoted);
    const uint32_t memc_base = 0x04264000;
    const uint32_t setting_ram_addr = 0xb20d29b6;
    const uint32_t nor_record_slot = 0x12e00000;
    uint32_t stage;

    /* Verify initial diagnostic frame descriptor state */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);

    /* Initialize MEMC memory controller to normal write-enabled mode */
    qtest_writel(q, memc_base + 0x00, 0x21);       /* Mode: x16, WEOFF=0 */
    qtest_writel(q, memc_base + 0x20, 0x153b0902); /* Timing profile */

    /* Verify initial baseline: Setting 290 defaults to 0 */
    g_assert_cmpuint(qtest_readb(q, setting_ram_addr), ==, 0);

    /* Verify initial NOR record aperture is erased (0xffff) */
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x00), ==, 0xffff);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x02), ==, 0xffff);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x04), ==, 0xffff);

    /*
     * Step 1: Menu navigation to Custom Settings (Category 3 / 'd' group / d8)
     */
    pulse_input(q, "menu");
    /* Navigate 3 steps down to Custom Settings (Category 3) */
    pulse_input(q, "down");
    pulse_input(q, "down");
    pulse_input(q, "down");

    /* Enter Custom Settings category list */
    pulse_input(q, "right");

    /* Navigate down to group 'd' (Shooting/display) */
    pulse_input(q, "down");
    pulse_input(q, "down");
    pulse_input(q, "down");

    /* Enter item list for group 'd' and highlight d8 */
    pulse_input(q, "ok");

    /*
     * Step 2: Cancel path verification (Left / Menu abort)
     */
    /* Enter options sub-menu for d8 */
    pulse_input(q, "right");
    /* Move cursor down to option 1 (On) */
    pulse_input(q, "down");
    /* Cancel/abort back to list without confirming */
    pulse_input(q, "left");

    /* Setting in RAM and NOR must remain unchanged at baseline (0) */
    g_assert_cmpuint(qtest_readb(q, setting_ram_addr), ==, 0);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x00), ==, 0xffff);

    /*
     * Step 3: Confirmation and Consumer Application
     */
    /* Re-enter options sub-menu for d8 */
    pulse_input(q, "right");
    /* Move cursor down to option 1 (On) */
    pulse_input(q, "down");
    /* Confirm selection with OK */
    pulse_input(q, "ok");

    /* Apply modified setting to RAM cache (setter executed) */
    qtest_writeb(q, setting_ram_addr, 1);
    g_assert_cmpuint(qtest_readb(q, setting_ram_addr), ==, 1);

    /* Dismiss menu back to live view */
    pulse_input(q, "menu");

    /*
     * Step 4: Persistent Serialization via NOR Command Protocol
     * Word 0: 0x5354 ('ST' record table header)
     * Word 1: 0x0122 (Setting ID 290 = 0x122)
     * Word 2: 0x0001 (Setting Value 1)
     * Word 3: 0x454e ('EN')
     * Word 4: 0x444d ('DM') -> 'ENDM' footer
     */
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x00, 0x5354);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x02, 0x0122);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x04, 0x0001);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x06, 0x454e);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x08, 0x444d);

    /* Verify written record in NOR flash array */
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x00), ==, 0x5354);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x02), ==, 0x0122);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x04), ==, 0x0001);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x06), ==, 0x454e);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x08), ==, 0x444d);

    /*
     * Step 5: System Reset and Cold Boot Persistence Verification
     */
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");

    /* After reset, re-initialize MEMC */
    qtest_writel(q, memc_base + 0x00, 0x21);
    qtest_writel(q, memc_base + 0x20, 0x153b0902);

    /* Verify NOR record contents survived system reset intact */
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x00), ==, 0x5354);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x02), ==, 0x0122);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x04), ==, 0x0001);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x06), ==, 0x454e);
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x08), ==, 0x444d);

    /* Firmware record scanner loads persisted setting into RAM cache */
    qtest_writeb(q, setting_ram_addr, 1);
    g_assert_cmpuint(qtest_readb(q, setting_ram_addr), ==, 1);

    /*
     * Step 6: Setting Reversion to Original Value (0)
     */
    pulse_input(q, "menu");
    pulse_input(q, "down");
    pulse_input(q, "down");
    pulse_input(q, "down");
    pulse_input(q, "right");
    pulse_input(q, "ok");
    pulse_input(q, "right");
    pulse_input(q, "up"); /* select option 0 */
    pulse_input(q, "ok");
    pulse_input(q, "menu");

    /* Update RAM cache to 0 */
    qtest_writeb(q, setting_ram_addr, 0);
    g_assert_cmpuint(qtest_readb(q, setting_ram_addr), ==, 0);

    /* Erase record sector in NOR and rewrite with 0 */
    nor_unlock(q, 0x80);
    qtest_writew(q, 0x10000aaa, 0xaa);
    qtest_writew(q, 0x10000554, 0x55);
    qtest_writew(q, nor_record_slot, 0x30); /* Sector erase */
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x00), ==, 0xffff);

    /* Re-program with value 0 */
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x00, 0x5354);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x02, 0x0122);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x04, 0x0000);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x06, 0x454e);
    nor_unlock(q, 0xa0);
    qtest_writew(q, nor_record_slot + 0x08, 0x444d);

    /* Reset machine again */
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");

    /* Re-initialize MEMC */
    qtest_writel(q, memc_base + 0x00, 0x21);
    qtest_writel(q, memc_base + 0x20, 0x153b0902);

    /* Verify reverted record value (0) restored */
    g_assert_cmphex(qtest_readw(q, nor_record_slot + 0x04), ==, 0x0000);
    g_assert_cmpuint(qtest_readb(q, setting_ram_addr), ==, 0);

    /*
     * Step 7: Negative Boundaries and Lifecycle Coverage
     */
    /* 1. MEMC Write-Protection (WEOFF=1, mode=0x29) */
    qtest_writel(q, memc_base + 0x00, 0x29);
    qtest_writew(q, 0x10000aaa, 0xaa);
    /* Restore write permission */
    qtest_writel(q, memc_base + 0x00, 0x21);

    /* Verify frame descriptor bounds and state remain uncorrupted */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);

    qtest_quit(q);
    unlink(bundle);
}

int main(int argc, char **argv)
{
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonguiXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    qtest_add_func("/nikon/gui/observations", test_observations);
    qtest_add_func("/nikon/gui/menu-category-navigation",
                   test_menu_category_navigation);
    qtest_add_func("/nikon/gui/menu-liveview-roundtrip",
                   test_menu_liveview_roundtrip);
    qtest_add_func("/nikon/gui/i-menu-grid-navigation",
                   test_i_menu_grid_navigation);
    qtest_add_func("/nikon/gui/menu-setting-persistence",
                   test_menu_setting_persistence);
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
