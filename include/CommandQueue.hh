#ifndef COMMAND_QUEUE_HH
#define COMMAND_QUEUE_HH

#include <variant>
#include <queue>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <utility>
#include "Protocol.hh"

class CommandQueue
{
public:

    CommandQueue() = default;
    ~CommandQueue() = default;

    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    CommandQueue(CommandQueue&&) = delete;
    CommandQueue& operator=(CommandQueue&&) = delete;
    
    // pushes a command into the command queue to be serviced
    void push(Command cmd);
    
    // blocks until a command is available, then returns it
    // returns nullopt only when the queue is empty and shut_down() has been called
    // which is the signal for the matching thread to exit its loop
    std::optional<Command> pop();

    // called by exchange server when the server is stopping
    // wakes up the matching thread if it is sleeping
    void shut_down();

private:

    // holds commands in order of when submitted
    std::queue<Command> m_command_queue;

    // used when pushing or popping into the command queue
    std::mutex m_mutex;

    // wakes the matching thread when a command is pushed, or when shut_down() is called
    std::condition_variable m_cv;

    // flag on when true, i.e. ready to shut down
    // used to signal the matching thread via m_cv
    bool m_shutdown { false };

};

inline void CommandQueue::push(Command cmd)
{

    {

        std::unique_lock lock { m_mutex };
        m_command_queue.push(std::move(cmd));

    }

    // wake the matching thread so it can pop the command we just pushed
    m_cv.notify_one();

}

inline std::optional<Command> CommandQueue::pop()
{

    // grab the mutex
    std::unique_lock lock { m_mutex };

    // thread sleeps until there is a command to process or the queue is shutting down
    m_cv.wait(lock, [this] { return !m_command_queue.empty() || m_shutdown; });

    // nullopt if the command queue is empty; checks which one woke it up
    if (m_command_queue.empty())
    {

        return std::nullopt;

    }

    // grab the command and return it (std::queue::pop does not return the popped item, so we move it first)
    Command cmd { std::move(m_command_queue.front()) };
    m_command_queue.pop();
    return cmd;

}

inline void CommandQueue::shut_down()
{

    {

        std::unique_lock lock { m_mutex };
        m_shutdown = true;

    }

    m_cv.notify_all();

}

#endif // COMMAND_QUEUE_HH