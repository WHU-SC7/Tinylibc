/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * beep — ALSA PCM 直写蜂鸣测试
 *
 * 机制：相位累加生成 1 秒 440Hz 正弦波 → 通过 ALSA 直接写入 /dev/snd/pcmC*D*p。
 * 不依赖文件系统（波形在内存中生成），用于验证 ALSA 直写链路打通。
 *
 * 自引用 fallback：若 PulseAudio 占用 PCM 设备（-EBUSY），
 * 则自动通过 execve 调用 pasuspender 挂起 PA 后重新执行自身。
 * 通过 _BEEP_PASUSPENDER 环境变量防止递归循环。
 *
 * 系统调用：openat, ioctl, write, close, execve
 *
 * 用法：
 *   beep              # 播放 440Hz 蜂鸣（默认设备）
 *   beep /dev/snd/pcmC0D0p  # 指定设备节点
 *
 * 索引：
 *   main             开设备 → 生成波形 → 配置 → 写入 → 关闭
 *     gen_sine        相位累加正弦波生成
 *     try_pasuspender_fallback  EBUSY → execve pasuspender 重新执行
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

/*
 * is_pulseaudio_active — 检测 PulseAudio 是否在运行
 *
 * 检查 PulseAudio 的 Unix 域 socket 是否存在。
 * PulseAudio 默认在 /run/user/<uid>/pulse/native 创建监听 socket。
 */
static int is_pulseaudio_active(void)
{
    char path[64];
    int uid = (int)getuid();
    int len = snprintf(path, sizeof(path),
                        "/run/user/%d/pulse/native", uid);
    if (len < 0 || (unsigned long)len >= sizeof(path))
        return 0;

    int fd = __openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    __close(fd);
    return 1;
}

/*
 * try_pasuspender_fallback — 通过 pasuspender 重新执行自身
 *
 * 构造 argv 和 envp，通过 execve 调用 /usr/bin/pasuspender
 * 挂起 PulseAudio 后重新执行自身。
 *
 * _BEEP_PASUSPENDER=1 环境变量作为递归防护标记：
 * 若该变量已存在（已在 pasuspender 中），则不再递归调用。
 *
 * 此函数仅在 execve 失败时返回（pasuspender 未安装等），
 * 成功时不返回（当前进程被完全替换）。
 */
