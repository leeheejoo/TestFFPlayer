#pragma once

#include <queue>
#include <SDL.h>

template <typename T> class ThreadQueue
{
public:
	ThreadQueue()
	{
		mutex_ = SDL_CreateMutex();
	}


	~ThreadQueue()
	{
		SDL_DestroyMutex(mutex_);
	}


	void push(T item) 
	{
		SDL_LockMutex(mutex_);
		queue_.push(item);
		SDL_UnlockMutex(mutex_);
	}

	T pop() 
	{
		SDL_LockMutex(mutex_);

		if (queue_.empty())
		{
			SDL_UnlockMutex(mutex_);
			return NULL;
		}
			

		auto ressult = queue_.front();
		queue_.pop();

		SDL_UnlockMutex(mutex_);

		return ressult;
	}

	bool pop(T &item)
	{
		item = NULL;

		SDL_LockMutex(mutex_);

		if (queue_.empty()) 
		{
			SDL_UnlockMutex(mutex_);
			return false;
		}

		item = queue_.front();
		queue_.pop();

		SDL_UnlockMutex(mutex_);

		return true;
	}

	T front()
	{
		SDL_LockMutex(mutex_);

		if (queue_.empty()) 
		{
			SDL_UnlockMutex(mutex_);
			return NULL;
		}

		T item = queue_.front();

		SDL_UnlockMutex(mutex_);

		return item;
	}

	T back()
	{
		SDL_LockMutex(mutex_);

		if (queue_.empty())
		{
			SDL_UnlockMutex(mutex_);
			return NULL;
		}

		T item = queue_.back();

		SDL_UnlockMutex(mutex_);

		return item;
	}

	bool is_empty() 
	{
		SDL_LockMutex(mutex_);
		bool bEmpty = queue_.empty();
		SDL_UnlockMutex(mutex_);

		return bEmpty;
	}

	int size() 
	{
		SDL_LockMutex(mutex_);
		int nSize = queue_.size();
		SDL_UnlockMutex(mutex_);

		return nSize;
	}

private:
	std::queue<T> queue_;
	SDL_mutex* mutex_;
};
