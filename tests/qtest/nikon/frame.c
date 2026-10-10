/* Descriptor-only frame handoff. No stage writes pixels or injects a frame. */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "hw/nikon/frame.h"

#define FRAME 0x24081000
#define RAM 0xb1810000
static char *image;

static QTestState *start(void)
{
    g_autofree char *bios = g_shell_quote(image);
    return qtest_initf("-M z7ii-mas-diagnostic,frame-contract=on -smp 1 "
                       "-bios %s -nodefaults -display none", bios);
}

static void descriptor(QTestState *q, unsigned base, uint32_t destination)
{
    qtest_writel(q, FRAME + base + 0x00, 0); qtest_writel(q, FRAME + base + 0x04, 0);
    qtest_writel(q, FRAME + base + 0x08, destination); qtest_writel(q, FRAME + base + 0x0c, 0);
    qtest_writel(q, FRAME + base + 0x10, 4); qtest_writel(q, FRAME + base + 0x14, 2);
    qtest_writel(q, FRAME + base + 0x18, 16); qtest_writel(q, FRAME + base + 0x1c, 4);
    qtest_writel(q, FRAME + base + 0x20, 0xdecafbad); qtest_writel(q, FRAME + base + 0x24, 0xfeed0001);
    qtest_writel(q, FRAME + base + 0x28, 0x19); qtest_writel(q, FRAME + base + 0x2c, 0x37);
    qtest_writel(q, FRAME + base + 0x30, 0x80000000); qtest_writel(q, FRAME + base + 0x34, 17);
    qtest_writel(q, FRAME + base + 0x38, 0);
}

static void test_handoff(void)
{
    QTestState *q = start();
    uint32_t before[8], after[8];

    qtest_memset(q, RAM, 0xa5, sizeof(before)); qtest_memread(q, RAM, before, sizeof(before));
    g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_UNBOUND);
    descriptor(q, 0, RAM); qtest_writel(q, FRAME + 0xc8, 1);
    g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_CAPTURE_PENDING);
    qtest_memread(q, RAM, after, sizeof(after)); g_assert_cmpmem(after, sizeof(after), before, sizeof(before));
    g_assert_cmphex(qtest_readl(q, FRAME + 0x20), ==, 0xdecafbad);
    qtest_writel(q, FRAME + 0xc8, 2); g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_CAPTURED);
    descriptor(q, 0x40, RAM + 0x1000); qtest_writel(q, FRAME + 0xc8, 3);
    g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_PROCESS_PENDING);
    qtest_writel(q, FRAME + 0xc8, 4); g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_PROCESSED);
    descriptor(q, 0x80, RAM + 0x2000); qtest_writel(q, FRAME + 0xc8, 5);
    g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_DISPLAY_PENDING);
    qtest_memread(q, RAM, after, sizeof(after)); g_assert_cmpmem(after, sizeof(after), before, sizeof(before));
    qtest_writel(q, FRAME + 0xc8, 6); g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_DISPLAYED);
    qtest_writel(q, FRAME + 0xc8, 7); g_assert_cmphex(qtest_readl(q, FRAME + 0xc0), ==, NIKON_FRAME_UNBOUND);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    int fd, result;
    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonframebiosXXXXXX", &image, NULL); g_assert_cmpint(fd, >=, 0); close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle, sizeof(idle), NULL));
    qtest_add_func("/nikon/frame/handoff", test_handoff); result = g_test_run();
    unlink(image); g_free(image); return result;
}
