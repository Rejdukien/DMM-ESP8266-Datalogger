#pragma once
#include <stdint.h>

/*

    0	[0, 1, 2, 3, 4, 5, 6, 7]
    1	[8, 9,10,11,12,13,14,15]
    2	...
*/
class BitBuffer
{
public:
    BitBuffer(uint8_t size)
    {
        countBuffers = size / 8 + 1;
        buffers = new uint8_t[countBuffers];
        bitPtr = 0;
    }

    void IRAM_ATTR pushBit(int bitFlag)
    {
        buffers[bitPtr / 8] |= bitFlag << (7 - (bitPtr % 8));
        bitPtr++;
    };

    uint8_t IRAM_ATTR getBitFlag(uint8_t bitPos)
    {
        return (buffers[bitPos / 8] & (1 << (7 - (bitPos % 8)))) != 0;
    };

    uint8_t IRAM_ATTR getByte(uint8_t bitPos)
    {
        uint8_t outByte = 0;
        for (uint8_t i = 0; i < 8; i++)
        {
            outByte |= getBitFlag(bitPos + i) << (7 - i);
        }
        return outByte;
    }

    void IRAM_ATTR reset()
    {
        for (int i = 0; i < countBuffers; i++)
        {
            buffers[i] = 0;
        }
        bitPtr = 0;
    };

    uint8_t IRAM_ATTR size()
    {
        return bitPtr;
    }

    uint8_t maxSize()
    {
        return countBuffers * 8;
    }

private:
    uint8_t countBuffers;
    uint8_t bitPtr;
    uint8_t *buffers;
};