// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Background task executor.
//
// Implementation:
//   - Worker is the executor; Task is a one-shot spawn recipe. State holds only what
//     the running task needs: entry, userdata, handle, running.
//   - taskEntry wraps OS lifecycle so user entry stays a plain callback with TaskContext.
//   - Startup notify gate: the spawned task waits until start() finishes publishing
//     State and calls notify(), so entry never sees a half-ready executor.
//   - Cooperative stop: entry polls isRunning(); stop() clears it and notify() wakes
//     idle waits. isAlive() tracks the OS handle until taskEntry calls reset().
//   - idle token (1,1) serializes ownership of the single task slot: start holds it,
//     reset() releases it, join() waits on release.
//   - mutex on handle keeps notify() and reset() from racing on xTaskNotifyGive vs clear.
//   - Cross-task wake uses task notifications; entry blocks on ulTaskNotifyTake.
//   - Single owner calls start/stop/join; entry must not join(self) or destroy Worker.
//   - Teardown: stop() -> join() -> destroy Worker or start() again.

#pragma once

#include <atomic>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "firmware.h"

#include "util/mutex.h"
#include "util/semaphore.h"

// ============================================================
//                     Forward declarations
// ============================================================

class Worker;

struct TaskContext;

// ============================================================
//                            Task
// ============================================================

// Task entry function.
using TaskEntry = void (*)(const TaskContext& ctx);

// Task spawn parameters.
struct Task {
    const char* name = nullptr;                  // Task name.
    uint32_t stack = 0;                          // Task stack size.
    uint8_t priority = 0;                        // Task priority.
    uint8_t core = Firmware::CORE_SERVICE;       // FreeRTOS CPU index.
    TaskEntry entry = nullptr;                     // Task entry function.
};

// Task entry context.
struct TaskContext {
    const Worker& worker;     // Host worker.
    void* userdata = nullptr; // Application context.
};

// ============================================================
//                          Worker
// ============================================================

// Background task executor.
class Worker {
  public:
    // --- Constructor / Destructor ---

    explicit Worker() = default;

    ~Worker() {
        this->stop();
        this->join();
    }

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // --- Lifecycle ---

    // Start a task.
    bool start(const Task& task, void* userdata) {
        if (task.name == nullptr || task.entry == nullptr || task.stack == 0 || task.core > 1 || this->isAlive()) {
            return false;
        }

        this->idle.take();

        this->entry = task.entry;
        this->userdata = userdata;

        TaskHandle_t handle = nullptr;
        if (xTaskCreatePinnedToCore(Worker::taskEntry, task.name, task.stack, this, task.priority, &handle,
                                    task.core) != pdPASS) {
            this->reset();
            return false;
        }

        this->handle.store(handle, std::memory_order_release);
        this->running.store(true, std::memory_order_release);
        this->notify();

        return true;
    }

    // Stop the task.
    void stop() {
        this->running.store(false, std::memory_order_release);
        this->notify();
    }

    // Wait until the task has finished.
    void join() {
        if (this->handle.load(std::memory_order_relaxed) == xTaskGetCurrentTaskHandle()) {
            return;
        }
        this->idle.take();
        this->idle.give();
    }

    // --- Status ---

    // Whether the task should keep running.
    bool isRunning() const noexcept {
        return this->running.load(std::memory_order_acquire);
    }

    // Whether the task is alive.
    bool isAlive() const noexcept {
        return this->handle.load(std::memory_order_acquire) != nullptr;
    }

    // --- Notify ---

    // Wake the task.
    void notify() const noexcept {
        MutexGuard guard(this->mutex);

        const TaskHandle_t handle = this->handle.load(std::memory_order_relaxed);
        if (handle != nullptr) {
            xTaskNotifyGive(handle);
        }
    }

  private:
    // --- State ---

    TaskEntry entry = nullptr;                  // Entry function.
    void* userdata = nullptr;                   // Application context.
    std::atomic<TaskHandle_t> handle = nullptr; // OS task handle.
    std::atomic<bool> running = false;          // Cooperative run flag.

    // --- Sync ---

    mutable Mutex mutex;  // Task handle mutex.
    Semaphore idle{1, 1}; // Idle token.

    // --- Internal ---

    // Task lifecycle wrapper.
    static void taskEntry(void* param) {
        Worker& self = *static_cast<Worker*>(param);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        TaskContext ctx{self, self.userdata};
        self.entry(ctx);

        self.reset();
        vTaskDelete(NULL);
    }

    // Clear state.
    void reset() {
        this->running.store(false, std::memory_order_relaxed);
        {
            MutexGuard guard(this->mutex);
            this->handle.store(nullptr, std::memory_order_release);
        }
        this->entry = nullptr;
        this->userdata = nullptr;
        this->idle.give();
    }
};
