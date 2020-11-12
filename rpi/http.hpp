#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>

class Http 
	:	public boost::enable_shared_from_this<Http> {
public:
	struct Req {
		std::string path_;
		bool connectionClose_;
	};
	
	struct Res {
		std::string headers_; // Empty on error
		std::vector<uint8_t> content_;
	};
	
	static boost::shared_ptr<Http> create(boost::asio::io_service & io, const std::string & host, const std::string & port) {
		boost::shared_ptr<Http> ptr(new Http(io, host, port));
		return ptr;
	}
	void start(boost::function<void(const boost::system::error_code & error)> connectHandler);
	
	~Http();
	void sendReq(const Req & req, boost::function<void(const Res & res)> handler);
private:
	enum State {
		RESOLVING,
		CONNECTING,
		CONNECTED,
		DEAD
	};
	
	Http(boost::asio::io_service & io, const std::string & host, const std::string & port);
	
	void onResolve(const boost::system::error_code & error, boost::asio::ip::tcp::resolver::iterator iterator, boost::function<void(const boost::system::error_code & error)> connectHandler);
	void onConnect(const boost::system::error_code & error, boost::asio::ip::tcp::resolver::iterator iterator, boost::function<void(const boost::system::error_code & error)> connectHandler);
	void onSendReq(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<std::string> reqMsg, boost::function<void(const Res & res)> handler);
	void onReceive(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr< std::vector<uint8_t> > data);
	
	boost::asio::io_service & io_;
	boost::asio::ip::tcp::resolver resolver_;
	boost::asio::ip::tcp::socket sock_;
	std::string host_, port_;
	State state_;
	bool requestBusy_;
};
