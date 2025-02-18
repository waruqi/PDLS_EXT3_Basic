//
// Screen_EPD_EXT3.cpp
// Library C++ code
// ----------------------------------
//
// Project Pervasive Displays Library Suite
// Based on highView technology
//
// Created by Rei Vilo, 28 Jun 2016
//
// Copyright (c) Rei Vilo, 2010-2023
// Licence Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)
//
// Release 508: Added support for E2969CS0B and E2B98CS0B
// Release 527: Added support for ESP32 PSRAM
// Release 541: Improved support for ESP32
// Release 550: Tested Xiao ESP32-C3 with SPI exception
// Release 601: Added support for screens with embedded fast update
// Release 602: Improved functions structure
// Release 604: Improved stability
// Release 607: Improved screens names consistency
// Release 608: Added screen report
// Release 609: Added temperature management
//

// Library header
#include "SPI.h"
#include "Screen_EPD_EXT3.h"

#if defined(ENERGIA)
///
/// @brief Proxy for SPISettings
/// @details Not implemented in Energia
/// @see https://www.arduino.cc/en/Reference/SPISettings
///
struct _SPISettings_s
{
    uint32_t clock; ///< in Hz, checked against SPI_CLOCK_MAX = 16000000
    uint8_t bitOrder; ///< LSBFIRST, MSBFIRST
    uint8_t dataMode; ///< SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3
};
///
/// @brief SPI settings for screen
///
_SPISettings_s _settingScreen;
#else
///
/// @brief SPI settings for screen
///
SPISettings _settingScreen;
#endif // ENERGIA

#ifndef SPI_CLOCK_MAX
#define SPI_CLOCK_MAX 16000000
#endif

// Class
Screen_EPD_EXT3::Screen_EPD_EXT3(eScreen_EPD_EXT3_t eScreen_EPD_EXT3, pins_t board)
{
    _eScreen_EPD_EXT3 = eScreen_EPD_EXT3;
    _pin = board;
    _newImage = 0; // nullptr
}

