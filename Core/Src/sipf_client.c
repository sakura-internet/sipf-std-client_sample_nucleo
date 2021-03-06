/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "main.h"
#include "sipf_client.h"
#include "xmodem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPUT_RETRY_MAX	(3)

UART_HandleTypeDef *pModUart = NULL;
#define UART_RX_BUFF_SZ (512)
static uint8_t rxBuff[UART_RX_BUFF_SZ];
static uint32_t p_read;
#define DMA_WRITE_PTR ( (UART_RX_BUFF_SZ - pModUart->hdmarx->Instance->NDTR) % (UART_RX_BUFF_SZ) )

static char cmd[512];

static uint32_t fw_version;

extern UART_HandleTypeDef huart2;

void SipfClientUartInit(UART_HandleTypeDef *puart)
{
    pModUart = puart;

    memset(rxBuff, 0, sizeof(rxBuff));
    p_read = 0;

    __HAL_UART_DISABLE_IT(pModUart, UART_IT_PE);
    __HAL_UART_DISABLE_IT(pModUart, UART_IT_ERR);
    HAL_UART_Receive_DMA(pModUart, rxBuff, UART_RX_BUFF_SZ);
}

int SipfClientUartIsEmpty(void)
{
    return (p_read == DMA_WRITE_PTR);
}

int SipfClientUartReadByte(uint8_t *byte)
{
    if (p_read != DMA_WRITE_PTR) {
        *byte = rxBuff[p_read++];
        p_read %= UART_RX_BUFF_SZ;
        return *byte;
    } else {
        return -1;
    }
}

int SipfClientUartWriteByte(uint8_t byte)
{
     int ret = HAL_UART_Transmit(pModUart, &byte, 1, 0);
     if (ret != HAL_OK) {
         return -ret;
     }
     return 0;
}

int SipfClientFlushReadBuff(void)
{
    uint8_t b;
    if (pModUart == NULL) {
        return -1;
    }
    while (SipfClientUartReadByte(&b) != -1);

    return HAL_OK;
}

int SipfUtilReadLine(uint8_t *buff, int buff_len, int timeout_ms)
{
    uint8_t b;
    int idx = 0;
    HAL_StatusTypeDef ret;

    uint32_t read_timeout;

    if (pModUart == NULL) {
        return -1;
    }

    read_timeout = uwTick + timeout_ms;
    memset(buff, 0, buff_len);
    for (;;) {
        if (!SipfClientUartIsEmpty()) {
            ret = SipfClientUartReadByte(&b);
            if (ret == -1) {
                return -1;
            }
            read_timeout = uwTick + timeout_ms;
            if (idx < buff_len) {
                if ((b == '\r') || (b == '\n')) {
                    return idx;
                }
                buff[idx++] = b;
            } else {
                return -1;
            }
        }
        if (((int)read_timeout - (int)uwTick) <= 0) {
            //リードタイムアウト
            return -HAL_TIMEOUT;
        }
    }
}

/**
 * $Wコマンドを送信
 */
char tmp[256];
static int sipfSendW(uint8_t addr, uint8_t value)
{
    int len, ret;

    //UART受信バッファを読み捨てる
    SipfClientFlushReadBuff();

    // $Wコマンド送信
    len = sprintf(cmd, "$W %02X %02X\r\n", addr, value);
    ret = HAL_UART_Transmit(pModUart, (uint8_t*)cmd, len, 100);
    if (ret != HAL_OK) {
        return -ret;
    }

    // $Wコマンド応答待ち
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CHAR); // キャラクタ間タイムアウトで1行読む
        if (ret < 0) {
            return ret;
        }
        if (cmd[0] == '$') {
            continue;
        }
        if (memcmp(cmd, "OK", 2) == 0) {
            //OK
            return 0;
        } else if (memcmp(cmd, "NG", 2) == 0) {
            //NG
            return 1;
        }
        HAL_Delay(10);
    }
    return 0;
}

/**
 * $Rコマンドを送信
 */
