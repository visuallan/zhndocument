#ifndef ___THREADING_H_INCLUDED___
#define ___THREADING_H_INCLUDED___

#include <windows.h>
#include <vector>
#include <queue>
#include "INCLUDE/sync_simple.h"

using namespace std;

const size_t TASKQUEUE_CAPACITY = 16;

//---------------------------- CLASS -------------------------------------------------------------
class CThread;
class CSimpleThreadPool;

///////////////////////////////////////////////////////////////////////////////////////////////
// The core interface of this threading implementation.

class IRunnable 
{
private:
	friend class CThread;
	friend class CSimpleThreadPool;

protected:
	virtual void run() = 0;		//纯虚函数，由派生类实现
};

///////////////////////////////////////////////////////////////////////////////////////////////
// structure holds Thread Local Storage descriptor
struct TLSDescriptor 
{
	DWORD descriptor;

	TLSDescriptor() 
	{
		descriptor = TlsAlloc();
		if (descriptor ==  TLS_OUT_OF_INDEXES) 
		{
			throw "TlsAlloc() failed to create descriptor";
		}
	}

	~TLSDescriptor() 
	{ 
		TlsFree(descriptor); 
	}
};

// Very basic thread class, implementing basic threading API
class CThread: public IRunnable 
{
private:
	static TLSDescriptor m_TLSDesc;
	volatile bool	m_bIsInterrupted;
	volatile bool	m_bIsRunning;

	int				m_nThreadPriority;		//线程的优先级
	IRunnable		*m_RunObj;
	QMutex			m_qMutex;

	// See ::CreateThread(...) within the start() method. This is
	// the thread's API function to be executed. Method executes
	// run() method of the CThread instance passed as parameter.
	static DWORD WINAPI StartThreadST(LPVOID PARAM) 
	{
		CThread *_this = (CThread *) PARAM;

		if (_this != NULL) 
		{
			_this->m_qMutex.Lock();

			// Set the pointer to the instance of the passed CThread
			// in the current Thread's Local Storage. Also see
			// currentThread() method.
			TlsSetValue(CThread::m_TLSDesc.descriptor, (LPVOID) _this);

			_this->run();
			_this->m_bIsRunning = false;

			_this->m_qMutex.Unlock();
		}

		return 0;
	};

protected:
	// It is not possible to instantiate CThread objects directly. Possible only by
	// specifying a IRunnable object to execute its run() method.
	CThread(int nPriority = THREAD_PRIORITY_NORMAL): m_qMutex() 
	{
		this->m_bIsInterrupted = false;
		this->m_bIsRunning = false;
		this->m_nThreadPriority = nPriority;
		this->m_RunObj = NULL;
	};

	// this implementation of the run() will execute the passed IRunnable
	// object (if not null). Inheriting class is responsible for using this
	// method or overriding it with a different one.
	virtual void run() 
	{
		if (this->m_RunObj != NULL) 
		{
			this->m_RunObj->run();
		}
	};

public:
	CThread(IRunnable *RunTask, int nPriority = THREAD_PRIORITY_NORMAL): m_qMutex() 
	{
		this->m_bIsInterrupted = false;
		this->m_bIsRunning = false;
		this->m_nThreadPriority = nPriority;

		if (this != RunTask) 
		{
			this->m_RunObj = RunTask;
		}
		else 
		{
			throw "Self referencing not allowed.";
		}
	};

	virtual ~CThread() 
	{
		this->interrupt();

		// wait until thread ends
		this->join();
	};

	// Method returns the instance of a CThread responsible
	// for the context of the current thread.
	static CThread& currentThread() 
	{
		CThread *thr = (CThread *) TlsGetValue(CThread::m_TLSDesc.descriptor);

		if (thr == NULL) 
		{
			throw "Call is not within a CThread context.";
		}

		return *thr;
	}

	// Method signals thread to stop execution.
	void interrupt() 
	{ 
		this->m_bIsInterrupted = true; 
	}

	// Check if thread was signaled to stop.
	bool isInterrupted() 
	{ 
		return this->m_bIsInterrupted; 
	}


	// Method will wait for thread's termination.
	void join() 
	{
		this->m_qMutex.Lock();
		this->m_qMutex.Unlock();
	};

	// Method starts the Thread. If thread is already started/running, method
	// will simply return.
	void start() 
	{
		HANDLE	hThread;	//线程句柄
		LPTHREAD_START_ROUTINE pStartRoutine = &CThread::StartThreadST;		//线程函数

		if (this->m_qMutex.TryLock()) 
		{
			if (!this->m_bIsRunning) 
			{
				this->m_bIsRunning = true;
				this->m_bIsInterrupted = false;

				hThread = ::CreateThread(NULL, 0, pStartRoutine, (PVOID) this, 0, NULL);
				if (hThread == NULL) 
				{
					this->m_bIsRunning = false;

					this->m_qMutex.Unlock();

					throw "Failed to call CreateThread(). Thread not started.";
				}

				::SetThreadPriority(hThread, this->m_nThreadPriority);
				::CloseHandle(hThread);
			}

			this->m_qMutex.Unlock();
		}
	};
};

//静态成员对象的初始化
TLSDescriptor CThread::m_TLSDesc;

