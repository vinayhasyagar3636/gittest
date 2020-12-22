#include <Windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <setupapi.h>
#include <newdev.h>
#include <strsafe.h>
#include <msports.h >

#ifdef _UNICODE
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesW"
#endif

typedef BOOL(WINAPI* UpdateDriverForPlugAndPlayDevicesProto)(_In_opt_ HWND hwndParent,	//this will update the driver
	_In_ LPCTSTR HardwareId,
	_In_ LPCTSTR FullInfPath,
	_In_ DWORD InstallFlags,
	_Out_opt_ PBOOL bRebootRequired
	);

//this function will make the required COM ports as "in use"
//Its not a WINDOWS function
void ClaimPorts(HCOMDB ComHandle, unsigned int PNumber, unsigned char* Buf, unsigned short NextFPort, unsigned short* Buffer)
{

	unsigned short count = 0;
	do {
		if (Buf[NextFPort - 1] != 1)
		{
			if (ComDBClaimPort(ComHandle, NextFPort, TRUE, NULL))
			{
				
				return;
			}
			Buffer[count] = NextFPort;
			count++; 
		}
		NextFPort++;
	} while (NextFPort < PNumber);

}

//this will release the COM ports from "in use" state in COM port Database.
void ReleasePorts(HCOMDB ComHandle, unsigned short* Buffer)
{
	if (ComHandle != HCOMDB_INVALID_HANDLE_VALUE)
	{
		int count = 0;
		while (Buffer[count] > 0)
		{
			if (ComDBReleasePort(ComHandle, Buffer[count]))
			{

				return;
			}
			count++;
		}
	}
}
int main()
{
	GUID ClassGUID;
	TCHAR ClassName[32];
	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
	SP_DEVINFO_DATA DeviceInfoData;
	TCHAR hwIdList[LINE_LEN + 4];
	LPCTSTR hwid = L"UMDF2VirtualCom";		//Hardware ID of the driver without any slash
	TCHAR InfPath[MAX_PATH] =L"C:/UMDF2VirtualComPackage/UMDF2VirtualCom.inf";	//path of the INF file
	LPCTSTR inf;

	UpdateDriverForPlugAndPlayDevicesProto UpdateFn;
	BOOL reboot = FALSE;
	DWORD flags = 0;
	HMODULE newdevMod = NULL;
	unsigned short  Buffer2[4096] = { 0 };
	unsigned int PortNumber;
	printf("Enter port number\n");
	scanf_s("%d", &PortNumber);
	if (PortNumber == 1 || PortNumber == 2)				//we can't install the driver on COM1 and COM2
	{
		printf("port is already in use\n");
		return 0;
	}
	HCOMDB PHComDB;
	DWORD DataBaseSize = 0;
	LONG x = ComDBOpen(&PHComDB);					//It will open COM port database and returns a handle 
	if (PHComDB == HCOMDB_INVALID_HANDLE_VALUE)
	{
		printf("Could not open the database\n");
		return 0;
	}
	if (PHComDB != HCOMDB_INVALID_HANDLE_VALUE)
	{
		if (ComDBGetCurrentPortUsage(PHComDB, NULL, 0, CDB_REPORT_BYTES, &DataBaseSize))	// It will return the database size
		{
			return 0;
		}
		if (PortNumber > DataBaseSize)
		{
			printf("Invalid Port number\n");
			return 0;
		}
		BYTE Buffer[4097] = { 0 };
		
		if (ComDBGetCurrentPortUsage(PHComDB, Buffer, 4097, CDB_REPORT_BYTES, &DataBaseSize))	//it will return the COM port numbers which are already in use
		{
			return 0;
		}

		if (Buffer[PortNumber - 1] == 1)
		{
			printf("Port is alredy in use\n");
			return 0;
		}
		DWORD ports = 0;
		if (ComDBClaimNextFreePort(PHComDB, &ports))				//it will return the next available port
		{
			return 0;
		}

		if (ComDBReleasePort(PHComDB, ports))						//it will release the aquired port
		{
			return 0;
		}

		if (PortNumber != ports)
		{
			ClaimPorts(PHComDB, PortNumber, Buffer, 1, Buffer2);
		}
	}
	




	ZeroMemory(hwIdList, sizeof(hwIdList));
	if (FAILED(StringCchCopy(hwIdList, LINE_LEN, hwid))) {
		return 0;
	}
	//it will fetch the class name and class GUID from the INF file
	if (!SetupDiGetINFClass(InfPath, &ClassGUID, ClassName, sizeof(ClassName) / sizeof(ClassName[0]), 0))		
	{
		 return 0;;
	}
	//it will create  device info list using CLASS GUID
	DeviceInfoSet = SetupDiCreateDeviceInfoList(&ClassGUID, 0);
	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	//it will create DeviceInfoSet using class GUID and Class name
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfo(DeviceInfoSet,
		ClassName,
		&ClassGUID,
		NULL,
		0,
		DICD_GENERATE_ID,
		&DeviceInfoData))
	{
		
		return 0;
	}

	//it will right the hardware ID to registry. It has to be done before installing device instance
	if (!SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
		&DeviceInfoData,
		SPDRP_HARDWAREID,
		(LPBYTE)hwIdList,
		((DWORD)_tcslen(hwIdList) + 1 + 1) * sizeof(TCHAR)))
	{
		return 0;
	}
		//It will install the driver instance
		if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
			DeviceInfoSet,
			&DeviceInfoData))
		{

			return 0;
		}

		if (GetFileAttributes(InfPath) == (DWORD)(-1)) {

			return 0;
		}

		inf = InfPath;
		flags |= INSTALLFLAG_FORCE;

		newdevMod = LoadLibrary(TEXT("newdev.dll"));

		if (!newdevMod) {
			return 0;
		}

		UpdateFn = (UpdateDriverForPlugAndPlayDevicesProto)GetProcAddress(newdevMod, UPDATEDRIVERFORPLUGANDPLAYDEVICES);

		if (!UpdateFn)
		{
			return 0;
		}
		printf("installing...\n");
		//it will load the driver on the installed device instance and update the driver with availble COM port
		if (!UpdateFn(NULL, hwid, inf, flags, &reboot)) {
			DWORD m = GetLastError();
			return 0;
		}

		if (DeviceInfoSet != INVALID_HANDLE_VALUE) {
			SetupDiDestroyDeviceInfoList(DeviceInfoSet);
		}

		printf("installed\n");
		ReleasePorts(PHComDB, Buffer2);
		memset(Buffer2, 0, 4096);
		if (PHComDB != HCOMDB_INVALID_HANDLE_VALUE)
		{
			if (ComDBClose(PHComDB))
			{
				return 0;
			}
		}
	printf("1st commit\n");
	printf("commit from the git website\n");
	printf("enhancement commit\n");
	printf("enhancement commit2\n");
	printf("enhancement commit2\n");
	printf("enhancement commit4\n");
	printf("enhancement commit5\n");
		printf("enhancement commit6\n");
	printf("master\n");
	return 0;
}

/** the dll which are need to be used are 
Setupapi.dll
newdev.dll
msports.dll
*/
