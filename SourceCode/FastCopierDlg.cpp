#define _CRT_SECURE_NO_WARNINGS 0
#define _CRT_NON_CONFORMING_SWPRINTFS 0

#include "FastCopierDlg.h"
#include <Windowsx.h>
#include <Commdlg.h>
#include <Shlobj.h>
#include <stdio.h>
#include  <fcntl.h>
#include <iostream>

struct FilePart
{
	int id;
	WCHAR* srcFilePath;
	WCHAR* dstFilePath;
	long seekPos;
	long copiedSize;
	long partFileSize;
	HWND hListView;
	FILE* pFileOut;
	FILE* pFileIn;
	char* buff;
};

extern HWND hGlobalMainWnd;
extern HINSTANCE hInst;
HANDLE* hGlobalThreadCopyFilePart; //Global thread copy
int nGlobalThreadNum; //Global thread copy part num
HANDLE hGlobalThreadOnStart; //Global thread OnStart
DWORD* globalThreadID;
FilePart** globalFileParts;

void OnCreate(HWND hDlg);
void OnBrowseCopySource(HWND hDlg);
void OnBrowseCopyDestinate(HWND hDlg);
void OnStart(HWND hDlg);
void OnStop(HWND hDlg);
void OnAbout(HWND hDlg);
INT_PTR CALLBACK OnExit(HWND hDlg);

//Threads
void ThreadOnStart(LPVOID lpParam);
void ThreadCopyPart(LPVOID lpParam);
void ThreadOnStop(LPVOID lpParam);

//Controls
void disableAllButtonControls(HWND hDlg);
void setStatus(HWND hDlg, WCHAR* statement);
void changeStateReady(HWND hDlg);
void changeStateBusy(HWND hDlg);

//Helper functions
WCHAR* formatAmount(long nBytes);
WCHAR* getFileName(WCHAR* filePathName);
long getFileSize(WCHAR* fileName);
void joinParts(HWND hDlg, FilePart** fileParts, int num, WCHAR* dstFilePath);

//Memory
void releaseGlobalMemory();


INT_PTR CALLBACK	DlgCopyProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		OnCreate(hDlg);
		return (INT_PTR)TRUE;
	case WM_CTLCOLORSTATIC:
		switch (GetDlgCtrlID((HWND)lParam))
		{
		case IDC_STATIC_STATUS:
			SetBkColor((HDC)wParam, COLORREF(GetSysColor(COLOR_3DFACE)));
			SetTextColor((HDC)wParam, RGB(0, 0, 255));
			return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
		default:
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			return OnExit(hDlg);
		case IDC_BUTTON_START:
			OnStart(hDlg);
			break;
		case IDC_BUTTON_STOP:
			OnStop(hDlg);
			break;
		case IDC_BUTTON_ABOUT:
			OnAbout(hDlg);
			break;
		case IDC_BUTTON_BROWSE_COPY_SOURCE:
			OnBrowseCopySource(hDlg);
			break;
		case IDC_BUTTON_BROWSE_COPY_DESTINATE:
			OnBrowseCopyDestinate(hDlg);
			break;			
		default:
			break;
		}
	}
	return (INT_PTR)FALSE;
}


void OnCreate(HWND hDlg)
{
	// Init Thread Num ComboBox
	HWND cbThreadNum = GetDlgItem(hDlg, IDC_COMBO_THREAD_NUM);
	for (int i = 1; i <= 5; ++i)
	{
		WCHAR buff[10];
		wsprintf(buff, L"%d", i);
		ComboBox_AddString(cbThreadNum, buff);
	}
	ComboBox_SetCurSel(cbThreadNum, 4);	

	// Init Copy Status ListView
	HWND lvStatus = GetDlgItem(hDlg, IDC_LISTVIEW_COPYSTATUS);
	ListView_SetExtendedListViewStyle(lvStatus, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP);
	LVCOLUMN lvCol;
	lvCol.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvCol.fmt = LVCFMT_LEFT;

	lvCol.iSubItem = 0;
	lvCol.cx = 30;
	lvCol.pszText = L"N.";
	ListView_InsertColumn(lvStatus, 0, &lvCol);
	ListView_SetColumnWidth(lvStatus, 2, 30);

	lvCol.iSubItem = 1;
	lvCol.cx = 100;
	lvCol.pszText = L"Copied";
	ListView_InsertColumn(lvStatus, 1, &lvCol);
	ListView_SetColumnWidth(lvStatus, 2, 100);

	lvCol.iSubItem = 2;
	lvCol.cx = 200;
	lvCol.pszText = L"Status";
	ListView_InsertColumn(lvStatus, 2, &lvCol);
	ListView_SetColumnWidth(lvStatus, 2, 200);

	// Button Controls Init
	changeStateReady(hDlg);
}