///////////////////////////////////////////////////////////////////////////////////////////////
// Helper class to submit tasks to the CSimpleThreadPool
// 有优先级的任务对象
class CPriorityTask 
{
private:
	int			m_nPriority;		//优先级
	IRunnable	*m_pRunObj;			//任务处理函数

public:
	CPriorityTask(const CPriorityTask &t) 
	{
		m_pRunObj = t.m_pRunObj;
		m_nPriority = t.m_nPriority;
	}

	CPriorityTask() 
	{
		m_pRunObj = NULL;
		m_nPriority = 0;
	}

	CPriorityTask(IRunnable *pRunObj, int nPriority = 0) 
	{
		m_pRunObj = pRunObj;
		m_nPriority = nPriority;
	}

	int getPriority() const 
	{ 
		return m_nPriority; 
	}

	IRunnable *getTask() const 
	{ 
		return m_pRunObj; 
	}

	~CPriorityTask() 
	{

	}

	CPriorityTask& operator=(const CPriorityTask& t) 
	{
		//处理自我赋值
		if(this == &t)
		{
			return *this;
		}

		m_nPriority = t.m_nPriority;
		m_pRunObj = t.m_pRunObj;

		return *this;
	}
};

//Overload the < operator.
bool operator< (const CPriorityTask& pt1, const CPriorityTask& pt2) 
{
	return pt1.getPriority() < pt2.getPriority();	
}

//Overload the > operator.
bool operator> (const CPriorityTask& pt1, const CPriorityTask& pt2) 
{
	//printf("compare > by ref\n");
	return pt1.getPriority() > pt2.getPriority();	
}

///////////////////////////////////////////////////////////////////////////////////////////////
// A priority queue that allows pre-setting capacity of the container.

template<class T>
class mpriority_queue : public priority_queue<T, vector<T>, less<typename vector<T>::value_type> > {
public:
	void reserve(size_type _N) { c.reserve(_N); }
	size_type capacity() const { return c.capacity(); }
};

///////////////////////////////////////////////////////////////////////////////////////////////
// A class containing a collection of CThreadTask's.
// Every CThreadTask will execute same CSimpleThreadPool::run() method.

class CSimpleThreadPool: public IRunnable 
{
private:
	QMutex							m_qMutex;

	vector<CThread*>				m_arrThreadTasks;	//线程池
	mpriority_queue<CPriorityTask>	m_PQueue;			//任务队列

	// Method will return a task from the queue,
	// if there are no tasks in the queue, method will return NULL.
	IRunnable *get() 
	{
		IRunnable *ret = NULL;

		m_qMutex.Lock();	//注意：对队列的操作需要加锁，即互斥的访问

		if (!m_PQueue.empty()) 
		{
			CPriorityTask t = m_PQueue.top();
			m_PQueue.pop();

			ret = t.getTask();
		}

		m_qMutex.Unlock();

		return ret;
	}

public:
	// How many threads are in the collection.
	int getThreadsCount() const 
	{
		m_arrThreadTasks.size(); 
	};

	// Method starts pool's threads.
	void startAll() 
	{
		for (int i = 0; i < m_arrThreadTasks.size(); i++) 
		{
			m_arrThreadTasks[i]->start();
		}
	};

	// Constructor creates the thread pool and sets capacity for the task queue.
	CSimpleThreadPool(unsigned int nThreadsCount, unsigned int nQueueCapacity = TASKQUEUE_CAPACITY): m_qMutex(), m_PQueue() 
	{
		int i;
		CThread *thTask = NULL;

		if (nThreadsCount <= 0)
		{
			throw "Invalid number of threads supplied.";
		}

		if (nQueueCapacity <= 0) 
		{
			throw "Invalid capacity supplied.";
		}

		// Set initial capacity of the tasks Queue.
		m_PQueue.reserve(nQueueCapacity);

		// Initialize thread pool.
		for (i = 0; i < nThreadsCount; i++) 
		{
			thTask = new CThread(this);
			if (thTask != NULL) 
			{ 
				m_arrThreadTasks.push_back(thTask);
			}
		}
	};

	// Submit a new task to the pool
	void submit(IRunnable *pRunObj, int nPriority = 0) 
	{
		if (this == pRunObj)
		{
			throw "Self referencing not allowed.";
		}

		m_qMutex.Lock();

		m_PQueue.push(CPriorityTask(pRunObj, nPriority));

		m_qMutex.Unlock();
	}

	// Method will execute task's run() method within its CThread context.
	virtual void run() 
	{
		IRunnable *task;

		while (!CThread::currentThread().isInterrupted()) {
			// Get a task from the queue.
			task = get();

			// Execute the task.
			if (task != NULL) 
			{	
				task->run();
			}

			::Sleep(2);
		}
	}

	virtual ~CSimpleThreadPool() 
	{
		vector<CThread*>::iterator itPos = m_arrThreadTasks.begin();

		for (; itPos < m_arrThreadTasks.end(); itPos++) 
		{
			delete *itPos;
			*itPos = NULL;
		}

		m_arrThreadTasks.clear();

		while (!m_PQueue.empty()) 
		{ 
			m_PQueue.pop(); 
		}
	};

	// Method signals threads to stop and waits for termination
	void shutdown() 
	{
		for (int i = 0; i < m_arrThreadTasks.size(); i++) 
		{
			m_arrThreadTasks[i]->interrupt();
			m_arrThreadTasks[i]->join();
		}
	}
};

#endif