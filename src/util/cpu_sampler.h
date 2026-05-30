#pragma once

#include <chrono>
#include <cstdint>

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN

namespace util
{
	/// Returns this process CPU usage (0–100, all logical cores) since the previous sample.
	class CpuSampler
	{
	public:
		bool sampleProcessCpuPercent(float* out_percent);

	private:
		uint64_t m_last_cpu_usec = 0;
		std::chrono::steady_clock::time_point m_last_at {};
		bool m_has_last = false;
	};
}

WAVE_NAMESPACE_END
