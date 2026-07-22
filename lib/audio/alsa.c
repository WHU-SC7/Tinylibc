/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * alsa.c — ALSA PCM 直写库
 *
 * 机制：open /dev/snd/pcmC0D0p → ioctl HW_PARAMS → SW_PARAMS → PREPARE
 *       → write() PCM 数据 → DRAIN → close
 * 不依赖 libasound，不经过 PulseAudio/OSS。
 *
 * 系统调用：openat, close, ioctl, write, getdents64
 *
 * 索引：
 *   tlibc_pcm_open      打开设备（支持 fallback 扫描）
 *     scan_pcm_device   扫描 /dev/snd/ 寻找 pcmC*D*p 设备
 *   tlibc_pcm_configure HW_PARAMS + SW_PARAMS + PREPARE
 *   tlibc_pcm_write     阻塞写入（内含部分写入重试）
 *   tlibc_pcm_drain     等待播放完成
 *   tlibc_pcm_close     关闭设备
 */

#include "tlibc_everything.h"
#include "linux_audio.h"
#include "dirent.h"
#include "errno.h"

/* ------------------------------------------------------------------ */
/*  内部辅助                                                          */
/* ------------------------------------------------------------------ */

/* PCM 设备路径格式：/dev/snd/pcmC<card>D<device>p */
#define PCM_PATH_MAX      64
#define PCM_SCAN_BUF_SIZE 4096

/* 检查文件名是否以 "pcm" 开头并以 'p' 结尾 */
static int is_pcm_playback(const char *name)
{
    int len;
    if (!name)
        return 0;
    len = strlen(name);
    return (len > 4 && name[0] == 'p' && name[1] == 'c' &&
            name[2] == 'm' && name[len - 1] == 'p');
}

