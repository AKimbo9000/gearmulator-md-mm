#pragma once

// Touch drag curve for rotary controls: finger travel in screen points becomes encoder detents (or a continuous
// value) with the feel of a heavy, detented knob. Header-only and free of JUCE and RmlUi so it can be unit tested
// (test/rmlTouchDrag_test.cpp).
//
// - A dead zone: nothing turns until the finger has moved slopPoints from where it landed, so a tap or a rolling
//   press never turns the knob. Counting starts at the edge of the dead zone, so the knob does not jump.
// - Axis lock: the gesture keeps the axis it left the dead zone on, with a bias towards vertical. Up and right
//   increase. Movement on the other axis is ignored, so a diagonal drag is not faster than a straight one.
// - Gain from a smoothed finger speed: slowPointsPerStep when slow, rising smoothly (smoothstep in log speed) to
//   fastPointsPerStep when fast. The speed estimate rises over riseSeconds and falls over fallSeconds, so the knob
//   spins up like a heavy wheel and slows down quickly to land on a value.
// - A soft cap on detents per second, applied to the instantaneous speed too, so a flick never floods the panel.
// - Detents are rounded to nearest with hysteresis: a finger resting on a detent boundary does not make the value
//   flicker.
// - The knob stops when the finger stops or lifts: no momentum.

#include <algorithm>
#include <cmath>

namespace juceRmlUi::touchDrag
{
	struct Curve
	{
		double slowPointsPerStep = 4.0;		// at or below slowSpeed
		double fastPointsPerStep = 1.25;	// at or above fastSpeed, before the rate cap
		double slowSpeed = 40.0;			// pt/s
		double fastSpeed = 400.0;			// pt/s
		double maxStepsPerSecond = 240.0;	// soft cap, 0 = none
		double slopPoints = 6.0;			// dead zone radius
		double verticalBias = 0.75;			// vertical if |dy| >= verticalBias * |dx| when leaving the dead zone
		double riseSeconds = 0.060;			// speed estimate time constant while speeding up
		double fallSeconds = 0.030;			// ... while slowing down
		double hysteresisSteps = 0.25;		// extra travel needed to step back after a reversal
		int burstCap = 8;					// most steps returned by one move()
		double teleportPoints = 80.0;		// a larger jump in one event re-anchors without turning

		// Scales the travel per step at both ends: 2 makes the knob twice as fast. The rate cap stays.
		Curve scaled(const double _speedFactor) const
		{
			Curve c = *this;
			if(_speedFactor > 0.0)
			{
				c.slowPointsPerStep /= _speedFactor;
				c.fastPointsPerStep /= _speedFactor;
			}
			return c;
		}
	};

	inline double smoothstep(double _x)
	{
		_x = std::clamp(_x, 0.0, 1.0);
		return _x * _x * (3.0 - 2.0 * _x);
	}

	// Steps per point at a smoothed finger speed (pt/s), before the rate cap.
	inline double baseStepsPerPoint(const Curve& _curve, const double _speed)
	{
		const double slow = 1.0 / _curve.slowPointsPerStep;
		const double fast = 1.0 / _curve.fastPointsPerStep;
		if(_speed <= _curve.slowSpeed || _curve.fastSpeed <= _curve.slowSpeed)
			return slow;
		const double x = std::log(_speed / _curve.slowSpeed) / std::log(_curve.fastSpeed / _curve.slowSpeed);
		return slow + (fast - slow) * smoothstep(x);
	}

	// Steps per point including the rate cap. The cap uses the larger of the smoothed and the instantaneous speed,
	// so it holds while the smoothed speed still lags behind a sudden flick: steps per event <= cap * dt.
	inline double stepsPerPoint(const Curve& _curve, const double _smoothedSpeed, const double _instantSpeed)
	{
		const double g = baseStepsPerPoint(_curve, _smoothedSpeed);
		if(_curve.maxStepsPerSecond <= 0.0)
			return g;
		const double r = g * std::max(_smoothedSpeed, _instantSpeed) / _curve.maxStepsPerSecond;
		const double r2 = r * r;
		return g / std::sqrt(std::sqrt(1.0 + r2 * r2));
	}

	class Tracker
	{
	public:
		explicit Tracker(const Curve& _curve = {}) : m_curve(_curve) {}

		void setCurve(const Curve& _curve) { m_curve = _curve; }
		const Curve& getCurve() const { return m_curve; }