void OnBrowseCopySource(HWND hDlg)
{
	OPENFILENAME ofn;
	WCHAR szFilter[] = L"All files\0*.*\0";
	WCHAR szPath[MAX_PATH];
	szPath[0] = '\0';

	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hDlg;
	ofn.lpstrFilter = szFilter;
	ofn.lpstrFile = szPath;
	ofn.nMaxFile = 128;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileName(&ofn))
	{
		SetWindowText(GetDlgItem(hDlg, IDC_EDIT_COPY_SOUCE), szPath);
	}
}

void OnBrowseCopyDestinate(HWND hDlg)
{
	TCHAR szDir[MAX_PATH];
	szDir[0] = '\0';
	BROWSEINFO bInfo;
	bInfo.hwndOwner = hDlg;
	bInfo.pidlRoot = NULL;
	bInfo.pszDisplayName = szDir; // Address of a buffer to receive the display name of the folder selected by the user
	bInfo.lpszTitle = L"Save to"; // Title of the dialog
	bInfo.ulFlags = 0;
	bInfo.lpfn = NULL;
	bInfo.lParam = 0;
	bInfo.iImage = -1;

	LPITEMIDLIST pidl;
	pidl = SHBrowseForFolder(&bInfo);

	if (pidl != NULL)
	{
		//get the name of the folder and put it in path
		SHGetPathFromIDList(pidl, szDir);

		//Set to edit
		SetWindowText(GetDlgItem(hDlg, IDC_EDIT_COPY_DESTINATE), szDir);

		//free memory used
		IMalloc * imalloc = 0;
		if (SUCCEEDED(SHGetMalloc(&imalloc)))
		{
			imalloc->Free(pidl);
			imalloc->Release();
		}
	}
}

void OnStart(HWND hDlg)
{
	hGlobalThreadOnStart = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadOnStart, (LPVOID)hDlg, 0, NULL);
}

void OnStop(HWND hDlg)
{
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadOnStop, (LPVOID)hDlg, 0, NULL);
}

void OnAbout(HWND hDlg)
{
	MessageBox(hDlg, L"Author: Mai Thanh Hiep\r\nOrganize: University of Science\r\nEmail: hiepxuan2008@gmail.com", L"About", MB_OK);
}

INT_PTR CALLBACK OnExit(HWND hDlg)
{
	SendMessage(hGlobalMainWnd, WM_CLOSE, 0, 0);
	return (INT_PTR)TRUE;
}

void setStatus(HWND hDlg, WCHAR* statement)
{
	SetWindowText(GetDlgItem(hDlg, IDC_STATIC_STATUS), statement);
}

void changeStateReady(HWND hDlg)
{
	disableAllButtonControls(hDlg);
	Button_Enable(GetDlgItem(hDlg, IDC_BUTTON_START), TRUE);
}

void changeStateBusy(HWND hDlg)
{
	disableAllButtonControls(hDlg);
	//Button_Enable(GetDlgItem(hDlg, IDC_BUTTON_STOP), TRUE);
}

