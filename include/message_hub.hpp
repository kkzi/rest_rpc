#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <set>
#include <deque>
#include <memory>
#include <functional>

#include <boost/asio.hpp>


namespace rpc
{

namespace hub_detail
{

class message;
class client;
class connection;

class hub
{
public:
    virtual ~hub() = 0 {};

public:
    virtual void distribute(std::shared_ptr<client> subscriber, message& msg) = 0;
    virtual void deliver(std::shared_ptr<connection> publisher, message& msg) = 0;
};



class message final
{
public:
    enum message_type : char { SUBSCRIBE, UNSUBSCRIBE, PUBLISH };
    enum { VERSION = 0x1 };
    enum { COOKIE = 0xF00D | (VERSION << 8) };
    enum { MESSAGE_SIZE = 0x2000 };

public:
    const char* data() const
    {
        return data_.data;
    }

    char* data()
    {
        return data_.data;
    }

    bool verify() const
    {
        return data_.magic == COOKIE;
    }

    size_t header_length() const
    {
        return offsetof(packet, payload);
    }
    size_t length() const
    {
        return header_length() + body_length();
    }

    char* body()
    {
        return data_.payload;
    }

    size_t body_length() const
    {
        return data_.topic_length + data_.body_length;
    }

    char* topic()
    {
        return body();
    }

    size_t topic_length() const
    {
        return data_.topic_length;
    }

    message_type type() const
    {
        return data_.msg_type;
    }

    void set_type(message_type t)
    {
        data_.msg_type = t;
    }

    char* content()
    {
        return body() + topic_length();
    }

    size_t content_length() const
    {
        return data_.body_length;
    }

    void set_message(const std::string& topic)
    {
        assert(topic.size() <= 0xffff);
        memcpy(body(), topic.c_str(), topic.length());
        data_.topic_length = (uint16_t)topic.length();
        data_.body_length = 0;
        data_.magic = COOKIE;
    }

    void set_message(const std::string& topic, const std::vector<char>& msg)
    {
        assert(msg.size() <= 0xffff);
        set_message(topic);
        data_.body_length = (uint16_t)msg.size();
        if (header_length() + msg.size() > MESSAGE_SIZE) {
            throw std::out_of_range("Povided message is too big");
        }

        memcpy(body() + data_.topic_length, msg.data(), msg.size());
    }

private:
#pragma pack(push, 1)
    union packet
    {
        struct
        {
            uint16_t     topic_length;
            uint16_t     body_length;
            message_type msg_type;
            uint16_t     magic;
            char         payload[1];
        };
        char data[MESSAGE_SIZE];
    };
#pragma pack(pop)
    packet data_;
};


using message_queue = std::deque<message>;
using tcp = boost::asio::ip::tcp;

class client final : public std::enable_shared_from_this<client>
{
public:
    client(boost::asio::ip::tcp::socket socket, hub &proc)
        : socket_(std::move(socket))
        , distributor_(proc)
    {

    }

    ~client() = default;

public:
    tcp::socket& socket()
    {
        return socket_;
    }

    void start()
    {
        do_read_header();

    }

    void write(const message& msg)
    {
        bool write_in_progress = !out_msg_queue_.empty();
        out_msg_queue_.push_back(msg);
        if (!write_in_progress) {
            do_write();
        }
    }

private:
    void do_read_header()
    {
        auto self(shared_from_this());
        auto data = boost::asio::buffer(in_msg_.data(), in_msg_.header_length());
        boost::asio::async_read(socket_, data, [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec && in_msg_.verify()) {
                do_read_body();
            }
            else {
                socket_.close();
            }
        });
    }

    void do_read_body()
    {
        auto self(shared_from_this());
        auto data = boost::asio::buffer(in_msg_.body(), in_msg_.body_length());
        boost::asio::async_read(socket_, data, [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                distributor_.distribute(self, in_msg_);
                do_read_header();
            }
            else {
                socket_.close();
            }
        });
    }

