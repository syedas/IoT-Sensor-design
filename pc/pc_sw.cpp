#define BOOST_ASIO_ENABLE_HANDLER_TRACKING

#include <deque>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/chrono.hpp>

enum {
	BUF_SIZE = 1024,
	SE_LINE_LEN_MAX = 16,
	GL_LINE_LEN_MAX = 80,
	GE_LINE_LEN_MAX = 80,
};

typedef boost::asio::local::stream_protocol strm;

class Program {
public:
	struct Config {
		std::string
			sensorLongpollAddr_,
			sensorEventAddr_,
			guiLongpollAddr_,
			guiEventAddr_;
		static Config fromArgv(int argc, char const * const * argv);
	};
	
	Program(const Config & config);
	~Program();
	void operator()();
private:
	enum SensorEvent {
		SMOKE_ON,
		SMOKE_OFF,
		MOTION
	};
	
	enum GuiEvent {
		LED = 1,
		SIREN_CTRL,
		SMOKE_SLEEP,
		AUDIO_STREAM,
		
		TOKEN // not an actual event, but reported at the end of longpolls
	};
	
	class Mgr {
	public:
		Mgr(Program & program, const std::string & addr);
		virtual ~Mgr();
	protected:
		boost::asio::io_service & getIo() { return program_.io_; }
		void startAccept();
		virtual void onAccept(boost::shared_ptr<strm::socket>, const boost::system::error_code & error) = 0;
		Program & program_;
		strm::acceptor acceptor_;
	};
	
	template <typename State, typename Event>
	class LongpollMgr
		:	public Mgr {
	public:
		LongpollMgr(Program & program, const std::string & addr);
		//virtual ~LongpollMgr();
	protected:
		virtual void onEvent(const Event & evt);
		virtual void updateState(State & state, const Event & evt) = 0;
		virtual std::string stateResponse(const State & state, uint8_t token) = 0;
		virtual std::string eventResponse(const State & state, uint8_t token, const std::deque<Event> & eventQueue) = 0;
	private:
		void onAccept(boost::shared_ptr<strm::socket>, const boost::system::error_code & error);
		
		void startRead(boost::shared_ptr<strm::socket> sock);
		void onRead(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data);
		
		void startLongpoll(boost::shared_ptr<strm::socket> sock, const std::string & token);
		
		void startWrite(boost::shared_ptr<strm::socket> sock, const std::string & data);
		void onWrite(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr<std::string> data);
		
		bool isLongpollActive() const { return longpollSock_; }
		
		uint8_t nextToken();
		
		std::string readBuf_;
		boost::shared_ptr<strm::socket> longpollSock_; // If not null, then the longpolling state is active.
		uint8_t lastToken_;
		State state_;
		std::deque<Event> eventQueue_;
	};
	
	// Hack. C++ doesn't seem to support nested classes as template parameters of parent classes.
	struct SensorLongpollMgr_State {
		std::string led_, sirenCtrl_;
	};
	
	struct SensorLongpollMgr_Event {
		GuiEvent event_;
		std::vector<uint8_t> content_;
	};
	
	class SensorLongpollMgr
		:	public LongpollMgr<SensorLongpollMgr_State, SensorLongpollMgr_Event> {
	public:
		SensorLongpollMgr(Program & program, const std::string & addr);
		//virtual ~SensorLongpollMgr();
		void onGuiCommand(const std::string & command);
		void onGuiAudio(const std::vector<uint8_t> & audio);
	private:
		typedef SensorLongpollMgr_State State;
		typedef SensorLongpollMgr_Event Event;
		
		virtual void updateState(State & state, const Event & evt);
		virtual std::string stateResponse(const State & state, uint8_t token);
		virtual std::string eventResponse(const State & state, uint8_t token, const std::deque<Event> & eventQueue);
	};
	
	class SensorEventMgr
		:	public Mgr {
	public:
		SensorEventMgr(Program & program, const std::string & addr);
		//virtual ~SensorEventMgr();
	private:
		virtual void onAccept(boost::shared_ptr<strm::socket>, const boost::system::error_code & error);
		
		void startRead(boost::shared_ptr<strm::socket> sock);
		void onRead(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data);
		
		void processLine(const std::string & line);
		
		std::string readBuf_;
	};
	
