#include "TaskScheduler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <future>
#include <string>

// Constructor with configuration
TaskScheduler::TaskScheduler(const Config& config) 
    : config_(config), isRunning(false), shouldStop(false), taskIdCounter(0), 
      executedTaskCount(0), failedTaskCount(0), cancelledTaskCount(0)
{
    LogMessage("TaskScheduler created with configuration");
    if (config_.enableDetailedLogging) {
        std::cout << "  Max queue size: " << config_.maxQueueSize << std::endl;
        std::cout << "  Shutdown timeout: " << config_.shutdownTimeout.count() << "ms" << std::endl;
    }
}

// Destructor
TaskScheduler::~TaskScheduler()
{
    Stop();
    LogMessage("TaskScheduler destroyed");
}

// Move constructor
TaskScheduler::TaskScheduler(TaskScheduler&& other) noexcept
    : config_(std::move(other.config_)), 
      taskQueue(std::move(other.taskQueue)),
      cancelledTasks(std::move(other.cancelledTasks)),
      isRunning(other.isRunning.load()),
      shouldStop(other.shouldStop.load()),
      taskIdCounter(other.taskIdCounter.load()),
      executedTaskCount(other.executedTaskCount.load()),
      failedTaskCount(other.failedTaskCount.load()),
      cancelledTaskCount(other.cancelledTaskCount.load())
{
    LogMessage("TaskScheduler moved");
}

// Move assignment operator
TaskScheduler& TaskScheduler::operator=(TaskScheduler&& other) noexcept
{
    if (this != &other) {
        Stop(); // Stop current scheduler
        
        config_ = std::move(other.config_);
        taskQueue = std::move(other.taskQueue);
        cancelledTasks = std::move(other.cancelledTasks);
        isRunning.store(other.isRunning.load());
        shouldStop.store(other.shouldStop.load());
        taskIdCounter.store(other.taskIdCounter.load());
        executedTaskCount.store(other.executedTaskCount.load());
        failedTaskCount.store(other.failedTaskCount.load());
        cancelledTaskCount.store(other.cancelledTaskCount.load());
        
        LogMessage("TaskScheduler move-assigned");
    }
    return *this;
}

// Add method - returns task ID for cancellation
TaskScheduler::TaskId TaskScheduler::Add(std::function<void()> task, std::time_t timestamp)
{
    if (!task) {
        LogMessage("Error: Cannot add null task");
        return 0; // Invalid task ID
    }
    
    // Generate unique task ID
    TaskId taskId = ++taskIdCounter;
    
    // Check queue size limit
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (taskQueue.size() >= config_.maxQueueSize) {
            LogMessage("Error: Task queue is full, cannot add Task #" + std::to_string(taskId));
            return 0; // Invalid task ID
        }
    }
    
    // Convert timestamp to readable format for logging (using thread-safe localtime_s)
    if (config_.enableDetailedLogging) {
        std::tm timeInfo = {};
        if (localtime_s(&timeInfo, &timestamp) == 0) {
            std::cout << "Adding Task #" << taskId 
                      << " scheduled for: " << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") 
                      << " (timestamp: " << timestamp << ")" << std::endl;
        } else {
            std::cout << "Adding Task #" << taskId 
                      << " with timestamp: " << timestamp << std::endl;
        }
    }
    
    // Thread-safe task insertion
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        taskQueue.emplace(std::move(task), timestamp, taskId);
        
        if (config_.enableDetailedLogging) {
            std::cout << "Task #" << taskId << " added to queue. "
                      << "Total pending tasks: " << taskQueue.size() << std::endl;
        }
    }
    
    // Notify worker thread that new task is available (if scheduler is running)
    if (isRunning.load()) {
        condition.notify_one();
        if (config_.enableDetailedLogging) {
            std::cout << "Task #" << taskId << " - Worker thread notified" << std::endl;
        }
    } else {
        if (config_.enableDetailedLogging) {
            std::cout << "Task #" << taskId << " - Scheduler not running, task queued for later execution" << std::endl;
        }
    }
    
    return taskId;
}

