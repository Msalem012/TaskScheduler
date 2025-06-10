#pragma once

#include <functional>
#include <ctime>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <atomic>
#include <unordered_set>
#include <memory>

/**
 * Task Scheduler Class
 * Manages scheduled execution of tasks at specified timestamps
 * Features: Task scheduling, cancellation, statistics, thread-safe operations
 */
class TaskScheduler
{
public:
    // Task ID type for cancellation and tracking
    using TaskId = size_t;
    
    // Configuration structure
    struct Config {
        bool enableDetailedLogging = true;
        size_t maxQueueSize = 1000;
        std::chrono::milliseconds shutdownTimeout{5000};
    };

    // Constructor with optional configuration
    TaskScheduler(const Config& config = Config{});
    
    // Destructor
    ~TaskScheduler();
    
    // Disable copy constructor and assignment (non-copyable)
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;
    
    // Enable move constructor and assignment
    TaskScheduler(TaskScheduler&& other) noexcept;
    TaskScheduler& operator=(TaskScheduler&& other) noexcept;
    
    // Main method to add tasks with timestamp - returns task ID for cancellation
    TaskId Add(std::function<void()> task, std::time_t timestamp);
    
    // Cancel a specific task by ID (returns true if task was found and cancelled)
    bool CancelTask(TaskId taskId);
    
    // Cancel all pending tasks
    size_t CancelAllTasks();
    
    // Method to start the scheduler
    void Start();
    
    // Method to stop the scheduler
    void Stop();
    
    // Method to check if scheduler is running
    bool IsRunning() const;
    
    // Utility methods for task management
    size_t GetPendingTaskCount() const;
    bool IsEmpty() const;
    std::time_t GetNextTaskTime() const;
    
    // Check if a specific task is still pending
    bool IsTaskPending(TaskId taskId) const;
    
    // Task execution statistics
    size_t GetExecutedTaskCount() const;
    size_t GetFailedTaskCount() const;
    size_t GetCancelledTaskCount() const;
    void PrintStatistics() const;
    void ResetStatistics();
    
    // Advanced utilities
    std::vector<TaskId> GetPendingTaskIds() const;
    size_t GetQueueCapacity() const;

private:
    // Structure to hold task information
    struct ScheduledTask
    {
        std::function<void()> task;
        std::time_t timestamp;
        TaskId taskId;
        bool isCancelled;
        
        // Constructor
        ScheduledTask(std::function<void()> t, std::time_t ts, TaskId id) 
            : task(std::move(t)), timestamp(ts), taskId(id), isCancelled(false) {}
        
        // Copy constructor - explicitly defaulted
        ScheduledTask(const ScheduledTask& other)
            : task(other.task), timestamp(other.timestamp), 
              taskId(other.taskId), isCancelled(other.isCancelled) {}
        
        // Copy assignment operator
        ScheduledTask& operator=(const ScheduledTask& other) {
            if (this != &other) {
                task = other.task;
                timestamp = other.timestamp;
                taskId = other.taskId;
                isCancelled = other.isCancelled;
            }
            return *this;
        }
        
        // Move constructor
        ScheduledTask(ScheduledTask&& other) noexcept
            : task(std::move(other.task)), timestamp(other.timestamp), 
              taskId(other.taskId), isCancelled(other.isCancelled) {}
        
        // Move assignment operator
        ScheduledTask& operator=(ScheduledTask&& other) noexcept {
            if (this != &other) {
                task = std::move(other.task);
                timestamp = other.timestamp;
                taskId = other.taskId;
                isCancelled = other.isCancelled;
            }
            return *this;
        }
        
        // Comparison operator for priority queue (earlier tasks have higher priority)
        bool operator<(const ScheduledTask& other) const {
            if (timestamp == other.timestamp) {
                return taskId > other.taskId;
            }
            return timestamp > other.timestamp;
        }
        
        // Helper methods
        bool IsReady() const {
            return !isCancelled && timestamp <= std::time(nullptr);
        }
        
        std::time_t GetTimeUntilExecution() const {
            std::time_t now = std::time(nullptr);
            return (timestamp > now) ? (timestamp - now) : 0;
        }
    };
    
    // Configuration
    Config config_;
    
    // Task storage - priority queue ordered by timestamp
    std::priority_queue<ScheduledTask> taskQueue;
    
    // Set of cancelled task IDs for quick lookup
    std::unordered_set<TaskId> cancelledTasks;
    
    // Threading components
    std::thread workerThread;
    mutable std::mutex queueMutex;
    std::condition_variable condition;
    
    // Control flags
    std::atomic<bool> isRunning;
    std::atomic<bool> shouldStop;
    
    // Task counter for unique IDs
    std::atomic<TaskId> taskIdCounter;
    
    // Execution statistics
    std::atomic<size_t> executedTaskCount;
    std::atomic<size_t> failedTaskCount;
    std::atomic<size_t> cancelledTaskCount;
    
    // Private methods
    void WorkerThreadFunction();
    
    // Helper methods for task management
    bool HasReadyTasks() const;
    ScheduledTask GetNextReadyTask();
    void CleanupCancelledTasks();
    
    // Task execution methods
    void ExecuteTask(const ScheduledTask& task);
    void SafeTaskExecution(const std::function<void()>& taskFunction, TaskId taskId);
    
    // Resource management
    void Reset();
    void LogMessage(const std::string& message) const;
}; 