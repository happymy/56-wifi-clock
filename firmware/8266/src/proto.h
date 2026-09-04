#pragma once
/* 与 51 的 UART 协议层：帧 `AA 55 CMD LEN PAYLOAD CHK`，CHK=XOR(CMD,LEN,PAYLOAD..)。
   权威对齐 firmware/STC/src/main.c 与 plan/串口通信协议.md。
   本模块只做收发与镜像缓冲，命令语义处理在 main.cpp */

#include <Arduino.h>

#define CFG_LEN 13

/* 命令码（与 51 main.c 一致） */
#define CMD_REQ_TIME  0x01
#define CMD_HEARTBEAT 0x02
#define CMD_ENTER_AP  0x04
#define CMD_CD_CTRL   0x05   /* MCU→ESP: 倒计时控制(0=暂停/恢复,1=取消) */
#define CMD_SET_TIME  0x81
#define CMD_SET_CFG   0x82
#define CMD_NET_STAT  0x83
#define CMD_AP_READY  0x84   /* ESP→MCU(设计稿, 51 暂不解析) */
#define CMD_REQ_CFG   0x87
#define CMD_STA_IP    0x88
#define CMD_DISP_OVERRIDE 0x89
#define CMD_RING      0x8A   /* ESP→MCU: 响铃触发(mode1=报时短滴, 2/3=提醒/闹钟长鸣, 0=取消) */
#define CMD_BOOT      0x8F

/* DISP_OVERRIDE mode */
#define DO_MODE_FREE  0x00
#define DO_MODE_CD    0x01
#define DO_MODE_RING  0x02

/* NET_STAT 值（协议 §3）：0未连 1连接中 2已连 3已同步 */
#define STAT_NONE    0
#define STAT_CONNECT 1
#define STAT_LINKED  2
#define STAT_SYNCED  3

/* 配置镜像：51 是配置权威。上线 REQ_CFG 拉取填充；Web 改字节后整帧下行；
   主线程读字段直接索引（偏移见 plan/串口通信协议.md §5）。 */
extern uint8_t g_cfg[CFG_LEN];
extern volatile bool g_cfg_valid;   /* 已从 51 拉到当前值（镜像基底就绪） */

/* 逐字节喂入（main loop 从 Serial 读）；完整合法帧到达时调 proto_on_frame() */
void proto_rx(uint8_t b);

/* 一帧完整到达：各命令语义由实现者处理 */
void proto_on_frame(uint8_t cmd, const uint8_t *p, uint8_t len);

/* 发帧；帧间保持 ≥5ms（由调用侧保证，见 proto_tx_gap_ok） */
void proto_send(uint8_t cmd, const uint8_t *p, uint8_t len);
void proto_send_null(uint8_t cmd);

/* 常用上行封装 */
void send_set_time(const uint8_t *bcd8);      /* 8B 已含 BCD 换算 */
void send_set_cfg();                          /* 整帧 13B 下发 g_cfg */
void send_disp_override(uint8_t mode, uint8_t mm, uint8_t ss);
void send_ring(uint8_t mode);                 /* RING 触发(mode: 1 报时, 2/3 提醒/闹钟, 0 停) */
void send_net_stat(uint8_t st);
void send_sta_ip(uint32_t ip);
void send_ap_ready();