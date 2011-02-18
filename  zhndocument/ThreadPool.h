#ifndef _ThreadPool_H_
#define _ThreadPool_H_

#pragma warning(disable: 4530)
#pragma warning(disable: 4786)

#include <cassert>
#include <vector>
#include <queue>
#include <windows.h>

using namespace std;

class ThreadJob		//任务基类，用户的任务需派生此类
{
public:
	//供线程池调用的虚函数，有用户定义
	virtual void DoJob(void *pPara) = 0;
};

//线程池
class ThreadPool
{
public:

	//任务项，即是线程池中的线程需要处理的任务
	struct JobItem 
	{
		void (*_pFunc)(void *);		 //任务函数，由用户定义
		void *_pPara;				 //任务函数的参数，有用户定义

		JobItem(void (*pFunc)(void *) = NULL, void *pPara = NULL) : _pFunc(pFunc), _pPara(pPara) 
		{ 

		};
	};

	//线程池中的线程包装结构：线程项，用于保存一个线程的有关信息
	struct ThreadItem
	{
		HANDLE _Handle;				//线程内核对象的句柄
		ThreadPool *_pThreadPool;	//线程所属线程池的指针
		DWORD _dwLastBeginTime;		//最近一次处理任务的开始时间
		DWORD _dwCount;				//线程处理任务项的个数
		bool _fIsRunning;			//线程是否正在处理任务项，不是指示线程是否运行

		ThreadItem(ThreadPool *pThreadPool = NULL) : _pThreadPool(pThreadPool), _Handle(NULL), _dwLastBeginTime(0), 
			_dwCount(0), _fIsRunning(false) 
		{ 

		};

		~ThreadItem()
		{
			if(_Handle)
			{
				CloseHandle(_Handle);	//释放线程内核对象的句柄
				_Handle = NULL;
			}
		}
	};

public:
	std::queue<JobItem *> _JobQueue;			//任务项队列，该队列中的任务项JobItem会按照FIFO被线程池中的线程进行处理
	std::vector<ThreadItem *> _ThreadVector;	//线程池，里面存放一系列的线程项，线程项就是对线程的包装

	CRITICAL_SECTION _csThreadVector;	 //用于对线程项vector操作进行互斥 
	CRITICAL_SECTION _csWorkQueue;		 //用于对任务项队列的操作进行互斥

	HANDLE _EventEnd;			//结束通知
	HANDLE _EventComplete;		//事件内核对象句柄，指示是否完成任务
	HANDLE _SemaphoreCall;		//工作信号，指示任务队列中是否有任务项需要进行处理
	HANDLE _SemaphoreDel;		//删除线程信号

	long _lThreadNum;		//线程池中总的线程数	
	long _lRunningNum;		//线程池中当前正在处理任务项的线程数，注意不是正在运行的线程数

public:
	//dwNum为线程池中线程数量，缺省设置为4
	ThreadPool(DWORD dwThreadNum = 4) : _lThreadNum(0), _lRunningNum(0) 
	{
		InitializeCriticalSection(&_csThreadVector);
		InitializeCriticalSection(&_csWorkQueue);

		//CreateEvent创建匿名的自动重置，初始状态为未触发的事件内核对象
		_EventComplete = CreateEvent(0, false, false, NULL);
		_EventEnd = CreateEvent(0, true, false, NULL);

		//用于线程同步，初始资源计数为0，即当前信号量处于未触发状，调用Wait Function的线程将会被阻塞
		_SemaphoreCall = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL);
		_SemaphoreDel = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL);

		assert(_SemaphoreCall != INVALID_HANDLE_VALUE);
		assert(_EventComplete != INVALID_HANDLE_VALUE);
		assert(_EventEnd != INVALID_HANDLE_VALUE);
		assert(_SemaphoreDel != INVALID_HANDLE_VALUE);

		AdjustSize(dwThreadNum <= 0 ? 4 : dwThreadNum);
	}
	~ThreadPool()
	{
		DeleteCriticalSection(&_csWorkQueue);

		CloseHandle(_EventEnd);
		CloseHandle(_EventComplete);
		CloseHandle(_SemaphoreCall);
		CloseHandle(_SemaphoreDel);

		vector<ThreadItem*>::iterator iter;
		for(iter = _ThreadVector.begin(); iter != _ThreadVector.end(); iter++)
		{
			if(*iter)
				delete *iter;
		}

		DeleteCriticalSection(&_csThreadVector);
	}

	//调整线程池规模，参数iThreadNum为线程池中线程的数目
	int AdjustSize(int iThreadNum)
	{
		if(iThreadNum > 0)
		{
			ThreadItem *pNew;

			//注意，这里需要独占的对线程池进行插入操作，所以需要加锁
			EnterCriticalSection(&_csThreadVector);

			for(int _i=0; _i<iThreadNum; _i++)
			{
				_ThreadVector.push_back(pNew = new ThreadItem(this)); 

				//WIN32 API函数CreateThread创建线程内核对象，并返回其句柄。推荐用CRT函数_beginthreadex
				//DefaultJobProc为线程函数，一旦线程被创建并被CPU调度，则会执行这个函数
				pNew->_Handle = CreateThread(NULL, 0, DefaultJobProc, pNew, 0, NULL);

				// 设置线程的优先级
				SetThreadPriority(pNew->_Handle, THREAD_PRIORITY_BELOW_NORMAL);
			}

			LeaveCriticalSection(&_csThreadVector);
		}
		else
		{
			iThreadNum *= -1;
			ReleaseSemaphore(_SemaphoreDel, iThreadNum > _lThreadNum ? _lThreadNum : iThreadNum, NULL);
		}

		return (int)_lThreadNum;
	}

	//暴露给用户的API
	//调用线程池中的线程对用户指定的任务进行处理
	void Call(void (*pFunc)(void *), void *pPara = NULL)
	{
		assert(pFunc);

		EnterCriticalSection(&_csWorkQueue);

		//把一个用户定义的任务项插入到线程池的任务队列中
		_JobQueue.push(new JobItem(pFunc, pPara));

		LeaveCriticalSection(&_csWorkQueue);

		//指示线程池中的线程有一个任务项需要进行处理了
		//具体是哪个线程进行处理，是有CPU调度机制决定，用户不需要关心
		ReleaseSemaphore(_SemaphoreCall, 1, NULL);	 
	}

	//调用线程池
	inline void Call(ThreadJob * p, void *pPara = NULL)
	{
		Call(CallProc, new CallProcPara(p, pPara));
	}
	//结束线程池, 并同步等待
	bool EndAndWait(DWORD dwWaitTime = INFINITE)
	{
		SetEvent(_EventEnd);
		return WaitForSingleObject(_EventComplete, dwWaitTime) == WAIT_OBJECT_0;
	}
	//结束线程池
	inline void End()
	{
		SetEvent(_EventEnd);
	}
	inline DWORD Size()
	{
		return (DWORD)_lThreadNum;
	}
	inline DWORD GetRunningSize()
	{
		return (DWORD)_lRunningNum;
	}
	bool IsRunning()
	{
		return _lRunningNum > 0;
	}

