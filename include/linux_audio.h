/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * linux_audio.h — Linux ALSA 内核 UAPI 最小子集
 *
 * 来源：Linux 内核 UAPI <sound/asound.h>，精简到 PCM 播放必要部分。
 * 结构体布局针对 x86_64（unsigned long = 8 字节）。
 */

#ifndef __LINUX_AUDIO_H
#define __LINUX_AUDIO_H

#include "tlibc_types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  PCM 流状态
 * ══════════════════════════════════════════════════════════════════════ */

#define SNDRV_PCM_STATE_OPEN       0
#define SNDRV_PCM_STATE_PREPARED   2
#define SNDRV_PCM_STATE_RUNNING    3
#define SNDRV_PCM_STATE_XRUN       4
#define SNDRV_PCM_STATE_DRAINING   5
#define SNDRV_PCM_STATE_SUSPENDED  7

/* ══════════════════════════════════════════════════════════════════════
 *  Access / Format / Subformat 常量
 * ══════════════════════════════════════════════════════════════════════ */

#define SNDRV_PCM_ACCESS_RW_INTERLEAVED     3
#define SNDRV_PCM_ACCESS_RW_NONINTERLEAVED  4

#define SNDRV_PCM_FORMAT_S16_LE     2
#define SNDRV_PCM_FORMAT_U8         1
#define SNDRV_PCM_FORMAT_S24_LE     6

#define SNDRV_PCM_SUBFORMAT_STD     0

/* ══════════════════════════════════════════════════════════════════════
 *  struct snd_mask / snd_interval — 参数描述基础类型
 * ══════════════════════════════════════════════════════════════════════ */

/* snd_mask 是 256 位位图（8 × uint32_t），表示离散可选值集合 */
struct snd_mask {
    uint32_t bits[8];
};

/* snd_interval 表示连续范围 [min, max] */
struct snd_interval {
    uint32_t min;
    uint32_t max;
    uint32_t openmin:1;
    uint32_t openmax:1;
    uint32_t integer:1;
    uint32_t empty:1;
};

/* ── mask 辅助 ── */

static inline void snd_mask_set(struct snd_mask *mask, int bit)
{
    mask->bits[bit >> 5] |= 1U << (bit & 31);
}

static inline int snd_mask_test(const struct snd_mask *mask, int bit)
{
    return (mask->bits[bit >> 5] >> (bit & 31)) & 1;
}

static inline void snd_mask_reset(struct snd_mask *mask)
{
    int i;
    for (i = 0; i < 8; i++)
        mask->bits[i] = 0;
}

/* ── interval 辅助 ── */

