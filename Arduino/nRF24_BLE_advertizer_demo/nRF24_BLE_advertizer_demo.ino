// Inspired by http://dmitry.gr/index.php?r=05.Projects&proj=11.%20Bluetooth%20LE%20fakery

#include "SPI.h"  // SPI in Arduino Uno/Nano: MOSI pin 11, MISO pin 12, SCK pin 13
#define PIN_CE	10 // chip enable
#define PIN_CSN	9   // chip select (for SPI)

// The MAC address of BLE advertizer -- just make one up
#define MY_MAC_0	0x11
#define MY_MAC_1	0x22
#define MY_MAC_2	0x33
#define MY_MAC_3	0x44
#define MY_MAC_4	0x55
#define MY_MAC_5	0x66

uint8_t buf[32];   

void btLeCrc(const uint8_t* data, uint8_t len, uint8_t* dst){
// implementing CRC with LFSR
    uint8_t v, t, d;
    
    while(len--){	
        d = *data++;
        for(v = 0; v < 8; v++, d >>= 1){
            t = dst[0] >> 7;
            dst[0] <<= 1;
            if(dst[1] & 0x80) dst[0] |= 1;
            dst[1] <<= 1;
            if(dst[2] & 0x80) dst[1] |= 1;
            dst[2] <<= 1;
            
            if(t != (d & 1)){
              dst[2] ^= 0x5B;
              dst[1] ^= 0x06;
            }
        }	
    }
}

uint8_t  swapbits(uint8_t a){
  // reverse the bit order in a single byte
    uint8_t v = 0;
    if(a & 0x80) v |= 0x01;
    if(a & 0x40) v |= 0x02;
    if(a & 0x20) v |= 0x04;
    if(a & 0x10) v |= 0x08;
    if(a & 0x08) v |= 0x10;
    if(a & 0x04) v |= 0x20;
    if(a & 0x02) v |= 0x40;
    if(a & 0x01) v |= 0x80;
    return v;
}

void btLeWhiten(uint8_t* data, uint8_t len, uint8_t whitenCoeff){
// Implementing whitening with LFSR
    uint8_t  m;
    while(len--){
        for(m = 1; m; m <<= 1){
            if(whitenCoeff & 0x80){
                whitenCoeff ^= 0x11;
                (*data) ^= m;
            }
            whitenCoeff <<= 1;
        }
        data++;
    }
}

static inline uint8_t btLeWhitenStart(uint8_t chan){
//the value we actually use is what BT'd use left shifted one...makes our life easier
    return swapbits(chan) | 2;	
}

void btLePacketEncode(uint8_t* packet, uint8_t len, uint8_t chan){
// Assemble the packet to be transmitted
// Length is of packet, including crc. pre-populate crc in packet with initial crc value!
    uint8_t i, dataLen = len - 3;
    btLeCrc(packet, dataLen, packet + dataLen);
    for(i = 0; i < 3; i++, dataLen++) 
        packet[dataLen] = swapbits(packet[dataLen]);
    btLeWhiten(packet, len, btLeWhitenStart(chan));
    for(i = 0; i < len; i++) 
        packet[i] = swapbits(packet[i]); // the byte order of the packet should be reversed as well
	
}

uint8_t spi_byte(uint8_t byte){
// using Arduino's SPI library; clock out one byte
    SPI.transfer(byte); 
    return byte;
}

void nrf_cmd(uint8_t cmd, uint8_t data){
// Write to nRF24's register
    digitalWrite(PIN_CSN, LOW);
    spi_byte(cmd);
    spi_byte(data);
    digitalWrite(PIN_CSN, HIGH); 
}

void nrf_simplebyte(uint8_t cmd){
// transfer only one byte 
    digitalWrite(PIN_CSN, LOW); 
    spi_byte(cmd);
    digitalWrite(PIN_CSN, HIGH); 
}

void nrf_manybytes(uint8_t* data, uint8_t len){
// transfer several bytes in a row
    digitalWrite(PIN_CSN, LOW); 
        do{
            spi_byte(*data++);
	}while(--len);
        digitalWrite(PIN_CSN, HIGH); 
}