static int sipfSendR(uint8_t addr, uint8_t *read_value)
{
    int len, ret;
    char *endptr;

    SipfClientFlushReadBuff();

    // $Rコマンド送信
    len = sprintf(cmd, "$R %02X\r\n", addr);
    for (;;) {
        ret = HAL_UART_Transmit(pModUart, (uint8_t*)cmd, len, 100);
        if (ret == HAL_OK) {
            break;
        } else if (ret != HAL_BUSY) {
            return -ret;
        }
        HAL_Delay(10);
    }
    // $R応答待ち
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CMD);
        if (ret < 0) {
            return ret;
        }
        if (cmd[0] == '$') {
            //エコーバックだったら次の行を読む
            continue;
        }
        if (memcmp(cmd, "NG", 2) == 0) {
            //NG
            return 1;
        }
        if (strlen(cmd) == 2) {
            //Valueらしきもの
            *read_value = strtol(cmd, &endptr, 16);
            if (*endptr != '\0') {
                //Null文字以外で変換が終わってる
                return -1;
            }
            break;
        }
    }
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CHAR);
        if (ret < 0) {
            return ret;
        }
        if (memcmp(cmd, "OK", 2) == 0) {
            //OK
            break;
        }
    }
    return 0;
}

/**
 * 認証モード設定
 * mode: 0x00 パスワード認証, 0x01: IPアドレス(SIM)認証
 */
int SipfSetAuthMode(uint8_t mode)
{
    int ret;
    uint8_t val;
    //認証モード切り替え要求
    if (sipfSendW(0x00, mode) != 0) {
        //$W 送信失敗
        return -1;
    }

    //認証モードが切り替わったか確認
    for (;;) {
        HAL_Delay(200);
        ret = sipfSendR(0x00, &val);
        if (ret != 0) {
            return ret;
        }
        if (val == mode) {
            break;
        }
    }
    return 0;
}

/**
 * 認証情報を設定
 */
int SipfSetAuthInfo(char *user_name, char *password)
{
    int ret;
    int len;
    //ユーザー名の長さを設定
    len = strlen(user_name);
    if (len > 80) {
        return -1;
    }
    ret = sipfSendW(0x10, (uint8_t)len);
    if (ret != 0) {
        return ret;  //$W送信失敗
    }
    HAL_Delay(10);
    //ユーザー名を設定
    for (int i = 0; i < len; i++) {
        ret = sipfSendW(0x20 + i, (uint8_t)user_name[i]);
        HAL_Delay(10);
        if (ret != 0) {
            return ret;  //$W送信失敗
        }
    }

    //パスワードの長さを設定
    len = strlen(password);
    if (len > 80) {
        return -1;
    }
    ret = sipfSendW(0x80, (uint8_t)len);
    if (ret != 0) {
        return ret;  //$W送信失敗
    }
    HAL_Delay(10);
    //パスワードを設定
    for (int i = 0; i < len; i++) {
        ret = sipfSendW(0x90 + i, (uint8_t)password[i]);
        HAL_Delay(10);
        if (ret != 0) {
            return ret;  //$W送信失敗
        }
    }
    return 0;
}

/**
 * Fwバージョンをモジュールから取得
 */
int SipfReadFwVersion(uint32_t *version)
{
    uint8_t v = 0;
    if (sipfSendR(0xf1, &v) != 0) {
        return -1;
    }
    fw_version = (uint32_t)v << 24;		// MAJOR
    if (sipfSendR(0xf2, &v) != 0) {
        return -1;
    }
    fw_version |= (uint32_t)v << 16;	// MINOR
    if (sipfSendR(0xf3, &v) != 0) {
        return -1;
    }
    fw_version |= (uint32_t)v;			// RELEASE下位
    if (sipfSendR(0xf4, &v) != 0) {
        return -1;
    }
    fw_version |= (uint32_t)v << 8;		// RELEASE上位

    if (version) {
        *version = fw_version;
    }

    return 0;
}