void Screen_EPD_EXT3::begin()
{
    _codeExtra = (_eScreen_EPD_EXT3 >> 16) & 0xff;
    _codeSize = (_eScreen_EPD_EXT3 >> 8) & 0xff;
    _codeType = _eScreen_EPD_EXT3 & 0xff;
    _screenColourBits = 2; // BWR

    switch (_codeSize)
    {
        case 0x15: // 1.54"

            _screenSizeV = 152; // vertical = wide size
            _screenSizeH = 152; // horizontal = small size
            _screenDiagonal = 154;
            break;

        case 0x21: // 2.13"

            _screenSizeV = 212; // vertical = wide size
            _screenSizeH = 104; // horizontal = small size
            _screenDiagonal = 213;
            break;

        case 0x26: // 2.66"

            _screenSizeV = 296; // vertical = wide size
            _screenSizeH = 152; // horizontal = small size
            _screenDiagonal = 266;
            break;

        case 0x27: // 2.71"

            _screenSizeV = 264; // vertical = wide size
            _screenSizeH = 176; // horizontal = small size
            _screenDiagonal = 271;
            break;

        case 0x28: // 2.87"

            _screenSizeV = 296; // vertical = wide size
            _screenSizeH = 128; // horizontal = small size
            _screenDiagonal = 287;
            break;

        case 0x37: // 3.70"

            _screenSizeV = 416; // vertical = wide size
            _screenSizeH = 240; // horizontal = small size
            _screenDiagonal = 370;
            break;

        case 0x41: // 4.17"

            _screenSizeV = 300; // vertical = wide size
            _screenSizeH = 400; // horizontal = small size
            _screenDiagonal = 417;
            break;

        case 0x43: // 4.37"

            _screenSizeV = 480; // vertical = wide size
            _screenSizeH = 176; // horizontal = small size
            _screenDiagonal = 437;
            break;

        case 0x56: // 5.65"

            _screenSizeV = 600; // v = wide size
            _screenSizeH = 448; // h = small size
            _screenDiagonal = 565;
            break;

        case 0x58: // 5.81"

            _screenSizeV = 720; // v = wide size
            _screenSizeH = 256; // h = small size
            _screenDiagonal = 581;
            break;

        case 0x74: // 7.40"

            _screenSizeV = 800; // v = wide size
            _screenSizeH = 480; // h = small size
            _screenDiagonal = 741;
            break;

        case 0x96: // 9.69"

            _screenSizeV = 672; // v = wide size
            _screenSizeH = 960; // Actually, 960 = 480 x 2, h = small size
            _screenDiagonal = 969;
            break;

        case 0xB9: // 11.98"

            _screenSizeV = 768; // v = wide size
            _screenSizeH = 960; // Actually, 960 = 480 x 2, h = small size
            _screenDiagonal = 1198;
            break;

        default:

            break;
    } // _codeSize

    _bufferDepth = _screenColourBits; // 2 colours
    _bufferSizeV = _screenSizeV; // vertical = wide size
    _bufferSizeH = _screenSizeH / 8; // horizontal = small size 112 / 8;

    // Force conversion for two unit16_t multiplication into uint32_t.
    // Actually for 1 colour; BWR requires 2 pages.
    _pageColourSize = (uint32_t)_bufferSizeV * (uint32_t)_bufferSizeH;

    // _frameSize = _pageColourSize, except for 9.69 and 11.98
    // 9.69 and 11.98 combine two half-screens, hence two frames with adjusted size
    switch (_codeSize)
    {
        case 0x96: // 9.69"
        case 0xB9: // 11.98"

            _frameSize = _pageColourSize / 2;
            break;

        default:

            _frameSize = _pageColourSize;
            break;
    }

#if defined(BOARD_HAS_PSRAM) // ESP32 PSRAM specific case

    if (_newImage == 0)
    {
        static uint8_t * _newFrameBuffer;
        _newFrameBuffer = (uint8_t *) ps_malloc(_pageColourSize * _bufferDepth);
        _newImage = (uint8_t *) _newFrameBuffer;
    }

#else // default case

    if (_newImage == 0)
    {
        static uint8_t * _newFrameBuffer;
        _newFrameBuffer = new uint8_t[_pageColourSize * _bufferDepth];
        _newImage = (uint8_t *) _newFrameBuffer;
    }

#endif // ESP32 BOARD_HAS_PSRAM

    // Check FRAM
    bool flag = true;
    uint8_t count = 8;

    _newImage[1] = 0x00;
    while (flag)
    {
        _newImage[1] = 0xaa;
        delay(100);
        if ((_newImage[1] == 0xaa) or (count == 0))
        {
            flag = false;
        }
        count--;
    }
    memset(_newImage, 0x00, _pageColourSize * _bufferDepth);

    // Initialise the /CS pins
    pinMode(_pin.panelCS, OUTPUT);
    digitalWrite(_pin.panelCS, HIGH); // CS# = 1

    // New generic solution
    pinMode(_pin.panelDC, OUTPUT);
    pinMode(_pin.panelReset, OUTPUT);
    pinMode(_pin.panelBusy, INPUT); // All Pins 0

    // Initialise Flash /CS as HIGH
    if (_pin.flashCS != NOT_CONNECTED)
    {
        pinMode(_pin.flashCS, OUTPUT);
        digitalWrite(_pin.flashCS, HIGH);
    }

    // Initialise slave panel /CS as HIGH
    if (_pin.panelCSS != NOT_CONNECTED)
    {
        pinMode(_pin.panelCSS, OUTPUT);
        digitalWrite(_pin.panelCSS, HIGH);
    }

    // Initialise slave Flash /CS as HIGH
    if (_pin.flashCSS != NOT_CONNECTED)
    {
        pinMode(_pin.flashCSS, OUTPUT);
        digitalWrite(_pin.flashCSS, HIGH);
    }

    // Initialise SD-card /CS as HIGH
    if (_pin.cardCS != NOT_CONNECTED)
    {
        pinMode(_pin.cardCS, OUTPUT);
        digitalWrite(_pin.cardCS, HIGH);
    }

    // Initialise SPI
    _settingScreen = {4000000, MSBFIRST, SPI_MODE0};

#if defined(ENERGIA)

    SPI.begin();
    SPI.setBitOrder(_settingScreen.bitOrder);
    SPI.setDataMode(_settingScreen.dataMode);
    SPI.setClockDivider(SPI_CLOCK_MAX / min(SPI_CLOCK_MAX, _settingScreen.clock));

#else

#if defined(ARDUINO_XIAO_ESP32C3)

    // Board Xiao ESP32-C3 crashes if pins are specified.
    SPI.begin(8, 9, 10); // SCK MISO MOSI

#elif defined(ARDUINO_ARCH_ESP32)

    // Board ESP32-Pico-DevKitM-2 crashes if pins are not specified.
    SPI.begin(14, 12, 13); // SCK MISO MOSI

#else

    SPI.begin();

#endif // ARDUINO_ARCH_ESP32

    SPI.beginTransaction(_settingScreen);

#endif // ENERGIA

    // Reset
    switch (_codeSize)
    {
        case 0x56: // 5.65"
        case 0x58: // 5.81"
        case 0x74: // 7.40"

            _reset(200, 20, 200, 50, 5);
            break;

        case 0x96: // 9.69"
        case 0xB9: // 11.98"

            _reset(200, 20, 200, 200, 5);
            break;

        default:

            _reset(5, 5, 10, 5, 5);
            break;
    } // _codeSize

    _screenWidth = _screenSizeH;
    _screenHeigth = _screenSizeV;

    // Standard
    hV_Screen_Buffer::begin();

    setOrientation(0);
    if (_f_fontMax() > 0)
    {
        _f_selectFont(0);
    }
    _f_fontSolid = false;

    _penSolid = false;
    _invert = false;

    // Report
    Serial.println(formatString("= Screen %s %ix%i", WhoAmI(), screenSizeX(), screenSizeY()));
    Serial.println(formatString("= PDLS v%i", SCREEN_EPD_EXT3_RELEASE));

    clear();
}

void Screen_EPD_EXT3::_reset(uint32_t ms1, uint32_t ms2, uint32_t ms3, uint32_t ms4, uint32_t ms5)
{
    delay_ms(ms1); // delay_ms 5ms
    digitalWrite(_pin.panelReset, HIGH); // RES# = 1
    delay_ms(ms2); // delay_ms 5ms
    digitalWrite(_pin.panelReset, LOW);
    delay_ms(ms3);
    digitalWrite(_pin.panelReset, HIGH);
    delay_ms(ms4);
    digitalWrite(_pin.panelCS, HIGH); // CS# = 1

    // For 9.69 and 11.98 panels
    if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        if (_pin.panelCSS != NOT_CONNECTED)
        {
            digitalWrite(_pin.panelCSS, HIGH); // CSS# = 1
        }
    }
    delay_ms(ms5);
}

String Screen_EPD_EXT3::WhoAmI()
{
    String text = "iTC ";
    text += String(_screenDiagonal / 100);
    text += ".";
    text += String(_screenDiagonal % 100);
    text += "\" -";

#if (FONT_MODE == USE_FONT_HEADER)

    text += "H";

#elif (FONT_MODE == USE_FONT_FLASH)

    text += "F";

#elif (FONT_MODE == USE_FONT_TERMINAL)

    text += "T";

#else

    text += "?";

#endif // FONT_MODE

    return text;
}

