#include "recover.h"
#include <conio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

FAT32::FAT32() {}

FAT32::FAT32(const char driveLetter) : driveLetter(driveLetter) {}

void FAT32::ReadMFTOrFATFromDisk()
{
    std::string path = std::string("\\\\.\\") + GetdriveLetter() + ":";

    hDrive = CreateFileA((path).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_WRITE_THROUGH, // or combine with FILE_FLAG_NO_BUFFERING if needed
        NULL
    );
    if (hDrive == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        std::cerr << "Error in reading the drive/it does not exist. Error code: " << err << "\n";
        CloseHandle(hDrive);
        return;
    }
  
}

void FAT32::readAndOpenDrive(std::string &drive, HANDLE &hDrive) {
    std::cout << "Enter thumb drive letter: ";
    std::cin >> drive;
    drive = "\\\\.\\" + drive + ":";

    hDrive = CreateFileA(drive.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_WRITE_THROUGH, // or combine with FILE_FLAG_NO_BUFFERING if needed
    NULL
);
    if (hDrive == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        // std::cerr << "Error in reading the drive/it does not exist. Error code: " << err << "\n";
        std::cerr << "Lỗi khi đọc ổ đĩa/ổ đĩa không tồn tại. Mã lỗi: " << err << "\n";
        CloseHandle(hDrive);
        return;
    }
}

void FAT32::FindAndRecover()
{
	BYTE bBootSector[512];				//Store boot sector		
	DWORD dwBytesRead(0);				//Number of read byes
	BPB _bpb;							//Boot sector data structure variable
	
		
	/*#########    Read Boot Sector     ##########*/
	if (hDrive != NULL)
	{
		// If drive exists and opened correctly go to else
		if (!readBootSector(hDrive, _bpb, bBootSector, dwBytesRead))
		{
			std::cerr << "\nLỗi khi đọc ổ đĩa/ổ đĩa không tồn tại\n";
		}
		else
		{
            std::cerr << "1. Liệt kê cái files có trong ổ đĩa" << std::endl;
            std::cerr << "2. Khôi phục file" << std::endl;
			int choice;
			std::cerr << "Lựa chọn: ";
			std::cin >> choice;
			if(choice == 1){
				listAllFilesAndFolders(_bpb, dwBytesRead, hDrive);
              
			}else{
                std::vector<DeletedFile> lst = searchForDeletedFiles(_bpb, dwBytesRead, hDrive);
                if(lst.empty()){
                    std::cerr << "Không có tập tin nào bị xoá!" << std::endl;
                    return;
                }
                printNameOfDeletedFile(lst);
                int index;
                std::cout << "Chọn tập tin khôi phục (chọn từ 1 - " << lst.size() << "): ";
                std::cin >> index;
                std::string target = lst[index - 1].fileName;
               
               
                std::cout << "======================================================\n";
                std::cerr << "Tập tin khôi phục: " << target << std::endl;
                std::cerr << "Chọn nơi khôi phục" << std::endl;
                std::cerr << "1. Khôi phục vào thư mục mà tập tin bị xoá" << std::endl;
                std::cerr << "2. Khôi mục vào thư mục chỉ định (FAT32_Recovered_Files)" << std::endl;
                std::cerr << "Lựa chọn (1/2): ";
                int choice;
                std::cin >> choice;
                std::cout << "======================================================\n";
                if(choice == 1){
                    recoverFile(hDrive, _bpb, target, 1);

                }else{
                    recoverFile(hDrive, _bpb, target, 0);
                }

		
			}
            std::cout << "======================================================\n";
            std::cout << "(Nhấn phím bất kỳ để kết thức chương trình)";
            getch();
          
		}
		CloseHandle(hDrive);
	}
    CloseHandle(hDrive);
}

void FAT32::SetdriveLetter(const char c)
{
    driveLetter = c;
}

char FAT32::GetdriveLetter()
{
    return driveLetter;
}

bool FAT32::hasValidAttribute(BYTE state) {
    return state == 0x01 || state == 0x02 || state == 0x04 ||
           state == 0x08 || state == 0x10 || state == 0x20;
}

