//主线程：
//1.创建完成端口
//2.创建一个监听套接字
//3.将监听套接字关联到完成端口中
//4.对监听套接字调用bind()、listen()
//5.通过WSAIoctl获取AcceptEx、GetAcceptExSockaddrs函数的指针
//子线程：
//1.获得监听端口的状态
//2.以状态来区分接下来的操作（1.做Accept操作，2.做RECV操作，3.做SEND操作）
//其他：
//1.创建struct结构体来装所有的信息，分为socket和Io来装信息,在做accept或者其他的操作的时候就可以直接取出来
//2.要将完成端口的信息投递到accept等操作中，所以要写三个post操作

#include "stdafx.h"
#include <winsock2.h> 
#include "ws2tcpip.h" 
#include "iocpserver.h"



int main()
{
	WSADATA wsaData;
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		printf_s("初始化Socket库 失败！\n");
		return 1;
	}

	// 建立完成端口
	mIoCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (mIoCompletionPort == NULL)
	{
		printf_s("建立完成端口失败！错误代码: %d!\n", WSAGetLastError());
		return 2;
	}

	SYSTEM_INFO si;
	GetSystemInfo(&si);
	// 根据本机中的处理器数量，建立对应的线程数
	int m_nThreads = 2 * si.dwNumberOfProcessors + 2;
	// 初始化线程句柄
	HANDLE* m_phWorkerThreads = new HANDLE[m_nThreads];
	// 根据计算出来的数量建立线程
	for (int i = 0; i < m_nThreads; i++)
	{
		m_phWorkerThreads[i] = CreateThread(0, 0, workThread, NULL, 0, NULL);
	}
	printf_s("建立 WorkerThread %d 个.\n", m_nThreads);

	// 服务器地址信息，用于绑定Socket
	struct sockaddr_in ServerAddress;

	// 生成用于监听的Socket的信息
	ListenContext = new PER_SOCKET_CONTEXT(g_SocketCountPool->getSocket());

	// 需要使用重叠IO，必须得使用WSASocket来建立Socket，才可以支持重叠IO操作
	*ListenContext->m_Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (*ListenContext->m_Socket == INVALID_SOCKET)
	{
		printf_s("初始化Socket失败，错误代码: %d.\n", WSAGetLastError());
	}
	else
	{
		printf_s("初始化Socket完成.\n");
	}

	// 填充地址信息
	ZeroMemory(&ServerAddress, sizeof(ServerAddress));
	ServerAddress.sin_family = AF_INET;
	ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	ServerAddress.sin_port = htons(8080);

	// 绑定地址和端口
	if (bind(*ListenContext->m_Socket, (struct sockaddr *) &ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
	{
		printf_s("bind()函数执行错误.\n");
		return 4;
	}

	// 开始对这个ListenContext里面的socket所绑定的地址端口进行监听
	if (listen(*ListenContext->m_Socket, SOMAXCONN) == SOCKET_ERROR)
	{
		printf_s("Listen()函数执行出现错误.\n");
		return 5;
	}

	//将这个将ListenSocket结构体放到完成端口中，有结果告诉我，并将监听ListenContext传进去
	if ((CreateIoCompletionPort((HANDLE)ListenContext->m_Socket, mIoCompletionPort, (DWORD)ListenContext, 0) == NULL))
	{
		printf_s("绑定服务端SocketContext至完成端口失败！错误代码: %d\n", WSAGetLastError());
		if (*ListenContext->m_Socket != INVALID_SOCKET)
		{
			closesocket(*ListenContext->m_Socket);
			*ListenContext->m_Socket = INVALID_SOCKET;
		}
		return 3;
	}
	else
	{
		printf_s("Listen Socket绑定完成端口 完成.\n");
	}

	DWORD dwBytes = 0;
	//使用WSAIoctl，通过GuidAcceptEx(AcceptEx的GUID)，获取AcceptEx函数指针
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
		printf_s("WSAIoctl 未能获取AcceptEx函数指针。错误代码: %d\n", WSAGetLastError());
		return 6;
	}

	//使用WSAIoctl，通过GuidGetAcceptExSockAddrs(AcceptExSockaddrs的GUID)，获取AcceptExSockaddrs函数指针
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
		printf_s("WSAIoctl 未能获取GuidGetAcceptExSockAddrs函数指针。错误代码: %d\n", WSAGetLastError());
		return 7;
	}

	//循环10次
	for (int i = 0; i < MAX_POST_ACCEPT; i++)
	{
		//通过网络操作结构体数组获得一个新的网络操作结构体
		PER_IO_CONTEXT* newAcceptIoContext = m_Per_Socket_Context->getNewIoContext(ACCEPT);
		//投递Send请求，发送完消息后会通知完成端口，
		if (PostAccept(newAcceptIoContext) == false)
		{
			return false;
		}
	}
	printf_s("投递 %d 个AcceptEx请求完毕 \n", MAX_POST_ACCEPT);

	printf_s("服务器端已启动......\n");

	//主线程阻塞，输入exit退出
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

