#define  _CRT_SECURE_NO_WARNINGS


#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <Lm.h>
#include <locale.h>
#include <aclapi.h>
#include <ws2tcpip.h>


#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#define WIN32_LEAN_AND_MEAN

#define GetWinVer 0         
#define GetCurTime 1		
#define GetWorkTime 2		
#define GetMemInfo 3		
#define GetDiskInfo 4		
#define GetDiskSpace 5		
#define GetAccesInfo 6		
#define GetOwner 7	

const char *options[] = { "Get Windows Version                  0\n",
						  "Get Server Time (UTC)                1\n",
						  "Get Server Time Since Boot           2\n",
						  "Get RAM Info                         3\n",
						  "Get Info About Storage Devices       4\n",
						  "Get Server Local Disks Free Space    5\n",
						  "Get Object Access Rights             6\n",
						  "Get Object Owner                     7\n" };

HANDLE g_StdOutput;
HANDLE g_StdInput;
int g_FileOrRegestry;


struct WinVer
{
	DWORD major;
	DWORD minor;
};

struct CurrentTime
{
	WORD year;
	WORD month;
	WORD day;
	WORD hour;
	WORD minute;
	WORD second;
	WORD ms;
};

struct TimeSinceStart
{
	WORD day;
	WORD hour;
	WORD minute;
	WORD second;
	WORD ms;
};

struct diskFreeSpace
{
	char driveLetter;
	DWORD ClusterSize;
	DWORD NumberOfClusters;
};

struct diskInfo
{
	char driveLetter;
	char type;
	char fsName[MAX_PATH + 1];
};

struct Memoryinfo
{
	DWORD dwMemoryLoad;
	DWORDLONG ullTotalPhys;
	DWORDLONG ullAvailPhys;
	DWORDLONG ullTotalPageFile;
	DWORDLONG ullAvailPageFile;
	DWORDLONG ullTotalVirtual;
	DWORDLONG ullAvailVirtual;
};

HCRYPTPROV ghProv;
HCRYPTKEY  ghKey;