std::string FAT32::getShortFileName(DIR _fpb) {
    std::string fileName;
    for (int i = 0; i < 8; i++) {
        if (_fpb.fName[i] == ' ') break;  // Bỏ khoảng trắng thừa
        fileName += (wchar_t)_fpb.fName[i];
    }
    if (_fpb.ext[0] != ' ') {
        fileName += ".";
        for (int i = 0; i < 3; i++) {
            if (_fpb.ext[i] == ' ') break;
            fileName += (wchar_t)_fpb.ext[i];
        }
    }
    return fileName;
}

std::string FAT32::extractLFN(const BYTE fInfo[32]){
    std::string namePart;
    
    for (int i = 1; i <= 10; i += 2)
        if (fInfo[i] != 0xFF) namePart += (wchar_t)(fInfo[i] | (fInfo[i + 1] << 8)); // UTF-16 decoding

    for (int i = 14; i <= 25; i += 2)
        if (fInfo[i] != 0xFF) namePart += (wchar_t)(fInfo[i] | (fInfo[i + 1] << 8));

    for (int i = 28; i <= 31; i += 2)
        if (fInfo[i] != 0xFF) namePart += (wchar_t)(fInfo[i] | (fInfo[i + 1] << 8));

    return namePart;
}

void FAT32::FindDate(WORD _fpb){
	std::bitset <16> bin(_fpb);
	std::string binNum = bin.to_string();
	std::string yearBin, monthBin, dayBin;
	std::bitset <7> year(binNum.substr(0, 7));
	std::bitset <4> month(binNum.substr(7, 4));
	std::bitset <5> day(binNum.substr(11, 5));
	
	int y, m, d;
	y = year.to_ulong();
	m = month.to_ulong();
	d = day.to_ulong();

	std::cout << (y + 1980) << "/" << m << "/" << d << std::endl;
}

bool FAT32::listAllFilesAndFolders(BPB _bpb, DWORD dwBytesRead, HANDLE hDrive) {
    BYTE fileInfo[512];  // Store unit (sector)
    DIR _fpb;            // Directory struct data type
    int rootDirStart;    // Calculated start of root directory
    DWORD dwCurrentPosition;
    std::vector<std::string> lfnEntries;  // Lưu các entry phụ (Long File Name)

    // Calculate start of root directory
    rootDirStart = _bpb.size_Sector_Reserved + (_bpb.fatCount * _bpb.FATSz32);
    SetFilePointer(hDrive, ((rootDirStart - 1) * 512), NULL, FILE_CURRENT);

    // std::cout << "\nListing all files and folders in the root directory:\n";
    std::cerr << "\nLiệt kê tất cả tập tin và thư mục trong thư mục gốc:\n";
    while (ReadFile(hDrive, fileInfo, 512, &dwBytesRead, NULL)) {
        for (int start = 0; start < 512; start += 32) {
            BYTE fInfo[32];
            memcpy(fInfo, &fileInfo[start], 32);
            memcpy(&_fpb, fInfo, 32);

            // Nếu entry trống, kết thúc danh sách
            if (_fpb.fName[0] == 0x00) {
                return true;
            }

            // Nếu entry bị xóa, bỏ qua
            if (_fpb.fName[0] == 0xE5) {
                continue;
            }

            // Nếu là entry phụ (0x0F), lưu lại dữ liệu tên file
            if (_fpb.state == 0x0F) {
                lfnEntries.push_back((extractLFN(fInfo)));  // Lấy tên từ entry LFN
                continue;
            }

            // Nếu là entry chính (SFN), ta ghép các entry phụ lại
            std::string fileName;
            if (!lfnEntries.empty()) {
                for (auto it = lfnEntries.rbegin(); it != lfnEntries.rend(); ++it) {
                    fileName += *it;  // Ghép theo thứ tự ngược lại
                }
                lfnEntries.clear();  // Xóa danh sách sau khi sử dụng
            } else {
				fileName = (getShortFileName(_fpb));
                
            }
			
			

			PrintFileInformation(_fpb, fileName);
            std::cout << "Số lượng clusters: " << getClusterCount(_fpb, _bpb, hDrive) << std::endl;
            
   
        }
    }
    return true;
}

bool FAT32::readBootSector(HANDLE hDrive, BPB& _bpb, BYTE bBootSector[512], DWORD dwBytesRead){
    if (!ReadFile(hDrive, bBootSector, 512, &dwBytesRead, NULL))
    {
        std::cerr << "\nLỗi khi đọc ổ đĩa/ổ đĩa không tồn tại\n";
        return false;
    }
    else
    {
        //Copy buffer to data structure for boot sector
        memcpy(&_bpb, bBootSector, 512);
        return true;
    }
}

