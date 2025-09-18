#include <chrono>

namespace NameGenerators_
{
	std::string generate_prefix_timestamp_suffix_name(std::string prefix = "", std::string suffix = "")
	{
		// Base path (you can change this to whatever you need)
		const std::string basePath = prefix;

		// Get current time as time_t
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);

		// Convert to local tm structure
		std::tm local_tm;
		#if defined(_MSC_VER)
		localtime_s(&local_tm, &t);          // for MSVC
		#else
		localtime_r(&t, &local_tm);          // for POSIX
		#endif

		// Build the timestamped folder name
		std::ostringstream oss;
		oss << basePath
			<< std::setfill('0') << std::setw(2) << local_tm.tm_mday << '_'
			<< std::setfill('0') << std::setw(2) << (local_tm.tm_mon + 1) << '_'
			<< (local_tm.tm_year + 1900) << '_'
			<< std::setfill('0') << std::setw(2) << local_tm.tm_hour << '_'
			<< std::setfill('0') << std::setw(2) << local_tm.tm_min << '_'

			<< std::setfill('0') << std::setw(2) << local_tm.tm_sec
			<< suffix
			;

		return oss.str();
	}
}
