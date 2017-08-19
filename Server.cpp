//���̣߳�
//1.������ɶ˿�
//2.����һ�������׽���
//3.�������׽��ֹ�������ɶ˿���
//4.�Լ����׽��ֵ���bind()��listen()
//5.ͨ��WSAIoctl��ȡAcceptEx��GetAcceptExSockaddrs������ָ��
//���̣߳�
//1.��ü����˿ڵ�״̬
//2.��״̬�����ֽ������Ĳ�����1.��Accept������2.��RECV������3.��SEND������
//������
//1.����struct�ṹ����װ���е���Ϣ����Ϊsocket��Io��װ��Ϣ,����accept���������Ĳ�����ʱ��Ϳ���ֱ��ȡ����
//2.Ҫ����ɶ˿ڵ���ϢͶ�ݵ�accept�Ȳ����У�����Ҫд����post����

#include "stdafx.h"
#include <winsock2.h> 
#include "ws2tcpip.h" 
#include "iocpserver.h"



int main()
{
	WSADATA wsaData;
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		printf_s("��ʼ��Socket�� ʧ�ܣ�\n");
		return 1;
	}

	// ������ɶ˿�
	mIoCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (mIoCompletionPort == NULL)
	{
		printf_s("������ɶ˿�ʧ�ܣ��������: %d!\n", WSAGetLastError());
		return 2;
	}

	SYSTEM_INFO si;
	GetSystemInfo(&si);
	// ���ݱ����еĴ�����������������Ӧ���߳���
	int m_nThreads = 2 * si.dwNumberOfProcessors + 2;
	// ��ʼ���߳̾��
	HANDLE* m_phWorkerThreads = new HANDLE[m_nThreads];
	// ���ݼ�����������������߳�
	for (int i = 0; i < m_nThreads; i++)
	{
		m_phWorkerThreads[i] = CreateThread(0, 0, workThread, NULL, 0, NULL);
	}
	printf_s("���� WorkerThread %d ��.\n", m_nThreads);

	// ��������ַ��Ϣ�����ڰ�Socket
	struct sockaddr_in ServerAddress;

	// �������ڼ�����Socket����Ϣ
	ListenContext = new PER_SOCKET_CONTEXT(g_SocketCountPool->getSocket());

	// ��Ҫʹ���ص�IO�������ʹ��WSASocket������Socket���ſ���֧���ص�IO����
	*ListenContext->m_Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (*ListenContext->m_Socket == INVALID_SOCKET)
	{
		printf_s("��ʼ��Socketʧ�ܣ��������: %d.\n", WSAGetLastError());
	}
	else
	{
		printf_s("��ʼ��Socket���.\n");
	}

	// ����ַ��Ϣ
	ZeroMemory(&ServerAddress, sizeof(ServerAddress));
	ServerAddress.sin_family = AF_INET;
	ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	ServerAddress.sin_port = htons(8080);

	// �󶨵�ַ�Ͷ˿�
	if (bind(*ListenContext->m_Socket, (struct sockaddr *) &ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
	{
		printf_s("bind()����ִ�д���.\n");
		return 4;
	}

	// ��ʼ�����ListenContext�����socket���󶨵ĵ�ַ�˿ڽ��м���
	if (listen(*ListenContext->m_Socket, SOMAXCONN) == SOCKET_ERROR)
	{
		printf_s("Listen()����ִ�г��ִ���.\n");
		return 5;
	}

	//�������ListenSocket�ṹ��ŵ���ɶ˿��У��н�������ң���������ListenContext����ȥ
	if ((CreateIoCompletionPort((HANDLE)ListenContext->m_Socket, mIoCompletionPort, (DWORD)ListenContext, 0) == NULL))
	{
		printf_s("�󶨷����SocketContext����ɶ˿�ʧ�ܣ��������: %d\n", WSAGetLastError());
		if (*ListenContext->m_Socket != INVALID_SOCKET)
		{
			closesocket(*ListenContext->m_Socket);
			*ListenContext->m_Socket = INVALID_SOCKET;
		}
		return 3;
	}
	else
	{
		printf_s("Listen Socket����ɶ˿� ���.\n");
	}

	DWORD dwBytes = 0;
	//ʹ��WSAIoctl��ͨ��GuidAcceptEx(AcceptEx��GUID)����ȡAcceptEx����ָ��
	if (SOCKET_ERROR == WSAIoctl(
		*ListenContext->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&mAcceptEx,
		sizeof(mAcceptEx),
		&dwBytes,
		NULL,
		NULL))
	{
		printf_s("WSAIoctl δ�ܻ�ȡAcceptEx����ָ�롣�������: %d\n", WSAGetLastError());
		return 6;
	}

	//ʹ��WSAIoctl��ͨ��GuidGetAcceptExSockAddrs(AcceptExSockaddrs��GUID)����ȡAcceptExSockaddrs����ָ��
	if (SOCKET_ERROR == WSAIoctl(
		*ListenContext->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs),
		&mAcceptExSockAddrs,
		sizeof(mAcceptExSockAddrs),
		&dwBytes,
		NULL,
		NULL))
	{
		printf_s("WSAIoctl δ�ܻ�ȡGuidGetAcceptExSockAddrs����ָ�롣�������: %d\n", WSAGetLastError());
		return 7;
	}

	//ѭ��10��
	for (int i = 0; i < MAX_POST_ACCEPT; i++)
	{
		//ͨ����������ṹ��������һ���µ���������ṹ��
		PER_IO_CONTEXT* newAcceptIoContext = m_Per_Socket_Context->getNewIoContext(ACCEPT);
		//Ͷ��Send���󣬷�������Ϣ���֪ͨ��ɶ˿ڣ�
		if (PostAccept(newAcceptIoContext) == false)
		{
			return false;
		}
	}
	printf_s("Ͷ�� %d ��AcceptEx������� \n", MAX_POST_ACCEPT);

	printf_s("��������������......\n");

	//���߳�����������exit�˳�
	bool run = true;
	while (run)
	{
		char st[40];
		gets_s(st);

		if (!strcmp("exit", st))
		{
			run = false;
		}
	}
	WSACleanup();

	return 0;
}