int SipfCmdTx(uint8_t tag_id, SipfObjTypeId type, uint8_t *value, uint8_t value_len, uint8_t *otid)
{
    int len;
    int ret;

    //UART受信バッファを読み捨てる
    SipfClientFlushReadBuff();

    // $$TXコマンド送信
    len = sprintf(cmd, "$$TX %02X %02X ", tag_id, (uint8_t)type);
    switch (type) {
        case OBJ_TYPE_BIN:
        case OBJ_TYPE_STR_UTF8:
            //順番どおりに文字列に変換
            for (int i = 0; i < value_len; i++) {
                len += sprintf(&cmd[len], "%02X", value[i]);
            }
            break;
        default:
            // リトルエンディアンだからアドレス上位から順に文字列に変換
            for (int i = (value_len - 1); i >= 0; i--) {
                len += sprintf(&cmd[len], "%02X", value[i]);
            }
            break;
    }

    len += sprintf(&cmd[len], "\r\n");
    for (;;) {
        ret = HAL_UART_Transmit(pModUart, (uint8_t*)cmd, len, 100);
        if (ret == HAL_OK) {
            break;
        } else if (ret != -HAL_BUSY) {
            return -ret;
        }
        HAL_Delay(10);
    }

    // OTID待ち
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CMD); // コマンドの応答を待つ
        if (ret == -3) {
            //タイムアウト
            return -3;
        }
        if (cmd[0] == '$') {
            //エコーバック
            continue;
        }
        if (ret == 32) {
            //OTIDらしきもの
            memcpy(otid, cmd, 32);
            break;
        }
        if (memcmp(cmd, "NG", 2) == 0) {
            //NG
            return -1;
        }
    }
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CHAR);
        if (ret == -3) {
            //タイムアウト
            return -3;
        }
        if (memcmp(cmd, "OK", 2) == 0) {
            //OK
            break;
        }
    }
    return 0;
}


static int utilHexToUint8(char *hex, uint8_t *value)
{
    if ((hex[0] >= '0') && (hex[0] <= '9')) {
        *value = (hex[0] - '0') << 4;
    } else if ((hex[0] >= 'A') && (hex[0] <= 'F')) {
        *value = (hex[0] - 'A' + 10) << 4;
    } else if ((hex[0] >= 'a') && (hex[0] <= 'f')) {
        *value = (hex[0] - 'a' + 10) << 4;
    } else {
        return -1;
    }

    if ((hex[1] >= '0') && (hex[1] <= '9')) {
        *value |= (hex[1] - '0') & 0x0f;
    } else if ((hex[1] >= 'A') && (hex[1] <= 'F')) {
        *value |= (hex[1] - 'A' + 10) & 0x0f;
    } else if ((hex[1] >= 'a') && (hex[1] <= 'f')) {
        *value |= (hex[1] - 'a' + 10) & 0x0f;
    } else {
        return -1;
    }

    return *value;
}

