// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#ifndef ADMMPD_TIMER_H_
#define ADMMPD_TIMER_H_

#include <chrono>

namespace admmpd {

// I call it MicroTimer to avoid name clashes. Originally from
// https://github.com/mattoverby/mclscene/blob/master/include/MCL/MicroTimer.hpp
class MicroTimer {
	//typedef std::chrono::high_resolution_clock C;
	typedef std::chrono::steady_clock C;
	typedef double T;
public:

	MicroTimer() : start_time( C::now() ){}

	void reset() { start_time = C::now(); }

	T elapsed_s() const { // seconds
		curr_time = C::now();
		std::chrono::duration<T> durr = curr_time-start_time;
		return durr.count();
	}

	T elapsed_ms() const { // milliseconds
		curr_time = C::now();
		std::chrono::duration<T, std::milli> durr = curr_time-start_time;
		return durr.count();
	}

	T elapsed_us() const { // microseconds
		curr_time = C::now();
		std::chrono::duration<T, std::micro> durr = curr_time-start_time;
		return durr.count();
	}

private:
	std::chrono::time_point<C> start_time;
	mutable std::chrono::time_point<C> curr_time;

}; // end class MicroTimer

} // namespace admmpd

#endif // ADMMPD_TIMER_H_