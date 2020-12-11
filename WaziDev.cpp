#include "WaziDev.h"
// #include <SPI.h>
#include <EEPROM.h>
#include <LowPower.h>
#include "local_lorawan.h"

WaziDev::WaziDev() {}

unsigned char AppSkey[16];
unsigned char NwkSkey[16];

struct EEPROMConfig
{
    uint16_t head;
    uint8_t packetNumber;
};

uint8_t WaziDev::setupLoRaWAN(const uint8_t *devAddr, const uint8_t *key)
{
    uint8_t e;
    memcpy(AppSkey, key, 16);
    memcpy(NwkSkey, key, 16);

    // Power ON the module
    e = sx1272.ON();
    if (e != 0) return e;

    // get config from EEPROM
    EEPROMConfig c;
    EEPROM.get(0, c);

    // found a valid config?
    if (c.head == 0x1234)
    {
        // set sequence number for SX1272 library
        sx1272._packetNumber = c.packetNumber;
    }

    sx1272.setLORA();
    sx1272.setCR(CR_5);   // CR = 4/5
    sx1272.setSF(SF_12);  // SF = 12
    sx1272.setBW(BW_125); // BW = 125 KHz

    // set frequencie 868.1 MHz
    sx1272.setChannel(CH_18_868);

    // set the sync word to the LoRaWAN sync word which is 0x34
    sx1272.setSyncWord(0x34);

    // enable carrier sense
    sx1272._enableCarrierSense = true;

    // TODO: with low power, when setting the radio module in sleep mode
    // there seem to be some issue with RSSI reading
    sx1272._RSSIonSend = false;

    sx1272._needPABOOST = true;

    sx1272.setPowerDBM(14);

    sx1272._rawFormat = true;

    delay(500);
    return 0;
}

#define REG_INVERT_IQ 0x33
#define REG_INVERT_IQ2 0x3B

uint8_t WaziDev::setLoRaFreq(uint32_t freq)
{
    uint32_t chan = (uint64_t (freq)) << 19 / 32000000;
    return sx1272.setChannel(chan);
}

uint8_t WaziDev::setLoRaCR(uint8_t cr)
{
    return sx1272.setCR(cr);
}

uint8_t WaziDev::setLoRaBW(uint16_t bw)
{
    return sx1272.setBW(bw);
}

uint8_t WaziDev::setLoRaSF(uint8_t sf)
{
    return sx1272.setSF(sf);
}

uint8_t WaziDev::sendLoRa(void *pl, uint8_t len)
{
    sx1272._payloadlength = len;
    sx1272.writeRegister(REG_INVERT_IQ, 0x27);
    sx1272.writeRegister(REG_INVERT_IQ2, 0x1D);
    uint8_t e = sx1272.setPacket(0, (char *) pl);
    if (e != 0) return e;
    return sx1272.sendWithTimeout();
}

uint8_t WaziDev::sendLoRaWAN(void *pl, uint8_t len)
{
    len = local_aes_lorawan_create_pkt((uint8_t *) pl, len, 0);
    return sendLoRa(pl, len);
}

uint8_t WaziDev::receiveLoRa(void *pl, uint8_t* len, uint16_t timeout)
{
    sx1272.writeRegister(REG_INVERT_IQ, 0x66);
    sx1272.writeRegister(REG_INVERT_IQ2, 0x19);
    int e = sx1272.receiveAll(timeout);
    if (e == 0)
    {
        *len = sx1272._payloadlength;
        memcpy(pl, sx1272.packet_received.data, sx1272._payloadlength);
        sx1272.getSNR();
        loRaSNR = sx1272._SNR;
        sx1272.getRSSI();
        loRaRSSI = sx1272._RSSI;
    }
    return e;
}

uint8_t WaziDev::receiveLoRaWAN(void *pl, uint8_t* offs, uint8_t* len, uint16_t timeout)
{
    int e = receiveLoRa(pl, len, timeout);
    if (e == 0)
    {
        e = local_lorawan_decode_pkt((uint8_t*) pl, *len);
        if (e >= 0) {
            *len -= 4; // remove 4 byte MIC
            ((char*) pl)[*len] = 0; // null terminate
            *offs = e;
            *len -= e; // skip offset
            e = 0;
        }
    }
    return e;
}