void ThreadOnStart(LPVOID lpParam)
{
	HWND hDlg = (HWND)lpParam;
	WCHAR SourcePath[MAX_PATH];
	WCHAR DestPath[MAX_PATH];

	//Get path
	GetDlgItemText(hDlg, IDC_EDIT_COPY_SOUCE, SourcePath, MAX_PATH);
	if (wcscmp(SourcePath, L"") == 0)
	{
		MessageBox(hDlg, L"Please choose file need to copy!", L"Warning", MB_OK);
		return;
	}

	GetDlgItemText(hDlg, IDC_EDIT_COPY_DESTINATE, DestPath, MAX_PATH);
	if (wcscmp(DestPath, L"") == 0)
	{
		MessageBox(hDlg, L"Please choose destinate folder!", L"Warning", MB_OK);
		return;
	}
	changeStateBusy(hDlg);

	//Get number of thread
	WCHAR buff[10];
	int nThreadNum = 0;
	ComboBox_GetText(GetDlgItem(hDlg, IDC_COMBO_THREAD_NUM), buff, 10);
	nThreadNum = _wtoi(buff);

	//Status
	long nFileSize = getFileSize(SourcePath);

	SetDlgItemText(hDlg, IDC_STATIC_FILESIZE, formatAmount(nFileSize));
	HWND lvStatus = GetDlgItem(hDlg, IDC_LISTVIEW_COPYSTATUS);
	ListView_DeleteAllItems(lvStatus);
	LVITEM lvItem;
	lvItem.mask = LVIF_IMAGE | LVIF_STATE;
	lvItem.state = 0;
	lvItem.stateMask = 0;
	for (int j = 0; j < nThreadNum; ++j)
	{
		lvItem.iItem = j;
		lvItem.iImage = 1;
		lvItem.iSubItem = 0;
		ListView_InsertItem(lvStatus, &lvItem);
		WCHAR temp[10];
		wsprintf(temp, L"%d", j + 1);
		ListView_SetItemText(lvStatus, j, 0, temp);
		ListView_SetItemText(lvStatus, j, 1, L"");
		ListView_SetItemText(lvStatus, j, 2, L"");
	}

	//
	WCHAR dstFilePath[MAX_PATH];
	WCHAR* fileName = getFileName(SourcePath);
	wsprintf(dstFilePath, L"%s/%s", DestPath, fileName);

	//Create Thread
	setStatus(hDlg, L"Copying...");
	hGlobalThreadCopyFilePart = new HANDLE[nThreadNum];
	nGlobalThreadNum = nThreadNum;
	long curSeekPos = 0;

	globalThreadID = new DWORD[nThreadNum];
	globalFileParts = new FilePart*[nThreadNum];
	for (int i = 0; i < nThreadNum; ++i)
	{
		FilePart*& filePart = globalFileParts[i];
		filePart = new FilePart();
		filePart->id = i;
		filePart->srcFilePath = SourcePath;
		filePart->dstFilePath = DestPath;
		filePart->copiedSize = 0;
		filePart->partFileSize = nFileSize / nThreadNum;
		if (i == nThreadNum - 1)
			filePart->partFileSize += nFileSize % nThreadNum;
		filePart->seekPos = curSeekPos;
		filePart->hListView = GetDlgItem(hDlg, IDC_LISTVIEW_COPYSTATUS);

		WCHAR* text = new WCHAR[100];
		wsprintf(text, L"%s/%s.%d", filePart->dstFilePath, fileName, filePart->id + 1);
		filePart->dstFilePath = text;

		curSeekPos += nFileSize / nThreadNum;

		globalThreadID[i] = i;
		hGlobalThreadCopyFilePart[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadCopyPart, (LPVOID)filePart, 0, &globalThreadID[i]);
	}

	//Waiting for sync
	WaitForMultipleObjects(nThreadNum, hGlobalThreadCopyFilePart, TRUE, INFINITE);

	//Join Parts
	joinParts(hDlg, globalFileParts, nThreadNum, dstFilePath);

	releaseGlobalMemory();

	setStatus(hDlg, L"Completed!");
	changeStateReady(hDlg);
}

void ThreadOnStop(LPVOID lpParam)
{
	HWND hDlg = (HWND)lpParam;

	//Stop Copy file parts
	for (int i = 0; i < nGlobalThreadNum; ++i)
		TerminateThread(hGlobalThreadCopyFilePart[i], 0);

	//Stop OnStart Thread
	TerminateThread(hGlobalThreadOnStart, 0);

	//Set stop state
	for (int i = 0; i < nGlobalThreadNum; ++i)
		ListView_SetItemText(GetDlgItem(hDlg, IDC_LISTVIEW_COPYSTATUS), i, 2, L"Stoped!");

	//Clear globalFileParts data
	for (int i = 0; i < nGlobalThreadNum; ++i)
	{
		if (globalFileParts[i]->pFileOut != NULL)
			fclose(globalFileParts[i]->pFileOut);
		if (globalFileParts[i]->pFileIn != NULL)
			fclose(globalFileParts[i]->pFileIn);
		
		delete globalFileParts[i]->buff;
	}

	releaseGlobalMemory();
	setStatus(hDlg, L"Stoped");
	changeStateReady(hDlg);
}

