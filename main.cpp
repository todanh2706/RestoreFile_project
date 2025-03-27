#include "recover.h"
#include <iomanip>
#include <winioctl.h>

std::wstring GetFileSystemType(wchar_t driveLetter) {
    wchar_t rootPath[] = { driveLetter, L':', L'\\', L'\0' };
    wchar_t fileSystemNameBuffer[MAX_PATH];

    if (GetVolumeInformationW(
            rootPath,
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            fileSystemNameBuffer,
            MAX_PATH
        )) 
    {
        return std::wstring(fileSystemNameBuffer);
    } 
    else 
    {
        std::wcerr << L"Lỗi khi lấy thông tin volume! Mã lỗi: " << GetLastError() << std::endl;
        return L"Unknown";
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    Recover recover;
    
    std::cout << "Nhập ổ đĩa mà bạn muốn tìm kiếm và khôi phục: ";
    char driveLetter; std::cin >> driveLetter;
    if (WStringToString(GetFileSystemType(driveLetter)) == "NTFS")
    {
        recover.SetStrategy(new NTFS(driveLetter));
    }
    else if (WStringToString(GetFileSystemType(driveLetter)) == "FAT32")
    {
        recover.SetStrategy(new FAT32(driveLetter));
    }
    else
    {
        std::cout << "Hệ thống tập tin của ổ đĩa " << driveLetter << " không được hỗ trợ!\n";
        return 0;
    }

    // Bắt đầu xử lí
    recover.StartRecover();
    return 0;
}
