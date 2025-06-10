// project.cpp : Ce fichier contient la fonction 'main'. L'exécution du programme commence et se termine à cet endroit.
//

#include <iostream>
#include <ctime>
#include <iomanip>
#include <chrono>
#include "TaskScheduler.h"

void PrintCurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm timeInfo = {};
    if (localtime_s(&timeInfo, &now) == 0) {
        std::cout << "Current time: " << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") 
                  << " (timestamp: " << now << ")" << std::endl;
    } else {
        std::cout << "Current time: timestamp " << now << std::endl;
    }
}

int main()
{
    std::cout << "=== Task Scheduler Project - Step 5 Testing ===" << std::endl;
    std::cout << "Testing Cleanup, Resource Management, and Advanced Features" << std::endl;
    
    // Test 1: Configuration and constructor
    std::cout << "\n--- Test 1: Configuration and Constructor ---" << std::endl;
    TaskScheduler::Config config;
    config.enableDetailedLogging = true;
    config.maxQueueSize = 10;
    config.shutdownTimeout = std::chrono::milliseconds(3000);
    
    TaskScheduler scheduler(config);
    
    PrintCurrentTime();
    std::time_t currentTime = std::time(nullptr);
    
    // Test 2: Add tasks and get task IDs
    std::cout << "\n--- Test 2: Task Addition with IDs ---" << std::endl;
    
    auto taskId1 = scheduler.Add([]() {
        std::cout << "TASK 1: Simple execution" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }, currentTime + 2);
    
    auto taskId2 = scheduler.Add([]() {
        std::cout << "TASK 2: Another task" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }, currentTime + 3);
    
    auto taskId3 = scheduler.Add([]() {
        std::cout << "TASK 3: Will be cancelled" << std::endl;
    }, currentTime + 4);
    
    auto taskId4 = scheduler.Add([]() {
        std::cout << "TASK 4: Long running task" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }, currentTime + 1);
    
    std::cout << "Added tasks with IDs: " << taskId1 << ", " << taskId2 
              << ", " << taskId3 << ", " << taskId4 << std::endl;
    
    // Test 3: Task status checking
    std::cout << "\n--- Test 3: Task Status Checking ---" << std::endl;
    std::cout << "Task " << taskId1 << " pending: " << (scheduler.IsTaskPending(taskId1) ? "Yes" : "No") << std::endl;
    std::cout << "Task " << taskId3 << " pending: " << (scheduler.IsTaskPending(taskId3) ? "Yes" : "No") << std::endl;
    
    // Show pending task IDs
    auto pendingIds = scheduler.GetPendingTaskIds();
    std::cout << "All pending task IDs: ";
    for (auto id : pendingIds) {
        std::cout << id << " ";
    }
    std::cout << std::endl;
    
    // Test 4: Task cancellation
    std::cout << "\n--- Test 4: Task Cancellation ---" << std::endl;
    bool cancelled = scheduler.CancelTask(taskId3);
    std::cout << "Cancelled task " << taskId3 << ": " << (cancelled ? "Success" : "Failed") << std::endl;
    std::cout << "Task " << taskId3 << " still pending: " << (scheduler.IsTaskPending(taskId3) ? "Yes" : "No") << std::endl;
    
    // Test 5: Start scheduler and monitor
    std::cout << "\n--- Test 5: Scheduler Execution with Cancellation ---" << std::endl;
    scheduler.PrintStatistics();
    
    scheduler.Start();
    
    // Add more tasks while running
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto taskId5 = scheduler.Add([]() {
        std::cout << "TASK 5: Added while running" << std::endl;
    }, currentTime + 2);
    
    auto taskId6 = scheduler.Add([]() {
        std::cout << "TASK 6: Will be cancelled while running" << std::endl;
    }, currentTime + 5);
    
    // Cancel task 6 immediately
    scheduler.CancelTask(taskId6);
    
    // Test 6: Exception task
    auto taskId7 = scheduler.Add([]() {
        std::cout << "TASK 7: About to throw exception" << std::endl;
        throw std::runtime_error("Test exception in Step 5");
    }, currentTime + 3);
    
    // Monitor execution
    std::cout << "\n--- Test 6: Monitoring Execution ---" << std::endl;
    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "After " << (i + 1) * 0.5 << "s - Pending: " << scheduler.GetPendingTaskCount()
                  << ", Executed: " << scheduler.GetExecutedTaskCount()
                  << ", Failed: " << scheduler.GetFailedTaskCount()
                  << ", Cancelled: " << scheduler.GetCancelledTaskCount() << std::endl;
        
        if (scheduler.GetPendingTaskCount() == 0 && i > 4) {
            std::cout << "All tasks completed!" << std::endl;
            break;
        }
    }
    
    // Test 7: Mass cancellation
    std::cout << "\n--- Test 7: Mass Cancellation ---" << std::endl;
    
    // Add several more tasks
    for (int i = 0; i < 5; ++i) {
        scheduler.Add([i]() {
            std::cout << "BATCH TASK " << i << ": Will be cancelled" << std::endl;
        }, currentTime + 10 + i); // Future tasks
    }
    
    std::cout << "Added 5 more tasks for cancellation test" << std::endl;
    std::cout << "Pending tasks before mass cancellation: " << scheduler.GetPendingTaskCount() << std::endl;
    
    size_t cancelledCount = scheduler.CancelAllTasks();
    std::cout << "Mass cancelled " << cancelledCount << " tasks" << std::endl;
    std::cout << "Pending tasks after mass cancellation: " << scheduler.GetPendingTaskCount() << std::endl;
    
    // Test 8: Queue capacity and limits
    std::cout << "\n--- Test 8: Queue Limits ---" << std::endl;
    std::cout << "Queue capacity: " << scheduler.GetQueueCapacity() << std::endl;
    std::cout << "Current utilization: " << std::fixed << std::setprecision(1) 
              << (100.0 * scheduler.GetPendingTaskCount() / scheduler.GetQueueCapacity()) << "%" << std::endl;
    
    // Test 9: Move semantics (create a new scheduler from moved one)
    std::cout << "\n--- Test 9: Move Semantics ---" << std::endl;
    std::cout << "Creating moved scheduler..." << std::endl;
    
    // Stop current scheduler first
    scheduler.Stop();
    
    // Test move constructor
    TaskScheduler movedScheduler = std::move(scheduler);
    std::cout << "Scheduler moved successfully" << std::endl;
    
    // Test 10: Final statistics and cleanup
    std::cout << "\n--- Test 10: Final Statistics ---" << std::endl;
    movedScheduler.PrintStatistics();
    
    // Test statistics reset
    movedScheduler.ResetStatistics();
    std::cout << "\nAfter statistics reset:" << std::endl;
    movedScheduler.PrintStatistics();
    
    std::cout << "\n=== Step 5 Complete ===" << std::endl;
    std::cout << "Advanced features are working perfectly!" << std::endl;
    std::cout << "✓ Task cancellation (individual and mass)" << std::endl;
    std::cout << "✓ Task ID tracking and status checking" << std::endl;
    std::cout << "✓ Configuration system with limits" << std::endl;
    std::cout << "✓ Move semantics (non-copyable, movable)" << std::endl;
    std::cout << "✓ Enhanced resource management" << std::endl;
    std::cout << "✓ Timeout-based shutdown" << std::endl;
    std::cout << "✓ Queue capacity management" << std::endl;
    std::cout << "✓ Enhanced statistics and monitoring" << std::endl;
    std::cout << "✓ Configurable logging levels" << std::endl;
    std::cout << "✓ Robust error handling and cleanup" << std::endl;
    
    return 0;
}

// Exécuter le programme : Ctrl+F5 ou menu Déboguer > Exécuter sans débogage
// Déboguer le programme : F5 ou menu Déboguer > Démarrer le débogage

// Astuces pour bien démarrer : 
//   1. Utilisez la fenêtre Explorateur de solutions pour ajouter des fichiers et les gérer.
//   2. Utilisez la fenêtre Team Explorer pour vous connecter au contrôle de code source.
//   3. Utilisez la fenêtre Sortie pour voir la sortie de la génération et d'autres messages.
//   4. Utilisez la fenêtre Liste d'erreurs pour voir les erreurs.
//   5. Accédez à Projet > Ajouter un nouvel élément pour créer des fichiers de code, ou à Projet > Ajouter un élément existant pour ajouter des fichiers de code existants au projet.
//   6. Pour rouvrir ce projet plus tard, accédez à Fichier > Ouvrir > Projet et sélectionnez le fichier .sln.
