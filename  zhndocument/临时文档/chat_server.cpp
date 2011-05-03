#define _DLL
#define _RTLDLL
#define BOOST_DYN_LINK

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <set>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include "chat_message.hpp"				// 消息包格式定义

using boost::asio::ip::tcp;

//----------------------------------------------------------------------

typedef std::deque<chat_message> chat_message_queue;

//----------------------------------------------------------------------

//聊天室的参与者
class chat_participant
{
public:
  virtual ~chat_participant() {		}

  // 发言
  virtual void deliver(const chat_message& msg) = 0;
};


// 智能指针：用于管理参与者对象
typedef boost::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

// 聊天室
class chat_room
{
public:

  /*
      *功能： 加入聊天室
      *participant： 参与者对象的指针
    */
  void join(chat_participant_ptr participant)
  {
    participants_.insert(participant);

    std::for_each(recent_msgs_.begin(), recent_msgs_.end(),
        boost::bind(&chat_participant::deliver, participant, _1));
  }

  // 离开聊天室
  void leave(chat_participant_ptr participant)
  {
    participants_.erase(participant);
  }

  // 发言
  void deliver(const chat_message& msg)
  {
    recent_msgs_.push_back(msg);			// 消息包入队	

    while (recent_msgs_.size() > max_recent_msgs)
	{
      recent_msgs_.pop_front();
	}

    std::for_each(participants_.begin(), participants_.end(),
        boost::bind(&chat_participant::deliver, _1, boost::ref(msg)));
  }

private:
  std::set<chat_participant_ptr> participants_;		// 聊天室所有参与者的集合

  enum { max_recent_msgs = 100 };					// 最大消息数

  chat_message_queue recent_msgs_;					// 消息队列
};

//----------------------------------------------------------------------

class chat_session
  : public chat_participant,
    public boost::enable_shared_from_this<chat_session>
{
public:
  chat_session(boost::asio::io_service& io_service, chat_room& room)
    : socket_(io_service),
      room_(room)
  {
  }

  tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    room_.join(shared_from_this());		// 加入聊天室

	// 投递一个异步读请求—读取消息头
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), chat_message::header_length),
        boost::bind(
          &chat_session::handle_read_header, shared_from_this(),
          boost::asio::placeholders::error));
  }

  // 发言
  void deliver(const chat_message& msg)
  {
    bool write_in_progress = !write_msgs_.empty();

    write_msgs_.push_back(msg);

    if (!write_in_progress)
    {
		// 投递一个异步写请求
      boost::asio::async_write(socket_,
          boost::asio::buffer(write_msgs_.front().data(),
            write_msgs_.front().length()),
          boost::bind(&chat_session::handle_write, shared_from_this(),
            boost::asio::placeholders::error));
    }
  }
  
  // 异步读取操作完成后的回调函数—读取消息头
  void handle_read_header(const boost::system::error_code& error)
  {
    if (!error && read_msg_.decode_header())
    {
		// 投递一个异步读请求—读取消息体
      boost::asio::async_read(socket_,				// I/O Object
		  boost::asio::buffer(read_msg_.body(),		
		  read_msg_.body_length()),
		  boost::bind(&chat_session::handle_read_body, shared_from_this(),
            boost::asio::placeholders::error));
    }
    else
    {
      room_.leave(shared_from_this());
    }
  }

  // 异步读取操作完成后的回调函数—读取消息体
  void handle_read_body(const boost::system::error_code& error)
  {
    if (!error)
    {
      room_.deliver(read_msg_);

	  // 投递一个异步读请求—读取消息头
      boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_.data(), chat_message::header_length),
          boost::bind(&chat_session::handle_read_header, shared_from_this(),
            boost::asio::placeholders::error));
    }
    else
    {
      room_.leave(shared_from_this());		// 离开聊天室
    }
  }

  
  void handle_write(const boost::system::error_code& error)
  {
    if (!error)
    {
      write_msgs_.pop_front();			// 这个消息包已发送，出队

      if (!write_msgs_.empty())			// 消息队列中有消息包
      {
		  // 投递一个异步写请求
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(),
              write_msgs_.front().length()),
            boost::bind(&chat_session::handle_write, shared_from_this(),
              boost::asio::placeholders::error));
      }
    }
    else
    {
      room_.leave(shared_from_this());	 // 离开聊天室
    }
  }

private:
  tcp::socket socket_;					// socket对象
  chat_room& room_;						// 聊天室
  chat_message read_msg_;				// 读取到的消息包
  chat_message_queue write_msgs_;
};


typedef boost::shared_ptr<chat_session> chat_session_ptr;


//----------------------------------------------------------------------

// 聊天室服务器
class chat_server
{
public:
  chat_server(boost::asio::io_service& io_service,
      const tcp::endpoint& endpoint)
    : io_service_(io_service),
      acceptor_(io_service, endpoint)
  {
    chat_session_ptr new_session(new chat_session(io_service_, room_));

	// 投递一个异步接受请求—接受客户端连接
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&chat_server::handle_accept, this, new_session,
          boost::asio::placeholders::error));
  }

  // 异步接收请求async_accept完成时调用的回调函数
  void handle_accept(chat_session_ptr session,
      const boost::system::error_code& error)
  {
    if (!error)
    {
      session->start();
      chat_session_ptr new_session(new chat_session(io_service_, room_));

	  // 再次投递相同的async_accept请求，不停的接受客户端的连接请求
      acceptor_.async_accept(new_session->socket(),
          boost::bind(&chat_server::handle_accept, this, new_session,
            boost::asio::placeholders::error));
    }
  }


private:
  boost::asio::io_service& io_service_;		// 前摄器对象	
  tcp::acceptor acceptor_;					// 接收器对象
  chat_room room_;							// 聊天室

};

typedef boost::shared_ptr<chat_server> chat_server_ptr;
typedef std::list<chat_server_ptr> chat_server_list;



//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";

      return 1;
    }

    boost::asio::io_service io_service;		// 前摄器对象

    chat_server_list servers;
    for (int i = 1; i < argc; ++i)
    {
      using namespace std;		// For atoi.

      tcp::endpoint endpoint(tcp::v4(), atoi(argv[i]));		// 本地监听的套接字地址信息（IP，Port）

      chat_server_ptr server(new chat_server(io_service, endpoint));
      servers.push_back(server);
    }

    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
