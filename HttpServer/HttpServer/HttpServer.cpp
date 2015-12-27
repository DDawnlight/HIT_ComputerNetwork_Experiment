// HttpServer.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <assert.h> 

#pragma comment(lib, "Ws2_32.lib")

#define MAXSIZE 65507              //发送数据报文的最大长度
#define HTTP_PORT 80               //HTTP服务器端口

//HTTP重要头部数据
struct HttpHeader
{
	char method[4];                //POST或GET，注意有些为CONNECT，本实验暂不考虑
	char url[1024];                //请求的url
	char host[1024];               //目标主机
	char cookie[1024 * 10];        //cookie
	HttpHeader()                   //ZeroMemory将指定的内存块清零，结构体清零
	{
		ZeroMemory(this, sizeof(HttpHeader));
	}
};

BOOL InitSocket();
void ParseHttpHeader(char *buffer, HttpHeader *httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//由于新的连接都使用新的线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
/*
const int ProxyThreadMaxNum = 20;
HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = { 0 };
DWORD ProxyThreadDW[ProxyThreadMaxNum] = { 0 };
*/

struct ProxyParam
{
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket())
	{
		printf("socket初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);

	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam *lpProxyParam;
	//HANDLE hThread;
	//DWORD dwThreadID;

	//代理服务器不断监听
	while (true)
	{
		acceptSocket = accept(ProxyServer, NULL, NULL);
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL)
		{
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;

		//hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);

		QueueUserWorkItem((LPTHREAD_START_ROUTINE)ProxyThread, (LPVOID)lpProxyParam, WT_EXECUTEINLONGTHREAD);

		//CloseHandle(hThread);

		Sleep(200);
	}

	closesocket(ProxyServer);
	WSACleanup();
	//system("pause");
	return 0;
}

//***************************************
//Method:    InitSocket
//FullName:  InitSocket
//Access:    public
//Returns:   BOOL
//Qualifier: 初始化套接字
//****************************************
BOOL InitSocket()
{
	//加载套接字库（必须）
	WORD wVersionRequested;
	//WSADATA结构被用来储存调用AfxSocketInit全局函数返回的Windows Sockets初始化信息。
	WSADATA wsaData;
	//套接字加载错误时提示
	int err;
	//WINSOCK版本2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载dll文件Socket库
	err = WSAStartup(wVersionRequested, &wsaData);

	if (err != 0)
	{
		//找不到winsock.dll
		printf("加载winsock失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的winsock版本\n");
		WSACleanup();
		return FALSE;
	}
	//指定何种地址类型PF_INET, AF_INET： Ipv4网络协议
	//type参数的作用是设置通信的协议类型，SOCK_STREAM： 提供面向连接的稳定数据传输，即TCP协议
	//参数protocol用来指定socket所使用的传输协议编号。这一参数通常不具体设置，一般设置为0即可
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);

	if (INVALID_SOCKET == ProxyServer)
	{
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}

	//sin_family表示协议簇，一般用AF_INET表示TCP/IP协议
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	//任意地址
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;

	if ( bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR )
	{
		printf("绑定套接字失败\n");
		return FALSE;
	}

	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("监听端口%d失败\n", ProxyPort);
		return FALSE;
	}

	return TRUE;
}

//***************************************
//Method:    ProxyThread
//FullName:  ProxyThread
//Access:    public
//Returns:   unsigned int __stdcall
//Qualifier: 线程执行函数
//Parameter: LPVOID lpParameter
//****************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
	char Buffer[MAXSIZE];
	char *CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE);

	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;

	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0)
	{
		goto error;
	}

	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
	ParseHttpHeader(CacheBuffer, httpHeader);
	delete CacheBuffer;
	// 网址过滤
	
	if (!strcmp(httpHeader->host, "donghua.dmzj.com"))
	{
		printf("网站过滤\n");
		goto error;
	}
	
	/*
	if (!strcmp(httpHeader->host, "www.sohu.com"))
	{
		memcpy(httpHeader->host, "donghua.dmzj.com", MAXSIZE);
	}
	*/
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host))
	{
		goto error;
	}

	printf("代理连接主机 %s 成功\n", httpHeader->host);

	//将客户端发送的HTTP数据报文直接转发给目标服务器
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	
	FILE *stream;
	fopen_s(&stream, "cache.txt", "w+");
	fprintf(stream, "%s", Buffer);
	fclose(stream);
	
	if (recvSize <= 0)
	{
		goto error;
	}

	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);

	//错误处理
error:
	printf("关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	//_endthreadex(0);
	return 0;
}

//***************************************
//Method:    ParseHttpHead
//FullName:  ParseHttpHead
//Access:    public
//Returns:   void
//Qualifier: 解析TCP报文中的HTTP头部
//Parameter: char *buffer
//Parameter: HttpHeader *httpHeader
//****************************************
void ParseHttpHeader(char *buffer, HttpHeader *httpHeader)
{
	char *p;
	char *ptr;
	const char* delim = "\r\n";

	//提取第一行
	p = strtok_s(buffer, delim, &ptr);
	printf("%s\n", p);

	//GET方式
	if (p[0] == 'G')
	{
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	//POST方式
	else if (p[0] == 'P')
	{
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);
	//实际为第二个指针，指向截断位置
	p = strtok_s(NULL, delim, &ptr);
	while (p)
	{
		switch (p[0])
		{
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C'://Cookie
			if (strlen(p) > 8)
			{
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie"))
				{
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//***************************************
//Method:    ConnectToServer
//FullName:  ConnectToServer
//Access:    public
//Returns:   BOOL
//Qualifier: 根据主机创建目标服务器套接字并连接
//Parameter: SOCKET *serverSocket
//Parameter: char *host
//****************************************
BOOL ConnectToServer(SOCKET *serverSocket, char *host)
{
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT *hostent = gethostbyname(host);
	if (!hostent)
	{
		return FALSE;
	}

	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET)
	{
		return FALSE;
	}

	if (connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}