void Screen_EPD_EXT3::flush()
{
    flushMode(UPDATE_GLOBAL);
}

void Screen_EPD_EXT3::_flushGlobal()
{
    uint8_t * blackBuffer = _newImage;
    uint8_t * redBuffer = _newImage + _pageColourSize;

    // Three groups:
    // + small: up to 4.37 included
    // + medium: 5.65, 5.81 and 7.4
    // + large: 9.69 and 11,98
    // switch..case does not allow variable declarations
    //
    if ((_codeSize == 0x56) or (_codeSize == 0x58) or (_codeSize == 0x74))
    {
        _reset(200, 20, 200, 50, 5);

        // Send image data
        if (_codeSize == 0x56)
        {
            uint8_t data1_565[] = {0x00, 0x37, 0x00, 0x00, 0x57, 0x02}; // DUW
            _sendIndexData(0x13, data1_565, 6); // DUW
            uint8_t data2_565[] = {0x00, 0x37, 0x00, 0x97}; // DRFW
            _sendIndexData(0x90, data2_565, 4); // DRFW
            uint8_t data3_565[] = {0x37, 0x00, 0x14}; // RAM_RW
            _sendIndexData(0x12, data3_565, 3); // RAM_RW
        }
        else if (_codeSize == 0x58)
        {
            uint8_t data1_565[] = {0x00, 0x1f, 0x50, 0x00, 0x1f, 0x03}; // DUW
            _sendIndexData(0x13, data1_565, 6); // DUW
            uint8_t data2_565[] = {0x00, 0x1f, 0x00, 0xc9}; // DRFW
            _sendIndexData(0x90, data2_565, 4); // DRFW
            uint8_t data3_565[] = {0x1f, 0x50, 0x14}; // RAM_RW
            _sendIndexData(0x12, data3_565, 3); // RAM_RW
        }
        else if (_codeSize == 0x74)
        {
            uint8_t data1_565[] = {0x00, 0x3b, 0x00, 0x00, 0x1f, 0x03}; // DUW
            _sendIndexData(0x13, data1_565, 6); // DUW
            uint8_t data2_565[] = {0x00, 0x3b, 0x00, 0xc9}; // DRFW
            _sendIndexData(0x90, data2_565, 4); // DRFW
            uint8_t data3_565[] = {0x3b, 0x00, 0x14}; // RAM_RW
            _sendIndexData(0x12, data3_565, 3); // RAM_RW
        }

        if (_codeType == 0x0B)
        {
            // y1 = 7 - (y1 % 8);
            uint8_t dtcl = 0x08; // 0=IST, 8=IST
            _sendIndexData(0x01, &dtcl, 1); // DCTL 0x10 of MTP
        }

        _sendIndexData(0x10, blackBuffer, _frameSize); // First frame

        if (_codeSize == 0x56)
        {
            uint8_t data3_565[] = {0x37, 0x00, 0x14}; // RAM_RW
            _sendIndexData(0x12, data3_565, 3); // RAM_RW
        }
        else if (_codeSize == 0x58)
        {
            uint8_t data3_565[] = {0x1f, 0x50, 0x14}; // RAM_RW
            _sendIndexData(0x12, data3_565, 3); // RAM_RW
        }
        else if (_codeSize == 0x74)
        {
            uint8_t data3_565[] = {0x3b, 0x00, 0x14}; // RAM_RW
            _sendIndexData(0x12, data3_565, 3); // RAM_RW
        }
        _sendIndexData(0x11, redBuffer, _frameSize); // Second frame

        // Initial COG
        uint8_t data4_565[] = {0x7d};
        _sendIndexData(0x05, data4_565, 1);
        delay_ms(200);
        uint8_t data5_565[] = {0x00};
        _sendIndexData(0x05, data5_565, 1);
        delay_ms(10);
        uint8_t data6_565[] = {0x3f};
        _sendIndexData(0xc2, data6_565, 1);
        delay_ms(1);
        uint8_t data7_565[] = {0x00};
        _sendIndexData(0xd8, data7_565, 1); // MS_SYNC mtp_0x1d
        uint8_t data8_565[] = {0x00};
        _sendIndexData(0xd6, data8_565, 1); // BVSS mtp_0x1e
        uint8_t data9_565[] = {0x10};
        _sendIndexData(0xa7, data9_565, 1);
        delay_ms(100);
        _sendIndexData(0xa7, data5_565, 1);
        delay_ms(100);
        // uint8_t data10_565[] = {0x00, 0x02 };
        if (_codeSize == 0x56)
        {
            uint8_t data10_565[] = {0x00, 0x02}; // OSC
            _sendIndexData(0x03, data10_565, 2); // OSC mtp_0x12
        }
        else if (_codeSize == 0x58)
        {
            uint8_t data10_565[] = {0x00, 0x01}; // OSC
            _sendIndexData(0x03, data10_565, 2); // OSC mtp_0x12
        }
        else if (_codeSize == 0x74)
        {
            uint8_t data10_565[] = {0x00, 0x01}; // OSC
            _sendIndexData(0x03, data10_565, 2); // OSC mtp_0x12
        }
        _sendIndexData(0x44, data5_565, 1);
        uint8_t data11_565[] = {0x80};
        _sendIndexData(0x45, data11_565, 1);
        _sendIndexData(0xa7, data9_565, 1);
        delay_ms(100);
        _sendIndexData(0xa7, data7_565, 1);
        delay_ms(100);
        uint8_t data12_565[] = {0x06};
        _sendIndexData(0x44, data12_565, 1);
        uint8_t data13_565[] = {0x82};
        data13_565[0] = _temperature * 2 + 0x50; // _temperature
        _sendIndexData(0x45, data13_565, 1); // Temperature 0x82@25C
        _sendIndexData(0xa7, data9_565, 1);
        delay_ms(100);
        _sendIndexData(0xa7, data7_565, 1);
        delay_ms(100);
        uint8_t data14_565[] = {0x25};
        _sendIndexData(0x60, data14_565, 1); // TCON mtp_0x0b
        // uint8_t data15_565[] = {0x01 };
        if (_codeSize == 0x56)
        {
            uint8_t data15_565[] = {0x01}; // STV_DIR
            _sendIndexData(0x61, data15_565, 1); // STV_DIR mtp_0x1c
        }
        else if (_codeSize == 0x58)
        {
            uint8_t data15_565[] = {0x00}; // STV_DIR
            _sendIndexData(0x61, data15_565, 1); // STV_DIR mtp_0x1c
        }
        else if (_codeSize == 0x74)
        {
            uint8_t data15_565[] = {0x00}; // STV_DIR
            _sendIndexData(0x61, data15_565, 1); // STV_DIR mtp_0x1c
        }
        uint8_t data16_565[] = {0x00};
        _sendIndexData(0x01, data16_565, 1); // DCTL mtp_0x10
        uint8_t data17_565[] = {0x00};
        _sendIndexData(0x02, data17_565, 1); // VCOM mtp_0x11

        // DC-DC soft-start
        uint8_t index51_565[] = {0x50, 0x01, 0x0a, 0x01};
        _sendIndexData(0x51, &index51_565[0], 2);
        uint8_t index09_565[] = {0x1f, 0x9f, 0x7f, 0xff};

        for (int value = 1; value <= 4; value++)
        {
            _sendIndexData(0x09, &index09_565[0], 1);
            index51_565[1] = value;
            _sendIndexData(0x51, &index51_565[0], 2);
            _sendIndexData(0x09, &index09_565[1], 1);
            delay_ms(2);
        }
        for (int value = 1; value <= 10; value++)
        {
            _sendIndexData(0x09, &index09_565[0], 1);
            index51_565[3] = value;
            _sendIndexData(0x51, &index51_565[2], 2);
            _sendIndexData(0x09, &index09_565[1], 1);
            delay_ms(2);
        }
        for (int value = 3; value <= 10; value++)
        {
            _sendIndexData(0x09, &index09_565[2], 1);
            index51_565[3] = value;
            _sendIndexData(0x51, &index51_565[2], 2);
            _sendIndexData(0x09, &index09_565[3], 1);
            delay_ms(2);
        }
        for (int value = 9; value >= 2; value--)
        {
            _sendIndexData(0x09, &index09_565[2], 1);
            index51_565[2] = value;
            _sendIndexData(0x51, &index51_565[2], 2);
            _sendIndexData(0x09, &index09_565[3], 1);
            delay_ms(2);
        }
        _sendIndexData(0x09, &index09_565[3], 1);
        delay_ms(10);

        // Display Refresh Start
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        }
        uint8_t data18_565[] = {0x3c};
        _sendIndexData(0x15, data18_565, 1); //Display Refresh
        delay_ms(5);

        // DC-DC off
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        }
        uint8_t data19_565[] = {0x7f};
        _sendIndexData(0x09, data19_565, 1);
        uint8_t data20_565[] = {0x7d};
        _sendIndexData(0x05, data20_565, 1);
        _sendIndexData(0x09, data5_565, 1);
        delay_ms(200);

        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        }
        digitalWrite(_pin.panelDC, LOW);
        digitalWrite(_pin.panelCS, LOW);
        digitalWrite(_pin.panelReset, LOW);
        // digitalWrite(PNLON_PIN, LOW); // PANEL_OFF# = 0
    }
    else if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        _reset(200, 20, 200, 200, 5);

        // Send image data
        if (_codeSize == 0x96)
        {
            uint8_t data1_970[] = {0x00, 0x3b, 0x00, 0x00, 0x9f, 0x02}; // DUW
            _sendIndexData(0x13, data1_970, 6); // DUW for Both Master and Slave
            uint8_t data2_970[] = {0x00, 0x3b, 0x00, 0xa9}; // DRFW
            _sendIndexData(0x90, data2_970, 4); // DRFW for Both Master and Slave
        }
        else if (_codeSize == 0xB9)
        {
            uint8_t data1_970[] = {0x00, 0x3b, 0x00, 0x00, 0x1f, 0x03}; // DUW
            _sendIndexData(0x13, data1_970, 6); // DUW for Both Master and Slave
            uint8_t data2_970[] = {0x00, 0x3b, 0x00, 0xc9}; // DRFW
            _sendIndexData(0x90, data2_970, 4); // DRFW for Both Master and Slave
        }

        uint8_t data3_970[] = {0x3b, 0x00, 0x14};

        if (_codeType == 0x0B)
        {
            uint8_t dtcl = 0x08; // 0=IST, 8=IST
            _sendIndexData(0x01, &dtcl, 1); // DCTL 0x10 of MTP
        }

        // Master
        _sendIndexDataMaster(0x12, data3_970, 3); // RAM_RW

        _sendIndexDataMaster(0x10, blackBuffer, _frameSize); // First frame

        _sendIndexDataMaster(0x12, data3_970, 3); // RAM_RW

        _sendIndexDataMaster(0x11, redBuffer, _frameSize); // Second frame

        // Slave
        _sendIndexDataSlave(0x12, data3_970, 3); // RAM_RW
        _sendIndexDataSlave(0x10, blackBuffer + _frameSize, _frameSize); // First frame

        _sendIndexDataSlave(0x12, data3_970, 3); // RAM_RW
        _sendIndexDataSlave(0x11, redBuffer + _frameSize, _frameSize); // Second frame

        // Initial COG
        uint8_t data4_970[] = {0x7d};
        _sendIndexData(0x05, data4_970, 1);
        delay_ms(200);
        uint8_t data5_970[] = {0x00};
        _sendIndexData(0x05, data5_970, 1);
        delay_ms(10);
        uint8_t data6_970[] = {0x3f};
        _sendIndexData(0xc2, data6_970, 1);
        delay_ms(1);
        uint8_t data7_970[] = {0x80};
        _sendIndexData(0xd8, data7_970, 1); // MS_SYNC
        uint8_t data8_970[] = {0x00};
        _sendIndexData(0xd6, data8_970, 1); // BVSS
        uint8_t data9_970[] = {0x10};
        _sendIndexData(0xa7, data9_970, 1);
        delay_ms(100);
        _sendIndexData(0xa7, data5_970, 1);
        delay_ms(100);

        // --- 9.69 and 11.9 specific
        if (_codeSize == 0x96)
        {
            uint8_t data10_970[] = {0x00, 0x11}; // OSC
            _sendIndexData(0x03, data10_970, 2); // OSC
        }
        else if (_codeSize == 0xB9)
        {
            uint8_t data10_970[] = {0x00, 0x12}; // OSC
            _sendIndexData(0x03, data10_970, 2); // OSC
        }

        _sendIndexDataMaster(0x44, data5_970, 1); // Master
        uint8_t data11_970[] = {0x80};
        _sendIndexDataMaster(0x45, data11_970, 1); // Master
        _sendIndexDataMaster(0xa7, data9_970, 1); // Master
        delay_ms(100);
        _sendIndexDataMaster(0xa7, data5_970, 1); // Master
        delay_ms(100);
        uint8_t data12_970[] = {0x06};
        _sendIndexDataMaster(0x44, data12_970, 1); // Master
        uint8_t data13_970[] = {0x82};
        // uint8_t data13_970[] = {getTemperature(0x50, 0x82) };
        data13_970[0] = _temperature * 2 + 0x50; // _temperature
        _sendIndexDataMaster(0x45, data13_970, 1); // Temperature 0x82@25C   0°C = 0x50, 25°C = 0x82
        _sendIndexDataMaster(0xa7, data9_970, 1); // Master
        delay_ms(100);
        _sendIndexDataMaster(0xa7, data5_970, 1); // Master
        delay_ms(100);

        _sendIndexDataSlave(0x44, data5_970, 1); // Slave
        _sendIndexDataSlave(0x45, data11_970, 1); // Slave
        _sendIndexDataSlave(0xa7, data9_970, 1); // Slave
        delay_ms(100);
        _sendIndexDataSlave(0xa7, data5_970, 1); // Slave
        delay_ms(100);
        _sendIndexDataSlave(0x44, data12_970, 1); // Slave
        _sendIndexDataSlave(0x45, data13_970, 1); // Temperature 0x82@25C   0°C = 0x50, 25°C = 0x82
        _sendIndexDataSlave(0xa7, data9_970, 1); // Slave
        delay_ms(100);
        _sendIndexDataSlave(0xa7, data5_970, 1); // Master
        delay_ms(100);

        uint8_t data14_970[] = {0x25};
        _sendIndexData(0x60, data14_970, 1); // TCON
        uint8_t data15_970[] = {0x01};
        _sendIndexDataMaster(0x61, data15_970, 1); // STV_DIR for Master
        uint8_t data16_970[] = {0x00};
        _sendIndexData(0x01, data16_970, 1); // DCTL
        uint8_t data17_970[] = {0x00};
        _sendIndexData(0x02, data17_970, 1); // VCOM

        // DC-DC soft-start
        uint8_t index51_970[] = {0x50, 0x01, 0x0a, 0x01};
        _sendIndexData(0x51, &index51_970[0], 2);
        uint8_t index09_970[] = {0x1f, 0x9f, 0x7f, 0xff};

        for (int value = 1; value <= 4; value++)
        {
            _sendIndexData(0x09, &index09_970[0], 1);
            index51_970[1] = value;
            _sendIndexData(0x51, &index51_970[0], 2);
            _sendIndexData(0x09, &index09_970[1], 1);
            delay_ms(2);
        }
        for (int value = 1; value <= 10; value++)
        {
            _sendIndexData(0x09, &index09_970[0], 1);
            index51_970[3] = value;
            _sendIndexData(0x51, &index51_970[2], 2);
            _sendIndexData(0x09, &index09_970[1], 1);
            delay_ms(2);
        }
        for (int value = 3; value <= 10; value++)
        {
            _sendIndexData(0x09, &index09_970[2], 1);
            index51_970[3] = value;
            _sendIndexData(0x51, &index51_970[2], 2);
            _sendIndexData(0x09, &index09_970[3], 1);
            delay_ms(2);
        }
        for (int value = 9; value >= 2; value--)
        {
            _sendIndexData(0x09, &index09_970[2], 1);
            index51_970[2] = value;
            _sendIndexData(0x51, &index51_970[2], 2);
            _sendIndexData(0x09, &index09_970[3], 1);
            delay_ms(2);
        }
        _sendIndexData(0x09, &index09_970[3], 1);
        delay_ms(10);

        // Display Refresh Start
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        }
        uint8_t data18_970[] = {0x3c};
        _sendIndexData(0x15, data18_970, 1); // Display Refresh
        delay_ms(5);

        // DC/DC off
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        }
        uint8_t data19_970[] = {0x7f};
        _sendIndexData(0x09, data19_970, 1);
        uint8_t data20_970[] = {0x7d};
        _sendIndexData(0x05, data20_970, 1);
        _sendIndexData(0x09, data5_970, 1);
        delay_ms(200);
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        }
        digitalWrite(_pin.panelDC, LOW);
        digitalWrite(_pin.panelCS, LOW);

        if (_pin.panelCSS != NOT_CONNECTED)
        {
            digitalWrite(_pin.panelCSS, LOW);
        }

        digitalWrite(_pin.panelReset, LOW);
        // digitalWrite(PNLON_PIN, LOW); // PANEL_OFF# = 0

        if (_pin.panelCSS != NOT_CONNECTED)
        {
            digitalWrite(_pin.panelCSS, HIGH); // CSS# = 1
        }
    }
    else // small, including 420 and 437
    {
        _reset(5, 5, 10, 5, 5);

        uint8_t data9[] = {0x0e};
        _sendIndexData(0x00, data9, 1); // Soft-reset
        delay_ms(5);

        uint8_t data7[] = {0x19};
        data7[0] = _temperature; // _temperature
        _sendIndexData(0xe5, data7, 1); // Input Temperature 0°C = 0x00, 22°C = 0x16, 25°C = 0x19
        uint8_t data6[] = {0x02};
        _sendIndexData(0xe0, data6, 1); // Active Temperature

        // Send image data
        _sendIndexData(0x10, blackBuffer, _frameSize); // First frame
        _sendIndexData(0x13, redBuffer, _frameSize); // Second frame

        delay_ms(50);
        uint8_t data8[] = {0x00};
        _sendIndexData(0x04, data8, 1); // Power on
        delay_ms(5);
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        };

        while (digitalRead(_pin.panelBusy) != HIGH);
        _sendIndexData(0x12, data8, 1); // Display Refresh
        delay_ms(5);
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        };

        _sendIndexData(0x02, data8, 1); // Turn off DC/DC
        delay_ms(5);
        while (digitalRead(_pin.panelBusy) != HIGH)
        {
            delay(100);
        };
        digitalWrite(_pin.panelDC, LOW);
        digitalWrite(_pin.panelCS, LOW);

        digitalWrite(_pin.panelReset, LOW);
        // digitalWrite(PNLON_PIN, LOW);
    }
    digitalWrite(_pin.panelCS, HIGH); // CS# = 1
}

