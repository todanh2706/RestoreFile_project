#include "recover.h"

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
            std::cerr << "1. Liệt cái files có trong ổ đĩa" << std::endl;
            std::cerr << "2. Khôi phục file" << std::endl;
			int choice;
			std::cerr << "Lựa chọn: ";
			std::cin >> choice;
			if(choice == 1){
				listAllFilesAndFolders(_bpb, dwBytesRead, hDrive);
              
			}else{
			std::vector<DeletedFile> lst = searchForDeletedFiles(_bpb, dwBytesRead, hDrive);
			if(lst.empty())
                std::cerr << "Không có tập tin nào bị xoá" << std::endl;
            else{
                printNameOfDeletedFile(lst);
                int index;
                std::cerr << "Chọn file (1 - " << lst.size() << "): ";
                std::cin >> index;
                std::cout << "==========================================================================\n";
                recoverFile(hDrive, _bpb, lst[index - 1]);
            }
			
		
			}
            char c;
            std::cerr << "Nhập phím bất kì để thoát: ";
            std::cin >> c;
          
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
	// std::cout << "\nFILE INFORMATION:\n";
    std::cerr << "\nTHÔNG TIN FILE:\n";
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
	std::cerr << std::endl;
	std::cerr << "Tên tập tin/thư mục: " << fileName << std::endl;
    std::cerr << "Số cluster bắt đầu: " << _fpb.start_clus_low << "\n";
    std::cerr << "Kích thước tập tin: " << _fpb.fSize << " bytes\n";
    std::cerr << "Ngày truy cập cuối: ";
    FindDate(_fpb.date);
    std::cerr << "\n";

	// printf("Starting cluster number: %d\n", _fpb.start_clus_low);
	// printf("File size: %d bytes\n", _fpb.fSize);
	// printf("Last day accessed: ");
	// FindDate(_fpb.date);
	// printf("\n");

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
	std::cerr << "DANH SÁCH CÁC FILE BỊ XOÁ" << std::endl;
	for(int i = 0; i < lstOfFileName.size(); i++){
        std::cout << i + 1 << " " << lstOfFileName[i].fileName << "-" << std::dec << lstOfFileName[i].entryOffset << "-" << lstOfFileName[i].firstCluster << std::endl;
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

    DWORD fatOffset = _bpb.size_Sector_Reserved * _bpb.bytes_Sector; // FAT start position
    DWORD fatSize = _bpb.FATSz32 * _bpb.bytes_Sector; // Size of FAT Table
    DWORD cluster = startCluster;
    DWORD nextCluster;
    DWORD fatEntry;
    
    // Read the FAT Table
    BYTE* fatTable = new BYTE[fatSize];

    DWORD bytesRead;
    SetFilePointer(hDrive, fatOffset, NULL, FILE_BEGIN);
    if (!ReadFile(hDrive, fatTable, fatSize, &bytesRead, NULL)) {
        delete[] fatTable;
        return false;
    }

    // Verify startCluster is empty
    if (*reinterpret_cast<DWORD*>(&fatTable[cluster * 4]) != 0x00000000) { 
        delete[] fatTable;
        return false; // Start cluster is already in use
    }

    int allocatedClusters = 0;
    DWORD lastWrittenCluster = 0; 

    while (allocatedClusters < numberOfCluster) {
        // Search for the next available cluster
        while (cluster < fatSize / 4 && *reinterpret_cast<DWORD*>(&fatTable[cluster * 4]) != 0x00000000) {
            cluster++;  // Skip used clusters
        }

        // If we reach the end of the FAT without enough clusters, return failure
        if (cluster >= fatSize / 4) {
            delete[] fatTable;
            return false;
        }

        // Look ahead for the next available cluster
        nextCluster = cluster + 1;
        while (nextCluster < fatSize / 4 && *reinterpret_cast<DWORD*>(&fatTable[nextCluster * 4]) != 0x00000000) {
            nextCluster++;  // Find the next available cluster
        }

        // If we found a next available cluster, write the current one
        if (nextCluster < fatSize / 4) {
            fatEntry = nextCluster;  // Link to next cluster
            memcpy(&fatTable[cluster * 4], &fatEntry, 4);
            lastWrittenCluster = cluster;
            allocatedClusters++;
        }

        // Move to the next cluster
        cluster = nextCluster;
    }

    // Mark the last cluster as EOF
    fatEntry = 0x0FFFFFFF;
    memcpy(&fatTable[lastWrittenCluster * 4], &fatEntry, 4);

    // Write the updated FAT table back to disk
    SetFilePointer(hDrive, fatOffset, NULL, FILE_BEGIN);
    DWORD bytesWritten;
    if (!WriteFile(hDrive, fatTable, fatSize, &bytesWritten, NULL)) {
        delete[] fatTable;
        return false;
    }

    delete[] fatTable;
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

    // std::cout << "Cluster " << cluster << " marked as EOF in FAT." << std::endl;
    return true;
}

