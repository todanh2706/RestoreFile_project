#include "recover.h"

void NTFS::ReadMFTOrFATFromDisk() {
    const DWORD RECORD_SIZE = 1024; // Kích thước mỗi MFT record (thường là 1024 bytes, bạn có thể điều chỉnh)

    std::string path = std::string("\\\\.\\") + GetdriveLetter() + ":";
    HANDLE hDrive = CreateFileW(StringToWString(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);

    if (hDrive == INVALID_HANDLE_VALUE) {
        std::cerr << "Không thể mở ổ đĩa! Mã lỗi: " << GetLastError() << std::endl;
        return;
    }

    NTFS_VOLUME_DATA_BUFFER volumeData;
    DWORD bytesReturned;
    if (!DeviceIoControl(hDrive, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, 
                         &volumeData, sizeof(volumeData), &bytesReturned, NULL)) {
        std::cerr << "Lỗi khi lấy thông tin NTFS! Mã lỗi: " << GetLastError() << std::endl;
        CloseHandle(hDrive);
        return;
    }

    LARGE_INTEGER mftSector;
    mftSector.QuadPart = volumeData.MftStartLcn.QuadPart * volumeData.BytesPerCluster / volumeData.BytesPerSector;

    LARGE_INTEGER byteOffset;
    byteOffset.QuadPart = mftSector.QuadPart * volumeData.BytesPerSector;

    if (SetFilePointerEx(hDrive, byteOffset, NULL, FILE_BEGIN) == 0) {
        std::cerr << "Không thể dịch con trỏ đọc! Mã lỗi: " << GetLastError() << std::endl;
        CloseHandle(hDrive);
        return;
    }

    LARGE_INTEGER mftSize;
    mftSize.QuadPart = volumeData.MftValidDataLength.QuadPart;

    size_t numRecords = static_cast<size_t>(mftSize.QuadPart / RECORD_SIZE);

    DWORD bytesRead;
    bool readError = false;

    for (size_t i = 0; i < numRecords; ++i) {
        records.push_back(std::vector<BYTE>(RECORD_SIZE));
        if (!ReadFile(hDrive, records[i].data(), RECORD_SIZE, &bytesRead, NULL) || bytesRead != RECORD_SIZE) {
            std::cerr << "Lỗi đọc record " << i << "! Mã lỗi: " << GetLastError() << std::endl;
            readError = true;
            break;
        }
    }

    if (!readError) {
        std::cout << "Đọc " << numRecords << " record MFT thành công." << std::endl;
    }

    CloseHandle(hDrive);
}

void NTFS::FindAndRecover()
{
    bool checkDeleted = 0;
    
    // Quét từng record đã bị xóa
    for (size_t i = 0; i < records.size(); i++) {
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
                bool success = ExtractClustersFromRecord(record, i);
                if (success)
                    std::cout << "Đã khôi phục tập tin: " << utf8FileName << "\n";
                else
                    std::cout << "Khôi phục tập tin thất bại: " << utf8FileName << "\n";
            }
        }
    }    

    if (checkDeleted == 0) std::cout << "Không có Record nào bị xóa trong ổ đĩa!\n";
}

NTFS::NTFS() {}

NTFS::NTFS(const char driveLetter) : driveLetter(driveLetter) {}

// Hàm áp dụng fixup cho 1 record MFT (không thay đổi)
bool NTFS::ApplyFixup(BYTE* record, size_t recordSize) {
    if (recordSize < SECTOR_SIZE) return false;
    WORD fixupOffset = *(WORD*)(record + 4);
    WORD fixupSize = *(WORD*)(record + 6);
    if (fixupOffset + fixupSize * sizeof(WORD) > recordSize)
        return false;
    
    // Lấy fixup value (WORD đầu tiên của fixup array)
    WORD fixupValue = *(WORD*)(record + fixupOffset);
    int sectors = fixupSize - 1;
    for (int i = 0; i < sectors; i++) {
        size_t sectorEnd = (i + 1) * SECTOR_SIZE - 2; // WORD cuối của mỗi sector
        WORD* pSectorEnd = (WORD*)(record + sectorEnd);
        if (*pSectorEnd != fixupValue) {
            std::cerr << "[!] Record fixup mismatch tại sector " << i << "\n";
            return false;
        }
        // Thay thế WORD cuối sector bằng WORD tương ứng trong fixup array
        WORD replacement = *(WORD*)(record + fixupOffset + (i + 1) * 2);
        *pSectorEnd = replacement;
    }
    return true;
}

