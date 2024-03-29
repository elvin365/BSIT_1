#define  _CRT_SECURE_NO_WARNINGS


#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Lm.h>
#include <locale.h>
#include <aclapi.h>
#include <sddl.h>



#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#define MAX_CLIENTS (100)
#define WIN32_LEAN_AND_MEAN

#define GetWinVer 0
#define GetCurTime 1
#define GetWorkTime 2
#define GetMemInfo 3
#define GetDiskInfo 4
#define GetDiskSpace 5
#define GetAccesInfo 6
#define GetOwner 7

HCRYPTPROV ghProv;



struct client_ctx
{
	int step;
	DWORD keyLen;
	DWORD tmpLen;
	PBYTE keyBuf;
	PBYTE tmpBuf;
	HCRYPTKEY  hKey; //сессионный ключ для шифрования сообщений
	int OptionExtraInfo;
	int socket;
	CHAR buf_recv[512]; // Буфер приема
	CHAR *buf_send; // Буфер отправки
	unsigned int sz_recv; // Принято данных
	unsigned int sz_send_total; // Данных в буфере отправки
	unsigned int sz_send; // Данных отправлено
						  // Структуры OVERLAPPED для уведомлений о завершении
	OVERLAPPED overlap_recv;
	OVERLAPPED overlap_send;
	OVERLAPPED overlap_cancel;
	DWORD flags_recv; // Флаги для WSARecv
};

// Прослушивающий сокет и все сокеты подключения хранятся
// в массиве структур (вместе с overlapped и буферами)

struct client_ctx g_ctxs[1 + MAX_CLIENTS];
int g_accepted_socket;
HANDLE g_io_port;


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


bool GetWinVersion(WinVer &ver)
{
	NET_API_STATUS nStatus;
	LPSERVER_INFO_101 info = NULL;
	nStatus = NetServerGetInfo(NULL, 101, (LPBYTE *)&info);
	if (nStatus == NERR_Success)
	{
		ver.major = MAJOR_VERSION_MASK & info->sv101_version_major;
		ver.minor = info->sv101_version_minor;
		printf("Windows %d.%d\n", ver.major, ver.minor);
	}
	else
	{
		printf("Error in NetServerGetInfo:%d\n", nStatus);
	}
	if (!info)
	{
		NetApiBufferFree(info);
	}
	return !nStatus;
}


void GetTime(CurrentTime &t)
{
	SYSTEMTIME time;
	memset(&time, 0, sizeof(SYSTEMTIME));
	GetSystemTime(&time);
	t.year = time.wYear;
	t.month = time.wMonth;
	t.day = time.wDay;
	t.hour = time.wHour;
	t.minute = time.wMinute;
	t.second = time.wSecond;
	t.ms = time.wMilliseconds;
	printf("Current date: %02d.%02d.%04d %02d:%02d:%02d.%03d\n", t.day, t.month, t.year, t.hour, t.minute, t.second, t.ms);
}

void GetTimeSinceStart(TimeSinceStart &t)
{
	DWORD msec = GetTickCount();
	t.hour = msec / (1000 * 60 * 60);
	t.day = t.hour / 24;
	t.minute = msec / (1000 * 60) - t.hour * 60;
	t.second = (msec / 1000) - (t.hour * 60 * 60) - t.minute * 60;
	t.ms = (msec)-(t.hour * 60 * 60 * 1000) - t.minute * 60 * 1000 - t.second * 1000;
	t.hour %= 24;
	printf("Time since start: %02d days %02d:%02d:%02d.%03d\n", t.day, t.hour, t.minute, t.second, t.ms);
}