//定义用来完成端口操作的线程
DWORD WINAPI workThread(LPVOID lpParam)
{
	//网络操作完成后接收的网络操作结构体里面的Overlapped

	OVERLAPPED           *pOverlapped = NULL;
	//网络操作完成后接收的Socket结构体，第一次是ListenSocket的结构体
	PER_SOCKET_CONTEXT   *pListenContext = NULL;
	//网络操作完成后接收的字节数 
	DWORD                dwBytesTransfered = 0;

	// 循环处理请求
	while (true)
	{
		BOOL bReturn = GetQueuedCompletionStatus(
			mIoCompletionPort,//这个就是我们建立的那个唯一的完成端口  
			&dwBytesTransfered,//这个是操作完成后返回的字节数 
			(PULONG_PTR)&pListenContext,//这个是我们建立完成端口的时候绑定的那个自定义结构体参数  
			&pOverlapped,//这个是我们在连入Socket的时候一起建立的那个重叠结构  
			INFINITE);//等待完成端口的超时时间，如果线程不需要做其他的事情，那就INFINITE

					  //通过这个Overlapped，得到包含这个的网错操作结构体
		PER_IO_CONTEXT* pIoContext = CONTAINING_RECORD(pOverlapped, PER_IO_CONTEXT, m_Overlapped);

		// 判断是否有客户端断开了
		if (!bReturn)
		{
			DWORD dwErr = GetLastError();
			//错误代码64，客户端closesocket
			if (dwErr == 64) {
				
				//如果客户端连接错误则直接移除这个socket和其相关的io操作
				pListenContext->~PER_SOCKET_CONTEXT();
				printf_s("客户端 %s:%d 断开连接！\n", inet_ntoa(pListenContext->m_ClientAddr.sin_addr), ntohs(pListenContext->m_ClientAddr.sin_port));
			}
			else {
				printf_s("客户端异常断开 %d\n", dwErr);
			}
			continue;
		}
		else
		{
			//判断这个网络操作的类型
			switch (pIoContext->m_OpType)
			{
			case ACCEPT:
			{
				// 1. 首先取得连入客户端的地址信息(查看业务员接待的客户信息)
				SOCKADDR_IN* ClientAddr = NULL;
				SOCKADDR_IN* LocalAddr = NULL;
				int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);
				mAcceptExSockAddrs(pIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2),
					sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&LocalAddr, &localLen, (LPSOCKADDR*)&ClientAddr, &remoteLen);

				printf_s("客户端 %s:%d 连接.\n", inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));
				//while(true){
				//接收的用户名
				char *input_username = new char[40];
				//接收的密码
				char *input_password = new char[40];

				//接收字符串为 用户名#密码 的结构，需要strtsuccess_s分割开
				input_username = strtok_s(pIoContext->m_wsaBuf.buf, "#", &input_password);

				//保存连接客户端的用户名
				char *user = new char[40];
				strcpy_s(user, strlen(input_username) + 1, input_username);

				//是否登陆成功
				bool success = false;

				if (strlen(input_username) > 0 && strlen(input_password) > 0)
				{
					//查找账号是否存在
					for (int i = 0; i < sizeof(username) / sizeof(username[0]); i++) {
						int j = 0;

						for (j = 0; username[i][j] == input_username[j] && input_username[j]; j++);
						if (username[i][j] == input_username[j] && input_username[j] == 0)
						{
							//账号存在查找密码是否正确
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


				//无论是否登陆成功，都要反馈一个结果给客户端 登陆成功 or 登陆失败
				//通过Socket结构体数组得到一个新的Socket结构体，并将用户性信息保存进去
				PER_SOCKET_CONTEXT* newSocketContext = m_Per_Socket_Context_List->GetNewSocketContext(ClientAddr,input_username);
				//将Socket结构体保存到Socket结构体数组中新获得的Socket结构体中
				newSocketContext->m_Socket = pIoContext->m_socket;
				//将客户端的地址保存到Socket结构体数组中新获得的Socket结构体中
				memcpy(&(newSocketContext->m_ClientAddr), ClientAddr, sizeof(SOCKADDR_IN));

				//将这个新得到的Socket结构体放到完成端口中
				HANDLE hTemp = CreateIoCompletionPort((HANDLE)newSocketContext->m_Socket, mIoCompletionPort, (DWORD)newSocketContext, 0);
				if (NULL == hTemp)
				{
					printf_s("执行CreateIoCompletionPort出现错误%d\n", GetLastError());
					break;
				}

				//send消息
				PER_IO_CONTEXT* newSendContext = newSocketContext->getNewIoContext(SEND);
				//复制pIoContext中的信息，将pIoContext中的信息发送给客户端
				memcpy(&(newSendContext->m_wsaBuf), &pIoContext->m_wsaBuf, pIoContext->m_wsaBuf.len);
				newSendContext->m_socket = newSocketContext->m_Socket;
				if (success)
				{
					printf_s("客户端 %s(%s:%d) 登陆成功！\n", user, inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));


					//之前的错误:buffer的大小太小，当我写入一个\n换行符的时候，11的值已经过小，装不下，所以把11改成了12
					//中间的数值的意思式确定前面的newSendContext的大小
					//=======================================================
					//strcpy和strcpy_s的不同
					//strcpy不能肯定复制的buffer的大小，所以可能会造成溢出
					//strcpy_s可以自己设定buffer的大小，相较于strcpy来说更安全
					//============================================================

					strcpy_s(newSendContext->m_wsaBuf.buf, 12, "登陆成功！\n");

				}
				else {
					printf_s("客户端 %s(%s:%d) 登陆失败！\n", user, inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));
					strcpy_s(newSendContext->m_wsaBuf.buf, 12, "登陆失败！\n");

				}
				PostSend(newSendContext);

				
				if (success)
				{
					PER_IO_CONTEXT* newRecvContext = newSocketContext->getNewIoContext(RECV);
					newRecvContext->m_socket = newSocketContext->m_Socket;
					newRecvContext->m_OpType = RECV;
					if (!PostRecv(newRecvContext))
					{
						printf_s("投递出现错误\n");
						newRecvContext->~PER_IO_CONTEXT();
					}
					
				}
				else {
					
				}
				pIoContext->ResetBuffer();
				//继续投递一个accept
				PostAccept(pIoContext);
			}
			break;
			case RECV:
			{
				char* username = pListenContext->m_username;
				//执行recv后，进行接收数据的处理，发给别的客户端，并再recv
				if (dwBytesTransfered > 1)
				{
					//得到接受信息的内存空间，并将其清空一次
					char* SendData = new char[DataBuf];
					ZeroMemory(SendData, DataBuf);
					
					//服务器端进行接受数据的拷贝

					//打印出客户端发送的消息
					printf_s("接收用户%s:%s:%d的信息,其信息为:%s\n", pListenContext->m_username, inet_ntoa(pListenContext->m_ClientAddr.sin_addr), ntohs(pListenContext->m_ClientAddr.sin_port), pIoContext->m_wsaBuf.buf);
					
					//进行消息的拷贝
					//sprintf_s(SendData, DataBuf, "接收用户%s,%s:%d的信息,其信息为:%s\n", pListenContext->m_username, inet_ntoa(pListenContext->m_ClientAddr.sin_addr), ntohs(pListenContext->m_ClientAddr.sin_port), pIoContext->m_wsaBuf.buf);

					//收到某一socket的消息，并且转发
					for (int i = 0; i < 2048; i++)
					{
						PER_SOCKET_CONTEXT* pSocketContext = m_Per_Socket_Context_List->Find_Socket(username);
						if (pSocketContext == nullptr) {
							break;
						}
						//如果遍历所有的socket数组，是自己的socket则不进行转发
						if (pSocketContext->m_Socket == ListenContext->m_Socket)
						{
							continue;
						}


						/*//进行消息的转发
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

			//接受到消息之后则直接关闭这个IO请求
			case SEND:
				pIoContext->~PER_IO_CONTEXT();
				break;
			default:
				printf_s("_WorkThread中的 pIoContext->m_OpType 参数异常.\n");
				break;
			}
		}
		
	}
	printf_s("线程退出.\n");
	return 0;
}

//定义投递Send请求，发送完消息后会通知完成端口
bool PostSend(PER_IO_CONTEXT* SendIoContext)
{
	// 初始化变量
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


//定义投递Recv请求，接收完请求会通知完成端口
bool PostRecv(PER_IO_CONTEXT* RecvIoContext)
{
	// 初始化变量
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	RecvIoContext->m_OpType = RECV;

	WSABUF *p_wbuf = &RecvIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &RecvIoContext->m_Overlapped;

	RecvIoContext->ResetBuffer();

	int nBytesRecv = WSARecv(*RecvIoContext->m_socket, p_wbuf, 1, &dwBytes, &dwFlags, p_ol, NULL);

	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if (nBytesRecv == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING))
	{
		if (WSAGetLastError() != 10054) {
			printf_s("投递一个WSARecv失败！%d \n", WSAGetLastError());
		}
		return false;
	}
	return true;
}

//定义投递Accept请求，收到一个连接请求会通知完成端口
bool PostAccept(PER_IO_CONTEXT* AcceptIoContext)
{
	// 准备参数
	DWORD dwBytes = 0;
	AcceptIoContext->m_OpType = ACCEPT;
	WSABUF *p_wbuf = &AcceptIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &AcceptIoContext->m_Overlapped;

	// 为以后新连入的客户端先准备好Socket(准备好接待客户的业务员，而不是像传统Accept现场new一个出来)
	*AcceptIoContext->m_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (*AcceptIoContext->m_socket == INVALID_SOCKET)
	{
		printf_s("创建用于Accept的Socket成功！\n");
		return false;
	}

	// 投递AcceptEx
	if (mAcceptEx(*ListenContext->m_Socket,*AcceptIoContext->m_socket, p_wbuf->buf, p_wbuf->len - ((sizeof(SOCKADDR_IN) + 16) * 2),
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &dwBytes, p_ol) == FALSE)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			printf_s("投递 AcceptEx 请求失败，错误代码: %d\n", WSAGetLastError());
			return false;
		}
	}
	return true;
}