PBYTE GeneratePublicKey(DWORD &dwBlobLength)
{
	ghProv = NULL;
	ghKey = NULL;
	dwBlobLength = 0;
	PBYTE ppbKeyBlob = NULL;

	if (!CryptAcquireContextA(&ghProv, NULL, MS_STRONG_PROV_A, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		printf("Error acquiring context.\n");
		goto Cleanup;
	}
	if (!CryptGenKey(ghProv, CALG_RSA_KEYX, RSA1024BIT_KEY, &ghKey))
	{
		printf("Error generating key.\n");
		goto Cleanup;
	}

	if (!CryptExportKey(ghKey, NULL, PUBLICKEYBLOB, 0, NULL, &dwBlobLength))
	{
		printf("Error computing BLOB length.\n");
		goto Cleanup;
	}

	ppbKeyBlob = (PBYTE)malloc(dwBlobLength);
	if (!ppbKeyBlob)
	{
		printf("Out of memory. \n");
		goto Cleanup;
	}

	if (!CryptExportKey(ghKey, NULL, PUBLICKEYBLOB, 0, ppbKeyBlob, &dwBlobLength))
	{
		printf("Error exporting key.\n");
		goto Cleanup;
	}

	return ppbKeyBlob;

Cleanup:
	if (ghProv)
	{
		CryptReleaseContext(ghProv, 0);
	}
	if (ghKey)
	{
		CryptDestroyKey(ghKey);
	}
	if (ppbKeyBlob)
	{
		free(ppbKeyBlob);
	}

	return ppbKeyBlob;
}

bool ImportSessionKey(BYTE *pbData, DWORD dwDataLen)
{
	if (!CryptDecrypt(ghKey, 0, true, 0, pbData, &dwDataLen))
	{
		printf("Error decrypting session key with a private key. %lu\n", GetLastError());
		goto Cleanup;
	}

	if (!CryptDestroyKey(ghKey))
	{
		printf("Error destroying key.\n");
		goto Cleanup;
	}

	if (!CryptReleaseContext(ghProv, 0))
	{
		printf("Error releasing context.\n");
		goto Cleanup;
	}

	if (!CryptAcquireContextA(&ghProv, NULL, MS_ENH_RSA_AES_PROV_A, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
	{
		printf("Error acquiring context.\n");
		goto Cleanup;
	}

	if (!CryptImportKey(ghProv, pbData, dwDataLen, 0, 0, &ghKey))
	{
		printf("Error importing key.\n");
		goto Cleanup;
	}

	return true;

Cleanup:
	if (ghProv)
	{
		CryptReleaseContext(ghProv, 0);
	}
	if (ghKey)
	{
		CryptDestroyKey(ghKey);
	}

	return false;
}

char *EncryptData(char *data, DWORD &size)
{
	DWORD dwDataSize = size, dwBufferSize = size;
	char *encryptedData = NULL;
	HCRYPTKEY hKey = ghKey;

	if (!CryptEncrypt(hKey, NULL, true, 0, NULL, &dwBufferSize, dwBufferSize))
	{
		printf("Error calculating space for encrypting data. \n");
		goto Cleanup;
	}
	encryptedData = (char*)malloc(dwBufferSize);
	memcpy(encryptedData, data, size);
	if (!CryptEncrypt(hKey, NULL, true, 0, (PBYTE)encryptedData, &dwDataSize, dwBufferSize))
	{
		printf("Error encrypting data.\n");
		goto Cleanup;
	}
	size = dwDataSize;
	return encryptedData;

Cleanup:

	if (encryptedData)
	{
		free(encryptedData);
	}

	return encryptedData;
}

char *DecryptData(char *data, DWORD &size)
{

	if (!CryptDecrypt(ghKey, 0, true, 0, (PBYTE)data, &size))
	{
		printf("Error decrypting data. %lu\n", GetLastError());
	}
	return data;
}

void ParseCommonAccessMask(DWORD mask)
{


	if (mask & DELETE)
	{
		printf("16 DELETE\n");
	}
	if (mask & READ_CONTROL)
	{
		printf("17 READ_CONTROL\n");
	}
	if (mask & WRITE_DAC)
	{
		printf("18 WRITE_DAC\n");
	}
	if (mask & WRITE_OWNER)
	{
		printf("19 WRITE_OWNER\n");
	}
	if (mask & SYNCHRONIZE)
	{
		printf("20 SYNCHRONIZE\n");
	}

	if (mask & MAXIMUM_ALLOWED)
	{
		printf("21 MAXIMUM_ALLOWED\n");
	}

	if (mask & GENERIC_ALL)
	{
		printf("28 GENERIC_ALL\n");
	}
	if (mask & GENERIC_EXECUTE)
	{
		printf("29 GENERIC_EXECUTE\n");
	}
	if (mask & GENERIC_WRITE)
	{
		printf("30 GENERIC_WRITE\n");
	}
	if (mask & GENERIC_READ)
	{
		printf("31 GENERIC_READ\n");
	}

}
void ParseFileAccessMask(DWORD mask)
{
	if (mask & (FILE_LIST_DIRECTORY | FILE_READ_DATA))
	{
		printf("0 FILE_LIST_DIRECTORY or FILE_READ_DATA\n");
	}
	if (mask & (FILE_ADD_FILE | FILE_WRITE_DATA))
	{
		printf("1 FILE_ADD_FILE or FILE_WRITE_DATA\n");
	}
	if (mask & (FILE_ADD_SUBDIRECTORY | FILE_APPEND_DATA))
	{
		printf("2 FILE_ADD_SUBDIRECTORY or FILE_APPEND_DATA\n");
	}
	if (mask & FILE_READ_EA)
	{
		printf("3 FILE_READ_EA\n");
	}
	if (mask & FILE_WRITE_EA)
	{
		printf("4 FILE_WRITE_EA\n");
	}
	if (mask & (FILE_TRAVERSE | FILE_EXECUTE))
	{
		printf("5 FILE_TRAVERSE or FILE_EXECUTE\n");
	}
	if (mask & FILE_DELETE_CHILD)
	{
		printf("6 FILE_DELETE_CHILD\n");
	}
	if (mask & FILE_READ_ATTRIBUTES)
	{
		printf("7 FILE_READ_ATTRIBUTES\n");
	}
	if (mask & FILE_WRITE_ATTRIBUTES)
	{
		printf("8 FILE_WRITE_ATTRIBUTES\n");
	}

}

void ParseRegestryAccessMask(DWORD mask)
{
	if (mask & KEY_QUERY_VALUE)
	{
		printf("0 KEY_QUERY_VALUE \n");
	}
	if (mask & KEY_SET_VALUE)
	{
		printf("1 KEY_SET_VALUE  \n");
	}
	if (mask & KEY_CREATE_SUB_KEY)
	{
		printf("2 KEY_CREATE_SUB_KEY  \n");
	}
	if (mask & KEY_ENUMERATE_SUB_KEYS)
	{
		printf("3 KEY_ENUMERATE_SUB_KEYS  \n");
	}
	if (mask & KEY_NOTIFY)
	{
		printf("4 KEY_NOTIFY  \n");
	}
	if (mask & KEY_CREATE_LINK)
	{
		printf("5 KEY_CREATE_LINK  \n");
	}
	if (mask & KEY_WOW64_64KEY)
	{
		printf("8 KEY_WOW64_64KEY  \n");
	}
	if (mask & KEY_WOW64_32KEY)
	{
		printf("9 KEY_WOW64_32KEY  \n");
	}
}

void ParseAceType(BYTE AceType)
{
	if (AceType == ACCESS_ALLOWED_ACE_TYPE)
	{
		printf("ACCESS_ALLOWED_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_DENIED_ACE_TYPE)
	{
		printf("ACCESS_DENIED_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_AUDIT_ACE_TYPE)
	{
		printf("SYSTEM_AUDIT_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_MAX_MS_V2_ACE_TYPE)
	{
		printf("ACCESS_MAX_MS_V2_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_ALLOWED_COMPOUND_ACE_TYPE)
	{
		printf("ACCESS_ALLOWED_COMPOUND_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE)
	{
		printf("ACCESS_ALLOWED_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_DENIED_OBJECT_ACE_TYPE)
	{
		printf("ACCESS_DENIED_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE)
	{
		printf("SYSTEM_AUDIT_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_ALARM_OBJECT_ACE_TYPE)
	{
		printf("SYSTEM_ALARM_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE)
	{
		printf("ACCESS_ALLOWED_CALLBACK_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_DENIED_CALLBACK_ACE_TYPE)
	{
		printf("ACCESS_DENIED_CALLBACK_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE)
	{
		printf("ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE)
	{
		printf("ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_AUDIT_CALLBACK_ACE_TYPE)
	{
		printf("SYSTEM_AUDIT_CALLBACK_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_ALARM_CALLBACK_ACE_TYPE)
	{
		printf("SYSTEM_ALARM_CALLBACK_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE)
	{
		printf("SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE)
	{
		printf("SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE)
	{
		printf("SYSTEM_MANDATORY_LABEL_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE)
	{
		printf("SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_SCOPED_POLICY_ID_ACE_TYPE)
	{
		printf("SYSTEM_SCOPED_POLICY_ID_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE)
	{
		printf("SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE\n");
	}
	else if (AceType == SYSTEM_ACCESS_FILTER_ACE_TYPE)
	{
		printf("SYSTEM_ACCESS_FILTER_ACE_TYPE\n");
	}
}



int sock_err(const char *function, int s)
{
	int err;
	err = errno;
	fprintf(stderr, "%s: socket error: %d\n", function, err);
	return -1;
}

// Функция определяет IP-адрес узла по его имени.
// Адрес возвращается в сетевом порядке байтов.
unsigned int get_host_ipn(const char *name)
{
	struct addrinfo *addr = 0;
	unsigned int ip4addr = 0;
	// Функция возвращает все адреса указанного хоста
	// в виде динамического однонаправленного списка
	if (0 == getaddrinfo(name, 0, 0, &addr))
	{
		struct addrinfo *cur = addr;
		while (cur)
		{
			// Интересует только IPv4 адрес, если их несколько - то первый
			if (cur->ai_family == AF_INET)
			{
				ip4addr = ((struct sockaddr_in *)cur->ai_addr)->sin_addr.s_addr;
				break;
			}
			cur = cur->ai_next;
		}
		freeaddrinfo(addr);
	}
	return ip4addr;
}


int sendEnc(int s, char *buf, DWORD size)
{
	char *enc = EncryptData(buf, size);

	if (send(s, enc, size, 0) < 0)
	{
		free(enc);
		return sock_err("send1", s);
	}
	free(enc);
}


int WaitConfirm(int s)
{
	char buffer[3];
	int res;
	while (1)
	{
		buffer[0] = '\0';
		buffer[1] = '\0';
		res = recv(s, buffer, 1, 0);
		if (res == 0)
		{
			printf("\nThe connection was terminated by the remote party\n");
			exit(0);
		}
		else if (res < 0)
		{
			return sock_err("recv", s);
		}
		if (buffer[0] == 'o')
		{
			break;
		}
	}

	while (1)
	{
		buffer[0] = '\0';
		buffer[1] = '\0';
		res = recv(s, buffer, 1, 0);
		if (res == 0)
		{
			printf("\nThe connection was terminated by the remote party\n");
			exit(0);
		}
		else if (res < 0)
		{
			return sock_err("recv", s);
		}
		if (buffer[0] == 'k')
		{
			break;
		}
	}
	return 0;
}

int RecvBytes(int s, int bytes, char *buf)
{
	int res = 0;
	while (bytes > 0)
	{
		res = recv(s, buf, 1, 0);
		if (res == 0)
		{
			printf("\nThe connection was terminated by the remote party\n");
			exit(0);
		}
		else if (res < 0)
		{
			return sock_err("recv", s);
		}
		bytes -= res;
		buf += res;
	}
	return 0;
}

int exchange_key(int s)
{
	DWORD dwBlobLength;
	PBYTE key = GeneratePublicKey(dwBlobLength);
	int res;
	char buf[512];
	// Step 0
	// Send length of public key
	// Receive ok 
	if (send(s, (const char*)&dwBlobLength, sizeof(dwBlobLength), 0) < 0)
	{
		return sock_err("send1", s);
	}
	WaitConfirm(s);
	// Step 1
	// Send public key
	// Receive session key length
	if (send(s, (const char*)key, dwBlobLength, 0) < 0)
	{
		return sock_err("send2", s);
	}
	free(key);
	RecvBytes(s, 4, buf);
	// Step 2
	// Send ok
	// Receive session key 
	if (send(s, "ok", 2, 0) < 0)
	{
		return sock_err("send3", s);
	}
	memcpy((void*)&dwBlobLength, buf, sizeof(DWORD));
	RecvBytes(s, dwBlobLength, buf);

	if (!ImportSessionKey((PBYTE)buf, dwBlobLength))
	{
		return 0;
	}

	printf("\nSession key was successfully received!\n");
	return 0;
}

void initWSA()
{
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0)
	{
		printf("WSAStartup ok\n");
	}
	else
	{
		printf("WSAStartup error\n");
	}
}

SOCKET InitClient(const char *str_ip, const char *str_port)
{
	unsigned short int port = (short int)atoi(str_port);

	int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return sock_err("socket", s);

	// Заполнение структуры с адресом удаленного узла
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = get_host_ipn(str_ip);

	// Установка соединения с удаленным хостом
	int times = 10;

	unsigned int ip = ntohl(addr.sin_addr.s_addr);
	printf("Connecting to %u.%u.%u.%u:%u \n",
		(ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, (ip) & 0xFF, port);

	// Производится 10 попыток подключения с интервалом в 100мс
	while (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		times--;
		if (times == 0)
		{
			closesocket(s);
			printf("Failed to connect\n");
			exit(0);
		}
		printf("%d attempts left\n", times);
		Sleep(100);
	}
	printf("Connected.\n");
	return s;
}

void PrintOptions()
{
	printf("                OPTIONS               \n");
	for (int i = 0; i < 8; i++)
	{
		printf(options[i]);
	}
}


char GetOptionFromUser()
{
	DWORD dwRead;
	char choice = 0;
	while (choice < '0' || choice > '7')
	{
		ReadConsoleA(g_StdInput, &choice, 1, &dwRead, NULL);
	}
	choice -= '0';
	printf("\nYou choose option %.37s\n", options[choice]);
	return choice;
}

void ShowOutput(int choice, char *data, DWORD size)
{
	if (choice == GetWinVer)
	{
		WinVer *ver = (WinVer*)data;
		printf("Windows %d.%d\n", ver->major, ver->minor);
	}
	else if (choice == GetCurTime)
	{
		CurrentTime *t = (CurrentTime*)data;
		printf("Current date: %02d.%02d.%04d %02d:%02d:%02d.%03d\n", t->day, t->month, t->year, t->hour, t->minute, t->second, t->ms);
	}
	else if (choice == GetWorkTime)
	{
		TimeSinceStart *t = (TimeSinceStart*)data;
		printf("Time since start: %02d days %02d:%02d:%02d.%03d\n", t->day, t->hour, t->minute, t->second, t->ms);

	}
	else if (choice == GetMemInfo)
	{
		int divider = 1024 * 1024;
		Memoryinfo *minf = (Memoryinfo*)data;
		printf("Memory Load: %u%%\n", minf->dwMemoryLoad);
		printf("Total Physical Memory: %lluMb\n", minf->ullTotalPhys / divider);
		printf("Available Physical Memory: %lluMb\n", minf->ullAvailPhys / divider);
		printf("Total Virtual Memory: %lluMb\n", minf->ullTotalVirtual / divider);
		printf("Available Virtual Memory: %lluMb\n", minf->ullAvailVirtual / divider);
	}
	else if (choice == GetDiskInfo)
	{
		int cnt = size / sizeof(diskInfo);
		diskInfo *info = (diskInfo*)data;
		for (int i = 0; i < cnt; i++)
		{
			char type[10];
			if (info[i].type == DRIVE_UNKNOWN)
			{
				strcpy(type, "Unknown");
			}
			else if (info[i].type == DRIVE_NO_ROOT_DIR)
			{
				strcpy(type, "No root dir");
			}
			else if (info[i].type == DRIVE_REMOVABLE)
			{
				strcpy(type, "Removable");
			}
			else if (info[i].type == DRIVE_FIXED)
			{
				strcpy(type, "Fixed");
			}
			else if (info[i].type == DRIVE_REMOTE)
			{
				strcpy(type, "Remote");
			}
			else if (info[i].type == DRIVE_CDROM)
			{
				strcpy(type, "CD-Rom");
			}
			else if (info[i].type == DRIVE_RAMDISK)
			{
				strcpy(type, "RAM disk");
			}
			else
			{
				strcpy(type, "Error");
			}
			printf("Disk %c:   Type: %11s   OS: %s\n", info[i].driveLetter, type, info[i].fsName);
		}
	}
	else if (choice == GetDiskSpace)
	{
		int cnt = size / sizeof(diskFreeSpace);
		diskFreeSpace *info = (diskFreeSpace*)data;
		for (int i = 0; i < cnt; i++)
		{
			double freeSpace = (double)info[i].ClusterSize*(double)info[i].NumberOfClusters / 1024.0 / 1024.0 / 1024.0;
			printf("Disk %c:   Free Space: %f Gb\n", info[i].driveLetter, freeSpace);
		}
	}
	else if (choice == GetOwner)
	{
		if (!strcmp(data, "FAIL ON SERVER!"))
		{
			printf("%s\n", data);
		}
		else
		{
			printf("Owner of specified object is %s\n", data);
		}
	}
	else if (choice == GetAccesInfo)
	{
		char *dataEnd = data;
		int deltaSize = 0;
		DWORD Mask;
		DWORD count;
		memcpy((char*)&count, data, sizeof(DWORD));
		dataEnd += sizeof(DWORD);
		size -= sizeof(DWORD);
		while (count)
		{
			ParseAceType((BYTE)*dataEnd);
			dataEnd++;
			size--;
			count--;
		}
		while (size > 0)
		{
			printf("\n\nName: %20s", dataEnd);
			deltaSize = strlen(dataEnd) + 1;
			dataEnd += deltaSize;
			size -= deltaSize;
			printf(" | SID: %15s", dataEnd);
			deltaSize = strlen(dataEnd) + 1;
			dataEnd += deltaSize;
			size -= deltaSize;
			memcpy((char*)&Mask, dataEnd, sizeof(Mask));
			dataEnd += sizeof(Mask);
			size -= sizeof(Mask);
			printf(" | Rights: %d\n", Mask);
			printf("BITS WHICH ARE SET IN MASK\n\n");
			if (g_FileOrRegestry == SE_FILE_OBJECT)
			{
				ParseFileAccessMask(Mask);
			}
			else if (g_FileOrRegestry == SE_REGISTRY_KEY)
			{
				ParseRegestryAccessMask(Mask);

			}
			ParseCommonAccessMask(Mask);
		}
	}
}


int Client(int s)
{
	char *buf = (char*)malloc(7000);
	while (1)
	{
		PrintOptions();
		char choice = GetOptionFromUser();

		//
		// Here should be added additional support for last 2 options
		//
		if (sendEnc(s, &choice, 1) < 0)
		{
			return sock_err("send1", s);
		}
		DWORD size;
		if (choice == GetWinVer || choice == GetCurTime || choice == GetWorkTime)
		{
			size = 16;
		}
		else if (choice == GetMemInfo)
		{
			size = 64;
		}
		else if (choice == GetDiskInfo || choice == GetDiskSpace)
		{
			size = 16;
			RecvBytes(s, size, buf);
			DecryptData(buf, size);
			memcpy((char*)&size, buf, size);
		}
		else if (choice == GetOwner || choice == GetAccesInfo)
		{
			size = MAX_PATH + 2;
			DWORD dwRead;
			char name[MAX_PATH + 2];
			printf("Choose type of object (%d - Regestry key, %d - File/Directory)\n", SE_REGISTRY_KEY, SE_FILE_OBJECT);
			while (name[size - 1] != '1' && name[size - 1] != '4')
			{
				ReadConsoleA(g_StdInput, &name[size - 1], 1, &dwRead, NULL);
			}
			name[size - 1] -= '0';
			g_FileOrRegestry = name[size - 1];
			DWORD dwMode;
			GetConsoleMode(g_StdInput, &dwMode);
			SetConsoleMode(g_StdInput, dwMode | (ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
			printf("Enter path to the %s (Max length is %d)\n", (name[size - 1] == 1) ? "file or directory" : "regestry key", MAX_PATH);
			ReadConsoleA(g_StdInput, name, MAX_PATH, &dwRead, NULL);
			name[dwRead - 1] = 0;
			name[dwRead - 2] = 0;
			SetConsoleMode(g_StdInput, dwMode &~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
			if (sendEnc(s, name, size) < 0)
			{
				return sock_err("send1", s);
			}
			if (choice == GetOwner)
			{
				size = 272;
			}
			else
			{
				size = 16;
				RecvBytes(s, size, buf);
				DecryptData(buf, size);
				if (!memcmp(buf, "FAIL", 4))
				{
					printf("ERROR ON SERVER\n\n");
					continue;
				}
				memcpy((char*)&size, buf, size);
			}
		}
		if (size > 7000)
		{
			buf = (char*)realloc(buf, size);
		}
		RecvBytes(s, size, buf);
		DecryptData(buf, size);

		ShowOutput(choice, buf, size);
		if (size > 7000 - 16)
		{
			buf = (char*)realloc(buf, 7000);
		}
	}
}


int main(int argc, const char *argv[])
{
	if (argc < 3)
	{
		printf("Wrong arguments!\n");
		return 0;
	}
	setlocale(LC_ALL, "Russian");
	g_StdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	g_StdInput = GetStdHandle(STD_INPUT_HANDLE);

	DWORD dwMode;
	GetConsoleMode(g_StdInput, &dwMode);
	SetConsoleMode(g_StdInput, dwMode &~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

	initWSA();
	SOCKET s = InitClient(argv[1], argv[2]);


	exchange_key(s);
	Client(s);

	closesocket(s);
	return 0;
}