    void do_write()
    {
        auto self(shared_from_this());
        auto data = boost::asio::buffer(out_msg_queue_.front().data(), out_msg_queue_.front().length());
        boost::asio::async_write(socket_, data, [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                out_msg_queue_.pop_front();
                if (!out_msg_queue_.empty()) {
                    do_write();
                }
            }
            else {
                socket_.close();
            }
        });
    }

private:
    tcp::socket   socket_;
    hub&          distributor_;
    message       in_msg_;
    message_queue out_msg_queue_;
};

using client_ptr = std::shared_ptr<client>;



class connection final : public std::enable_shared_from_this<connection>
{
public:
    connection(boost::asio::io_context& io, hub& courier)
        : io_(io)
        , socket_(io)
        , courier_(courier)
    {

    }

    bool init(const std::string& host, uint16_t port)
    {
        try {
            tcp::resolver resolver(io_);
            tcp::resolver::query query(host, std::to_string(port));
            tcp::resolver::iterator iterator = resolver.resolve(query);
            boost::asio::connect(socket_, iterator);

            auto data = boost::asio::buffer(inmsg_.data(), inmsg_.header_length());
            boost::asio::async_read(socket_, data, [=](boost::system::error_code ec, size_t) {
                if (!ec) {
                    do_read_header();
                }
            });
        }
        catch (std::exception&) {
            return false;
        }
        return true;
    }

    bool write(const message& msg, bool blocking = false)
    {
        try {
            if (blocking) {
                boost::asio::write(socket_, boost::asio::buffer(msg.data(), msg.length()));
            }
            else {
                boost::asio::post(io_, [this, msg]() {
                    bool write_in_progress = !outmsg_queue_.empty();
                    outmsg_queue_.push_back(msg);
                    if (!write_in_progress) {
                        do_write();
                    }
                });
            }
        }
        catch (std::exception&) {
            return false;
        }

        return true;
    }

    void close()
    {
        io_.post(std::bind(&connection::do_close, this));
    }

private:
    void do_read_header()
    {
        boost::asio::async_read(socket_, boost::asio::buffer(inmsg_.body(), inmsg_.body_length()), [=](boost::system::error_code ec, size_t) {
            if (!ec && inmsg_.verify()) {
                do_read_body();
            }
            else {
                do_close();
            }
        });
    }

    void do_read_body()
    {
        courier_.deliver(shared_from_this(), inmsg_);
        auto data = boost::asio::buffer(inmsg_.data(), inmsg_.header_length());
        boost::asio::async_read(socket_, data, [=](boost::system::error_code ec, size_t) {
            if (!ec) {
                do_read_header();
            }
            else {
                do_close();
            }
        });
    }

    void do_write()
    {
        auto data = boost::asio::buffer(outmsg_queue_.front().data(), outmsg_queue_.front().length());
        boost::asio::async_write(socket_, data, [this](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                outmsg_queue_.pop_front();
                if (!outmsg_queue_.empty()) {
                    do_write();
                }
            }
            else {
                socket_.close();
            }
        });
    }

    void do_close()
    {
        socket_.close();
    }

private:
    hub&                         courier_;
    boost::asio::io_context&     io_;
    boost::asio::ip::tcp::socket socket_;
    message                      inmsg_;
    message_queue                outmsg_queue_;
};


using connection_ptr = std::shared_ptr<connection>;

}  // !namespace message_hub_detail





class message_hub : public hub_detail::hub
{
public:
    using subscribe_callback_t = std::function<void(const std::string& topic, const std::string& message)>;

public:
    message_hub()
        : acceptor_(io_)
        , work_guard_(new boost::asio::io_context::work(io_))
        , publisher_(new hub_detail::connection(io_, *this))
        , available_(false)
    {
        //boost::asio::make_work_guard(io_);
    }

