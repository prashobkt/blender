// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_types.h"
#include "admmpd_log.h"

namespace admmpd {

void Logger::reset()
{
	curr_timer.clear();
	elapsed_ms.clear();
}

void Logger::start_state(int state)
{
	if (m_log_level < LOGLEVEL_HIGH)
		return;

	if (m_log_level >= LOGLEVEL_DEBUG)
		printf("Starting state %s\n",state_string(state).c_str());

	if (curr_timer.count(state)==0)
	{
		elapsed_ms[state] = 0;
		curr_timer[state] = MicroTimer();
		return;
	}
	curr_timer[state].reset();
}

// Returns time elapsed
double Logger::stop_state(int state)
{
	if (m_log_level < LOGLEVEL_HIGH)
		return 0;

	if (m_log_level >= LOGLEVEL_DEBUG)
		printf("Stopping state %s\n",state_string(state).c_str());

	if (curr_timer.count(state)==0)
	{
		elapsed_ms[state] = 0;
		curr_timer[state] = MicroTimer();
		return 0;
	}
	double dt = curr_timer[state].elapsed_ms();
	elapsed_ms[state] += dt;
	return dt;
}

std::string Logger::state_string(int state)
{
	std::string str = "unknown";
	switch (state)
	{
		default: break;
		case SOLVERSTATE_INIT: str="init"; break;
		case SOLVERSTATE_SOLVE: str="solve"; break;
		case SOLVERSTATE_INIT_SOLVE: str="init_solve"; break;
		case SOLVERSTATE_LOCAL_STEP: str="local_step"; break;
		case SOLVERSTATE_GLOBAL_STEP: str="global_step"; break;
		case SOLVERSTATE_COLLISION_UPDATE: str="collision_update"; break;
		case SOLVERSTATE_TEST_CONVERGED: str="test_converged"; break;
	}
	return str;
}

std::string Logger::to_string()
{
	// Sort by largest time
	auto sort_ms = [](const std::pair<int, double> &a, const std::pair<int, double> &b)
		{ return (a.second > b.second); };
	std::vector<std::pair<double, int> > ms(elapsed_ms.begin(), elapsed_ms.end());
	std::sort(ms.begin(), ms.end(), sort_ms);

	// Concat string
	std::stringstream ss;
	int n_timers = ms.size();
	for (int i=0; i<n_timers; ++i)
		ss << state_string(ms[i].first) << ": " << ms[i].second << "ms" << std::endl;

	return ss.str();
}

} // namespace admmpd
