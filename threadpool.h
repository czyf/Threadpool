#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <iostream>
#include <thread>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <future>

const int THREAD_MAX_THRESHHOLD = 4;  // 最大线程数量上限阈值
const int TASK_MAX_THREADSHHOLD = 10; // 最大任务数量上限阈值
const int THREAD_WAIT_MAX_TIME = 10;  // 线程释放等待时间阈值

enum class PoolMode
{
    MODE_FIXED,
    MODE_CACHE
};

// 线程类
class Thread
{

    using ThreadFunc = std::function<void(int)>;

public:
    Thread(ThreadFunc func) : func_(func),
                              threadID_(getID_++)
    {
    }

    ~Thread() = default;
    // 开始执行线程函数
    void start()
    {
        std::thread th(func_, threadID_);
        th.detach(); // 将该线程分离
    }

    int getThreadID() const
    {
        return threadID_;
    }

private:
    int threadID_;     // 线程id
    static int getID_; // 生成线程id
    ThreadFunc func_;  // 可调用函数对象
};

int Thread::getID_ = 0;

// 线程池类
class ThreadPool
{
public:
    // 构造函数
    ThreadPool() : isPoolRunning_(false), poolMode_(PoolMode::MODE_FIXED), initThreadSize_(0), curThreadSize_(0), idleThreadSize_(0), threadMaxSizeThreshHold_(THREAD_MAX_THRESHHOLD), taskSize_(0), taskSizeMaxThreshHold_(TASK_MAX_THREADSHHOLD)
    {
    }

    // 析构函数，需要回收线程资源
    ~ThreadPool()
    {
        isPoolRunning_ = false;

        // 等待线程池中所有线程返回，有两种状态：阻塞、 正在运行
        std::unique_lock<std::mutex> lock(taskMtx_);
        notEmpty_.notify_all();
        isExit_.wait(lock, [&]() -> bool
                     { return threads_.size() == 0; });
    }

    // 设置线程的工作模式
    void setPoolMode(PoolMode mode)
    {
        if (checkRunningStatus())
        {
            return;
        }
        poolMode_ = mode;
    }
    // 设置线程数量上限阈值
    // 在cache模式下限制线程最大数量
    void setThreadMaxSizeThreshHold(int threshHold)
    {
        if (checkRunningStatus())
        {
            return;
        }
        if (poolMode_ == PoolMode::MODE_CACHE)
            threadMaxSizeThreshHold_ = threshHold;
    }
    // 设置task任务队列上限阈值
    void setTaskMaxSizeThresgHold(int threshHold)
    {
        if (checkRunningStatus())
        {
            return;
        }
        taskSizeMaxThreshHold_ = threshHold;
    }

