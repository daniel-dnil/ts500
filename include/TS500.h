/*
 * TS500.h
 *
 *  Created on: Apr 18, 2019
 *      Author: daniel
 */

#ifndef TS500_H_
#define TS500_H_

#include <string>
#include <stdint.h>
#include <vector>

struct addrinfo;

namespace TS500
{

/*
 * Port Numbers.
 */
typedef enum {
    Port1 = 1,
    Port2 = 2,
    Port3 = 3,
    Port4 = 4,
    Port5 = 5,
} Port_t;

/*
 * Baudrates
 */
typedef enum {
    Baudrate1200 = 1200,
    Baudrate2400 = 2400,
    Baudrate4800 = 4800,
    Baudrate9600 = 9600,
    Baudrate19200 = 19200,
    Baudrate38400 = 38400,
    Baudrate57600 = 57600,
    Baudrate115200 = 115200,
    Baudrate230400 = 230400,
    Baudrate460800 = 460800,
    Baudrate921600 = 921600,
    BaudrateUnknown
} Baudrate_t;

/*
 * Databits.
 */
typedef enum {
    DatabitsSeven = 7,
    DatabitsEight = 8,
    DatabitsNine = 9,
    DatabitsUnknown
} Databits_t;

/*
 * Parity
 */
typedef enum {
    ParityNone = 0,
    ParityOdd = 1,
    ParityEven = 2,
    ParityUnknown,
} Parity_t;

/*
 * Stopbits
 */
typedef enum {
    StopbitsOne = 1,
    StopbitsTwo = 2,
    StopbitsUnknown
} Stopbits_t;

/*
 * Flowcontrol
 */
typedef enum {
    FlowcontrolNone,
    FlowcontrolHW,
    FlowcontrolGPIOMode,
    FlowcontrolUnknown,
} Flowcontrol_t;

/*
 * IO Voltage in mV
 */
typedef enum {
    IOVoltage1800,
    IOVoltage2500,
    IOVoltage3300,
    IOVoltage5000,
    IOVoltageUnknown,
} IOVoltage_t;

/*
 * IO State
 */
typedef enum {
    IOStateEnabled,
    IOStateDisabled,
    IOStateUnknown,
} IOState_t;

/*
 * Protocol
 */
typedef enum {
    ProtocolRaw,
    ProtocolTelnet,
    ProtocolUnknown,
} Protocol_t;

/*
 * GPIO state
 */
typedef enum {
    GPIOStateHigh,
    GPIOStateLow,
    GPIOStateUnknown,
} GPIOState_t;

class TS500Port
{
public:
    /* Create a new TS500Port instance, connecting to
     * IP or hostname and the selected serial port
     * throws std::runtime_error in case hostname can't be
     * resolved
     */
    TS500Port(const std::string& hostname,
              const std::string& port);
    virtual ~TS500Port();

    /* Connect to TS via TCP/IP and open the port.
     * Throws std::runtime_error if port is already open
     * or if the ctrl and/or data socket can't be opened */
    void openPort(void);
    void closePort(void);
    bool portOpen(void);

    Port_t portNumber(void);

    /* Read "size" bytes from TS500Port into "buffer".
     * timeout applies to all bytes requested.
     * returns number of bytes read into buffer */
    size_t readPort(const uint32_t size,
                      std::vector<uint8_t>& buffer,
                      const uint32_t timeout_ms);

    /* Write all bytes from "buffer" to TS500Port.
     * timeout applies to all bytes in buffer.
     * returns number of bytes written */
    size_t writePort(const std::vector<uint8_t>& buffer,
                     uint32_t timeout_ms);

    Baudrate_t getBaudrate(void);
    void setBaudrate(const Baudrate_t baudrate);

    Databits_t getDatabits(void);
    void setDatabits(const Databits_t databits);

    Parity_t getParity(void);
    void setParity(const Parity_t parity);

    Stopbits_t getStopbits(void);
    void setStopbits(const Stopbits_t stopbits);

    Flowcontrol_t getFlowctrl(void);
    void setFlowctrl(const Flowcontrol_t flowctrl);

    IOVoltage_t getIOVoltage(void);
    void setIOVoltage(const IOVoltage_t iovoltage);

    IOState_t getIOState(void);
    void setIOState(const IOState_t iostate);

    Protocol_t getProtocol(void);
    void setProtocol(const Protocol_t protocol);

    /* Get / set GPIO output level (RTS signal).
     * Requires flowcontrol in mode FlowcontrolGPIOMode
     */
    GPIOState_t getGPIOOutput(void);
    void setGPIOOutput(const GPIOState_t state);

    /* Get GPIO input level (CTS signal).
     * Requires flowcontrol in mode FlowcontrolGPIOMode
     */
    GPIOState_t getGPIOInput(void);

private:
    /* Don't allow copies of this object */
    TS500Port(TS500Port const &);
    TS500Port& operator=(TS500Port const &);

    void connectCtrlSocket();
    void disconnectCtrlSocket(void);
    void sendCtrlGetRequest(void);
    void sendCtrlSetRequest(std::string setRequest);
    void handleCtrlPortResponse(void);

    void sendGPIOGetRequest(void);
    void sendGPIOSetRequest(GPIOState_t state);
    void handleGPIOResponse(void);

    std::string m_hostname;
    uint16_t m_ctrlport;
    struct addrinfo* m_dataAddrInfo = NULL;
    struct addrinfo* m_ctrlAddrInfo = NULL;
    int m_dataSocket;
    int m_ctrlSocket;

    Port_t m_tsPort;
    Databits_t m_databits;
    Parity_t m_parity;
    Stopbits_t m_stopbits;
    Baudrate_t m_baudrate;
    Flowcontrol_t m_flowctrl;
    IOVoltage_t m_iovolt;
    IOState_t m_iostate;
    Protocol_t m_protocol;
    GPIOState_t m_rtsState;
    GPIOState_t m_ctsState;

    bool m_open = false;
};

}
#endif /* TS500_H_ */
