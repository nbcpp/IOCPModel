#include "stdafx.h"
#include "IOCPModel.h"


CIOCPModel::CIOCPModel()
	:m_nPort(DEFAULT_PORT),
	 m_numThreads(0),
	 m_hIOCP(NULL),
	 m_hQuitEvent(NULL),
	 m_phWorkerThreads(nullptr),
	 m_pListenContext(nullptr),
	 m_lpfnAcceptEx(nullptr),
	 m_lpfnGetAcceptExSockAddrs(nullptr)
{
	

	//��ʼ���׽��ֿ�
	LoadSocketLib();

}


CIOCPModel::~CIOCPModel()
{
	StopServer();
}

//�̺߳���
DWORD WINAPI CIOCPModel::WorkerThreadFun(LPVOID lpParam)
{
	//��ȡ����
	THREADPARAM_WORKER *pParam = (THREADPARAM_WORKER *)lpParam;
	CIOCPModel * pIOCPModel = (CIOCPModel *)pParam->m_IOCPModel;
	int nThreadNo = pParam->m_noThread;

	printf("�������߳�������ID��%d\n", nThreadNo);
	
	OVERLAPPED *ol = nullptr;
	PPER_SOCKET_CONTEXT pSocketContext = nullptr;
	DWORD dwBytestransferred = 0;

	//�ȴ��¼��˳�
	while (WAIT_OBJECT_0 != WaitForSingleObject(pIOCPModel->m_hQuitEvent, 0))
	{
		bool retVal = GetQueuedCompletionStatus(pIOCPModel->m_hIOCP,&dwBytestransferred,
			(PULONG_PTR)&pSocketContext,&ol,INFINITE);
		if (EXIT_CODE == (DWORD)pSocketContext)
		{
			break;
		}
		//�����˴���  �������
		else if (!retVal)
		{
			DWORD dwErr = GetLastError();
			if (!pIOCPModel->SolveHandleError(pSocketContext, dwErr))
			{
				break;
			}
		}
		//��ʼ��������
		else 
		{
			//Ѱ����ol��ͷ��per_io_context�ĵ�io����
			PPER_IO_CONTEXT pIoContext = CONTAINING_RECORD(ol, PER_IO_CONTEXT, m_overLapped);
			//�ͻ��˶Ͽ���
			if ((0 == dwBytestransferred) && (SEND == pIoContext->m_type || RECV == pIoContext->m_type))
			{
				//����Ͽ��Ŀͻ��˵���Ϣ
				printf("�ͻ��� %s:%d�Ͽ�����!\n", inet_ntoa(pSocketContext->m_clientAddr.sin_addr), 
					ntohs(pSocketContext->m_clientAddr.sin_port));
				pIOCPModel->RemoveSocketContext(pSocketContext);
			}
			else 
			{
				//�ֱ������ֲ�������
				switch (pIoContext->m_type)
				{
				case ACCEPT:
					{
						pIOCPModel->DoAccept(pSocketContext,pIoContext);
					}
					break;
				case SEND:
					{
						pIOCPModel->DoSend(pSocketContext, pIoContext);
					}
					break;
				case RECV:
					{
						pIOCPModel->DoRecv(pSocketContext, pIoContext);
					}
					break;
				default:
					printf("WorkThread�е� pIoContext->m_OpType �����쳣.\n");
					break;
				}
			}
		}
	}
	printf("�������߳� %d ���˳���\n", nThreadNo);
	Sleep(10);
	RELEASE(pParam);
	return 0;
}

bool CIOCPModel::LoadSocketLib()
{
	WSADATA wsaData;
	//���ִ���
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR)
	{
		printf("��ʼ��winsock 2.2ʧ��\n");
		return false;
	}
	return true;
}




bool CIOCPModel::InitIOCP()
{
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL==m_hIOCP)
	{
		printf("������ɶ˿�ʧ�ܣ�������룺%d\n", WSAGetLastError());
		return false;
	}
	return true;
}