static uint8_t rxValueBuff[1024];
int SipfCmdRx(uint8_t *otid, uint64_t *user_send_datetime_ms, uint64_t *sipf_recv_datetime_ms, uint8_t *remain, uint8_t *obj_cnt, SipfObjObject *obj_list, uint8_t obj_list_sz)
{
    enum cmd_rx_stat {
        W_OTID,		//OTID待ち
        W_SEND_DTM,	//ユーザーサーバー送信時刻待ち
        W_RECV_DTM,	//SIPF受信時刻待ち
        W_REMAIN,	//REMAIN待ち
        W_QTY,		//OBJQTY待ち
        W_OBJS,		//OBJ待ち
    };

    int len;
    int ret;

    //UART受信バッファを読み捨てる
    SipfClientFlushReadBuff();

    // $$RXコマンド送信
    len = sprintf(cmd, "$$RX\r\n");
    for (;;) {
        ret = HAL_UART_Transmit(pModUart, (uint8_t*)cmd, len, 100);
        if (ret == HAL_OK) {
            break;
        } else if (ret != -HAL_BUSY) {
            return -ret;
        }
        HAL_Delay(10);
    }

    // 応答を受け取る
    enum cmd_rx_stat rx_stat = W_OTID;
    int line_timeout_ms = TMOUT_CMD;    // 最初はSIPFからの応答を待つのでコマンドタイムアウトを指定
    uint8_t cnt = 0;
    uint16_t idx = 0;
    char *value_top;
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), line_timeout_ms);	// 1行読む
        if (ret == -3) {
            //タイムアウト
            return -3;
        }

        if (ret == 0) {
            // 何もなければもういちど
            continue;
        }

        if ((ret == 1) && ((cmd[0] == '\r') || (cmd[0] == '\n'))) {
            // 空行は読み飛ばし
            continue;
        }

        switch (rx_stat) {
        /* OTID待ち */
        case W_OTID:
            if (cmd[0] == '$') {
                // エコーバックを受信したら読み捨て
                continue;
            }
            if (memcmp(cmd, "OK", 2) == 0) {
                // 受信データなし
                return 0;
            }
            if (ret != 32) {
                // OTIDじゃないっぽい
                return -1;
            }
            memcpy(otid, cmd, 32);
            rx_stat = W_SEND_DTM;				// ユーザーサーバー送信時刻待ちへ
            line_timeout_ms = TMOUT_CHAR;	// 応答は受け取ったから以降はキャラクタ間タイムアウトを指定
            break;
        /* ユーザーサーバー送信時刻待ち */
        case W_SEND_DTM:
            if (ret != 16) {
                // ユーザーサーバーの送信時刻(64bit整数)じゃないっぽい
                return -1;
            }
            *user_send_datetime_ms = 0;
            for (int i = 0; i < 8; i++) {
                uint8_t v;
                if (utilHexToUint8(&cmd[i*2], &v) == -1) {
                    // HEXから数値への変換ができなかった
                    return -1;
                }
                *user_send_datetime_ms |= (uint64_t)v << (56-(i*8));
            }
            rx_stat = W_RECV_DTM;
            break;
        /* SIPF受信時刻待ち */
        case W_RECV_DTM:
            if (ret != 16) {
                // SIPFの受信時刻(64bit整数)じゃないっぽい
                return -1;
            }
            *sipf_recv_datetime_ms = 0;
            for (int i = 0; i < 8; i++) {
                uint8_t v;
                if (utilHexToUint8(&cmd[i*2], &v) == -1) {
                    // HEXから数値への変換ができなかった
                    return -1;
                }
                *sipf_recv_datetime_ms |= (uint64_t)v << (56-(i*8));
            }
            rx_stat = W_REMAIN;
            break;
        /* REMAIN待ち */
        case W_REMAIN:
            if (ret != 2) {
                // REMAIN(8bit整数)じゃないっぽい
                return -1;
            }
            if (utilHexToUint8(cmd, remain) == -1) {
                // HEXから変換できなかった
                return -1;
            }
            rx_stat = W_QTY;
            break;
        /* OBJQTY待ち */
        case W_QTY:
            if (ret != 2) {
                // OBJQTY(8bit整数)じゃないっぽい
                return -1;
            }
            if (utilHexToUint8(cmd, obj_cnt) == -1) {
                // HEXから変換できなかった
                return -1;
            }
            rx_stat = W_OBJS;
            break;
        /* OBJ待ち */
        case W_OBJS:
            if (memcmp(cmd, "OK", 2) == 0) {
                // OBJECTを受信しきった
                return cnt;
            }
            if (memcmp(cmd, "NG", 2) == 0) {
                // エラーらしい
                return -1;
            }
            if (cnt >= obj_list_sz) {
                // リストの上限に達した
                continue;
            }
            if (ret >= 11) {
                // OBJECTっぽい
                if ((cmd[2] != ' ') || (cmd[5] != ' ') || (cmd[8] != ' ')) {
                    // OBJCTじゃない
                    return -1;
                }

                SipfObjObject *obj = &obj_list[cnt++];
                if (fw_version >= 0x00030001) {
                    /* v0.3.1以降 */
                    // TAG_ID
                    if (utilHexToUint8(&cmd[0], &obj->tag_id) == -1) {
                        // HEXから変換できなかった
                        return -1;
                    }
                    // TYPE
                    if (utilHexToUint8(&cmd[3], &obj->type) == -1) {
                        // HEXから変換できなかった
                        return -1;
                    }
                } else {
                    /* v0.3.0以前 */
                    // TYPE
                    if (utilHexToUint8(&cmd[0], &obj->type) == -1) {
                        // HEXから変換できなかった
                        return -1;
                    }
                    // TAG_ID
                    if (utilHexToUint8(&cmd[3], &obj->tag_id) == -1) {
                        // HEXから変換できなかった
                        return -1;
                    }
                }
                // VALUE_LEN
                if (utilHexToUint8(&cmd[6], &obj->value_len) == -1) {
                    // HEXから変換できなかった
                    return -1;
                }
                //VALUE
                obj->value = &rxValueBuff[idx];    //VALUEの先頭のポインタをvalueに設定
                value_top = &cmd[9];
                if ((obj->type == OBJ_TYPE_BIN) || (obj->type == OBJ_TYPE_STR_UTF8)) {
                    //そのままの順でHEXから変換してバッファに追加
                    for (int i = 0; i < obj->value_len; i++) {
                        if (utilHexToUint8(&value_top[i*2], &rxValueBuff[idx++]) == -1) {
                            // HEXからの変換に失敗
                            return -1;
                        }
                    }
                } else {
                    //バイトスワップしてHEXから変換してバッファに追加
                    for (int i = obj->value_len; i > 0; i--) {
                        if (utilHexToUint8(&value_top[(i-1)*2], &rxValueBuff[idx++]) == -1) {
                            // HEXからの変換に失敗
                            return -1;
                        }
                    }
                }
            }
            break;
        }
    }

}