WCHAR* formatAmount(long nBytes)
{
	//Memory leak??
	WCHAR* buff = new WCHAR[100];
	if (nBytes < 1024)
		swprintf(buff, L"%d bytes", nBytes);
	else if (nBytes < 1024 * 1024)
		swprintf(buff, L"%.2f KB", (float)nBytes / (1024));
	else if (nBytes < 1024 * 1024 * 1024)
		swprintf(buff, L"%.2f MB", (float)nBytes / (1024 * 1024));
	else
		swprintf(buff, L"%.2f GB", (float)nBytes / (1024 * 1024 * 1024));

	return buff;
}

void ThreadCopyPart(LPVOID lpParam)
{
	FilePart* filePart = (FilePart*)lpParam;

	long BUFF_SIZE = 1000000;
	char* buff = filePart->buff = new char[BUFF_SIZE];
	int bytes = 0;
	WCHAR text[100];

	//Copy
	FILE* pFile = filePart->pFileIn = _wfopen(filePart->srcFilePath, L"rb");
	FILE* pFileOut = filePart->pFileOut = _wfopen(filePart->dstFilePath, L"wb");

	fseek(pFile, (long)filePart->seekPos, SEEK_SET);
	long leftBytes = filePart->partFileSize;
	long nBuffSize = leftBytes < BUFF_SIZE ? leftBytes : BUFF_SIZE;

	ListView_SetItemText(filePart->hListView, filePart->id, 2, L"Copying...");
	while ((leftBytes > 0) && ((bytes = fread(buff, sizeof(char), nBuffSize, pFile)) > 0))
	{
		fwrite(buff, sizeof(char), bytes, pFileOut);
		leftBytes -= bytes;
		nBuffSize = leftBytes < BUFF_SIZE ? leftBytes : BUFF_SIZE;

		swprintf(text, L"%s (%.2f %%)", formatAmount(filePart->partFileSize - leftBytes), (1.0 - leftBytes / (float)filePart->partFileSize) * 100);
		ListView_SetItemText(filePart->hListView, filePart->id, 1, text);
	}
	ListView_SetItemText(filePart->hListView, filePart->id, 2, L"Completed!");

	//Close files && Release memory
	delete buff;
	fclose(pFile);
	fclose(pFileOut);
}

void joinParts(HWND hDlg, FilePart** fileParts, int num, WCHAR* dstFilePath)
{
	setStatus(hDlg, L"Joinning file parts...");

	FILE* pFileOut = _wfopen(dstFilePath, L"wb");
	long nBuffSize = 1000000;
	char* buff = new char[nBuffSize];
	int bytes;

	for (int i = 0; i < num; ++i)
	{
		FilePart* filePart = fileParts[i];
		ListView_SetItemText(filePart->hListView, i, 2, L"Waiting for join...");
	}

	for (int i = 0; i < num; ++i)
	{
		FilePart* filePart = fileParts[i];
		FILE* pFile = _wfopen(filePart->dstFilePath, L"rb");
		ListView_SetItemText(filePart->hListView, i, 2, L"Joining...");

		while ((bytes = fread(buff, sizeof(char), nBuffSize, pFile)) > 0)
		{
			fwrite(buff, sizeof(char), bytes, pFileOut);
		}

		ListView_SetItemText(filePart->hListView, i, 2, L"Completed!");
		fclose(pFile);

		DeleteFile(fileParts[i]->dstFilePath); //Delete file part
	}

	delete buff;
	fclose(pFileOut);
}

long getFileSize(WCHAR* fileName)
{
	long ans = 0;
	FILE* pFile = _wfopen(fileName, L"rb");
	fseek(pFile, 0L, SEEK_END);
	ans = ftell(pFile);
	fclose(pFile);

	return ans;
}

WCHAR* getFileName(WCHAR* filePathName)
{
	int n = wcslen(filePathName) - 1;
	while (filePathName[n] != L'\\' && filePathName[n] != L'/')
		n--;

	WCHAR* res = new WCHAR[wcslen(filePathName) - n];
	int j = 0;
	for (size_t i = n + 1; i < wcslen(filePathName); ++i, ++j)
		res[j] = filePathName[i];
	res[j] = '\0';
	return res;
}

void disableAllButtonControls(HWND hDlg)
{
	Button_Enable(GetDlgItem(hDlg, IDC_BUTTON_START), FALSE);
	Button_Enable(GetDlgItem(hDlg, IDC_BUTTON_STOP), FALSE);
}

void releaseGlobalMemory()
{
	//Release memory
	for (int i = 0; i < nGlobalThreadNum; ++i)
		delete globalFileParts[i];
	delete[] globalFileParts;
	delete[] globalThreadID;

	globalThreadID = NULL;
	globalFileParts = NULL;
}