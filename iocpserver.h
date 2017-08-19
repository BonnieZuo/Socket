#include "stdafx.h"
#include <stdio.h>
#include <winsock2.h> 
#include "ws2tcpip.h" 
#include "mswsock.h"
#pragma comment(lib,"ws2_32.lib") 

//�������Ĵ�С
#define DataBuf 4096

//ͬʱͶ�ݵ�AcceptEx���������
#define MAX_POST_ACCEPT 10

//����socket����������
#define c_Socket_Num  2048

//�û�������
char* username[2] = { "admin","root"};
//��������
char* password[2] = { "adminadmin","rootroot"};

enum OPERATION_TYPE { ACCEPT, RECV, SEND, NONE, ROOT };

//���ü�������
//ÿһ�����ü���������socketpool�е���С��һ����Ԫ
//��ʵ����socket
class c_SocketCount{
public:
	//��������
	volatile int m_UserNum;
	//�����ü����Ķ���
	SOCKET m_socket;

	//���캯��
	c_SocketCount() {
		//��ʼ����������
		m_UserNum = 0;
		//��ʼ��SOCKET
		m_socket = WSASocket(AF_INET, SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
	}

	SOCKET* Retain() {
		//ÿʹ��һ��socket��num�Լӣ�num�൱����ʹ�ø�socket������
		m_UserNum++;
		return &m_socket;
	}

	void Release() {
		//��һ����ʹ��socket��num�Լ�����numΪ0ʱ�Ϳ����ͷŵ����socket
		m_UserNum--;

		//����������������㣬��ر�socket
		if (m_UserNum == 0)
		{
			closesocket(m_socket);
			m_socket = WSASocket(AF_INET,SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
		}
	}

};


// ���������õ�socket, �����ڴ�
class c_SocketCountPool {
private:
	volatile int num;
public:
	//pool�е���С��Ԫ��c_SocketCount���ҷ���ֵΪ����������ǰ׺������һ������
	c_SocketCount* SOCKET_ARR[2048];//ֻ����������˿ռ䣬����c_SocketCountû�пռ�洢

	//��ʼ�����socket������
	c_SocketCountPool():num(c_Socket_Num)
	{

		for (int i = 0; i <  c_Socket_Num; i++)
		{
			SOCKET_ARR[i] = new c_SocketCount;
		}
	}

	//�õ�pool�е�c_SocketCount
	c_SocketCount* getSocket() {
		for (int i = 0; i < num; i++)
		{
			//����õ���socketû����ʹ�õĻ���ȡ����socketʹ��
			if (SOCKET_ARR[i]->m_UserNum == 0)
			{
				//�������е����socketȡ��
				return SOCKET_ARR[i];
			}
		}
	}
};

//����socketpool
c_SocketCountPool* g_SocketCountPool;

//����Overlapped��������socket���������Լ�������������ͣ�accpet��received����send
class PER_IO_CONTEXT
{
public:
	c_SocketCount* m_ScoketCount;
	OVERLAPPED     m_Overlapped;                               // ÿһ���ص�����������ص��ṹ(���ÿһ��Socket��ÿһ����������Ҫ��һ��          
	SOCKET*         m_socket;                                     // ������������ʹ�õ�Socket
	WSABUF         m_wsaBuf;                                   // WSA���͵Ļ����������ڸ��ص�������������
	char           m_szBuffer[DataBuf];                           // �����WSABUF�������ַ��Ļ�����
	OPERATION_TYPE m_OpType;									// ʶ���������������
	PER_IO_CONTEXT* Pre;		//˫�����������ָ��һ��ָ��ǰ��Ľڵ�һ��ָ�����Ľڵ�
	PER_IO_CONTEXT* Next;

	//PER_IO_CONTEXT *HeadIoContext;						//�õ�head�ڵ�

	// ��ʼ��
	PER_IO_CONTEXT(c_SocketCount* p, OPERATION_TYPE m_Type)
	{
		//��ֵ
		//�жϲ���ʼ��һ��socket
		m_ScoketCount = p;
		if (ROOT == m_Type)
		{
			m_socket = 0;
		}
		else 
		{
			m_socket = p->Retain();
		}

		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
		ZeroMemory(m_szBuffer, DataBuf);
		m_wsaBuf.buf = m_szBuffer;
		m_wsaBuf.len = DataBuf;
		m_OpType = m_Type;
		Pre = NULL;
		Next = NULL;

	}

	// �ͷ�IO����
	//���io�����������ͷŵ����������
	~PER_IO_CONTEXT()
	{
		if (ROOT != m_OpType)
		{
			if (NULL != Next)
			{
				//������Ľڵ�ɾ������һ��������һ���ϣ���һ��������һ����
				Pre->Next = Next;
				Next->Pre = Pre;
			}
			else
			{
				Pre->Next = NULL;
			}
		}
		m_ScoketCount->Release();
		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));
		m_OpType = NONE;
		Pre = NULL;
		Next = NULL;
	}

