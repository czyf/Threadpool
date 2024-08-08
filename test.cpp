// 线程池项目将在此处开始并运行结束

#include <iostream>
#include <chrono>
#include <thread>
#include <memory>

#include "threadpool.h"

using ulong = unsigned long long;

class MyTask : public Task
{
public:
    MyTask(int begin, int end) : begin_(begin), end_(end){}
    // 问题1，怎么设计run的返回值，可以表示任意的类型
    // 在java，python中Object是所有其他类型的基类
    // C++17，Any类型
    Any run() override
    {
        std::cout << "tid: " << std::this_thread::get_id() << "开始。。。" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(4));
        ulong sum = 0;
        for (ulong i = begin_; i != end_; ++i){
            sum += i;
        }
        std::cout << "tid: " << std::this_thread::get_id() << "结束。。。" << std::endl;
        return sum;
    }

private:
    ulong begin_;
    ulong end_;
};

int main()
{


    {
        ThreadPool pool;
        // 用户自己设定线程池模式
        // pool.setMode(PoolMode::MODE_CACHE);
        // 开始线程
        pool.start(4);
        // 如何设计这里的Result机制呢？
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        ulong sum1 = res1.get().cast_<ulong>();
        std::cout << sum1 << std::endl;
    }
    std::cout << "main end!!!" << std::endl;

    getchar();

#if 0
    {
        ThreadPool pool;
        // 用户自己设定线程池模式
        pool.setMode(PoolMode::MODE_CACHE);
        // 开始线程
        pool.start(4);
        // 如何设计这里的Result机制呢？
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        pool.submitTask(std::make_shared<MyTask>(1, 1000000));
        pool.submitTask(std::make_shared<MyTask>(1, 1000000));

        // 随着task被执行完，task对象析构了，依赖于task对象的Result对象也没了。
        ulong sum1 = res1.get().cast_<ulong>(); // get返回一个Any类型，怎么转成具体的类型呢
        ulong sum2 = res2.get().cast_<ulong>();
        ulong sum3 = res3.get().cast_<ulong>();

        std::cout << sum1 + sum2 + sum3 << std::endl;
    }


    getchar();
#endif


    return 0;
}