/* 尝试打开指定路径，成功返回 fd，失败返回负 errno */
static int try_open_dev(const char *path)
{
    int fd = __openat(AT_FDCWD, path, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
    if (fd < 0) {
        /* openat 返回负 errno，直接传递 */
        return fd;
    }
    return fd;
}

/* 扫描 /dev/snd/ 寻找第一个 pcmC*D*p 设备，返回 fd */
static int scan_pcm_device(void)
{
    char path[PCM_PATH_MAX];
    unsigned char buf[PCM_SCAN_BUF_SIZE];
    struct linux_dirent64 *entry;
    int fd, nread, offset;

    fd = __openat(AT_FDCWD, "/dev/snd", O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0)
        return fd;  /* -ENOENT etc */

    nread = __getdents64(fd, (struct linux_dirent64 *)buf, PCM_SCAN_BUF_SIZE);
    __close(fd);

    if (nread < 0)
        return nread;

    offset = 0;
    while (offset < nread) {
        entry = (struct linux_dirent64 *)(buf + offset);
        if (entry->d_reclen == 0)
            break;

        if (is_pcm_playback(entry->d_name)) {
            /* 构造完整路径：/dev/snd/<name> */
            int plen = 9 + strlen(entry->d_name);  /* "/dev/snd/" + name */
            if (plen < PCM_PATH_MAX) {
                strcpy(path, "/dev/snd/");
                strcat(path, entry->d_name);
                int pfd = try_open_dev(path);
                if (pfd >= 0)
                    return pfd;
                /* 打开失败继续尝试下一个 */
            }
        }
        offset += entry->d_reclen;
    }

    return -ENOENT;
}

/* ------------------------------------------------------------------ */
/*  public API                                                         */
/* ------------------------------------------------------------------ */

/*
 * tlibc_pcm_open — 打开 PCM 播放设备
 *
 * dev 为 NULL 时自动扫描 /dev/snd/。
 * 成功返回 0，pcm 结构体被填充；失败返回负 errno。
 */
int tlibc_pcm_open(struct tlibc_pcm *pcm, const char *dev,
                   unsigned int channels, unsigned int rate,
                   unsigned int bits)
{
    int fd;

    if (!pcm)
        return -EINVAL;

    if (dev) {
        fd = try_open_dev(dev);
    } else {
        /* 1. 尝试默认路径 */
        fd = try_open_dev("/dev/snd/pcmC0D0p");
        /* 2. 失败则扫描 */
        if (fd < 0)
            fd = scan_pcm_device();
    }

    if (fd < 0)
        return fd;

    pcm->fd             = fd;
    pcm->channels       = channels;
    pcm->rate           = rate;
    pcm->bits_per_sample = bits;
    pcm->frame_size     = (bits / 8) * channels;
    pcm->buffer_size    = 0;   /* 由 HW_PARAMS 填充 */

    return 0;
}

/*
 * tlibc_pcm_configure — HW_PARAMS + SW_PARAMS + PREPARE
 *
 * 成功返回 0，失败返回负 errno。
 */
int tlibc_pcm_configure(struct tlibc_pcm *pcm)
{
    struct snd_pcm_hw_params hw;
    struct snd_pcm_sw_params sw;
    int fd = pcm->fd;
    int ret;
    int access_idx = SND_MASK_IDX(SNDRV_PCM_HW_PARAM_ACCESS);
    int fmt_idx    = SND_MASK_IDX(SNDRV_PCM_HW_PARAM_FORMAT);
    int sub_idx    = SND_MASK_IDX(SNDRV_PCM_HW_PARAM_SUBFORMAT);
    int ch_idx     = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_CHANNELS);
    int rate_idx   = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_RATE);

    /* ── 初始化：版本协商与时间戳配置 ── */
    {
        int ver = 0;  /* 忽略内核版本号 */
        ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_PVERSION, &ver);
        if (ret < 0) return ret;

        /* 告知内核使用当前协议版本 */
        ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_USER_PVERSION, &ver);
        if (ret < 0) return ret;

        /* 禁用时间戳 */
        int tstamp = 0;
        ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_TTSTAMP, &tstamp);
        if (ret < 0) return ret;
    }

    /* ── HW_PARAMS ── */

    /* 正确初始化 hw 参数以获取硬件能力 */
    memset(&hw, 0, sizeof(hw));

    /* 1. masks 初始化为全 1（允许所有可选值） */
    for (int i = 0; i < 3; i++) {
        memset(&hw.masks[i], 0xFF, sizeof(hw.masks[i]));
    }

    /* 2. intervals 初始化为 [0, UINT_MAX]（允许任意范围） */
    for (int i = 0; i < 12; i++) {
        hw.intervals[i].min = 0;
        hw.intervals[i].max = 0xFFFFFFFF;
        hw.intervals[i].openmin = 0;
        hw.intervals[i].openmax = 0;
        hw.intervals[i].integer = 0;
        hw.intervals[i].empty = 0;
    }

    hw.rmask = ~0U;
    hw.cmask = 0;
    hw.info = ~0U;

    /* 先用 HW_REFINE 获取硬件支持的全部值 */
    ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw);
    if (ret < 0)
        return ret;

    /* 适配采样率：若请求的 rate 不在硬件支持范围内，自动取最近值 */
    if (pcm->rate < hw.intervals[rate_idx].min ||
        pcm->rate > hw.intervals[rate_idx].max) {
        unsigned int orig = pcm->rate;
        if (hw.intervals[rate_idx].min == hw.intervals[rate_idx].max) {
            pcm->rate = hw.intervals[rate_idx].min;
        } else if (pcm->rate < hw.intervals[rate_idx].min) {
            pcm->rate = hw.intervals[rate_idx].min;
        } else {
            pcm->rate = hw.intervals[rate_idx].max;
        }
        PRINT_COLOR(YELLOW_COLOR_PRINT,
                    "alsa: 采样率 %uHz 不受支持，使用 %uHz\n", orig, pcm->rate);
    }

    /* 清空 mask 并设置我们需要的 */
    snd_mask_reset(&hw.masks[access_idx]);
    snd_mask_set(&hw.masks[access_idx], SNDRV_PCM_ACCESS_RW_INTERLEAVED);

    snd_mask_reset(&hw.masks[fmt_idx]);
    snd_mask_set(&hw.masks[fmt_idx],    SNDRV_PCM_FORMAT_S16_LE);

    snd_mask_reset(&hw.masks[sub_idx]);
    snd_mask_set(&hw.masks[sub_idx],    SNDRV_PCM_SUBFORMAT_STD);

    /* 限制 interval 为我们需要的值 */
    snd_interval_set(&hw.intervals[ch_idx],   pcm->channels);
    snd_interval_set(&hw.intervals[rate_idx], pcm->rate);

    /* 显式设置 sample_bits=16, frame_bits=channels×16 */
    {
        int sb_idx = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
        int fb_idx = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_FRAME_BITS);
        snd_interval_set(&hw.intervals[sb_idx], pcm->bits_per_sample);
        snd_interval_set(&hw.intervals[fb_idx],
                         pcm->bits_per_sample * pcm->channels);
    }

    hw.rmask = ~0U;

    ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw);
    if (ret < 0)
        return ret;

    /* 从内核返回的结果中读取实际 buffer_size */
    {
        int bs_idx = SND_INTERVAL_IDX(SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
        pcm->buffer_size = hw.intervals[bs_idx].max;
    }

    /* ── SW_PARAMS ── */

    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode      = 0;
    sw.period_step      = 1;
    sw.sleep_min        = 0;
    sw.avail_min        = 1;                       /* 有空间即唤醒 */
    sw.xfer_align       = 1;
    sw.start_threshold  = 1;                       /* 有数据即启动 */
    sw.stop_threshold   = 0x7FFFFFFFFFFFFFFFUL;    /* 几乎不自动停止 */
    sw.silence_threshold = 0;
    sw.silence_size     = 0;
    sw.boundary         = 0x7FFFFFFFFFFFFFFFUL;
    sw.proto            = 0;
    sw.tstamp_type      = 0;

    ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw);
    if (ret < 0)
        return ret;

    /* ── PREPARE ── */

    ret = (int)__ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, 0);
    if (ret < 0)
        return ret;

    /* 配置完成后切回阻塞模式（open 时用了 O_NONBLOCK），使 write 自然阻塞 */
    __fcntl(fd, F_SETFL, O_RDWR | O_CLOEXEC);

    return 0;
}

