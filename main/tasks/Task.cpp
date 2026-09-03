#include "Task.h"

#include <esp_timer.h>

void TaskHistory::add(const std::string& taskName, TaskStatus status, const std::string& message) {
    TaskHistoryEntry entry;
    entry.taskName = taskName;
    entry.status = status;
    entry.message = message;
    entry.timestamp = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    entries_.push_back(entry);
}

void TaskRegistry::addTask(std::shared_ptr<Task> task) {
    if (task) {
        tasks_.push_back(task);
    }
}
