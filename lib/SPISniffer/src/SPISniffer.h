#pragma once
#include "Bitbuffer.h"
#include <Arduino.h>

extern int IRAM_ATTR __digitalRead(uint8_t pin)
{
    if (pin < 16)
    {
        return GPIP(pin);
    }
    else if (pin == 16)
    {
        return GP16I & 0x01;
    }
    return 0;
}

// Supported list of multimeter modes, it's missing a few that I don't care about
enum DmmMode
{
    VoltageDC,
    VoltageAC,
    AmperageDC,
    AmperageAC,
    Resistance,
    Temperature,
    Unknown
};

// Bit locations for various lcd segments to figure out the current state
enum BitFlags
{
    VoltageFlag = 128,
    AmperageFlag = 129,
    ResistanceFlag = 132,
    TemperatureFlag = 134,
    AcFlag = 87,
    MegaFlag = 130,
    KiloFlag = 131,
    MilliFlag = 126,
    MicroFlag = 125,
    KiloFlagForSecondaryDisplay = 73
};

// 8bit patterns of the digits themselves; Bit5 (dot/minus) is assumed low
enum DigitMasks
{
    Zero = 0xD7,
    One = 0x50,
    Two = 0xB5,
    Three = 0xF1,
    Four = 0x72,
    Five = 0xE3,
    Six = 0xE7,
    Seven = 0x51,
    Eight = 0xF7,
    Nine = 0xF3,
    Overload = 0x86
};

// Struct that holds all the parsed information
typedef struct
{
    boolean overloaded = false;
    DmmMode mode = DmmMode::Unknown;
    double parsedNumber = 0.0;
    double secondParsedNumber = 0.0;
} DmmData;

class SPISniffer
{
public:
    SPISniffer() {}

    uint8_t _enablePin;
    uint8_t _dataPin;
    uint8_t _clkPin;
    void (*_handleData)(DmmData data);

    void setup(uint8_t enablePin, uint8_t dataPin, uint8_t clkPin, uint8_t bufferSize) {
        // Setup pins, interrupts handled in wrapper setup function calling member functions due to limitations with attachInterrupt
        _enablePin = enablePin;
        _dataPin = dataPin;
        _clkPin = clkPin;
        _bufferSize = bufferSize;
        buffer = new BitBuffer(bufferSize);
        pinMode(_clkPin, INPUT);
        pinMode(_enablePin, INPUT);
        pinMode(_clkPin, INPUT);
    };

    uint8_t IRAM_ATTR parseByteToReadableDigit(uint8_t byte)
    {
        if (testByte(byte, DigitMasks::Zero))
            return 0;
        if (testByte(byte, DigitMasks::One))
            return 1;
        if (testByte(byte, DigitMasks::Two))
            return 2;
        if (testByte(byte, DigitMasks::Three))
            return 3;
        if (testByte(byte, DigitMasks::Four))
            return 4;
        if (testByte(byte, DigitMasks::Five))
            return 5;
        if (testByte(byte, DigitMasks::Six))
            return 6;
        if (testByte(byte, DigitMasks::Seven))
            return 7;
        if (testByte(byte, DigitMasks::Eight))
            return 8;
        if (testByte(byte, DigitMasks::Nine))
            return 9;

        // None matches, just go with a zero ¯\_(ツ)_/¯
        return 0;
    }

    boolean IRAM_ATTR hasDotOrMinusFlag(uint8_t byte)
    {
        return ((byte >> 3) & 1); // bit 4 is set
    }

    boolean IRAM_ATTR isOverload(uint8_t byte)
    {
        return testByte(byte, DigitMasks::Overload);
    }

    void IRAM_ATTR handleEnable()
    {
        // parse buffer into shit that makes sense, packet has finished when this interrupt fires
        if (buffer->size() != _bufferSize)
        {
            buffer->reset();
            return;
        }

        DmmData data;

        // Handle mode flags
        bool isAC = buffer->getBitFlag(BitFlags::AcFlag);
        data.mode = DmmMode::Unknown;
        if (buffer->getBitFlag(BitFlags::VoltageFlag))
        {
            data.mode = isAC ? DmmMode::VoltageAC : DmmMode::VoltageDC;
        }
        else if (buffer->getBitFlag(BitFlags::AmperageFlag))
        {
            data.mode = isAC ? DmmMode::AmperageAC : DmmMode::AmperageDC;
        }
        else if (buffer->getBitFlag(BitFlags::ResistanceFlag))
        {
            data.mode = DmmMode::Resistance;
        }
        else if (buffer->getBitFlag(BitFlags::TemperatureFlag))
        {
            data.mode = DmmMode::Temperature;
        }
        if (data.mode == DmmMode::Unknown)
        {
            buffer->reset();
            return;
        }

        // Handle exponents for the main digit display
        int8_t exponent = 0;
        if (buffer->getBitFlag(BitFlags::MegaFlag))
        {
            exponent = 6;
        }
        else if (buffer->getBitFlag(BitFlags::KiloFlag))
        {
            exponent = 3;
        }
        else if (buffer->getBitFlag(BitFlags::MilliFlag))
        {
            exponent = -3;
        }
        else if (buffer->getBitFlag(BitFlags::MicroFlag))
        {
            exponent = -6;
        }

        // Parse main digits
        int negative = 1;
        double parsedNumber = 0;
        for (uint8_t i = 0; i < 4; i++)
        {
            uint8_t byte = buffer->getByte(9 + (i * 8));

            if (i == 1 && isOverload(byte))
            {
                data.overloaded = true;
                break;
            }

            if (hasDotOrMinusFlag(byte))
            {
                if (i == 3)
                {
                    negative = -1;
                }
                else
                {
                    exponent -= i + 1;
                }
            }

            parsedNumber += (double)(parseByteToReadableDigit(byte)) * pow(10, i);
        }
        if (!data.overloaded)
        {
            parsedNumber *= pow(10, exponent);
            parsedNumber *= negative;
        }
        data.parsedNumber = parsedNumber;

        // Parse secondary digits (frequency when in AC modes)
        parsedNumber = 0.0;
        exponent = buffer->getBitFlag(BitFlags::KiloFlagForSecondaryDisplay) ? 3 : 0;
        if (data.mode == DmmMode::VoltageAC || data.mode == DmmMode::AmperageAC)
        {
            for (uint8_t i = 0; i < 4; i++)
            {
                uint8_t byte = buffer->getByte(41 + (i * 8));
                if (hasDotOrMinusFlag(byte))
                {
                    exponent -= i + 1;
                }
                parsedNumber += (double)(parseByteToReadableDigit(byte)) * pow(10, i);
            }
            parsedNumber *= pow(10, exponent);
            data.secondParsedNumber = parsedNumber;
        }

        // AAAAAAAAAAAHHHHHHHHHHHHHHHHHHHHHHHHHH finally
        _handleData(data);

        // Reset for next packet
        buffer->reset();
    }

    void IRAM_ATTR handleClk()
    {
        buffer->pushBit(__digitalRead(_dataPin));
    }

private:
    BitBuffer *buffer;
    uint8_t _bufferSize;

    boolean IRAM_ATTR testByte(uint8_t byteToTest, uint8_t byteToTestAgainst)
    {
        return byteToTest == byteToTestAgainst || byteToTest == byteToTestAgainst + 8;
    }
};