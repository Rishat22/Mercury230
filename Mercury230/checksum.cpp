#include "CheckSum.h"

//1. Генерация контрольной суммы MOD2 (XOR) 8 бит
void GetMOD2_8bit(unsigned char *ARRAY,unsigned char *KS,unsigned long LENGTH)
{
    unsigned char kc =ARRAY[0];
    for(unsigned long i =1; i < LENGTH-1; i++)
    {
        kc ^= ARRAY[i];
    }
    *KS = kc;
}

//2. Генерация CRC16
void GetCRC16(unsigned char *ARRAY,unsigned char *KS_H,unsigned char *KS_L,unsigned long LENGHT)
{
    unsigned int TEMP,flag;
    unsigned int i,j;
    TEMP=0xffff;   /* START MODE OF REGISTER */

    for(i=0;i<LENGHT-2;i++) /* LENGHT=FULL MESSAGE*/
         { TEMP=TEMP ^ ARRAY[i];
          for(j=1;j<=8;j++)
             {
              flag = TEMP & 0x0001;
              TEMP = TEMP >> 1; /* OFFSET REGISTER IN 1 BIT */
              if(flag) TEMP = TEMP ^ 0xa001;
             }
         }
  *KS_L=(TEMP<<8)>>8; /* low byte of crc16 */
  *KS_H=TEMP>>8;      /* high byte of crc16 */
}

//3. Генерация CRC32
void GetCRC32(unsigned char *ARRAY, unsigned char *KS_H1, unsigned char *KS_H2,
              unsigned char *KS_L1, unsigned char *KS_L2, quint32 LENGTH)
{
    LENGTH -=4;
    //инициализируем таблицу расчёта Crc32
    quint32 crc_table[256];
    quint32 crc;
    for (int i = 0; i < 256; i++)
    {
        crc = i;
        for (int j = 0; j < 8; j++)//цикл перебора полинома
            crc = crc & 1 ? (crc >> 1) ^ (quint32)0xEDB88320 : crc >> 1;
        crc_table[i] = crc;
    }
    crc = (quint32)0xFFFFFFFF;
    while (LENGTH--)// проверка условия продолжения
        crc = crc_table[(crc ^ *ARRAY++) & 0xFF] ^ (crc >> 8);
    crc = crc ^ (quint32)0xFFFFFFFF; //конец функции расчёта Crc32
    *KS_H1 = crc >> 24;
    *KS_H2 = (crc << 8) >> 24;
    *KS_L1 = (crc << 16) >> 24;
    *KS_L2 = (crc << 24) >> 24;
}

//4. Генерация контрольной суммы MOD256 (SUMM) 8 бит
void GetMOD256_8bit(unsigned char *ARRAY,unsigned char *KS,unsigned long LENGTH)
{
    unsigned char kc =ARRAY[0];
    for(unsigned long i =1; i < LENGTH-1; i++)
    {
        kc += ARRAY[i];
    }
    *KS = kc;
}
