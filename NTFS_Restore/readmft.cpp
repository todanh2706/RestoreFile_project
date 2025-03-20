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
    // Kiểm tra flag: nếu bit 0 không được set thì record đã bị xóa
    bool inUse = (header->flags & 0x0001) != 0;
    if (inUse) {
        // Chỉ xử lý record đã bị xóa
        return;
    }
    std::cout << "Entry bị xóa: #" << recordIndex << "\n";
    size_t attrOffset = header->firstAttributeOffset;
    // Duyệt các attribute trong record
    while (attrOffset < RECORD_SIZE) {
        DWORD attrType = *(DWORD*)(record + attrOffset);
        if (attrType == 0xFFFFFFFF) {
            break; // Kết thúc attribute
        }
        DWORD attrSize = *(DWORD*)(record + attrOffset + 4);
        if (attrSize == 0 || attrSize > RECORD_SIZE) {
            std::cerr << "[!] Entry #" << recordIndex << ": Kích thước attribute không hợp lệ (attrSize=" 
                      << attrSize << ")\n";
            break;
        }
        // Nếu attribute là $DATA (0x80)
        if (attrType == 0x80) {
            // Kiểm tra non-resident flag (byte ở offset 8)
            BYTE nonResidentFlag = *(BYTE*)(record + attrOffset + 8);
            if (nonResidentFlag == 1) {
                // Với non-resident, runlist nằm trong attribute header.
                // Offset của runlist được lưu tại offset + 20 (WORD)
                WORD runListOffset = *(WORD*)(record + attrOffset + 20);
                BYTE* runList = record + attrOffset + runListOffset;
                // Xác định độ dài runlist bằng cách quét cho đến khi gặp byte 0 (không vượt quá attrSize)
                size_t runlistLength = 0;
                while ((attrOffset + runListOffset + runlistLength) < (attrOffset + attrSize) &&
                       runList[runlistLength] != 0)
                {
                    runlistLength++;
                }
                
                auto clusters = DecodeRunlist(runList, runlistLength);
                if (!clusters.empty()) {
                    std::cout << " - Cluster của entry #" << recordIndex << ": ";
                    for (auto& p : clusters) {
                        std::cout << "[" << p.first << " -> " << (p.first + p.second - 1) << "] ";
                    }
                    std::cout << "\n";
                } else {
                    std::cout << " - Entry #" << recordIndex << " không có runlist.\n";
                }
            } else {
                std::cout << " - Entry #" << recordIndex << " có attribute $DATA dạng resident (không có runlist).\n";
                DWORD contentSize = *(DWORD*)(record + attrOffset + 16);
                WORD contentOffset = *(WORD*)(record + attrOffset + 20);
                BYTE* data = record + attrOffset + contentOffset;
                std::string fileName = WStringToString((ExtractFileName(record)));
                std::ofstream outFile((std::string(1, driveLetter) + ":\\" + fileName).c_str(), std::ios::binary);
                outFile.write(reinterpret_cast<const char*>(data), contentSize);
                outFile.close();
                
                std::cout << "Đã khôi phục file " << fileName << " từ record #" << recordIndex 
                          << " với kích thước " << contentSize << " bytes.\n";

            }
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