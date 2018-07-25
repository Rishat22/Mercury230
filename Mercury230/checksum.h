#ifndef CHECKSUM
#define CHECKSUM

#include "QString"

// Генерация MOD2 8 бит
void GetMOD2_8bit(unsigned char *ARRAY,unsigned char *KS,unsigned long LENGHT);
// Генерация CRC16
void GetCRC16(unsigned char *ARRAY,unsigned char *KS_H,unsigned char *KS_L,unsigned long LENGHT);
// Генерация CRC32
void GetCRC32(unsigned char *ARRAY, unsigned char *KS_H1, unsigned char *KS_H2,
              unsigned char *KS_L1, unsigned char *KS_L2, quint32 LENGTH);
// Генерация MOD256 8 бит
void GetMOD256_8bit(unsigned char *ARRAY,unsigned char *KS,unsigned long LENGTH);

#endif // CHECKSUM