static int sipfCmdFputWaitNg(void)
{
    int ret;
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CHAR);
        if (ret == -3) {
            //タイムアウト
            return -3;
        }
        if (memcmp(cmd, "NG", 2) == 0) {
            //OK
            break;
        }
    }
    return 0;
}

uint8_t buf_xmodem_buff[XMODEM_SZ_BLOCK];
int SipfCmdFput(char *file_id, uint8_t *file_body, size_t sz_file)
{
    //$$FGETコマンドを送る
    int len;
    int ret;

    //UART受信バッファを読み捨てる
    SipfClientFlushReadBuff();

    // $$RXコマンド送信
    len = sprintf(cmd, "$$FPUT %s %08X\r\n", file_id, sz_file);
    for (;;) {
        ret = HAL_UART_Transmit(pModUart, (uint8_t*)cmd, len, 100);
        if (ret == HAL_OK) {
            break;
        } else if (ret != -HAL_BUSY) {
            return -ret;
        }
        HAL_Delay(10);
    }

    //XMODEM送信
    XmodemBegin();
    // 送信要求待ち
    XmodemSendRet xret = XmodemSendWaitRequest(30000);
    switch (xret) {
    case XMODEM_SEND_RET_OK:		//OK
        break;
    case XMODEM_SEND_RET_CANCELED:	//キャンセル受信
    default:					//失敗
        // NG待ち
        sipfCmdFputWaitNg();
        return xret;
    }
    // ブロック送信
    uint8_t fget_bn = 1;
    size_t sz_block;
    for (int idx = 0; idx < sz_file; idx += XMODEM_SZ_BLOCK) {
        for (int i = 0; i < FPUT_RETRY_MAX; i++) {
            if ((sz_file - idx) < XMODEM_SZ_BLOCK) {
                sz_block = len % XMODEM_SZ_BLOCK;
            } else {
                sz_block = XMODEM_SZ_BLOCK;
            }
            xret = XmodemSendBlock(&fget_bn, &file_body[idx], sz_block, 10000);
            switch (xret) {
            case XMODEM_SEND_RET_OK:
                goto next_block;
            case XMODEM_SEND_RET_CANCELED:
                //NG待ちへ
                sipfCmdFputWaitNg();
                return xret;
            case XMODEM_SEND_RET_RETRY:
                //同じブロックを再送
                continue;
            case XMODEM_SEND_RET_TIMEOUT:
            case XMODEM_SEND_RET_FAILED:
                //NG待ちへ
                sipfCmdFputWaitNg();
                return xret;
            }
            break;
        }
next_block:
        fget_bn++;    //ブロック番号を加算
    }

    // XMODEM転送終了
    XmodemSendEnd(500);

    //$$FGETコマンドの応答を見る
    for (;;) {
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), TMOUT_CMD);
        if (ret == -3) {
            //タイムアウト
            return -3;
        }
        if (memcmp(cmd, "OK", 2) == 0) {
            //OK
            break;
        }
    }
    return 0;
}


