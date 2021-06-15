/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "main.h"
#include "sipf_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UART_HandleTypeDef *pModUart = NULL;
#define UART_RX_BUFF_SZ (128)
static uint8_t rxBuff[UART_RX_BUFF_SZ];
static uint32_t p_read;
#define DMA_WRITE_PTR ( (UART_RX_BUFF_SZ - pModUart->hdmarx->Instance->NDTR) % (UART_RX_BUFF_SZ) )

static char cmd[256];

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
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), 1000); // キャラクタ間タイムアウト1秒で1行読む
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
		ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), 10000);
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
		ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), 500);
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

int SipfCmdTx(uint8_t tag_id, SimpObjTypeId type, uint8_t *value, uint8_t value_len, uint8_t *otid)
{
    int len;
    int ret;

    //UART受信バッファを読み捨てる
    SipfClientFlushReadBuff();

    // $$TXコマンド送信
    len = sprintf(cmd, "$$TX %02X %02X ", tag_id, (uint8_t)type);
    switch (type) {
    	case OBJ_TYPE_BIN_BASE64:
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
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), 3000); // キャラクタ間タイムアウト1秒で1行読む
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
        ret = SipfUtilReadLine((uint8_t*)cmd, sizeof(cmd), 500); // キャラクタ間タイムアウト1秒で1行読む
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
