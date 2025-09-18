#include "File.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace File
{

	void writeFile_OverrideIfExistAlready(const std::string& filename, const std::string& content)
	{
		std::ifstream file(filename);

		std::ofstream newFile(filename);

		if (!newFile.is_open()) {
			std::cerr << "Error: Unable to create the file." << std::endl;
			return;
		}

		newFile << content;
		newFile.close();
		std::cout << "File '" << filename << "' has been created and written." << std::endl;
	}

	void writeFileIfNotExists(const std::string& filename, const std::string& content)
	{
		std::ifstream file(filename);

		if (file.good()) {
			std::cout << "File '" << filename << "' already exists. Not overwriting." << std::endl;
			return; // File already exists, do not overwrite
		}

		std::ofstream newFile(filename);

		if (!newFile.is_open()) {
			std::cerr << "Error: Unable to create the file." << std::endl;
			return;
		}

		newFile << content;
		newFile.close();
		std::cout << "File '" << filename << "' has been created and written." << std::endl;
	}

	void writeFileIfNotExists(const char* filename, const char* content)
	{
		std::ifstream file(filename);

		if (file.good()) {
			std::cout << "File '" << filename << "' already exists. Not overwriting." << std::endl;
			return; // File already exists, do not overwrite
		}

		std::ofstream newFile(filename);

		if (!newFile.is_open()) {
			std::cerr << "Error: Unable to create the file." << std::endl;
			return;
		}

		newFile << content;
		newFile.close();
		std::cout << "File '" << filename << "' has been created and written." << std::endl;
	}

	void appendLineToAFile(const std::string& filename, const std::string& data) {
		std::ofstream outputFile;
		outputFile.open(filename, std::ios::app); // Open the file in append mode

		if (!outputFile.is_open()) {
			std::cerr << "Error opening the file." << std::endl;
			return;
		}

		outputFile << data << "\n"; // Write data to the file

		outputFile.close(); // Close the file
	}

	void read_file_line_by_line_with_FpCallback(std::string filepath, void(*f)(std::string line))
	{
		/// Create and open a text file
		std::string line;
		std::ifstream myfile(filepath);
		if (myfile.is_open())
		{
			while (getline(myfile, line))
			{
				f(line);
			}
			myfile.close();
		}
	}

	void read_file_line_by_line(std::string filepath, std::function<void(std::string)> f)
	{
		/// Create and open a text file
		std::string line;
		std::ifstream myfile(filepath);
		if (myfile.is_open())
		{
			while (getline(myfile, line))
			{
				f(line);
			}
			myfile.close();
		}
	}

	std::string readFileToString(const std::string& filename)
	{
		std::ifstream file(filename);
		if (!file.is_open()) {
			std::cerr << "Error: Unable to open file " << filename << std::endl;
			return "";
		}

		std::ostringstream oss;
		oss << file.rdbuf(); // Read the entire file into the stringstream
		file.close();

		return oss.str(); // Return the contents of the stringstream as a string
	}

	bool exists(const std::string& path)
	{
		return std::filesystem::exists(path);
	}

	inline bool exists(const char* filename)
	{
		return exists(std::string{ filename });
	}

}
