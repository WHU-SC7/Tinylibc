/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * alsa_devices — 发现 ALSA PCM 设备及其能力
 *
 * 机制：
 *   1) 打开 /dev/snd/controlC0 → SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE 枚举
 *      → SNDRV_CTL_IOCTL_PCM_INFO 获取名称/流向/子设备数
 *      （control 设备永不 busy，被 PulseAudio 占用的设备也能读出名称）
 *   2) 若 control 设备不可用（旧内核），从 /proc/asound/ 后备读取
 *   3) 尝试 open PCM 节点 → HW_REFINE 获取采样率/位深/声道等
 *   4) 根据设备名判别模拟/数字类型
 *
 * 系统调用：openat, ioctl, read, close
 *
 * 索引：
 *   main            controlC0 → 枚举 → 名称 → 类型判别 → open → HW_REFINE
 *     classify_dev  设备名含 "Analog/HDMI" 判别
 *     probe_caps    HW_REFINE 查询能力
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "errno.h"

#define PROC_BUF_SIZE 256

/* ── 设备分类 ── */

static const char *classify_dev(const char *name)
{
    if (!name || !name[0]) return "?";
    if (strstr(name, "HDMI"))               return "HDMI";
    if (strstr(name, "SPDIF") ||
        strstr(name, "IEC958"))             return "SPDIF";
    if (strstr(name, "Analog"))             return "模拟";
    if (strstr(name, "Mic"))                return "麦克风";
    return "其他";
}

/* ── 后备：从 /proc/asound/ 读取设备名称 ── */

static int read_name_from_procfs(int card, int dev, int playback,
                                 char *name, int name_sz)
{
    char path[64];
    char buf[PROC_BUF_SIZE];
    snprintf(path, sizeof(path), "/proc/asound/card%d/pcm%d%c/info",
             card, dev, playback ? 'p' : 'c');
    int fd = __openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    int n = (int)__read(fd, buf, PROC_BUF_SIZE - 1);
    __close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char *line = buf;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, "name:", 5) == 0) {
            char *val = line + 5;
            while (*val == ' ' || *val == '\t') val++;
            int len = 0;
            while (val[len] && len < name_sz - 1) { name[len] = val[len]; len++; }
            name[len] = '\0';
            return 0;
        }
        if (!nl) break;
        line = nl + 1;
    }
    return -1;
}

/* ── HW_REFINE 能力探测 ── */

static const char *fmt_name(int fmt)
{
    switch (fmt) {
    case 1: return "U8";
    case 2: return "S16_LE";
    case 6: return "S24_LE";
    case 7: return "S32_LE";
    default: return NULL;
    }
}

static void print_mask(const struct snd_mask *m, int max,
                       const char *(*fn)(int))
{
    int first = 1;
    for (int i = 0; i <= max; i++) {
        if (snd_mask_test(m, i)) {
            const char *n = fn(i);
            if (n) { __printf("%s%s", first ? "" : ", ", n); first = 0; }
        }
    }
    if (first) __printf("(null)");
}

static void print_iv(const struct snd_interval *iv)
{
    if (iv->empty) { __printf("(null)"); return; }
    if (iv->min == iv->max) __printf("%u", iv->min);
    else __printf("%u ~ %u", iv->min, iv->max);
}

static void probe_caps(int fd)
{
    struct snd_pcm_hw_params hw;
    memset(&hw, 0, sizeof(hw));
    for (int i = 0; i < 3; i++)
        memset(&hw.masks[i], 0xFF, sizeof(hw.masks[i]));
    for (int i = 0; i < 12; i++) {
        hw.intervals[i].min = 0;
        hw.intervals[i].max = 0xFFFFFFFF;
    }
    hw.rmask = ~0U;
    hw.info = ~0U;

    if (__ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw) < 0) {
        __printf("  HW_REFINE 失败\n");
        return;
    }

    int fmt_i  = SND_MASK_IDX(SNDRV_PCM_HW_PARAM_FORMAT);
    int sb_i   = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
    int ch_i   = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_CHANNELS);
    int rate_i = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_RATE);
    int ps_i   = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    int bs_i   = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_BUFFER_SIZE);

    __printf("  格式:     ");
    print_mask(&hw.masks[fmt_i], 10, fmt_name);
    __printf("\n");

    __printf("  采样位深: ");
    print_iv(&hw.intervals[sb_i]);
    __printf("  |  声道: ");
    print_iv(&hw.intervals[ch_i]);
    __printf("\n");

    __printf("  采样率:   ");
    print_iv(&hw.intervals[rate_i]);
    __printf(" Hz\n");

    __printf("  Period:   ");
    print_iv(&hw.intervals[ps_i]);
    __printf("  |  Buffer: ");
    print_iv(&hw.intervals[bs_i]);
    __printf(" 帧\n");

    if (hw.info & SNDRV_PCM_INFO_MULTI)
        __printf("  特性: MULTI（可多重打开）— 通常 HDMI/数字才有\n");
    if (snd_mask_test(&hw.masks[fmt_i], 3))
        __printf("  格式: AC3（仅数字接口）\n");
}

