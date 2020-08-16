// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_LOG_H_
#define ADMMPD_LOG_H_

#include "admmpd_timer.h"
#include <unordered_map>

namespace admmpd {

class Logger {
protected:
	std::unordered_map<int,double> elapsed_ms;
	std::unordered_map<int,MicroTimer> curr_timer;
	int m_log_level;
public:
    Logger(int level) : m_log_level(level) {}
	void reset();
	void start_state(int state);
	double stop_state(int state); // ret time elapsed
	std::string state_string(int state);
	std::string to_string();
};

} // namespace admmpd

#endif // ADMMPD_LOG_H_