bool GetRamInfo(Memoryinfo &minf)
{
	MEMORYSTATUSEX st;
	int divider = 1024 * 1024;
	st.dwLength = sizeof(st);

	if (!GlobalMemoryStatusEx(&st))
	{
		return false;
	}
	minf.dwMemoryLoad = st.dwMemoryLoad;
	minf.ullTotalPhys = st.ullTotalPhys;
	minf.ullAvailPhys = st.ullAvailPhys;
	minf.ullTotalPageFile = st.ullTotalPageFile;
	minf.ullAvailPageFile = st.ullAvailPageFile;
	minf.ullTotalVirtual = st.ullTotalVirtual;
	minf.ullAvailVirtual = st.ullAvailVirtual;
	printf("Memory Load: %u%%\n", minf.dwMemoryLoad);
	printf("Total Physical Memory: %lluMb\n", minf.ullTotalPhys / divider);
	printf("Available Physical Memory: %lluMb\n", minf.ullAvailPhys / divider);
	printf("Total Virtual Memory: %lluMb\n", minf.ullTotalVirtual / divider);
	printf("Available Virtual Memory: %lluMb\n", minf.ullAvailVirtual / divider);
	return true;
}

diskFreeSpace *GetLocalDisksFreeSpace(int &count)
{
	count = 0;
	char disks[26][3] = { 0 };
	diskFreeSpace *disksInfo = (diskFreeSpace*)malloc(26 * sizeof(diskFreeSpace));
	DWORD s, b, f, c;
	double freeSpace;
	DWORD dr = GetLogicalDrives();
	for (int i = 0; i < 26; i++)
	{
		int n = ((dr >> i) & 0x00000001);
		if (n == 1)
		{
			disks[count][0] = char('A' + i);
			disks[count][1] = ':';

			if (GetDriveTypeA(disks[count]) == DRIVE_FIXED)
			{
				GetDiskFreeSpaceA(disks[count], &s, &b, &f, &c);
				freeSpace = (double)f * (double)s * (double)b / 1024.0 / 1024.0 / 1024.0;
				printf("Disk %s %f Gb is available\n", disks[count], freeSpace);

				disksInfo[count].driveLetter = char('A' + i);
				disksInfo[count].ClusterSize = s * b;
				disksInfo[count].NumberOfClusters = f;
				count++;
			}
		}
	}
	return (diskFreeSpace*)realloc(disksInfo, count * sizeof(diskFreeSpace));
}

diskInfo *GetDisksInfo(int &count)
{
	diskInfo *disksInfo = (diskInfo*)malloc(26 * sizeof(diskInfo));
	count = 0;
	char disks[26][4] = { 0 };
	DWORD dr = GetLogicalDrives();
	for (int i = 0; i < 26; i++)
	{
		int n = ((dr >> i) & 0x00000001);
		if (n == 1)
		{
			disksInfo[count].driveLetter = char('A' + i);
			disks[count][0] = char('A' + i);
			disks[count][1] = ':';
			disks[count][2] = '\\';

			disksInfo[count].type = GetDriveTypeA(disks[count]);
			if (!GetVolumeInformationA(disks[count], NULL, 0, NULL, NULL, NULL, disksInfo[count].fsName, sizeof(disksInfo[count].fsName)))
			{
				disksInfo[count].fsName[0] = '\0';
				//printf("Error in GetVolumeInformationA: %u\n",GetLastError());
			}
			count++;
		}
	}
	return (diskInfo*)realloc(disksInfo, count * sizeof(diskInfo));
}

char *GetObjectOwner(char *objectName, SE_OBJECT_TYPE ObjectType)
{
	PSID owner;
	PACL acl;

	if (ERROR_SUCCESS != GetNamedSecurityInfoA(objectName, ObjectType, OWNER_SECURITY_INFORMATION, &owner, NULL, NULL, NULL, NULL))
	{
		printf("Error in GetNamedSecurityInfoA:\n");
		return NULL;
	}
	DWORD sizeName = 0, sizeDomain = 0;
	if (!LookupAccountSidA(NULL, owner, NULL, &sizeName, NULL, &sizeDomain, NULL))
	{
		if (122 != GetLastError())
		{
			printf("Error in LookupAccountSidA\n");
			return NULL;
		}
	}
	char *domain = (char*)malloc(sizeName + sizeDomain);
	char *ownerName = domain + sizeDomain;
	SID_NAME_USE t;
	if (!LookupAccountSidA(NULL, owner, ownerName, &sizeName, domain, &sizeDomain, &t))
	{
		printf("Error in LookupAccountSidA\n");
		free(ownerName);
		free(domain);
		return NULL;
	}
	domain[sizeDomain] = '\\';
	printf("Owner is: %s\n", domain);
	return domain;
}



