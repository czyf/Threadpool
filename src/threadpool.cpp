#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THREAD_HOLD = INT16_MAX;
const int THREAD_MAX_THREAD_HOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10; // 单位为秒

// 线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(4), taskSize_(0), taskQueMaxThreshHold_(TASK_MAX_THREAD_HOLD),
      isPoolRunning_(false), idleTreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THREAD_HOLD),
      poolMode_(PoolMode::MODE_FIXED), curThreadSize_(0)
{
}

// 线程池析构
ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;
    // 等待线程池里面所有的线程返回， 有两种状态： 阻塞 & 正在执行中
    

    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();

    exitCond_.wait(lock, [&]() -> bool
                   { return threads_.size() == 0; });
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
        return;
    poolMode_ = mode;
}

// 设置线程数量阈值阈值
void ThreadPool::setThreadQueMaxThreshHold(int threshHold)
{
    if (checkRunningState())
        return;
    if (poolMode_ == PoolMode::MODE_CACHE)
    {
        threadSizeThreshHold_ = threshHold;
    }
}

// 设置task任务队列上线阈值
void ThreadPool::setTaskQueMaxThreshHold(unsigned int threshHold)
{
    if (checkRunningState())
        return;
    taskQueMaxThreshHold_ = threshHold;
}

// 给线程池提交任务  用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> pt)
{

    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // 线程的通信，等待任务队列有空余
    // 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
    // while (taskQue_.size() == taskQueMaxThreshHold_){
    //     notFull_.wait(lock);
    // }
    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
                           [&]() -> bool
                           { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
    {
        // 表示notfull_等待1s，条件依然没有满足
        std::cerr << "task queue is full, submit task fail." << std::endl;

        // return task->getResult();  // 这种方式不行，result依赖于task，线程执行完task，task对象就被析构掉了
        return Result(pt, false);
    }

    // 如果任务队列有空余，将任务放入任务队列中
    taskQue_.emplace(pt);
    ++taskSize_;

    // 因为放了任务，任务队列肯定不空，在notEmpty_上进行通知，赶快分配线程执行任务
    notEmpty_.notify_all();

    // cached模式，适合任务比较紧急，场景：小而快的任务
    // 需要根据任务数量和线程空闲的数量，判断是否需要创建新的线程
    if (poolMode_ == PoolMode::MODE_CACHE &&
        taskSize_ > idleTreadSize_ &&
        curThreadSize_ < threadSizeThreshHold_)
    {
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[threadId]->start(); // 创建新的线程后启动线程
        // 修改线程个数的相关变量
        ++idleTreadSize_;
        ++curThreadSize_;
    }

    // 返回任务对象的Result
    // return task->getResult();  // 这种方式不行，result依赖于task，线程执行完task，task对象就被析构掉了
    return Result(pt);
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
    // 设置线程池得运行状态
    isPoolRunning_ = true;

    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; ++i)
    {
        // 创建thread线程对象的时候，把线程函数给到Thread线程对象
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        // threads_.emplace_back(std::move(ptr));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动线程
    for (int i = 0; i < initThreadSize_; ++i)
    {
        threads_[i]->start();
        ++idleTreadSize_; // 记录初始空闲线程的数量
    }
}

// 定义线程函数   线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc(int threadId)
{

    //     std::cout << "begin threadFunc tid: " << std::this_thread::get_id() << std::endl;
    //     std::cout << "end threadFunc tid: " << std::this_thread::get_id() << std::endl;
    auto lastTime = std::chrono::high_resolution_clock().now();
    // 所有任务必须执行完成，线程池才可以回收所有线程
    for (;;)
    {
        std::shared_ptr<Task> task;
        {
            // 先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务。。。" << std::endl;

            // cached模式下，有可能已经创建了很多线程，但是空闲时间超过60s， 应该把多余线程结束回收掉
            // （超过initThreadSize_数量的线程要进行回收）
            // 当前时间 - 上一次线程执行时间 》 60s
            
            // 每一秒钟返回一次，  怎么区分：超时返回？还是有任务待执行返回
            //  锁 + 双重判断
            while (taskQue_.size() == 0)
            {
                if (!isPoolRunning_)
                {
                    threads_.erase(threadId);
                    std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;
                    exitCond_.notify_all();
                    return;    // 线程函数结束，线程结束
                }

                if (poolMode_ == PoolMode::MODE_CACHE)
                {

                    // 条件变量超时返回
                    if (std::cv_status::timeout ==
                        notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto nowTime = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);

                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收线程
                            // 记录线程数量的相关变量的值修改
                            // 把线程对象从线程列表容器中删除
                            threads_.erase(threadId);
                            --curThreadSize_;
                            --idleTreadSize_;

                            std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;

                            return;
                        }
                    }
                }
                else
                {
                    // 等待notEmpty条件
                    notEmpty_.wait(lock);
                }
                // 线程池要结束，回收线程资源
            //     if (!isPoolRunning_){
            //         threads_.erase(threadId);
            //         std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;
            //         exitCond_.notify_all();
            //         return ;
            //     }
            }

            --idleTreadSize_;

            std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功" << std::endl;
            // 从任务队列中获取一个任务
            task = taskQue_.front();
            taskQue_.pop();
            --taskSize_;

            // 如果还有剩余任务，继续通知其他线程执行任务
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }

            // 取出一个任务，进行通知
            notFull_.notify_all();

        } // 局部作用域结束后会释放锁

        // 当前线程负责执行该任务
        if (task != nullptr)
        {
            // task->run();  // 执行任务，把任务得返回值setVal方法传给Result
            task->exec();
        }

        ++idleTreadSize_;

        lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务时间
    }

    
}

