#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <pthread.h>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <thread>

using namespace std;

struct Matrix {
    size_t n;
    vector<double> data; //  элемент (i, j) лежит по индексу i * n + j
};

struct ThreadArgs {
    const Matrix* a;
    const Matrix* b;
    Matrix* c;
    size_t start_row;
    size_t end_row; // диапазон строк для каждого потока
};

Matrix load_matrix(const string& path) {
    if (!filesystem::exists(path)) {
        throw runtime_error("Файл не найден: " + path);
    }

    size_t bytes = static_cast<size_t>(filesystem::file_size(path));
    if (bytes % sizeof(double) != 0) {
        throw runtime_error("Размер файла не кратен sizeof(double)");
    }

    size_t count = bytes / sizeof(double);
    size_t n = static_cast<size_t>(sqrt(count));

    if (n * n != count) {
        throw runtime_error("Матрица не квадратная");
    }

    Matrix m;
    m.n = n;
    m.data.resize(count);

    ifstream in(path, ios::binary); // открытие файла в бинарном режиме
    if (!in.read(reinterpret_cast<char*>(m.data.data()), bytes)) { // этот адрес памяти интерпретируется как указатель на последовательность байт, не меняя сам адрес и не преобразуя данные
        throw runtime_error("Ошибка чтения файла");
    }

    return m;
}

void print_matrix(const Matrix& m, const string& name) {
    cout << name << " (" << m.n << "x" << m.n << "):\n";
    for (size_t i = 0; i < m.n; i++) {
        for (size_t j = 0; j < m.n; j++) {
            cout << setw(10) << fixed << setprecision(2) << m.data[i * m.n + j] << " ";
        }
        cout << "\n";
    }
    cout << "\n";
}

Matrix multiply_sequential(const Matrix& a, const Matrix& b) { // O(N^3)
    if (a.n != b.n) {
        throw runtime_error("Размеры матриц не совпадают");
    }

    Matrix c;
    c.n = a.n;
    c.data.resize(c.n * c.n, 0.0);

    for (size_t i = 0; i < a.n; i++) {
        for (size_t j = 0; j < a.n; j++) {
            double sum = 0.0;
            for (size_t k = 0; k < a.n; k++) {
                sum += a.data[i * a.n + k] * b.data[k * b.n + j];
            }
            c.data[i * c.n + j] = sum;
        }
    }

    return c;
}

void* thread_func(void* arg) {
    ThreadArgs* args = static_cast<ThreadArgs*>(arg);
    size_t n = args->a->n;

    for (size_t i = args->start_row; i < args->end_row; i++) {
        for (size_t j = 0; j < n; j++) {
            double sum = 0.0;
            for (size_t k = 0; k < n; k++) {
                sum += args->a->data[i * n + k] * args->b->data[k * n + j];
            }
            args->c->data[i * n + j] = sum;
        }
    }

    return nullptr;
}

Matrix multiply_parallel(const Matrix& a, const Matrix& b, int thread_count) {
    if (a.n != b.n) {
        throw runtime_error("Размеры матриц не совпадают");
    }

    Matrix c;
    c.n = a.n;
    c.data.resize(c.n * c.n, 0.0);

    vector<pthread_t> threads(thread_count);
    vector<ThreadArgs> args(thread_count);

    for (int i = 0; i < thread_count; i++) {
        args[i].a = &a;
        args[i].b = &b;
        args[i].c = &c;
        args[i].start_row = i * a.n / thread_count;
        args[i].end_row = (i + 1) * a.n / thread_count;

        pthread_create(&threads[i], nullptr, thread_func, &args[i]); // check
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], nullptr); // check
    }

    return c;
}

bool matrices_equal(const Matrix& a, const Matrix& b) {
    if (a.n != b.n) return false;
    
    const double eps = 1e-9;
    for (size_t i = 0; i < a.data.size(); i++) {
        if (fabs(a.data[i] - b.data[i]) > eps) {
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 5) {
            cout << "Использование: " << argv[0] 
                 << " <small_A.bin> <small_B.bin> <large_A.bin> <large_B.bin>\n";
            return 1;
        }

        int num_threads = static_cast<int>(thread::hardware_concurrency());
        if (num_threads < 1) num_threads = 2;

        cout << "=== Загрузка матриц ===\n";
        cout << "Количество потоков: " << num_threads << "\n\n";

        Matrix small_a = load_matrix(argv[1]);
        Matrix small_b = load_matrix(argv[2]);

        cout << "=== Демонстрация (малые матрицы) ===\n";
        print_matrix(small_a, "Matrix A");
        print_matrix(small_b, "Matrix B");

        Matrix c_seq_small = multiply_sequential(small_a, small_b);
        Matrix c_par_small = multiply_parallel(small_a, small_b, num_threads);

        print_matrix(c_seq_small, "Result (sequential)");
        print_matrix(c_par_small, "Result (parallel)");

        cout << "Совпадение результатов: " 
             << (matrices_equal(c_seq_small, c_par_small) ? "ДА" : "НЕТ") << "\n\n";

        cout << "=== Замеры производительности (большие матрицы) ===\n";
        Matrix large_a = load_matrix(argv[3]);
        Matrix large_b = load_matrix(argv[4]);

        cout << "Размер матриц: " << large_a.n << "x" << large_a.n << "\n";

        auto start = chrono::high_resolution_clock::now();
        Matrix c_seq_large = multiply_sequential(large_a, large_b);
        auto end = chrono::high_resolution_clock::now();
        double t_seq = chrono::duration<double>(end - start).count();

        start = chrono::high_resolution_clock::now();
        Matrix c_par_large = multiply_parallel(large_a, large_b, num_threads);
        end = chrono::high_resolution_clock::now();
        double t_par = chrono::duration<double>(end - start).count();

        cout << "Время (последовательно): " << t_seq << " с\n";
        cout << "Время (параллельно): " << t_par << " с\n";
        if (t_par > 0) {
            cout << "Ускорение: " << (t_seq / t_par) << "x\n";
        }
        cout << "Совпадение результатов: " 
             << (matrices_equal(c_seq_large, c_par_large) ? "ДА" : "НЕТ") << "\n";

        return 0;
    }
    catch (const exception& e) {
        cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
}
