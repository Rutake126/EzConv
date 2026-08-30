#include "thread_pool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t threads) {
    if (threads == 0) threads = 1;
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->cv_task.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });
                    if (this->stop && this->tasks.empty()) {
                        return;
                    }
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                
                try {
                    if (task) {
                        task();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[ThreadPool Exception] " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[ThreadPool Unknown Exception]" << std::endl;
                }
                
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    if (active_tasks > 0) {
                        active_tasks--;
                    }
                    if (tasks.empty() && active_tasks == 0) {
                        cv_finished.notify_all();
                    }
                }
            }
        });
    }
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    cv_finished.wait(lock, [this]() {
        return tasks.empty() && (active_tasks == 0);
    });
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    cv_task.notify_all();
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
