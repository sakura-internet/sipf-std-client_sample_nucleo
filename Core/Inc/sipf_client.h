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

typedef struct {
	uint8_t	type;
	uint8_t	tag_id;
	uint8_t	value_len;
	uint8_t *value;
}	SipfObjObject;

typedef enum {
    OBJ_TYPE_UINT8    = 0x00,
    OBJ_TYPE_INT8     = 0x01,
    OBJ_TYPE_UINT16   = 0x02,
    OBJ_TYPE_INT16    = 0x03,
    OBJ_TYPE_UINT32   = 0x04,
    OBJ_TYPE_INT32    = 0x05,
    OBJ_TYPE_UINT64   = 0x06,
    OBJ_TYPE_INT64    = 0x07,
    OBJ_TYPE_FLOAT32  = 0x08,
    OBJ_TYPE_FLOAT64  = 0x09,
    OBJ_TYPE_BIN		= 0x10,
    OBJ_TYPE_STR_UTF8 = 0x20
}   SipfObjTypeId;

typedef union {
	uint8_t		u8;
	int8_t		i8;
	uint16_t	u16;
	int16_t		i16;
	uint32_t	u32;
	int32_t		i32;
	uint64_t	u64;
	int64_t		i64;
	float		f;
	double		d;
	uint8_t		b[8];
}	SipfObjPrimitiveType;

#define TMOUT_CMD	(10000)	// コマンドタイムアウト[ms]
#define TMOUT_CHAR	(500)	// キャラクタ間タイムアウト[ms]

int SipfSetAuthMode(uint8_t mode);
int SipfSetAuthInfo(char *user_name, char *password);

int SipfReadFwVersion(uint32_t *version);

int SipfCmdTx(uint8_t tag_id, SipfObjTypeId type, uint8_t *value, uint8_t value_len, uint8_t *otid);
int SipfCmdRx(uint8_t *otid, uint64_t *user_send_datetime_ms, uint64_t *sipf_recv_datetime_ms, uint8_t *remain, uint8_t *obj_cnt, SipfObjObject *obj_list, uint8_t obj_list_sz);

int SipfCmdFput(char *file_id, uint8_t *file_body, size_t sz_file);

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
