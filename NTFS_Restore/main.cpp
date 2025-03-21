#include "readmft.h"
#include <iomanip>
#include <winioctl.h>

char driveLetter;

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Nhập ổ đĩa mà bạn muốn tìm kiếm và khôi phục: ";
    std::cin >> driveLetter;
    ReadMFTFromDisk();

    std::ifstream infile("mft_dump.bin", std::ios::binary);
    if (!infile) {
        std::cerr << "Không thể mở file mft_dump.bin\n";
        return 1;
    }
    
    // Lấy kích thước file và tính số record (mỗi record 1024 byte)
    infile.seekg(0, std::ios::end);
    size_t fileSize = infile.tellg();
    infile.seekg(0, std::ios::beg);
    
    size_t numRecords = fileSize / RECORD_SIZE;
    std::cout << "Số lượng record MFT: " << numRecords << "\n";
    
    // Đọc toàn bộ record vào vector
    std::vector<std::vector<BYTE>> records(numRecords, std::vector<BYTE>(RECORD_SIZE));
    for (size_t i = 0; i < numRecords; i++) {
        infile.read(reinterpret_cast<char*>(records[i].data()), RECORD_SIZE);
    }
    infile.close();

    bool checkDeleted = 0;
    
    // Quét từng record đã bị xóa
    for (size_t i = 0; i < numRecords; i++) {
        BYTE* record = records[i].data();
    
        // Nếu record trống, bỏ qua
        if (IsRecordEmpty(record)) continue;
    
        MFTRecordHeader* header = reinterpret_cast<MFTRecordHeader*>(record);
    
        // Nếu record bị xóa
        if ((header->flags & 0x01) == 0 || header->allocatedEntrySize == 0) { 
            checkDeleted = 1;
            std::wstring fileName = ExtractFileName(record);
            std::string utf8FileName = WStringToString(fileName);
            
            std::cout << "Record #" << i << " - Tập tin: " << utf8FileName << " (đã bị xóa)\n";
            std::cout << "Bạn có muốn khôi phục tập tin này không? (y/n): ";
            
            char choice;
            std::cin >> choice;
            if (choice == 'y' || choice == 'Y') {
                ExtractClustersFromRecord(record, i);
                std::cout << "Đã khôi phục tập tin: " << utf8FileName << "\n";
            }
        }
    }    

    if (checkDeleted == 0) std::cout << "Không có Record nào bị xóa trong ổ đĩa!\n";

    return 0;
}