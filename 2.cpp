#include <pthread.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <thread>

using namespace std;
using namespace std::filesystem;
using namespace std::chrono;

void check_result(int result) {
    if (result != 0) {
        throw runtime_error(strerror(result));
    }
}

struct Arg {
    const vector<int>* arr;
    vector<long long>* result; // long long для защиты от переполнения суммы
    pthread_barrier_t* barrier;
    size_t thread_index;
    size_t thread_count;
};

vector<int> load_array(const string& path) {
    if (!exists(path)) {
        throw runtime_error("Файл не найден: " + path);
    }

    uintmax_t bytes = file_size(path);
    size_t count = bytes / sizeof(int);

    vector<int> arr(count);

    ifstream in(path, ios::binary);
    if (!in) {
        throw runtime_error("Не удалось открыть файл");
    }

    in.read((char*)arr.data(), count * sizeof(int));

    if (!in) {
        throw runtime_error("Ошибка чтения файла");
    }

    return arr;
}

long long reduce_sum_seq(const vector<int>& arr) { // эталон
    long long sum = 0;
    for (size_t i = 0; i < arr.size(); ++i) {
        sum += arr[i];
    }
    return sum;
}

void* thread_main(void* arg_) {
    Arg* arg = (Arg*)arg_;

    size_t start = arg->thread_index * arg->arr->size() / arg->thread_count;
    size_t end   = (arg->thread_index + 1) * arg->arr->size() / arg->thread_count; // целочисленное деление с округлением вниз

    long long local_sum = 0;
    for (size_t i = start; i < end; ++i) {
        local_sum += (*arg->arr)[i];
    }
    (*arg->result)[arg->thread_index] = local_sum;

    size_t step = arg->thread_count;

    while (true) {
        int barrier_result = pthread_barrier_wait(arg->barrier);
        if (barrier_result != 0 && barrier_result != PTHREAD_BARRIER_SERIAL_THREAD) {
            return nullptr; //  error message
        }

        if (step == 1) {
            break;
        }

        size_t p = step;           
        step = (step + 1) / 2; // целочисленное округление вверх

        if (arg->thread_index + step < p) {
            (*arg->result)[arg->thread_index] += (*arg->result)[arg->thread_index + step];
        }
    }

    return nullptr;
}

long long reduce_sum_parallel(const vector<int>& arr, size_t thread_count) {
    if (thread_count == 0) {
        throw runtime_error("Количество потоков должно быть больше 0");
    }
    if (thread_count > arr.size()) {
        thread_count = arr.size();
    }

    vector<long long> result(thread_count, 0);
    vector<pthread_t> threads(thread_count);
    vector<Arg> args(thread_count);

    pthread_barrier_t barrier;
    check_result(pthread_barrier_init(&barrier, nullptr, thread_count));

    for (size_t i = 0; i < thread_count; ++i) {
        args[i].arr = &arr;
        args[i].result = &result;
        args[i].barrier = &barrier;
        args[i].thread_index = i;
        args[i].thread_count = thread_count;

        check_result(pthread_create(&threads[i], nullptr, thread_main, &args[i]));
    }

    for (size_t i = 0; i < thread_count; ++i) {
        check_result(pthread_join(threads[i], nullptr));
    }

    check_result(pthread_barrier_destroy(&barrier));

    return result[0];
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2 || argc > 3) {
            cout << "Использование: " << argv[0] << " array.bin [threads]\n";
            return 1;
        }

        size_t thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 2;
        
        if (argc == 3) {
            thread_count = stoull(argv[2]);
        }

        vector<int> arr = load_array(argv[1]);

        auto start_seq = high_resolution_clock::now();
        long long sum_seq = reduce_sum_seq(arr);
        auto end_seq = high_resolution_clock::now();

        auto start_par = high_resolution_clock::now();
        long long sum_par = reduce_sum_parallel(arr, thread_count);
        auto end_par = high_resolution_clock::now();

        auto time_seq = duration_cast<microseconds>(end_seq - start_seq);
        auto time_par = duration_cast<microseconds>(end_par - start_par);

        cout << "Размер массива: " << arr.size() << "\n";
        cout << "Количество потоков: " << thread_count << "\n";
        cout << "Последовательная сумма: " << sum_seq << "\n";
        cout << "Параллельная сумма: " << sum_par << "\n";
        cout << "Время последовательной версии: " << time_seq.count() << " мкс\n";
        cout << "Время параллельной версии: " << time_par.count() << " мкс\n";

        if (sum_seq == sum_par) {
            cout << "Результаты совпадают\n";
        } else {
            cout << "Результаты не совпадают\n";
        }

        return 0;
    }
    catch (const exception& e) {
        cout << "Ошибка: " << e.what() << "\n";
        return 1;
    }
}
