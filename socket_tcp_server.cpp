#include "socket_tcp_server.h"
#include<winsock2.h>
#include<iostream>
#include<string>
using namespace std;
#pragma comment(lib,"ws2_32.lib")
#define PORT 8888
#define SERVER_PORT      8554
#define SERVER_RTP_PORT  55532
#define SERVER_RTCP_PORT 55533
#define err(errMsg) cout<<"[line:%d]%s failed code %d"<<__LINE__<<errMsg<<WSAGetLastError()<<endl;

static SOCKET createTcpSocket()
{
    SOCKET Server_Socket_fd;
    int on = 1;

    //创建Socket
    //parm1:af 地址协议族{ipv4,ipv6}
    //parm2:type 协议传输类型{stream,datagram}
    //parm3:protocol 使用具体的传输协议{TCP,UDP}
    Server_Socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Server_Socket_fd == INVALID_SOCKET)
    {
        err(socket);
        return 1;
    }

    setsockopt(Server_Socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

    return Server_Socket_fd;
}

static int bindSocketAddr(SOCKET Server_Socket_fd, const char* ip, int port)
{

    //给Socket绑定ip地址和端口号
    struct sockaddr_in sockAddr{};
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(port);//把本地字节序转换为网络字节序
    sockAddr.sin_addr.S_un.S_addr = inet_addr(ip);//绑定本地网卡的任意ip

    if (bind(Server_Socket_fd, (sockaddr*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR){
        err(bind);
        return 1;
    }

    return 0;
}

static SOCKET acceptClient(SOCKET sockfd, char* ip, int* port)
{
    SOCKET clientfd;
    sockaddr_in client_sin{};
    int len = sizeof(client_sin);

    memset(&client_sin, 0, sizeof(client_sin));
    len = sizeof(client_sin);

    clientfd = accept(sockfd, (struct sockaddr*)&client_sin, &len);
    if (clientfd < 0){
        err(accept);
        return 1;
    }

    strcpy(ip, inet_ntoa(client_sin.sin_addr));
    *port = ntohs(client_sin.sin_port);

    return clientfd;
}

static int handleCmd_OPTIONS(char* result, int cseq)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Public: OPTIONS, DESCRIBE, SETUP, PLAY\r\n"
                    "\r\n",
            cseq);

    return 0;
}

static int handleCmd_DESCRIBE(char* result, int cseq, char* url)
{
    char sdp[500];
    char localIp[100];

    sscanf(url, "rtsp://%[^:]:", localIp);
    sprintf(sdp, "v=0\r\n"
                 "o=- 9%ld 1 IN IP4 %s\r\n"
                 "t=0 0\r\n"
                 "a=control:*\r\n"
                 "m=video 0 RTP/AVP 96\r\n"
                 "a=rtpmap:96 H264/90000\r\n"
                 "a=control:track0\r\n",
            time(NULL), localIp);

    sprintf(result, "RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
                    "Content-Base: %s\r\n"
                    "Content-type: application/sdp\r\n"
                    "Content-length: %zu\r\n\r\n"
                    "%s",
            cseq,
            url,
            strlen(sdp),
            sdp);

    return 0;
}

static int handleCmd_SETUP(char* result, int cseq, int clientRtpPort)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
                    "Session: 66334873\r\n"
                    "\r\n",
            cseq,
            clientRtpPort,
            clientRtpPort + 1,
            SERVER_RTP_PORT,
            SERVER_RTCP_PORT);

    return 0;
}

static int handleCmd_PLAY(char* result, int cseq)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Range: npt=0.000-\r\n"
                    "Session: 66334873; timeout=10\r\n\r\n",
            cseq);

    return 0;
}