	// ���û���������
	//���õ�����:�ڵ�һ��Ͷ��ʧ�ܵ�ʱ����ջ�����
	void ResetBuffer()
	{
		ZeroMemory(m_szBuffer, DataBuf);
	}
};

class PER_SOCKET_CONTEXT
{
private:
	PER_IO_CONTEXT *HeadIoContext;
public:
	c_SocketCount* m_SocketCount;
	SOCKET*      m_Socket;                                  // ÿһ���ͻ������ӵ�Socket
	SOCKADDR_IN m_ClientAddr;                              // �ͻ��˵ĵ�ַ
	char m_username[40];

	PER_SOCKET_CONTEXT* pSocket_Pre;								//	socketҲ����Ϊ˫������ָ��ǰ�ڵ��Preָ��
	PER_SOCKET_CONTEXT* pSocket_Next;								//	socketҲ����Ϊ˫������ָ���ڵ��Nextָ��

	// ��ʼ��
	PER_SOCKET_CONTEXT(c_SocketCount* p)			//�������ĺ������������ʵ��p����ֵ�������m_SocketCount
	{
		m_SocketCount = p;							//��ʼ��m_SocketCount
		m_Socket = m_SocketCount->Retain();
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
		ZeroMemory(m_username, 40);

		//���ݲ��������ж�Ϊͷ��㣬����ͷ�ڵ�
		HeadIoContext = new PER_IO_CONTEXT(m_SocketCount,ROOT);
	
		PER_SOCKET_CONTEXT* Socket_Pre = NULL;								
		PER_SOCKET_CONTEXT* Socket_Next = NULL;
	}


	//****************************************************************************
	//
	//
	//Ϊ�˷����ڹر�socket��ʱ�����е������е�io������ɾ����io����д��socket��
	//Ҳ���Բ�д��socket��
	//
	//*****************************************************************************

	//������һ����������ָ��������Ϊǰ׺
	PER_IO_CONTEXT* getNewIoContext(OPERATION_TYPE m_Type) {	//������ص���PER_IO_CONTEXT,����������������ʱ�����õ�PER_IO_CONTEXT*


				//ʹ��ָ�뽫�µ�IO�����ŵ�������
					//�����µ�IO����
		PER_IO_CONTEXT* temp = new PER_IO_CONTEXT(m_SocketCount,m_Type);

		//===========================================
		//ʹ��ͷ�巨���µ�IO��������
		//============================================

		if (NULL != HeadIoContext->Next) {						//���ͷ�����治Ϊ��
			HeadIoContext->Next->Pre = temp;
			temp->Next = HeadIoContext->Next;
		}
		HeadIoContext->Next = temp;
		temp->Pre = HeadIoContext;
		temp->Next = NULL;
		return temp;
	}


	// �ͷ���Դ
	~PER_SOCKET_CONTEXT()
	{
		if (*m_Socket != INVALID_SOCKET)
		{
			while (NULL != HeadIoContext->Next)
			{
				HeadIoContext->Next->~PER_IO_CONTEXT();
			}
			HeadIoContext->~PER_IO_CONTEXT();
			HeadIoContext = NULL;
			m_SocketCount->Release();
			m_Socket = NULL;
			memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
			ZeroMemory(m_username, 40);
		}

		//�ͷ���ԴͬIo��ģʽ
		if (NULL != pSocket_Next)
		{
			pSocket_Pre->pSocket_Next = pSocket_Next;
			pSocket_Next->pSocket_Pre = pSocket_Pre;
		}
		else 
		{
			pSocket_Pre->pSocket_Next = NULL;
		}

		pSocket_Next = NULL;
		pSocket_Pre = NULL;
	}
};

//Socket�ṹ��������࣬��������Socket��Ͻṹ�����飬���Ը�������ɾ��
class PER_SOCKET_CONTEXT_LIST {
private:
	//����ͷ�ڵ�
	PER_SOCKET_CONTEXT* Head_Socket_context;
public:
	//��socketpool�еõ�һ��SOCKET��Ϊͷ�ڵ㣬��������ΪROOT
	volatile int num;
	
