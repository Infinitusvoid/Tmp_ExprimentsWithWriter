#include <filesystem>
#include <iostream>

namespace Folder
{
	int create_folder_if_does_not_exist_already(std::string folderPath)
	{
		namespace fs = std::filesystem;
		if (!fs::exists(folderPath)) {
			if (fs::create_directory(folderPath)) {
				std::cout << "Folder created successfully." << std::endl;
			}
			else {
				std::cerr << "Failed to create folder." << std::endl;
				return 1;
			}
		}
		else {
			std::cout << "Folder already exists." << std::endl;
		}
	}

	std::vector<std::string> getFilePathsInFolder(const std::string& folderPath)
	{
		namespace fs = std::filesystem;
		std::vector<std::string> filePaths;

		try {
			for (const auto& entry : fs::directory_iterator(folderPath)) {
				if (entry.is_regular_file()) {
					filePaths.push_back(entry.path().string());
				}
			}
		}
		catch (const fs::filesystem_error& e) {
			std::cerr << "Filesystem error: " << e.what() << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << "General exception: " << e.what() << std::endl;
		}

		return filePaths;
	}

int countFilesInDirectory(const std::string& path)
{
	namespace fs = std::filesystem;
	try {
		if (!fs::exists(path) || !fs::is_directory(path)) {
			std::cerr << "Path does not exist or is not a directory." << std::endl;
			return -1;
		}

		int file_count = 0;
		for (const auto& entry : fs::directory_iterator(path)) {
			if (fs::is_regular_file(entry.path())) {
				++file_count;
			}
		}

		return file_count;
	}
	catch (const fs::filesystem_error& e) {
		std::cerr << "Filesystem error: " << e.what() << std::endl;
		return -1;
	}
	catch (const std::exception& e) {
		std::cerr << "General error: " << e.what() << std::endl;
		return -1;
	}
}

std::vector<std::string> getFilePathsWithExtension(const std::string& folderPath, const std::string& extension)
{
	namespace fs = std::filesystem;

	std::vector<std::string> filePaths;

	try {
		for (const auto& entry : fs::directory_iterator(folderPath)) {
			if (entry.is_regular_file() && entry.path().extension() == extension) {
				filePaths.push_back(entry.path().string());
			}
		}
	}
	catch (const fs::filesystem_error& e) {
		std::cerr << "Filesystem error: " << e.what() << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "General error: " << e.what() << std::endl;
	}

	return filePaths;
}

}
