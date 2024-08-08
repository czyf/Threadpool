#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>

// Any类型，可以指向任意类型
class Any{
public:

    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    template<typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

    // 将Any对象中存储的data数据提取出来
    template<typename T>
    T cast_(){
        // 将基类类型指针转为派生类指针，从他里面提出data数据
        Derive<T>* pt = dynamic_cast<Derive<T>*>(base_.get());
        if (pt == nullptr){
            throw "type is nomatch!";
        }
        return pt->data_;
    }
private:    

    // 基类类型  嵌套类
    class Base{
    public:
        virtual ~Base() = default;
    };

    // 派生类类型
    template<typename T>
    class Derive : public Base{
    public:
        Derive(T data) : data_(data) {}

        T data_;
    };

private:
    // 定义一个基类的指针
    std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore{
public:
    Semaphore(int resLimit = 0) : resLimit_(resLimit){}

    ~Semaphore() = default;

    // 获取一个信号量资源
    void wait(){

        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock,[&]()->bool { return resLimit_ > 0; });
        --resLimit_;
    }

    // 增加一个信号量资源
    void post()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        ++resLimit_;
        cond_.notify_all(); 
    }

private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

// Task类型的前置声明
class Task;

// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    // 
    void setVal(Any any);

    // 
    Any get();

private:
    Any any_;   // 存储任务的返回值
    Semaphore sem_;   // 线程通信信号量
    std::shared_ptr<Task> task_;   // 指向对应获取返回值得任务对象
    std::atomic_bool isValid_;   // 返回值是否有效  任务提交失败一定无效 
};

// 任务抽象基类
class Task
{
public:

    Task();
    ~Task() = default;
    // 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
    virtual Any run() = 0;

    void exec();
    void setResult(Result* result);
private:
    Result* re_;     // 不使用智能指针是因为Task和Result都使用智能指针会造成交叉引用，无法执行析构函数而造成内存泄漏
                     // Result对象生命周期长于Task
};

// 线程池支持的模式
enum class PoolMode
{
    MODE_FIXED, // 固定数量的线程
    MODE_CACHE, // 线程数量可以动态增长
};

// 线程类型
class Thread
{
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(int)>;
    // 线程构造函数
    Thread(ThreadFunc func);
    // 线程析构函数
    ~Thread();

    // 启动线程
    void start();

    // 获取线程id
    int getId() const;

private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_;   //  保存线程id
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task{
public:
    void run() override{ 线程代码... }
};

pool.submitTaskk(std::make_shared<MyTask>());
*/

// 线程池类型
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    // 设置线程池的工作模式
    void setMode(PoolMode mode);

    // 设置任务队列最大阈值
    void setTaskQueMaxThreshHold(unsigned int threshHold);

    // 设置线程数量阈值
    void setThreadQueMaxThreshHold(int threshHold);

    // 给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    // 开始运行线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());

    ThreadPool(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

private:
    // 定义线程函数
    void threadFunc(int threadId);
    // 检查线程池运行状态
    bool checkRunningState() const;

private:
    // std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  // 线程列表
    size_t initThreadSize_;                        // 初值的线程数
    int threadSizeThreshHold_;          // 线程数量上限阈值
    std::atomic_int curThreadSize_;      // 当前线程池里面线程的数量
    std::atomic_int idleTreadSize_;   // 空闲线程的数量

    std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列, 不使用裸指针或引用是由于临时任务提前析构释放使得裸指针指向的任务对象已经无效
    std::atomic_uint taskSize_;                 // 任务数量
    unsigned int taskQueMaxThreshHold_;         // 任务队列数量上限阈值

    std::mutex taskQueMtx_;            // 保证任务队列线程安全
    std::condition_variable notFull_;  // 表示任务队列不满，可生产
    std::condition_variable notEmpty_; // 表示任务队列不空，可消费
    std::condition_variable exitCond_;  // 等待线程资源全部回收

    PoolMode poolMode_; // 当前线程池的工作模式

    std::atomic_bool isPoolRunning_;  // 表示线程池是否启动

    
};

#endif