static void try_pasuspender_fallback(int argc, char **argv)
{
    /* 递归防护：已在 pasuspender 下则不再重试 */
    char *flag = get_env_var(global_envp, "_BEEP_PASUSPENDER");
    if (flag && flag[0] == '1')
        return;

    /* 检查 /usr/bin/pasuspender 是否存在 */
    int fd = __openat(AT_FDCWD, "/usr/bin/pasuspender",
                      O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return;
    __close(fd);

    PRINT_COLOR(YELLOW_COLOR_PRINT,
                "beep: 设备被 PulseAudio 占用，通过 pasuspender 重试...\n");

    /* ── 构建新 argv ── */
    int new_argc = argc + 2;  /* + "pasuspender" + "--" */
    char **new_argv = tlibc_malloc((unsigned long)(new_argc + 1)
                                   * sizeof(char *));
    if (!new_argv) return;

    new_argv[0] = "/usr/bin/pasuspender";
    new_argv[1] = "--";
    new_argv[2] = argv[0];
    for (int i = 1; i < argc; i++)
        new_argv[i + 2] = argv[i];
    new_argv[new_argc] = NULL;

    /* ── 构建新 envp（追加 _BEEP_PASUSPENDER=1）── */
    int env_count = tlibc_envp_count(global_envp);
    char **new_envp = tlibc_malloc((unsigned long)(env_count + 2)
                                   * sizeof(char *));
    if (!new_envp) {
        tlibc_free(new_argv);
        return;
    }

    for (int i = 0; i < env_count; i++)
        new_envp[i] = global_envp[i];

    /* 递归防护标记 */
    {
        static char beep_flag[] = "_BEEP_PASUSPENDER=1";
        new_envp[env_count] = beep_flag;
    }
    new_envp[env_count + 1] = NULL;

    /* ── execve：成功则当前进程完全替换 ── */
    {
        int ret = execve("/usr/bin/pasuspender", new_argv, new_envp);

        /* 执行到这里说明 execve 失败，清理后返回 */
        PRINT_COLOR(RED_COLOR_PRINT,
                    "beep: pasuspender 启动失败 (errno=%d)\n", -ret);
    }
    tlibc_free(new_argv);
    tlibc_free(new_envp);
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

    if (dev) {
        /* 用户指定了设备 → 直接打开 */
        ret = tlibc_pcm_open(&pcm, dev, CHANNELS, SAMPLE_RATE, BITS);
        if (ret == -EBUSY)
            try_pasuspender_fallback(argc, argv);
    } else {
        /* 先尝试主设备 /dev/snd/pcmC0D0p */
        ret = tlibc_pcm_open(&pcm, "/dev/snd/pcmC0D0p",
                             CHANNELS, SAMPLE_RATE, BITS);
        if (ret == -EBUSY) {
            /* 主设备被占用 → PulseAudio 可能导致 → pasuspender */
            try_pasuspender_fallback(argc, argv);
            /* pasuspender 失败 → 回退到扫描任何可用设备 */
            ret = tlibc_pcm_open(&pcm, NULL,
                                 CHANNELS, SAMPLE_RATE, BITS);
        } else if (ret < 0) {
            /* 主设备不存在或其他错误 → 直接扫描 */
            ret = tlibc_pcm_open(&pcm, NULL,
                                 CHANNELS, SAMPLE_RATE, BITS);
        }
        /* 若扫描成功但主设备被 PA 占用，仍尝试 pasuspender
           以通过主设备播放（而非替代设备） */
        if (ret == 0 && is_pulseaudio_active()) {
            int pri_fd = __openat(AT_FDCWD, "/dev/snd/pcmC0D0p",
                                  O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
            if (pri_fd == -EBUSY) {
                tlibc_pcm_close(&pcm);
                try_pasuspender_fallback(argc, argv);
                /* pasuspender 失败 → 重新扫描 */
                ret = tlibc_pcm_open(&pcm, NULL,
                                     CHANNELS, SAMPLE_RATE, BITS);
            } else if (pri_fd >= 0) {
                __close(pri_fd);
            }
        }
    }

    if (ret < 0) {
        /* 最终失败：EBUSY + PA 运行 → 再尝试一次 pasuspender */
        if (ret == -EBUSY || is_pulseaudio_active())
            try_pasuspender_fallback(argc, argv);
        print_error("无法打开 PCM 设备", ret);
        return 1;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "beep: 已打开设备，配置 %dHz/%dbit/%dch...\n",
                pcm.rate, BITS, CHANNELS);

    ret = tlibc_pcm_configure(&pcm);
    if (ret < 0) {
        /* configure 阶段也可能返回 EBUSY（设备已被 PA 锁定参数） */
        if (ret == -EBUSY || is_pulseaudio_active())
            try_pasuspender_fallback(argc, argv);
        print_error("PCM 配置失败", ret);
        tlibc_pcm_close(&pcm);
        return 1;
    }

    PRINT_COLOR(GREEN_COLOR_PRINT,
                "beep: 已配置，缓冲区=%lu 帧\n", pcm.buffer_size);

    /* ── 生成波形（使用实际协商的采样率）── */
    {
        unsigned long total_frames = pcm.rate * DURATION_SEC;
        buf = (short *)tlibc_malloc(total_frames * pcm.frame_size);
        if (!buf) {
            print_error("内存分配失败", -ENOMEM);
            tlibc_pcm_close(&pcm);
            return 1;
        }

        PRINT_COLOR(GREEN_COLOR_PRINT,
                    "beep: 生成 %dHz 正弦波 %lu 帧 (%uHz, %d 秒)...\n",
                    FREQ_HZ, total_frames, pcm.rate, DURATION_SEC);
        gen_sine(buf, FREQ_HZ, pcm.rate, total_frames);

        /* ── 播放 ── */
        PRINT_COLOR(GREEN_COLOR_PRINT, "beep: 播放中...\n");

        ret = tlibc_pcm_write_all(&pcm, buf, total_frames);
        if (ret < 0) {
            print_error("播放失败", ret);
            tlibc_free(buf);
            tlibc_pcm_close(&pcm);
            return 1;
        }

        PRINT_COLOR(GREEN_COLOR_PRINT, "beep: 播放完成 ✓\n");

        /* ── 清理 ── */
        tlibc_free(buf);
    }
    tlibc_pcm_close(&pcm);
    return 0;
}
