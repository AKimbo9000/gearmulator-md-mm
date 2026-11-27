// Unit test of the touch drag curve (rmlTouchDrag.h). Depends on nothing but the header, so it also builds on its
// own: clang++ -std=c++17 -I source/framework/juce source/framework/juce/juceRmlUi/test/rmlTouchDrag_test.cpp
#include "juceRmlUi/rmlTouchDrag.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
	using juceRmlUi::touchDrag::Curve;
	using juceRmlUi::touchDrag::Tracker;
	using juceRmlUi::touchDrag::stepsPerPoint;

	int g_failures = 0;

	void check(const bool _condition, const char* _what, const double _value = 0.0)
	{
		std::printf("%s %s (%g)\n", _condition ? "PASS" : "FAIL", _what, _value);
		if(!_condition)
			++g_failures;
	}

	struct PathResult
	{
		int total = 0;
		int maxAbsPerEvent = 0;
		double maxGainJump = 0.0;
	};

	// Drives a straight path from (x0,y0) to (x1,y1) at _hz for _seconds, continuing a gesture already begun.
	PathResult drive(Tracker& _t, double& _time, const double _x0, const double _y0, const double _x1, const double _y1,
		const double _seconds, const double _hz, const bool _roundMs = false)
	{
		PathResult r;
		const int n = static_cast<int>(std::lround(_seconds * _hz));
		double lastGain = -1.0;
		const double start = _time;
		for(int i = 1; i <= n; ++i)
		{
			const double f = static_cast<double>(i) / n;
			_time = start + i / _hz;
			const double t = _roundMs ? std::round(_time * 1000.0) / 1000.0 : _time;
			const int k = _t.move(_x0 + (_x1 - _x0) * f, _y0 + (_y1 - _y0) * f, t);
			r.total += k;
			r.maxAbsPerEvent = std::max(r.maxAbsPerEvent, std::abs(k));
			if(_t.engaged() && lastGain >= 0.0)
				r.maxGainJump = std::max(r.maxGainJump, std::abs(_t.gain() - lastGain));
			if(_t.engaged())
				lastGain = _t.gain();
		}
		return r;
	}
}