		// _x, _y in screen points (y down), _t in seconds
		void begin(const double _x, const double _y, const double _t)
		{
			m_active = true;
			m_engaged = false;
			m_axisX = false;
			m_x0 = m_lastX = _x;
			m_y0 = m_lastY = _y;
			m_lastT = _t;
			m_speed = 0.0;
			m_gain = 0.0;
			m_pos = 0.0;
			m_steps = 0;
			m_lastDir = 0;
		}

		// Whole detents to turn for this move (positive = up/right), at most burstCap either way.
		int move(const double _x, const double _y, const double _t)
		{
			double d = 0.0;
			if(!advance(_x, _y, _t, d))
				return 0;
			m_pos += d * m_gain;
			return quantize();
		}

		// Continuous variant: the change in steps for this move, unquantized (for a pot such as master volume).
		double moveContinuous(const double _x, const double _y, const double _t)
		{
			double d = 0.0;
			if(!advance(_x, _y, _t, d))
				return 0.0;
			return d * m_gain;
		}

		void end() { m_active = false; }

		bool active() const { return m_active; }
		bool engaged() const { return m_engaged; }
		bool axisIsHorizontal() const { return m_axisX; }
		double speed() const { return m_speed; }		// smoothed, pt/s
		double gain() const { return m_gain; }			// steps per point used by the last move
		int steps() const { return m_steps; }			// net steps since begin()

	private:
		void updateSpeed(const double _distance, const double _dt)
		{
			const double raw = _distance / std::max(_dt, 0.002);
			const double tau = raw > m_speed ? m_curve.riseSeconds : m_curve.fallSeconds;
			const double alpha = tau > 0.0 ? 1.0 - std::exp(-_dt / tau) : 1.0;
			m_speed += alpha * (raw - m_speed);
		}

		// Updates the speed and the axis state; returns false when this move turns nothing. _d is the signed travel
		// along the locked axis that counts for this move.
		bool advance(const double _x, const double _y, const double _t, double& _d)
		{
			if(!m_active)
				return false;
			double dt = _t - m_lastT;
			if(!(dt > 0.0))
				dt = 0.001;
			const double dx = _x - m_lastX;
			const double dy = _y - m_lastY;
			m_lastX = _x;
			m_lastY = _y;
			m_lastT = _t;

			if(std::hypot(dx, dy) > m_curve.teleportPoints)
			{
				// the position jumped (a lost event, a second finger): continue from here without turning
				if(!m_engaged)
				{
					m_x0 = _x;
					m_y0 = _y;
				}
				return false;
			}

			if(!m_engaged)
			{
				updateSpeed(std::hypot(dx, dy), dt);
				const double ox = _x - m_x0;
				const double oy = _y - m_y0;
				const double dist = std::hypot(ox, oy);
				if(dist < m_curve.slopPoints)
					return false;
				m_engaged = true;
				m_axisX = std::abs(oy) < m_curve.verticalBias * std::abs(ox);
				// count only the part beyond the dead zone, along the locked axis
				const double along = m_axisX ? ox : -oy;
				_d = (dist - m_curve.slopPoints) * along / dist;
			}
			else
			{
				_d = m_axisX ? dx : -dy;
				updateSpeed(std::abs(_d), dt);
			}

			m_gain = stepsPerPoint(m_curve, m_speed, std::abs(_d) / std::max(dt, 0.004));
			return true;
		}

		int quantize()
		{
			const int before = m_steps;
			const double h = m_curve.hysteresisSteps;
			for(;;)
			{
				const double up = m_steps + 0.5 + (m_lastDir < 0 ? h : 0.0);
				const double down = m_steps - 0.5 - (m_lastDir > 0 ? h : 0.0);
				if(m_pos >= up)
				{
					++m_steps;
					m_lastDir = 1;
				}
				else if(m_pos <= down)
				{
					--m_steps;
					m_lastDir = -1;
				}
				else
				{
					break;
				}
			}
			int k = m_steps - before;
			const int cap = std::max(1, m_curve.burstCap);
			if(k > cap || k < -cap)
			{
				// only after a stall: drop the excess rather than queueing it
				k = std::clamp(k, -cap, cap);
				m_steps = before + k;
				m_pos = m_steps;
			}
			return k;
		}

		Curve m_curve;
		bool m_active = false;
		bool m_engaged = false;
		bool m_axisX = false;
		double m_x0 = 0.0;
		double m_y0 = 0.0;
		double m_lastX = 0.0;
		double m_lastY = 0.0;
		double m_lastT = 0.0;
		double m_speed = 0.0;
		double m_gain = 0.0;
		double m_pos = 0.0;
		int m_steps = 0;
		int m_lastDir = 0;
	};
}
