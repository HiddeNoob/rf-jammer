#ifndef TASK_H
#define TASK_H

#include <memory>
#include <string>
#include <vector>
#include <U8g2lib.h>
#include "RfSweeper.h"

enum class TaskStatus {
    IDLE,
    READY,
    RUNNING,
    SUCCEEDED,
    FAILED,
    STOPPED
};

struct TaskHistoryEntry {
    std::string taskName;
    TaskStatus status;
    std::string message;
    uint32_t timestamp;
};

class Task {
public:
    virtual ~Task() = default;

    virtual const char* name() const = 0;
    virtual std::shared_ptr<Task> clone() const = 0;

    virtual void setSelectedModuleIds(const std::vector<int>& ids) = 0;
    virtual std::vector<int> getSelectedModuleIds() const = 0;

    virtual bool validate() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual void renderStatus(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) = 0;
    virtual void renderConfig(U8G2_SSD1306_128X64_NONAME_F_HW_I2C& display) = 0;

    TaskStatus status() const { return status_; }
    void setStatus(TaskStatus status) { status_ = status; }

protected:
    TaskStatus status_ = TaskStatus::IDLE;
};

class TaskHistory {
public:
    void add(const std::string& taskName, TaskStatus status, const std::string& message);
    const std::vector<TaskHistoryEntry>& entries() const { return entries_; }

private:
    std::vector<TaskHistoryEntry> entries_;
};

class TaskRegistry {
public:
    void addTask(std::shared_ptr<Task> task);
    std::vector<std::shared_ptr<Task>>& tasks() { return tasks_; }
    const std::vector<std::shared_ptr<Task>>& tasks() const { return tasks_; }

private:
    std::vector<std::shared_ptr<Task>> tasks_;
};

#endif
