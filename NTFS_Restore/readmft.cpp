#include "readmft.h"

void ReadMFTFromDisk() {
    std::string path = std::string("\\\\.\\") + driveLetter + ":";
    HANDLE hDrive = CreateFileW(StringToWString(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);

    if (hDrive == INVALID_HANDLE_VALUE) {
        std::cerr << "Không thể mở ổ đĩa! Mã lỗi: " << GetLastError() << std::endl;
        return;
    }

    // Lấy thông tin NTFS Volume (bao gồm vị trí MFT trên ổ đĩa)
    NTFS_VOLUME_DATA_BUFFER volumeData;
    DWORD bytesReturned;
    if (!DeviceIoControl(hDrive, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, 
                         &volumeData, sizeof(volumeData), &bytesReturned, NULL)) {
        std::cerr << "Lỗi khi lấy thông tin NTFS! Mã lỗi: " << GetLastError() << std::endl;
        CloseHandle(hDrive);
        return;
    }

    // Tính toán vị trí sector MFT
    LARGE_INTEGER mftSector;
    mftSector.QuadPart = volumeData.MftStartLcn.QuadPart * volumeData.BytesPerCluster / volumeData.BytesPerSector;

    // Dịch con trỏ đọc đến sector MFT
    LARGE_INTEGER byteOffset;
    byteOffset.QuadPart = mftSector.QuadPart * volumeData.BytesPerSector;

    if (SetFilePointerEx(hDrive, byteOffset, NULL, FILE_BEGIN) == 0) {
        std::cerr << "Không thể dịch con trỏ đọc! Mã lỗi: " << GetLastError() << std::endl;
        CloseHandle(hDrive);
        return;
    }

    // Mở file để ghi dữ liệu MFT
    std::ofstream outFile("mft_dump.bin", std::ios::binary);
    if (!outFile) {
        std::cerr << "Không thể mở file đầu ra!" << std::endl;
        CloseHandle(hDrive);
        return;
    }

    // Đọc dữ liệu MFT và ghi ra file
    LARGE_INTEGER mftSize;
    mftSize.QuadPart = volumeData.MftValidDataLength.QuadPart;

    BYTE* mftBuffer = new BYTE[mftSize.QuadPart];
    DWORD bytesRead;
    if (ReadFile(hDrive, mftBuffer, mftSize.QuadPart, &bytesRead, NULL)) {
        outFile.write(reinterpret_cast<const char*>(mftBuffer), bytesRead);
        std::cout << "Đọc toàn bộ MFT thành công: " << bytesRead << " bytes." << std::endl;
    } else {
        std::cerr << "Lỗi đọc toàn bộ MFT! Mã lỗi: " << GetLastError() << std::endl;
    }
    delete[] mftBuffer;

    std::cout << "Dữ liệu MFT đã được ghi vào mft_dump.bin" << std::endl;

    outFile.close();
    CloseHandle(hDrive);
}

// Hàm áp dụng fixup cho 1 record MFT
bool ApplyFixup(BYTE* record, size_t recordSize) {
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
std::vector<std::pair<LONGLONG, ULONGLONG>> DecodeRunlist(BYTE* runlist, size_t runlistLength) {
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

std::wstring ExtractFileName(BYTE* record) {
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

// Hàm phân tích 1 record MFT và nếu record đã bị xóa, giải mã các attribute $DATA
void ExtractClustersFromRecord(BYTE* record, size_t recordIndex) {
    MFTRecordHeader* header = reinterpret_cast<MFTRecordHeader*>(record);
    
    // Nếu record bị xóa, ta cần đặt lại flag để đánh dấu nó là file hợp lệ
    header->flags |= 0x01; // Đặt lại bit in-use

    size_t attrOffset = header->firstAttributeOffset;
    while (attrOffset < RECORD_SIZE) {
        DWORD attrType = *(DWORD*)(record + attrOffset);
        if (attrType == 0xFFFFFFFF) break; // Kết thúc attribute
        DWORD attrSize = *(DWORD*)(record + attrOffset + 4);
        if (attrSize == 0 || attrSize > RECORD_SIZE) break;

        // Nếu attribute là $DATA (0x80)
        if (attrType == 0x80) {
            BYTE nonResidentFlag = *(BYTE*)(record + attrOffset + 8);

            if (nonResidentFlag == 0) {
                // Nếu file là resident, ghi trực tiếp từ record MFT ra file
                DWORD contentSize = *(DWORD*)(record + attrOffset + 16);
                WORD contentOffset = *(WORD*)(record + attrOffset + 20);
                BYTE* data = record + attrOffset + contentOffset;

                // Tạo file khôi phục với đúng tên
                std::wstring fileName = ExtractFileName(record);
                std::string utf8FileName = WStringToString(fileName);
                std::ofstream outFile(std::string(1, driveLetter) + ":\\" + utf8FileName, std::ios::binary);
                if (!outFile) {
                    std::cerr << "Không thể tạo file khôi phục!\n";
                    return;
                }

                outFile.write(reinterpret_cast<const char*>(data), contentSize);
                outFile.close();

                std::cout << "Đã khôi phục file vào record #" << recordIndex << " thành công!\n";
            }
            // Nếu file là non-resident, cần đọc từ cluster gốc
        }

        attrOffset += attrSize;
    }
}

// Hàm kiểm tra xem record có toàn 0 hay không
bool IsRecordEmpty(BYTE* record) {
    for (size_t i = 0; i < RECORD_SIZE; i++) {
        if (record[i] != 0)
            return false;
    }
    return true;
}