static inline void snd_interval_set(struct snd_interval *iv, unsigned int val)
{
    iv->min = val;
    iv->max = val;
    iv->openmin = 0;
    iv->openmax = 0;
    iv->integer = 1;
    iv->empty = 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  HW 参数索引（各值在 masks[] / intervals[] 中的位置）
 * ══════════════════════════════════════════════════════════════════════ */

#define SNDRV_PCM_HW_PARAM_ACCESS       0   /* 掩码 */
#define SNDRV_PCM_HW_PARAM_FORMAT       1   /* 掩码 */
#define SNDRV_PCM_HW_PARAM_SUBFORMAT    2   /* 掩码 */
#define SNDRV_PCM_HW_PARAM_FIRST_MASK   0
#define SNDRV_PCM_HW_PARAM_LAST_MASK    2

#define SNDRV_PCM_HW_PARAM_SAMPLE_BITS      8
#define SNDRV_PCM_HW_PARAM_FRAME_BITS       9
#define SNDRV_PCM_HW_PARAM_CHANNELS        10
#define SNDRV_PCM_HW_PARAM_RATE            11
#define SNDRV_PCM_HW_PARAM_PERIOD_TIME     12
#define SNDRV_PCM_HW_PARAM_PERIOD_SIZE     13
#define SNDRV_PCM_HW_PARAM_BUFFER_SIZE     17
#define SNDRV_PCM_HW_PARAM_FIRST_INTERVAL   8
#define SNDRV_PCM_HW_PARAM_LAST_INTERVAL   19

/* 将参数索引映射到数组下标 */
#define SND_MASK_IDX(p)  ((p) - SNDRV_PCM_HW_PARAM_FIRST_MASK)
#define SND_INTERVAL_IDX(p) ((p) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL)

/* ══════════════════════════════════════════════════════════════════════
 *  struct snd_pcm_hw_params — 硬件参数
 *
 *  大小 608 字节（x86_64, Linux UAPI）。
 *  标准层：masks[3] + mres[5] + intervals[12] + ires[9]
 *  布局（偏移量）：
 *     0    flags            uint32_t
 *     4    masks[3]         3 × 32 = 96B
 *   100    mres[5]          5 × 32 = 160B
 *   260    intervals[12]   12 × 12 = 144B
 *   404    ires[9]          9 × 12 = 108B
 *   512    rmask            uint32_t
 *   516    cmask            uint32_t
 *   520    info             uint32_t
 *   524    msbits           uint32_t
 *   528    rate_num         uint32_t
 *   532    rate_den         uint32_t
 *   536    fifo_size        unsigned long (8B)
 *   544    reserved[64]
 *   608    总大小
 * ══════════════════════════════════════════════════════════════════════ */

struct snd_pcm_hw_params {
    uint32_t       flags;
    struct snd_mask masks[3];         /* ACCESS, FORMAT, SUBFORMAT */
    struct snd_mask mres[5];          /* 保留掩码区 */
    struct snd_interval intervals[12]; /* SAMPLE_BITS 至 TICK_TIME */
    struct snd_interval ires[9];      /* 保留区间区 */
    uint32_t       rmask;
    uint32_t       cmask;
    uint32_t       info;
    uint32_t       msbits;
    uint32_t       rate_num;
    uint32_t       rate_den;
    unsigned long  fifo_size;         /* snd_pcm_uframes_t */
    unsigned char  reserved[64];
} __attribute__((aligned(4)));

/* ══════════════════════════════════════════════════════════════════════
 *  struct snd_pcm_sw_params — 软件参数
 *
 *  大小 136 字节（x86_64）。
 *   0    tstamp_mode      int32_t
 *   4    period_step      uint32_t
 *   8    sleep_min        uint32_t
 *  12    [padding 4B]
 *  16    avail_min        unsigned long (8B)
 *  24    xfer_align       unsigned long (8B)
 *  32    start_threshold  unsigned long (8B)
 *  40    stop_threshold   unsigned long (8B)
 *  48    silence_threshold unsigned long (8B)
 *  56    silence_size     unsigned long (8B)
 *  64    boundary         unsigned long (8B)
 *  72    proto            uint32_t
 *  76    tstamp_type      uint32_t
 *  80    reserved[56]
 * 136    总大小
 * ══════════════════════════════════════════════════════════════════════ */

struct snd_pcm_sw_params {
    int            tstamp_mode;       /* 时间戳模式 */
    unsigned int   period_step;
    unsigned int   sleep_min;         /* 最小休眠 tick */
    unsigned long  avail_min;         /* 唤醒最小可用帧数 */
    unsigned long  xfer_align;        /* 已废弃 */
    unsigned long  start_threshold;   /* 自动启动阈值 */
    unsigned long  stop_threshold;    /* 自动停止阈值 */
    unsigned long  silence_threshold; /* 静音填充阈值 */
    unsigned long  silence_size;      /* 静音块大小 */
    unsigned long  boundary;          /* 指针环绕点 */
    unsigned int   proto;             /* 协议版本 */
    unsigned int   tstamp_type;       /* 时间戳类型 */
    unsigned char  reserved[56];
} __attribute__((aligned(4)));

/* ══════════════════════════════════════════════════════════════════════
 *  struct snd_xferi — 帧传输描述符（用于 WRITEI_FRAMES）
 *
 *  大小 24 字节（x86_64）。
 *   0    result           long
 *   8    buf              void *
 *  16    frames           unsigned long
 * ══════════════════════════════════════════════════════════════════════ */

struct snd_xferi {
    long           result;
    void          *buf;
    unsigned long  frames;
};

/* ══════════════════════════════════════════════════════════════════════
 *  流方向
 * ══════════════════════════════════════════════════════════════════════ */

#define SNDRV_PCM_STREAM_PLAYBACK   0
#define SNDRV_PCM_STREAM_CAPTURE    1

/* ══════════════════════════════════════════════════════════════════════
 *  struct snd_pcm_info — PCM 设备信息（通过 SNDRV_PCM_IOCTL_INFO
 *  或 SNDRV_CTL_IOCTL_PCM_INFO 查询）
 *
 *  大小 288 字节（x86_64），包含设备名称、ID、同步 ID、子设备计数等。
 *  注意：无需打开 PCM 设备也可通过 controlC* 查询（不会因 EBUSY 受阻）
 *
 *   0    device           uint32_t
 *   4    subdevice        uint32_t
 *   8    stream           int32_t
 *  12    card             int32_t
 *  16    id[64]           unsigned char
 *  80    name[80]         unsigned char
 * 160    subname[32]      unsigned char
 * 192    dev_class        int32_t
 * 196    dev_subclass     int32_t
 * 200    subdevices_count uint32_t
 * 204    subdevices_avail uint32_t
 * 208    sync             union snd_pcm_sync_id (16B)
 * 224    reserved[64]     unsigned char
 * 288    总大小
 * ══════════════════════════════════════════════════════════════════════ */

union snd_pcm_sync_id {
    unsigned char  id[16];
    unsigned short id16[8];
    unsigned int   id32[4];
};

struct snd_pcm_info {
    uint32_t       device;
    uint32_t       subdevice;
    int            stream;          /* SNDRV_PCM_STREAM_* */
    int            card;
    unsigned char  id[64];
    unsigned char  name[80];
    unsigned char  subname[32];
    int            dev_class;
    int            dev_subclass;
    uint32_t       subdevices_count;
    uint32_t       subdevices_avail;
    union snd_pcm_sync_id sync;     /* 硬件同步 ID */
    unsigned char  reserved[64];
} __attribute__((aligned(4)));

/* PCM 信息 flags（在 snd_pcm_hw_params.info 中出现，非 pcm_info 结构体） */
#define SNDRV_PCM_INFO_MULTI           0x10000000  /* 支持多重打开 */

/* ══════════════════════════════════════════════════════════════════════
 *  PCM IOCTL 命令
 *
 *  _IOC 编码：dir(2bit) | type(8bit) | nr(8bit) | size(14bit)
 *  dir:  0=_IO, 1=_IOW, 2=_IOR, 3=_IOWR
 *  type: 'A' = 0x41（PCM）、'U' = 0x55（Control）
 *  nr:   命令序号
 *  size: sizeof(struct)，编码在 ioctl 号中以兼容新旧版本
 * ══════════════════════════════════════════════════════════════════════ */

/* _IOC 构造器（Linux asm-generic/ioctl.h 兼容） */
#define __ALSA_IOC(dir, type, nr, size) \
    (((dir)  << 30) | ((type) << 8) | ((nr) << 0) | \
     ((unsigned int)(size) << 16))

#define __ALSA_IO(type, nr)          __ALSA_IOC(0, type, nr, 0)
#define __ALSA_IOW(type, nr, size)   __ALSA_IOC(1, type, nr, \
                                     (unsigned int)(size))
