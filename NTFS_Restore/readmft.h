#pragma once
#define UNICODE
#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include "wstring_string.h"

extern char driveLetter;
void ReadMFTFromDisk();
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

bool ApplyFixup(BYTE* record, size_t recordSize);
std::vector<std::pair<LONGLONG, ULONGLONG>> DecodeRunlist(BYTE* runlist, size_t runlistLength);
std::wstring ExtractFileName(BYTE* record);
void ExtractClustersFromRecord(BYTE* record, size_t recordIndex);
bool IsRecordEmpty(BYTE* record);