void FAT32::recoverFile(HANDLE hDrive, BPB _bpb, DeletedFile delFile) {
    BYTE fileInfo[512];  
    DIR _fpb;            
    
    // Calculate absolute offset of the root directory.
    DWORD rootDirOffset = (delFile.entryOffset / 512) * 512;
    DWORD bytesRead;
    std::vector<std::string> lfnEntries;
    
    if (SetFilePointer(hDrive, rootDirOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        // std::cerr << "Error setting file pointer to root directory" << std::endl;
        std::cerr << "Lỗi khi đặt con trỏ tập tin đến thư mục gốc" << std::endl;
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
   
                if (fileName == delFile.fileName) {
                    // std::cout << "File found. Recovering file entry..." << std::endl;
                    std::cerr << "Đã tìm thấy tập tin. Đang khôi phục entry tập tin..." << std::endl;
                    if(getClusterCount(_fpb, _bpb, hDrive) > 1){
                        isMoreCluster = true;
                    }else{
                        // std::cout << "File has 1 cluster" << std::endl;
                        std::cerr << "Tập tin có 1 cluster" << std::endl;
                    }

                    // Calculate the absolute offset of this directory entry.
                    DWORD fileEntryOffset = rootDirOffset + currentSectorOffset + start;
                    
                    // Lock and dismount the volume.
                    DWORD bytesReturned;
                    if (!DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                        //std::cerr << "Error locking volume. Error code: " << GetLastError() << std::endl;
                        std::cerr << "Lỗi khi khóa volume. Mã lỗi: " << GetLastError() << std::endl;
                        return;
                    }else{
                        //std::cerr << "Sucessfully locking volume" << std::endl;
                        std::cerr << "Đã khóa volume thành công" << std::endl;
                    }
                    if (!DeviceIoControl(hDrive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                        //std::cerr << "Error dismounting volume. Error code: " << GetLastError() << std::endl;
                        std::cerr << "Lỗi khi unmount volume. Mã lỗi: " << GetLastError() << std::endl;
                        return;
                    }
                    // Prepare full sector buffer
                    BYTE sectorBuffer[512]; 
                    
                    // Set file pointer to the start of the sector containing the file entry
                    DWORD sectorStartOffset = rootDirOffset + currentSectorOffset;

                    if (SetFilePointer(hDrive, sectorStartOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
                        //std::cerr << "Error setting file pointer to sector start" << std::endl;
                        std::cerr << "Lỗi khi đặt con trỏ tập tin đến đầu sector" << std::endl;
                        return;
                    }
           
                    // Read the full sector
                    if (!ReadFile(hDrive, sectorBuffer, 512, &bytesRead, NULL) || bytesRead != 512) {
                        // std::cerr << "Error reading sector" << std::endl;
                        std::cerr << "Lỗi khi đọc sector" << std::endl;
                        return;
                    }

                    
                    // Prepare for LFN entry restoration
                    int subEntry = start - 32;
                    BYTE sequence = 0x01;
                    
                    // Temporary storage for original LFN entries
                    BYTE originalLFNEntries[10][32];  // Assuming max 10 LFN entries
                    int lfnEntryCount = 0;
                   

                    // Collect original LFN entries
                    std::cerr << "Tiến hành thu thập entry phụ..." << std::endl;
                    std::cerr << "\tEntry phụ đầu tiên: " << subEntry << std::endl;
                    std::cerr << "\tTiến hành kiểm tra các entry phụ..." << std::endl;
                    while (subEntry >= 0 && 
                           sectorBuffer[subEntry + 11] == 0x0F && 
                           sectorBuffer[subEntry] == 0xE5 && 
                           lfnEntryCount < 10) {
                        // Store the original entry
                        memcpy(originalLFNEntries[lfnEntryCount], &sectorBuffer[subEntry], 32);
                        lfnEntryCount++;
                        subEntry -= 32;
                        isLFN = true;
                        std::cout << "\tThu thập LFN entry: " << lfnEntryCount << " " << subEntry << std::endl;
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

                    // Modify the main directory entry and calculate checksum
                     //set checksum=================================================
                    //=============================================================
                  
                    if(isLFN){
                        // std::cout << "Calculating checksum..." << std::endl;
                        std::cout << "\tĐang tính toán checksum..." << std::endl;
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
                                std::cerr << "\t==>Thành công tính checkusm." << std::endl;
                                std::cerr << "\t==>Hoàn thành thu thập entry phụ." << std::endl;
                                break;
                            }
                        }
                        
                        
                    }else{
                        // std::cout << "Not check LFN. Set first letter of file to '_'" << std::endl;
                        std::cout << "==> Không kiểm tra LFN. Đặt chữ cái đầu tiên của tập tin thành '_'" << std::endl;
                        sectorBuffer[start] = '_';
                    }

                   
                    
                   
                   
                    // Set file pointer back to sector start for writing
                    if (SetFilePointer(hDrive, sectorStartOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
                        // std::cerr << "Error setting file pointer for writing" << std::endl;
                        std::cerr << "Lỗi khi đặt con trỏ tập tin để ghi" << std::endl;
                        return;
                    }
                    std::cerr << "Đã đặt con trỏ tập tin về đầu sector để bắt đầu ghi" << std::endl;
                    
                    // Write back the entire sector
                    DWORD bytesWritten;
                    if (!WriteFile(hDrive, sectorBuffer, 512, &bytesWritten, NULL) || bytesWritten != 512) {
                        // std::cerr << "Error writing sector at offset: " << sectorStartOffset 
                        //           << ". Error code: " << GetLastError() << std::endl;
                        std::cerr << "Lỗi khi ghi sector tại offset: " << sectorStartOffset 
                                  << ". Mã lỗi: " << GetLastError() << std::endl;
                        return;
                    }

                 
                    // // Mark the starting cluster as EOF in the FAT.
                    // std::cerr << "Mark the starting cluster as EOF in the FAT." << std::endl;
                    std::cerr << "\tBắt đầu đánh dấu cluster bắt đầu là EOF trong FAT..." << std::endl;
                  
                    std::cerr << "\tStart cluster: " << delFile.firstCluster << std::endl;
                    if(isMoreCluster){
                        int numCluster = getClusterCount(_fpb, _bpb, hDrive);
                        // std::cout << "Recovering long files..." << std::endl;
                        std::cerr << "\tĐang khôi phục tập tin dài..." << std::endl;
                        if(!markMultipleEOF(hDrive, _bpb, delFile.firstCluster, numCluster)){
                            //std::cerr << "Failed to mark cluster " << delFile.firstCluster << " as EOF." << std::endl;
                            std::cerr << "Không thể đánh dấu cluster " << delFile.firstCluster << " là EOF." << std::endl;
                        }else{
                            std::cerr << "==>Khôi phục tập tin thành công" << std::endl;
                        }
                    }else{
                        if (!markClusterEOF(hDrive, _bpb, delFile.firstCluster)) {
                            std::cerr << "Failed to mark cluster " << delFile.firstCluster << " as EOF." << std::endl;
                        }else{
                            std::cerr << "==>Khôi phục tập tin thành công" << std::endl;
                        }
                    }
                    return; // Exit after recovery.
                }
            }
            
            if (_fpb.fName[0] == 0x00) {
                return;
            }
        }
        currentSectorOffset += 512;
    }
}