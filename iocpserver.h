#include "stdafx.h"
#include <stdio.h>
#include <winsock2.h> 
#include "ws2tcpip.h" 
#include "mswsock.h"
#pragma comment(lib,"ws2_32.lib") 

//缓冲区的大小
#define DataBuf 4096

//同时投递的AcceptEx请求的数量
#define MAX_POST_ACCEPT 10

//创建socket的最大的数量
#define c_Socket_Num  2048

//用户名数组
char* username[2] = { "admin","root"};
//密码数组
char* password[2] = { "adminadmin","rootroot"};

enum OPERATION_TYPE { ACCEPT, RECV, SEND, NONE, ROOT };

//引用计数基类
//每一个引用计数对象是socketpool中的最小的一个单元
//其实就是socket
class c_SocketCount{
public:
	//计数变量
	volatile int m_UserNum;
	//被引用计数的对象
	SOCKET m_socket;

	//构造函数
	c_SocketCount() {
		//初始化计数变量
		m_UserNum = 0;
		//初始化SOCKET
		m_socket = WSASocket(AF_INET, SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
	}

	SOCKET* Retain() {
		//每使用一次socket则num自加，num相当于是使用该socket的人数
		m_UserNum++;
		return &m_socket;
	}

	void Release() {
		//少一个人使用socket则num自减，到num为0时就可以释放掉这个socket
		m_UserNum--;

		//如果计数变量等于零，则关闭socket
		if (m_UserNum == 0)
		{
			closesocket(m_socket);
			m_socket = WSASocket(AF_INET,SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
		}
	}

};


// 创建池来得到socket, 管理内存
class c_SocketCountPool {
private:
	volatile int num;
public:
	//pool中的最小单元是c_SocketCount，且返回值为它所以是其前缀并定义一个数组
	c_SocketCount* SOCKET_ARR[2048];//只给数组分配了空间，但是c_SocketCount没有空间存储

	//初始化存放socket的数组
	c_SocketCountPool():num(c_Socket_Num)
	{

		for (int i = 0; i <  c_Socket_Num; i++)
		{
			SOCKET_ARR[i] = new c_SocketCount;
		}
	}

	//得到pool中的c_SocketCount
	c_SocketCount* getSocket() {
		for (int i = 0; i < num; i++)
		{
			//如果得到的socket没有人使用的话则取出该socket使用
			if (SOCKET_ARR[i]->m_UserNum == 0)
			{
				//将数组中的这个socket取出
				return SOCKET_ARR[i];
			}
		}
	}
};

//定义socketpool
c_SocketCountPool* g_SocketCountPool;

//包含Overlapped，关联的socket，缓冲区以及这个操作的类型，accpet，received还是send
class PER_IO_CONTEXT
{
public:
	c_SocketCount* m_ScoketCount;
	OVERLAPPED     m_Overlapped;                               // 每一个重叠网络操作的重叠结构(针对每一个Socket的每一个操作，都要有一个          
	SOCKET*         m_socket;                                     // 这个网络操作所使用的Socket
	WSABUF         m_wsaBuf;                                   // WSA类型的缓冲区，用于给重叠操作传参数的
	char           m_szBuffer[DataBuf];                           // 这个是WSABUF里具体存字符的缓冲区
	OPERATION_TYPE m_OpType;									// 识别网络操作的类型
	PER_IO_CONTEXT* Pre;		//双向链表的两个指针一个指向前面的节点一个指向后面的节点
	PER_IO_CONTEXT* Next;

	//PER_IO_CONTEXT *HeadIoContext;						//得到head节点

	// 初始化
	PER_IO_CONTEXT(c_SocketCount* p, OPERATION_TYPE m_Type)
	{
		//赋值
		//判断并初始化一个socket
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

	// 释放IO操作
	//如果io操作结束则释放掉里面的内容
	~PER_IO_CONTEXT()
	{
		if (ROOT != m_OpType)
		{
			if (NULL != Next)
			{
				//将本身的节点删除，上一个挂在下一个上，下一个挂在上一个上
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

	// 重置缓冲区内容
	//重置的作用:在第一次投递失败的时候清空缓冲区
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
	SOCKET*      m_Socket;                                  // 每一个客户端连接的Socket
	SOCKADDR_IN m_ClientAddr;                              // 客户端的地址
	char m_username[40];

	PER_SOCKET_CONTEXT* pSocket_Pre;								//	socket也声明为双向链表，指向前节点的Pre指针
	PER_SOCKET_CONTEXT* pSocket_Next;								//	socket也声明为双向链表，指向后节点的Next指针

	// 初始化
	PER_SOCKET_CONTEXT(c_SocketCount* p)			//带参数的函数，将传入的实参p，赋值给定义的m_SocketCount
	{
		m_SocketCount = p;							//初始化m_SocketCount
		m_Socket = m_SocketCount->Retain();
		memset(&m_ClientAddr, 0, sizeof(m_ClientAddr));
		ZeroMemory(m_username, 40);

		//根据操作类型判断为头结点，定义头节点
		HeadIoContext = new PER_IO_CONTEXT(m_SocketCount,ROOT);
	
		PER_SOCKET_CONTEXT* Socket_Pre = NULL;								
		PER_SOCKET_CONTEXT* Socket_Next = NULL;
	}


	//****************************************************************************
	//
	//
	//为了方便在关闭socket的时候将所有的链表中的io操作都删除将io操作写到socket中
	//也可以不写到socket中
	//
	//*****************************************************************************

	//返回哪一个类型则将其指针类型作为前缀
	PER_IO_CONTEXT* getNewIoContext(OPERATION_TYPE m_Type) {	//这个返回的是PER_IO_CONTEXT,所以在声明方法的时候是用的PER_IO_CONTEXT*


				//使用指针将新的IO操作放到链表中
					//声明新的IO操作
		PER_IO_CONTEXT* temp = new PER_IO_CONTEXT(m_SocketCount,m_Type);

		//===========================================
		//使用头插法将新的IO操作放入
		//============================================

		if (NULL != HeadIoContext->Next) {						//如果头结点后面不为空
			HeadIoContext->Next->Pre = temp;
			temp->Next = HeadIoContext->Next;
		}
		HeadIoContext->Next = temp;
		temp->Pre = HeadIoContext;
		temp->Next = NULL;
		return temp;
	}


	// 释放资源
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

		//释放资源同Io的模式
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

//Socket结构体数组的类，包含上面Socket组合结构体数组，并对改数组增删改
class PER_SOCKET_CONTEXT_LIST {
private:
	//声明头节点
	PER_SOCKET_CONTEXT* Head_Socket_context;
public:
	//从socketpool中得到一个SOCKET作为头节点，操作类型为ROOT
	volatile int num;
	
	PER_SOCKET_CONTEXT_LIST() {
		num = 0;
		c_SocketCount* p = g_SocketCountPool->getSocket();
		Head_Socket_context = new PER_SOCKET_CONTEXT(p);		//之前定义的PER_SOCKET_CONTEXT为带参构造函数
	}
	PER_SOCKET_CONTEXT* GetNewSocketContext(SOCKADDR_IN* addr, char* u) 
	{
		//从池中得到socket
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


	//通过userid（也就是用户名)找到相应的socket
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
			//如果某一个IO_CONTEXT_ARRAY[i]为0，表示哪一个位可以放入PER_IO_CONTEXT  
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

	// 从数组中移除一个指定的IoContext
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

//包含每个客户端的socket，地址端口，用户名


//完成接口
HANDLE mIoCompletionPort;

//AcceptEx函数指针
GUID GuidAcceptEx = WSAID_ACCEPTEX;
LPFN_ACCEPTEX mAcceptEx;
//AcceptExSockaddrs的GUID，用于导出AcceptExSockaddrs函数指针
GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
LPFN_GETACCEPTEXSOCKADDRS mAcceptExSockAddrs;

//接下来用来Listen的Socket结构体
PER_SOCKET_CONTEXT* ListenContext;


PER_SOCKET_CONTEXT_LIST* m_Per_Socket_Context_List;


PER_SOCKET_CONTEXT* m_Per_Socket_Context;


//声明用来完成端口操作的线程
DWORD WINAPI workThread(LPVOID lpParam);
//声明投递Send请求，发送完消息后会通知完成端口
bool PostSend(PER_IO_CONTEXT* pIoContext);
//声明投递Recv请求，接收完请求会通知完成端口
bool PostRecv(PER_IO_CONTEXT* pIoContext);
//声明投递Accept请求，收到一个连接请求会通知完成端口
bool PostAccept(PER_IO_CONTEXT* pAcceptIoContext);