	// Hack. C++ doesn't seem to support nested classes as template parameters of parent classes.
	struct GuiLongpollMgr_State {
		bool smokeState_, hasLastSmokeEvent_, hasLastMotion_;
		boost::chrono::system_clock::time_point lastSmokeEvent_, lastMotion_;
		GuiLongpollMgr_State() : smokeState_(), hasLastSmokeEvent_(false), hasLastMotion_(false) { }
	};
	
	struct GuiLongpollMgr_Event {
		boost::chrono::system_clock::time_point time_;
		SensorEvent event_;
	};
	
	class GuiLongpollMgr
		:	public LongpollMgr<GuiLongpollMgr_State, GuiLongpollMgr_Event> {
	public:
		GuiLongpollMgr(Program & program, const std::string & addr);
		//virtual ~GuiLongpollMgr();
		void onSensorEvent(SensorEvent evt);
	private:
		typedef GuiLongpollMgr_State State;
		typedef GuiLongpollMgr_Event Event;
		
		static std::string timeToString(const boost::chrono::system_clock::time_point & time) {
			std::ostringstream result;
			result << boost::chrono::duration_cast<boost::chrono::seconds>(time.time_since_epoch()).count();
			return result.str();
		}
		
		virtual void updateState(State & state, const Event & evt);
		virtual std::string stateResponse(const State & state, uint8_t token);
		virtual std::string eventResponse(const State & state, uint8_t token, const std::deque<Event> & eventQueue);
	};
	
	class GuiEventMgr
		:	public Mgr {
	public:
		GuiEventMgr(Program & program, const std::string & addr);
		//virtual ~GuiEventMgr();
	private:
		virtual void onAccept(boost::shared_ptr<strm::socket>, const boost::system::error_code & error);
		
		void startRead(boost::shared_ptr<strm::socket> sock, void(GuiEventMgr::*handler)(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data));
		void onReadFirstLine(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data);
		void onReadTheRest(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data);
		
		std::string readBuf_;
		std::string command_;
		bool audioStream_;
	};
	
	void onSignal(const boost::system::error_code & error, int signal_number);
	
	void onSensorEvent(SensorEvent evt) { gl_.onSensorEvent(evt); }
	void onGuiCommand(const std::string & command) { sl_.onGuiCommand(command); }
	void onGuiAudio(const std::vector<uint8_t> & audio) { sl_.onGuiAudio(audio); }
	
	boost::asio::io_service io_;
	boost::asio::signal_set signals_;
	SensorLongpollMgr sl_;
	SensorEventMgr    se_;
	GuiLongpollMgr    gl_;
	GuiEventMgr       ge_;
};

Program::Program(const Config & config)
	:	signals_(io_, SIGINT, SIGTERM),
		sl_(*this, config.sensorLongpollAddr_),
		se_(*this, config.sensorEventAddr_   ),
		gl_(*this, config.guiLongpollAddr_   ),
		ge_(*this, config.guiEventAddr_      ) {
	signals_.async_wait(boost::bind(&Program::onSignal, this, boost::asio::placeholders::error, boost::asio::placeholders::signal_number));
}

Program::~Program() {
}

void Program::operator()() {
	io_.run();
}

void Program::onSignal(const boost::system::error_code & error, int signal_number) {
	(void) signal_number;
	if (!error) {
		io_.stop();
	}
}

Program::Config Program::Config::fromArgv(int argc, char const * const * argv) {
	if (argc != 5) {
		throw std::runtime_error("argc != 5");
	}
	Config config;
	config.sensorLongpollAddr_ = argv[1];
	config.sensorEventAddr_    = argv[2];
	config.guiLongpollAddr_    = argv[3];
	config.guiEventAddr_       = argv[4];
	return config;
}

Program::Mgr::Mgr(Program & program, const std::string & addr)
	:	program_(program),
		acceptor_(getIo(), strm::endpoint(addr)) {
	startAccept();
}

Program::Mgr::~Mgr() {
}

void Program::Mgr::startAccept() {
	boost::shared_ptr<strm::socket> sock(new strm::socket(getIo()));
	acceptor_.async_accept(*sock, boost::bind(&Mgr::onAccept, this, sock, boost::asio::placeholders::error));
}

