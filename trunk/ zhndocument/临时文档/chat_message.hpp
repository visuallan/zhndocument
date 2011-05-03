
#ifndef CHAT_MESSAGE_HPP
#define CHAT_MESSAGE_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>

// 通讯消息包
// 消息包 = 消息头（4byte） + 消息体（<512byte）
class chat_message
{
public:

	// 类中的匿名枚举，用作类作用域下的整型常量
	enum { header_length = 4 };
	enum { max_body_length = 512 };

	chat_message()
		: body_length_(0)
	{
	}

	const char* data() const
	{
		return data_;
	}

	char* data()
	{
		return data_;
	}

	size_t length() const
	{
		return header_length + body_length_;
	}

	const char* body() const
	{
		return data_ + header_length;
	}

	char* body()
	{
		return data_ + header_length;
	}

	size_t body_length() const
	{
		return body_length_;
	}

	void body_length(size_t length)
	{
		body_length_ = length;

		if (body_length_ > max_body_length)
		{
			body_length_ = max_body_length;
		}
	}

	bool decode_header()
	{
		using namespace std;	// For strncat and atoi.

		char header[header_length + 1] = "";
		strncat(header, data_, header_length);
		body_length_ = atoi(header);
		if (body_length_ > max_body_length)
		{
			body_length_ = 0;

			return false;
		}

		return true;
	}

	void encode_header()
	{
		using namespace std;	// For sprintf and memcpy.

		char header[header_length + 1] = "";
		sprintf(header, "%4d", body_length_);
		memcpy(data_, header, header_length);
	}

private:
	char data_[header_length + max_body_length];		// 消息包内部缓冲区
	size_t body_length_;								// 消息体长度

};


#endif // CHAT_MESSAGE_HPP
