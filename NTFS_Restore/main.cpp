#include "readmft.h"
#include <iomanip>
#include <winioctl.h>
#include <cstring>

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
    
    // Quét từng record:
    for (size_t i = 0; i < numRecords; i++) {
        
        BYTE* record = records[i].data();
        // Nếu record trống, bỏ qua
        if (IsRecordEmpty(record))
        {
            std::cout << "Record #" << i << " trống!\n";
            continue;
        }

        // In ra thông tin flag của record
        MFTRecordHeader* header = reinterpret_cast<MFTRecordHeader*>(record);
        std::cout << "Record #" << i << " Flags: " << std::hex << header->flags << std::dec << "\n";
        std::cout << "Nội dung hex (64 byte đầu): ";
        for (int j = 0; j < 64; j++) {
            std::cout << std::hex << std::setfill('0') << std::setw(2)
                        << (int)record[j] << " ";
        }
        std::cout << std::dec << "\n";
        
        // Kiểm tra signature của record
        if (std::memcmp(record, "FILE", 4) != 0 && std::memcmp(record, "BAAD", 4) != 0) {
            std::cout << "Record " << i << " có signature không chuẩn: \""
                      << std::string((char*)record, 4) << "\"\n";
        } else {
            // Áp dụng fixup cho record
            if (!ApplyFixup(record, RECORD_SIZE)) {
                std::cerr << "[!] Record " << i << ": Lỗi fixup.\n";
                continue;
            }
            // Nếu record bị xóa (flag in-use không được set), tiến hành giải mã cluster
            if ((header->flags & 0x01) == 0 || header->allocatedEntrySize == 0) { 
                std::cout << "Record #" << i << " có thể là file đã bị xóa!\n";
                ExtractClustersFromRecord(record, i);
            }
        }
    }
    
    return 0;
}