void Screen_EPD_EXT3::clear(uint16_t colour)
{
    if (colour == myColours.red)
    {
        // physical red 01
        memset(_newImage, 0x00, _pageColourSize);
        memset(_newImage + _pageColourSize, 0xff, _pageColourSize);
    }
    else if (colour == myColours.grey)
    {
        for (uint16_t i = 0; i < _bufferSizeV; i++)
        {
            uint16_t pattern = (i % 2) ? 0b10101010 : 0b01010101;
            for (uint16_t j = 0; j < _bufferSizeH; j++)
            {
                _newImage[i * _bufferSizeH + j] = pattern;
            }
        }
        memset(_newImage + _pageColourSize, 0x00, _pageColourSize);
    }
    else if (colour == myColours.darkRed)
    {
        // red = 0-1, black = 1-0, white 0-0
        for (uint16_t i = 0; i < _bufferSizeV; i++)
        {
            uint16_t pattern1 = (i % 2) ? 0b10101010 : 0b01010101; // black
            uint16_t pattern2 = (i % 2) ? 0b01010101 : 0b10101010; // red
            for (uint16_t j = 0; j < _bufferSizeH; j++)
            {
                _newImage[i * _bufferSizeH + j] = pattern1;
                _newImage[i * _bufferSizeH + j + _pageColourSize] = pattern2;
            }
        }
    }
    else if (colour == myColours.lightRed)
    {
        // red = 0-1, black = 1-0, white 0-0
        for (uint16_t i = 0; i < _bufferSizeV; i++)
        {
            uint16_t pattern1 = (i % 2) ? 0b00000000 : 0b00000000; // white
            uint16_t pattern2 = (i % 2) ? 0b01010101 : 0b10101010; // red
            for (uint16_t j = 0; j < _bufferSizeH; j++)
            {
                _newImage[i * _bufferSizeH + j] = pattern1;
                _newImage[i * _bufferSizeH + j + _pageColourSize] = pattern2;
            }
        }
    }
    else if ((colour == myColours.white) xor _invert)
    {
        // physical black 00
        memset(_newImage, 0x00, _pageColourSize);
        memset(_newImage + _pageColourSize, 0x00, _pageColourSize);
    }
    else
    {
        // physical white 10
        memset(_newImage, 0xff, _pageColourSize);
        memset(_newImage + _pageColourSize, 0x00, _pageColourSize);
    }
}