bool CIOCPModel::InitSocket()
{
	// AcceptEx �� GetAcceptExSockaddrs ��GUID�����ڵ�������ָ��
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

	// �������ڼ�����Socket����Ϣ
	m_pListenContext = new PER_SOCKET_CONTEXT;

	//ע�� ��Ҫ��wsasocket����
	m_pListenContext->m_socket = WSASocket(AF_INET, SOCK_STREAM,0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_pListenContext->m_socket)
	{
		printf("��ʼ��socketʧ�ܣ������룺%d\n", WSAGetLastError());
		return false;
	}
	//�󶨵���������ַ
	if (SOCKET_ERROR==bind(m_pListenContext->m_socket, (sockaddr *)&m_serverAddr, sizeof(m_serverAddr)))
	{
		printf("bind()����ִ�д���\n");
		RELEASE_SOCKET(m_pListenContext->m_socket);
		return false;
	}
	//������ɶ˿�
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenContext->m_socket, m_hIOCP, (DWORD)m_pListenContext,0))
	{
		printf("��listen socket����ɶ˿�ʧ�ܣ�������룺%d\n", WSAGetLastError());
		RELEASE_SOCKET(m_pListenContext->m_socket);
		return false;
	}
	//��ʼ����
	if (SOCKET_ERROR == listen(m_pListenContext->m_socket, 10))
	{
		printf("listen()����ִ��ʧ�ܣ����ü���ʧ�ܣ�������룺%d\n", WSAGetLastError());
		RELEASE_SOCKET(m_pListenContext->m_socket);
		return false;
	}


	// ʹ��AcceptEx��������Ϊ���������WinSock2�淶֮���΢�������ṩ����չ����
	// ������Ҫ�����ȡһ�º�����ָ�룬
	// ��ȡAcceptEx����ָ��
	DWORD dwBytes = 0;
	if (SOCKET_ERROR == WSAIoctl(
		m_pListenContext->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx),
		&dwBytes,
		NULL,
		NULL))
	{
		DeInit();
		return false;
	}

	// ��ȡGetAcceptExSockAddrs����ָ�룬Ҳ��ͬ��
	if (SOCKET_ERROR == WSAIoctl(
		m_pListenContext->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs),
		&m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs),
		&dwBytes,
		NULL,
		NULL))
	{
		DeInit();
		return false;
	}

	//Ϊacceptex׼��������Ȼ��Ͷ��io����
	for (int i = 0; i < MAX_POST_ACCEPT; i++)
	{
		//�½�һ��io_context
		PPER_IO_CONTEXT p = m_pListenContext->GetNewIOContext();
		if (false == PostAccept(p))
		{
			m_pListenContext->RemoveContext(p);
			return false;
		}
	}

	return true;
}

bool CIOCPModel::InitWorkerThread()
{
	//��ô���������
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	int numOfProcessors = si.dwNumberOfProcessors;
	m_numThreads = THREAD_PER_PROCESSOR*numOfProcessors;
	//��ʼ���߳�
	m_phWorkerThreads = new HANDLE[m_numThreads];
	DWORD nWorkerID;
	for (int i = 0; i < m_numThreads; i++)
	{
		PTHREADPARAM_WORKER param = new THREADPARAM_WORKER;
		param->m_IOCPModel = this;
		param->m_noThread = i + 1;
		m_phWorkerThreads[i] = CreateThread(0, 0, WorkerThreadFun, (LPVOID)param, 0, &nWorkerID);
	}
	Sleep(10);
	printf("�����������߳� %d��\n", m_numThreads);


	return true;
}

void CIOCPModel::DeInit()
{
	//ɾ���̻߳�����
	DeleteCriticalSection(&m_csContextList);
	//�ͷ�iocp�˿ھ��
	RELEASE_HANDLE(m_hIOCP);
	//�ر��¼�
	RELEASE_HANDLE(m_hQuitEvent);
	//�ر��ͷ��߳�
	for (int i = 0; i < m_numThreads; i++)
	{
		RELEASE_HANDLE(m_phWorkerThreads[i]);
	}
	//ɾ��
	delete[] m_phWorkerThreads;

	printf("�ͷ���Դ��ϣ�\n");
}

//ע�� �������н���ͨ�õ���Դ ���Ա�����һ��ͬ��������
void CIOCPModel::AddToSocketContextList(PPER_SOCKET_CONTEXT p)
{
	EnterCriticalSection(&m_csContextList);
	m_clientSocketContextArray.push_back(p);
	LeaveCriticalSection(&m_csContextList);
}

void CIOCPModel::RemoveSocketContext(PPER_SOCKET_CONTEXT p)
{
	EnterCriticalSection(&m_csContextList);
	for (auto i = m_clientSocketContextArray.begin(); i != m_clientSocketContextArray.end();)
	{
		if (p == (*i))
		{
			RELEASE((*i));
			i = m_clientSocketContextArray.erase(i);
			break;
		}
		i++;
	}
	LeaveCriticalSection(&m_csContextList);
}