    // 提交任务
    template <typename Func, typename... Args>
    auto submitTask(Func &&func, Args &&...args) -> std::future<decltype(func(args...))>
    {
        using Rtype = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<Rtype()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        std::future<Rtype> result = task->get_future();

        std::unique_lock<std::mutex> lock(taskMtx_);

        if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                               { return taskSize_ < taskSizeMaxThreshHold_; }))
        {
            std::cerr << "task queue is full..." << std::endl;
            auto task = std::make_shared<std::packaged_task<Rtype()>>([]() -> Rtype
                                                                      { return Rtype(); });
            (*task)();
            return task->get_future();
        }

        taskQue_.emplace([task]()
                         { (*task)(); }); // !!!
        ++taskSize_;

        notEmpty_.notify_all();

        // cache模式下如何当前任务数大于线程数，判断是否增加线程
        if (poolMode_ == PoolMode::MODE_CACHE &&
            taskQue_.size() > idleThreadSize_ &&
            curThreadSize_ < threadMaxSizeThreshHold_)
        {

            std::cout << "tid: " << std::this_thread::get_id() << " new thread create..." << std::endl;

            std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            int threadId = ptr->getThreadID();
            threads_.emplace(threadId, std::move(ptr));

            // 启动线程
            threads_[threadId]->start();

            ++curThreadSize_;
            ++idleThreadSize_;
        }

        return result;
    }

    // 开始启动线程池
    void start(int initThreadSize = 4)
    {
        isPoolRunning_ = true;

        initThreadSize_ = initThreadSize;

        for (int i = 0; i < initThreadSize_; ++i)
        {
            std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            int threadId = ptr->getThreadID();
            threads_.emplace(threadId, std::move(ptr));
        }

        // 启动线程
        for (int i = 0; i < initThreadSize_; ++i)
        {
            threads_[i]->start();
        }

        curThreadSize_ = initThreadSize;
        idleThreadSize_ = initThreadSize;
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    // 定义线程函数
    void threadFunc(int threadID)
    {

        // 获取当前时间，在cache模式下若闲置线程总数超过初始线程总数超过60s将回收线程资源。
        auto lastTime = std::chrono::high_resolution_clock::now();
        for (;;)
        {
            Task task;
            {

                // 尝试获取锁
                std::unique_lock<std::mutex> lock(taskMtx_);
                std::cout << "tid: " << std::this_thread::get_id() << " 尝试获取任务。。。" << std::endl;

                while (taskQue_.size() == 0)
                {

                    // 线程池析构，待所有任务结束回收所有线程资源
                    if (!isPoolRunning_)
                    {
                        threads_.erase(threadID);
                        std::cout << "tid: " << std::this_thread::get_id() << " exit!!!" << std::endl;
                        isExit_.notify_all();
                        return;
                    }

                    // cache下线程回收
                    if (poolMode_ == PoolMode::MODE_CACHE)
                    {
                        if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                        {

                            auto nowTime = std::chrono::high_resolution_clock::now();
                            auto diff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);

                            if (diff.count() > THREAD_WAIT_MAX_TIME && curThreadSize_ > initThreadSize_)
                            {
                                //  超过60s， 回收线程
                                threads_.erase(threadID);
                                --curThreadSize_;
                                --idleThreadSize_;
                                std::cout << "tid: " << std::this_thread::get_id() << " 退出！！！" << std::endl;
                                return;
                            }
                        }
                    }
                    else
                    {
                        // 等待wait条件
                        notEmpty_.wait(lock);
                    }
                }

                --idleThreadSize_;
                std::cout << "tid: " << std::this_thread::get_id() << " 获取任务！！！" << std::endl;

                // 获取任务
                task = taskQue_.front();
                taskQue_.pop();
                --taskSize_;

                // 若还有任务，通知其他线程继续分配任务
                if (taskSize_ > 0)
                {
                    notEmpty_.notify_all();
                }

                // 取出一个任务，通知可以继续提交任务
                notFull_.notify_all();

            } // 出作用域，释放互斥锁

            // 该线程执行该任务
            if (task != nullptr)
            {
                task();
            }

            // 任务执行结束
            ++idleThreadSize_;

            auto lastTime = std::chrono::high_resolution_clock::now();
        }
    }

    bool checkRunningStatus()
    {
        return isPoolRunning_;
    }

private:
    PoolMode poolMode_;              // 线程池模式
    std::atomic_bool isPoolRunning_; // 线程池运行状态

    std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 保存线程的容器
    int initThreadSize_;                                       // 初始线程个数
    std::atomic_int curThreadSize_;                            // 当前线程个数
    std::atomic_int idleThreadSize_;                           // 线程闲置个数
    int threadMaxSizeThreshHold_;                              // 最大线程上限阈值

    using Task = std::function<void()>; // 任务类型
    std::queue<Task> taskQue_;          // 任务队列
    std::atomic_int taskSize_;          // 任务个数
    int taskSizeMaxThreshHold_;         // 最大任务个数阈值

    std::mutex taskMtx_;               // 任务队列互斥锁
    std::condition_variable notFull_;  // 任务队列未满，可继续添加任务
    std::condition_variable notEmpty_; // 任务队列非空，可继续分配任务执行

    std::condition_variable isExit_; // 等待线程资源回收
};

#endif