void Screen_EPD_EXT3::invert(bool flag)
{
    _invert = flag;
}

void Screen_EPD_EXT3::_setPoint(uint16_t x1, uint16_t y1, uint16_t colour)
{
    // Orient and check coordinates are within screen
    // _orientCoordinates() returns false = success, true = error
    if (_orientCoordinates(x1, y1))
    {
        return;
    }

    uint32_t z1 = _getZ(x1, y1);

    // Convert combined colours into basic colours
    bool flagOdd = ((x1 + y1) % 2 == 0);

    if (colour == myColours.darkRed)
    {
        if (flagOdd)
        {
            colour = myColours.red; // red
        }
        else
        {
            colour = _invert ? myColours.white : myColours.black; // white
        }
    }
    else if (colour == myColours.lightRed)
    {
        if (flagOdd)
        {
            colour = myColours.red; // red
        }
        else
        {
            colour = _invert ? myColours.black : myColours.white; // black
        }
    }
    else if (colour == myColours.grey)
    {
        if (flagOdd)
        {
            colour = myColours.black; // black
        }
        else
        {
            colour = myColours.white; // white
        }
    }

    // Basic colours
    if (colour == myColours.red)
    {
        // physical red 01
        bitClear(_newImage[z1], 7 - (y1 % 8));
        bitSet(_newImage[_pageColourSize + z1], 7 - (y1 % 8));
    }
    else if ((colour == myColours.white) xor _invert)
    {
        // physical black 00
        bitClear(_newImage[z1], 7 - (y1 % 8));
        bitClear(_newImage[_pageColourSize + z1], 7 - (y1 % 8));
    }
    else if ((colour == myColours.black) xor _invert)
    {
        // physical white 10
        bitSet(_newImage[z1], 7 - (y1 % 8));
        bitClear(_newImage[_pageColourSize + z1], 7 - (y1 % 8));
    }
}

