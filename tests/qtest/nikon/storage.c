/*
 * Original NVMe queue geometry against the mature controller and real media.
 * The ARM virt PCI host is a test fixture, not a Z7II host-address claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "libqtest.h"
#include "hw/nikon/storage.h"
#include "hw/nikon/sla.h"
#include "qobject/qdict.h"

#define ECAM UINT64_C(0x3f008000) /* virt fixture: bus 0, slot 1 */
#define BAR UINT64_C(0x10000000)
#define ASQ UINT64_C(0x40100000)
#define ACQ UINT64_C(0x40101000)
#define ISQ UINT64_C(0x40102000)
#define ICQ UINT64_C(0x40103000)
#define DATA UINT64_C(0x40400000)
#define PRPL UINT64_C(0x40300000)
#define MEDIA_SIZE (2 * 1024 * 1024)

typedef struct StorageQueue {
    uint64_t sq, cq;
    uint16_t id, tail, head, cid;
    bool phase;
} StorageQueue;

typedef struct StorageTest {
    QTestState *qts;
    char *path;
    char *image;
    uint64_t bar;
    StorageQueue admin, io;
} StorageTest;

static void wait_status(StorageTest *t, uint32_t mask, uint32_t expected)
{
    unsigned i;

    for (i = 0; i < 10000; i++) {
        if ((qtest_readl(t->qts, t->bar + 0x1c) & mask) == expected) {
            return;
        }
        qtest_clock_step(t->qts, 100000);
    }
    g_error("NVMe controller status did not reach %#x", expected);
}

static uint16_t submit(StorageTest *t, StorageQueue *q, uint8_t opcode,
                       uint32_t nsid, uint64_t prp1, uint64_t prp2,
                       uint32_t cdw10, uint32_t cdw11, uint32_t cdw12,
                       uint32_t *result)
{
    uint8_t cmd[64] = { 0 }, cqe[16];
    uint16_t status;
    unsigned i;

    cmd[0] = opcode;
    stw_le_p(cmd + 2, ++q->cid);
    stl_le_p(cmd + 4, nsid);
    stq_le_p(cmd + 24, prp1);
    stq_le_p(cmd + 32, prp2);
    stl_le_p(cmd + 40, cdw10);
    stl_le_p(cmd + 44, cdw11);
    stl_le_p(cmd + 48, cdw12);
    qtest_memwrite(t->qts, q->sq + q->tail * sizeof(cmd), cmd, sizeof(cmd));
    q->tail ^= 1;
    qtest_writel(t->qts, t->bar + 0x1000 + q->id * 8, q->tail);

    for (i = 0; i < 10000; i++) {
        qtest_memread(t->qts, q->cq + q->head * sizeof(cqe), cqe, sizeof(cqe));
        status = lduw_le_p(cqe + 14);
        if ((status & 1) == q->phase) {
            g_assert_cmpuint(lduw_le_p(cqe + 12), ==, q->cid);
            g_assert_cmpuint(lduw_le_p(cqe + 10), ==, q->id);
            if (result) {
                *result = ldl_le_p(cqe);
            }
            q->head ^= 1;
            if (!q->head) {
                q->phase = !q->phase;
            }
            qtest_writel(t->qts, t->bar + 0x1004 + q->id * 8, q->head);
            return status >> 1;
        }
        qtest_clock_step(t->qts, 100000);
    }
    g_error("NVMe command %u did not complete", q->cid);
    return 0;
}

static void storage_queues_init(StorageTest *t)
{
    g_assert_cmpuint((qtest_readq(t->qts, t->bar) >> 32) & 0xf, ==, 0);
    qtest_writel(t->qts, t->bar + 0x24, 0x00010001);
    qtest_writeq(t->qts, t->bar + 0x28, t->admin.sq);
    qtest_writeq(t->qts, t->bar + 0x30, t->admin.cq);
    qtest_writel(t->qts, t->bar + 0x14, 0x00460001);
    wait_status(t, 1, 1);
    g_assert_cmpuint(submit(t, &t->admin, 5, 0, t->io.cq, 0,
                           0x10001, 3, 0, NULL), ==, 0);
    g_assert_cmpuint(submit(t, &t->admin, 1, 0, t->io.sq, 0,
                           0x10001, 0x10001, 0, NULL), ==, 0);
}

