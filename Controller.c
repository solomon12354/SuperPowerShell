// Controller.c - User-Mode Application
#include <windows.h>
#include <winioctl.h> // For IOCTL codes
#include <stdio.h>

// 1. 定義我們和驅動程式之間溝通的「密碼」(IOCTL)
//    這個碼必須和驅動程式中的定義一模一樣。
#define IOCTL_EXECUTE_MEMORY_OP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 2. 定義我們要傳遞給驅動程式的資料結構
typedef struct _KERNEL_OP_REQUEST {
    ULONG ProcessId; // 我們要操作的目標 PID
    // 我們可以加入更多欄位，例如要讀寫的位址、資料等
    // 但在這個範例中，我們讓驅動程式自己完成所有計算
} KERNEL_OP_REQUEST, * PKERNEL_OP_REQUEST;


int main() {
    // --- 啟動目標處理程序 (與您的程式碼相同) ---
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
        NULL, NULL, NULL, FALSE,
        CREATE_SUSPENDED, // 關鍵：以暫停模式建立
        NULL, NULL, &si, &pi)) {
        printf("[-] CreateProcess 失敗，錯誤碼: %lu\n", GetLastError());
        return 1;
    }

    printf("[+] Powershell 啟動成功!\n");
    printf("    PID = %lu\n", pi.dwProcessId);

    // --- 與驅動程式通訊 ---

    // 3. 取得驅動程式的設備控制代碼
    //    "\\.\\MyDriver" 是我們在驅動程式中建立設備時定義的符號連結名稱
    // 將您 Controller.c 中的 CreateFileW 呼叫替換為此版本

    // 在 Controller.c 的 main 函式中...

    HANDLE hDevice = CreateFileW(
        L"\\\\.\\MyDriver",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, // 【修改點】恢復分享模式
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[-] 無法連接到驅動程式。請確保驅動程式已載入。\n");
        printf("    錯誤碼: %lu\n", GetLastError());
        // 清理: 終止我們已建立的處理程序
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    printf("[+] 成功連接到 MyDriver.sys\n");

    // 4. 準備請求並發送給驅動程式
    KERNEL_OP_REQUEST request;
    request.ProcessId = pi.dwProcessId;

    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_EXECUTE_MEMORY_OP, // 使用我們定義的 IOCTL
        &request,                // 輸入緩衝區：包含 PID
        sizeof(request),
        NULL,                    // 輸出緩衝區 (此範例中不需要)
        0,
        &bytesReturned,
        NULL
    );

    if (!success) {
        printf("[-] DeviceIoControl 失敗，錯誤碼: %lu\n", GetLastError());
    }
    else {
        printf("[+] 請求已成功發送給驅動程式，驅動程式已完成操作。\n");
    }

    // --- 清理 ---
    printf("[+] 恢復 powershell.exe 執行緒並關閉控制代碼。\n");
    ResumeThread(pi.hThread);
    CloseHandle(hDevice); // 關閉與驅動程式的連接
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}