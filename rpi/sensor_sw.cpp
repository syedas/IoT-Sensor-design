//#include <cstdlib>
#include <iostream>
#include <sstream>
#include <queue>

#include <boost/asio.hpp>
#include <boost/asio/high_resolution_timer.hpp>
#include <boost/bind.hpp>
//#include <boost/date_time/posix_time/posix_time.hpp>
//#include <boost/iostreams/device/array.hpp>
//#include <boost/iostreams/stream.hpp>

#include <portaudio.h>

// FreeBSD's GPIO library
#include <libgpio.h>

#include "http.hpp"

// Functionality:
// - GPIO
//   - polling sensors
//   - LED output
// - HTTP
//   - Longpoll incoming events
//     - Also receive audio, in small chunks
//   - Send sensor events
// - Audio
//   - Output the data received from HTTP

template<typename To, typename From> To & typepun(From & a) {
	return *static_cast<To*>(static_cast<void*>(&a));
}

template<typename To, typename From> const To & typepun(const From & a) {
	return *static_cast<const To*>(static_cast<const void*>(&a));
}

enum {
	BUF_SIZE = 1024,
	GPIO_FIRE_ALARM_PIN = 2,
	GPIO_MOTION_PIN = 3,
	GPIO_LED_PIN = 4,
	SMOKE_STOP_TIME = 1000,
	MOTION_DELAY_TIME = 200,
	AUDIO_SAMPLE_RATE = 8000,
	AUDIO_BUFFER_SIZE = 1024,
	AUDIO_QUEUE_SIZE = 40000
};

/*namespace {
	const char
		*LONGPOLL_ADDR = "192.168.42.1",
		*LONGPOLL_PORT = "http",
		*LONGPOLL_PATH = "sensor_longpoll.php";
}*/

typedef boost::asio::local::stream_protocol strm;

class Program {
public:
	struct Config {
		std::string
			longpollAddr_,
			eventAddr_;
		static Config fromArgv(int argc, char const * const * argv);
	};
	
	Program(const Config & config);
	~Program();
	void operator()();
private:
	typedef int8_t AudioSample;
	typedef std::vector<AudioSample> AudioVec;
	
	enum Event {
		SMOKE_ON,
		SMOKE_OFF,
		MOTION
	};
	
	class Gpio {
	public:
		Gpio(Program & program);
		~Gpio();
		void led(unsigned int onTime, unsigned int offTime);
		void smokeSleep(unsigned int time);
	private:
		void sampleGpio();
		
		Program & program_;
		gpio_handle_t gpio_;
		boost::asio::high_resolution_timer gpioTimer_;
		bool smokeState_, ledState_;
		unsigned int onTime_, offTime_, smokeSleep_, smokeStopCounter_, ledCounter_, motionDelayCounter_;
	};
	
	class AudioOut {
	public:
		AudioOut(boost::asio::io_service & io);
		~AudioOut();
		void pushAudio(const AudioVec & av);
		void sirenState(bool state);
		void sirenEnable(bool enable);
	private:
		void timeStep();
		
		bool paInitialized_, sirenState_, sirenEnable_;
		unsigned int sirenCounter_;
		boost::asio::io_service & io_;
		boost::asio::high_resolution_timer stepTimer_;
		PaStream * paStream_;
		std::queue<AudioSample> audioQueue_;
	};
	
	class Longpoll {
	public:
		Longpoll(Program & program);
		~Longpoll();
	private:
		enum MsgCode {
			LED = 1,
			SIREN_CTRL,
			SMOKE_SLEEP,
			AUDIO_STREAM,
			TOKEN
		};
		
		void startLongpoll(const std::string & token);
		void onConnect(const boost::system::error_code & error, boost::shared_ptr<strm::socket> sock, const std::string & token);
		void onWrite(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr<std::string> msgOut);
		void startRead(boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > allDataIn);
		void onRead(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > allDataIn, boost::shared_ptr< std::vector<uint8_t> > dataIn);
		void onEof(boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > allDataIn);
		void startTimer(const std::string & token);
		void onTimer(const std::string & token);
		
		Program & program_;
		boost::asio::high_resolution_timer timer_;
	};
	
	class EventOut {
	public:
		EventOut(Program & program);
		~EventOut();
		void pushEvent(Event event);
	private:
		void onConnect(const boost::system::error_code & error, boost::shared_ptr<strm::socket> sock, Event event);
		void onWrite(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr<std::string> msgOut);
	
