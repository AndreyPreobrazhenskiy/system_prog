#include <pthread.h>
#include <iostream>
#include <queue>
#include <vector>
#include <optional>
#include <stdexcept>
#include <cstring>
#include <string>

using namespace std;

void check_result(int result) {
    if (result != 0) {
        throw runtime_error(strerror(result));
    }
}

template <typename T>
class mt_queue {
private:
    size_t max_size_;
    queue<T> q_; // внутренний контейнер для хранения элементов
    mutable pthread_mutex_t mutex_;
    pthread_cond_t not_empty_;
    pthread_cond_t not_full_;
    bool closed_ = false;

public:
    explicit mt_queue(size_t max_size) : max_size_(max_size) {
        if (max_size_ == 0) throw runtime_error("Размер очереди должен быть > 0");
        check_result(pthread_mutex_init(&mutex_, nullptr));
        check_result(pthread_cond_init(&not_empty_, nullptr));
        check_result(pthread_cond_init(&not_full_, nullptr));
    }

    ~mt_queue() {
        pthread_cond_destroy(&not_empty_);
        pthread_cond_destroy(&not_full_);
        pthread_mutex_destroy(&mutex_);
    }

    // запрет копирования и перемещения
    mt_queue(const mt_queue&) = delete;
    mt_queue(mt_queue&&) = delete;
    mt_queue& operator=(const mt_queue&) = delete;
    mt_queue& operator=(mt_queue&&) = delete;

    void enqueue(const T& v) { // блокирующая запись
        check_result(pthread_mutex_lock(&mutex_));
        while (q_.size() >= max_size_ && !closed_)
            check_result(pthread_cond_wait(&not_full_, &mutex_));
        
        if (closed_) { 
            pthread_mutex_unlock(&mutex_); 
            throw runtime_error("Очередь закрыта"); 
        }

        q_.push(v);
        check_result(pthread_cond_signal(&not_empty_));
        check_result(pthread_mutex_unlock(&mutex_));
    }

    T dequeue() { // блокирующее чтение
        check_result(pthread_mutex_lock(&mutex_));
        while (q_.empty() && !closed_)
            check_result(pthread_cond_wait(&not_empty_, &mutex_));
        
        if (q_.empty() && closed_) {
            pthread_mutex_unlock(&mutex_);
            throw out_of_range("Очередь закрыта и пуста");
        }

        T value = q_.front();
        q_.pop();
        check_result(pthread_cond_signal(&not_full_));
        check_result(pthread_mutex_unlock(&mutex_));
        return value;
    }

    bool full() const {
        check_result(pthread_mutex_lock(&mutex_));
        bool result = (q_.size() >= max_size_);
        check_result(pthread_mutex_unlock(&mutex_));
        return result;
    }

    bool empty() const {
        check_result(pthread_mutex_lock(&mutex_));
        bool result = q_.empty();
        check_result(pthread_mutex_unlock(&mutex_));
        return result;
    }

    optional<T> try_dequeue() { // неблокирующее чтение
        check_result(pthread_mutex_lock(&mutex_));
        if (q_.empty() || closed_) {
            check_result(pthread_mutex_unlock(&mutex_));
            return nullopt;
        }
        T value = q_.front();
        q_.pop();
        check_result(pthread_cond_signal(&not_full_));
        check_result(pthread_mutex_unlock(&mutex_));
        return value;
    }

    bool try_enqueue(const T& v) { // неблокирующая запись
        check_result(pthread_mutex_lock(&mutex_));
        if (q_.size() >= max_size_ || closed_) {
            check_result(pthread_mutex_unlock(&mutex_));
            return false;
        }
        q_.push(v);
        check_result(pthread_cond_signal(&not_empty_));
        check_result(pthread_mutex_unlock(&mutex_));
        return true;
    }

    void close() {
        check_result(pthread_mutex_lock(&mutex_));
        closed_ = true;
        pthread_cond_broadcast(&not_full_);
        pthread_cond_broadcast(&not_empty_);
        check_result(pthread_mutex_unlock(&mutex_));
    }
};

struct ProducerArg {
    mt_queue<int>* q;
    int id;
    int count;
};

struct ConsumerArg {
    mt_queue<int>* q;
    int id;
};

pthread_mutex_t cout_mutex = PTHREAD_MUTEX_INITIALIZER; // Глобальный мьютекс для защиты std::cout. Без него строки от разных потоков перемешаются.

void safe_print(const string& msg) {
    pthread_mutex_lock(&cout_mutex);
    cout << msg << std::endl;
    pthread_mutex_unlock(&cout_mutex);
}

void* producer_thread(void* arg_) {
    auto* arg = static_cast<ProducerArg*>(arg_);
    for (int i = 1; i <= arg->count; ++i) {
        int val = arg->id * 1000 + i;
        
        safe_print("Писатель " + to_string(arg->id) + " -> " + to_string(val));
        arg->q->enqueue(val);
    }
    safe_print("Писатель " + to_string(arg->id) + " завершил работу");
    return nullptr;
}

void* consumer_thread(void* arg_) {
    auto* arg = static_cast<ConsumerArg*>(arg_);
    try {
        while (true) {
            int val = arg->q->dequeue();
            safe_print("Читатель " + to_string(arg->id) + " <- " + to_string(val));
        }
    } catch (const out_of_range&) {
        // Очередь закрыта и пуста -> корректный выход
    }
    safe_print("Читатель " + to_string(arg->id) + " завершил работу");
    return nullptr;
}

int main() {
    try {
        size_t queue_size, prod_count, cons_count;
        int items_per_prod;

        cout << "Размер очереди: "; cin >> queue_size;
        cout << "Количество писателей: "; cin >> prod_count;
        cout << "Количество читателей: "; cin >> cons_count;
        cout << "Элементов на писателя: "; cin >> items_per_prod;

        mt_queue<int> q(queue_size);
        vector<pthread_t> producers(prod_count), consumers(cons_count);
        vector<ProducerArg> p_args(prod_count);
        vector<ConsumerArg> c_args(cons_count);

        cout << "\n[Запуск потоков...]\n\n";

        for (size_t i = 0; i < cons_count; ++i) {
            c_args[i] = {&q, (int)i + 1};
            check_result(pthread_create(&consumers[i], nullptr, consumer_thread, &c_args[i]));
        }

        for (size_t i = 0; i < prod_count; ++i) {
            p_args[i] = {&q, (int)i + 1, items_per_prod};
            check_result(pthread_create(&producers[i], nullptr, producer_thread, &p_args[i]));
        }

        for (auto& t : producers) pthread_join(t, nullptr);

        cout << "\n[Все писатели завершили. Уведомляем читателей...]\n";
        q.close();

        for (auto& t : consumers) pthread_join(t, nullptr);

        cout << "\n[Демонстрация завершена успешно]\n";
        pthread_mutex_destroy(&cout_mutex);
        return 0;
    } catch (const exception& e) {
        cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
}