static StorageTest storage_init(bool readonly)
{
    StorageTest t = {
        .bar = BAR,
        .admin = { .sq = ASQ, .cq = ACQ, .phase = true },
        .io = { .sq = ISQ, .cq = ICQ, .id = 1, .phase = true },
    };
    int fd = g_file_open_tmp("nikon-storage-XXXXXX", &t.path, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, MEDIA_SIZE), ==, 0);
    close(fd);
    t.qts = qtest_initf("-machine virt,highmem=off -m 64M -nodefaults "
                        "-display none -drive if=none,id=media,file=%s,"
                        "format=raw,cache=writeback,readonly=%s "
                        "-device nvme,addr=1,drive=media,serial=Nightglass",
                        t.path, readonly ? "on" : "off");
    /* Standard PCI configuration. These addresses belong only to virt. */
    g_assert_cmphex(qtest_readw(t.qts, ECAM), !=, 0xffff);
    qtest_writel(t.qts, ECAM + 0x10, BAR | 4);
    qtest_writel(t.qts, ECAM + 0x14, 0);
    qtest_writew(t.qts, ECAM + 4, 6); /* memory decoding and bus master */
    storage_queues_init(&t);
    return t;
}

static void storage_quit(StorageTest *t)
{
    qtest_quit(t->qts);
    unlink(t->path);
    g_free(t->path);
    if (t->image) {
        unlink(t->image);
        g_free(t->image);
    }
}

#define DBI UINT64_C(0x2a900000)
#define CFG UINT64_C(0x2a800000)
#define PHY UINT64_C(0x2b200000)
#define PLATFORM UINT64_C(0x2b100000)
#define HOST_DATA UINT64_C(0xb1810000)
#define MSI_DEST UINT64_C(0xb1800740)

static StorageTest storage_host_init(bool connected)
{
    StorageTest t = {
        .bar = 0x2a810000,
        .admin = { .sq = 0xb1801000, .cq = 0xb1802000, .phase = true },
        .io = { .sq = 0xb1803000, .cq = 0xb1804000, .id = 1, .phase = true },
    };
    uint32_t loop = cpu_to_le32(0xeafffffe);
    g_autofree char *drive = NULL;
    int fd = g_file_open_tmp("nikon-storage-XXXXXX", &t.path, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, MEDIA_SIZE), ==, 0);
    close(fd);
    fd = g_file_open_tmp("nikon-storage-XXXXXX", &t.image, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, &loop, sizeof(loop)), ==, sizeof(loop));
    close(fd);
    drive = connected ? g_strdup_printf("-drive if=none,id=storage,file=%s,"
                                       "format=raw,cache=writeback", t.path) :
                        g_strdup("");
    t.qts = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-display none -serial none %s", t.image, drive);
    /* Board-owned storage has a stable named child, not an unattached device. */
    qtest_irq_intercept_out_named(t.qts, "/machine/storage", "sysbus-irq");
    qtest_qmp_assert_success(t.qts, "{'execute':'cont'}");
    return t;
}

static void storage_host_start(StorageTest *t)
{
    /* Complete source platform fields, not a guessed physical clock model. */
    qtest_writel(t->qts, PLATFORM, 0x101);
    qtest_writel(t->qts, PLATFORM + 4, 0);
    qtest_writel(t->qts, PLATFORM + 8, 1);
    qtest_writel(t->qts, PLATFORM + 0xc, 0);
    qtest_writel(t->qts, DBI + 0x168, 0);
    qtest_writel(t->qts, DBI + 0x80c, 1u << 17);
    g_assert_cmpuint(qtest_readl(t->qts, DBI + 0x80c), ==, 1u << 17);
    /* Original10517b40 finite two-region producer, CFG0 target remains zero. */
    qtest_writel(t->qts, DBI + 0x900, 0);
    qtest_writel(t->qts, DBI + 0x904, 4);
    qtest_writel(t->qts, DBI + 0x90c, CFG);
    qtest_writel(t->qts, DBI + 0x914, CFG + 0xffff);
    qtest_writel(t->qts, DBI + 0x908, 1u << 31);
    qtest_writel(t->qts, DBI + 0x900, 1);
    qtest_writel(t->qts, DBI + 0x904, 0);
    qtest_writel(t->qts, DBI + 0x90c, t->bar);
    qtest_writel(t->qts, DBI + 0x914, 0x2a8fffff);
    qtest_writel(t->qts, DBI + 0x918, t->bar);
    qtest_writel(t->qts, DBI + 0x908, 1u << 31);
    qtest_writel(t->qts, PHY + 0x24, 0x10001);
    qtest_writel(t->qts, PHY + 0x28, 0x33333);
    qtest_writel(t->qts, PHY + 0x2c, 0x33033300);
    qtest_writel(t->qts, PHY + 0xc, 0x33);
    qtest_writel(t->qts, PHY + 0x50, 0x100);
    qtest_writel(t->qts, PHY, 0x14);
    g_assert_cmpuint(qtest_readl(t->qts, PHY + 0x58) >> 16, ==, 0);
    qtest_clock_step(t->qts, 1000000);
}

static void test_host_offline(void)
{
    StorageTest t = storage_host_init(false);
    storage_host_start(&t);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x210), ==, 0);
    g_assert_cmpuint(qtest_readl(t.qts, DBI + 0x98) & 15, ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 1));
    g_assert_false(qtest_get_irq(t.qts, 3));
    storage_quit(&t);
}

static char *storage_bundle(void)
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
    fd = g_file_open_tmp("nikon-storage-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, total, NULL));
    return path;
}

static void test_host_domains(void)
{
    g_autofree char *bundle = storage_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s "
                              "-smp cpus=2,maxcpus=5 -nodefaults", quoted);
    const char *const hosts[] = { "/machine/storage", "/machine/sla/storage" };
    g_autofree char *memory = NULL;
    unsigned i;

    /* Both real machine factories must finish VMState registration. GPEX's
     * default fixed root path aborts here even when neither host has media.
     */
    for (i = 0; i < ARRAY_SIZE(hosts); i++) {
        QDict *reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                                   "'path':%s,'property':'memory'}}", hosts[i]);
        const char *selected = qdict_get_str(reply, "return");

        if (i == 0) {
            memory = g_strdup(selected);
        } else {
            g_assert_cmpstr(selected, !=, memory);
        }
        qobject_unref(reply);
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                              "'path':%s,'property':'type'}}", hosts[i]);
        g_assert_cmpstr(qdict_get_str(reply, "return"), ==,
                        TYPE_NIKON_STORAGE_HOST);
        qobject_unref(reply);
    }
    g_assert_cmpuint(qtest_readl(q, PHY + 0x58) >> 16, ==, 0);
    g_assert_cmpuint(qtest_readl(q, PHY + 0x210), ==, 0);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_host_roundtrip_cancel(void)
{
    StorageTest t = storage_host_init(true);
    uint8_t written[512], actual[sizeof(written)];
    unsigned i;
    int fd;

    storage_host_start(&t);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0x11);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x210), ==, 1);
    g_assert_cmpuint(qtest_readl(t.qts, DBI + 0x98) & 15, ==, 2);
    /* Native external class0802 proves target0 did not select the root bridge. */
    g_assert_cmphex(qtest_readl(t.qts, CFG + 8) >> 16, ==, 0x0108);
    qtest_writel(t.qts, CFG + 0x10, t.bar | 4);
    qtest_writel(t.qts, CFG + 0x14, 0);
    qtest_writew(t.qts, CFG + 4, 6);
    storage_queues_init(&t);
    for (i = 0; i < sizeof(written); i++) {
        written[i] = i * 13;
    }
    qtest_memwrite(t.qts, HOST_DATA, written, sizeof(written));
    g_assert_cmpuint(submit(&t, &t.io, 1, 1, HOST_DATA, 0, 8, 0, 0, NULL), ==, 0);
    g_assert_cmpuint(submit(&t, &t.io, 0, 1, 0, 0, 0, 0, 0, NULL), ==, 0);
    fd = open(t.path, O_RDONLY);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(pread(fd, actual, sizeof(actual), 8 * 512), ==, sizeof(actual));
    close(fd);
    g_assert_cmpmem(actual, sizeof(actual), written, sizeof(written));
    /* Real CQ producer and consumption cause native INTx assertion/deassertion. */
    g_assert_cmphex(qtest_readl(t.qts, PHY + 0x24) & 0x30000000, ==, 0x30000000);
    g_assert_true(qtest_get_irq(t.qts, 1));
    qtest_writel(t.qts, PHY + 0x24, qtest_readl(t.qts, PHY + 0x24));
    g_assert_false(qtest_get_irq(t.qts, 1));
    qtest_writel(t.qts, PHY + 0x3c, 1u << 4);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 1));
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0x15);
    g_assert_cmphex(qtest_readl(t.qts, PHY + 0x2c) & (1u << 26), ==, 1u << 26);
    g_assert_true(qtest_get_irq(t.qts, 1));
    qtest_writel(t.qts, PHY + 0x2c, qtest_readl(t.qts, PHY + 0x2c));
    g_assert_false(qtest_get_irq(t.qts, 1));
    /* ACK cannot retrain. A new explicit start does; native CC is reset. */
    qtest_clock_step(t.qts, 10000000);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0x15);
    qtest_writel(t.qts, PHY, 0x14);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmpuint(qtest_readl(t.qts, CFG + 0x10), ==, 4);
    qtest_writel(t.qts, CFG + 0x10, t.bar | 4);
    qtest_writel(t.qts, CFG + 0x14, 0);
    qtest_writew(t.qts, CFG + 4, 6);
    g_assert_cmpuint(qtest_readl(t.qts, t.bar + 0x1c) & 1, ==, 0);
    qtest_writel(t.qts, PHY + 0x3c, 1u << 4);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0x15);
    g_assert_cmphex(qtest_readl(t.qts, PHY + 0x2c) & (1u << 26), ==, 1u << 26);
    qtest_writel(t.qts, PHY + 0x300, 1);
    g_assert_cmpuint(qtest_readl(t.qts, PHY + 0x58) >> 16, ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 1));
    g_assert_false(qtest_get_irq(t.qts, 3));
    storage_quit(&t);
}

static void test_host_msi(void)
{
    StorageTest t = storage_host_init(true);
    uint8_t cap;
    uint32_t table;
    unsigned limit;

    storage_host_start(&t);
    qtest_writel(t.qts, CFG + 0x10, t.bar | 4);
    qtest_writel(t.qts, CFG + 0x14, 0);
    qtest_writew(t.qts, CFG + 4, 6);
    storage_queues_init(&t);
    qtest_writel(t.qts, PHY + 0x24, qtest_readl(t.qts, PHY + 0x24));
    /* Use the native endpoint's real MSI-X capability. The source's optional
     * plain-MSI branch is not manufactured on this virtual PCI device.
     */
    cap = qtest_readb(t.qts, CFG + 0x34);
    for (limit = 0; cap && limit < 48; limit++) {
        if (qtest_readb(t.qts, CFG + cap) == 0x11) {
            break;
        }
        cap = qtest_readb(t.qts, CFG + cap + 1);
    }
    g_assert_cmpuint(cap, !=, 0);
    g_assert_cmpuint(limit, <, 48);
    table = qtest_readl(t.qts, CFG + cap + 4);
    g_assert_cmpuint(table & 7, ==, 0);
    table &= ~7u;
    qtest_writel(t.qts, DBI + 0x820, MSI_DEST);
    qtest_writel(t.qts, DBI + 0x828, 1);
    qtest_writel(t.qts, DBI + 0x82c, 1);
    qtest_writel(t.qts, MSI_DEST, 0x76543210);
    g_assert_cmpuint(qtest_readl(t.qts, DBI + 0x830), ==, 0);
    qtest_writel(t.qts, t.bar + table, MSI_DEST);
    qtest_writel(t.qts, t.bar + table + 4, 0);
    qtest_writel(t.qts, t.bar + table + 8, 0);
    qtest_writel(t.qts, t.bar + table + 12, 0);
    qtest_writew(t.qts, CFG + cap + 2,
                 qtest_readw(t.qts, CFG + cap + 2) | 0x8000);
    g_assert_cmpuint(submit(&t, &t.admin, 6, 1, HOST_DATA, 0,
                           0, 0, 0, NULL), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, DBI + 0x830), ==, 1);
    g_assert_false(qtest_get_irq(t.qts, 3));
    g_assert_cmphex(qtest_readl(t.qts, MSI_DEST), ==, 0x76543210);
    qtest_writel(t.qts, DBI + 0x82c, 0);
    g_assert_true(qtest_get_irq(t.qts, 3));
    qtest_writel(t.qts, DBI + 0x830, 1);
    g_assert_false(qtest_get_irq(t.qts, 3));
    storage_quit(&t);
}

static void test_roundtrip(void)
{
    StorageTest t = storage_init(false);
    uint8_t expected[12288], actual[sizeof(expected)], blank[sizeof(expected)] = { 0 };
    uint8_t list[16];
    int fd;
    unsigned i;

    /* Identify reports the actual backend capacity and 512-byte LBA format. */
    g_assert_cmpuint(submit(&t, &t.admin, 6, 1, DATA, 0, 0, 0, 0, NULL), ==, 0);
    qtest_memread(t.qts, DATA, actual, 4096);
    g_assert_cmpuint(ldq_le_p(actual), ==, MEDIA_SIZE / 512);
    g_assert_cmpuint(ldq_le_p(actual + 8), ==, MEDIA_SIZE / 512);
    g_assert_cmpuint(actual[128 + (actual[26] & 0xf) * 4 + 2], ==, 9);

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = (i * 17 + (i >> 8)) & 0xff;
    }
    stq_le_p(list, DATA + 4096);
    stq_le_p(list + 8, DATA + 8192);
    qtest_memwrite(t.qts, PRPL, list, sizeof(list));
    qtest_memwrite(t.qts, DATA, expected, sizeof(expected));
    /* LBA 8, 24 sectors: PRP2 is a list, not the second data page. */
    g_assert_cmpuint(submit(&t, &t.io, 1, 1, DATA, PRPL, 8, 0, 23, NULL), ==, 0);
    g_assert_cmpuint(submit(&t, &t.io, 0, 1, 0, 0, 0, 0, 0, NULL), ==, 0);
    fd = open(t.path, O_RDONLY);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(pread(fd, actual, sizeof(actual), 8 * 512), ==, sizeof(actual));
    close(fd);
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    qtest_memwrite(t.qts, DATA, blank, sizeof(blank));
    g_assert_cmpuint(submit(&t, &t.io, 2, 1, DATA, PRPL, 8, 0, 23, NULL), ==, 0);
    qtest_memread(t.qts, DATA, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    storage_quit(&t);
}

static void test_errors_reset(void)
{
    StorageTest t = storage_init(false);
    uint32_t result;

    g_assert_cmpuint(submit(&t, &t.io, 2, 2, DATA, 0, 0, 0, 0, NULL), !=, 0);
    g_assert_cmpuint(submit(&t, &t.io, 2, 1, DATA, 0, MEDIA_SIZE / 512, 0, 0, NULL), !=, 0);
    /* A completed CID is not abortable; upstream reports that in DW0. */
    g_assert_cmpuint(submit(&t, &t.admin, 8, 0, 0, 0,
                           ((uint32_t)t.io.cid << 16) | 1, 0, 0, &result), ==, 0);
    g_assert_cmpuint(result & 1, ==, 1);
    qtest_writel(t.qts, t.bar + 0x14, 0);
    wait_status(&t, 1, 0);
    storage_quit(&t);
}

static void test_readonly(void)
{
    StorageTest t = storage_init(true);

    g_assert_cmpuint(submit(&t, &t.io, 1, 1, DATA, 0, 0, 0, 0, NULL), !=, 0);
    g_assert_cmpuint(submit(&t, &t.io, 2, 1, DATA, 0, 0, 0, 0, NULL), ==, 0);
    storage_quit(&t);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/storage/roundtrip", test_roundtrip);
    qtest_add_func("/nikon/storage/errors/reset", test_errors_reset);
    qtest_add_func("/nikon/storage/readonly", test_readonly);
    qtest_add_func("/nikon/storage/host/offline", test_host_offline);
    qtest_add_func("/nikon/storage/host/domains", test_host_domains);
    qtest_add_func("/nikon/storage/host/roundtrip/cancel", test_host_roundtrip_cancel);
    qtest_add_func("/nikon/storage/host/msi", test_host_msi);
    return g_test_run();
}
