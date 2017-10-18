#pragma once
#include <winsock2.h>
#include <iostream>
#include <vector>
#include <string>
#include <string.h>
#include <assert.h>
#include <MSWSock.h>
using namespace std;
#pragma comment(lib,"ws2_32.lib")

#define MAX_POST_ACCEPT 10     //��ʼͶ��Accept����ĸ���
#define MAX_BUFFLEN   1024     //��������󳤶�
#define DEFAULT_PORT  9990     //Ĭ�϶˿�
#define THREAD_PER_PROCESSOR 2 //һ����������Ӧ�̵߳�����
#define EXIT_CODE NULL         //�˳���


#define RELEASE_SOCKET(x) {if((x)!=INVALID_SOCKET){closesocket(x);(x)=INVALID_SOCKET;}}
#define RELEASE_HANDLE(x) {if((x)!=INVALID_HANDLE_VALUE&&(x)!=NULL){CloseHandle(x);(x)=NULL;}}
#define RELEASE(x) {if((x)!=nullptr){delete (x);(x)=nullptr;}}




typedef enum _OPERATION_TYPE
{
	ACCEPT,                  //���µĿͻ�������
	RECV,                    //�����ݴӿͻ��˹���
	SEND,                    //�������ݵ��ͻ���
	INITIALIZE               //��ʼ��
}OPERATION_TYPE;

//��IO����
typedef struct _PER_IO_CONTEXT
{
	OVERLAPPED m_overLapped;          //�ص�IO
	OPERATION_TYPE m_type;            //��������
	SOCKET m_socket;                  //socket
	WSABUF m_wsaBuf;                  //�ַ�������
	char m_buffer[MAX_BUFFLEN];       

	_PER_IO_CONTEXT()
	{
		memset(&m_overLapped, 0, sizeof(m_overLapped));
		m_type = INITIALIZE;
		m_socket = INVALID_SOCKET;
		memset(m_buffer, 0, sizeof(m_buffer));
		m_wsaBuf.buf = m_buffer;
		m_wsaBuf.len = MAX_BUFFLEN;
	}

	//����buf
	void ResetBuf()
	{
		memset(m_buffer, 0, sizeof(m_buffer));
	}

}PER_IO_CONTEXT,*PPER_IO_CONTEXT;


//���������
typedef struct _PER_SOCKET_CONTEXT
{
	SOCKET m_socket;                           //socket
	SOCKADDR_IN m_clientAddr;                  //�ͻ��˵�ַ��Ϣ
	vector<PPER_IO_CONTEXT>m_IOContextList;    //װsocket�ϵĵ�io����

	//��ʼ��
	_PER_SOCKET_CONTEXT()
	{
		m_socket = INVALID_SOCKET;  
		memset(&m_clientAddr, 0, sizeof(m_clientAddr));
	}

	~_PER_SOCKET_CONTEXT()
	{
		if (INVALID_SOCKET != m_socket)
		{
			closesocket(m_socket);
			m_socket = INVALID_SOCKET;
		}
		//�ͷ����е�io����
		for (int i = 0; i < m_IOContextList.size(); i++)
		{
			delete m_IOContextList[i];
		}
		m_IOContextList.clear();
	}

	//�õ�һ���µ�io���ݽṹ������
	PPER_IO_CONTEXT GetNewIOContext()
	{
		PPER_IO_CONTEXT p = new PER_IO_CONTEXT;
		m_IOContextList.push_back(p);
		return p;
	}

	//�Ƴ���ĳһ��io���ݽṹ
	void RemoveContext(PPER_IO_CONTEXT p)
	{
		assert(p != nullptr);
		for (auto i = m_IOContextList.begin(); i != m_IOContextList.end();)
		{
			if (*i == p)
			{
				delete (*i);
				(*i) = p = nullptr;
				i = m_IOContextList.erase(i);
				break;
			}
			i++;
		}
	}


}PER_SOCKET_CONTEXT,*PPER_SOCKET_CONTEXT;


class CIOCPModel;
//���幤�����̲߳���
typedef struct _THREADPARARM_WORKER
{
	int m_noThread;                         //�̺߳�
	CIOCPModel *m_IOCPModel;                //ָ�����ָ��

}THREADPARAM_WORKER,*PTHREADPARAM_WORKER;




//iocpModel
class CIOCPModel
{
public:
	CIOCPModel();
	~CIOCPModel();
private:
	int m_nPort;                                                //�������˿�
	SOCKADDR_IN m_serverAddr;                                  //������ip��ַ
	int m_numThreads;                                           //�̸߳���
	HANDLE m_hIOCP;                                             //��ɶ˿ھ��
	HANDLE m_hQuitEvent;                                        //�Ƴ��¼����
	HANDLE * m_phWorkerThreads;                                 //�������߳̾��ָ��
	CRITICAL_SECTION m_csContextList;                           //�߳�ͬ��������
	vector<PPER_SOCKET_CONTEXT>m_clientSocketContextArray;      //���пͻ��˵�SocketContext��Ϣ
	PPER_SOCKET_CONTEXT m_pListenContext;                       //���ڼ���
	LPFN_ACCEPTEX                m_lpfnAcceptEx;                // AcceptEx �� GetAcceptExSockaddrs �ĺ���ָ�룬���ڵ�����������չ����
	LPFN_GETACCEPTEXSOCKADDRS    m_lpfnGetAcceptExSockAddrs;

	static DWORD WINAPI WorkerThreadFun(LPVOID lpParam);        //�̺߳���

	bool LoadSocketLib();                                       //�����׽��ֿ�
	void UnloadSocketLib() { WSACleanup(); }                    //ж���׽��ֿ�
	bool InitIOCP();                                            //��ʼ����ɶ˿�
	bool InitSocket();                                          //��ʼ��socket
	bool InitWorkerThread();                                    //��ʼ���������߳�
	void DeInit();                                              //���ȫ���ͷŵ�


	bool PostAccept(PPER_IO_CONTEXT p);                         //Ͷ��accept io����
	bool PostRecv(PPER_IO_CONTEXT p);                           //Ͷ��recv io����
	bool PostSend(PPER_IO_CONTEXT p);                           //Ͷ��send io����
	                                                            //�ֱ�����������
	bool DoAccept(PPER_SOCKET_CONTEXT pSocketContext,PPER_IO_CONTEXT pIoContext);
	bool DoSend(PPER_SOCKET_CONTEXT pSocketContext, PPER_IO_CONTEXT pIoContext);
	bool DoRecv(PPER_SOCKET_CONTEXT pSocketContext, PPER_IO_CONTEXT pIoContext);
	                                                    
	
	void AddToSocketContextList(PPER_SOCKET_CONTEXT p);         //���뵽socketcontext��ȥ ͳһ����
	void RemoveSocketContext(PPER_SOCKET_CONTEXT p);            //��socketcontext��ɾ��
	void ClearSocketContext();                                  //���������socketcontext������

	bool IsSocketAlive(SOCKET s);                               //ȷ�Ͽͻ����ǲ����쳣�˳���
	bool SolveHandleError(PPER_SOCKET_CONTEXT pSockeContext,const DWORD& dwErr);
	                                                            //������ɶ˿��ϵĴ���
	
public:
	bool StartServer();                                         //����������
	void StopServer();                                          //�رշ�����                      
};

