// Format.cpp - see Format.h.
#include "engine/Format.h"

#include <cmath>
#include <cstdio>

namespace imager {

std::string
FormatBytes(uint64_t bytes)
{
	static const char* const kUnits[] = { "KiB", "MiB", "GiB", "TiB", "PiB" };
	char text[32];
	if (bytes < 1024) {
		snprintf(text, sizeof(text), "%u B", unsigned(bytes));
		return text;
	}
	double value = bytes / 1024.0;
	int unit = 0;
	// 1023.96 MiB would print as "1024.0 MiB"; go up a unit before that.
	while (value >= 1023.95 && unit < 4) {
		value /= 1024.0;
		unit++;
	}
	snprintf(text, sizeof(text), value < 100 ? "%.1f %s" : "%.0f %s", value,
		kUnits[unit]);
	return text;
}


std::string
FormatRate(double bytesPerSecond)
{
	if (!(bytesPerSecond > 0))
		return "0 B/s";
	return FormatBytes(uint64_t(bytesPerSecond + 0.5)) + "/s";
}


std::string
FormatDuration(double seconds)
{
	char text[32];
	long total = seconds < 0 ? 0 : long(std::lround(seconds));
	if (total < 60)
		snprintf(text, sizeof(text), "%ld s", total);
	else if (total < 3600) {
		snprintf(text, sizeof(text), "%ld min %ld s", total / 60, total % 60);
	} else {
		total = (total + 30) / 60;
		snprintf(text, sizeof(text), "%ld h %ld min", total / 60, total % 60);
	}
	return text;
}


RateMeter::RateMeter(double window)
	:
	fWindow(window),
	fFirstSeconds(-1)
{
}


void
RateMeter::Reset()
{
	fSamples.clear();
	fFirstSeconds = -1;
}


void
RateMeter::Add(double seconds, uint64_t bytes)
{
	if (fFirstSeconds < 0)
		fFirstSeconds = seconds;
	fSamples.push_back({ seconds, bytes });
	// Keep one sample at or before the window's start, so the window is
	// always fully covered.
	while (fSamples.size() > 2 && fSamples[1].seconds <= seconds - fWindow)
		fSamples.pop_front();
}


double
RateMeter::Rate() const
{
	if (fSamples.size() < 2)
		return 0;
	const Sample& first = fSamples.front();
	const Sample& last = fSamples.back();
	double span = last.seconds - first.seconds;
	if (last.seconds - fFirstSeconds < 1.0 || span <= 0)
		return 0;
	return (last.bytes - first.bytes) / span;
}


double
RateMeter::SecondsLeft(uint64_t total) const
{
	double rate = Rate();
	if (rate <= 0 || fSamples.empty())
		return -1;
	uint64_t done = fSamples.back().bytes;
	return done >= total ? 0 : (total - done) / rate;
}

}  // namespace imager