    ~message_hub()
    {
        work_guard_.reset();
        io_.stop();
        for (auto & it : work_threads_) {
            it->join();
        }
    }

public:
    void distribute(hub_detail::client_ptr subscriber, hub_detail::message& msg) override
    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);

        std::string topic(msg.body(), msg.topic_length());
        auto it = subscribers_.find(topic);
        auto exist = it != subscribers_.end();
        switch (msg.type()) {
        case hub_detail::message::PUBLISH:
            if (exist) {
                for (auto s : it->second) {
                    s->write(msg);
                }
            }
            break;

        case hub_detail::message::SUBSCRIBE:
            if (exist) {
                it->second.insert(subscriber);
            }
            else {
                subscriber_set temp;
                temp.insert(subscriber);
                subscribers_.insert(std::make_pair(topic, temp));
            }
            break;

        case hub_detail::message::UNSUBSCRIBE:
            if (exist) {
                it->second.erase(subscriber);
                if (!it->second.size()) {
                    subscribers_.erase(it);
                }
            }
            break;

        default:
            break;
        }
    }

    void deliver(hub_detail::connection_ptr publisher, hub_detail::message& msg) override
    {
        std::string topic(msg.body(), msg.topic_length());
        if (handlers_.count(topic)) {
            std::string body(msg.content(), msg.content() + msg.content_length());
            handlers_.at(topic)(topic, body);
        }
    }

public:
    bool connect(const std::string& hostip, uint16_t port, uint8_t threads = 1)
    {
        initpool(threads);
        available_ = publisher_->init(hostip, port);
        return available_;
    }

    bool create(uint16_t port, uint8_t threads = 1)
    {
        using tcp = boost::asio::ip::tcp;
        tcp::endpoint endpoint(tcp::v4(), port);
        acceptor_ = tcp::acceptor(io_, endpoint);

        accept_next();
        initpool(threads);
        available_ = publisher_->init("localhost", port);
        return available_;
    }

    bool unsubscribe(const std::string& topic)
    {
        if (!available_) return false;

        if (handlers_.count(topic)) {
            hub_detail::message msg;
            msg.set_message(topic);
            msg.set_type(hub_detail::message::UNSUBSCRIBE);
            return publisher_->write(msg, true);
        }
        return true;
    }

    bool subscribe(const std::string& topic, subscribe_callback_t handler)
    {
        assert(handler);

        if (!available_) return false;

        handlers_[topic] = handler;

        hub_detail::message msg;
        msg.set_message(topic);
        msg.set_type(hub_detail::message::SUBSCRIBE);
        publisher_->write(msg, true);

        // TODO: wait feedback form server here?
        return true;
    }

    bool publish(const std::string& topic, const std::vector<char>& message)
    {
        if (!available_) return false;

        hub_detail::message msg;
        msg.set_message(topic, message);
        msg.set_type(hub_detail::message::PUBLISH);
        return publisher_->write(msg);
    }

    bool publish(const std::string& topic, const std::string& message)
    {
        std::vector<char> data;
        std::copy(message.begin(), message.end(), back_inserter(data));
        return publish(topic, data);
    }

private:
    void accept_next()
    {
        acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec) {
                std::make_shared<hub_detail::client>(std::move(socket), *this)->start();
            }
            accept_next();
        });
    }

    void handle_accept(hub_detail::client_ptr client, boost::system::error_code ec)
    {
        if (!ec) {
            client->start();
        }
        else {
            // TODO: Handle IO error - on thread exit
            //int e = ec.value();
        }
        accept_next();
    }

    void initpool(uint8_t count)
    {
        if (count == 0) count++;

        for (auto i = 0; i < count; ++i) {
            work_threads_.emplace_back(std::make_shared<std::thread>([=] {
                io_.run();
            }));
        }
    }


private:
    boost::asio::io_context io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<boost::asio::io_context::work> work_guard_;
    hub_detail::connection_ptr publisher_;
    bool available_;

    std::mutex subscribers_mutex_;

    using thread_ptr = std::shared_ptr<std::thread>;
    std::vector<thread_ptr> work_threads_;

    typedef std::set<hub_detail::client_ptr> subscriber_set;
    std::map<std::string, subscriber_set> subscribers_;

    std::map<std::string, message_hub::subscribe_callback_t> handlers_;
};


}  // !namespace rpc