public:
	//线程处理函数，必须是静态成员函数
	static DWORD WINAPI DefaultJobProc(LPVOID lpParameter = NULL)
	{
		ThreadItem *pThread = static_cast<ThreadItem*>(lpParameter);
		assert(pThread);

		ThreadPool *pThreadPoolObj = pThread->_pThreadPool;	//得到本线程所属线程池的指针
		assert(pThreadPoolObj);

		InterlockedIncrement(&pThreadPoolObj->_lThreadNum);

		HANDLE hWaitHandle[3];
		hWaitHandle[0] = pThreadPoolObj->_SemaphoreCall;
		hWaitHandle[1] = pThreadPoolObj->_SemaphoreDel;
		hWaitHandle[2] = pThreadPoolObj->_EventEnd;

		JobItem * pJob;		//当前线程处理的任务项
		bool fHasJob;		//当前线程池的任务项队列中是否有任务项

		while(true)
		{
			//该函数返回的原因有三个
			//1、_SemaphoreCall变为触发态，即主线程调用了ReleaseSemaphore
			//2、_SemaphoreDel变为触发态
			//3、_EventEnd触发，即主线程调用了SetEvent
			DWORD wr = WaitForMultipleObjects(3, hWaitHandle, false, INFINITE);	

			//响应删除线程信号
			if(wr == WAIT_OBJECT_0 + 1) 
			{
				break;
			}

			//从任务项队列中以FIFO取得一个任务项
			EnterCriticalSection(&pThreadPoolObj->_csWorkQueue);

			fHasJob = !pThreadPoolObj->_JobQueue.empty();
			if(fHasJob)
			{
				pJob = pThreadPoolObj->_JobQueue.front();
				pThreadPoolObj->_JobQueue.pop();			//将任务项从线程池的任务队列中弹出

				assert(pJob);
			}

			LeaveCriticalSection(&pThreadPoolObj->_csWorkQueue);

			//收到结束线程信号，判断是否结束线程
			//只有当线程池的任务项队列为空时才会终止线程
			if(wr == WAIT_OBJECT_0 + 2 && !fHasJob) 
			{
				break;
			}

			if(fHasJob && pJob)		//以下即是真正的任务处理核心
			{
				InterlockedIncrement(&pThreadPoolObj->_lRunningNum);

				pThread->_dwLastBeginTime = GetTickCount();
				pThread->_dwCount++;
				pThread->_fIsRunning = true;

				pJob->_pFunc(pJob->_pPara);		//运行用户任务项

				delete pJob;
				pJob = NULL;

				pThread->_fIsRunning = false;

				InterlockedDecrement(&pThreadPoolObj->_lRunningNum);


				break;	//add for testing
			}
		}

		//线程即将终止，需要清理所持有的资源
		//删除自身结构
		EnterCriticalSection(&pThreadPoolObj->_csThreadVector);

		//从线程池中删除自身，使用STL算法find
		pThreadPoolObj->_ThreadVector.erase(find(pThreadPoolObj->_ThreadVector.begin(), pThreadPoolObj->_ThreadVector.end(), pThread));

		LeaveCriticalSection(&pThreadPoolObj->_csThreadVector);

		delete pThread;
		pThread = NULL;

		InterlockedDecrement(&pThreadPoolObj->_lThreadNum);

		if(!pThreadPoolObj->_lThreadNum)	//线程池中已经没有任何线程项对象了
		{
			//SetEvent(pThreadPoolObj->_EventComplete);	//触发事件内核对象
		}

		return 0;
	}

	//调用用户对象虚函数
	static void CallProc(void *pPara) 
	{
		CallProcPara *cp = static_cast<CallProcPara *>(pPara);
		assert(cp);

		if(cp)
		{
			cp->_pObj->DoJob(cp->_pPara);	//多态调用

			delete cp;
			cp = NULL;
		}
	}

	//用户对象结构
	struct CallProcPara 
	{
		ThreadJob* _pObj;		//用户对象 
		void *_pPara;			//用户参数

		CallProcPara(ThreadJob* p = NULL, void *pPara = NULL) : _pObj(p), _pPara(pPara) 
		{

		};
	};
};

#endif