/* ── 主函数 ── */

int main(int argc, char **argv)
{
    int card = 0;
    (void)argc;
    (void)argv;

    PRINT_COLOR(BRIGHT_BLUE_COLOR_PRINT,
        "╔══════════════════════════════════════════════╗\n"
        "║  ALSA PCM 设备探针                           ║\n"
        "║  controlC* ioctl → 名称 → HW_REFINE → 能力   ║\n"
        "╚══════════════════════════════════════════════╝\n");

    __printf("\n缩写: PLAY=播放  CAP=录音  [模拟]=模拟音频  [HDMI]=数字输出\n");
    __printf("名称来源: SNDRV_CTL_IOCTL_PCM_INFO（控制设备 ioctl，设备被占也可读）\n");

    /* 打开控制设备 */
    char ctl_path[32];
    snprintf(ctl_path, sizeof(ctl_path), "/dev/snd/controlC%d", card);
    int ctl_fd = __openat(AT_FDCWD, ctl_path, O_RDWR | O_CLOEXEC, 0);
    int use_ctl = (ctl_fd >= 0);

    /* 枚举所有 PCM 设备 */
    int dev_idx = -1;
    int found = 0;

    while (1) {
        int next = dev_idx;
        if (!use_ctl || __ioctl(ctl_fd,
                SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, &next) < 0)
            break;
        if (next <= dev_idx) break;
        dev_idx = next;

        /* 两个方向：PLAYBACK 和 CAPTURE */
        static const int streams[] = {
            SNDRV_PCM_STREAM_PLAYBACK,
            SNDRV_PCM_STREAM_CAPTURE
        };
        static const char stream_ch[] = { 'p', 'c' };
        static const char *stream_str[] = { "PLAY", "CAP " };

        for (int si = 0; si < 2; si++) {
            struct snd_pcm_info info;
            memset(&info, 0, sizeof(info));
            info.device    = dev_idx;
            info.subdevice = 0;
            info.stream    = streams[si];

            /* 通过 control 设备查询名称 */
            int has_ctl = (use_ctl && __ioctl(ctl_fd,
                           SNDRV_CTL_IOCTL_PCM_INFO, &info) == 0);

            /* 后备：无 control 或查询失败 → procfs */
            char fallback_name[96] = "?";
            if (!has_ctl) {
                if (read_name_from_procfs(card, dev_idx, streams[si] == 0,
                                           fallback_name,
                                           sizeof(fallback_name)) < 0)
                    continue;  /* 该设备无此方向 */
            }

            const char *dev_name = has_ctl ? (const char *)info.name
                                           : fallback_name;
            const char *type = classify_dev(dev_name);

            /* 颜色 */
            const char *color;
            if (strstr(type, "HDMI"))       color = BRIGHT_YELLOW_COLOR_PRINT;
            else if (si == 1)               color = BRIGHT_GREEN_COLOR_PRINT;
            else                            color = BRIGHT_CYAN_COLOR_PRINT;

            __printf("\n");
            PRINT_COLOR(color,
                "pcmC%dD%d%c  %s  %s  [%s]\n",
                info.card, info.device, stream_ch[si],
                stream_str[si], dev_name, type);

            /* 打开 PCM 节点做 HW_REFINE */
            char path[48];
            snprintf(path, sizeof(path), "/dev/snd/pcmC%dD%d%c",
                     info.card, info.device, stream_ch[si]);
            int pcm_fd = __openat(AT_FDCWD, path,
                                  O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);

            if (pcm_fd == -EBUSY) {
                __printf("  状态: 被占用 (EBUSY)\n");
            } else if (pcm_fd < 0) {
                __printf("  状态: 无法打开 (errno=%d)\n", -pcm_fd);
            } else {
                __printf("  状态: 可用\n");
                probe_caps(pcm_fd);
                __close(pcm_fd);
            }
            found++;
        }
    }

    if (use_ctl) __close(ctl_fd);

    if (!found) __printf("未发现 PCM 设备\n");

    __printf("\n══════════════════════════════════════════════\n");
    __printf("判别依据:\n");
    __printf("  • 设备名称含 \"Analog\" → 模拟输出（接耳机/扬声器）\n");
    __printf("  • 设备名称含 \"HDMI\"   → 数字输出（接显示器）\n");
    __printf("  • 支持 MULTI 多重打开    → HDMI/数字接口特性\n");
    __printf("  • 支持 AC3 格式         → 仅数字接口\n");
    __printf("\n");
    return 0;
}
