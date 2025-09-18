
#pragma once

#include <string>
#include <functional>

namespace File
{

	void writeFile_OverrideIfExistAlready(const std::string& filename, const std::string& content);
	
	void writeFileIfNotExists(const std::string& filename, const std::string& content);
	
	void writeFileIfNotExists(const char* filename, const char* content);
	
	void appendLineToAFile(const std::string& filename, const std::string& data);
	
	void read_file_line_by_line_with_FpCallback(std::string filepath, void(*f)(std::string line));

	void read_file_line_by_line(std::string filepath, std::function<void(std::string)> f);
	
	std::string readFileToString(const std::string& filename);

	bool exists(const std::string& path);
	
	inline bool exists(const char* filename);

}
