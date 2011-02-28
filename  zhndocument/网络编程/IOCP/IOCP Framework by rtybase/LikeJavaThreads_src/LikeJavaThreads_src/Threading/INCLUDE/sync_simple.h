#ifndef ___SYNC_SIMPLE_H_INCLUDED___
#define ___SYNC_SIMPLE_H_INCLUDED___

//---------------------------- CLASS -------------------------------------------------------------

// Implementation of the critical section
// 同步对象，用于实现对资源的互斥访问
class QMutex 
{
private:
	CRITICAL_SECTION	m_Cs;

public:
	QMutex() 
	{ 
		::InitializeCriticalSection(&this->m_Cs); 
	}

	~QMutex() 
	{ 
		::DeleteCriticalSection(&this->m_Cs); 
	}
	void Lock() 
	{ 
		::EnterCriticalSection(&this->m_Cs); 
	}

	//测试当前线程是否可以获取临界区对象
	BOOL TryLock() 
	{ 
		return (BOOL) ::TryEnterCriticalSection(&this->m_Cs); 
	}

	void Unlock() 
	{ 
		::LeaveCriticalSection(&this->m_Cs); 
	}
};

// Implementation of the semaphore
class QSemaphore 
{
private:
	HANDLE	m_hSemaphore;
	long m_lMaximumCount;

public:
	QSemaphore(long lMaximumCount) 
	{
		this->m_hSemaphore = ::CreateSemaphore(NULL, lMaximumCount, lMaximumCount, NULL);

		if (this->m_hSemaphore == NULL) 
		{
			throw "Call to CreateSemaphore() failed. Could not create semaphore.";
		}

		this->m_lMaximumCount = lMaximumCount;
	}

	~QSemaphore() 
	{ 
		::CloseHandle(this->m_hSemaphore); 
	}

	long GetMaximumCount() const 
	{ 
		return this->m_lMaximumCount; 
	}

	void Inc() 
	{ 
		::WaitForSingleObject(this->m_hSemaphore, INFINITE); 
	}

	void Dec() 
	{ 
		::ReleaseSemaphore(this->m_hSemaphore, 1, NULL); 
	}

	void Dec(long lCount) 
	{ 
		::ReleaseSemaphore(this->m_hSemaphore, lCount, NULL); 
	}
};


// Implementation of the read-write mutex.
// Multiple threads can have read access at the same time.
// Write access is exclusive for only one thread.

// 读写锁
// 多个线程可以同时读取共享数据，但是对数据的写操作是独占的
class ReadWriteMutex 
{
private:
	QMutex		m_qMutex;
	QSemaphore	m_qSemaphore;

public:
	ReadWriteMutex(long lMaximumReaders): m_qSemaphore(lMaximumReaders) 
	{

	}

	void lockRead() 
	{ 
		m_qSemaphore.Inc(); 
	}

	void unlockRead() 
	{ 
		m_qSemaphore.Dec(); 
	}

	void lockWrite() 
	{
		m_qMutex.Lock();

		for (int i = 0; i < maxReaders(); ++i)
		{
			m_qSemaphore.Inc();
		}

		m_qMutex.Unlock();
	}
	
	void unlockWrite() 
	{  
		m_qSemaphore.Dec(m_qSemaphore.GetMaximumCount()); 
	}

	int maxReaders() const 
	{ 
		return m_qSemaphore.GetMaximumCount(); 
	}
};

//---------------------------- IMPLEMENTATION ----------------------------------------------------

#endif