void CIOCPModel::ClearSocketContext()
{
	for (int i = 0; i < m_clientSocketContextArray.size(); i++)
	{
		delete m_clientSocketContextArray[i];
	}
	m_clientSocketContextArray.clear();
}

bool CIOCPModel::SolveHandleError(PPER_SOCKET_CONTEXT pSockeContext, const DWORD & dwErr)
{
	//��ʱ
	if (WAIT_TIMEOUT == dwErr)
	{
		//ȷ�Ͽͻ����ǲ����쳣�˳���
		if (!IsSocketAlive(pSockeContext->m_socket))
		{
			printf("�ͻ����쳣�˳�!\n");
			RemoveSocketContext(pSockeContext);
			return true;
		}
		else
		{
			printf("���糬ʱ��������......\n");
			return true;
		}
	}
	//�ͻ����쳣�˳�
	else if (ERROR_NETNAME_DELETED == dwErr)
	{
		printf("�ͻ����쳣�˳�!\n");
		RemoveSocketContext(pSockeContext);
		return true;
	}
	printf("��ɶ˿ڷ��������߳��˳��������룺%d\n",dwErr);
	return false;
}

bool CIOCPModel::StartServer()
{
	//��ʼ���̻߳�����
	InitializeCriticalSection(&m_csContextList);
	//�����߳��˳��¼� Ĭ�����ź�  ��Ĭ�������ź�״̬
	m_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == m_hQuitEvent)
	{
		printf("�����߳��˳��¼�ʧ�ܣ�\n");
		return false;
	}
	//���÷�������ַ��Ϣ
	m_serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	m_serverAddr.sin_family = AF_INET;
	m_serverAddr.sin_port = htons(m_nPort);

	if (false == InitIOCP())
	{
		printf("��ʼ��IOCPʧ�ܣ�\n");
		return false;
	}
	else printf("ICOP��ʼ����ɣ�\n");
	if (false == InitWorkerThread())
	{
		printf("��ʼ���߳�ʧ�ܣ�\n");
		return false;
	}
	else printf("�̳߳�ʼ����ɣ�\n");
	if (false == InitSocket())
	{
		printf("socket��ʼ��ʧ�ܣ�\n");
		return false;
	}
	else printf("socket��ʼ����ɣ�\n");
	return true;
}

void CIOCPModel::StopServer()
{
	if (nullptr != m_pListenContext&&INVALID_SOCKET != m_pListenContext->m_socket)
	{
		//�����߳��˳��¼�
		SetEvent(m_hQuitEvent);
		//֪ͨ���е���ɶ˿ڲ����˳�
		for (int i = 0; i < m_numThreads; i++)
		{
			PostQueuedCompletionStatus(m_hIOCP,0,(DWORD)EXIT_CODE,NULL);
		}
		//�ȴ����н��̽���
		WaitForMultipleObjects(m_numThreads,m_phWorkerThreads,true,INFINITE);

		//������пͻ�����Ϣ
		ClearSocketContext();

		printf("ֹͣ������\n");
		DeInit();
		UnloadSocketLib();
	}
}

//�ͻ�������쳣�˳��Ļ�������ͻ��˱������߰ε�����֮��ģ���������޷��յ��ͻ��˶Ͽ���֪ͨ��
bool CIOCPModel::IsSocketAlive(SOCKET s)
{
	int nBytesSend = send(s,"",0,0);
	if (-1 == nBytesSend)return false;
	return true;
}

bool CIOCPModel::PostAccept(PPER_IO_CONTEXT p)
{
	assert(INVALID_SOCKET != m_pListenContext->m_socket);
	p->ResetBuf();

	DWORD dwbytes = 0;
	p->m_type = ACCEPT;
	OVERLAPPED *olp = &p->m_overLapped;
	WSABUF *wb = &p->m_wsaBuf;

	//ͬʱΪ�Ժ�������Ŀͻ���׼����socket ������accept��������
	p->m_socket = WSASocket(AF_INET,SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == p->m_socket)
	{
		printf("��������accept��socketʧ��!�����룺%\n", WSAGetLastError());
		return false;
	}


	//Ͷ��
	if (false == m_lpfnAcceptEx(m_pListenContext->m_socket, p->m_socket, wb->buf, wb->len - 2 * (sizeof(sockaddr_in) + 16),
		sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &dwbytes, olp))
	{
		if (WSA_IO_PENDING != WSAGetLastError())
		{
			printf("Ͷ��ʧ�ܣ������룺%\n", WSAGetLastError());
			return false;
		}
	}

	return true;
}