void Screen_EPD_EXT3::_setOrientation(uint8_t orientation)
{
    _orientation = orientation % 4;
}

bool Screen_EPD_EXT3::_orientCoordinates(uint16_t & x, uint16_t & y)
{
    bool flag = true; // false = success, true = error
    switch (_orientation)
    {
        case 3: // checked, previously 1

            if ((x < _screenSizeV) and (y < _screenSizeH))
            {
                x = _screenSizeV - 1 - x;
                flag = false;
            }
            break;

        case 2: // checked

            if ((x < _screenSizeH) and (y < _screenSizeV))
            {
                x = _screenSizeH - 1 - x;
                y = _screenSizeV - 1 - y;
                swap(x, y);
                flag = false;
            }
            break;

        case 1: // checked, previously 3

            if ((x < _screenSizeV) and (y < _screenSizeH))
            {
                y = _screenSizeH - 1 - y;
                flag = false;
            }
            break;

        default: // checked

            if ((x < _screenSizeH) and (y < _screenSizeV))
            {
                swap(x, y);
                flag = false;
            }
            break;
    }

    return flag;
}

uint32_t Screen_EPD_EXT3::_getZ(uint16_t x1, uint16_t y1)
{
    uint32_t z1 = 0;
    // According to 11.98 inch Spectra Application Note
    // at http:// www.pervasivedisplays.com/LiteratureRetrieve.aspx?ID=245146
    if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        if (y1 >= (_screenSizeH >> 1))
        {
            y1 -= (_screenSizeH >> 1); // rebase y1
            z1 += (_pageColourSize >> 1); // buffer second half
        }
        z1 += (uint32_t)x1 * (_bufferSizeH >> 1) + (y1 >> 3);
    }
    else
    {
        z1 = (uint32_t)x1 * _bufferSizeH + (y1 >> 3);
    }
    return z1;
}