// Cancel a specific task by ID
bool TaskScheduler::CancelTask(TaskId taskId)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    // Add to cancelled set
    auto result = cancelledTasks.insert(taskId);
    bool wasInserted = result.second;
    
    if (wasInserted) {
        ++cancelledTaskCount;
        LogMessage("Task #" + std::to_string(taskId) + " marked for cancellation");
        condition.notify_one(); // Wake up worker to process cancellation
        return true;
    } else {
        LogMessage("Task #" + std::to_string(taskId) + " was already cancelled or not found");
        return false;
    }
}

// Cancel all pending tasks
size_t TaskScheduler::CancelAllTasks()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    size_t cancelledCount = 0;
    std::priority_queue<ScheduledTask> tempQueue = taskQueue;
    
    while (!tempQueue.empty()) {
        TaskId taskId = tempQueue.top().taskId;
        if (cancelledTasks.find(taskId) == cancelledTasks.end()) {
            cancelledTasks.insert(taskId);
            cancelledCount++;
        }
        tempQueue.pop();
    }
    
    cancelledTaskCount += cancelledCount;
    LogMessage("Cancelled " + std::to_string(cancelledCount) + " pending tasks");
    condition.notify_one();
    
    return cancelledCount;
}

// Start the scheduler
void TaskScheduler::Start()
{
    if (isRunning.load()) {
        LogMessage("TaskScheduler is already running");
        return;
    }
    
    LogMessage("Starting TaskScheduler...");
    isRunning.store(true);
    shouldStop.store(false);
    
    // Start worker thread
    try {
        workerThread = std::thread(&TaskScheduler::WorkerThreadFunction, this);
        LogMessage("Worker thread started successfully");
    } catch (const std::exception& e) {
        LogMessage("Failed to start worker thread: " + std::string(e.what()));
        isRunning.store(false);
        return;
    }
    
    LogMessage("TaskScheduler started successfully");
}

// Stop the scheduler
void TaskScheduler::Stop()
{
    if (!isRunning.load()) {
        return;
    }
    
    LogMessage("Stopping TaskScheduler...");
    
    // Signal the worker thread to stop
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        shouldStop.store(true);
    }
    
    // Wake up the worker thread if it's waiting
    condition.notify_all();
    
    // Wait for worker thread to finish with timeout
    if (workerThread.joinable()) {
        LogMessage("Waiting for worker thread to finish...");
        
        // Try to join with timeout
        auto future = std::async(std::launch::async, [this]() {
            workerThread.join();
        });
        
        if (future.wait_for(config_.shutdownTimeout) == std::future_status::timeout) {
            LogMessage("Warning: Worker thread did not finish within timeout");
            // In a real implementation, you might need to detach or force terminate
            workerThread.detach();
        } else {
            LogMessage("Worker thread joined successfully");
        }
    }
    
    isRunning.store(false);
    LogMessage("TaskScheduler stopped");
}

// Check if scheduler is running
bool TaskScheduler::IsRunning() const
{
    return isRunning.load();
}

// Get number of pending tasks (excluding cancelled)
size_t TaskScheduler::GetPendingTaskCount() const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    // Count non-cancelled tasks
    size_t count = 0;
    std::priority_queue<ScheduledTask> tempQueue = taskQueue;
    
    while (!tempQueue.empty()) {
        if (cancelledTasks.find(tempQueue.top().taskId) == cancelledTasks.end()) {
            count++;
        }
        tempQueue.pop();
    }
    
    return count;
}

// Check if task queue is empty (excluding cancelled tasks)
bool TaskScheduler::IsEmpty() const
{
    return GetPendingTaskCount() == 0;
}

// Get timestamp of next task to execute (excluding cancelled)
std::time_t TaskScheduler::GetNextTaskTime() const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    std::priority_queue<ScheduledTask> tempQueue = taskQueue;
    
    while (!tempQueue.empty()) {
        const auto& task = tempQueue.top();
        if (cancelledTasks.find(task.taskId) == cancelledTasks.end()) {
            return task.timestamp;
        }
        tempQueue.pop();
    }
    
    return 0; // No valid tasks
}

// Check if there are tasks ready to execute
bool TaskScheduler::HasReadyTasks() const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    if (taskQueue.empty()) {
        return false;
    }
    return taskQueue.top().IsReady();
}

// Get next ready task (removes from queue)
TaskScheduler::ScheduledTask TaskScheduler::GetNextReadyTask()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    if (taskQueue.empty() || !taskQueue.top().IsReady()) {
        throw std::runtime_error("No ready tasks available");
    }
    
    ScheduledTask task = taskQueue.top();
    taskQueue.pop();
    
    std::cout << "Retrieved Task #" << task.taskId 
              << " for execution. Remaining tasks: " << taskQueue.size() << std::endl;
    
    return task;
}

