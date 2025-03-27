#pragma once
#define UNICODE
#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include "wstring_string.h"

// Các hằng số
const size_t RECORD_SIZE = 1024;
const size_t SECTOR_SIZE = 512;

#pragma pack(push, 1)
struct MFTRecordHeader {
    char signature[4];       // "FILE" nếu record hợp lệ; record bị xóa hoặc ghi đè có thể không có "FILE"
    WORD fixupOffset;
    WORD fixupSize;
    ULONGLONG logSequenceNumber;
    WORD sequenceNumber;
    WORD hardLinkCount;
    WORD firstAttributeOffset;
    WORD flags;              // Bit 0: in-use; nếu bị xóa thì bit này không được set
    DWORD usedEntrySize;
    DWORD allocatedEntrySize;
    ULONGLONG baseFileRecord;
    WORD nextAttributeID;
    // Các trường khác (nếu cần)...
};
#pragma pack(pop)

class RecoverStrategy
{
public:
    virtual void ReadMFTOrFATFromDisk() = 0;
    virtual void FindAndRecover() = 0;
    virtual void SetdriveLetter(const char c) = 0;
    virtual char GetdriveLetter() = 0;
private:
};

class Recover
{
public:
    Recover();
    Recover(RecoverStrategy* rs);
    void SetStrategy(RecoverStrategy* rs);
    void StartRecover();
    ~Recover();
private:
    RecoverStrategy* rs;
};

class NTFS : public RecoverStrategy
{
public:
    NTFS();
    NTFS(const char driveLetter);
    bool ApplyFixup(BYTE* record, size_t recordSize);
    std::vector<std::pair<LONGLONG, ULONGLONG>> DecodeRunlist(BYTE* runlist, size_t runlistLength);
    std::wstring ExtractFileName(BYTE* record);
    bool ExtractClustersFromRecord(BYTE* record, size_t recordIndex);
    bool IsRecordEmpty(BYTE* record);
    void ReadMFTOrFATFromDisk();
    void FindAndRecover();
    void SetdriveLetter(const char c);
    char GetdriveLetter();
private:
    char driveLetter;
    std::vector<std::vector<BYTE>> records;
};

class FAT32 : public RecoverStrategy
{
public:
    FAT32();
    FAT32(const char driveLetter);
    // Nhớ sửa 2 hàm này ***************
    void ReadMFTOrFATFromDisk();
    void FindAndRecover();
    // *********************************
    void SetdriveLetter(const char c);
    char GetdriveLetter();
    // ******************************************************* THÊM CODE XỬ LÍ FAT32 TẠI ĐÂY *******************************************************
private:
    char driveLetter;
};