// void ThreadPool::threadFunc(int threadId)
// {

//     //     std::cout << "begin threadFunc tid: " << std::this_thread::get_id() << std::endl;
//     //     std::cout << "end threadFunc tid: " << std::this_thread::get_id() << std::endl;
//     auto lastTime = std::chrono::high_resolution_clock().now();
//     // 所有任务必须执行完成，线程池才可以回收所有线程
//     while (isPoolRunning_)
//     {
//         std::shared_ptr<Task> task;
//         {
//             // 先获取锁
//             std::unique_lock<std::mutex> lock(taskQueMtx_);

//             std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务。。。" << std::endl;

//             // cached模式下，有可能已经创建了很多线程，但是空闲时间超过60s， 应该把多余线程结束回收掉
//             // （超过initThreadSize_数量的线程要进行回收）
//             // 当前时间 - 上一次线程执行时间 》 60s
            
//             // 每一秒钟返回一次，  怎么区分：超时返回？还是有任务待执行返回
//             //  锁 + 双重判断
//             while (isPoolRunning_ && taskQue_.size() == 0)
//             {
//                 if (poolMode_ == PoolMode::MODE_CACHE)
//                 {

//                     // 条件变量超时返回
//                     if (std::cv_status::timeout ==
//                         notEmpty_.wait_for(lock, std::chrono::seconds(1)))
//                     {
//                         auto nowTime = std::chrono::high_resolution_clock().now();
//                         auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);

//                         if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
//                         {
//                             // 开始回收线程
//                             // 记录线程数量的相关变量的值修改
//                             // 把线程对象从线程列表容器中删除
//                             threads_.erase(threadId);
//                             --curThreadSize_;
//                             --idleTreadSize_;

//                             std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;

//                             return;
//                         }
//                     }
//                 }
//                 else
//                 {
//                     // 等待notEmpty条件
//                     notEmpty_.wait(lock);
//                 }
//                 // 线程池要结束，回收线程资源
//             //     if (!isPoolRunning_){
//             //         threads_.erase(threadId);
//             //         std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;
//             //         exitCond_.notify_all();
//             //         return ;
//             //     }
//             }

//             if (!isPoolRunning_){
//                 break;
//             }

//             --idleTreadSize_;

//             std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功" << std::endl;
//             // 从任务队列中获取一个任务
//             task = taskQue_.front();
//             taskQue_.pop();
//             --taskSize_;

//             // 如果还有剩余任务，继续通知其他线程执行任务
//             if (taskQue_.size() > 0)
//             {
//                 notEmpty_.notify_all();
//             }

//             // 取出一个任务，进行通知
//             notFull_.notify_all();

//         } // 局部作用域结束后会释放锁

//         // 当前线程负责执行该任务
//         if (task != nullptr)
//         {
//             // task->run();  // 执行任务，把任务得返回值setVal方法传给Result
//             task->exec();
//         }

//         ++idleTreadSize_;

//         lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务时间
//     }

//     threads_.erase(threadId);
//     std::cout << "threadid: " << std::this_thread::get_id() << " exit" << std::endl;
//     exitCond_.notify_all();
// }

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}

///////////////////////////////////////////   Thread类实现
// 线程方法实现
int Thread::generateId_ = 0;
// 线程构造函数
Thread::Thread(ThreadFunc func)
    : func_(func),
      threadId_(generateId_++)
{
}

// 线程析构函数
Thread::~Thread()
{
}

// 启动线程
void Thread::start()
{
    // 创建一个线程来执行一个线程函数
    std::thread t(func_, threadId_); // c++11来说 线程对象t和线程函数func_
    t.detach();                      // 设置分离线程，否则线程出作用域会析构
}

int Thread::getId() const
{
    return threadId_;
}

///////////////////////////////// Task
Task::Task() : re_(nullptr) {}

void Task::exec()
{
    if (re_ != nullptr)
        re_->setVal(run()); // 这里发生多态调用
}

void Task::setResult(Result *result)
{
    re_ = result;
}

/////////////////////////////////  Result
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task), isValid_(isValid)
{
    task_->setResult(this);
}
Any Result::get()
{ // 用户调用
    if (!isValid_)
        return "";

    sem_.wait(); // task任务如果没有执行完，将会阻塞用户线程

    return std::move(any_);
}

void Result::setVal(Any any)
{
    this->any_ = std::move(any);
    sem_.post();
}