// Get executed task count
size_t TaskScheduler::GetExecutedTaskCount() const
{
    return executedTaskCount.load();
}

// Get failed task count
size_t TaskScheduler::GetFailedTaskCount() const
{
    return failedTaskCount.load();
}

// Check if a specific task is still pending
bool TaskScheduler::IsTaskPending(TaskId taskId) const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    // Check if task is cancelled
    if (cancelledTasks.find(taskId) != cancelledTasks.end()) {
        return false;
    }
    
    // Search in the queue
    std::priority_queue<ScheduledTask> tempQueue = taskQueue;
    while (!tempQueue.empty()) {
        if (tempQueue.top().taskId == taskId && !tempQueue.top().isCancelled) {
            return true;
        }
        tempQueue.pop();
    }
    
    return false;
}

// Get cancelled task count
size_t TaskScheduler::GetCancelledTaskCount() const
{
    return cancelledTaskCount.load();
}

// Reset statistics
void TaskScheduler::ResetStatistics()
{
    executedTaskCount.store(0);
    failedTaskCount.store(0);
    cancelledTaskCount.store(0);
    LogMessage("Statistics reset");
}

// Get pending task IDs
std::vector<TaskScheduler::TaskId> TaskScheduler::GetPendingTaskIds() const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    
    std::vector<TaskId> pendingIds;
    std::priority_queue<ScheduledTask> tempQueue = taskQueue;
    
    while (!tempQueue.empty()) {
        const auto& task = tempQueue.top();
        if (cancelledTasks.find(task.taskId) == cancelledTasks.end()) {
            pendingIds.push_back(task.taskId);
        }
        tempQueue.pop();
    }
    
    return pendingIds;
}

// Get queue capacity
size_t TaskScheduler::GetQueueCapacity() const
{
    return config_.maxQueueSize;
}

// Enhanced statistics with cancellation info
void TaskScheduler::PrintStatistics() const
{
    std::cout << "\n=== Enhanced Task Scheduler Statistics ===" << std::endl;
    std::cout << "Tasks executed successfully: " << GetExecutedTaskCount() << std::endl;
    std::cout << "Tasks failed: " << GetFailedTaskCount() << std::endl;
    std::cout << "Tasks cancelled: " << GetCancelledTaskCount() << std::endl;
    std::cout << "Tasks pending: " << GetPendingTaskCount() << std::endl;
    std::cout << "Total tasks processed: " << (GetExecutedTaskCount() + GetFailedTaskCount() + GetCancelledTaskCount()) << std::endl;
    std::cout << "Queue capacity: " << GetQueueCapacity() << std::endl;
    std::cout << "Queue utilization: " << std::fixed << std::setprecision(1) 
              << (100.0 * GetPendingTaskCount() / GetQueueCapacity()) << "%" << std::endl;
    std::cout << "===========================================" << std::endl;
}

// Cleanup cancelled tasks from queue
void TaskScheduler::CleanupCancelledTasks()
{
    if (cancelledTasks.empty()) return;
    
    std::priority_queue<ScheduledTask> cleanQueue;
    size_t removedCount = 0;
    
    while (!taskQueue.empty()) {
        // Extract task components to avoid copy issues
        const ScheduledTask& topTask = taskQueue.top();
        
        if (cancelledTasks.find(topTask.taskId) == cancelledTasks.end()) {
            // Create new task with moved function to avoid copy
            cleanQueue.emplace(
                std::move(const_cast<ScheduledTask&>(topTask).task),
                topTask.timestamp,
                topTask.taskId
            );
        } else {
            removedCount++;
        }
        
        taskQueue.pop();
    }
    
    taskQueue = std::move(cleanQueue);
    cancelledTasks.clear();
    
    if (config_.enableDetailedLogging && removedCount > 0) {
        LogMessage("Cleaned up " + std::to_string(removedCount) + " cancelled tasks from queue");
    }
}