/*
 * tlibc_pcm_write — 写入 PCM 数据
 *
 * 阻塞写入 frames 帧。内含部分写入重试循环。
 * 成功返回写入的帧数，失败返回负 errno。
 */
long tlibc_pcm_write(struct tlibc_pcm *pcm, const void *buf, unsigned long frames)
{
    int fd           = pcm->fd;
    int frame_size   = pcm->frame_size;
    unsigned long remain = frames;
    const unsigned char *ptr = (const unsigned char *)buf;

    while (remain > 0) {
        ssize_t written = __write(fd, ptr, (int)(remain * frame_size));
        if (written < 0) {
            /* EPIPE = underrun，尝试恢复 */
            if (written == -EPIPE) {
                /* 重新 PREPARE */
                __ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, 0);
                continue;
            }
            /* EAGAIN = 非阻塞模式下缓冲区满，等待 POLLOUT 后重试 */
            if (written == -EAGAIN) {
                struct pollfd pfd;
                pfd.fd     = fd;
                pfd.events = POLLOUT;
                __poll(&pfd, 1, -1);  /* 无限等待直至可写 */
                continue;
            }
            return (long)written;
        }
        if (written == 0)
            break;  /* 不应发生 */

        unsigned long frames_written = (unsigned long)written / frame_size;
        remain -= frames_written;
        ptr    += frames_written * frame_size;
    }

    return (long)(frames - remain);
}

/*
 * tlibc_pcm_drain — 等待播放完成
 *
 * 成功后流进入 DRAINING 状态。
 */
int tlibc_pcm_drain(struct tlibc_pcm *pcm)
{
    return (int)__ioctl(pcm->fd, SNDRV_PCM_IOCTL_DRAIN, 0);
}

/*
 * tlibc_pcm_drop — 立即停止播放
 */
int tlibc_pcm_drop(struct tlibc_pcm *pcm)
{
    return (int)__ioctl(pcm->fd, SNDRV_PCM_IOCTL_DROP, 0);
}

/*
 * tlibc_pcm_close — 关闭设备
 */
void tlibc_pcm_close(struct tlibc_pcm *pcm)
{
    if (pcm && pcm->fd >= 0) {
        __close(pcm->fd);
        pcm->fd = -1;
    }
}

/*
 * tlibc_pcm_write_all — 简化接口：一次性写入并 drain
 *
 * 等效于 tlibc_pcm_write() + tlibc_pcm_drain()。
 * 成功返回 0，失败返回负 errno。
 */
int tlibc_pcm_write_all(struct tlibc_pcm *pcm, const void *buf,
                        unsigned long frames)
{
    long ret = tlibc_pcm_write(pcm, buf, frames);
    if (ret < 0)
        return (int)ret;
    if ((unsigned long)ret < frames)
        return -EIO;    /* 未写完 */

    return tlibc_pcm_drain(pcm);
}