uint16_t Screen_EPD_EXT3::_getPoint(uint16_t x1, uint16_t y1)
{
    // Orient and check coordinates are within screen
    // _orientCoordinates() returns false = success, true = error
    if (_orientCoordinates(x1, y1))
    {
        return 0;
    }

    uint16_t result = 0;
    uint8_t value = 0;

    uint32_t z1 = _getZ(x1, y1);

    value = bitRead(_newImage[z1], 7 - (y1 % 8));
    value <<= 4;
    value |= bitRead(_newImage[_pageColourSize + z1], 7 - (y1 % 8));

    // red = 0-1, black = 1-0, white 0-0
    switch (value)
    {
        case 0x10:

            result = myColours.black;
            break;

        case 0x01:

            result = myColours.red;
            break;

        default:

            result = myColours.white;
            break;
    }

    return result;
}

void Screen_EPD_EXT3::point(uint16_t x1, uint16_t y1, uint16_t colour)
{
    _setPoint(x1, y1, colour);
}

uint16_t Screen_EPD_EXT3::readPixel(uint16_t x1, uint16_t y1)
{
    return _getPoint(x1, y1);
}

// Utilities
void Screen_EPD_EXT3::_sendIndexData(uint8_t index, const uint8_t * data, uint32_t size)
{
    digitalWrite(_pin.panelDC, LOW); // DC Low
    digitalWrite(_pin.panelCS, LOW); // CS Low
    if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        if (_pin.panelCSS != NOT_CONNECTED)
        {
            digitalWrite(_pin.panelCSS, LOW);
        }
        delayMicroseconds(450); // 450 + 50 = 500
    }
    delayMicroseconds(50);
    SPI.transfer(index);
    delayMicroseconds(50);
    if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        if (_pin.panelCSS != NOT_CONNECTED)
        {
            delayMicroseconds(450);    // 450 + 50 = 500
            digitalWrite(_pin.panelCSS, HIGH);
        }
    }
    digitalWrite(_pin.panelCS, HIGH); // CS High
    digitalWrite(_pin.panelDC, HIGH); // DC High
    digitalWrite(_pin.panelCS, LOW); // CS Low
    if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        if (_pin.panelCSS != NOT_CONNECTED)
        {
            digitalWrite(_pin.panelCSS, LOW); // CSS Low
            delayMicroseconds(450); // 450 + 50 = 500
        }
    }
    delayMicroseconds(50);
    for (uint32_t i = 0; i < size; i++)
    {
        SPI.transfer(data[i]);
    }
    delayMicroseconds(50);
    if ((_codeSize == 0x96) or (_codeSize == 0xB9))
    {
        if (_pin.panelCSS != NOT_CONNECTED)
        {
            delayMicroseconds(450); // 450 + 50 = 500
            digitalWrite(_pin.panelCSS, HIGH);
        }
    }
    digitalWrite(_pin.panelCS, HIGH); // CS High
}

void Screen_EPD_EXT3::regenerate()
{
    clear(myColours.black);
    flush();

    delay(100);

    clear(myColours.white);
    flush();
}

