#include <iostream>
#include <boost/bind.hpp>
#include "http.hpp"

enum {
	BUF_SIZE = 2048
};

Http::~Http() {
}

void Http::sendReq(const Http::Req & req, boost::function<void(const Res & res)> handler) {
	if (state_ != CONNECTED || requestBusy_) {
		io_.post(boost::bind(handler, Res())); // Empty error result
	}
	else {
		requestBusy_ = true;
		std::ostringstream msg;
		msg << "GET " << req.path_ << " HTTP/1.1\r\n";
		msg << "Host: " << host_ << "\r\n";
		msg << "Connection: " << (req.connectionClose_ ? "close" : "keep-alive") << "\r\n";
		msg << "\r\n";
		boost::shared_ptr<std::string> msgPtr(new std::string(msg.str()));
		boost::asio::async_write(sock_, boost::asio::buffer(*msgPtr), boost::bind(&Http::onSendReq, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, msgPtr, handler));
	}
}

Http::Http(boost::asio::io_service & io, const std::string & host, const std::string & port)
	:	io_(io),
		resolver_(io_),
		sock_(io_),
		host_(host),
		port_(port),
		state_(RESOLVING),
		requestBusy_(false) {
}

void Http::start(boost::function<void(const boost::system::error_code & error)> connectHandler) {
	boost::asio::ip::tcp::resolver::query q(host_, port_);
	resolver_.async_resolve(q, boost::bind(&Http::onResolve, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::iterator, connectHandler));
}

void Http::onResolve(const boost::system::error_code & error, boost::asio::ip::tcp::resolver::iterator iterator, boost::function<void(const boost::system::error_code & error)> connectHandler) {
	if (!error) {
		state_ = CONNECTING;
		boost::asio::async_connect(sock_, iterator, boost::bind(&Http::onConnect, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::iterator, connectHandler));
	}
	else {
		state_ = DEAD;
		connectHandler(error);
	}
}

void Http::onConnect(const boost::system::error_code & error, boost::asio::ip::tcp::resolver::iterator iterator, boost::function<void(const boost::system::error_code & error)> connectHandler) {
	(void) iterator;
	if (!error) {
		state_ = CONNECTED;
		std::cout << "HTTP Connected" << std::endl;
		boost::shared_ptr< std::vector<uint8_t> > data(new std::vector<uint8_t>(BUF_SIZE));
		sock_.async_read_some(boost::asio::buffer(*data), boost::bind(&Http::onReceive, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, data));
	}
	else {
		state_ = DEAD;
	}
	connectHandler(error);
}

void Http::onSendReq(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<std::string> reqMsg, boost::function<void(const Res & res)> handler) {
	reqMsg.reset();
	if (!error) {
		// TODO!?
	}
	else {
		state_ = DEAD;
		handler(Res()); // Empty error result
	}
}

void Http::onReceive(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr< std::vector<uint8_t> > data) {
	if (!error) {
		// TODO: process the HTTP data
	}
	else {
		// TODO!?
		state_ = DEAD;
	}
}