		Program & program_;
	};
	
	void onSignal(const boost::system::error_code & error, int signal_number);
	
	boost::asio::io_service io_;
	boost::asio::signal_set signals_;
	const Config config_;
	Gpio gpio_;
	AudioOut audioOut_;
	Longpoll longpoll_;
	EventOut eventOut_;
};

Program::Program(const Config & config)
	:	signals_(io_, SIGINT, SIGTERM),
		config_(config),
		gpio_(*this),
		audioOut_(io_),
		longpoll_(*this),
		eventOut_(*this) {
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
	if (argc != 3) {
		throw std::runtime_error("argc != 3");
	}
	Config config;
	config.longpollAddr_ = argv[1];
	config.eventAddr_    = argv[2];
	return config;
}

Program::Gpio::Gpio(Program & program)
	:	program_(program),
		gpioTimer_(program.io_),
		smokeState_(false),
		ledState_(false),
		onTime_(0),
		offTime_(1000),
		smokeSleep_(0),
		smokeStopCounter_(0),
		ledCounter_(0),
		motionDelayCounter_(0) {
	gpio_ = gpio_open(0);
	if (gpio_ == GPIO_INVALID_HANDLE) {
//		throw std::runtime_error("gpio_open fail");
	}
	else {
		gpio_pin_input (gpio_, GPIO_FIRE_ALARM_PIN);
		gpio_pin_input (gpio_, GPIO_MOTION_PIN    );
		gpio_pin_output(gpio_, GPIO_LED_PIN       );
		sampleGpio();
	}
}

Program::Gpio::~Gpio() {
	gpio_close(gpio_);
}

void Program::Gpio::led(unsigned int onTime, unsigned int offTime) {
	// TODO
	onTime_ = onTime;
	offTime_ = offTime;
}

void Program::Gpio::smokeSleep(unsigned int time) {
	// TODO
	smokeSleep_ = time;
}

