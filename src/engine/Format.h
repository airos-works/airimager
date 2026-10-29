// Format.h - sizes, speeds and durations as the window shows them, and the
// speed and time-left estimate of a running write.
#ifndef AIRIMAGER_FORMAT_H
#define AIRIMAGER_FORMAT_H

#include <cstdint>
#include <deque>
#include <string>

namespace imager {

// 1023 bytes, 1.5 KiB, 31.9 GiB (binary units, as Haiku shows sizes).
std::string FormatBytes(uint64_t bytes);
// 0 B/s, 512 KiB/s, 38.2 MiB/s.
std::string FormatRate(double bytesPerSecond);
// 45 s, 2 min 10 s, 1 h 5 min.
std::string FormatDuration(double seconds);

// The speed over the last few seconds, and the time left at that speed.
class RateMeter {
public:
	explicit			RateMeter(double window = 4.0);

	void				Reset();
	// A sample: bytes done after seconds (both only ever growing).
	void				Add(double seconds, uint64_t bytes);

	// Bytes per second, 0 until there is a second of samples.
	double				Rate() const;
	// Seconds until total, or a negative value when not yet known.
	double				SecondsLeft(uint64_t total) const;

private:
	struct Sample {
		double		seconds;
		uint64_t	bytes;
	};

	double				fWindow;
	std::deque<Sample>	fSamples;
	double				fFirstSeconds;
};

}  // namespace imager

#endif
