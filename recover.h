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
#include <bitset> 
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



//==================FAT32===============================================
//======================================================================
#pragma pack(1)
typedef struct _BIOS_PARAM_BLOCK
{
	BYTE jump_instruction[3];
	BYTE oem[8];
	WORD bytes_Sector;					//Required//
	BYTE sec_Cluster;					//Required//
	WORD size_Sector_Reserved;			
	BYTE fatCount;						//Required//
	WORD Max_Root_Entry;
	WORD Total_Sector_FS; 
	BYTE media_type;
	WORD fat_sectors;
	WORD sec_per_track; 
	WORD num_head;
	DWORD num_before_part; 
	DWORD no_Sector_FS32;				 //Required//
	DWORD FATSz32;
	WORD ExtFlags;
	WORD FsVer;
	DWORD RootClus;
	BYTE rest[464];
} BPB;
#pragma pack()

#pragma pack(1)
typedef struct dir{
	BYTE fName[8]; //file name
	BYTE ext[3]; //file extension
	BYTE state; //Thuộc tính trạng thái 
	BYTE rest[6];
	WORD date; 
	WORD start_clus_high;
	BYTE rest2[4];
	WORD start_clus_low; 
	DWORD fSize; // Kích thước của phần nội dung tập tin
} DIR;
#pragma pack()


struct DeletedFile {
	std::string fileName;
    WORD firstCluster;  // Cluster đầu tiên của file bị xóa
    DWORD entryOffset;   // Offset của entry file trong thư mục
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
    
    bool hasValidAttribute(BYTE state);
    std::string extractLFN(const BYTE fInfo[32]);
    std::string getShortFileName(DIR _fpb);
    void FindDate(WORD _fpb);
    int getClusterCount(DIR _fpb, BPB _bpb, HANDLE hDrive);

    
    bool listAllFilesAndFolders(BPB _bpb, DWORD dwBytesRead, HANDLE hDrive);

    void PrintFileInformation(DIR _fpb, std::string fileName );
    void printNameOfDeletedFile(std::vector<DeletedFile> lstOfFileName);

    void readAndOpenDrive(std::string &drive, HANDLE &hDrive);
    bool readBootSector(HANDLE hDrive, BPB& _bpb, BYTE bBootSector[512], DWORD dwBytesRead);
    std::vector<DeletedFile> searchForDeletedFiles(BPB _bpb, DWORD dwBytesRead, HANDLE hDrive);
   
    bool markClusterEOF(HANDLE hDrive, BPB _bpb, DWORD cluster);
    
    bool markMultipleEOF(HANDLE hDrive, BPB _bpb, DWORD startCluster, int numberOfCluster);
    void recoverFile(HANDLE hDrive, BPB _bpb, DeletedFile delFile);
    

private:
    char driveLetter;
    HANDLE hDrive;
};
