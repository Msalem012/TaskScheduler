# Планировщик Задач (TaskScheduler)

## Описание задания

**Исходное техническое задание:**
> Реализовать класс планировщик задач. В классе должен быть метод `Add(std::function<void()> task, std::time_t timestamp)`, параметры: task - задача на выполнение, timestamp - время, не раньше которого задача должна быть выполнена. Задание на знание потоков и примитивов синхронизации. Используем стандартную библиотеку.

## Обзор реализации

Данный проект представляет собой полнофункциональный планировщик задач на C++, который превосходит первоначальные требования и включает множество продвинутых возможностей для промышленного использования.

### Основные возможности

-  **Планирование задач по времени** - выполнение функций в заданное время
-  **Многопоточность** - асинхронное выполнение задач в отдельном потоке
-  **Отмена задач** - индивидуальная и массовая отмена запланированных задач
-  **Безопасность потоков** - все операции thread-safe
-  **Статистика выполнения** - подробная отчетность о выполненных/неудачных задачах
-  **Обработка исключений** - надежная обработка ошибок без крашей
-  **Конфигурируемость** - настраиваемые параметры работы
-  **Управление ресурсами** - RAII, move-семантика, автоматическая очистка

## Поэтапная реализация

### Этап 1: Базовая архитектура класса

**Что реализовано:**
- Основная структура класса `TaskScheduler`
- Интерфейс метода `Add(std::function<void()> task, std::time_t timestamp)`
- Структура `ScheduledTask` для хранения задач
- Базовые методы `Start()`, `Stop()`

**Ключевые компоненты:**
```cpp
class TaskScheduler {
    struct ScheduledTask {
        std::function<void()> task;
        std::time_t timestamp;
        size_t taskId;
    };
    
    std::priority_queue<ScheduledTask> taskQueue;  // Очередь с приоритетом по времени
    std::thread workerThread;                      // Рабочий поток
    std::mutex queueMutex;                        // Мьютекс для безопасности
    std::condition_variable condition;            // Условная переменная
};
```

### Этап 2: Система хранения задач

**Что реализовано:**
- Приоритетная очередь для сортировки задач по времени выполнения
- Уникальные ID задач для отслеживания
- Улучшенное логирование с читаемыми временными метками
- Статистические методы: `GetPendingTaskCount()`, `IsEmpty()`, `GetNextTaskTime()`

**Почему важно:**
- `std::priority_queue` автоматически сортирует задачи по времени выполнения
- Уникальные ID обеспечивают детерминированный порядок для задач с одинаковым временем
- Thread-safe операции гарантируют корректность в многопоточной среде

### Этап 3: Инфраструктура потоков

**Что реализовано:**
- Полноценный рабочий поток (`WorkerThreadFunction`)
- Умное ожидание с `condition_variable`
- Точное планирование с `wait_until()`
- Корректное завершение потока

**Ключевые особенности:**
```cpp
void WorkerThreadFunction() {
    while (!shouldStop.load()) {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        if (taskQueue.empty()) {
            condition.wait(lock, [this] { 
                return !taskQueue.empty() || shouldStop.load(); 
            });
            continue;
        }
        
        if (nextTask.IsReady()) {
            // Выполнить задачу
        } else {
            // Ждать до времени выполнения
            auto waitUntil = std::chrono::system_clock::from_time_t(nextTask.timestamp);
            condition.wait_until(lock, waitUntil, [this] {
                return shouldStop.load() || HasReadyTasks();
            });
        }
    }
}
```

### Этап 4: Выполнение задач и обработка ошибок

**Что реализовано:**
- Безопасное выполнение пользовательских функций
- Комплексная обработка исключений (`std::exception` и неизвестных)
- Измерение времени выполнения с микросекундной точностью
- Статистика выполнения: успешные/неудачные задачи

**Обработка исключений:**
```cpp
void ExecuteTask(const ScheduledTask& task) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        task.task();  // Выполнение пользовательской функции
        ++executedTaskCount;
    } catch (const std::exception& e) {
        ++failedTaskCount;
        LogMessage("Task failed: " + std::string(e.what()));
    } catch (...) {
        ++failedTaskCount;
        LogMessage("Task failed with unknown exception");
    }
}
```

### Этап 5: Продвинутые возможности

**Что реализовано:**