// Enhanced worker thread with cancellation support
void TaskScheduler::WorkerThreadFunction()
{
    LogMessage("Worker thread started (Thread ID: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ")");
    
    while (!shouldStop.load()) {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        // Periodic cleanup of cancelled tasks
        CleanupCancelledTasks();
        
        if (shouldStop.load()) break;
        
        if (taskQueue.empty()) {
            if (config_.enableDetailedLogging) {
                LogMessage("Worker thread: No tasks, waiting...");
            }
            condition.wait(lock, [this] { 
                return !taskQueue.empty() || shouldStop.load(); 
            });
            continue;
        }
        
        const ScheduledTask& nextTask = taskQueue.top();
        std::time_t currentTime = std::time(nullptr);
        
        // Check if task is cancelled
        if (cancelledTasks.find(nextTask.taskId) != cancelledTasks.end()) {
            taskQueue.pop();
            if (config_.enableDetailedLogging) {
                LogMessage("Worker thread: Skipping cancelled Task #" + std::to_string(nextTask.taskId));
            }
            continue;
        }
        
        if (nextTask.IsReady()) {
            // Create a copy of the task before popping (since top() returns const reference)
            ScheduledTask taskToExecute(
                std::move(const_cast<ScheduledTask&>(taskQueue.top()).task), 
                taskQueue.top().timestamp, 
                taskQueue.top().taskId
            );
            taskQueue.pop();
            
            lock.unlock();
            ExecuteTask(taskToExecute);
            lock.lock();
            
            if (config_.enableDetailedLogging) {
                LogMessage("Worker thread: Task #" + std::to_string(taskToExecute.taskId) + 
                          " execution completed. Remaining tasks: " + std::to_string(taskQueue.size()));
            }
        } else {
            std::time_t waitTime = nextTask.timestamp - currentTime;
            if (config_.enableDetailedLogging) {
                LogMessage("Worker thread: Next task #" + std::to_string(nextTask.taskId) + 
                          " not ready yet. Waiting " + std::to_string(waitTime) + " seconds...");
            }
            
            auto waitUntil = std::chrono::system_clock::from_time_t(nextTask.timestamp);
            auto waitResult = condition.wait_until(lock, waitUntil, [this] {
                return shouldStop.load() || 
                       (!taskQueue.empty() && taskQueue.top().IsReady());
            });
            
            if (config_.enableDetailedLogging) {
                LogMessage(waitResult ? "Worker thread: Wait interrupted" : "Worker thread: Wait timeout");
            }
        }
    }
    
    LogMessage("Worker thread stopping");
}

// Enhanced task execution with cancellation check
void TaskScheduler::ExecuteTask(const ScheduledTask& task)
{
    // Double-check if task was cancelled
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (cancelledTasks.find(task.taskId) != cancelledTasks.end()) {
            LogMessage("Task #" + std::to_string(task.taskId) + " was cancelled before execution");
            return;
        }
    }
    
    LogMessage(">>> EXECUTING Task #" + std::to_string(task.taskId) + " <<<");
    
    // Record execution start time
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        // Execute the actual task function with error handling
        SafeTaskExecution(task.task, task.taskId);
        
        // Record successful execution
        ++executedTaskCount;
        
        // Calculate execution time
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        LogMessage(">>> Task #" + std::to_string(task.taskId) + " completed successfully in " + 
                  std::to_string(duration.count()) + " microseconds <<<");
        
    } catch (const std::exception& e) {
        ++failedTaskCount;
        LogMessage(">>> Task #" + std::to_string(task.taskId) + " failed with exception: " + std::string(e.what()) + " <<<");
    } catch (...) {
        ++failedTaskCount;
        LogMessage(">>> Task #" + std::to_string(task.taskId) + " failed with unknown exception <<<");
    }
}

// Safe execution wrapper for task functions
void TaskScheduler::SafeTaskExecution(const std::function<void()>& taskFunction, TaskId taskId)
{
    if (!taskFunction) {
        throw std::runtime_error("Task function is null");
    }
    
    if (config_.enableDetailedLogging) {
        LogMessage("Executing task function for Task #" + std::to_string(taskId) + "...");
    }
    
    // Execute the user's task function
    taskFunction();
    
    if (config_.enableDetailedLogging) {
        LogMessage("Task function for Task #" + std::to_string(taskId) + " completed.");
    }
}

// Log message helper
void TaskScheduler::LogMessage(const std::string& message) const
{
    if (config_.enableDetailedLogging) {
        std::cout << "[TaskScheduler] " << message << std::endl;
    }
} 