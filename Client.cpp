
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

	//�����������Ӳſ��԰��µ���Ϣ
	while (true) {
		//��һ������˼�ǽ�֮ǰ��userrname��password�������,��server�Ͽ�����#Ϊ��־���зָ�ж�
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
			printf("���ӳɹ��������ɹ�!\n");
		}

		printf("�������û�����");
		char username[40];
		gets_s(username);
		//username��#�����username,��ͬ�γ�username
		strcat(username, "#");

		printf("���������룺");
		char password[40];
		gets_s(password);
		strcat(username, password);

		//�����û���������
		send(sclient, username, strlen(username), 0);

		//���ܵõ��Ľ��
		recv(sclient, recData, 255, 0);
		printf(recData);


		//while���������˼����ѭ������server��

		//���õ��Ľ�����Ƿ�ɹ����бȶ�
		int m_strcmp = strcmp(recData, "��½�ɹ���\n");

		//���ݵõ��Ľ���жϽ�����������
		if (0 == m_strcmp)
		{

			while (true) {
				printf("��������Ҫ���͵���Ϣ:");

				gets_s(m_SendData);
				send(sclient, m_SendData, strlen(recData), 0);
			}
		}
		else {

			closesocket(sclient);
			WSACleanup();
			ZeroMemory(m_SendData, strlen(m_SendData));
			printf("����һ�ε�½\n");
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

//��½�ɹ�֮�������߳̽�������client����Ϣ
/*DWORD WINAPI client_recvThreads(LPVOID LPlparameter) {
	char recvBuf[256];

	while (true) {
		ZeroMemory(recvBuf, 256);
		recv(sclient, recvBuf, 256, 0);
		printf(recvBuf);
	}
}*/