// Hàm giải mã runlist của attribute $DATA (non-resident)
// Trả về vector các cặp (LCN bắt đầu, số cluster)
std::vector<std::pair<LONGLONG, ULONGLONG>> NTFS::DecodeRunlist(BYTE* runlist, size_t runlistLength) {
    std::vector<std::pair<LONGLONG, ULONGLONG>> clusters;
    LONGLONG prevLCN = 0;
    size_t offset = 0;
    while (offset < runlistLength) {
        BYTE header = runlist[offset];
        offset++;
        if (header == 0) break;
        BYTE lengthSize = header & 0x0F;
        BYTE offsetSize = (header >> 4) & 0x0F;
        if (offset + lengthSize + offsetSize > runlistLength) break;
        ULONGLONG clusterCount = 0;
        memcpy(&clusterCount, runlist + offset, lengthSize);
        offset += lengthSize;
        LONGLONG relativeLCN = 0;
        memcpy(&relativeLCN, runlist + offset, offsetSize);
        offset += offsetSize;
        LONGLONG currentLCN = prevLCN + relativeLCN;
        clusters.push_back({ currentLCN, clusterCount });
        prevLCN = currentLCN;
    }
    return clusters;
}

std::wstring NTFS::ExtractFileName(BYTE* record) {
    size_t offset = ((MFTRecordHeader*)record)->firstAttributeOffset;
    while (offset < RECORD_SIZE) {
        DWORD attrType = *(DWORD*)(record + offset);
        if (attrType == 0xFFFFFFFF) break;
        if (attrType == 0x30) {
            WORD nameLen = *(WORD*)(record + offset + 88);
            return std::wstring((wchar_t*)(record + offset + 90), nameLen);
        }
        offset += *(DWORD*)(record + offset + 4);
    }
    return L"Recovered_File.bin";
}

