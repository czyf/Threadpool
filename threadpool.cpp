#include"threadpool.h"

#include<iostream>
#include<thread>
#include<chrono>

int fun1(int a, int b){
    std::this_thread::sleep_for(std::chrono::seconds(4));
    return a + b;
}

int fun2(int a, int b, int c){
    std::this_thread::sleep_for(std::chrono::seconds(4));
    return a + b + c;
}

void fun3(){
    std::this_thread::sleep_for(std::chrono::seconds(4));
}

int main(){

    {
        ThreadPool pool;
        pool.setPoolMode(PoolMode::MODE_CACHE);
        pool.start(2);

        std::future<int> res1 = pool.submitTask(fun1, 11, 21);
        std::future<int> res2 = pool.submitTask(fun2, 11, 21, 31);
        pool.submitTask(fun3);

        std::future<int> res4 = pool.submitTask([](int a, int b) -> int
                                                {
        std::chrono::seconds(4);
        return a + b; }, 11, 21);

        std::future<int> res5 = pool.submitTask([](int a, int b, int c) -> int
                                                {
        std::chrono::seconds(4);
        return a + b + c; }, 11, 21, 31);

        std::cout << "res1: " << res1.get() << std::endl;
        std::cout << "res2: " << res2.get() << std::endl;
        std::cout << "res4: " << res4.get() << std::endl;

    }
    std::cout << "main exit..."  << std::endl;


}