#define __ALSA_IOR(type, nr, size)   __ALSA_IOC(2, type, nr, \
                                     (unsigned int)(size))
#define __ALSA_IOWR(type, nr, size)  __ALSA_IOC(3, type, nr, \
                                     (unsigned int)(size))

/* 基础操作 */
#define SNDRV_PCM_IOCTL_PVERSION     __ALSA_IOR('A', 0x00, sizeof(int))
#define SNDRV_PCM_IOCTL_USER_PVERSION __ALSA_IOW('A', 0x02, sizeof(int))
#define SNDRV_PCM_IOCTL_TTSTAMP      __ALSA_IOW('A', 0x03, sizeof(int))
#define SNDRV_PCM_IOCTL_PREPARE      __ALSA_IO('A', 0x40)
#define SNDRV_PCM_IOCTL_RESET        __ALSA_IO('A', 0x41)
#define SNDRV_PCM_IOCTL_START        __ALSA_IO('A', 0x42)
#define SNDRV_PCM_IOCTL_DROP         __ALSA_IO('A', 0x43)
#define SNDRV_PCM_IOCTL_DRAIN        __ALSA_IO('A', 0x44)

/* 参数配置 */
#define SNDRV_PCM_IOCTL_INFO      __ALSA_IOR('A', 0x01, \
                                     sizeof(struct snd_pcm_info))