void FAT32::PrintFileInformation(DIR _fpb, std::string fileName ){
	std::cout << "\nTHÔNG TIN THƯ MỤC/TẬP TIN:\n";
	std::cout << "============================\n";
	std::string attributes[6] = {"ReadOnly", "Hidden", "System", "VolLabel", "Directory", "Archive"};
    std::string str = "";
    for(int i = 7; i >= 0; i--){
        str += (_fpb.state & (1 << i)) ? '1' : '0';
    }
	
	for (size_t i = 0; i < str.size(); i++) {
		if (str[str.size() - 1 - i] == '1') {
			std::cout << attributes[i];  // Print remaining attributes with a comma
		}
	}
	std::cout << std::endl;
	std::cout << "Tên: " << fileName << std::endl;


	printf("Cluster bắt đầu: %d\n", _fpb.start_clus_low);
	printf("Kích thước: %d bytes\n", _fpb.fSize);
	printf("Ngày truy cập gần nhất: ");
	FindDate(_fpb.date);
	printf("\n");
}

std::vector<DeletedFile> FAT32::searchForDeletedFiles(BPB _bpb, DWORD dwBytesRead, HANDLE hDrive) {
    BYTE fileInfo[512];  
    DIR _fpb;            
    
    // Calculate the absolute byte offset of the root directory
    int rootDirStart = _bpb.size_Sector_Reserved + (_bpb.fatCount * _bpb.FATSz32);
    DWORD rootDirOffset = rootDirStart * _bpb.bytes_Sector;  // Convert sectors to bytes
    DWORD bytesRead;
    std::vector<DeletedFile> lstOfDel;
    std::vector<std::string> lfnEntries;

    if (SetFilePointer(hDrive, rootDirOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        // std::cerr << "Error setting file pointer to root directory\n";
        std::cerr << "Lỗi khi đặt con trỏ tập tin đến thư mục gốc\n";
        return {};
    }

    DWORD currentSectorOffset = 0;  // Track absolute position in the root directory
    char firstLetter;
    bool isLFN = false;

    while (ReadFile(hDrive, fileInfo, 512, &bytesRead, NULL) && bytesRead > 0) {
        for (int start = 0; start < bytesRead; start += 32) {
            memcpy(&_fpb, &fileInfo[start], sizeof(DIR));

            // Handle Long File Name (LFN) Entries
            if (_fpb.state == 0x0F && _fpb.fName[0] == 0xE5) {  
                firstLetter = _fpb.fName[1];
               
                lfnEntries.push_back((extractLFN(fileInfo + start)));
                continue;
            }
            // Check for deleted entry (0xE5)
            else if(static_cast<unsigned char>(_fpb.fName[0]) == 0xE5 && hasValidAttribute(_fpb.state)) {
                DeletedFile tmp;
                
                std::string fileName;
                if (!lfnEntries.empty()) {
                    isLFN = true;
                    for (auto it = lfnEntries.rbegin(); it != lfnEntries.rend(); ++it) {
                        fileName += *it;
                    }
                    lfnEntries.clear();
                
                } else {
                    fileName = (getShortFileName(_fpb));
                }

                if(isLFN)
                    fileName[0] = firstLetter;  // Replace first character
                else
                    fileName[0] = '_';

                if(fileName[fileName.size() - 1] == '\0')
                    fileName.pop_back();
               
                // Calculate correct entryOffset (absolute byte position)
                tmp.entryOffset = rootDirOffset + currentSectorOffset + start;
                tmp.fileName = fileName;
                tmp.firstCluster = (_fpb.start_clus_high << 16) | _fpb.start_clus_low;
                
                lstOfDel.push_back(tmp);
                isLFN = false;
            }else{
                lfnEntries.clear();
            }

            // Stop if empty entry (end of directory)
            if (_fpb.fName[0] == 0x00 ) {
                return lstOfDel;
            }
        }
        currentSectorOffset += 512;  // Move to next sector
    }
    return lstOfDel;
}

void FAT32::printNameOfDeletedFile(std::vector<DeletedFile> lstOfFileName){
	std::cout << "DANH SÁCH CÁC TẬP TIN BỊ XOÁ" << std::endl;
	for(int i = 0; i < lstOfFileName.size(); i++){
        std::cout << i + 1 << "." << lstOfFileName[i].fileName << "-" << std::dec << lstOfFileName[i].entryOffset << "-" << lstOfFileName[i].firstCluster << std::endl;
    }
}

int FAT32::getClusterCount(DIR _fpb, BPB _bpb, HANDLE hDrive) {
    int firstCluster = (_fpb.start_clus_high << 16) | _fpb.start_clus_low;
    int clusterSize = _bpb.sec_Cluster * 512;
    int totalClusters = _fpb.fSize / clusterSize;
    
    if (_fpb.fSize % clusterSize != 0) {
        totalClusters++;  // If there's remainder, add one more cluster
    }
    return totalClusters;
}
bool FAT32::markMultipleEOF(HANDLE hDrive, BPB _bpb, DWORD startCluster, int numberOfCluster) {
    if (numberOfCluster <= 0) return false;

    DWORD fatOffset = _bpb.size_Sector_Reserved * _bpb.bytes_Sector;       // FAT start offset
    DWORD fatSize = _bpb.FATSz32 * _bpb.bytes_Sector;                      // FAT size in bytes

    // Allocate memory for FAT table
    BYTE* fatTable = new BYTE[fatSize];
    DWORD bytesRead = 0;

    // Read FAT table into memory
    SetFilePointer(hDrive, fatOffset, NULL, FILE_BEGIN);
    if (!ReadFile(hDrive, fatTable, fatSize, &bytesRead, NULL) || bytesRead != fatSize) {
        std::cerr << "Failed to read FAT table." << std::endl;
        delete[] fatTable;
        return false;
    }

    DWORD cluster = startCluster;

    // Ensure the first cluster is free
    DWORD currentValue;
    memcpy(&currentValue, &fatTable[cluster * 4], 4);
    if (currentValue != 0x00000000) {
        std::cerr << "Start cluster " << cluster << " is not free (value: 0x" 
                  << std::hex << currentValue << ")." << std::endl;
        delete[] fatTable;
        return false;
    }

    // Mark the cluster chain
    for (int i = 0; i < numberOfCluster - 1; ++i) {
        DWORD nextCluster = cluster + 1;
        memcpy(&fatTable[cluster * 4], &nextCluster, 4);
        cluster = nextCluster;
    }

    // Mark the last cluster as EOF
    DWORD eofMarker = 0x0FFFFFFF;
    memcpy(&fatTable[cluster * 4], &eofMarker, 4);

    // Write FAT back to disk
    SetFilePointer(hDrive, fatOffset, NULL, FILE_BEGIN);
    DWORD bytesWritten = 0;
    if (!WriteFile(hDrive, fatTable, fatSize, &bytesWritten, NULL) || bytesWritten != fatSize) {
        std::cerr << "Failed to write FAT table back to disk." << std::endl;
        delete[] fatTable;
        return false;
    }

    delete[] fatTable;
    // std::cerr << "[+] Successfully marked " << numberOfCluster 
    //           << " clusters starting at " << startCluster << " as used with EOF." << std::endl;
    std::cerr << "[+]Tập tin khôi phục thành công" << std::endl;
    return true;
}

bool FAT32::markClusterEOF(HANDLE hDrive, BPB _bpb, DWORD cluster) {
    // In FAT32, the EOF marker is typically 0x0FFFFFFF.
    const DWORD FAT32_EOF = 0x0FFFFFFF;
    
    // Calculate the FAT start offset: FAT begins after the reserved sectors.
    ULONGLONG FATStartOffset = (ULONGLONG)_bpb.size_Sector_Reserved * _bpb.bytes_Sector;
    
    // Each FAT32 entry is 4 bytes. Compute the absolute offset for the given cluster.
    ULONGLONG fatEntryOffset = FATStartOffset + (ULONGLONG)(cluster * 4);
    
    // std::cout << "Calculated FAT entry offset: " << fatEntryOffset << std::endl;
    
    // Determine the sector size (usually 512 bytes).
    DWORD sectorSize = _bpb.bytes_Sector;
    
    // Compute the sector-aligned offset and the offset within that sector.
    ULONGLONG alignedOffset = (fatEntryOffset / sectorSize) * sectorSize;
    DWORD offsetWithinSector = (DWORD)(fatEntryOffset - alignedOffset);
    
    // Allocate a sector-sized buffer using VirtualAlloc (ensuring proper alignment).
    BYTE sectorBuffer[512]; 
   
    
    LARGE_INTEGER liAligned;
    liAligned.QuadPart = alignedOffset;
    
    // Set the file pointer to the aligned offset.
    if (!SetFilePointerEx(hDrive, liAligned, NULL, FILE_BEGIN)) {
        std::cerr << "Error setting file pointer to aligned offset: " 
                  << alignedOffset << ". Error code: " << GetLastError() << std::endl;
        return false;
    }
    
    DWORD bytesReadSector;
    if (!ReadFile(hDrive, sectorBuffer, sectorSize, &bytesReadSector, NULL) || bytesReadSector != sectorSize) {
        std::cerr << "Error reading sector at offset: " << alignedOffset << std::endl;
        return false;
    }
    
    // Modify the 4-byte FAT entry within the sector buffer.
    memcpy(&sectorBuffer[offsetWithinSector], &FAT32_EOF, sizeof(FAT32_EOF));
    
    // Set the file pointer back to the aligned offset for writing.
    if (!SetFilePointerEx(hDrive, liAligned, NULL, FILE_BEGIN)) {
        std::cerr << "Error setting file pointer for writing at offset: " 
                  << alignedOffset << ". Error code: " << GetLastError() << std::endl;
        return false;
    }
    
    DWORD bytesWritten;
    if (!WriteFile(hDrive, sectorBuffer, sectorSize, &bytesWritten, NULL) || bytesWritten != sectorSize) {
        std::cerr << "Error writing sector at offset: " << alignedOffset 
                  << ". Error code: " << GetLastError() << std::endl;
        return false;
    }

    std::cout << "Cluster " << cluster << " marked as EOF in FAT." << std::endl;
    std::cerr << "Tập tin khôi phục thành công" << std::endl;
    return true;
}

bool FAT32::setCheckSum(BYTE sectorBuffer[512], int start){
    BYTE sfn[11];
    for(int i = 0; i < 11; i++){
        sfn[i]= sectorBuffer[i + start];
    }
  
    BYTE targetChecksum = sectorBuffer[start -32 + 13];  // Desired checksum
    BYTE sum = 0;
    
    // Compute initial checksum
    for (int i = 0; i < 11; i++) {
        sum = ((sum >> 1) | (sum << 7)) + sfn[i];
    }

    // Adjust first byte to match the target checksum
    for (DWORD firstByte = 0; firstByte <= 0xFF; firstByte++) {
        sfn[0] = (BYTE)firstByte; // Modify first byte
        
        // Recalculate checksum
        sum = 0;
        for (int i = 0; i < 11; i++) {
            sum = ((sum >> 1) | (sum << 7)) + sfn[i];
        }

        if (sum == targetChecksum) {
            sectorBuffer[start]  = sfn[0];
            std::cout << "Finishing checkusm sucessfully." << std::endl;
            break;
        }
    }
    return true;
}

bool FAT32::setOriginalEntries(BYTE sectorBuffer[512], bool &isLFN, int start){
     // Prepare for LFN entry restoration
     int subEntry = start - 32;
     BYTE sequence = 0x01;
     
     // Temporary storage for original LFN entries
     BYTE originalLFNEntries[10][32];  // Assuming max 10 LFN entries
     int lfnEntryCount = 0;
    

     // Collect original LFN entries
     std::cout << "Sub entry: " << subEntry << std::endl;
     while (subEntry >= 0 && 
            sectorBuffer[subEntry + 11] == 0x0F && 
            sectorBuffer[subEntry] == 0xE5 && 
            lfnEntryCount < 10) {
         // Store the original entry
         memcpy(originalLFNEntries[lfnEntryCount], &sectorBuffer[subEntry], 32);
         lfnEntryCount++;
         subEntry -= 32;
         isLFN = true;
         std::cout << "Collect LFN entry: " << lfnEntryCount << " " << subEntry << std::endl;
     }
   
     // Restore LFN entries in reverse order
     //=================================================
     for (int i = 0; i < lfnEntryCount; i++) {
         // Calculate the actual offset to restore
         DWORD restoreOffset = start - (32 * (i + 1));
     
         // Copy original entry
         memcpy(&sectorBuffer[restoreOffset], originalLFNEntries[i], 32);
         
         // Modify sequence number
         if (i == lfnEntryCount - 1) {
             // Last entry (first in sequence) gets 0x40 | sequence
             sectorBuffer[restoreOffset] = 0x40 | sequence;
         } else {
             // Other entries get sequential numbers
             sectorBuffer[restoreOffset] = sequence;
         }
         sequence++;
     }
     return true;
}


bool FAT32::recoverContiguousToNTFS(const std::string& path, HANDLE hDrive, const BPB& bpb, DWORD startCluster, DWORD fileSize) {
    DWORD bytesPerCluster = bpb.bytes_Sector * bpb.sec_Cluster;
    DWORD totalClusters = (fileSize + bytesPerCluster - 1) / bytesPerCluster;
    DWORD firstDataSector = bpb.size_Sector_Reserved + (bpb.fatCount * bpb.FATSz32);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[-] Failed to open output file." << std::endl;
        return false;
    }

    for (DWORD i = 0; i < totalClusters; ++i) {
        DWORD cluster = startCluster + i;
        DWORD sector = firstDataSector + (cluster - 2) * bpb.sec_Cluster;
        DWORD offset = sector * bpb.bytes_Sector;

        std::vector<BYTE> buffer(bytesPerCluster);
        LARGE_INTEGER pos;
        pos.QuadPart = offset;
        DWORD bytesRead = 0;

        if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN) ||
            !ReadFile(hDrive, buffer.data(), bytesPerCluster, &bytesRead, NULL)) {
            std::cerr << "[-] Failed to read cluster #" << cluster << " at offset " << offset << std::endl;
            return false;
        }

        DWORD toWrite = MIN(fileSize, bytesPerCluster);
        out.write(reinterpret_cast<char*>(buffer.data()), toWrite);
        // std::cout << "[+] Recovered cluster #" << cluster << ", wrote " << toWrite << " bytes" << std::endl;

        fileSize -= toWrite;
    }

    out.close();
    std::cout << "[+] Tập tin khôi phục thành công. Output: " << path << std::endl;
    return true;
}


