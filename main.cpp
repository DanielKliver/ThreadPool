#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <cmath>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    
                    task();
                }
            });
        }
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        
        for (std::thread &worker : workers) {
            worker.join();
        }
    }
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            if (stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            
            tasks.emplace([task]() { (*task)(); });
        }
        
        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// Функция для вычисления части суммы ряда Лейбница для числа π
double calculate_pi_part(int start, int end) {
    double sum = 0.0;
    for (int i = start; i < end; ++i) {
        double term = (i % 2 == 0) ? 1.0 : -1.0;
        sum += term / (2 * i + 1);
    }
    return sum;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <num_threads>\n";
        return 1;
    }
    
    const size_t num_threads = std::stoi(argv[1]);
    const int terms = 1000000; // Количество членов ряда
    const int terms_per_thread = terms / num_threads;
    
    ThreadPool pool(num_threads);
    std::vector<std::future<double>> futures;
    
    // Разделяем работу между потоками
    for (size_t i = 0; i < num_threads; ++i) {
        int start = i * terms_per_thread;
        int end = (i == num_threads - 1) ? terms : (i + 1) * terms_per_thread;
        
        futures.emplace_back(
            pool.enqueue(calculate_pi_part, start, end)
        );
    }
    
    // Собираем результаты
    double pi = 0.0;
    for (auto& future : futures) {
        pi += future.get();
    }
    
    pi *= 4; // Формула Лейбница дает π/4
    
    std::cout.precision(15);
    std::cout << "Approximate value of π: " << pi << std::endl;
    std::cout << "Difference from actual π: " << std::abs(pi - M_PI) << std::endl;
    
    return 0;
}