void Program::Gpio::sampleGpio() {
	//gpio_value_t val;
	int val;
	
	val = gpio_pin_get(gpio_, GPIO_FIRE_ALARM_PIN);
	bool nowSmoke = (val != GPIO_PIN_HIGH);
	
	val = gpio_pin_get(gpio_, GPIO_MOTION_PIN);
	bool nowMotion = (val != GPIO_PIN_LOW);
	
	if (smokeSleep_ > 0) {
		smokeSleep_ -= std::min<unsigned int>(50, smokeSleep_);
	}
	if (smokeStopCounter_ > 0) {
		smokeStopCounter_ -= std::min<unsigned int>(50, smokeStopCounter_);
	}
	
	if (nowSmoke) {
		smokeStopCounter_ = SMOKE_STOP_TIME;
	}
	
	if (smokeState_ == true) {
		if (smokeSleep_ > 0 || smokeStopCounter_ == 0) {
			smokeState_ = false;
			program_.eventOut_.pushEvent(SMOKE_OFF);
		}
	}
	else {
		if (nowSmoke && smokeSleep_ == 0) {
			smokeState_ = true;
			program_.eventOut_.pushEvent(SMOKE_ON);
		}
	}
	
	if (motionDelayCounter_ > 0) {
		motionDelayCounter_ -= std::min<unsigned int>(50, motionDelayCounter_);
	}
	
	if (nowMotion && motionDelayCounter_ == 0) {
		program_.eventOut_.pushEvent(MOTION);
		motionDelayCounter_ = MOTION_DELAY_TIME;
	}
	
	ledCounter_++;
	
	if (ledState_ == true) {
		if (ledCounter_ >= onTime_) {
			ledState_ = !ledState_;
			ledCounter_ = 0;
		}
	}
	else {
		if (ledCounter_ >= offTime_) {
			ledState_ = !ledState_;
			ledCounter_ = 0;
		}
	}
	
	gpio_pin_set(gpio_, GPIO_LED_PIN, ledState_ ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
	
	gpioTimer_.expires_from_now(boost::chrono::milliseconds(50));
	gpioTimer_.async_wait(boost::bind(&Gpio::sampleGpio, this));
}

Program::AudioOut::AudioOut(boost::asio::io_service & io)
	:	sirenState_(false),
		sirenEnable_(true),
		sirenCounter_(0),
		io_(io),
		stepTimer_(io_),
		paStream_() {
	if (paInitialized_ = (Pa_Initialize() == paNoError)) {
		// http://portaudio.com/docs/v19-doxydocs/blocking_read_write.html
		PaStreamParameters outParams = { };
		outParams.device = Pa_GetDefaultOutputDevice();
		outParams.channelCount = 1;
		outParams.sampleFormat = paInt8;
		outParams.suggestedLatency = Pa_GetDeviceInfo(outParams.device)->defaultHighOutputLatency;
		outParams.hostApiSpecificStreamInfo = NULL;
		
		if (Pa_OpenStream(&paStream_, NULL, &outParams, AUDIO_SAMPLE_RATE, AUDIO_BUFFER_SIZE, paClipOff, NULL, NULL) != paNoError) {
			Pa_Terminate() == paNoError || (std::cout << "Pa_Terminate error" << std::endl);
			paInitialized_ = false;
		}
		else if (Pa_StartStream(paStream_) != paNoError) {
			Pa_CloseStream(paStream_) == paNoError || (std::cout << "Pa_CloseStream error" << std::endl);
			Pa_Terminate() == paNoError || (std::cout << "Pa_Terminate error" << std::endl);
			paInitialized_ = false;
		}
		else {
			// All OK
			std::cout << "Audio init success" << std::endl;
			timeStep();
		}
	}
}

Program::AudioOut::~AudioOut() {
	if (paInitialized_) {
		Pa_StopStream(paStream_) == paNoError || (std::cout << "Pa_StopStream error" << std::endl);
		Pa_CloseStream(paStream_) == paNoError || (std::cout << "Pa_CloseStream error" << std::endl);
		Pa_Terminate() == paNoError || (std::cout << "Pa_Terminate error" << std::endl);
	}
}

void Program::AudioOut::pushAudio(const Program::AudioVec & av) {
	/*signed long num = Pa_GetStreamWriteAvailable(paStream_);
	if (num > 0) {
		if (av.size() < num) {
			num = av.size();
		}
		Pa_WriteStream(paStream_, reinterpret_cast<const void *>(av.data()), num);
	}*/
	size_t i = 0;
	while (audioQueue_.size() < AUDIO_QUEUE_SIZE && i < av.size()) {
		audioQueue_.push(av[i++]);
	}
}

void Program::AudioOut::sirenState(bool state) {
	if (state != sirenState_) {
		sirenCounter_ = 0;
	}
	sirenState_ = state;
}

void Program::AudioOut::sirenEnable(bool enable) {
	sirenEnable_ = enable;
}

void Program::AudioOut::timeStep() {
	signed long num = Pa_GetStreamWriteAvailable(paStream_);
	if (num > 0) {
		AudioVec av(num);
		for (size_t i = 0; i < av.size(); i++) {
			int16_t sample = 0;
			if (!audioQueue_.empty()) {
				sample += audioQueue_.front();
				audioQueue_.pop();
			}
			if (sirenEnable_ && sirenState_) {
				if (((sirenCounter_ >> 10) & 0x3) != 0x3) {
					sample += ((sirenCounter_ & 0x2) ? 127 : -128);
				}
				sirenCounter_++;
			}
			av[i] = AudioSample(std::min<int16_t>(127, std::max<int16_t>(-128, sample)));
		}
		Pa_WriteStream(paStream_, reinterpret_cast<const void *>(av.data()), num);
	}
	stepTimer_.expires_from_now(boost::chrono::milliseconds(50));
	stepTimer_.async_wait(boost::bind(&AudioOut::timeStep, this));
}

Program::Longpoll::Longpoll(Program & program)
	:	program_(program),
		timer_(program_.io_) {
	startLongpoll("");
}

Program::Longpoll::~Longpoll() {
}

void Program::Longpoll::startLongpoll(const std::string & token) {
	boost::shared_ptr<strm::socket> sock(new strm::socket(program_.io_));
	sock->async_connect(strm::endpoint(program_.config_.longpollAddr_), boost::bind(&Longpoll::onConnect, this, boost::asio::placeholders::error, sock, token));
}

void Program::Longpoll::onConnect(const boost::system::error_code & error, boost::shared_ptr<strm::socket> sock, const std::string & token) {
	if (!error) {
		boost::shared_ptr<std::string> msgOut(new std::string(token + "\n"));
		boost::asio::async_write(*sock, boost::asio::buffer(*msgOut), boost::bind(&Longpoll::onWrite, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, msgOut));
	}
	else {
		// TODO: error
	}
}

void Program::Longpoll::onWrite(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr<std::string> msgOut) {
	if (!error) {
		boost::shared_ptr< std::vector<uint8_t> > allDataIn(new std::vector<uint8_t>);
		startRead(sock, allDataIn);
	}
	else {
		// TODO: error
	}
}

void Program::Longpoll::startRead(boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > allDataIn) {
	boost::shared_ptr< std::vector<uint8_t> > dataIn(new std::vector<uint8_t>(BUF_SIZE));
	sock->async_read_some(boost::asio::buffer(*dataIn), boost::bind(&Longpoll::onRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, allDataIn, dataIn));
}

void Program::Longpoll::onRead(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > allDataIn, boost::shared_ptr< std::vector<uint8_t> > dataIn) {
	allDataIn->insert(allDataIn->begin(), dataIn->begin(), dataIn->begin() + bytes_transferred);
	if (!error) {
		startRead(sock, allDataIn);
	}
	else if (error == boost::asio::error::eof) {
		onEof(sock, allDataIn);
	}
	else {
		// TODO: error
	}
}

void Program::Longpoll::onEof(boost::shared_ptr<strm::socket> sock, boost::shared_ptr< std::vector<uint8_t> > allDataIn) {
	sock->close();
	sock.reset();
	
	// TODO: Process the data
	std::istringstream stream(std::string(allDataIn->begin(), allDataIn->end()));
	std::string token;
	
	uint8_t type, sizeBuf[2];
	
	while (stream.get(typepun<char>(type)).get(typepun<char>(sizeBuf[0])).get(typepun<char>(sizeBuf[1])).good()) {
		std::size_t size = (std::size_t(sizeBuf[0]) << 8) | std::size_t(sizeBuf[1]);
		std::vector<uint8_t> content(size);
		if (stream.get(typepun<char*>(content.data()), size).gcount() != size) {
			break;
		}
		else {
			std::istringstream stream2(std::string(content.begin(), content.end()));
			unsigned int u0 = 0, u1 = 0;
			switch (MsgCode(type)) {
			case LED:
				stream >> u0 >> u1;
				program_.gpio_.led(u0, u1);
				break;
			case SIREN_CTRL:
				stream >> u0;
				program_.audioOut_.sirenEnable(bool(u0));
				break;
			case SMOKE_SLEEP:
				stream >> u0;
				program_.gpio_.smokeSleep(u0);
				break;
			case AUDIO_STREAM:
				// TODO
				program_.audioOut_.pushAudio(typepun<AudioVec>(content)); // FIXME: dangerous typepun!
				break;
			case TOKEN:
				token.assign(content.begin(), content.end());
				break;
			}
		}
	}
	
	// Start new longpoll after a while
	startTimer(token);
}

void Program::Longpoll::startTimer(const std::string & token) {
	timer_.expires_from_now(boost::chrono::milliseconds(50));
	timer_.async_wait(boost::bind(&Longpoll::onTimer, this, token));
}

void Program::Longpoll::onTimer(const std::string & token) {
	startLongpoll(token);
}

Program::EventOut::EventOut(Program & program)
	:	program_(program) {
}

Program::EventOut::~EventOut() {
}

void Program::EventOut::pushEvent(Event event) {
	boost::shared_ptr<strm::socket> sock(new strm::socket(program_.io_));
	sock->async_connect(strm::endpoint(program_.config_.eventAddr_), boost::bind(&EventOut::onConnect, this, boost::asio::placeholders::error, sock, event));
}

void Program::EventOut::onConnect(const boost::system::error_code & error, boost::shared_ptr<strm::socket> sock, Event event) {
	if (!error) {
		boost::shared_ptr<std::string> msgOut(new std::string);
		switch (event) {
		case SMOKE_ON:  *msgOut = "smoke_on\n" ; break;
		case SMOKE_OFF: *msgOut = "smoke_off\n"; break;
		case MOTION:    *msgOut = "motion\n"   ; break;
		}
		boost::asio::async_write(*sock, boost::asio::buffer(*msgOut), boost::bind(&EventOut::onWrite, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, sock, msgOut));
	}
	else {
		// TODO: error
	}
}

void Program::EventOut::onWrite(const boost::system::error_code & error, std::size_t bytes_transferred, boost::shared_ptr<strm::socket> sock, boost::shared_ptr<std::string> msgOut) {
	if (!error) {
		sock->close();
		sock.reset();
	}
	else {
		// TODO: error
	}
}

int main(int argc, char const * const * argv) {
	Program(Program::Config::fromArgv(argc, argv))();
}