	PER_SOCKET_CONTEXT_LIST() {
		num = 0;
		c_SocketCount* p = g_SocketCountPool->getSocket();
		Head_Socket_context = new PER_SOCKET_CONTEXT(p);		//֮ǰ�����PER_SOCKET_CONTEXTΪ���ι��캯��
	}
	PER_SOCKET_CONTEXT* GetNewSocketContext(SOCKADDR_IN* addr, char* u) 
	{
		//�ӳ��еõ�socket
		PER_SOCKET_CONTEXT* temp = new PER_SOCKET_CONTEXT(g_SocketCountPool->getSocket());
		if (NULL != Head_Socket_context->pSocket_Next)
		{
			Head_Socket_context->pSocket_Next->pSocket_Pre = temp;
			temp->pSocket_Next = Head_Socket_context->pSocket_Next;
		}

		Head_Socket_context->pSocket_Next = temp;
		temp->pSocket_Pre = Head_Socket_context;

		memcpy(&(temp->m_ClientAddr), addr, sizeof(SOCKADDR_IN));
		strcpy_s(temp->m_username, strlen(u) + 1, u);
		num++;
		return temp;
	}


	//ͨ��userid��Ҳ�����û���)�ҵ���Ӧ��socket
	PER_SOCKET_CONTEXT* Find_Socket(char* userid) 
	{
		PER_SOCKET_CONTEXT* temp = Head_Socket_context;
		while (NULL != temp->pSocket_Next)
		{
			temp = temp->pSocket_Next;
			if (0 == strcmp(userid, temp->m_username))
			{
				return temp;
			}
		}
	}

	bool ConnnetServer(SOCKADDR_IN* addr) 
	{
		PER_SOCKET_CONTEXT* temp = Head_Socket_context;
		while (NULL != Head_Socket_context->pSocket_Next)
		{
			temp = temp->pSocket_Next;
			if (0 == memcpy(&(temp->m_ClientAddr), addr, sizeof(SOCKADDR_IN)))
			{
				return true;
			}
		}
	}

};

/*class PER_SOCKET_CONTEXT_ARR
{
private:
	PER_SOCKET_CONTEXT *SOCKET_CONTEXT_ARR[2048];
public:


	PER_SOCKET_CONTEXT* GetNewSocketContext(SOCKADDR_IN* addr, char* u)
	{
		for (int i = 0; i < 2048; i++)
		{
			//���ĳһ��IO_CONTEXT_ARRAY[i]Ϊ0����ʾ��һ��λ���Է���PER_IO_CONTEXT  
			if (SOCKET_CONTEXT_ARR[i] == NULL)
			{
				SOCKET_CONTEXT_ARR[i] = new PER_SOCKET_CONTEXT(p);
				memcpy(&(SOCKET_CONTEXT_ARR[i]->m_ClientAddr), addr, sizeof(SOCKADDR_IN));
				strcpy_s(SOCKET_CONTEXT_ARR[i]->m_username, strlen(u) + 1, u);
				return SOCKET_CONTEXT_ARR[i];
			}
		}
		return NULL;
	}

	PER_SOCKET_CONTEXT* getARR(int i)
	{
		return SOCKET_CONTEXT_ARR[i];
	}

	// ���������Ƴ�һ��ָ����IoContext
	void RemoveContext(PER_SOCKET_CONTEXT* S)
	{
		for (int i = 0; i < 2048; i++)
		{
			if (SOCKET_CONTEXT_ARR[i] == S)
			{
				closesocket(SOCKET_CONTEXT_ARR[i]->m_Socket);
				SOCKET_CONTEXT_ARR[i] = NULL;
				break;
			}
		}
	}
};*/

//����ÿ���ͻ��˵�socket����ַ�˿ڣ��û���


//��ɽӿ�
HANDLE mIoCompletionPort;

//AcceptEx����ָ��
GUID GuidAcceptEx = WSAID_ACCEPTEX;
LPFN_ACCEPTEX mAcceptEx;
//AcceptExSockaddrs��GUID�����ڵ���AcceptExSockaddrs����ָ��
GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
LPFN_GETACCEPTEXSOCKADDRS mAcceptExSockAddrs;

//����������Listen��Socket�ṹ��
PER_SOCKET_CONTEXT* ListenContext;


PER_SOCKET_CONTEXT_LIST* m_Per_Socket_Context_List;


PER_SOCKET_CONTEXT* m_Per_Socket_Context;


//����������ɶ˿ڲ������߳�
DWORD WINAPI workThread(LPVOID lpParam);
//����Ͷ��Send���󣬷�������Ϣ���֪ͨ��ɶ˿�
bool PostSend(PER_IO_CONTEXT* pIoContext);
//����Ͷ��Recv���󣬽����������֪ͨ��ɶ˿�
bool PostRecv(PER_IO_CONTEXT* pIoContext);
//����Ͷ��Accept�����յ�һ�����������֪ͨ��ɶ˿�
bool PostAccept(PER_IO_CONTEXT* pAcceptIoContext);