//����������ɶ˿ڲ������߳�
DWORD WINAPI workThread(LPVOID lpParam)
{
	//���������ɺ���յ���������ṹ�������Overlapped

	OVERLAPPED           *pOverlapped = NULL;
	//���������ɺ���յ�Socket�ṹ�壬��һ����ListenSocket�Ľṹ��
	PER_SOCKET_CONTEXT   *pListenContext = NULL;
	//���������ɺ���յ��ֽ��� 
	DWORD                dwBytesTransfered = 0;

	// ѭ����������
	while (true)
	{
		BOOL bReturn = GetQueuedCompletionStatus(
			mIoCompletionPort,//����������ǽ������Ǹ�Ψһ����ɶ˿�  
			&dwBytesTransfered,//����ǲ�����ɺ󷵻ص��ֽ��� 
			(PULONG_PTR)&pListenContext,//��������ǽ�����ɶ˿ڵ�ʱ��󶨵��Ǹ��Զ���ṹ�����  
			&pOverlapped,//���������������Socket��ʱ��һ�������Ǹ��ص��ṹ  
			INFINITE);//�ȴ���ɶ˿ڵĳ�ʱʱ�䣬����̲߳���Ҫ�����������飬�Ǿ�INFINITE

					  //ͨ�����Overlapped���õ������������������ṹ��
		PER_IO_CONTEXT* pIoContext = CONTAINING_RECORD(pOverlapped, PER_IO_CONTEXT, m_Overlapped);

		// �ж��Ƿ��пͻ��˶Ͽ���
		if (!bReturn)
		{
			DWORD dwErr = GetLastError();
			//�������64���ͻ���closesocket
			if (dwErr == 64) {
				
				//����ͻ������Ӵ�����ֱ���Ƴ����socket������ص�io����
				pListenContext->~PER_SOCKET_CONTEXT();
				printf_s("�ͻ��� %s:%d �Ͽ����ӣ�\n", inet_ntoa(pListenContext->m_ClientAddr.sin_addr), ntohs(pListenContext->m_ClientAddr.sin_port));
			}
			else {
				printf_s("�ͻ����쳣�Ͽ� %d\n", dwErr);
			}
			continue;
		}
		else
		{
			//�ж�����������������
			switch (pIoContext->m_OpType)
			{
			case ACCEPT:
			{
				// 1. ����ȡ������ͻ��˵ĵ�ַ��Ϣ(�鿴ҵ��Ա�Ӵ��Ŀͻ���Ϣ)
				SOCKADDR_IN* ClientAddr = NULL;
				SOCKADDR_IN* LocalAddr = NULL;
				int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);
				mAcceptExSockAddrs(pIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2),
					sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&LocalAddr, &localLen, (LPSOCKADDR*)&ClientAddr, &remoteLen);

				printf_s("�ͻ��� %s:%d ����.\n", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));
				//while(true){
				//���յ��û���
				char *input_username = new char[40];
				//���յ�����
				char *input_password = new char[40];

				//�����ַ���Ϊ �û���#���� �Ľṹ����Ҫstrtsuccess_s�ָ
				input_username = strtok_s(pIoContext->m_wsaBuf.buf, "#", &input_password);

				//�������ӿͻ��˵��û���
				char *user = new char[40];
				strcpy_s(user, strlen(input_username) + 1, input_username);

				//�Ƿ��½�ɹ�
				bool success = false;

				if (strlen(input_username) > 0 && strlen(input_password) > 0)
				{
					//�����˺��Ƿ����
					for (int i = 0; i < sizeof(username) / sizeof(username[0]); i++) {
						int j = 0;

						for (j = 0; username[i][j] == input_username[j] && input_username[j]; j++);
						if (username[i][j] == input_username[j] && input_username[j] == 0)
						{
							//�˺Ŵ��ڲ��������Ƿ���ȷ
							int k;
							for (k = 0; password[i][k] == input_password[k] && input_password[k]; k++);
							if (password[i][k] == input_password[k] && input_password[k] == 0)
							{
								success = true;
							}
							break;
						}
					}
				}


				//�����Ƿ��½�ɹ�����Ҫ����һ��������ͻ��� ��½�ɹ� or ��½ʧ��
				//ͨ��Socket�ṹ������õ�һ���µ�Socket�ṹ�壬�����û�����Ϣ�����ȥ
				PER_SOCKET_CONTEXT* newSocketContext = m_Per_Socket_Context_List->GetNewSocketContext(ClientAddr,input_username);
				//��Socket�ṹ�屣�浽Socket�ṹ���������»�õ�Socket�ṹ����
				newSocketContext->m_Socket = pIoContext->m_socket;
				//���ͻ��˵ĵ�ַ���浽Socket�ṹ���������»�õ�Socket�ṹ����
				memcpy(&(newSocketContext->m_ClientAddr), ClientAddr, sizeof(SOCKADDR_IN));

				//������µõ���Socket�ṹ��ŵ���ɶ˿���
				HANDLE hTemp = CreateIoCompletionPort((HANDLE)newSocketContext->m_Socket, mIoCompletionPort, (DWORD)newSocketContext, 0);
				if (NULL == hTemp)
				{
					printf_s("ִ��CreateIoCompletionPort���ִ���%d\n", GetLastError());
					break;
				}

				//send��Ϣ
				PER_IO_CONTEXT* newSendContext = newSocketContext->getNewIoContext(SEND);
				//����pIoContext�е���Ϣ����pIoContext�е���Ϣ���͸��ͻ���
				memcpy(&(newSendContext->m_wsaBuf), &pIoContext->m_wsaBuf, pIoContext->m_wsaBuf.len);
				newSendContext->m_socket = newSocketContext->m_Socket;
				if (success)
				{
					printf_s("�ͻ��� %s(%s:%d) ��½�ɹ���\n", user, inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));


					//֮ǰ�Ĵ���:buffer�Ĵ�С̫С������д��һ��\n���з���ʱ��11��ֵ�Ѿ���С��װ���£����԰�11�ĳ���12
					//�м����ֵ����˼ʽȷ��ǰ���newSendContext�Ĵ�С
					//=======================================================
					//strcpy��strcpy_s�Ĳ�ͬ
					//strcpy���ܿ϶����Ƶ�buffer�Ĵ�С�����Կ��ܻ�������
					//strcpy_s�����Լ��趨buffer�Ĵ�С�������strcpy��˵����ȫ
					//============================================================

					strcpy_s(newSendContext->m_wsaBuf.buf, 12, "��½�ɹ���\n");

				}
				else {
					printf_s("�ͻ��� %s(%s:%d) ��½ʧ�ܣ�\n", user, inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));
					strcpy_s(newSendContext->m_wsaBuf.buf, 12, "��½ʧ�ܣ�\n");

				}
				PostSend(newSendContext);

				
				if (success)
				{
					PER_IO_CONTEXT* newRecvContext = newSocketContext->getNewIoContext(RECV);
					newRecvContext->m_socket = newSocketContext->m_Socket;
					newRecvContext->m_OpType = RECV;
					if (!PostRecv(newRecvContext))
					{
						printf_s("Ͷ�ݳ��ִ���\n");
						newRecvContext->~PER_IO_CONTEXT();
					}
					
				}
				else {
					
				}
				pIoContext->ResetBuffer();
				//����Ͷ��һ��accept
				PostAccept(pIoContext);
			}
			break;
			case RECV:
			{
				char* username = pListenContext->m_username;
				//ִ��recv�󣬽��н������ݵĴ���������Ŀͻ��ˣ�����recv
				if (dwBytesTransfered > 1)
				{
					//�õ�������Ϣ���ڴ�ռ䣬���������һ��
					char* SendData = new char[DataBuf];
					ZeroMemory(SendData, DataBuf);
					
					//�������˽��н������ݵĿ���

					//��ӡ���ͻ��˷��͵���Ϣ
					printf_s("�����û�%s:%s:%d����Ϣ,����ϢΪ:%s\n", pListenContext->m_username, inet_ntoa(pListenContext->m_ClientAddr.sin_addr), ntohs(pListenContext->m_ClientAddr.sin_port), pIoContext->m_wsaBuf.buf);
					
					//������Ϣ�Ŀ���
					//sprintf_s(SendData, DataBuf, "�����û�%s,%s:%d����Ϣ,����ϢΪ:%s\n", pListenContext->m_username, inet_ntoa(pListenContext->m_ClientAddr.sin_addr), ntohs(pListenContext->m_ClientAddr.sin_port), pIoContext->m_wsaBuf.buf);

					//�յ�ĳһsocket����Ϣ������ת��
					for (int i = 0; i < 2048; i++)
					{
						PER_SOCKET_CONTEXT* pSocketContext = m_Per_Socket_Context_List->Find_Socket(username);
						if (pSocketContext == nullptr) {
							break;
						}
						//����������е�socket���飬���Լ���socket�򲻽���ת��
						if (pSocketContext->m_Socket == ListenContext->m_Socket)
						{
							continue;
						}


						/*//������Ϣ��ת��
						PER_IO_CONTEXT* SendIoContext = pSocketContext->getNewIoContext();
						memcpy(&SendIoContext->m_wsaBuf, &SendData, sizeof(SendData));
						printf("%s",SendData);
						SendIoContext->m_socket = pSocketContext->m_Socket;
						PostSend(SendIoContext);*/
					}
				}
				pIoContext->ResetBuffer();
				PostRecv(pIoContext);
			}
			break;

			//���ܵ���Ϣ֮����ֱ�ӹر����IO����
			case SEND:
				pIoContext->~PER_IO_CONTEXT();
				break;
			default:
				printf_s("_WorkThread�е� pIoContext->m_OpType �����쳣.\n");
				break;
			}
		}
		
	}
	printf_s("�߳��˳�.\n");
	return 0;
}

