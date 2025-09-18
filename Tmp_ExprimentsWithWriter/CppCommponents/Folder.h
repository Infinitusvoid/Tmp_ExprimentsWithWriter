#pragma once

#include <string>
#include <vector>

namespace Folder
{
	int create_folder_if_does_not_exist_already(std::string folderPath);
	
	std::vector<std::string> getFilePathsInFolder(const std::string& folderPath);

	int countFilesInDirectory(const std::string& path);

	std::vector<std::string> getFilePathsWithExtension(const std::string& folderPath, const std::string& extension);
}