char *GetObjectAccessRights(char *objectName, SE_OBJECT_TYPE ObjectType, DWORD &size)
{
	char *packEnd;
	PSID pSid;
	PACL acl;
	ACL_SIZE_INFORMATION aclSize;
	DWORD count;

	if (ERROR_SUCCESS != GetNamedSecurityInfoA(objectName, ObjectType, DACL_SECURITY_INFORMATION, NULL, NULL, &acl, NULL, NULL))
	{
		printf("Error in GetNamedSecurityInfoA: \n");
		return NULL;
	}

	if (!GetAclInformation(acl, &aclSize, sizeof(aclSize), AclSizeInformation))
	{
		printf("Error in GetAclInformation. \n");
		return NULL;
	}
	count = aclSize.AceCount;
	char *pack = (char*)malloc(count + sizeof(DWORD));
	memcpy(pack, (const char*)&count, sizeof(DWORD));
	BYTE *AceTypes = (BYTE*)pack + sizeof(DWORD);
	DWORD written = count + sizeof(DWORD);
	for (int i = 0; i < count; i++)
	{
		ACCESS_ALLOWED_ACE *pAce;
		if (!GetAce(acl, i, (LPVOID*)&pAce))
		{
			printf("Error in GetAce. \n");
			free(pack);
			return NULL;
		}
		AceTypes[i] = pAce->Header.AceType;
		if (!pAce->Header.AceType == ACCESS_ALLOWED_ACE_TYPE)
		{
			continue;
		}
		pSid = (PSID)(&(pAce->SidStart));
		char *name, *domain;
		DWORD szName = 0, szDomain = 0;
		SID_NAME_USE type;
		if (!LookupAccountSidA(NULL, pSid, NULL, &szName, NULL, &szDomain, &type))
		{
			if (GetLastError() != 122)
			{
				continue;
			}
		}
		name = (char*)malloc(szName);
		domain = (char*)malloc(szDomain);
		if (!LookupAccountSidA(NULL, pSid, name, &szName, domain, &szDomain, &type))
		{
			continue;
		}
		char *sidDisplay;
		if (!ConvertSidToStringSidA(pSid, &sidDisplay))
		{
			printf("Error in ConvertSidToStringSidA.\n");
			free(pack);
			return NULL;
		}
		printf("Name: %20s | SID: %15s | Rights: %d\n", name, sidDisplay, pAce->Mask);
		DWORD SidLen = strlen(sidDisplay) + 1;
		pack = (char*)realloc(pack, written + SidLen + szName + 1 + sizeof(DWORD));
		packEnd = pack + written;
		strcpy(packEnd, name);
		strcpy(packEnd + szName + 1, sidDisplay);
		memcpy(packEnd + SidLen + szName + 1, (const char*)&pAce->Mask, sizeof(DWORD));
		written += SidLen + szName + 1 + sizeof(DWORD);
		LocalFree(sidDisplay);
		free(name);
		free(domain);
	}
	size = written;
	if (written == 0)
	{
		free(pack);
		pack = NULL;
	}
	return pack;
}




