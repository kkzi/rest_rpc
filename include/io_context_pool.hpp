#pragma once

#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>

namespace rpc {

class io_context_pool final : private boost::noncopyable
{
public:
    explicit io_context_pool(std::size_t pool_size)
        : io_cursor_(0)
    {
        for (auto i = 0; i < pool_size; ++i) {
            auto io = std::make_shared<boost::asio::io_context>();
            io_contexts_.push_back(io);
            works_.push_back(std::make_shared<boost::asio::io_context::work>(*io));
        }
    }

    ~io_context_pool()
    {
        stop();
    }

    void run()
    {
        std::vector<std::shared_ptr<std::thread>> threads;
        for (auto & io : io_contexts_) {
            threads.emplace_back(std::make_shared<std::thread>([](io_context_ptr svr) {
                svr->run();
            }, io));
        }

        for (auto & t : threads) {
            t->join();
        }
    }

    void stop()
    {
        for (auto & io : io_contexts_) {
            io->stop();
            io->reset();
        }
    }

    boost::asio::io_context & get_io_service()
    {
        auto & io = *io_contexts_[io_cursor_];
        ++io_cursor_;
        if (io_cursor_ == io_contexts_.size()) io_cursor_ = 0;
        return io;
    }

private:
    typedef std::shared_ptr<boost::asio::io_context> io_context_ptr;
    typedef std::shared_ptr<boost::asio::io_context::work> work_ptr;

    /// The pool of io_services.
    std::vector<io_context_ptr> io_contexts_;

    /// The work that keeps the io_services running.
    std::vector<work_ptr> works_;

    /// The next io_context to use for a connection.
    std::size_t io_cursor_;
};

}  // namespace rest_rpc
