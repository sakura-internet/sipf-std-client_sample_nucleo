/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _SIPF_OBJ_PARSER_H_
#define _SIPF_OBJ_PARSER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OBJ_TYPE_UINT8      = 0x00,
    OBJ_TYPE_INT8       = 0x01,
    OBJ_TYPE_UINT16     = 0x02,
    OBJ_TYPE_INT16      = 0x03,
    OBJ_TYPE_UINT32     = 0x04,
    OBJ_TYPE_INT32      = 0x05,
    OBJ_TYPE_UINT64     = 0x06,
    OBJ_TYPE_INT64      = 0x07,
    OBJ_TYPE_FLOAT32    = 0x08,
    OBJ_TYPE_FLOAT64    = 0x09,
    OBJ_TYPE_BIN_BASE64 = 0x10,
    OBJ_TYPE_STR_UTF8   = 0x20
}   SimpObjTypeId;

int SipfSetAuthMode(uint8_t mode);
int SipfSetAuthInfo(char *user_name, char *password);

int SipfCmdTx(uint8_t tag_id, SimpObjTypeId type, uint8_t *value, uint8_t value_len, uint8_t *otid);

void SipfClientUartInit(UART_HandleTypeDef *puart);
int SipfClientUartIsEmpty(void);
int SipfClientUartReadByte(uint8_t *byte);
int SipfClientUartWriteByte(uint8_t byte);

int SipfClientFlushReadBuff(void);
int SipfUtilReadLine(uint8_t *buff, int buff_len, int timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
