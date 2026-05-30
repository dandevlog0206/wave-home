#include "util/cpu_sampler.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <unistd.h>

WAVE_NAMESPACE_BEGIN
namespace util
{
	namespace
	{
		bool readProcessCpuUsec(uint64_t* out_usec)
		{
			std::ifstream stat("/proc/self/stat");
			if (!stat)
				return false;

			std::string token;
			// pid
			if (!(stat >> token))
				return false;
			// comm in parentheses
			if (!std::getline(stat, token, ')'))
				return false;

			uint64_t utime = 0;
			uint64_t stime = 0;
			// state, ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt
			for (int i = 0; i < 10; ++i)
			{
				if (!(stat >> token))
					return false;
			}
			if (!(stat >> utime >> stime))
				return false;

			const long clock_ticks = sysconf(_SC_CLK_TCK);
			if (clock_ticks <= 0)
				return false;

			const double to_usec = 1'000'000.0 / static_cast<double>(clock_ticks);
			*out_usec = static_cast<uint64_t>((utime + stime) * to_usec);
			return true;
		}
	}

	bool CpuSampler::sampleProcessCpuPercent(float* out_percent)
	{
		if (out_percent == nullptr)
			return false;

		uint64_t cpu_usec = 0;
		if (!readProcessCpuUsec(&cpu_usec))
			return false;

		const auto now = std::chrono::steady_clock::now();
		if (!m_has_last)
		{
			m_has_last = true;
			m_last_cpu_usec = cpu_usec;
			m_last_at = now;
			*out_percent = 0.f;
			return true;
		}

		const auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_at).count();
		if (wall_us <= 0)
		{
			*out_percent = 0.f;
			return true;
		}

		const long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
		const double capacity_us =
			static_cast<double>(wall_us) * static_cast<double>(cpu_count > 0 ? cpu_count : 1);
		const double delta_cpu_us = static_cast<double>(cpu_usec - m_last_cpu_usec);
		float percent = static_cast<float>((delta_cpu_us / capacity_us) * 100.0);
		if (percent < 0.f)
			percent = 0.f;
		if (percent > 100.f)
			percent = 100.f;

		m_last_cpu_usec = cpu_usec;
		m_last_at = now;
		*out_percent = percent;
		return true;
	}
}

WAVE_NAMESPACE_END
