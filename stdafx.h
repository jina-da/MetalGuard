#pragma once

#ifndef _SECURE_ATL
#define _SECURE_ATL 1
#endif

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include "targetver.h"

// ── 반드시 이 순서대로 ──────────────────────────────────────
// 1. Winsock2를 가장 먼저 (winsock.h 충돌 방지)
#include <winsock2.h>
#include <ws2tcpip.h>

// 2. Pylon (MFC보다 먼저)
#include <pylon/PylonIncludes.h>
#include <pylon/PylonGUI.h>


#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

// 3. MFC (Pylon 다음)
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#define _AFX_ALL_WARNINGS
#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
// ────────────────────────────────────────────────────────────

#ifndef _AFX_NO_OLE_SUPPORT
#include <afxdtctl.h>
#endif
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>
#endif

#ifdef _UNICODE
#if defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif