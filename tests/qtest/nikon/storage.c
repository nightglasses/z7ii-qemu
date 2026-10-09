/*
 * Original NVMe queue geometry against the mature controller and real media.
 * The ARM virt PCI host is a test fixture, not a Z7II host-address claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"

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
    StorageQueue admin, io;
} StorageTest;

static void wait_status(StorageTest *t, uint32_t mask, uint32_t expected)
{
    unsigned i;

    for (i = 0; i < 10000; i++) {
        if ((qtest_readl(t->qts, BAR + 0x1c) & mask) == expected) {
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
    qtest_writel(t->qts, BAR + 0x1000 + q->id * 8, q->tail);

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
            qtest_writel(t->qts, BAR + 0x1004 + q->id * 8, q->head);
            return status >> 1;
        }
        qtest_clock_step(t->qts, 100000);
    }
    g_error("NVMe command %u did not complete", q->cid);
    return 0;
}

static StorageTest storage_init(bool readonly)
{
    StorageTest t = {
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
    g_assert_cmpuint((qtest_readq(t.qts, BAR) >> 32) & 0xf, ==, 0);
    qtest_writel(t.qts, BAR + 0x24, 0x00010001); /* two entries per admin queue */
    qtest_writeq(t.qts, BAR + 0x28, ASQ);
    qtest_writeq(t.qts, BAR + 0x30, ACQ);
    qtest_writel(t.qts, BAR + 0x14, 0x00460001); /* SQ=64B, CQ=16B, MPS=4KiB */
    wait_status(&t, 1, 1);
    g_assert_cmpuint(submit(&t, &t.admin, 5, 0, ICQ, 0, 0x10001, 3, 0, NULL), ==, 0);
    g_assert_cmpuint(submit(&t, &t.admin, 1, 0, ISQ, 0, 0x10001, 0x10001, 0, NULL), ==, 0);
    return t;
}

static void storage_quit(StorageTest *t)
{
    qtest_quit(t->qts);
    unlink(t->path);
    g_free(t->path);
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
    qtest_writel(t.qts, BAR + 0x14, 0);
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
    return g_test_run();
}