// Software SPI Master protocol setup
void Screen_EPD_EXT3::_sendIndexDataMaster(uint8_t index, const uint8_t * data, uint32_t size)
{
    if (_pin.panelCSS != NOT_CONNECTED)
    {
        digitalWrite(_pin.panelCSS, HIGH); // CS slave HIGH
    }
    digitalWrite(_pin.panelDC, LOW); // DC Low = Command
    digitalWrite(_pin.panelCS, LOW); // CS Low = Select
    delayMicroseconds(500);
    SPI.transfer(index);
    delayMicroseconds(500);
    digitalWrite(_pin.panelCS, HIGH); // CS High = Unselect
    digitalWrite(_pin.panelDC, HIGH); // DC High = Data
    digitalWrite(_pin.panelCS, LOW); // CS Low = Select
    delayMicroseconds(500);

    for (uint32_t i = 0; i < size; i++)
    {
        SPI.transfer(data[i]);
    }
    delayMicroseconds(500);
    digitalWrite(_pin.panelCS, HIGH); // CS High= Unselect
}

// Software SPI Slave protocol setup
void Screen_EPD_EXT3::_sendIndexDataSlave(uint8_t index, const uint8_t * data, uint32_t size)
{
    digitalWrite(_pin.panelCS, HIGH); // CS Master High
    digitalWrite(_pin.panelDC, LOW); // DC Low= Command
    if (_pin.panelCSS != NOT_CONNECTED)
    {
        digitalWrite(_pin.panelCSS, LOW); // CS slave LOW
    }

    delayMicroseconds(500);
    SPI.transfer(index);
    delayMicroseconds(500);

    if (_pin.panelCSS != NOT_CONNECTED)
    {
        digitalWrite(_pin.panelCSS, HIGH); // CS slave HIGH
    }

    digitalWrite(_pin.panelDC, HIGH); // DC High = Data

    if (_pin.panelCSS != NOT_CONNECTED)
    {
        digitalWrite(_pin.panelCSS, LOW); // CS slave LOW
    }

    delayMicroseconds(500);

    for (uint32_t i = 0; i < size; i++)
    {
        SPI.transfer(data[i]);
    }
    delayMicroseconds(500);
    if (_pin.panelCSS != NOT_CONNECTED)
    {
        digitalWrite(_pin.panelCSS, HIGH); // CS slave HIGH
    }
}

//
// === Temperature section
//
void Screen_EPD_EXT3::setTemperatureC(int8_t temperatureC)
{
    _temperature = temperatureC;

    uint8_t _temperature2;
    if (_temperature < 0)
    {
        _temperature2 = -_temperature;
        _temperature2 = (uint8_t)(~_temperature2) + 1; // 2's complement
    }
    else
    {
        _temperature2 = _temperature;
    }
    // indexE5_data[0] = _temperature2;
}

void Screen_EPD_EXT3::setTemperatureF(int16_t temperatureF)
{
    int8_t temperatureC = ((temperatureF - 32) * 5) / 9; // C = (F - 32) * 5 / 9
    setTemperatureC(temperatureC);
}

uint8_t Screen_EPD_EXT3::checkTemperatureMode(uint8_t updateMode)
{
    // #define FEATURE_FAST 0x01 ///< With embedded fast update
    // #define FEATURE_TOUCH 0x02 ///< With capacitive touch panel
    // #define FEATURE_OTHER 0x04 ///< With other feature
    // #define FEATURE_WIDE_TEMPERATURE 0x08 ///< With wide operating temperature
    // #define FEATURE_RED 0x10 ///< With red colour
    updateMode = UPDATE_GLOBAL;

    switch (_codeExtra & 0x19)
    {
        case FEATURE_FAST: // PS series
        
            // Fast 	PS 	Embedded fast update 	FU: +15 to +30 °C 	GU: 0 to +50 °C
            if ((_temperature < 0) or (_temperature > 50))
            {
                updateMode = UPDATE_NONE;
            }
            break;

        case (FEATURE_FAST | FEATURE_WIDE_TEMPERATURE): // KS series

            // Wide 	KS 	Wide temperature and embedded fast update 	FU: 0 to +50 °C 	GU: -15 to +60 °C
            if ((_temperature < -15) or (_temperature > 60))
            {
                updateMode = UPDATE_NONE;
            }
            break;

        case FEATURE_WIDE_TEMPERATURE: // HS series

            // Freezer 	HS 	Global update below 0 °C 	FU: - 	GU: -25 to +30 °C
            if ((_temperature < -25) or (_temperature > 30))
            {
                updateMode = UPDATE_NONE;
            }
            break;

        case FEATURE_RED: // JS series

            // Red      JS 	Red colour 	FU: - 	GU: 0 to +40 °C
            if ((_temperature < 0) or (_temperature > 40))
            {
                updateMode = UPDATE_NONE;
            }
            break;

        default: // CS series

            // Normal 	CS 	Global update above 0 °C 	FU: - 	GU: 0 to +50 °C
            updateMode = UPDATE_GLOBAL;
            if ((_temperature < 0) or (_temperature > 50))
            {
                updateMode = UPDATE_NONE;
            }
            break;
    }

    return updateMode;
}

uint8_t Screen_EPD_EXT3::flushMode(uint8_t updateMode)
{
    updateMode = checkTemperatureMode(updateMode);

    switch (updateMode)
    {
        case UPDATE_FAST:
        case UPDATE_PARTIAL:
        case UPDATE_GLOBAL:

            _flushGlobal();
            break;

        default:

            Serial.println("* PDLS - UPDATE_NONE invoked");
            break;
    }

    return updateMode;
}
//
// === End of Temperature section
//

