## SuperShell
This is a SuperShell, you can use this to do a lot of things

# MyDriver + Controller (Kernel Privilege Escalation Demo)

## 📖 專案簡介
這是一個 **Windows Kernel-Mode Driver** 搭配 **User-Mode Controller** 的教學範例。  
專案目標是示範 **如何透過 IOCTL 與 Kernel Driver 溝通，並操作目標 Process 的 Token 權限**。  

- **MyDriver.sys**  
  - 一個簡單的 Kernel Driver  
  - 提供 IOCTL 介面，讓 User-mode 傳入目標 PID  
  - 透過 `PsLookupProcessByProcessId` 找到目標 `EPROCESS`  
  - 調整 `_SEP_TOKEN_PRIVILEGES` 權限位元 → 讓目標 Process 擁有 SYSTEM 級別的權限  

- **Controller.exe**  
  - User-mode 應用程式  
  - 建立一個目標進程（例如 PowerShell），但先以 **暫停模式** 啟動  
  - 呼叫 `DeviceIoControl` 把 PID 丟給驅動  
  - 驅動修改權限後，再恢復執行進程 → 直接變成 SYSTEM 權限  

---

## 🛠️ 編譯方法

### 1. Driver.sys
1. 使用 **Visual Studio + WDK (Windows Driver Kit)** 建立 Kernel Driver 專案  
2. 將 `MyDriver.c` 加入專案  
3. 設定目標平台為 **x64 (Release)**  
4. 編譯後輸出 `MyDriver.sys`  

### 2. Controller.exe
1. 使用 **Visual Studio (任意 C/C++ 專案即可)**  
2. 將 `Controller.c` 加入專案  
3. 編譯後輸出 `Controller.exe`  

---

## 🚀 執行步驟

1. **安裝並啟動測試模式（僅限教育用途！）**  
   ```powershell
   bcdedit /set testsigning on
   shutdown /r /t 0
  
2. **載入驅動**  
   ```powershell
   sc create MyDriver type= kernel binPath= C:\Path\To\MyDriver.sys
   sc start MyDriver
   
3. **執行 Controller**  
   ```powershell
   Controller.exe


---

## 🔑 驅動程式簽章 (Driver Signing)

Windows 64 位元系統 禁止未簽章的驅動載入，因此需要簽章。

### 方法一：測試簽章 (Test Signing)

適合開發 / 教學環境：

啟用測試模式（見上方步驟）。

1. **建立一個自簽憑證：**
   ```powershell
   makecert -r -pe -ss TestCertStore -n "CN=MyDriverCert" MyDriverCert.cer
   certmgr -add MyDriverCert.cer -s -r localMachine root
   certmgr -add MyDriverCert.cer -s -r localMachine trustedpublisher


2. **使用SignTool簽章：**
   ```powershell
   signtool sign /v /s TestCertStore /n MyDriverCert /t http://timestamp.digicert.com MyDriver.sys


驅動即可在測試模式下正常載入。

### 方法二：正式簽章 (正式上線用)

向 Microsoft Partner Center 申請 EV Code Signing 憑證（需付費 + 身份驗證）。

**使用該憑證簽署：**
   ```powershell
   signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 /v MyDriver.sys


驅動必須透過 Microsoft 驗證 才能在正式 Windows 環境載入。


---

## ⚠️ 注意事項

本專案 僅限教育與研究用途，請勿用於實際攻擊行為。

驅動程式中的 Token 偏移 (0x248, 0x40) 與 Windows 版本有關，版本不符可能導致 BSOD 藍屏。

在正式環境，Windows 會強制要求簽章並通過 Microsoft 驗證，否則無法載入。