// Sửa đổi hàm phục hồi để xử lý cả resident và non-resident và trả về bool
bool NTFS::ExtractClustersFromRecord(BYTE* record, size_t recordIndex) {
    MFTRecordHeader* header = reinterpret_cast<MFTRecordHeader*>(record);
    
    // Nếu record bị xóa, đặt lại flag đánh dấu là file hợp lệ
    header->flags |= 0x01; // Đặt lại bit in-use

    bool recovered = false;

    size_t attrOffset = header->firstAttributeOffset;
    // Lấy tên file để phục vụ cho cả resident và non-resident
    std::wstring fileName = ExtractFileName(record);
    std::string utf8FileName = WStringToString(fileName);
    std::string outputPath = "NTFS_Recovered_Files\\" + utf8FileName;

    while (attrOffset < RECORD_SIZE) {
        DWORD attrType = *(DWORD*)(record + attrOffset);
        if (attrType == 0xFFFFFFFF) break; // Kết thúc attribute
        DWORD attrSize = *(DWORD*)(record + attrOffset + 4);
        if (attrSize == 0 || attrSize > RECORD_SIZE) break;

        // Nếu attribute là $DATA (0x80)
        if (attrType == 0x80) {
            BYTE nonResidentFlag = *(BYTE*)(record + attrOffset + 8);

            if (nonResidentFlag == 0) {
                // Resident: ghi trực tiếp từ record MFT ra file
                DWORD contentSize = *(DWORD*)(record + attrOffset + 16);
                WORD contentOffset = *(WORD*)(record + attrOffset + 20);
                BYTE* data = record + attrOffset + contentOffset;

                if (!std::filesystem::exists(std::filesystem::path(outputPath).parent_path()))
                {
                    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
                }

                std::ofstream outFile(outputPath, std::ios::binary);
                if (!outFile) {
                    std::cerr << "Không thể tạo file khôi phục!\n";
                    return false;
                }
                outFile.write(reinterpret_cast<const char*>(data), contentSize);
                outFile.close();
                recovered = true;
            } else {
                // Non-resident: xử lý runlist
                WORD runlistOffset = *(WORD*)(record + attrOffset + 32);
                size_t runlistLength = attrSize - runlistOffset;
                BYTE* runlist = record + attrOffset + runlistOffset;

                auto clusters = DecodeRunlist(runlist, runlistLength);
                // Mở ổ đĩa để đọc dữ liệu các cluster
                std::string path = std::string("\\\\.\\") + GetdriveLetter() + ":";
                HANDLE hDrive = CreateFileW(StringToWString(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                              NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
                if (hDrive == INVALID_HANDLE_VALUE) {
                    std::cerr << "Không thể mở ổ đĩa để đọc dữ liệu non-resident!\n";
                    return false;
                }
                // Lấy thông tin volume để xác định kích thước cluster
                NTFS_VOLUME_DATA_BUFFER volumeData;
                DWORD bytesReturned;
                if (!DeviceIoControl(hDrive, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, 
                                     &volumeData, sizeof(volumeData), &bytesReturned, NULL)) {
                    std::cerr << "Lỗi khi lấy thông tin NTFS để đọc dữ liệu non-resident!\n";
                    CloseHandle(hDrive);
                    return false;
                }
                ULONGLONG bytesPerCluster = volumeData.BytesPerCluster;

                std::ofstream outFile(outputPath, std::ios::binary);
                if (!outFile) {
                    std::cerr << "Không thể tạo file khôi phục!\n";
                    CloseHandle(hDrive);
                    return false;
                }

                // Đọc dữ liệu từng run và ghi nối tiếp vào file
                for (auto &run : clusters) {
                    LONGLONG lcn = run.first;
                    ULONGLONG clusterCount = run.second;
                    ULONGLONG byteOffset = lcn * bytesPerCluster;
                    ULONGLONG byteCount = clusterCount * bytesPerCluster;

                    LARGE_INTEGER li;
                    li.QuadPart = byteOffset;
                    if (SetFilePointerEx(hDrive, li, NULL, FILE_BEGIN) == 0) {
                        std::cerr << "Không thể dịch con trỏ đọc trên ổ đĩa!\n";
                        outFile.close();
                        CloseHandle(hDrive);
                        return false;
                    }
                    BYTE* buffer = new BYTE[byteCount];
                    DWORD bytesRead;
                    if (!ReadFile(hDrive, buffer, byteCount, &bytesRead, NULL) || bytesRead != byteCount) {
                        std::cerr << "Lỗi đọc dữ liệu từ ổ đĩa!\n";
                        delete[] buffer;
                        outFile.close();
                        CloseHandle(hDrive);
                        return false;
                    }
                    outFile.write(reinterpret_cast<const char*>(buffer), byteCount);
                    delete[] buffer;
                }
                outFile.close();
                CloseHandle(hDrive);
                recovered = true;
            }
            break; // Xử lý attribute $DATA đầu tiên
        }
        attrOffset += attrSize;
    }
    return recovered;
}

// Hàm kiểm tra xem record có toàn 0 hay không
bool NTFS::IsRecordEmpty(BYTE* record) {
    for (size_t i = 0; i < RECORD_SIZE; i++) {
        if (record[i] != 0)
            return false;
    }
    return true;
}

void NTFS::SetdriveLetter(const char c)
{
    driveLetter = c;
}

char NTFS::GetdriveLetter()
{
    return driveLetter;
}

Recover::Recover()
{
    rs = nullptr;
}

Recover::Recover(RecoverStrategy *rs) : rs(rs) {}

void Recover::SetStrategy(RecoverStrategy *rs)
{
    this->rs = rs;
}

void Recover::StartRecover()
{
    rs->ReadMFTOrFATFromDisk();
    rs->FindAndRecover();
}

Recover::~Recover()
{
    rs = nullptr;
}

FAT32::FAT32() {}

FAT32::FAT32(const char driveLetter) : driveLetter(driveLetter) {}

void FAT32::ReadMFTOrFATFromDisk()
{
    // Thêm code tại đây
}

void FAT32::FindAndRecover()
{
    // Thêm code tại đây
}

void FAT32::SetdriveLetter(const char c)
{
    driveLetter = c;
}

char FAT32::GetdriveLetter()
{
    return driveLetter;
}