/* 注意：SNDRV_PCM_IOCTL_INFO 需要传一个打开的 PCM fd，
   对 EBUSY 设备无效。如需查询被占用的设备名称，
   应通过 SNDRV_CTL_IOCTL_PCM_INFO（/dev/snd/controlC*）。 */
#define SNDRV_PCM_IOCTL_HW_REFINE  __ALSA_IOWR('A', 0x10, \
                                     sizeof(struct snd_pcm_hw_params))
#define SNDRV_PCM_IOCTL_HW_PARAMS  __ALSA_IOWR('A', 0x11, \
                                     sizeof(struct snd_pcm_hw_params))
#define SNDRV_PCM_IOCTL_SW_PARAMS  __ALSA_IOWR('A', 0x13, \
                                     sizeof(struct snd_pcm_sw_params))

/* 帧传输 */
#define SNDRV_PCM_IOCTL_WRITEI_FRAMES  __ALSA_IOW('A', 0x50, \
                                         sizeof(struct snd_xferi))

/* ══════════════════════════════════════════════════════════════════════
 *  Control 设备 IOCTL — 用于查询 PCM 设备信息，无需打开 PCM 节点
 *
 *  type: 'U' = 0x55，/dev/snd/controlC* 永不 busy
 *
 *  可用性：这些 ioctl 在 6.8 内核中存在，但并非在所有内核版本
 *  都可用。若返回 ENOTTY，需回退到 /proc/asound/ 读取设备名称。
 * ══════════════════════════════════════════════════════════════════════ */

/* 枚举下一个 PCM 设备号。传入 -1 获取第一个，返回下一个设备号 */
#define SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE  __ALSA_IOR('U', 0x30, \
                                             sizeof(int))
/* 获取指定 PCM 设备的详细信息（名称、流向、子设备数等） */
#define SNDRV_CTL_IOCTL_PCM_INFO        __ALSA_IOWR('U', 0x31, \
                                             sizeof(struct snd_pcm_info))

/* ══════════════════════════════════════════════════════════════════════
 *  PCM 描述符（上层应用使用）
 * ══════════════════════════════════════════════════════════════════════ */

struct tlibc_pcm {
    int             fd;              /* 设备 fd */
    unsigned int    channels;        /* 声道数 */
    unsigned int    rate;            /* 采样率 */
    unsigned int    bits_per_sample; /* 位深 */
    unsigned int    frame_size;      /* 每帧字节数 */
    unsigned long   buffer_size;     /* 缓冲区大小（帧） */
};

/* ── PCM API 声明 ── */

int  tlibc_pcm_open(struct tlibc_pcm *pcm, const char *dev,
                     unsigned int channels, unsigned int rate,
                     unsigned int bits);
int  tlibc_pcm_configure(struct tlibc_pcm *pcm);
long tlibc_pcm_write(struct tlibc_pcm *pcm, const void *buf,
                      unsigned long frames);
int  tlibc_pcm_write_all(struct tlibc_pcm *pcm, const void *buf,
                          unsigned long frames);
int  tlibc_pcm_drain(struct tlibc_pcm *pcm);
int  tlibc_pcm_drop(struct tlibc_pcm *pcm);
void tlibc_pcm_close(struct tlibc_pcm *pcm);

#endif /* __LINUX_AUDIO_H */
