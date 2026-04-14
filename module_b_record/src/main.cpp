#include <iostream>
#include <fstream>
#include <filesystem> // C++17 的文件系统库
#include <opencv2/opencv.hpp> // OpenCV 核心库

namespace fs = std::filesystem;

int main() {
	std::cout << "===== Module B: Environment & Storage Self-Check =====" << std::endl;
	std::cout << "Engineer: Ye Yu (Module B Leader)" << std::endl;

	// 1. 验证 OpenCV 是否链接成功
	std::cout << "\n[Check 1] OpenCV Library:" << std::endl;
	std::cout << " -> Current OpenCV Version: " << CV_VERSION << std::endl;

	// 2. 验证咱们刚分出的 V 盘存储路径 (根据契约)
	// 这里的路径要和你在 TeamInterfaceContract.md 里写的一致
	std::string storagePath = "V:/raw_video";

	std::cout << "\n[Check 2] Checking storage path: " << storagePath << std::endl;

	if (fs::exists(storagePath)) {
		std::cout << " -> SUCCESS: Path exists on V: drive." << std::endl;

		// 3. 验证是否有写权限（尝试建一个空文件）
		std::string testFilePath = storagePath + "/W1_test_file.txt";
		std::ofstream testFile(testFilePath);

		if (testFile.is_open()) {
			testFile << "Module B write test success! Verified at W1.";
			testFile.close();
			std::cout << " -> SUCCESS: Write permission granted. Test file created." << std::endl;
		}
		else {
			std::cerr << " -> ERROR: Cannot write to file! Check SMB permissions." << std::endl;
		}
	}
	else {
		std::cerr << " -> ERROR: Path not found! Did you create 'raw_video' folder in V:?" << std::endl;
	}

	std::cout << "\n======================================================" << std::endl;
	return 0;
}