static void doClient(SOCKET clientSockfd, const char* clientIP, int clientPort) {

    char method[40];
    char url[100];
    char version[40];
    int CSeq;

    int clientRtpPort, clientRtcpPort;
//    char* rBuf = (char*)malloc(10000);
//    char* sBuf = (char*)malloc(10000);
    char recvBuf[10000];
    char sendBuf[10000];

    while (true) {
        int recvLen;

        recvLen = recv(clientSockfd, recvBuf, 2000, 0);
        if (recvLen <= 0) {
            err(recv);
            break;
        }

        recvBuf[recvLen] = '\0';
        string recvStr = recvBuf;
        if (recvLen > 0)
        {
            recvBuf[recvLen] = '\0';
            cout <<"Client say->"<< recvBuf << endl;

        }
//        printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
//        printf("%s rBuf = %s \n", __FUNCTION__, rBuf);

        const char* sep = "\n";
        char* line = strtok(recvBuf, sep);
        while (line) {
            if (strstr(line, "OPTIONS") ||
                strstr(line, "DESCRIBE") ||
                strstr(line, "SETUP") ||
                strstr(line, "PLAY")) {

                if (sscanf(line, "%s %s %s\r\n", method, url, version) != 3) {
                    // error
                }
            }
            else if (strstr(line, "CSeq")) {
                if (sscanf(line, "CSeq: %d\r\n", &CSeq) != 1) {
                    // error
                }
            }
            else if (!strncmp(line, "Transport:", strlen("Transport:"))) {
                // Transport: RTP/AVP/UDP;unicast;client_port=13358-13359
                // Transport: RTP/AVP;unicast;client_port=13358-13359

                if (sscanf(line, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
                           &clientRtpPort, &clientRtcpPort) != 2) {
                    // error
                    printf("parse Transport error \n");
                }
            }
            line = strtok(NULL, sep);
        }

        if (!strcmp(method, "OPTIONS")) {
            if (handleCmd_OPTIONS(sendBuf, CSeq))
            {
                printf("failed to handle options\n");
                break;
            }
        }
        else if (!strcmp(method, "DESCRIBE")) {
            if (handleCmd_DESCRIBE(sendBuf, CSeq, url))
            {
                printf("failed to handle describe\n");
                break;
            }

        }
        else if (!strcmp(method, "SETUP")) {
            if (handleCmd_SETUP(sendBuf, CSeq, clientRtpPort))
            {
                printf("failed to handle setup\n");
                break;
            }
        }
        else if (!strcmp(method, "PLAY")) {
            if (handleCmd_PLAY(sendBuf, CSeq))
            {
                printf("failed to handle play\n");
                break;
            }
        }
        else {
            printf("undefined method = %s \n", method);
            break;
        }
        cout <<"Server say->"<< sendBuf << endl;
//        printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
//        printf("%s sBuf = %s \n", __FUNCTION__, sendBuf);

        send(clientSockfd, sendBuf, strlen(sendBuf), 0);


        //开始播放，发送RTP包
        if (!strcmp(method, "PLAY")) {

            printf("start play\n");
            printf("client ip:%s\n", clientIP);
            printf("client port:%d\n", clientRtpPort);

            while (true) {


                Sleep(40);
                //usleep(40000);//1000/25 * 1000
            }

            break;
        }

        memset(method, 0, sizeof(method) / sizeof(char));
        memset(url, 0, sizeof(url) / sizeof(char));
        CSeq = 0;

    }

    closesocket(clientSockfd);
//    free(rBuf);
//    free(sBuf);

}
int RTSP_Server(){
    //初始化DLL
    WORD sockVersion = MAKEWORD(2, 2);
    WSADATA wsdata;
    if (WSAStartup(sockVersion, &wsdata) != 0)
    {
        //cout<<"WSAStartup failed code %d"<<WSAGetLastError()<<endl;
        err(WSAStartup);
        return -1;
    }

    // 启动windows socket end

    SOCKET serverSockfd;
    serverSockfd = createTcpSocket();
    if (serverSockfd < 0)
    {
        WSACleanup();
        cout<<"failed to create tcp socket"<<endl;
        return -1;
    }

    if (bindSocketAddr(serverSockfd, "0.0.0.0", SERVER_PORT) < 0)
    {
        cout<<"failed to bind addr"<<endl;
        return -1;
    }

    if (listen(serverSockfd, 10) < 0)
    {
        err(listen)
        return -1;
    }

    cout<<"RTSP_Server>"<<"rtsp://127.0.0.1:"<<SERVER_PORT<<endl;

    while (true) {
        SOCKET clientSockfd;
        char clientIp[40];
        int clientPort;

        clientSockfd = acceptClient(serverSockfd, clientIp, &clientPort);
        if (clientSockfd < 0)
        {
            printf("failed to accept client\n");
            return -1;
        }

        printf("accept client;client ip:%s,client port:%d\n", clientIp, clientPort);

        doClient(clientSockfd, clientIp, clientPort);
    }
    closesocket(serverSockfd);
    return 0;
}

int socket_tcp_server(){

    //初始化DLL
    WORD sockVersion = MAKEWORD(2, 2);
    WSADATA wsdata;
    if (WSAStartup(sockVersion, &wsdata) != 0)
    {
        //cout<<"WSAStartup failed code %d"<<WSAGetLastError()<<endl;
        err(WSAStartup);
        return 1;
    }

    //创建Socket
    //parm1:af 地址协议族{ipv4,ipv6}
    //parm2:type 协议传输类型{stream,datagram}
    //parm3:protocol 使用具体的传输协议{TCP,UDP}
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET)
    {
        cout << "Socket error" << endl;
        return 1;
    }


    //给Socket绑定ip地址和端口号
    sockaddr_in sockAddr{};
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(PORT);//把本地字节序转换为网络字节序
    sockAddr.sin_addr.S_un.S_addr = INADDR_ANY;//绑定本地网卡的任意ip

    if (bind(serverSocket, (sockaddr*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR){
        cout << "Bind error" << endl;
        return 1;
    }

    //开始监听
    if (listen(serverSocket, 10) == SOCKET_ERROR){
        cout << "Listen error" << endl;
        return 1;
    }


    SOCKET clientSocket;
    sockaddr_in client_sin{};
    char msg[100];//存储传送的消息
    int flag = 0;//是否已经连接上
    int len = sizeof(client_sin);
    while (true){
        if (!flag)
            cout << "wait link..." << endl;
        clientSocket = accept(serverSocket, (sockaddr*)&client_sin, &len);
        if (clientSocket == INVALID_SOCKET){
            cout << "Accept error" << endl;
            flag = 0;
            return 1;
        }
        if (!flag)
            cout << "receive a link: " << inet_ntoa(client_sin.sin_addr) << endl;
        flag = 1;
        int num = recv(clientSocket, msg, 100, 0);
        if (num > 0)
        {
            msg[num] = '\0';
            cout <<"Client say->"<< msg << endl;

        }

        string data;
        getline(cin, data);
        const char * sendData;
        sendData = data.c_str();
        send(clientSocket, sendData, strlen(sendData), 0);
        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}