#include <iostream>
#include <winsock2.h>
#include <mswsock.h>
#include <qos2.h>

#pragma comment(lib, "qwave.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable:4996)

#define BUFLEN      1400
#define PORT        5021

CHAR                dataBuffer[BUFLEN];
QOS_VERSION QosVersion = { 1 , 0 };

VOID
socketCreate(
    __in    LPWSTR destination,
    __out   SOCKET* socket,
    __out   ADDRESS_FAMILY* addressFamily,
    __out   LPFN_TRANSMITPACKETS* transmitPackets
) {
    WSADATA             wsaData;
    SOCKADDR_STORAGE    destAddr;
    INT                 returnValue;
    INT                 sockaddrLen;
    DWORD               bytesReturned;

    // GUID of the TransmitPacket Winsock2 function which we will
    // use to send the traffic at the client side.
    GUID TransmitPacketsGuid = WSAID_TRANSMITPACKETS;

    // Start Winsock
    returnValue = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (returnValue != 0) {
        printf("%s:%d - WSAStartup failed (%d)\n",
            __FILE__, __LINE__, returnValue);
        exit(1);
    }

    // First attempt to convert the string to an IPv4 address
    sockaddrLen = sizeof(destAddr);
    destAddr.ss_family = AF_INET;

    returnValue = WSAStringToAddressW(destination,
        AF_INET, NULL, (LPSOCKADDR)&destAddr, &sockaddrLen);
    if (returnValue != ERROR_SUCCESS) {
        printf("%s:%d - WSAStringToAddressW failed (%d)\n",
            __FILE__, __LINE__, WSAGetLastError());
        exit(1);
    }

    // Set the destination port.
    SS_PORT((PSOCKADDR)&destAddr) = PORT;
    // Copy the address family back to caller
    *addressFamily = destAddr.ss_family;

    // Create a UDP socket
    *socket = WSASocket(destAddr.ss_family,
        SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (*socket == INVALID_SOCKET) {
        printf("%s:%d - WSASocket failed (%d)\n",
            __FILE__, __LINE__, WSAGetLastError());
        exit(1);
    }

    // Connect the new socket to the destination
    returnValue = WSAConnect(*socket,
        (PSOCKADDR)&destAddr, sizeof(destAddr), NULL, NULL, NULL, NULL);
    if (returnValue != NO_ERROR) {
        printf("%s:%d - WSAConnect failed (%d)\n",
            __FILE__, __LINE__, WSAGetLastError());
        exit(1);
    }

    // Query the function pointer for the TransmitPacket function
    returnValue = WSAIoctl(*socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &TransmitPacketsGuid, sizeof(GUID), transmitPackets, sizeof(PVOID), &bytesReturned, NULL, NULL);
    if (returnValue == SOCKET_ERROR) {
        printf("%s:%d - WSAIoctl failed (%d)\n",
            __FILE__, __LINE__, WSAGetLastError());
        exit(1);
    }
}

int main(
    __in                int argc,
    __in_ecount(argc)   char* argv[])
{
    ADDRESS_FAMILY              addressFamily;
    SOCKET                      socket;
    LPFN_TRANSMITPACKETS        transmitPacketsFn;
    LPTRANSMIT_PACKETS_ELEMENT  transmitEl;

    HANDLE                      qosHandle;
    QOS_FLOWID                  flowID;
    DWORD                       dscpValue;
    BOOL                        result;

    ULONG temp;
    wchar_t dest[255];

    if (argc != 3) {
        printf("Usage: %s <ipv4_address> <dscp_value>\n", __FILE__);
        exit(1);
    }

    // Create a UDP socket
    std::mbstowcs(dest, argv[1], strlen(argv[1]) + 1);
    socketCreate((LPWSTR) dest, &socket, &addressFamily, &transmitPacketsFn);

    // Initialize the QoS subsystem
    if (FALSE == QOSCreateHandle(&QosVersion, &qosHandle)) {
        printf("%s:%d - QOSCreateHandle failed (%d)\n",
            __FILE__, __LINE__, GetLastError());
        exit(1);
    }
    
    // Create a flow for our socket
    flowID = 0;
    result = QOSAddSocketToFlow(qosHandle,
        socket, NULL, QOSTrafficTypeBestEffort, QOS_NON_ADAPTIVE_FLOW, &flowID);
    if (result == FALSE) {
        printf("%s:%d - QOSAddSocketToFlow failed (%d)\n",
            __FILE__, __LINE__, GetLastError());
        exit(1);
    }

    // Set DSCP value for the flow
    dscpValue = atol(argv[2]);
    result = QOSSetFlow(qosHandle,
        flowID, QOSSetOutgoingDSCPValue, sizeof(dscpValue), &dscpValue, 0, NULL);
    if (result == FALSE) {
        printf("%s:%d - QOSSetFlow failed (%d)\n",
            __FILE__, __LINE__, GetLastError());
        exit(1);
    }

    // Initialize transmit packets
    transmitEl = (LPTRANSMIT_PACKETS_ELEMENT) HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, sizeof(TRANSMIT_PACKETS_ELEMENT)*1 );
    if (transmitEl == NULL) {
        printf("%s:%d - HeapAlloc failed (%d)\n",
            __FILE__, __LINE__, GetLastError());
        exit(1);
    }

    ZeroMemory(&dataBuffer, sizeof(dataBuffer));
    for (temp = 0; temp < 1; temp++) {
        transmitEl[temp].dwElFlags = TP_ELEMENT_MEMORY | TP_ELEMENT_EOP;
        transmitEl[temp].pBuffer = dataBuffer;
        transmitEl[temp].cLength = sizeof(dataBuffer);
    }

    do {
        result = (*transmitPacketsFn)(socket,
            transmitEl, 1, 0xFFFFFFFF, NULL, TF_USE_KERNEL_APC);
        if (result == FALSE) {
            DWORD lastError;

            lastError = WSAGetLastError();
            if (lastError != ERROR_IO_PENDING) {
                printf("%s:%d - TransmitPackets failed (%d)\n",
                    __FILE__, __LINE__, GetLastError());
                exit(1);
            }
        }
        
        printf("Send once.\n");
        Sleep(1000);
    } while (true);

    closesocket(socket);
    WSACleanup();
    return 0;
}
