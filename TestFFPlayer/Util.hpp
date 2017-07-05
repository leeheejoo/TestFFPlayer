#pragma once

#include <windows.h>
//UTILL
class Util
{
public:

	static char* ANSIToUTF8(const char * pszCode){
		int     nLength, nLength2;
		BSTR    bstrWide;
		char*   pszUTFCode = NULL;

		nLength = MultiByteToWideChar(CP_ACP, 0, pszCode, strlen(pszCode), NULL, NULL);
		bstrWide = SysAllocStringLen(NULL, nLength);
		MultiByteToWideChar(CP_ACP, 0, pszCode, strlen(pszCode), bstrWide, nLength);

		nLength2 = WideCharToMultiByte(CP_UTF8, 0, bstrWide, -1, pszUTFCode, 0, NULL, NULL);
		pszUTFCode = (char*)malloc(nLength2 + 1);
		WideCharToMultiByte(CP_UTF8, 0, bstrWide, -1, pszUTFCode, nLength2, NULL, NULL);
		SysFreeString(bstrWide);

		return pszUTFCode;
	}


	static char* UTF8ToANSI(const char *pszCode){
		BSTR    bstrWide;
		char*   pszAnsi;
		int     nLength;

		nLength = MultiByteToWideChar(CP_UTF8, 0, pszCode, strlen(pszCode) + 1, NULL, NULL);
		bstrWide = SysAllocStringLen(NULL, nLength);

		MultiByteToWideChar(CP_UTF8, 0, pszCode, strlen(pszCode) + 1, bstrWide, nLength);

		nLength = WideCharToMultiByte(CP_ACP, 0, bstrWide, -1, NULL, 0, NULL, NULL);
		pszAnsi = new char[nLength];

		WideCharToMultiByte(CP_ACP, 0, bstrWide, -1, pszAnsi, nLength, NULL, NULL);
		SysFreeString(bstrWide);

		return pszAnsi;
	}

};