int main()
{
	const Curve c;

	// gain table: steps per point at steady speeds (instantaneous == smoothed)
	check(std::abs(stepsPerPoint(c, 20, 20) - 0.25) < 0.003, "gain at 20 pt/s = 0.25 (4 pt per detent)", stepsPerPoint(c, 20, 20));
	check(std::abs(stepsPerPoint(c, 100, 100) - 0.442) < 0.003, "gain at 100 pt/s = 0.442", stepsPerPoint(c, 100, 100));
	check(std::abs(stepsPerPoint(c, 200, 200) - 0.664) < 0.003, "gain at 200 pt/s = 0.664", stepsPerPoint(c, 200, 200));
	check(std::abs(stepsPerPoint(c, 1000, 1000) * 1000 - 239.5) < 1.0, "rate at 1000 pt/s capped near 240 detents/s", stepsPerPoint(c, 1000, 1000) * 1000);
	check(stepsPerPoint(c, 30, 30) <= stepsPerPoint(c, 60, 60) && stepsPerPoint(c, 60, 60) <= stepsPerPoint(c, 300, 300), "gain rises with speed");

	// (a) a tap with a rolling finger (within 4 pt) never turns
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 100, time);
		int total = 0;
		const double roll[][2] = {{102, 97}, {103, 98}, {101, 99}, {99, 101}, {100, 103}, {98, 100}};
		for(const auto& p : roll)
		{
			time += 0.025;
			total += t.move(p[0], p[1], time);
		}
		check(total == 0 && !t.engaged(), "tap roll within 4 pt: 0 detents, not engaged", total);
	}

	// (b) 62 pt straight up at 20 pt/s, 120 Hz: (62 - 6 slop) * 0.25 = 14 detents
	int vertical = 0;
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 200, time);
		vertical = drive(t, time, 100, 200, 100, 138, 3.1, 120).total;
		check(vertical == 14, "62 pt up at 20 pt/s: 14 detents", vertical);
	}

	// (c) the same at 60 Hz and with timestamps rounded to whole milliseconds (JUCE event times)
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 200, time);
		const int total = drive(t, time, 100, 200, 100, 138, 3.1, 60, true).total;
		check(std::abs(total - 14) <= 1, "same path at 60 Hz with ms timestamps: 14 +-1", total);
	}

	// (d) a 45 degree diagonal with the same vertical travel is not faster
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 200, time);
		const int total = drive(t, time, 100, 200, 138, 138, 3.1, 120).total;
		check(std::abs(total - vertical) <= 1 && !t.axisIsHorizontal(), "diagonal counts like vertical (+-1), axis vertical", total);
	}

	// horizontal drags work too: right increases, and vertical wobble after the lock is ignored
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 100, time);
		const int right = drive(t, time, 100, 100, 162, 100, 3.1, 120).total;
		const int wobble = drive(t, time, 162, 100, 162, 130, 1.5, 120).total;
		check(right == 14 && t.axisIsHorizontal() && wobble == 0, "horizontal: 14 detents to the right, vertical wobble ignored", right);
	}

	// (e) a flick at 1000 pt/s for 100 ms stays under the rate cap: <= 2 per 120 Hz event, <= 4 per 60 Hz event
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 300, time);
		const auto r = drive(t, time, 100, 300, 100, 200, 0.1, 120);
		check(r.maxAbsPerEvent <= 2, "flick at 120 Hz: at most 2 detents per event", r.maxAbsPerEvent);
		check(r.total >= 15 && r.total <= 25, "flick: 15..25 detents in 100 ms (cap 240/s)", r.total);
		Tracker t60(c);
		time = 0;
		t60.begin(100, 300, time);
		const auto r60 = drive(t60, time, 100, 300, 100, 200, 0.1, 60);
		check(r60.maxAbsPerEvent <= 4, "flick at 60 Hz: at most 4 detents per event", r60.maxAbsPerEvent);
	}

	// burst cap after a stall: one event 100 ms late covering 40 pt
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 300, time);
		drive(t, time, 100, 300, 100, 280, 0.2, 120);
		time += 0.1;
		const int k = t.move(100, 240, time);
		check(k >= 1 && k <= 8, "stalled event: 1..8 detents", k);
	}

	// (g) the gain changes smoothly while the finger accelerates hard (4000 pt/s^2, 0 to 333 pt/s in 83 ms): no step
	// larger than 10 % of the fastest gain (0.8) between two 120 Hz events. The largest step (0.065) is the intended
	// spin-up in the middle of the log-speed smoothstep.
	{
		Tracker t(c);
		t.begin(100, 400, 0);
		double y = 400, v = 0, time = 0, lastGain = -1, maxJump = 0;
		for(int i = 0; i < 40; ++i)
		{
			time += 1.0 / 120;
			v += 4000.0 / 120;
			y -= v / 120;
			t.move(100, y, time);
			if(t.engaged() && lastGain >= 0)
				maxJump = std::max(maxJump, std::abs(t.gain() - lastGain));
			if(t.engaged())
				lastGain = t.gain();
		}
		check(maxJump < 0.08, "gain step per event < 0.08 while accelerating hard", maxJump);
	}

	// (h) a finger resting on a detent boundary (dither +-0.6 pt for 2 s) changes at most one detent
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 200, time);
		drive(t, time, 100, 200, 100, 186, 0.7, 120);		// 14 pt up: 8 pt past the dead zone = detent 2 boundary
		int moved = 0;
		for(int i = 0; i < 240; ++i)
		{
			time += 1.0 / 120;
			moved += std::abs(t.move(100, 186 + ((i & 1) ? 0.6 : -0.6), time));
		}
		check(moved <= 1, "dither on a boundary: at most 1 detent", moved);
	}

	// (i) slow up then slow down by the same distance returns to the same value
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 200, time);
		drive(t, time, 100, 200, 100, 194, 0.3, 120);		// engage
		const int up = drive(t, time, 100, 194, 100, 174, 1.0, 120).total;
		const int down = drive(t, time, 100, 174, 100, 194, 1.0, 120).total;
		check(up == 5 && up + down == 0, "20 pt up then 20 pt down at 20 pt/s: +5 then back to 0", up + down);
	}

	// (j) a jump of 200 pt in one event turns nothing
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 300, time);
		drive(t, time, 100, 300, 100, 290, 0.5, 120);
		time += 1.0 / 120;
		const int k = t.move(100, 90, time);
		check(k == 0, "200 pt teleport: 0 detents", k);
	}

	// (k) after a fast drag and a 500 ms pause, a slow move is back at the slow gain at once
	{
		Tracker t(c);
		double time = 0;
		t.begin(100, 400, time);
		drive(t, time, 100, 400, 100, 200, 0.4, 120);		// 500 pt/s
		time += 0.5;
		t.move(100, 199.8, time);
		time += 1.0 / 120;
		t.move(100, 199.6, time);
		check(std::abs(t.gain() - 0.25) < 0.01, "slow move after a pause: gain back to 0.25", t.gain());
	}

	// speed setting: twice as fast halves the travel per detent
	{
		Tracker t(c.scaled(2.0));
		double time = 0;
		t.begin(100, 200, time);
		const int total = drive(t, time, 100, 200, 100, 138, 3.1, 120).total;
		check(total == 28, "speed x2: 28 detents for the same 62 pt", total);
	}

	// continuous mode (master volume): 300 pt per full range at slow speed
	{
		Curve pot = c;
		pot.slowPointsPerStep = 3.0;		// one step = 1 % of the range
		pot.fastPointsPerStep = 1.5;
		pot.maxStepsPerSecond = 0;
		Tracker t(pot);
		t.begin(100, 300, 0);
		double time = 0, sum = 0;
		for(int i = 1; i <= 372; ++i)
		{
			time = i / 120.0;
			sum += t.moveContinuous(100, 300 - 66.0 * i / 372, time);		// 66 pt at ~21 pt/s
		}
		check(std::abs(sum - 20.0) < 0.01, "continuous: (66 - 6) pt / 3 = 20 % of the range", sum);
	}

	std::printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
