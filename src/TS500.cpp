/*
 * TS500.cpp
 *
 *  Created on: Apr 18, 2019
 *      Author: daniel
 */

#include <TS500.h>
#include <stdexcept>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <sstream>
#include <iostream>
#include <chrono>

#if defined(_WIN32)
/* Link this code against libws2_32.a when using mingw. Untested on MSVC */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winbase.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#endif

using TS500::TS500Port;
using namespace TS500;

#define SOCKET_PORT_OFFSET    (3300U) // Offset for socket port, Port1 = TCP 3301

static void busyWaitMilliSec(int ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void portableSocketClose(int descriptor, bool graceful)
{
#if defined(_WIN32)
    if (graceful)
    {
        if(SOCKET_ERROR == shutdown(descriptor, SD_SEND))
            throw std::runtime_error("Socket shutdown failed!");

        while(1)
        {
            char buf;
            int status = recv(descriptor, &buf, sizeof(buf), 0);

            if (0 == status)
                break;

            if (SOCKET_ERROR == status)
                throw std::runtime_error("Graceful socket shutdown err!");
        }
    }

    if(0 != closesocket(descriptor))
        throw std::runtime_error("Socket close failed!");
#else
    if (graceful)
    {
        if(-1 == shutdown(descriptor, SHUT_WR))
            throw std::runtime_error("Socket shutdown failed!");

        while(1)
        {
            char buf;
            int status = read(descriptor, &buf, sizeof(buf));

            if (0 == status)
                break;

            if (-1 == status)
                throw std::runtime_error("Graceful socket shutdown err!");
        }
    }

    if (0 != close(descriptor))
        throw std::runtime_error("Socket close failed!");
#endif
}

static int portableSocketRead(int filedes, char* buffer, size_t size)
{
#if defined(_WIN32)
    return recv(filedes, buffer, size, 0);
#else
    return read(filedes, buffer, size);
#endif
}

static int portableSocketWrite(int filedes, const char* buffer, size_t size)
{
#if defined(_WIN32)
    return send(filedes, buffer, size, 0);
#else
    return write(filedes, buffer, size);
#endif
}

static int socketErrorCode(void)
{
#if defined(_WIN32)
    return SOCKET_ERROR;
#else
    return -1;
#endif
}

TS500Port::TS500Port(const std::string& hostname,
                     const std::string& port) :
    m_hostname(hostname),
    m_ctrlport(80),
    m_dataSocket(-1),
    m_ctrlSocket(-1),
    m_databits(DatabitsUnknown),
    m_parity(ParityUnknown),
    m_stopbits(StopbitsUnknown),
    m_baudrate(BaudrateUnknown),
    m_flowctrl(FlowcontrolUnknown),
    m_iovolt(IOVoltageUnknown),
    m_iostate(IOStateUnknown),
    m_protocol(ProtocolUnknown),
    m_rtsState(GPIOStateUnknown),
    m_ctsState(GPIOStateUnknown)
{
    /* Port setup */
    if (("3301" == port) || ("Port1" == port))
        m_tsPort = Port1;
    else if (("3302" == port) || ("Port2" == port))
        m_tsPort = Port2;
    else if (("3303" == port) || ("Port3" == port))
        m_tsPort = Port3;
    else if (("3304" == port) || ("Port4" == port))
        m_tsPort = Port4;
    else if (("3305" == port) || ("Port5" == port))
        m_tsPort = Port5;
    else
        throw std::runtime_error("Port does not exist! : " + port);

#if defined(_WIN32)
    WSADATA wsaData;
    int startRes = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (0 != startRes)
    {
        throw std::runtime_error("Winsock startup error! : " +
                                 std::to_string(startRes));
    }
#endif

    /* Host lookup */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;       /* Allow IPv4 only */
    hints.ai_socktype = SOCK_STREAM; /* TCP socket */
    hints.ai_flags = 0;              /* No flags */
    hints.ai_protocol = 0;           /* Any protocol */

    /* Perform hostname lookup for data and ctrl sockets */
    int ret;
    if ( 0!= (ret = getaddrinfo(m_hostname.c_str(),
                                std::to_string(m_tsPort + SOCKET_PORT_OFFSET).c_str(),
                                &hints, &m_dataAddrInfo)))
        throw std::runtime_error("Can't resolve hostname: " + m_hostname +
                                 " : "+ std::to_string(ret));

    if ( 0!= (ret = getaddrinfo(m_hostname.c_str(), std::to_string(m_ctrlport).c_str(),
                                &hints, &m_ctrlAddrInfo)))
        throw std::runtime_error("Can't resolve hostname: " + m_hostname +
                                 " : "+ std::to_string(ret));
}

TS500Port::~TS500Port()
{
    freeaddrinfo(m_dataAddrInfo);
    freeaddrinfo(m_ctrlAddrInfo);

#if defined(_WIN32)
    int stopRes = WSACleanup();
    if (0 != stopRes)
    {
        std::cerr << "Winsock cleanup error! : " + std::to_string(stopRes);
    }
#endif
}

void TS500Port::openPort(void)
{
    if (m_open)
        throw std::runtime_error("Port already open!");

    /* getaddrinfo() returns a list of address structures.
     * Try each address and connect to data socket */
    struct addrinfo* rp;
    for (rp = m_dataAddrInfo; rp != NULL; rp = rp->ai_next)
    {
        if (-1 == (m_dataSocket = socket(rp->ai_family, rp->ai_socktype,
                                         rp->ai_protocol)))
        {
            continue;
        }

        if (-1 != connect(m_dataSocket, rp->ai_addr, rp->ai_addrlen))
        {
            break;                  /* Success */
        }
        portableSocketClose(m_dataSocket, false);
    }
    if (NULL == rp)               /* No address succeeded */
        throw std::runtime_error("Failed to connect data socket");

    /* set port into "raw" mode since this is expected by this driver
     * This will also update all other cached settings since all port
     * settings are returned in the response from the TS webserver */

    setProtocol(ProtocolRaw);

    m_open = true;
}

void TS500Port::closePort(void)
{
    if (!m_open)
        throw std::runtime_error("Port not open!");

    portableSocketClose(m_dataSocket, true);

    m_open = false;

    /* The TS500 only support one connection at a time on the datasockets
     * Even though we synchronize closing the sockets we need to give the
     * TS500 a little bit of time to start listening for new connections
     * on the socket. System works witout this, but sometimes the initial
     * "SYN" will need a retransmit which causes much longer delays. This
     * is specifically for the case where port are opened and closed in
     * tight loops */
    busyWaitMilliSec(20);
}

bool TS500Port::portOpen(void)
{
    return m_open;
}

Port_t TS500Port::portNumber(void)
{
    return m_tsPort;
}

size_t TS500Port::readPort(const uint32_t size, std::vector<uint8_t>& buffer,
                           const uint32_t timeout_ms)
{
    if (!m_open)
        throw std::runtime_error("Port not open!");

    if (0 == size)
    {
        buffer.clear();
        return 0;
    }

    /* Windows does not support timeouts lower than 500ms using setsockopt
     * so on Windows we need to use select(). To keep things simple we use
     * select on both platform */

    fd_set fdSet;
    FD_ZERO(&fdSet);
    FD_SET(m_dataSocket, &fdSet);

    int result;
    struct timeval tv;
    uint32_t timeRemain_ms = timeout_ms;
    uint32_t recvSize = 0;
    bool timeout = false;

    while(!timeout && (recvSize < size))
    {
        char readBuffer[4096];

        tv.tv_sec = timeRemain_ms / 1000UL;
        tv.tv_usec = (timeRemain_ms * 1000UL ) % 1000000UL;

        /* Record start time and issue the select call */
        auto start = std::chrono::high_resolution_clock::now();
        int rv = select(m_dataSocket + 1, &fdSet, NULL, NULL, &tv);

        if (socketErrorCode() == rv)
        {
            throw std::runtime_error("Socket select error!");
        }
        else if (0 == rv)
        {
            /* timeout */
            result = 0;
        }
        else
        {
            uint32_t bytesRemain = size - recvSize;
            uint32_t chunkSize = (bytesRemain >= sizeof(readBuffer)) ? sizeof(readBuffer) : bytesRemain;
            result = portableSocketRead(m_dataSocket, readBuffer, chunkSize);
            if (socketErrorCode() == result)
            {
                throw std::runtime_error("Socket read failure!");
            }
            else if (0 == result)
            {
                /* Err code 0 is for disconnected peer */
                break;
            }
            else
            {
                /* We got some data */
                buffer.insert(buffer.end(), readBuffer, readBuffer+result);
                recvSize += result;
            }
        }

        /* Calculate duration if this iteration */
        auto end = std::chrono::high_resolution_clock::now();
        auto span = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        /* Check if we have time remaining for another select, +1 is for
         * handling rounding between time measurements and select logic */
        if ((0UL != timeRemain_ms) && (timeRemain_ms > (span.count() + 1)))
            timeRemain_ms -= span.count();
        else
            timeout = true;
    }

    return (size_t) recvSize;
}

size_t TS500Port::writePort(const std::vector<uint8_t>& buffer,
                            uint32_t timeout_ms)
{
    if (!m_open)
        throw std::runtime_error("Port not open!");

    /* Windows does not support timeouts using SO_SNDTIMEO in a reasonable way
     * so on Windows we need to use select(). To keep things simple we use
     * select on both platform */

    fd_set fdSet;
    FD_ZERO(&fdSet);
    FD_SET(m_dataSocket, &fdSet);

    int result;
    struct timeval tv;
    uint32_t timeRemain_ms = timeout_ms;
    uint32_t sentSize = 0;
    bool timeout = false;

    while(!timeout && (sentSize < buffer.size()))
    {
        tv.tv_sec = timeRemain_ms / 1000UL;
        tv.tv_usec = (timeRemain_ms * 1000UL ) % 1000000UL;

        /* Record start time and issue the select call */
        auto start = std::chrono::high_resolution_clock::now();

        int rv = select(m_dataSocket + 1, NULL, &fdSet, NULL, &tv);

        if (socketErrorCode() == rv)
        {
            throw std::runtime_error("Socket select error!");
        }
        else if (0 == rv)
        {
            /* timeout */
            result = 0;
        }
        else
        {
            uint32_t remaining = buffer.size() - sentSize;
            result = portableSocketWrite(m_dataSocket,
                                         (const char*) &buffer.at(sentSize),
                                         remaining);
            if (socketErrorCode() == result)
            {
                throw std::runtime_error("Socket write failure!");
            }
            else if (0 == result)
            {
                /* Err code 0 is for disconnected peer */
                break;
            }
            else
            {
                /* We sent some data */
                sentSize += result;
            }
        }

        /* Calculate duration if this iteration */
        auto end = std::chrono::high_resolution_clock::now();
        auto span = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        /* Check if we have time remaining for another select, +1 is for
         * handling rounding between time measurements and select logic */
        if ((0UL != timeRemain_ms) && (timeRemain_ms > (span.count() + 1)))
            timeRemain_ms -= span.count();
        else
            timeout = true;
    }

    return (size_t) sentSize;
}

void TS500Port::connectCtrlSocket(void)
{
    if (-1 != m_ctrlSocket)
        return;

    /* Create and connect control socket */
    struct addrinfo* rp;
    for (rp = m_ctrlAddrInfo; rp != NULL; rp = rp->ai_next)
    {
        if (-1 == (m_ctrlSocket = socket(rp->ai_family, rp->ai_socktype,
                                         rp->ai_protocol)))
            continue;

        if (-1 != connect(m_ctrlSocket, rp->ai_addr, rp->ai_addrlen))
        {
            break;                  /* Success */
        }
        portableSocketClose(m_ctrlSocket, false);
    }
    if (NULL == rp)               /* No address succeeded */
        throw std::runtime_error("Failed to create ctrl socket");

#if defined(_WIN32)
    DWORD timeout = 3000;
    if (0 != setsockopt(m_ctrlSocket, SOL_SOCKET, SO_RCVTIMEO,
                        (const char*)&timeout, sizeof(DWORD)))
        throw std::runtime_error("Socket option error!");
#else
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    if (0 != setsockopt(m_ctrlSocket, SOL_SOCKET, SO_RCVTIMEO,
                        (const char*)&tv, sizeof(tv)))
        throw std::runtime_error("Socket option error!");
#endif
}

void TS500Port::disconnectCtrlSocket(void)
{
    if (-1 != m_ctrlSocket)
        portableSocketClose(m_ctrlSocket, true);
    m_ctrlSocket = -1;
}

static Baudrate_t baudrateUpdate(std::string& baudString)
{
    if ("1200" == baudString)
        return Baudrate1200;
    else if ("2400" == baudString)
        return Baudrate2400;
    else if ("4800" == baudString)
        return Baudrate4800;
    else if ("9600" == baudString)
        return Baudrate9600;
    else if ("19200" == baudString)
        return Baudrate19200;
    else if ("38400" == baudString)
        return Baudrate38400;
    else if ("57600" == baudString)
        return Baudrate57600;
    else if ("115200" == baudString)
        return Baudrate115200;
    else if ("230400" == baudString)
        return Baudrate230400;
    else if ("460800" == baudString)
        return Baudrate460800;
    else if ("921600" == baudString)
        return Baudrate921600;
    else
        return BaudrateUnknown;
}

static Parity_t parityFromString(std::string& parityString)
{
    if ("No" == parityString)
        return ParityNone;
    else if ("Even" == parityString)
        return ParityEven;
    else if ("Odd" == parityString)
        return ParityOdd;
    else
        return ParityUnknown;
}

static std::string stringFromParity(Parity_t parity)
{
    if (ParityNone == parity)
        return "No";
    else if (ParityEven == parity)
        return "Even";
    else if (ParityOdd == parity)
        return "Odd";
    else
        throw std::runtime_error("Invalid parity param");
}

static Databits_t databitsFromString(std::string& databitsString)
{
    if ("7" == databitsString)
        return DatabitsSeven;
    else if ("8" == databitsString)
        return DatabitsEight;
    else if ("9" == databitsString)
        return DatabitsNine;
    else
        return DatabitsUnknown;
}

static Stopbits_t stopbitsFromString(std::string& stopbitsString)
{
    if ("1" == stopbitsString)
        return StopbitsOne;
    else if ("2" == stopbitsString)
        return StopbitsTwo;
    else
        return StopbitsUnknown;
}

static Flowcontrol_t flowctrlFromString(std::string& flowctrlString)
{
    if ("None" == flowctrlString)
        return FlowcontrolNone;
    else if ("HW" == flowctrlString)
        return FlowcontrolHW;
    else if ("GPIO" == flowctrlString)
        return FlowcontrolGPIOMode;
    else
        return FlowcontrolUnknown;
}

static std::string stringFromFlowctrl(Flowcontrol_t flowctrl)
{
    if (FlowcontrolNone == flowctrl)
        return "None";
    else if (FlowcontrolHW == flowctrl)
        return "HW";
    else if (FlowcontrolGPIOMode == flowctrl)
        return "GPIO";
    else
        throw std::runtime_error("Invalid flowctrl param");
}

static IOVoltage_t ioVoltageFromString(std::string& ioVoltString)
{
    if ("1.8V" == ioVoltString)
        return IOVoltage1800;
    else if ("2.5V" == ioVoltString)
        return IOVoltage2500;
    else if ("3.3V" == ioVoltString)
        return IOVoltage3300;
    else if ("5.0V" == ioVoltString)
        return IOVoltage5000;
    else
        return IOVoltageUnknown;
}

static std::string stringFromIOVoltage(IOVoltage_t ioVoltage)
{
    if (IOVoltage1800 == ioVoltage)
        return "1.8V";
    if (IOVoltage2500 == ioVoltage)
        return "2.5V";
    if (IOVoltage3300 == ioVoltage)
        return "3.3V";
    if (IOVoltage5000 == ioVoltage)
        return "5.0V";
    else
        throw std::runtime_error("Invalid ioVoltage param");
}

static IOState_t ioStateFromString(std::string& ioStateString)
{
    if ("Enabled" == ioStateString)
        return IOStateEnabled;
    else if ("Disabled" == ioStateString)
        return IOStateDisabled;
    else
        return IOStateUnknown;
}

static std::string stringFromIOState(IOState_t ioState)
{
    if (IOStateEnabled == ioState)
        return "Enabled";
    if (IOStateDisabled == ioState)
        return "Disabled";
    else
        throw std::runtime_error("Invalid ioState param");
}

static Protocol_t protocolFromString(std::string& protocolString)
{
    if ("Raw" == protocolString)
        return ProtocolRaw;
    else if ("Telnet" == protocolString)
        return ProtocolTelnet;
    else
        return ProtocolUnknown;
}

static std::string stringFromProtocol(Protocol_t protocol)
{
    if (ProtocolRaw == protocol)
        return "Raw";
    if (ProtocolTelnet == protocol)
        return "Telnet";
    else
        throw std::runtime_error("Invalid protocol param");
}

static GPIOState_t gpioStateFromString(std::string& gpioString)
{
    if ("High" == gpioString)
        return GPIOStateHigh;
    if ("Low" == gpioString)
        return GPIOStateLow;
    else
        throw std::runtime_error("Invalid GPIO state string");
}

static std::string stringFromGPIOState(GPIOState_t state)
{
    if (GPIOStateHigh == state)
        return "High";
    if (GPIOStateLow == state)
        return "Low";
    else
        throw std::runtime_error("Invalid GPIO state param");
}

/*
 * Launch a HTTP GET query like:
 * http://ts500.home.dnil.se/params_get.php?port=1
 * which will give a response like.
 * 460800;No;8;1;None;3.3V;Enabled;Raw
 */
void TS500Port::sendCtrlGetRequest(void)
{
    connectCtrlSocket();

    // Sending a HTTP-GET-Request to the Web Server
    char tmpSendBuf[4096];
    snprintf(tmpSendBuf, sizeof(tmpSendBuf),
             "GET /params_get.php?port=%u HTTP/1.0\r\n"
             "Host: %s\r\nConnection: close\r\n\r\n",
             m_tsPort, m_hostname.c_str());

    int sentBytes = send(m_ctrlSocket, tmpSendBuf, strlen(tmpSendBuf), 0);
    if (((int) strlen(tmpSendBuf) > sentBytes) || (-1 == sentBytes))
        throw std::runtime_error("Can't send data to control socket!");

    handleCtrlPortResponse();
    disconnectCtrlSocket();
}

/*
 * Launch a HTTP GET query like:
 * http://ts500.home.dnil.se/params_set.php?port=3?databits=7
 * which will give a response like.
 * 460800;No;8;1;None;3.3V;Enabled;Raw
 */
void TS500Port::sendCtrlSetRequest(std::string setRequest)
{
    connectCtrlSocket();

    /* Sending a HTTP-GET-Request to the Web Server with the
     * volatile=1 flag set to omit storing settings in NVM on the TS
     */
    char tmpSendBuf[4096];
    snprintf(tmpSendBuf, sizeof(tmpSendBuf),
             "GET /params_set.php?port=%u?volatile=1?%s HTTP/1.0\r\n"
             "Host: %s\r\nConnection: close\r\n\r\n",
             m_tsPort, setRequest.c_str(), m_hostname.c_str());

    int sentBytes = send(m_ctrlSocket, tmpSendBuf, strlen(tmpSendBuf), 0);
    if (((int) strlen(tmpSendBuf) > sentBytes) || (-1 == sentBytes))
        throw std::runtime_error("Can't send data to control socket!");

    handleCtrlPortResponse();
    disconnectCtrlSocket();
}

/* Handle the common get/set params ctrl port response */
void TS500Port::handleCtrlPortResponse(void)
{
    /* Fetch the response */
    std::string ctrlResponse;
    std::string::size_type contentPos = std::string::npos;
    std::string headerSeparator("\r\n\r\n");
    std::string bodyLengthToken("Content-Length:");
    bool headerReceived = false;
    bool bodyRecevied = false;
    long bodyLength;

    while (1)
    {
        /* Read from socket in 512 byte chunks, we may or may not get
         * the whole response in the same packet */
        char tmpRcvBuf[512];
        int dataLen = recv(m_ctrlSocket, tmpRcvBuf, sizeof(tmpRcvBuf), 0);

        if (0 > dataLen)
            throw std::runtime_error("Control socket read error!" +
                                     std::to_string(dataLen));

        ctrlResponse.append(tmpRcvBuf, dataLen);

        /* Check if the header is received completely */
        std::string::size_type headerSize;
        if ((!headerReceived) &&
            (std::string::npos != (headerSize = ctrlResponse.find(headerSeparator))))
        {
            /* Extract the expected body length from the header */
            std::string header = ctrlResponse.substr(0, headerSize);

            std::string::size_type lengthPos;
            if (std::string::npos != (lengthPos = header.find(bodyLengthToken)))
                bodyLength = std::stol(header.substr(lengthPos +
                                                     bodyLengthToken.size()));
            else
                throw std::runtime_error("Body length missing is header ?");

            contentPos = headerSize + headerSeparator.size();
            headerReceived = true;
        }

        /* Check that we have received both header and body of the response */
        if (headerReceived && !bodyRecevied)
        {
            const std::string::size_type expectedResponse =
                            headerSize + bodyLength + headerSeparator.size();
            if (ctrlResponse.size() >= expectedResponse)
                bodyRecevied = true;
        }

        /* Break out of the loop when we are all done */
        if (headerReceived && bodyRecevied)
            break; /* All done */
    }

    /* Remove HTTP header */
    ctrlResponse.erase(0, contentPos);

    /* Split the response into a vector of strings */
    std::vector<std::string> responseTokens;
    std::istringstream tmpStream(ctrlResponse);
    std::string token;
    while (getline(tmpStream, token, ';'))
        responseTokens.push_back(token);

    /* We expect 8 settings in a parameter response */
    if (8 == responseTokens.size())
    {
        /*
         * Update all local variables from the tokenized string
         */
        m_baudrate = baudrateUpdate(responseTokens.at(0));
        m_parity = parityFromString(responseTokens.at(1));
        m_databits = databitsFromString(responseTokens.at(2));
        m_stopbits = stopbitsFromString(responseTokens.at(3));
        m_flowctrl = flowctrlFromString(responseTokens.at(4));
        m_iovolt = ioVoltageFromString(responseTokens.at(5));
        m_iostate = ioStateFromString(responseTokens.at(6));
        m_protocol = protocolFromString(responseTokens.at(7));
    }
    /* We expect 2 settings in a GPIO response */
    else if (2 == responseTokens.size())
    {
        m_rtsState = gpioStateFromString(responseTokens.at(0));
        m_ctsState = gpioStateFromString(responseTokens.at(1));
    }
    else
    {
        throw std::runtime_error("Control socket response mismatch! "
                                 "Expect 8 received " +
                                 std::to_string(responseTokens.size()) +
                                 " : " + ctrlResponse);
    }
}

/*
 * Launch a HTTP GET query like:
 * http://ts500.home.dnil.se/gpio_get.php?port=1
 * which will give a response like :
 * High:Low
 * Answer has (RTS (out);CTS (in))
 */
void TS500Port::sendGPIOGetRequest(void)
{
    connectCtrlSocket();

    // Sending a HTTP-GET-Request to the Web Server
    char tmpSendBuf[4096];
    snprintf(tmpSendBuf, sizeof(tmpSendBuf),
             "GET /gpio_get.php?port=%u HTTP/1.0\r\n"
             "Host: %s\r\nConnection: close\r\n\r\n",
             m_tsPort, m_hostname.c_str());

    int sentBytes = send(m_ctrlSocket, tmpSendBuf, strlen(tmpSendBuf), 0);
    if (((int) strlen(tmpSendBuf) > sentBytes) || (-1 == sentBytes))
        throw std::runtime_error("Can't send data to control socket!");

    handleCtrlPortResponse();
    disconnectCtrlSocket();
}

/*
 * Launch a HTTP GET query like:
 * http://ts500.home.dnil.se/gpio_set.php?port=1?rts=High
 * High:Low
 * Answer has (RTS (out);CTS (in))
 */
void TS500Port::sendGPIOSetRequest(GPIOState_t state)
{
    connectCtrlSocket();

    /* Sending a HTTP-GET-Request to the Web Server */
    char tmpSendBuf[4096];
    snprintf(tmpSendBuf, sizeof(tmpSendBuf),
             "GET /gpio_set.php?port=%u?rts=%s HTTP/1.0\r\n"
             "Host: %s\r\nConnection: close\r\n\r\n",
             m_tsPort, stringFromGPIOState(state).c_str(), m_hostname.c_str());

    int sentBytes = send(m_ctrlSocket, tmpSendBuf, strlen(tmpSendBuf), 0);
    if (((int) strlen(tmpSendBuf) > sentBytes) || (-1 == sentBytes))
        throw std::runtime_error("Can't send data to control socket!");

    handleCtrlPortResponse();
    disconnectCtrlSocket();
}

/*
 * Launch a HTTP GET query like:
 * http://ts500.home.dnil.se/gpio_get.php?port=1
 * High:Low
 * Answer has (RTS (out);CTS (in))
 */
GPIOState_t TS500Port::getGPIOInput(void)
{
    if (FlowcontrolGPIOMode != m_flowctrl)
        throw std::runtime_error("Invalid flowctrl mode for GPIO functions");

    connectCtrlSocket();

    /* Sending a HTTP-GET-Request to the Web Server */
    char tmpSendBuf[4096];
    snprintf(tmpSendBuf, sizeof(tmpSendBuf),
             "GET /gpio_get.php?port=%u? HTTP/1.0\r\n"
             "Host: %s\r\nConnection: close\r\n\r\n",
             m_tsPort, m_hostname.c_str());

    int sentBytes = send(m_ctrlSocket, tmpSendBuf, strlen(tmpSendBuf), 0);
    if (((int) strlen(tmpSendBuf) > sentBytes) || (-1 == sentBytes))
        throw std::runtime_error("Can't send data to control socket!");

    handleCtrlPortResponse();
    disconnectCtrlSocket();

    return m_ctsState;
}

Databits_t TS500Port::getDatabits(void)
{
    return m_databits;
}

void TS500Port::setDatabits(const Databits_t databits)
{
    if (databits == m_databits)
        return;
    if (DatabitsUnknown == databits)
        throw std::runtime_error("Invalid databits param");

    sendCtrlSetRequest("databits=" + std::to_string(databits));
}

Parity_t TS500Port::getParity(void)
{
    return m_parity;
}

void TS500Port::setParity(const Parity_t parity)
{
    if (parity == m_parity)
        return;

    sendCtrlSetRequest("parity=" + stringFromParity(parity));
}

Stopbits_t TS500Port::getStopbits(void)
{
    return m_stopbits;
}

void TS500Port::setStopbits(const Stopbits_t stopbits)
{
    if (stopbits == m_stopbits)
        return;
    if (StopbitsUnknown == stopbits)
        throw std::runtime_error("Invalid stopbits param");

    sendCtrlSetRequest("stopbits=" + std::to_string(stopbits));
}

Baudrate_t TS500Port::getBaudrate(void)
{
    return m_baudrate;
}

void TS500Port::setBaudrate(const Baudrate_t baudrate)
{
    if (baudrate == m_baudrate)
        return;
    if (BaudrateUnknown == baudrate)
        throw std::runtime_error("Invalid baudrate param");

    sendCtrlSetRequest("baud=" + std::to_string(baudrate));
}

Flowcontrol_t TS500Port::getFlowctrl(void)
{
    return m_flowctrl;
}

void TS500Port::setFlowctrl(const Flowcontrol_t flowctrl)
{
    if (flowctrl == m_flowctrl)
        return;

    sendCtrlSetRequest("flowctrl=" + stringFromFlowctrl(flowctrl));
}

IOVoltage_t TS500Port::getIOVoltage(void)
{
    return m_iovolt;
}

void TS500Port::setIOVoltage(const IOVoltage_t iovoltage)
{
    if (iovoltage == m_iovolt)
        return;

    sendCtrlSetRequest("iovolt=" + stringFromIOVoltage(iovoltage));
}

IOState_t TS500Port::getIOState(void)
{
    return m_iostate;
}

void TS500Port::setIOState(const IOState_t iostate)
{
    if (iostate == m_iostate)
        return;

    sendCtrlSetRequest("iostate=" + stringFromIOState(iostate));
}

Protocol_t TS500Port::getProtocol(void)
{
    return m_protocol;
}

void TS500Port::setProtocol(const Protocol_t protocol)
{
    if (protocol == m_protocol)
        return;

    sendCtrlSetRequest("protocol=" + stringFromProtocol(protocol));
}

GPIOState_t TS500Port::getGPIOOutput(void)
{
    if (FlowcontrolGPIOMode != m_flowctrl)
        throw std::runtime_error("Invalid flowctrl mode for GPIO functions");

    return m_rtsState;
}

void TS500Port::setGPIOOutput(const GPIOState_t state)
{
    if (FlowcontrolGPIOMode != m_flowctrl)
        throw std::runtime_error("Invalid flowctrl mode for GPIO functions");

    if (state == m_rtsState)
        return;

    sendGPIOSetRequest(state);
}