void setup() {
    pinMode(PIN_CSN, OUTPUT);
    pinMode(PIN_CE, OUTPUT);
    pinMode(11, OUTPUT);
    pinMode(13, OUTPUT);
    digitalWrite(PIN_CSN, HIGH);
    digitalWrite(PIN_CE, LOW);
    
    Serial.begin(9600);
    Serial.println("Start LE advertizing");
    SPI.begin();
    SPI.setBitOrder(MSBFIRST);
    
    // Now initialize nRF24L01+, setting general parameters
    nrf_cmd(0x20, 0x12);	//on, no crc, int on RX/TX done
    nrf_cmd(0x21, 0x00);	//no auto-acknowledge
    nrf_cmd(0x22, 0x00);	//no RX
    nrf_cmd(0x23, 0x02);	//4-byte address 
    nrf_cmd(0x24, 0x00);	//no auto-retransmit
    nrf_cmd(0x26, 0x06);	//1MBps at 0dBm
    nrf_cmd(0x27, 0x3E);	//clear various flags
    nrf_cmd(0x3C, 0x00);	//no dynamic payloads
    nrf_cmd(0x3D, 0x00);	//no features
    nrf_cmd(0x31, 32);	        //always RX 32 bytes
    nrf_cmd(0x22, 0x01);	//RX on pipe 0
    
    // Set access addresses (TX address in nRF24L01) to BLE advertising 0x8E89BED6
    // Remember that both bit and byte orders are reversed for BLE packet format
    buf[0] = 0x30;
    buf[1] = swapbits(0x8E);
    buf[2] = swapbits(0x89);
    buf[3] = swapbits(0xBE);
    buf[4] = swapbits(0xD6);
    nrf_manybytes(buf, 5);
    buf[0] = 0x2A;    // set RX address in nRF24L01, doesn't matter because RX is ignored in this case
    nrf_manybytes(buf, 5);
}

void loop() {
    static const uint8_t chRf[] = {2, 26,80};
    static const uint8_t chLe[] = {37,38,39};
    uint8_t i, L=0, ch = 0;
    
    buf[L++] = 0x42;	//PDU type, given address is random; 0x42 for Android and 0x40 for iPhone
    buf[L++] = 16+4; // length of payload
        
    buf[L++] = MY_MAC_0;
    buf[L++] = MY_MAC_1;
    buf[L++] = MY_MAC_2;
    buf[L++] = MY_MAC_3;
    buf[L++] = MY_MAC_4;
    buf[L++] = MY_MAC_5;
        
    buf[L++] = 2;		//flags (LE-only, limited discovery mode)
    buf[L++] = 0x01;
    buf[L++] = 0x05;
		
    buf[L++] = 6;   // length of the name, including type byte
    buf[L++] = 0x08;
    buf[L++] = 'n';
    buf[L++] = 'R';
    buf[L++] = 'F';
    buf[L++] = '2';
    buf[L++] = '4';
        
    buf[L++] = 3;   // length of custom data, including type byte
    buf[L++] = 0xff;   
    buf[L++] = 0x01;
    buf[L++] = 0x02;  // some test data
        
    buf[L++] = 0x55;	//CRC start value: 0x555555
    buf[L++] = 0x55;
    buf[L++] = 0x55;
        
    // Channel hopping
    if(++ch == sizeof(chRf)) ch = 0;	
    nrf_cmd(0x25, chRf[ch]);
    nrf_cmd(0x27, 0x6E);	// Clear flags
        
    btLePacketEncode(buf, L, chLe[ch]);
    nrf_simplebyte(0xE2); //Clear RX Fifo
    nrf_simplebyte(0xE1); //Clear TX Fifo
    
    digitalWrite(PIN_CSN, LOW);
    spi_byte(0xA0);
    for(i = 0 ; i < L ; i++) spi_byte(buf[i]);
    digitalWrite(PIN_CSN, HIGH); 
    
    nrf_cmd(0x20, 0x12);	// TX on
    digitalWrite(PIN_CE, HIGH); // Enable Chip
    delay(50);        // 
    digitalWrite(PIN_CE, LOW);   // (in preparation of switching to RX quickly)
    delay(500);    // Broadcasting interval
}

