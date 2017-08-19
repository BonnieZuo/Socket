
#include "stdafx.h"
#include "stdio.h"
#include "winsock2.h" 
#include <string.h>
#pragma comment(lib,"ws2_32.lib")  

//DWORD WINAPI client_recvThreads(LPVOID LPlparameter);
SOCKET sclient;
char recData[255];
char m_SendData[256];

int main()
{

	//进行重新连接才可以把新的消息
	while (true) {
		//这一步的意思是将之前的userrname和password结合起来,再server断可以以#为标志进行分割，判断
		WORD sockVersion = MAKEWORD(2, 2);
		WSADATA data;
		if (WSAStartup(sockVersion, &data) != 0)
		{
			return 0;
		}

		SOCKET sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sclient == INVALID_SOCKET)
		{
			printf("invalid socket !");
			return 0;
		}

		sockaddr_in serAddr;
		serAddr.sin_family = AF_INET;
		serAddr.sin_port = htons(8080);
		serAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
		if (connect(sclient, (sockaddr *)&serAddr, sizeof(serAddr)) == SOCKET_ERROR)
		{
			printf("connect error !");
			closesocket(sclient);
			return 0;
		}
		else {
			printf("连接成功服务器成功!\n");
		}

		printf("请输入用户名：");
		char username[40];
		gets_s(username);
		//username和#结合是username,共同形成username
		strcat(username, "#");

		printf("请输入密码：");
		char password[40];
		gets_s(password);
		strcat(username, password);

		//发送用户名和密码
		send(sclient, username, strlen(username), 0);

		//接受得到的结果
		recv(sclient, recData, 255, 0);
		printf(recData);


		//while在这里的意思就是循环访问server端

		//将得到的结果与是否成功进行比对
		int m_strcmp = strcmp(recData, "登陆成功！\n");

		//根据得到的结果判断接下来怎样做
		if (0 == m_strcmp)
		{

			while (true) {
				printf("请输入您要发送的信息:");

				gets_s(m_SendData);
				send(sclient, m_SendData, strlen(recData), 0);
			}
		}
		else {

			closesocket(sclient);
			WSACleanup();
			ZeroMemory(m_SendData, strlen(m_SendData));
			printf("请再一次登陆\n");
			continue;
		}
		//HANDLE h_client_recvThreads = CreateThread(0, NULL, client_recvThreads, NULL, 0, NULL);
	}

	bool run = true;
	while (run)
	{
		char str[40];
		gets_s(str);
		if (0 == strcmp(str, "exit"))
		{
			run = false;
		}
		WSACleanup();
	}
	return 0;
}

//登陆成功之后开启该线程接受其他client的消息
/*DWORD WINAPI client_recvThreads(LPVOID LPlparameter) {
	char recvBuf[256];

	while (true) {
		ZeroMemory(recvBuf, 256);
		recv(sclient, recvBuf, 256, 0);
		printf(recvBuf);
	}
}*/