bool CIOCPModel::PostRecv(PPER_IO_CONTEXT p)
{
	//����
	p->ResetBuf();

	//��ʼ������
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	WSABUF *wb = &p->m_wsaBuf;
	OVERLAPPED *ol = &p->m_overLapped;

	int retVal = WSARecv(p->m_socket, wb, 1, &dwBytes, &dwBytes, ol, NULL);
	if (retVal == SOCKET_ERROR&&WSAGetLastError() != WSA_IO_PENDING)
	{
		printf("Ͷ��recvʧ��! �����룺%d\n ", WSAGetLastError());
		return false;
	}
	return true;
}

bool CIOCPModel::PostSend(PPER_IO_CONTEXT p)
{
	return true;
}

bool CIOCPModel::DoAccept(PPER_SOCKET_CONTEXT pSocketContext, PPER_IO_CONTEXT pIoContext)
{
	sockaddr_in *localAddr = nullptr;
	sockaddr_in *remoteAddr = nullptr;
	int remoteLen = sizeof(sockaddr_in);
	int localLen = sizeof(sockaddr_in);
	//��������ȡ�ÿͻ��˺ͱ��ض˵ĵ�ַ��Ϣ������˳��ȡ���ͻ��˷����ĵ�һ������
	m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		pIoContext->m_wsaBuf.len-2*(sizeof(sockaddr_in)+16),
		sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
		(LPSOCKADDR *)&localAddr,&localLen,(LPSOCKADDR *)&remoteAddr,&remoteLen);
	printf("�ͻ��� %s:%d ����\n", inet_ntoa(remoteAddr->sin_addr), ntohs(remoteAddr->sin_port));
	printf("�ͻ��� %s:%d ��Ϣ:%s\n", inet_ntoa(remoteAddr->sin_addr), ntohs(remoteAddr->sin_port), pIoContext->m_wsaBuf.buf);

	//�����µĿͻ���context
	PPER_SOCKET_CONTEXT pNewSocketContext = new PER_SOCKET_CONTEXT;
	pNewSocketContext->m_socket = pIoContext->m_socket;
	pNewSocketContext->m_clientAddr = *remoteAddr;
	//���µ�socket�󶨵���ɶ˿���
	HANDLE retVal = CreateIoCompletionPort((HANDLE)pNewSocketContext->m_socket, m_hIOCP, (DWORD)pNewSocketContext, 0);
	if (NULL == retVal)
	{
		RELEASE(pNewSocketContext);
		printf("ִ��CreateIoCompletionPort()���ִ���.������룺%d", GetLastError());
		return false;
	}
	//�����¿ͻ����µ�io����
	PPER_IO_CONTEXT pNewIoContext = pNewSocketContext->GetNewIOContext();
	pNewIoContext->m_type = RECV;
	pNewIoContext->m_socket = pNewSocketContext->m_socket;

	//��ʼͶ��
	if (false == PostRecv(pNewIoContext))
	{
		pNewSocketContext->RemoveContext(pNewIoContext);
		return false;
	}

	//Ͷ�ݳɹ� ���µ�socket���뵽socketcontext��ȥ ͳһ����
	m_clientSocketContextArray.push_back(pNewSocketContext);

	//������ԭsocket��Ͷ��accept����
	return PostAccept(pIoContext);
}

bool CIOCPModel::DoSend(PPER_SOCKET_CONTEXT pSocketContext, PPER_IO_CONTEXT pIoContext)
{
	return true;
}

bool CIOCPModel::DoRecv(PPER_SOCKET_CONTEXT pSocketContext, PPER_IO_CONTEXT pIoContext)
{
	sockaddr_in *clientAddr = &pSocketContext->m_clientAddr;
	printf("�յ� %s:%d  ��Ϣ:%s\n",inet_ntoa(clientAddr->sin_addr),
		ntohs(clientAddr->sin_port),pIoContext->m_wsaBuf.buf);
	PostRecv(pIoContext);
	return true;
}