template <typename State, typename Event>
Program::LongpollMgr<State, Event>::LongpollMgr(Program & program, const std::string & addr)
	:	Mgr(program, addr),
		lastToken_() {
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::onEvent(const Event & evt) {
	updateState(state_, evt);
	if (isLongpollActive()) {
		std::deque<Event> q;
		q.push_back(evt);
		startWrite(longpollSock_, eventResponse(state_, nextToken(), q)); // TODO: token
		longpollSock_.reset();
	}
	else {
		eventQueue_.push_back(evt);
	}
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::onAccept(boost::shared_ptr<strm::socket> sock, const boost::system::error_code & error) {
	if (!error) {
		startRead(sock);
	}
	else {
		// TODO: error
	}
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::startRead(boost::shared_ptr<strm::socket> sock) {
	boost::shared_ptr< std::vector<uint8_t> > data(new std::vector<uint8_t>(GL_LINE_LEN_MAX));
	sock->async_read_some(boost::asio::buffer(*data), boost::bind(&LongpollMgr<State, Event>::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, data));
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::onRead(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data) {
	// TODO
	bool handleError = false;
	bool finishRead = false;
	std::string token;
	for (size_t i = 0; i < bytes_transferred; i++) {
		uint8_t c = (*data)[i];
		readBuf_ += c;
		if (c == '\n') {
			token = readBuf_.substr(0, readBuf_.size()-1);
			readBuf_.clear();
			finishRead = true;
			break;
		}
		else if (readBuf_.size() >= GL_LINE_LEN_MAX) {
			handleError = true;
			break;
		}
	}
	// We can try even if we have an error.
	if (finishRead) {
		startLongpoll(sock, token);
	}
	else if (error || handleError) {
		// error.
		// Close the current connection and start listening for a new one.
		sock->close();
		sock.reset();
		startAccept();
	}
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::startLongpoll(boost::shared_ptr<strm::socket> sock, const std::string & token) {
	// TODO. For example, handle tokens correctly.
	longpollSock_.reset();
	if (token.empty()) {
		startWrite(sock, stateResponse(state_, nextToken()));
		eventQueue_.clear();
	}
	else {
		if (eventQueue_.empty()) {
			// Go into waiting state
			longpollSock_ = sock;
		}
		else {
			startWrite(sock, eventResponse(state_, nextToken(), eventQueue_));
			eventQueue_.clear();
		}
	}
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::startWrite(boost::shared_ptr<strm::socket> sock, const std::string & data) {
	boost::shared_ptr<std::string> dataPtr(new std::string(data));
	boost::asio::async_write(*sock, boost::asio::buffer(*dataPtr), boost::bind(&LongpollMgr<State, Event>::onWrite, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, dataPtr));
}

template <typename State, typename Event>
void Program::LongpollMgr<State, Event>::onWrite(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr<std::string> data) {
	(void) data;
	sock->close();
	sock.reset();
	// Start listening for a new connection
	startAccept();
}

template <typename State, typename Event>
uint8_t Program::LongpollMgr<State, Event>::nextToken() {
	if (++lastToken_ == 0) {
		++lastToken_;
	}
	return lastToken_;
}

Program::SensorLongpollMgr::SensorLongpollMgr(Program & program, const std::string & addr)
	:	LongpollMgr<State, Event>(program, addr) {
}

void Program::SensorLongpollMgr::onGuiCommand(const std::string & command) {
	std::string cmdLine(command.substr(0, command.find('\n'))), content(command.substr(cmdLine.size()+1));
	Event evt;
	evt.content_.assign(content.begin(), content.end());
	if (cmdLine == "led") {
		evt.event_ = LED;
		onEvent(evt);
	}
	else if (cmdLine == "siren_ctrl") {
		evt.event_ = SIREN_CTRL;
		onEvent(evt);
	}
	else if (cmdLine == "smoke_sleep") {
		evt.event_ = SMOKE_SLEEP;
		onEvent(evt);
	}
	//else if (cmdLine == "audio_stream") {
	//}
	else {
		// error?
	}
}

void Program::SensorLongpollMgr::onGuiAudio(const std::vector<uint8_t> & audio) {
	Event evt;
	evt.event_ = AUDIO_STREAM;
	evt.content_ = audio;
	onEvent(evt);
}

void Program::SensorLongpollMgr::updateState(State & state, const Event & evt) {
	switch (evt.event_) {
	case LED:
		state.led_ = std::string(evt.content_.begin(), evt.content_.end());
		break;
	case SIREN_CTRL:
		state.sirenCtrl_ = std::string(evt.content_.begin(), evt.content_.end());
		break;
	default:
		break;
	}
}

std::string Program::SensorLongpollMgr::stateResponse(const State & state, uint8_t token) {
	std::ostringstream response;
	response << uint8_t(LED       ) << uint8_t(state.led_.size()       >> 8) << uint8_t(state.led_.size()      ) << state.led_.substr      (0, 0xffff);
	response << uint8_t(SIREN_CTRL) << uint8_t(state.sirenCtrl_.size() >> 8) << uint8_t(state.sirenCtrl_.size()) << state.sirenCtrl_.substr(0, 0xffff);
	response << uint8_t(TOKEN) << uint8_t(0) << uint8_t(1) << token;
	return response.str();
}

std::string Program::SensorLongpollMgr::eventResponse(const State & state, uint8_t token, const std::deque<Event> & eventQueue) {
	(void) state;
	std::ostringstream response;
	for (std::deque<Event>::const_iterator it = eventQueue.begin(); it != eventQueue.end(); ++it) {
		const Event & item = *it;
		response << uint8_t(item.event_) << uint8_t(item.content_.size() >> 8) << uint8_t(item.content_.size()) << std::string(item.content_.begin(), item.content_.begin() + std::min<std::string::size_type>(0xffff, item.content_.size()));
	}
	response << uint8_t(TOKEN) << uint8_t(0) << uint8_t(1) << token;
	return response.str();
}

Program::SensorEventMgr::SensorEventMgr(Program & program, const std::string & addr)
	:	Mgr(program, addr) {
}

void Program::SensorEventMgr::onAccept(boost::shared_ptr<strm::socket> sock, const boost::system::error_code & error) {
	if (!error) {
		startRead(sock);
	}
	else {
		// TODO: error
	}
}

void Program::SensorEventMgr::startRead(boost::shared_ptr<strm::socket> sock) {
	boost::shared_ptr< std::vector<uint8_t> > data(new std::vector<uint8_t>(SE_LINE_LEN_MAX));
	sock->async_read_some(boost::asio::buffer(*data), boost::bind(&SensorEventMgr::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, data));
}

void Program::SensorEventMgr::onRead(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data) {
	bool handleError = false;
	for (size_t i = 0; i < bytes_transferred; i++) {
		uint8_t c = (*data)[i];
		readBuf_ += c;
		if (c == '\n') {
			processLine(readBuf_);
			readBuf_.clear();
		}
		else if (readBuf_.size() >= SE_LINE_LEN_MAX) {
			handleError = true;
		}
	}
	if (error || handleError) {
		// error.
		// Close the current connection and start listening for a new one.
		sock->close();
		sock.reset();
		startAccept();
	}
	else {
		startRead(sock);
	}
}

void Program::SensorEventMgr::processLine(const std::string & line) {
	// TODO
	if (line == "smoke_on\n") {
		program_.onSensorEvent(SMOKE_ON);
	}
	else if (line == "smoke_off\n") {
		program_.onSensorEvent(SMOKE_OFF);
	}
	else if (line == "motion\n") {
		program_.onSensorEvent(MOTION);
	}
	else {
		// error...?
	}
}

Program::GuiLongpollMgr::GuiLongpollMgr(Program & program, const std::string & addr)
	:	LongpollMgr<State, Event>(program, addr) {
}

void Program::GuiLongpollMgr::onSensorEvent(Program::SensorEvent evt) {
	Event timedEvent;
	timedEvent.time_ = boost::chrono::system_clock::now();
	timedEvent.event_ = evt;
	onEvent(timedEvent);
}

void Program::GuiLongpollMgr::updateState(State & state, const Event & evt) {
	switch (evt.event_) {
	case SMOKE_ON:
		state.smokeState_ = true;
		state.hasLastSmokeEvent_ = true;
		state.lastSmokeEvent_ = evt.time_;
		break;
	case SMOKE_OFF:
		state.smokeState_ = false;
		state.hasLastSmokeEvent_ = true;
		state.lastSmokeEvent_ = evt.time_;
		break;
	case MOTION:
		state.hasLastMotion_ = true;
		state.lastMotion_ = evt.time_;
		break;
	}
}

std::string Program::GuiLongpollMgr::stateResponse(const State & state, uint8_t token) {
	std::ostringstream response;
	(state.hasLastSmokeEvent_ ? (response << timeToString(state.lastSmokeEvent_)) : (response << '-')) << ' ' << (state.smokeState_ ? "smoke_on" : "smoke_off") << '\n';
	if (state.hasLastMotion_) {
		response << timeToString(state.lastMotion_) << ' ' << "motion" << '\n';
	}
	response << "token:" << static_cast<unsigned int>(token) << '\n';
	return response.str();
}

std::string Program::GuiLongpollMgr::eventResponse(const State & state, uint8_t token, const std::deque<Event> & eventQueue) {
	(void) state;
	std::ostringstream response;
	for (std::deque<Event>::const_iterator it = eventQueue.begin(); it != eventQueue.end(); ++it) {
		const Event & item = *it;
		std::string eventType;
		switch (item.event_) {
		case SMOKE_ON:  eventType = "smoke_on" ; break;
		case SMOKE_OFF: eventType = "smoke_off"; break;
		case MOTION:    eventType = "motion"   ; break;
		}
		response << timeToString(item.time_) << ' ' << eventType << '\n';
	}
	response << "token:" << static_cast<unsigned int>(token) << '\n';
	return response.str();
}


Program::GuiEventMgr::GuiEventMgr(Program & program, const std::string & addr)
	:	Mgr(program, addr) {
}

void Program::GuiEventMgr::onAccept(boost::shared_ptr<strm::socket> sock, const boost::system::error_code & error) {
	if (!error) {
		startRead(sock, &GuiEventMgr::onReadFirstLine);
	}
	else {
		// TODO: error
	}
}

void Program::GuiEventMgr::startRead(boost::shared_ptr<strm::socket> sock, void(GuiEventMgr::*handler)(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data)) {
	boost::shared_ptr< std::vector<uint8_t> > data(new std::vector<uint8_t>(BUF_SIZE));
	sock->async_read_some(boost::asio::buffer(*data), boost::bind(handler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, data));
}

void Program::GuiEventMgr::onReadFirstLine(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data) {
	// TODO
	bool handleError = false;
	for (size_t i = 0; i < bytes_transferred; i++) {
		uint8_t c = (*data)[i];
		readBuf_ += c;
		if (c == '\n') {
			command_ = readBuf_.substr(0, readBuf_.size());
			audioStream_ = (command_ == "audio_stream\n");
			readBuf_.clear();
			data->erase(data->begin(), data->begin() + i + 1);
			onReadTheRest(error, bytes_transferred - (i + 1), sock, data); // Hacky
			return;
		}
		else if (readBuf_.size() >= GE_LINE_LEN_MAX) {
			handleError = true;
		}
	}
	if (error || handleError) {
		// error.
		// Close the current connection and start listening for a new one.
		sock->close();
		sock.reset();
		startAccept();
	}
}

void Program::GuiEventMgr::onReadTheRest(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > data) {
	// TODO
	if (audioStream_) {
		program_.onGuiAudio(*data);
		if (!error) {
			startRead(sock, &GuiEventMgr::onReadTheRest);
		}
		else {
			sock->close();
			sock.reset();
			startAccept();
		}
	}
	else {
		bool handleError = false;
		bool finishRead = false;
		std::string token;
		for (size_t i = 0; i < bytes_transferred; i++) {
			uint8_t c = (*data)[i];
			command_ += c;
			if (c == '\n') {
				finishRead = true;
				break;
			}
			else if (readBuf_.size() >= GL_LINE_LEN_MAX) {
				handleError = true;
				break;
			}
		}
		if (finishRead) {
			program_.onGuiCommand(command_);
			command_.clear();
		}
		if (finishRead || error || handleError) {
			// error.
			// Close the current connection and start listening for a new one.
			sock->close();
			sock.reset();
			startAccept();
		}
		else {
			startRead(sock, &GuiEventMgr::onReadTheRest);
		}
	}
}

int main(int argc, char const * const * argv) {
	(Program(Program::Config::fromArgv(argc, argv)))();
}