char *EncryptData(char *data, DWORD &size, int idx)
{
	DWORD dwDataSize = size, dwBufferSize = size;
	char *encryptedData = NULL;
	HCRYPTKEY hKey = g_ctxs[idx].hKey;

	if (!CryptEncrypt(hKey, NULL, true, 0, NULL, &dwBufferSize, dwBufferSize))
	{
		printf("Error calculating space for encrypting data. %lu\n", GetLastError());
		goto Cleanup;
	}
	encryptedData = (char*)malloc(dwBufferSize);
	memcpy(encryptedData, data, size);
	if (!CryptEncrypt(hKey, NULL, true, 0, (PBYTE)encryptedData, &dwDataSize, dwBufferSize))
	{
		printf("Error encrypting data. %lu\n", GetLastError());
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


char *DecryptData(char *data, DWORD &size, int idx)
{

	if (!CryptDecrypt(g_ctxs[idx].hKey, 0, true, 0, (PBYTE)data, &size))
	{
		printf("Error decrypting data. %lu\n", GetLastError());
	}
	return data;
}



PBYTE GenerateSessionKey(DWORD &dwBlobLength, int client_idx)
{
	dwBlobLength = 0;
	ghProv = NULL;
	g_ctxs[client_idx].hKey = NULL;
	PBYTE ppbKeyBlob = NULL;

	if (!CryptAcquireContextA(&ghProv, NULL, MS_ENH_RSA_AES_PROV_A, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
	{
		printf("Error acquiring context.\n");
		goto Cleanup;
	}
	if (!CryptGenKey(ghProv, CALG_AES_256, 0x01000000 | CRYPT_EXPORTABLE, &g_ctxs[client_idx].hKey))
	{
		printf("Error generating key.%lu\n", GetLastError());
		goto Cleanup;
	}

	if (!CryptExportKey(g_ctxs[client_idx].hKey, NULL, PLAINTEXTKEYBLOB, 0, NULL, &dwBlobLength))
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

	if (!CryptExportKey(g_ctxs[client_idx].hKey, NULL, PLAINTEXTKEYBLOB, 0, ppbKeyBlob, &dwBlobLength))
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
	if (g_ctxs[client_idx].hKey)
	{
		CryptDestroyKey(g_ctxs[client_idx].hKey);
	}
	if (ppbKeyBlob)
	{
		free(ppbKeyBlob);
	}

	return ppbKeyBlob;
}

PBYTE EncryptSessionKey(const BYTE *pbData, DWORD &dwDataLen, int client_idx)//BLOB with public key and it's length. On exit dwDataLen contains resulting length
{
	HCRYPTPROV hProv = NULL;
	HCRYPTKEY hKey = NULL;
	DWORD dwBlobLength = 0, dwPlainlen;
	PBYTE ppbKeyBlob = GenerateSessionKey(dwPlainlen, client_idx);

	if (!CryptAcquireContextA(&hProv, NULL, MS_STRONG_PROV_A, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_NEWKEYSET))
	{
		printf("Error acquiring context.\n");
		goto Cleanup;
	}

	if (!CryptImportKey(hProv, pbData, dwDataLen, 0, 0, &hKey))
	{
		printf("Error importing key.\n");
		goto Cleanup;
	}

	if (!CryptEncrypt(hKey, NULL, true, 0, NULL, &dwBlobLength, dwBlobLength))
	{
		printf("Error calculating space for encrypting session key with a public key. %lu\n", GetLastError());
		goto Cleanup;
	}
	ppbKeyBlob = (PBYTE)realloc(ppbKeyBlob, dwBlobLength);
	if (!CryptEncrypt(hKey, NULL, true, 0, ppbKeyBlob, &dwPlainlen, dwBlobLength))
	{
		printf("Error encrypting session key with a public key. %lu\n", GetLastError());
		goto Cleanup;
	}
	CryptDestroyKey(hKey);
	CryptReleaseContext(hProv, 0);
	dwDataLen = dwBlobLength;
	return ppbKeyBlob;

Cleanup:
	if (hProv)
	{
		CryptReleaseContext(hProv, 0);
	}
	if (hKey)
	{
		CryptDestroyKey(hKey);
	}
	if (ppbKeyBlob)
	{
		free(ppbKeyBlob);
	}

	return ppbKeyBlob;
}










// Функция стартует операцию чтения из сокета
void schedule_read(DWORD idx)
{
	WSABUF buf;
	buf.buf = g_ctxs[idx].buf_recv + g_ctxs[idx].sz_recv;
	buf.len = sizeof(g_ctxs[idx].buf_recv) - g_ctxs[idx].sz_recv;
	memset(&g_ctxs[idx].overlap_recv, 0, sizeof(OVERLAPPED));
	g_ctxs[idx].flags_recv = 0;
	WSARecv(g_ctxs[idx].socket, &buf, 1, NULL, &g_ctxs[idx].flags_recv, &g_ctxs[idx].overlap_recv, NULL);
}
// Функция стартует операцию отправки подготовленных данных в сокет
void schedule_write(DWORD idx)
{
	WSABUF buf;
	buf.buf = g_ctxs[idx].buf_send + g_ctxs[idx].sz_send;
	buf.len = g_ctxs[idx].sz_send_total - g_ctxs[idx].sz_send;
	memset(&g_ctxs[idx].overlap_send, 0, sizeof(OVERLAPPED));
	WSASend(g_ctxs[idx].socket, &buf, 1, NULL, 0, &g_ctxs[idx].overlap_send, NULL);
}
// Функция добавляет новое принятое подключение клиента
void add_accepted_connection()
{
	DWORD i; // Поиск места в массиве g_ctxs для вставки нового подключения
	for (i = 0; i < sizeof(g_ctxs) / sizeof(g_ctxs[0]); i++)
	{
		if (g_ctxs[i].socket == 0)
		{
			unsigned int ip = 0;
			struct sockaddr_in *local_addr = 0, *remote_addr = 0;
			int local_addr_sz, remote_addr_sz;
			GetAcceptExSockaddrs(g_ctxs[0].buf_recv, g_ctxs[0].sz_recv, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16, (struct sockaddr **) &local_addr, &local_addr_sz, (struct sockaddr **) &remote_addr, &remote_addr_sz);
			if (remote_addr)
			{
				ip = ntohl(remote_addr->sin_addr.s_addr);
			}
			printf(" connection %u created, remote IP: %u.%u.%u.%u\n", i, (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip) & 0xff);
			g_ctxs[i].socket = g_accepted_socket;
			g_ctxs[i].step = 0;
			g_ctxs[i].buf_send = (CHAR*)malloc(7000);
			// Связь сокета с портом IOCP, в качестве key используется индекс массива
			if (NULL == CreateIoCompletionPort((HANDLE)g_ctxs[i].socket, g_io_port, i, 0))
			{
				printf("CreateIoCompletionPort error: %x\n", GetLastError());
				return;
			}

			// Ожидание данных от сокета
			schedule_read(i);
			return;
		}
	}
	// Место не найдено => нет ресурсов для принятия соединения
	closesocket(g_accepted_socket);
	g_accepted_socket = 0;
}
// Функция стартует операцию приема соединения
void schedule_accept()
{
	// Создание сокета для принятия подключения (AcceptEx не создает сокетов)
	g_accepted_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	memset(&g_ctxs[0].overlap_recv, 0, sizeof(OVERLAPPED));
	// Принятие подключения.
	// Как только операция будет завершена - порт завершения пришлет уведомление. 
	// Размеры буферов должны быть на 16 байт больше размера адреса согласно документации разработчика ОС
	AcceptEx(g_ctxs[0].socket, g_accepted_socket, g_ctxs[0].buf_recv, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16, NULL, &g_ctxs[0].overlap_recv);
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

SOCKET startServer()
{
	struct sockaddr_in addr;
	// Создание сокета прослушивания
	SOCKET s = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	// Создание порта завершения
	g_io_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL == g_io_port)
	{
		printf("CreateIoCompletionPort error: %x\n", GetLastError());
		exit(0);
	}

	// Обнуление структуры данных для хранения входящих соединений
	memset(g_ctxs, 0, sizeof(g_ctxs));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9000);


	if (bind(s, (struct sockaddr*) &addr, sizeof(addr)) < 0 || listen(s, 1) < 0)
	{
		printf("error bind() or listen()\n");
		exit(0);
	}
	printf("Listening: %hu\n", ntohs(addr.sin_port));

	return s;
}

void freeReadBuf(int idx)
{
	g_ctxs[idx].sz_recv = 0;
	memset(g_ctxs[idx].buf_recv, 0, sizeof(g_ctxs[idx].buf_recv));
}

void freeWriteBuf(int idx)
{
	g_ctxs[idx].buf_send = (CHAR*)realloc(g_ctxs[idx].buf_send, 7000);
	g_ctxs[idx].sz_send = 0;
	g_ctxs[idx].sz_send_total = 0;
}

bool CheckAndAnswer(int idx)
{
	if (g_ctxs[idx].step == 0)
	{
		if (g_ctxs[idx].sz_recv < sizeof(DWORD))
		{
			return false;
		}
		memcpy((void*)&g_ctxs[idx].keyLen, g_ctxs[idx].buf_recv, sizeof(DWORD));
		freeReadBuf(idx);
		sprintf(g_ctxs[idx].buf_send, "ok");
		g_ctxs[idx].sz_send_total = strlen(g_ctxs[idx].buf_send);
		schedule_write(idx);
		g_ctxs[idx].step++;
	}
	else if (g_ctxs[idx].step == 1)
	{
		if (g_ctxs[idx].sz_recv < g_ctxs[idx].keyLen)
		{
			return false;
		}
		DWORD len = g_ctxs[idx].keyLen;
		g_ctxs[idx].keyBuf = EncryptSessionKey((const BYTE*)g_ctxs[idx].buf_recv, len, idx);
		g_ctxs[idx].keyLen = len;
		freeReadBuf(idx);
		memcpy(g_ctxs[idx].buf_send, (const void*)&len, sizeof(DWORD));
		g_ctxs[idx].sz_send_total = sizeof(DWORD);
		schedule_write(idx);
		g_ctxs[idx].step++;
	}
	else if (g_ctxs[idx].step == 2)
	{
		if (g_ctxs[idx].sz_recv < 2 || (g_ctxs[idx].buf_recv[0] != 'o' && g_ctxs[idx].buf_recv[1] != 'k'))
		{
			return false;
		}
		freeReadBuf(idx);
		memcpy(g_ctxs[idx].buf_send, g_ctxs[idx].keyBuf, g_ctxs[idx].keyLen);
		g_ctxs[idx].sz_send_total = g_ctxs[idx].keyLen;
		schedule_write(idx);
		g_ctxs[idx].step++;
		free(g_ctxs[idx].keyBuf);
		printf("\nSession key was successfully sent!\n");

	}
	else if (g_ctxs[idx].step == 3)
	{
		if (g_ctxs[idx].sz_recv < 16)
		{
			return false;
		}
		DWORD size = 16;
		int choice;
		char *choiceStr;
		if (g_ctxs[idx].OptionExtraInfo)
		{
			choice = g_ctxs[idx].OptionExtraInfo + 10;
		}
		else
		{
			choiceStr = DecryptData(g_ctxs[idx].buf_recv, size, idx);
			choice = *choiceStr;
		}
		if (choice == GetWinVer)
		{
			WinVer ver;
			char *res = NULL;
			GetWinVersion(ver);
			freeReadBuf(idx);
			size = sizeof(ver);
			res = EncryptData((char*)&ver, size, idx);
			memcpy(g_ctxs[idx].buf_send, res, size);
			g_ctxs[idx].sz_send_total = size;
			schedule_write(idx);
			free(res);
		}
		else if (choice == GetCurTime)
		{
			CurrentTime time;
			char *res = NULL;
			GetTime(time);
			freeReadBuf(idx);
			size = sizeof(time);
			res = EncryptData((char*)&time, size, idx);
			memcpy(g_ctxs[idx].buf_send, res, size);
			g_ctxs[idx].sz_send_total = size;
			schedule_write(idx);
			free(res);
		}
		else if (choice == GetWorkTime)
		{
			TimeSinceStart time;
			char *res = NULL;
			GetTimeSinceStart(time);
			freeReadBuf(idx);
			size = sizeof(time);
			res = EncryptData((char*)&time, size, idx);
			memcpy(g_ctxs[idx].buf_send, res, size);
			g_ctxs[idx].sz_send_total = size;
			schedule_write(idx);
			free(res);
		}
		else if (choice == GetMemInfo)
		{
			Memoryinfo mem;
			char *res = NULL;
			GetRamInfo(mem);
			freeReadBuf(idx);
			size = sizeof(mem);
			res = EncryptData((char*)&mem, size, idx);
			memcpy(g_ctxs[idx].buf_send, res, size);
			g_ctxs[idx].sz_send_total = size;
			schedule_write(idx);
			free(res);
		}
		else if (choice == GetDiskInfo)
		{
			diskInfo *info;
			DWORD sizeTmp = sizeof(size);
			int count;
			char *res = NULL;
			char *sizeEnc = NULL;
			info = GetDisksInfo(count);
			freeReadBuf(idx);
			size = sizeof(diskInfo)*count;
			res = EncryptData((char*)info, size, idx);
			sizeEnc = EncryptData((char*)&size, sizeTmp, idx);
			memcpy(g_ctxs[idx].buf_send, sizeEnc, sizeTmp);
			memcpy(g_ctxs[idx].buf_send + sizeTmp, res, size);
			g_ctxs[idx].sz_send_total = size + sizeTmp;
			schedule_write(idx);
			free(res);
			free(info);
		}
		else if (choice == GetDiskSpace)
		{
			diskFreeSpace *info;
			DWORD sizeTmp = sizeof(size);
			int count;
			char *res = NULL;
			char *sizeEnc = NULL;
			info = GetLocalDisksFreeSpace(count);
			freeReadBuf(idx);
			size = sizeof(diskFreeSpace)*count;
			res = EncryptData((char*)info, size, idx);
			sizeEnc = EncryptData((char*)&size, sizeTmp, idx);
			memcpy(g_ctxs[idx].buf_send, sizeEnc, sizeTmp);
			memcpy(g_ctxs[idx].buf_send + sizeTmp, res, size);
			g_ctxs[idx].sz_send_total = size + sizeTmp;
			schedule_write(idx);
			free(res);
			free(info);
			free(sizeEnc);
		}
		else if (choice == GetOwner)
		{
			freeReadBuf(idx);
			g_ctxs[idx].OptionExtraInfo = GetOwner;
			return false;
		}
		else if (choice == GetOwner + 10)
		{
			g_ctxs[idx].OptionExtraInfo = 0;
			size = 272;
			char *Owner;
			char *res = NULL;
			res = DecryptData(g_ctxs[idx].buf_recv, size, idx);
			char type = res[size - 1];
			Owner = GetObjectOwner(res, (SE_OBJECT_TYPE)type);
			freeReadBuf(idx);
			char tmp[MAX_PATH + 1] = { 0 };
			if (!Owner)
			{
				strcpy(tmp, "FAIL ON SERVER!");
				res = EncryptData(tmp, size, idx);
				memcpy(g_ctxs[idx].buf_send, res, size);
				g_ctxs[idx].sz_send_total = size;
				schedule_write(idx);
				free(res);
				return true;
			}
			strcpy(tmp, Owner);
			free(Owner);
			Owner = tmp;
			size = sizeof(tmp);
			res = EncryptData(Owner, size, idx);
			memcpy(g_ctxs[idx].buf_send, res, size);
			g_ctxs[idx].sz_send_total = size;
			schedule_write(idx);
			free(res);
		}
		else if (choice == GetAccesInfo)
		{
			freeReadBuf(idx);
			g_ctxs[idx].OptionExtraInfo = GetAccesInfo;
			return false;
		}
		else if (choice == GetAccesInfo + 10)
		{
			g_ctxs[idx].OptionExtraInfo = 0;
			size = 272;
			DWORD sizeTmp = sizeof(size);
			char *pack;
			char *sizeEnc = NULL;
			char *res = NULL;
			res = DecryptData(g_ctxs[idx].buf_recv, size, idx);
			char type = res[size - 1];
			pack = GetObjectAccessRights(res, (SE_OBJECT_TYPE)type, size);
			freeReadBuf(idx);
			if (!pack)
			{
				size = 4;
				res = EncryptData((char*)"FAIL", size, idx);
				memcpy(g_ctxs[idx].buf_send, res, size);
				g_ctxs[idx].sz_send_total = size;
				schedule_write(idx);
				free(res);
				return true;
			}

			res = EncryptData(pack, size, idx);
			if (size + 16 > 7000)
			{
				g_ctxs[idx].buf_send = (CHAR*)realloc(g_ctxs[idx].buf_send, size);
			}
			sizeEnc = EncryptData((char*)&size, sizeTmp, idx);
			memcpy(g_ctxs[idx].buf_send, sizeEnc, sizeTmp);
			memcpy(g_ctxs[idx].buf_send + sizeTmp, res, size);
			g_ctxs[idx].sz_send_total = size + sizeTmp;
			schedule_write(idx);
			free(res);
			free(pack);
		}
	}
	return true;
}

void io_serv()
{
	SOCKET s = startServer();

	// Присоединение существующего сокета s к порту io_port.
	// В качестве ключа для прослушивающего сокета используется 0
	if (NULL == CreateIoCompletionPort((HANDLE)s, g_io_port, 0, 0))
	{
		printf("CreateIoCompletionPort error: %x\n", GetLastError());
		return;
	}

	g_ctxs[0].socket = s;
	// Старт операции принятия подключения.
	schedule_accept();
	// Бесконечный цикл принятия событий о завершенных операциях
	while (1)
	{
		DWORD transferred;
		ULONG_PTR key;
		OVERLAPPED* lp_overlap;
		// Ожидание событий в течение 1 секунды
		BOOL b = GetQueuedCompletionStatus(g_io_port, &transferred, &key, &lp_overlap, 1000);
		if (b)
		{
			// Поступило уведомление о завершении операции
			if (key == 0) // ключ 0 - для прослушивающего сокета
			{
				g_ctxs[0].sz_recv += transferred;
				// Принятие подключения и начало принятия следующего
				add_accepted_connection();
				schedule_accept();
			}
			else
			{
				// Иначе поступило событие по завершению операции от клиента. // Ключ key - индекс в массиве g_ctxs
				if (&g_ctxs[key].overlap_recv == lp_overlap)
				{
					int len;
					// Данные приняты:
					if (transferred == 0)
					{
						// Соединение разорвано
						CancelIo((HANDLE)g_ctxs[key].socket);
						PostQueuedCompletionStatus(g_io_port, 0, key, &g_ctxs[key].overlap_cancel);
						continue;
					}
					g_ctxs[key].sz_recv += transferred;
					if (!CheckAndAnswer(key))
					{
						// Иначе - ждем данные дальше
						schedule_read(key);
					}
				}
				else if (&g_ctxs[key].overlap_send == lp_overlap)
				{
					// Данные отправлены
					g_ctxs[key].sz_send += transferred;
					if (g_ctxs[key].sz_send < g_ctxs[key].sz_send_total && transferred > 0)
					{
						// Если данные отправлены не полностью - продолжить отправлять
						schedule_write(key);
					}
					else
					{
						freeWriteBuf(key);
						schedule_read(key);
						continue;
					}
				}
				else if (&g_ctxs[key].overlap_cancel == lp_overlap)
				{
					// Все коммуникации завершены, сокет может быть закрыт
					closesocket(g_ctxs[key].socket); memset(&g_ctxs[key], 0, sizeof(g_ctxs[key]));
					printf(" connection %u closed\n", key);
				}
			}
		}
		else
		{
			// Ни одной операции не было завершено в течение заданного времени, программа может
			// выполнить какие-либо другие действия
			// ...
		}
	}
}



int main()
{
	setlocale(LC_ALL, "Russian");
	initWSA();
	io_serv();
	system("pause");
	return 0;
}