#### Система отмены задач
- `CancelTask(taskId)` - отмена конкретной задачи
- `CancelAllTasks()` - массовая отмена всех задач
- `IsTaskPending(taskId)` - проверка статуса задачи

#### Конфигурационная система
```cpp
struct Config {
    bool enableDetailedLogging = true;     // Детальное логирование
    size_t maxQueueSize = 1000;           // Максимальный размер очереди
    std::chrono::milliseconds shutdownTimeout{5000}; // Таймаут завершения
};
```

#### Move-семантика и RAII
- Класс не копируется (deleted copy constructor/assignment)
- Поддержка move-конструктора и move-assignment
- Автоматическая очистка ресурсов в деструкторе

#### Расширенная статистика
- `GetExecutedTaskCount()` - количество выполненных задач
- `GetFailedTaskCount()` - количество неудачных задач  
- `GetCancelledTaskCount()` - количество отмененных задач
- `PrintStatistics()` - подробный отчет
- `ResetStatistics()` - сброс статистики

## Технические детали

### Примитивы синхронизации

1. **std::mutex (queueMutex)** - защищает доступ к очереди задач
2. **std::condition_variable (condition)** - эффективное ожидание событий
3. **std::atomic<bool>** - флаги состояния без блокировок
4. **std::unique_lock/std::lock_guard** - RAII-обертки для мьютексов

### Управление временем

- **std::time_t** - временные метки для планирования
- **std::chrono::system_clock** - для точного ожидания
- **std::chrono::high_resolution_clock** - измерение времени выполнения

### Структуры данных

- **std::priority_queue<ScheduledTask>** - автоматическая сортировка по времени
- **std::unordered_set<TaskId>** - быстрый поиск отмененных задач O(1)
- **std::vector<TaskId>** - список ID ожидающих задач

## Использование

### Базовое использование
```cpp
#include "TaskScheduler.h"

int main() {
    TaskScheduler scheduler;
    scheduler.Start();
    
    // Добавление задачи на выполнение через 5 секунд
    std::time_t futureTime = std::time(nullptr) + 5;
    auto taskId = scheduler.Add([]() {
        std::cout << "Задача выполнена!" << std::endl;
    }, futureTime);
    
    // Ожидание выполнения
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    scheduler.Stop();
    return 0;
}
```

### Продвинутое использование
```cpp
// Конфигурация
TaskScheduler::Config config;
config.maxQueueSize = 500;
config.enableDetailedLogging = false;

TaskScheduler scheduler(config);
scheduler.Start();

// Добавление задач
auto id1 = scheduler.Add([]() { /* задача 1 */ }, time1);
auto id2 = scheduler.Add([]() { /* задача 2 */ }, time2);

// Отмена задачи
scheduler.CancelTask(id1);

// Мониторинг
std::cout << "Ожидающих: " << scheduler.GetPendingTaskCount() << std::endl;
std::cout << "Выполнено: " << scheduler.GetExecutedTaskCount() << std::endl;

// Статистика
scheduler.PrintStatistics();
```

## Преимущества реализации

### Производительность
- Минимальные блокировки благодаря atomic операциям
- Эффективное ожидание с condition_variable
- O(log n) вставка и O(1) доступ к следующей задаче

### Надежность
- Исключения в пользовательских задачах не роняют планировщик
- Корректное завершение потоков с таймаутом
- Автоматическая очистка отмененных задач

### Гибкость
- Конфигурируемые параметры работы
- Поддержка lambda-функций с захватом переменных
- Возможность отмены и мониторинга задач

### Безопасность
- Thread-safe операции
- RAII для управления ресурсами
- Защита от переполнения очереди

## Соответствие требованиям

 **Метод Add с нужной сигнатурой**: `TaskId Add(std::function<void()> task, std::time_t timestamp)`  
 **Использование потоков**: рабочий поток для выполнения задач  
 **Примитивы синхронизации**: mutex, condition_variable, atomic  
 **Стандартная библиотека**: только STL, никаких внешних зависимостей  
 **Планирование по времени**: задачи выполняются не раньше указанного времени  

## Требования к компиляции

- **C++11** или новее (рекомендуется C++14+)
- Поддержка стандартной библиотеки потоков
- Компиляторы: GCC, Clang, MSVC

### Компиляция
```bash
g++ -std=c++14 -pthread -O2 project.cpp TaskScheduler.cpp -o scheduler
```
