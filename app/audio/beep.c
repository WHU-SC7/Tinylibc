/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * beep — ALSA PCM 直写蜂鸣测试
 *
 * 机制：相位累加生成 1 秒 440Hz 正弦波 → 通过 ALSA 直接写入 /dev/snd/pcmC*D*p。
 * 不依赖文件系统（波形在内存中生成），用于验证 ALSA 直写链路打通。
 *
 * 系统调用：openat, ioctl, write, close
 *
 * 用法：
 *   beep              # 播放 440Hz 蜂鸣（默认设备）
 *   beep /dev/snd/pcmC0D0p  # 指定设备节点
 *   pasuspender -- beep # 在 PulseAudio 环境下需先挂起 PA 再执行
 *
 * 索引：
 *   main             开设备 → 生成波形 → 配置 → 写入 → 关闭
 *     gen_sine        相位累加正弦波生成
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "math.h"
#include "string.h"
#include "errno.h"

#define SAMPLE_RATE   44100
#define CHANNELS      2
#define BITS          16
#define DURATION_SEC  1
#define FREQ_HZ       440
#define TOTAL_FRAMES  (SAMPLE_RATE * DURATION_SEC)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * gen_sine — 填充 16-bit 立体声正弦波缓冲区
 *
 * buf:    输出缓冲区（帧数为 TOTAL_FRAMES × 2 个 int16_t）
 * freq:   频率（Hz）
 * rate:   采样率
 * frames: 帧数
 */
static void gen_sine(short *buf, double freq, unsigned int rate,
                     unsigned long frames)
{
    double phase = 0.0;
    double step  = 2.0 * M_PI * freq / rate;
    unsigned long i;

    for (i = 0; i < frames; i++) {
        short sample = (short)(sin(phase) * 30000.0);
        buf[i * 2]     = sample;  /* 左声道 */
        buf[i * 2 + 1] = sample;  /* 右声道 */
        phase += step;
        if (phase >= 2.0 * M_PI)
            phase -= 2.0 * M_PI;
    }
}

static void print_error(const char *msg, int err)
{
    char buf[128];
    strcpy(buf, "beep: ");
    strcat(buf, msg);
    PRINT_COLOR(RED_COLOR_PRINT, "%s (errno=%d)\n", buf, -err);
}

int main(int argc, char **argv)
{
    struct tlibc_pcm pcm;
    const char *dev = NULL;
    short *buf;
    int ret;

    /* ── 参数 ── */
    if (argc > 1)
        dev = argv[1];

    PRINT_COLOR(GREEN_COLOR_PRINT, "beep: 正在打开 PCM 设备...\n");

    ret = tlibc_pcm_open(&pcm, dev, CHANNELS, SAMPLE_RATE, BITS);
    if (ret < 0) {
        print_error("无法打开 PCM 设备", ret);
        return 1;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "beep: 已打开设备，配置 %dHz/%dbit/%dch...\n",
                SAMPLE_RATE, BITS, CHANNELS);

    ret = tlibc_pcm_configure(&pcm);
    if (ret < 0) {
        print_error("PCM 配置失败", ret);
        tlibc_pcm_close(&pcm);
        return 1;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "beep: 已配置，缓冲区=%lu 帧\n", pcm.buffer_size);

    /* ── 生成波形 ── */
    buf = (short *)tlibc_malloc(TOTAL_FRAMES * pcm.frame_size);
    if (!buf) {
        print_error("内存分配失败", -ENOMEM);
        tlibc_pcm_close(&pcm);
        return 1;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "beep: 生成 %dHz 正弦波 %d 秒...\n", FREQ_HZ, DURATION_SEC);
    gen_sine(buf, FREQ_HZ, SAMPLE_RATE, TOTAL_FRAMES);

    /* ── 播放 ── */
    PRINT_COLOR(GREEN_COLOR_PRINT, "beep: 播放中...\n");

    ret = tlibc_pcm_write_all(&pcm, buf, TOTAL_FRAMES);
    if (ret < 0) {
        print_error("播放失败", ret);
        tlibc_free(buf);
        tlibc_pcm_close(&pcm);
        return 1;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT, "beep: 播放完成 ✓\n");

    /* ── 清理 ── */
    tlibc_free(buf);
    tlibc_pcm_close(&pcm);
    return 0;
}
