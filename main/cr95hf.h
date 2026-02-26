#ifndef CR95HF_H
#define CR95HF_H

#include "driver/spi_master.h"
#include "driver/gpio.h"

// Command Codes
#define CMD_IDN 0x01
#define CMD_PROTOCOL 0x02
#define CMD_SENDRECV 0x04
#define CMD_ECHO 0x55

// Response Codes
#define RSP_SUCCESS 0x00
#define RSP_DATA 0x80

// Protocol Codes
#define PROTO_OFF 0x00
#define PROTO_ISO14443A 0x02

// ISO14443-A Commands
#define ISO14443A_REQA 0x26
#define ISO14443A_WUPA 0x52
#define ISO14443A_CT 0x88
#define ISO14443A_SEL_CL1 0x93
#define ISO14443A_SEL_CL2 0x95
#define ISO14443A_NVB_ANTICOLL 0x20
#define ISO14443A_NVB_SELECT 0x70

// Transmit Flags
#define FLAG_SHORTFRAME 0x07
#define FLAG_STD 0x08
#define FLAG_STD_CRC 0x28

// SAK Card Types
#define SAK_MIFARE_UL 0x00
#define SAK_MIFARE_1K 0x08
#define SAK_MIFARE_MINI 0x09
#define SAK_MIFARE_4K 0x18

typedef struct {
    spi_device_handle_t spi_handle;
    int cs;
    int irq_out;
    int irq_in;
} cr95hfv5_t;

void cr95hfv5_init(cr95hfv5_t *dev);
void cr95hf_info(cr95hfv5_t *dev);

#endif // CR95HF_H_