std::string FAT32::getFileName(DIR _fpb, std::vector<std::string> lfnEntries, bool &isLFN, unsigned char firstLetter){
    std::string fileName;
    if (!lfnEntries.empty()) {
        isLFN = true;
        for (auto it = lfnEntries.rbegin(); it != lfnEntries.rend(); ++it) {
            fileName += *it;
        }
        lfnEntries.clear();
    } else {
        fileName = (getShortFileName(_fpb));
       
    }
    if(isLFN)
        fileName[0] = firstLetter;
    else
        fileName[0] = '_';
    size_t pos = fileName.find('\0');
    if (pos != std::string::npos)
        fileName = fileName.substr(0, pos);
    return fileName;
}

void FAT32::recoverFile(HANDLE hDrive, BPB _bpb, std::string target, int state) {
    BYTE fileInfo[512];  
    DIR _fpb;            
    
    // Calculate absolute offset of the root directory.
    int rootDirStart = _bpb.size_Sector_Reserved + (_bpb.fatCount * _bpb.FATSz32);
    DWORD rootDirOffset = rootDirStart * _bpb.bytes_Sector; 
    DWORD bytesRead;
    std::vector<std::string> lfnEntries;
    
    if (SetFilePointer(hDrive, rootDirOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        std::cerr << "Error setting file pointer to root directory" << std::endl;
        return;
    }
    
    DWORD currentSectorOffset = 0;  
    unsigned char  firstLetter;
    
    
    while (ReadFile(hDrive, fileInfo, 512, &bytesRead, NULL) && bytesRead > 0) {
      
        for (int start = 0; start < bytesRead; start += 32) {
            memcpy(&_fpb, &fileInfo[start], sizeof(DIR));
            bool isLFN = false;
            bool isMoreCluster = false;
            if (_fpb.state == 0x0F) {  
                firstLetter = _fpb.fName[1];
                lfnEntries.push_back((extractLFN(fileInfo + start)));
                continue;
            }
       
            if (static_cast<unsigned char>(_fpb.fName[0]) == 0xE5 && hasValidAttribute(_fpb.state)) {
                std::string fileName = getFileName(_fpb, lfnEntries, isLFN, firstLetter);

                std::cout << fileName << std::endl;
              
                
                if (fileName == target) {
                    if(state){
                        std::cout << "File found. Recovering file entry..." << std::endl;
                        if(getClusterCount(_fpb, _bpb, hDrive) > 1){
                            isMoreCluster = true;
                        }else{
                            std::cout << "File has 1 cluster" << std::endl;
                        }

                        // Calculate the absolute offset of this directory entry.
                        DWORD fileEntryOffset = rootDirOffset + currentSectorOffset + start;
                        
                        // Lock and dismount the volume.
                        DWORD bytesReturned;
                        if (!DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                            std::cerr << "Error locking volume. Error code: " << GetLastError() << std::endl;
                            return;
                        }else{
                            std::cerr << "Sucessfully locking volume" << std::endl;
                        }
                        if (!DeviceIoControl(hDrive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                            std::cerr << "Error dismounting volume. Error code: " << GetLastError() << std::endl;
                            return;
                        }
                        // Prepare full sector buffer
                        BYTE sectorBuffer[512]; 
                        // Set file pointer to the start of the sector containing the file entry
                        DWORD sectorStartOffset = rootDirOffset + currentSectorOffset;

                        if (SetFilePointer(hDrive, sectorStartOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
                            std::cerr << "Error setting file pointer to sector start" << std::endl;
                            return;
                        }
                    
                        if (!ReadFile(hDrive, sectorBuffer, 512, &bytesRead, NULL) || bytesRead != 512) {
                            std::cerr << "Error reading sector" << std::endl;
                            return;
                        }
                        
                        
                        //Recover sub-entries
                        setOriginalEntries(sectorBuffer, isLFN, start);

                        //Recover checksum
                        if(isLFN){
                            std::cout << "Calculating checksum..." << std::endl;
                            setCheckSum(sectorBuffer, start);
                        }else{
                            std::cout << "Not check LFN. Set first letter of file to '_'" << std::endl;
                            sectorBuffer[start] = '_';
                        }

                        // Set file pointer back to sector start for writing
                        if (SetFilePointer(hDrive, sectorStartOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
                            std::cerr << "Error setting file pointer for writing" << std::endl;
                            return;
                        }
                
                        
                        // Write back the entire sector
                        DWORD bytesWritten;
                        if (!WriteFile(hDrive, sectorBuffer, 512, &bytesWritten, NULL) || bytesWritten != 512) {
                            std::cerr << "Error writing sector at offset: " << sectorStartOffset 
                                    << ". Error code: " << GetLastError() << std::endl;
                            return;
                        }

                        // Mark the starting cluster as EOF in the FAT.
                        // std::cerr << "Mark the starting cluster as EOF in the FAT." << std::endl;
                        DWORD startingCluster = (_fpb.start_clus_high << 16) | _fpb.start_clus_low;
                        if(isMoreCluster){
                            int numCluster = getClusterCount(_fpb, _bpb, hDrive);
                            std::cout << "Recovering long files..." << numCluster  << std::endl;
                            if(!markMultipleEOF(hDrive, _bpb, startingCluster, numCluster)){
                                std::cerr << "Failed to mark cluster " << startingCluster << " as EOF." << std::endl;
                                return;
                            }
                           
                        }else{
                            if (!markClusterEOF(hDrive, _bpb, startingCluster)) {
                                std::cerr << "Failed to mark cluster " << startingCluster << " as EOF." << std::endl;
                            }
                        }
                        return; // Exit after recovery.
                    }else{
                         // Recovery to NTFS path
                        
                      
                        DWORD startingCluster = (_fpb.start_clus_high << 16) | _fpb.start_clus_low;
                        std::string outputPath = "FAT32_Recovered_Files\\" + fileName;

                        if (!std::filesystem::exists(std::filesystem::path(outputPath).parent_path()))
                        {
                            std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
                        }
          
                        recoverContiguousToNTFS(outputPath, hDrive, _bpb, startingCluster, _fpb.fSize);
                        return;
                    }
            }
            }
            
            if (_fpb.fName[0] == 0x00) {
                return;
            }
        }
        currentSectorOffset += 512;
    }
}