//����Ͷ��Send���󣬷�������Ϣ���֪ͨ��ɶ˿�
bool PostSend(PER_IO_CONTEXT* SendIoContext)
{
	// ��ʼ������
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	SendIoContext->m_OpType = SEND;
	WSABUF *p_wbuf = &SendIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &SendIoContext->m_Overlapped;

	SendIoContext->ResetBuffer();

	if ((WSASend(*SendIoContext->m_socket, p_wbuf, 1, &dwBytes, dwFlags, p_ol,
		NULL) == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
	{
		printf_s("%d", GetLastError());
		ZeroMemory(&SendIoContext->m_wsaBuf, sizeof(&SendIoContext->m_wsaBuf));
		return false;
	}

	return true;
}


//����Ͷ��Recv���󣬽����������֪ͨ��ɶ˿�
bool PostRecv(PER_IO_CONTEXT* RecvIoContext)
{
	// ��ʼ������
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	RecvIoContext->m_OpType = RECV;

	WSABUF *p_wbuf = &RecvIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &RecvIoContext->m_Overlapped;

	RecvIoContext->ResetBuffer();

	int nBytesRecv = WSARecv(*RecvIoContext->m_socket, p_wbuf, 1, &dwBytes, &dwFlags, p_ol, NULL);

	// �������ֵ���󣬲��Ҵ���Ĵ��벢����Pending�Ļ����Ǿ�˵������ص�����ʧ����
	if (nBytesRecv == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING))
	{
		if (WSAGetLastError() != 10054) {
			printf_s("Ͷ��һ��WSARecvʧ�ܣ�%d \n", WSAGetLastError());
		}
		return false;
	}
	return true;
}

//����Ͷ��Accept�����յ�һ�����������֪ͨ��ɶ˿�
bool PostAccept(PER_IO_CONTEXT* AcceptIoContext)
{
	// ׼������
	DWORD dwBytes = 0;
	AcceptIoContext->m_OpType = ACCEPT;
	WSABUF *p_wbuf = &AcceptIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &AcceptIoContext->m_Overlapped;

	// Ϊ�Ժ�������Ŀͻ�����׼����Socket(׼���ýӴ��ͻ���ҵ��Ա����������ͳAccept�ֳ�newһ������)
	*AcceptIoContext->m_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (*AcceptIoContext->m_socket == INVALID_SOCKET)
	{
		printf_s("��������Accept��Socket�ɹ���\n");
		return false;
	}

	// Ͷ��AcceptEx
	if (mAcceptEx(*ListenContext->m_Socket,*AcceptIoContext->m_socket, p_wbuf->buf, p_wbuf->len - ((sizeof(SOCKADDR_IN) + 16) * 2),
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &dwBytes, p_ol) == FALSE)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			printf_s("Ͷ�� AcceptEx ����ʧ�ܣ��������: %d\n", WSAGetLastError());
			return false;
		}
	}
	return true;
}